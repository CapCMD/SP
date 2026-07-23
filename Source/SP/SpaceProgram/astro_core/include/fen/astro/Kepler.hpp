// fen/astro/Kepler.hpp
// Propagation képlérienne par VARIABLES UNIVERSELLES (Bate-Mueller-White /
// Vallado §2.2). Un seul code de calcul pour l'ellipse, la parabole et
// l'hyperbole : pas de branchement sur le type de conique, donc pas de
// discontinuité artificielle quand e -> 1 (ce qui arrive en permanence dans un
// jeu où le joueur ajuste ses Delta-v).
//
// ATTENTION DE DOCTRINE : ceci est un OUTIL DE CONCEPTION (modèle 2 corps),
// pas le propagateur de vérité. La vérité est prop/Propagator (N-corps +
// poussée finie). L'écart entre les deux est facturé au joueur en Delta-v de
// correction. C'est le cœur de "l'économie de fidélité de modèle".
#pragma once
#include "fen/core/Vec3.hpp"
#include "fen/core/State.hpp"

namespace fen::astro {

// Fonctions de Stumpff C(z), S(z), avec développement en série près de z=0
// (sinon annulation catastrophique : (1-cos x)/x^2 perd ~8 chiffres pour x<1e-4).
double stumpff_C(double z);
double stumpff_S(double z);

struct KeplerResult {
  Vec3 r;
  Vec3 v;
  int iterations{0};
  bool converged{false};
};

// Propage (r0,v0) de dt secondes dans un champ képlérien de paramètre mu.
// dt peut être négatif (rétro-propagation exacte, requise par la détermination
// d'orbite et le ciblage plan-B).
KeplerResult kepler_propagate(const Vec3& r0, const Vec3& v0, double dt, double mu,
                              double tol = 1e-12, int max_iter = 60);

// Équation de Kepler elliptique : M = E - e sin E. Newton amorcé par Danby.
double solve_kepler_elliptic(double M, double e, double tol = 1e-13, int max_iter = 50);
// Hyperbolique : M = e sinh H - H
double solve_kepler_hyperbolic(double M, double e, double tol = 1e-13, int max_iter = 100);

} // namespace fen::astro
