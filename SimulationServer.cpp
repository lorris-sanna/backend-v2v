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

void SimulationServer::creerVoitures(int nb)
{
    voitures.clear();

    const auto& aretes = graphe.getAretes();
    if (aretes.empty()) return;

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
        const auto& noeuds = graphe.getNoeuds();
        int nbNoeuds = noeuds.size();
        if (nbNoeuds == 0) return;
        for (int i = 0; i < nb; ++i) {
            Noeud* noeudDepart = noeuds[i % nbNoeuds];
            voitures.emplace_back(i, noeudDepart, 0.4);
        }
    } else {
        size_t m = aretes.size();
        std::vector<double> ideals(m);
        std::vector<int> counts(m, 0);
        double accum = 0.0;
        for (size_t i = 0; i < m; ++i) {
            ideals[i] = (longueurs[i] / totalLongueur) * nb;
            counts[i] = static_cast<int>(std::floor(ideals[i]));
            accum += counts[i];
        }

        int remaining = nb - static_cast<int>(accum);
        std::vector<size_t> idx(m);
        for (size_t i = 0; i < m; ++i) idx[i] = i;
        std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) {
            return (ideals[a] - std::floor(ideals[a])) > (ideals[b] - std::floor(ideals[b]));
        });
        for (int k = 0; k < remaining; ++k) {
            counts[idx[k % m]] += 1;
        }

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
                double t = (static_cast<double>(k) + 1.0) / (static_cast<double>(c) + 1.0);
                double initX = ax + t * (bx - ax);
                double initY = ay + t * (by - ay);
                voitures.emplace_back(idCounter++, a, b, initX, initY, 0.4);
            }
        }
    }

    qWarning() << "Voitures recrees:" << nb;
}

void SimulationServer::chargerGrapheEtVoitures(const std::string& pathOSM, int nbVoitures)
{
    if (!graphe.chargerDepuisOSM(pathOSM)) {
        qWarning() << "Erreur: impossible de charger le fichier OSM";
        return;
    }

    creerVoitures(nbVoitures);

    qWarning() << "Simulation chargee avec" << nbVoitures << "voitures et"
               << graphe.getAretes().size() << "aretes";
}

void SimulationServer::setNombreVoitures(int nb)
{
    if (nb < 1) return;
    creerVoitures(nb);
    broadcastSimulationState();
}

void SimulationServer::demarrerSimulation()
{
    if (voitures.empty()) {
        qWarning() << "Erreur: aucune voiture n'a ete chargee";
        return;
    }

    simulationActive = true;
    simulationPausee = false;
    simulationTimer->start(16); //~60 FPS
    qWarning() << "Simulation démarree";
    broadcastSimulationState();
}

void SimulationServer::arreterSimulation()
{
    simulationActive = false;
    simulationTimer->stop();
    qWarning() << "Simulation arrêtée";
}

void SimulationServer::togglePause()
{
    simulationPausee = !simulationPausee;
    qWarning() << "Simulation" << (simulationPausee ? "mise en pause" : "reprise");
    broadcastSimulationState();
}

void SimulationServer::setFacteurVitesse(double facteur)
{
    facteurVitesse = facteur;
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
    const QString command = payload.value("command").toString();

    qWarning() << "Commande WebSocket:" << command;

    if (command == "start") {
        demarrerSimulation();
    } else if (command == "stop") {
        arreterSimulation();
    } else if (command == "pause") {
        togglePause();
    } else if (command == "speed") {
        setFacteurVitesse(payload.value("value").toDouble(1.0));
        broadcastSimulationState();
    } else if (command == "setVehicles") {
        setNombreVoitures(payload.value("value").toInt(1000));
    }
}

void SimulationServer::onSimulationTick()
{
    if (!simulationActive || simulationPausee) return;
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
    root["simulationRunning"] = simulationActive;
    root["simulationPaused"] = simulationPausee;

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
