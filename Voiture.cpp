/**
 * @file Voiture.cpp
 * @brief Implémentation de la classe Voiture pour la simulation de trafic.
 */

#include "Voiture.h"
#include <cmath>
#include <cstdlib>

/** @brief Longueur standard d'une voiture en unités de simulation */
const int Voiture::LONGUEUR_VOITURE = 12;

/** @brief Largeur standard d'une voiture en unités de simulation */
const int Voiture::LARGEUR_VOITURE = 7;

/** @brief Temps par frame (60 FPS ~ 16 ms) */
static constexpr double dt = 0.016;

/**
 * @brief Constructeur de la voiture
 * @param id Identifiant unique de la voiture
 * @param depart Noeud de départ
 * @param vitesse Vitesse initiale
 */
Voiture::Voiture(int id, Noeud* depart, double vitesse)
    : d_id(id), d_currentNode(depart), d_vitesse(vitesse), d_ancienNoeud(nullptr), d_vitesseReelle(0.0),
    framesBloquee(0), d_forceMove(false)
{
    d_x = depart->getX();
    d_y = depart->getY();
    choisirProchainVoisin();
}

//constructeur alternatif avec position initiale le long d'une arête
Voiture::Voiture(int id, Noeud* depart, Noeud* nextNode, double initX, double initY, double vitesse)
    : d_id(id), d_currentNode(depart), d_nextNode(nextNode), d_vitesse(vitesse), d_ancienNoeud(nullptr), d_vitesseReelle(0.0),
    framesBloquee(0), d_forceMove(false)
{
    d_x = initX;
    d_y = initY;
    //on conserve le prochain noeud fourni
    if (!d_nextNode) {
        choisirProchainVoisin();
    }
}

/**
 * @brief Déplace la voiture en fonction de la vitesse et des voitures proches
 * @param facteurVitesse Facteur de vitesse pour ajuster le déplacement
 * @param voituresProches Liste des voitures proches pour éviter les collisions
 */
void Voiture::deplacer(double facteurVitesse, const std::vector<Voiture*>& voituresProches)
{
    if (!d_currentNode || !d_nextNode) return;

    //direction vers next node
    double deltaXVersNoeud = d_nextNode->getX() - d_x;
    double deltaYVersNoeud = d_nextNode->getY() - d_y;
    double distanceVersNoeud = std::sqrt(deltaXVersNoeud * deltaXVersNoeud + deltaYVersNoeud * deltaYVersNoeud);
    if (distanceVersNoeud < 1e-6) return;

    double directionX = deltaXVersNoeud / distanceVersNoeud;
    double directionY = deltaYVersNoeud / distanceVersNoeud;

    double vitesseEffective = d_vitesse * facteurVitesse;

    const double seuilCollision = Voiture::LONGUEUR_VOITURE;

    //ajustement de la vitesse si voitures proches
    if (!d_forceMove) {
        for (auto* autreVoiture : voituresProches) {
            double dxAutre = autreVoiture->getX() - d_x;
            double dyAutre = autreVoiture->getY() - d_y;
            double distAutre = std::sqrt(dxAutre*dxAutre + dyAutre*dyAutre);
            if (distAutre < 1e-6) distAutre = 1e-6;

            double projectionAvant = dxAutre * directionX + dyAutre * directionY;

            double dirAutreX = autreVoiture->d_nextNode->getX() - autreVoiture->getX();
            double dirAutreY = autreVoiture->d_nextNode->getY() - autreVoiture->getY();
            double normeDirAutre = std::sqrt(dirAutreX*dirAutreX + dirAutreY*dirAutreY);
            if (normeDirAutre > 1e-6) { dirAutreX /= normeDirAutre; dirAutreY /= normeDirAutre; }

            double alignement = directionX * dirAutreX + directionY * dirAutreY;

            if (distAutre < seuilCollision) {
                if (projectionAvant > 0) {
                    double facteurFrein = distAutre / seuilCollision;
                    if (facteurFrein < 0.2) facteurFrein = 0.2;
                    vitesseEffective *= facteurFrein;
                } else if (projectionAvant < 0 && alignement < -0.8 && distAutre < seuilCollision) {
                    vitesseEffective *= 0.8;
                } else if (std::abs(projectionAvant) < 1e-6) {
                    auto voisins = d_currentNode->getVoisins();
                    if (!voisins.empty()) {
                        Noeud* oldNext = d_nextNode;
                        while (voisins.size() > 1 && voisins[rand() % voisins.size()] == oldNext) {}
                        d_nextNode = voisins[rand() % voisins.size()];
                    }
                }
            }
        }
    }

    //deplacement reel
    double deltaX = vitesseEffective * directionX;
    double deltaY = vitesseEffective * directionY;
    double distanceParcourue = std::sqrt(deltaX*deltaX + deltaY*deltaY);
    d_vitesseReelle = distanceParcourue / dt;

    d_x += deltaX;
    d_y += deltaY;

    //detection blocage
    if (distanceParcourue < 0.01) {
        framesBloquee++;
        if (framesBloquee > 50) d_forceMove = true;
    } else {
        framesBloquee = 0;
        d_forceMove = false;
    }

    //changement de noeud
    if (distanceVersNoeud < vitesseEffective) {
        d_ancienNoeud = d_currentNode;
        d_x = d_nextNode->getX();
        d_y = d_nextNode->getY();
        d_currentNode = d_nextNode;
        choisirProchainVoisin();
        framesBloquee = 0;
        d_forceMove = false;
    }
}

/**
 * @brief Choisit le prochain noeud vers lequel se diriger
 */
void Voiture::choisirProchainVoisin()
{
    const auto& voisins = d_currentNode->getVoisins();
    if (voisins.empty()) {
        d_nextNode = nullptr;
        return;
    }

    if (voisins.size() == 1) {
        d_nextNode = voisins[0];
        return;
    }

    Noeud* choisi = nullptr;
    for (int i = 0; i < 10; ++i) {
        Noeud* cand = voisins[rand() % voisins.size()];
        if (cand != d_ancienNoeud) {
            choisi = cand;
            break;
        }
    }

    if (!choisi) {
        choisi = voisins[rand() % voisins.size()];
    }

    d_nextNode = choisi;
}

/** @brief Modifie la vitesse de la voiture */
void Voiture::setVitesse(int v) { d_vitesse = v; }

/** @brief Retourne la vitesse programmée de la voiture */
int Voiture::getVitesse() const { return d_vitesse; }

/** @brief Retourne l'identifiant de la voiture */
int Voiture::getId() const { return d_id; }

/** @brief Retourne la position X de la voiture */
int Voiture::getX() const { return d_x; }

/** @brief Retourne la position Y de la voiture */
int Voiture::getY() const { return d_y; }

/** @brief Modifie la position X de la voiture */
void Voiture::setX(double x) { d_x = x; }

/** @brief Modifie la position Y de la voiture */
void Voiture::setY(double y) { d_y = y; }

/** @brief Retourne la vitesse réelle en km/h */
double Voiture::getVitesseKmH() const {
    return d_vitesseReelle * 3.6;
}
