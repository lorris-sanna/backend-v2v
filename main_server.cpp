#include <QCoreApplication>
#include <QDebug>
#include "SimulationServer.h"

/**
 * @file main_server.cpp
 * @brief Point d'entrée pour le serveur WebSocket de simulation (sans interface graphique)
 *
 * Utilisation: Ce code lance le serveur backend.
 * Le frontend React se connecte au WebSocket pour recevoir les données.
 */

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    qWarning() << "Serveur WebSocket de Simulation de Voitures";
    qWarning() << "Demarrage...";

    //creation du serveur sur le port 8080
    SimulationServer server(8080);

    qWarning() << "Serveur WebSocket actif sur ws://localhost:8080";
    qWarning() << "En attente d'un chargement OSM depuis le frontend...";

    return app.exec();
}
