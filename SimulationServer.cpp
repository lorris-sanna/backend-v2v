#include "SimulationServer.h"
#include <QDebug>
#include <QEventLoop>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonParseError>
#include <QJsonValue>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>
#include <QTextStream>
#include <iostream>
#include <algorithm>
#include <cmath>

namespace {
bool fetchOverpassOsm(double minLon, double minLat, double maxLon, double maxLat, QString& osmXml, QString& errorMsg)
{
    const QString overpassQuery = QStringLiteral(
        "[out:xml][timeout:60];(way[\"highway\"](%1,%2,%3,%4);>;);out body;"
    ).arg(minLat, 0, 'f', 7)
     .arg(minLon, 0, 'f', 7)
     .arg(maxLat, 0, 'f', 7)
     .arg(maxLon, 0, 'f', 7);

    const QStringList endpoints = {
        QStringLiteral("https://overpass-api.de/api/interpreter"),
        QStringLiteral("https://lz4.overpass-api.de/api/interpreter"),
        QStringLiteral("https://overpass.openstreetmap.ru/cgi/interpreter")
    };

    QNetworkAccessManager manager;
    for (const QString& endpoint : endpoints) {
        QNetworkRequest request;
        request.setUrl(QUrl(endpoint));
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
        request.setRawHeader("User-Agent", "V2V-Simulation-Frontend/1.0");

        QUrlQuery formBody;
        formBody.addQueryItem(QStringLiteral("data"), overpassQuery);
        const QByteArray body = formBody.toString(QUrl::FullyEncoded).toUtf8();

        QNetworkReply* reply = manager.post(request, body);
        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();

        if (reply->error() != QNetworkReply::NoError) {
            const QString err = QStringLiteral("Endpoint %1 returned error: %2").arg(endpoint, reply->errorString());
            errorMsg = err;
            reply->deleteLater();
            continue;
        }

        const QVariant statusVar = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        if (statusVar.isValid()) {
            const int statusCode = statusVar.toInt();
            if (statusCode < 200 || statusCode >= 300) {
                const QString err = QStringLiteral("Endpoint %1 HTTP status %2").arg(endpoint).arg(statusCode);
                errorMsg = err;
                reply->deleteLater();
                continue;
            }
        }

        osmXml = QString::fromUtf8(reply->readAll());
        reply->deleteLater();
        if (osmXml.isEmpty()) {
            errorMsg = QStringLiteral("Endpoint %1 returned empty response").arg(endpoint);
            continue;
        }
        return true;
    }

    if (errorMsg.isEmpty()) errorMsg = QStringLiteral("Echec inconnu lors de la requete Overpass");
    return false;
}
}

SimulationServer::SimulationServer(quint16 port, QObject* parent)
    : QObject(parent),
      webSocketServer(new QWebSocketServer(QStringLiteral("SimulationServer"), QWebSocketServer::NonSecureMode, this)),
      simulationTimer(new QTimer(this))
{
    //demarrage du serveur WebSocket
    if (!webSocketServer->listen(QHostAddress::Any, port)) {
        qWarning() << "Erreur: impossible de demarrer le serveur WebSocket sur le port" << port;
        return;
    }

    qWarning() << "Serveur WebSocket demarre sur le port" << port;

    //connexion des signaux
    connect(webSocketServer, &QWebSocketServer::newConnection, this, &SimulationServer::onNewConnection);
    connect(simulationTimer, &QTimer::timeout, this, &SimulationServer::onSimulationTick);

    simulationTimer->start(16);
}

SimulationServer::~SimulationServer()
{
    for (QWebSocket* client : clients) {
        if (client) {
            client->close();
        }
    }

    webSocketServer->close();
}

void SimulationServer::chargerGrapheEtVoitures(const std::string& pathOSM, int nbVoitures)
{
    graphe.clear();
    voitures.clear();
    frameCount = 0;

    if (!graphe.chargerDepuisOSM(pathOSM)) {
        qWarning() << "Erreur: impossible de charger le fichier OSM";
        QJsonObject errObj;
        errObj["type"] = "error";
        errObj["message"] = QStringLiteral("Echec du parsing OSM sur le serveur pour le fichier: %1").arg(QString::fromStdString(pathOSM));
        QJsonDocument doc(errObj);
        const QString errText = QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
        for (QWebSocket* c : clients) if (c && c->isValid()) c->sendTextMessage(errText);
        return;
    }

    //creation des voitures en les repartissant sur les aretes
    const auto& aretes = graphe.getAretes();
    if (aretes.empty()) return;

    //calcul longueurs des aretes
    std::vector<double> longueurs;
    longueurs.reserve(aretes.size());
    double totalLongueur = 0.0;
    for (const auto& e : aretes) {
        double dx = e.first->getX() - e.second->getX();
        double dy = e.first->getY() - e.second->getY();
        double L = std::sqrt(dx*dx + dy*dy);
        longueurs.push_back(L);
        totalLongueur += L;
    }

    if (totalLongueur <= 0.0) {
        //repartition par noeuds si pas de longueur calculable
        const auto& noeuds = graphe.getNoeuds();
        int nbNoeuds = noeuds.size();
        if (nbNoeuds == 0) return;
        for (int i = 0; i < nbVoitures; ++i) {
            Noeud* noeudDepart = noeuds[i % nbNoeuds];
            voitures.emplace_back(i, noeudDepart, 0.4);
        }
    } else {
        //allocation entiere basee sur parties fractionnaires
        size_t m = aretes.size();
        std::vector<double> ideals(m);
        std::vector<int> counts(m, 0);
        double accum = 0.0;
        for (size_t i = 0; i < m; ++i) {
            ideals[i] = (longueurs[i] / totalLongueur) * nbVoitures;
            counts[i] = static_cast<int>(std::floor(ideals[i]));
            accum += counts[i];
        }

        int remaining = nbVoitures - static_cast<int>(accum);
        //tri des indices par partie fractionnaire decroissante
        std::vector<size_t> idx(m);
        for (size_t i = 0; i < m; ++i) idx[i] = i;
        std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) {
            return (ideals[a] - std::floor(ideals[a])) > (ideals[b] - std::floor(ideals[b]));
        });
        for (int k = 0; k < remaining; ++k) {
            counts[idx[k % m]] += 1;
        }

        //creation des voitures le long de chaque arete
        int idCounter = 0;
        for (size_t ei = 0; ei < m; ++ei) {
            int c = counts[ei];
            if (c <= 0) continue;
            Noeud* a = aretes[ei].first;
            Noeud* b = aretes[ei].second;
            double ax = a->getX();
            double ay = a->getY();
            double bx = b->getX();
            double by = b->getY();

            for (int k = 0; k < c; ++k) {
                double t = (static_cast<double>(k) + 1.0) / (static_cast<double>(c) + 1.0); //pour eviter les extremites
                double initX = ax + t * (bx - ax);
                double initY = ay + t * (by - ay);
                //creation de voiture orientée de a vers b
                voitures.emplace_back(idCounter++, a, b, initX, initY, 0.4);
            }
        }
    }

    qWarning() << "Simulation chargee avec" << nbVoitures << "voitures et"
               << graphe.getAretes().size() << "aretes";
}

bool SimulationServer::chargerGrapheDepuisBbox(double minLon, double minLat, double maxLon, double maxLat, int nbVoitures, QString& errorMsg)
{
    QString osmXml;
    if (!fetchOverpassOsm(minLon, minLat, maxLon, maxLat, osmXml, errorMsg)) {
        return false;
    }

    const QDir tempDir(QDir::tempPath());
    const QString tempPath = tempDir.filePath(
        QStringLiteral("frontend_bbox_map_%1.osm").arg(QDateTime::currentMSecsSinceEpoch())
    );

    QFile file(tempPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream out(&file);
    out << osmXml;
    file.close();

    QJsonObject infoMsg;
    infoMsg["type"] = "info";
    infoMsg["message"] = QStringLiteral("OSM reçu et sauvegardé. Démarrage du chargement du graphe (cela peut prendre plusieurs secondes)...");
    QJsonDocument infoDoc(infoMsg);
    const QString infoText = QString::fromUtf8(infoDoc.toJson(QJsonDocument::Compact));
    for (QWebSocket* c : clients) {
        if (c && c->isValid()) c->sendTextMessage(infoText);
    }

    chargerGrapheEtVoitures(tempPath.toStdString(), nbVoitures);

    if (graphe.getAretes().empty()) {
        QJsonObject errObj;
        errObj["type"] = "error";
        errObj["message"] = QStringLiteral("Le graphe charge est vide. Verifiez la bbox ou reduisez la zone.");
        QJsonDocument doc(errObj);
        const QString errText = QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
        for (QWebSocket* c : clients) if (c && c->isValid()) c->sendTextMessage(errText);
        return false;
    }

    QJsonObject loadedMsg;
    loadedMsg["type"] = "loaded";
    loadedMsg["nbVoitures"] = getNombreVoitures();
    loadedMsg["nbAretes"] = getNombreAretes();
    QJsonDocument loadedDoc(loadedMsg);
    const QString loadedText = QString::fromUtf8(loadedDoc.toJson(QJsonDocument::Compact));
    for (QWebSocket* c : clients) {
        if (c && c->isValid()) c->sendTextMessage(loadedText);
    }
    return true;
}

void SimulationServer::onNewConnection()
{
    while (QWebSocket* socket = webSocketServer->nextPendingConnection()) {
        qWarning() << "Nouveau client WebSocket connecte";

        connect(socket, &QWebSocket::textMessageReceived, this, &SimulationServer::onTextMessageReceived);
        connect(socket, &QWebSocket::disconnected, this, &SimulationServer::onClientDisconnected);

        clients.append(socket);
        socket->sendTextMessage(generateJsonResponse());
    }
}

void SimulationServer::onClientDisconnected()
{
    QWebSocket* socket = qobject_cast<QWebSocket*>(sender());
    if (socket) {
        clients.removeAll(socket);
        socket->deleteLater();
        qWarning() << "Client déconnecte. Clients actifs:" << clients.size();
    }
}

void SimulationServer::onTextMessageReceived(const QString& message)
{
    QWebSocket* socket = qobject_cast<QWebSocket*>(sender());
    if (!socket) return;

    const QJsonDocument document = QJsonDocument::fromJson(message.toUtf8());
    if (!document.isObject()) {
        return;
    }

    const QJsonObject payload = document.object();
    const QString command = payload["command"].toString();

    if (command == "loadOsmContent") {
        const QJsonObject value = payload["value"].toObject();
        const QString xmlContent = value["osmContent"].toString();
        const int nbVoitures = value["nbVoitures"].toInt(10000);

        if (xmlContent.isEmpty()) {
            return;
        }

        const QDir tempDir(QDir::tempPath());
        const QString tempPath = tempDir.filePath(
            QStringLiteral("frontend_uploaded_map_%1.osm").arg(QDateTime::currentMSecsSinceEpoch())
        );

        QFile file(tempPath);
            if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            return;
        }

        QTextStream out(&file);
        out << xmlContent;
        file.close();

        chargerGrapheEtVoitures(tempPath.toStdString(), nbVoitures);

        if (graphe.getAretes().empty()) {
            QJsonObject errObj;
            errObj["type"] = "error";
            errObj["message"] = QStringLiteral("Le graphe charge est vide. Verifiez le fichier OSM.");
            QJsonDocument doc(errObj);
            socket->sendTextMessage(QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
            return;
        }

        QJsonObject loadedMsg;
        loadedMsg["type"] = "loaded";
        loadedMsg["nbVoitures"] = getNombreVoitures();
        loadedMsg["nbAretes"] = getNombreAretes();
        QJsonDocument loadedDoc(loadedMsg);
        const QString loadedText = QString::fromUtf8(loadedDoc.toJson(QJsonDocument::Compact));
        for (QWebSocket* c : clients) {
            if (c && c->isValid()) c->sendTextMessage(loadedText);
        }
    }
    else if (command == "loadOsmBbox") {
        const QJsonObject value = payload["value"].toObject();
        const QJsonObject bbox = value["bbox"].toObject();
        const double minLon = bbox["minLon"].toDouble();
        const double minLat = bbox["minLat"].toDouble();
        const double maxLon = bbox["maxLon"].toDouble();
        const double maxLat = bbox["maxLat"].toDouble();
        const int nbVoitures = value["nbVoitures"].toInt(10000);

        if (minLon >= maxLon || minLat >= maxLat) {
            return;
        }

        QString errMsg;
        if (!chargerGrapheDepuisBbox(minLon, minLat, maxLon, maxLat, nbVoitures, errMsg)) {

            QJsonObject errObj;
            errObj["type"] = "error";
            errObj["message"] = QStringLiteral("LOAD_OSM_BBOX failed: %1").arg(errMsg);
            QJsonDocument doc(errObj);
            socket->sendTextMessage(QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
            return;
        }
    }
}

void SimulationServer::onSimulationTick()
{
    deplacerVoitures();
    if (frameCount % 10 == 0) {
        broadcastSimulationState();
    }
}

void SimulationServer::broadcastSimulationState()
{
    if (clients.isEmpty()) {
        return;
    }

    const QString data = generateJsonResponse();
    for (QWebSocket* socket : clients) {
        if (socket && socket->isValid()) {
            socket->sendTextMessage(data);
        }
    }
}

QString SimulationServer::generateJsonResponse() const
{
    QJsonArray voituresArray;

    for (size_t i = 0; i < voitures.size(); ++i) {
        const Voiture& v = voitures[i];
        QJsonObject voitureObj;

        //convertion XY de metres en lat/lon pour afficher sur la carte
        double lat, lon;
        graphe.metersToLatLon(v.getX(), v.getY(), lat, lon);

        voitureObj["id"] = (int)i;
        voitureObj["x"] = lon;  //longitude pour frontend
        voitureObj["y"] = lat;  //latitude pour frontend
        voitureObj["angle"] = 0.0;
        voitureObj["vitesse"] = v.getVitesseKmH();

        voituresArray.append(voitureObj);
    }

    QJsonObject root;
    root["type"] = "update";
    root["timestamp"] = (int)(QDateTime::currentMSecsSinceEpoch() / 1000);
    root["data"] = voituresArray;

    QJsonDocument doc(root);
    return QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
}

void SimulationServer::deplacerVoitures()
{
    for (size_t i = 0; i < voitures.size(); ++i) {
        voitures[i].deplacer(1.0, {});
    }

    frameCount++;
}