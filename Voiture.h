#ifndef VOITURE_H
#define VOITURE_H

#include "Noeud.h"
#include <cmath>
#include <cstdlib>

class Voiture
{
public:
    const static int LONGUEUR_VOITURE;
    const static int LARGEUR_VOITURE;

    Voiture(int id, Noeud* depart, double vitesse);

    //constructeur alternatif avec position initiale le long d'une arête
    Voiture(int id, Noeud* depart, Noeud* nextNode, double initX, double initY, double vitesse);

    void deplacer(double facteurVitesse, const std::vector<Voiture*>& voituresProches);

    //getters & setters
    int getX() const;
    int getY() const;
    void setX(double x) ;
    void setY(double y) ;
    int getVitesse() const;
    int getId() const;
    void setVitesse(int v);
    double getVitesseKmH() const;

private:
    void choisirProchainVoisin();

    int d_id;
    double d_x, d_y;
    double d_vitesse;
    double d_vitesseReelle;
    Noeud* d_currentNode;
    Noeud* d_nextNode;
    Noeud* d_ancienNoeud;
    bool d_forceMove;
    int framesBloquee;
};

#endif // VOITURE_H
