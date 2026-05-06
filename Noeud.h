#ifndef NOEUD_H
#define NOEUD_H

#include <vector>

class Noeud {
public:
    //constructeur pour simulation avec x/y
    Noeud(int id, double x, double y)
        : d_id(id), d_x(x), d_y(y), d_lat(0.0), d_lon(0.0) {}

    //constructeur pour chargement OSM avec lat/lon
    Noeud(double lat, double lon, long long id)
        : d_id((int)id), d_x(0.0), d_y(0.0), d_lat(lat), d_lon(lon) {}

    int getId() const { return d_id; }
    double getX() const { return d_x; }
    double getY() const { return d_y; }

    double getLat() const { return d_lat; }
    double getLon() const { return d_lon; }

    void setX(double x) { d_x = x; }
    void setY(double y) { d_y = y; }

    void ajouterVoisin(Noeud* voisin) { voisins.push_back(voisin); }
    const std::vector<Noeud*>& getVoisins() const { return voisins; }

private:
    int d_id;
    double d_x, d_y;     //coordonnées pour affichage OpenGL
    double d_lat, d_lon; //coordonnées GPS (OSM)
    std::vector<Noeud*> voisins;
};

#endif // NOEUD_H
