#ifndef SIMULATIONSERVER_H
#define SIMULATIONSERVER_H

#include <QObject>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QtWebSockets/QWebSocketServer>
#include <QtWebSockets/QWebSocket>
#include <vector>
#include <string>

#include "Graphe.h"
#include "Voiture.h"

/**
 * @class SimulationServer
 * @brief Serveur WebSocket pour la simulation de voitures en temps réel.
 *
 * Cette classe gère:
 * - La simulation pure (calcul de positions) sans interface graphique
 * - Un flux WebSocket pour pousser l'état au frontend React
 * - L'envoi des données des voitures en JSON
 * 
 * Le frontend s'abonne au WebSocket pour recevoir les mises à jour en direct.
 */
class SimulationServer : public QObject
{
    Q_OBJECT

public:
    explicit SimulationServer(quint16 port = 8080, QObject* parent = nullptr);
    ~SimulationServer();

    void chargerGrapheEtVoitures(const std::string& pathOSM, int nbVoitures);
    bool chargerGrapheDepuisBbox(double minLon, double minLat, double maxLon, double maxLat, int nbVoitures, QString& errorMsg);

    //accesseurs
    int getNombreVoitures() const { return voitures.size(); }
    int getNombreAretes() const { return graphe.getAretes().size(); }

private slots:
    void onNewConnection();
    void onTextMessageReceived(const QString& message);
    void onSimulationTick();
    void onClientDisconnected();

private:
    void deplacerVoitures();
    
    QString generateJsonResponse() const;
    void broadcastSimulationState();

    //membres
    QWebSocketServer* webSocketServer;
    QList<QWebSocket*> clients;
    QTimer* simulationTimer;
    
    Graphe graphe;
    std::vector<Voiture> voitures;
    int frameCount = 0;
};

#endif // SIMULATIONSERVER_H
