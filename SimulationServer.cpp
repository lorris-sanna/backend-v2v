#include "SimulationServer.h"
#include <QDebug>
#include <QDateTime>
#include <QHostAddress>
#include <QJsonParseError>
#include <QJsonValue>
#include <QJsonDocument>
#include <iostream>
#include <algorithm>
#include <cmath>

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
    if (!graphe.chargerDepuisOSM(pathOSM)) {
        qWarning() << "Erreur: impossible de charger le fichier OSM";
        return;
    }

    voitures.clear();

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

void SimulationServer::demarrerSimulation()
{
    if (voitures.empty()) {
        qWarning() << "Erreur: aucune voiture n'a ete chargee";
        return;
    }

    if (!isRunning) {
        simulationTimer->start(16); //~60 FPS
        isRunning = true;
        qWarning() << "Simulation démarree";
    }
    
    broadcastStatus();
}

void SimulationServer::arreterSimulation()
{
    if (isRunning) {
        simulationTimer->stop();
        isRunning = false;
        qWarning() << "Simulation arretee";
    }
    
    broadcastStatus();
}

void SimulationServer::setFacteurVitesse(double facteur)
{
    facteurVitesse = facteur;
    qWarning() << "Facteur de vitesse : " << facteur;
    broadcastStatus();
}

void SimulationServer::onNewConnection()
{
    while (QWebSocket* socket = webSocketServer->nextPendingConnection()) {
        qWarning() << "Nouveau client WebSocket connecte";

        connect(socket, &QWebSocket::textMessageReceived, this, &SimulationServer::onTextMessageReceived);
        connect(socket, &QWebSocket::disconnected, this, &SimulationServer::onClientDisconnected);

        clients.append(socket);
        socket->sendTextMessage(generateJsonResponse());
        //envoi du status initial au nouveau client
        socket->sendTextMessage(generateStatusMessage());
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

    if (command == "play") {
        demarrerSimulation();
        qWarning() << "Commande PLAY recue";
    }
    else if (command == "pause") {
        arreterSimulation();
        qWarning() << "Commande PAUSE recue";
    }
    else if (command == "setSpeed") {
        double speed = payload["value"].toDouble(1.0);
        setFacteurVitesse(speed);
        qWarning() << "Commande SET_SPEED recue:" << speed;
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
        voitures[i].deplacer(facteurVitesse, {});
    }

    frameCount++;
}

QString SimulationServer::generateStatusMessage() const
{
    QJsonObject root;
    root["type"] = "status";
    root["isRunning"] = isRunning;
    root["speedFactor"] = facteurVitesse;
    root["timestamp"] = (int)(QDateTime::currentMSecsSinceEpoch() / 1000);

    QJsonDocument doc(root);
    return QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
}

void SimulationServer::broadcastStatus()
{
    if (clients.isEmpty()) {
        return;
    }

    const QString data = generateStatusMessage();
    for (QWebSocket* socket : clients) {
        if (socket && socket->isValid()) {
            socket->sendTextMessage(data);
        }
    }
}