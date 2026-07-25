// fen/nav/OrbitDetermination.hpp
//
// DÉTERMINATION D'ORBITE — moindres carrés par lots (Tapley, Schutz & Born, ch. 4).
//
// Le joueur n'observe pas son état : il ajuste un état à des mesures bruitées.
// Ce qui en sort, c'est un ESTIMÉ et une COVARIANCE. La covariance n'est pas une
// décoration : c'est la matrice qui dit dans quelles directions il ne sait pas
// où il est, et c'est elle qui fixe la taille de la correction qu'il devra
// budgéter. Acheter des passes, c'est acheter de l'inverse de covariance.
//
// MATRICE DE TRANSITION D'ÉTAT (STM)
// ----------------------------------
// Phi(t1,t0) = d x(t1) / d x(t0). Obtenue ici par DIFFÉRENCES FINIES CENTRÉES
// sur le propagateur de vérité lui-même : 12 propagations perturbées.
//
// Ce choix est délibéré, pas paresseux :
//   - c'est EXACTEMENT ce que le joueur peut faire avec l'API publique (aucune
//     capacité privilégiée dans le moteur) ;
//   - il n'y a AUCUN modèle dupliqué à maintenir en cohérence avec la vérité :
//     la STM dérive du propagateur réel, gradient de gravité compris ;
//   - il est vérifiable par un invariant que rien ne peut satisfaire par hasard :
//     le flot d'un système hamiltonien est SYMPLECTIQUE, donc Phi^T J Phi = J.
//     C'est l'oracle du test.
// Coût : 12 propagations par arc. Les équations variationnelles analytiques
// (dPhi/dt = A Phi) seraient ~10x plus rapides : PHASE 5b, sans changement d'API.
#pragma once
#include <vector>
#include "fen/core/Matrix.hpp"
#include "fen/core/State.hpp"
#include "fen/force/Forces.hpp"
#include "fen/nav/Gates.hpp"
#include "fen/nav/Tracking.hpp"
#include "fen/prop/Propagator.hpp"

namespace fen::nav {

// Phi(t1, t0) par différences finies centrées sur le propagateur de vérité.
// Pas de differenciation : h <= 0 => ECHELLE RELATIVE automatique (5e-7 de |r|,
// |v|). Ce n'est pas un chiffre devine : c'est le fond de la courbe en V mesuree
// (troncature en h^2 d'un cote, bruit de l'integrateur de l'autre) :
//     h_r = 5000 m -> 7.2e-1     h_r =  100 m -> 2.9e-4
//     h_r = 2000 m -> 1.2e-1     h_r =   20 m -> 1.5e-5   <- plancher
//     h_r =  500 m -> 7.2e-3     h_r =    1 m -> 4.0e-5   <- l'arrondi reprend
// (residu de symplecticite, arc de 0.37 periode GTO, rtol=1e-12)
Mat6 stm(const force::ForceStack& forces, double t0, const StateN& y0, double t1,
         const prop::PropOptions& opt, double h_r = -1.0, double h_v = -1.0);

struct OdResult {
  Vec6 x_hat;             // état estimé à t_ref
  Mat6 P;                 // covariance à t_ref
  double t_ref{};
  double mass{};          // non estimée : lue sur la télémétrie du véhicule
  int iterations{0};
  int n_measurements{0};
  double rms_residual{0}; // en sigmas. ~1 = le modèle explique les données.
  bool converged{false};
  bool observable{true};  // false = matrice normale singulière
};

// Moindres carrés par lots (Gauss-Newton). x0 = a priori.
OdResult batch_least_squares(const force::ForceStack& forces,
                             const std::vector<Measurement>& meas,
                             const std::vector<Station>& stations,
                             double t_ref, const StateN& x0_apriori,
                             const prop::PropOptions& opt,
                             int max_iter = 6);

// Propage un estimé ET sa covariance : P(t1) = Phi P(t0) Phi^T.
struct StateEstimate {
  Vec6 x;
  Mat6 P;
  double t{};
  double mass{};
};
StateEstimate propagate_estimate(const force::ForceStack& forces,
                                 const StateEstimate& e, double t1,
                                 const prop::PropOptions& opt);

// Une manoeuvre AJOUTE de l'incertitude : l'erreur d'exécution de Gates est,
// dans l'espace d'état, une covariance de vitesse ajoutée. Le joueur qui ne
// rachète pas de poursuite après une manoeuvre garde cette incertitude, et elle
// se propage. C'est la raison physique pour laquelle on retrace après chaque
// manoeuvre — et donc pourquoi la note de poursuite grimpe.
Mat6 gates_covariance(const Vec3& dv_commanded_inertial, const GatesParams& g);

} // namespace fen::nav
