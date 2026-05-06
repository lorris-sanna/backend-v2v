#ifndef GRAPHE_H
#define GRAPHE_H

#include "Noeud.h"
#include <vector>
#include <utility>
#include <string>
#include <map>
#include "tinyxml2.h"

class Graphe {
public:
    ~Graphe();
    void clear();
    void ajouterNoeud(Noeud* n) { noeuds.push_back(n); }

    void latLonToMeters(double latDeg, double lonDeg,
                                double originLatDeg, double originLonDeg,
                        double& outX, double& outY);

    void metersToLatLon(double x, double y, double& outLat, double& outLon) const;

    void ajouterArete(Noeud* a, Noeud* b) {
        a->ajouterVoisin(b);
        b->ajouterVoisin(a);
        aretes.push_back({a, b});
    }

    const std::vector<Noeud*>& getNoeuds() const { return noeuds; }
    const std::vector<std::pair<Noeud*, Noeud*>>& getAretes() const { return aretes; }


    bool chargerDepuisOSM(const std::string& fichierOSM);

private:
    std::vector<Noeud*> noeuds;
    std::vector<std::pair<Noeud*, Noeud*>> aretes;
    
    double originLat = 0.0;
    double originLon = 0.0;
    friend class SimulationServer;
};

#endif // GRAPHE_H
