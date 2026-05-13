/**
 * @file Graphe.cpp
 * @brief Implémentation de la classe Graphe pour gérer les noeuds et arêtes d'un graphe routier.
 */

#include "Graphe.h"
#include <iostream>
#include <vector>
#include <map>
#include <string>
#include <algorithm>
#include <cmath>

#include "tinyxml2.h"

using namespace tinyxml2;

/** @brief Rayon approximatif de la Terre en mètres */
static constexpr double RAYON_TERRIEN = 6378137.0;

Graphe::~Graphe()
{
    clear();
}

/**
 * @brief Libère tous les noeuds et arêtes du graphe
 */
void Graphe::clear()
{
    for (Noeud* n : noeuds) {
        delete n;
    }
    noeuds.clear();
    aretes.clear();
}

/**
 * @brief Convertit des coordonnées latitude/longitude en coordonnées X/Y en mètres
 * @param latDeg Latitude en degrés
 * @param lonDeg Longitude en degrés
 * @param originLatDeg Latitude de l'origine en degrés
 * @param originLonDeg Longitude de l'origine en degrés
 * @param[out] outX Coordonnée X en mètres
 * @param[out] outY Coordonnée Y en mètres
 */
void Graphe::latLonToMeters(double latDeg, double lonDeg,
                            double originLatDeg, double originLonDeg,
                            double& outX, double& outY)
{
    double latRad = latDeg * M_PI / 180.0;
    double lonRad = lonDeg * M_PI / 180.0;
    double originLatRad = originLatDeg * M_PI / 180.0;
    double originLonRad = originLonDeg * M_PI / 180.0;

    double dLon = lonRad - originLonRad;
    outX = RAYON_TERRIEN * dLon * std::cos((latRad + originLatRad) / 2.0);

    double dLat = latRad - originLatRad;
    outY = RAYON_TERRIEN * dLat;
}

/**
 * @brief Convertit des coordonnées X/Y en mètres vers latitude/longitude
 * @param x Position X en mètres
 * @param y Position Y en mètres
 * @param outLat Latitude résultante en degrés
 * @param outLon Longitude résultante en degrés
 */
void Graphe::metersToLatLon(double x, double y, double& outLat, double& outLon) const
{
    double dLat = y / RAYON_TERRIEN;
    double originLatRad = originLat * M_PI / 180.0;
    double originLonRad = originLon * M_PI / 180.0;
    
    double latRad = originLatRad + dLat;
    double dLon = x / (RAYON_TERRIEN * std::cos((latRad + originLatRad) / 2.0));
    double lonRad = originLonRad + dLon;
    
    outLat = latRad * 180.0 / M_PI;
    outLon = lonRad * 180.0 / M_PI;
}

/**
 * @brief Charge un graphe à partir d'un fichier OSM
 * @param fichierOSM Chemin vers le fichier OSM
 * @return true si le chargement est réussi, false sinon
 *
 * Cette fonction :
 * 1. Lit tous les noeuds du fichier OSM
 * 2. Crée les arêtes à partir des <way> de type highway
 * 3. Définit les coordonnées X/Y des noeuds en mètres
 */
bool Graphe::chargerDepuisOSM(const std::string& fichierOSM)
{
    XMLDocument doc;

    if (doc.LoadFile(fichierOSM.c_str()) != XML_SUCCESS) {
        std::cerr << "Erreur de chargement du fichier OSM : " << fichierOSM << std::endl;
        return false;
    }

    std::map<long long, Noeud*> idToNode;

    //chargement des noeuds
    XMLElement* root = doc.FirstChildElement("osm");
    for (XMLElement* nodeElem = root->FirstChildElement("node"); nodeElem; nodeElem = nodeElem->NextSiblingElement("node")) {
        long long id = nodeElem->Int64Attribute("id");
        double lat = nodeElem->DoubleAttribute("lat");
        double lon = nodeElem->DoubleAttribute("lon");

        Noeud* n = new Noeud(lat, lon, id); //lat/lon brutes
        idToNode[id] = n;
    }

    std::vector<Noeud*> noeudsUtiles;

    //chargement des way (routes) et creation des aretes
    for (XMLElement* wayElem = root->FirstChildElement("way"); wayElem; wayElem = wayElem->NextSiblingElement("way")) {
        bool estRoute = false;

        for (XMLElement* tag = wayElem->FirstChildElement("tag"); tag; tag = tag->NextSiblingElement("tag")) {
            const char* k = tag->Attribute("k");
            if (k && std::string(k) == "highway") {
                estRoute = true;
                break;
            }
        }

        if (!estRoute) continue;

        std::vector<Noeud*> points;
        for (XMLElement* nd = wayElem->FirstChildElement("nd"); nd; nd = nd->NextSiblingElement("nd")) {
            long long ref = nd->Int64Attribute("ref");
            auto it = idToNode.find(ref);
            if (it != idToNode.end()) {
                Noeud* n = it->second;
                points.push_back(n);

                if (std::find(noeudsUtiles.begin(), noeudsUtiles.end(), n) == noeudsUtiles.end())
                    noeudsUtiles.push_back(n);
            }
        }

        for (size_t i = 1; i < points.size(); ++i) {
            ajouterArete(points[i - 1], points[i]);
        }
    }

    //ajout uniquement des noeuds connectes aux aretes
    for (Noeud* n : noeudsUtiles)
        ajouterNoeud(n);

    //definition de coordonnees X/Y coherentes (en mètres)
    if (!noeudsUtiles.empty()) {
        double originLatTemp = noeudsUtiles[0]->getLat();
        double originLonTemp = noeudsUtiles[0]->getLon();
        
        //stockage de l'origine pour les futures conversions
        originLat = originLatTemp;
        originLon = originLonTemp;

        for (Noeud* n : noeudsUtiles) {
            double mx, my;
            latLonToMeters(n->getLat(), n->getLon(), originLatTemp, originLonTemp, mx, my);
            n->setX(mx);
            n->setY(my);
        }
    }

    std::cout << "Chargement OSM termine : " << noeudsUtiles.size()
              << " noeuds connectes, " << aretes.size() << " aretes." << std::endl;

    return true;
}
