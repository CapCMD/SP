// fen/astro/Lambert.hpp
// Problème de Lambert — algorithme d'Izzo (2014, "Revisiting Lambert's problem",
// Celest. Mech. Dyn. Astron. 121:1-15). Itération de Householder d'ordre 3 sur la
// variable x, convergence en 2-3 itérations, sans dérive pour e -> 1, avec
// support multi-révolution.
//
// C'EST LE CŒUR DE LA CONCEPTION INTERPLANÉTAIRE. Tout porkchop, toute recherche
// de fenêtre, tout ciblage plan-B en dépend. Pas de MVP sans Lambert.
//
// Il reste un OUTIL DE CONCEPTION 2-corps : la solution qu'il renvoie n'atteint
// PAS exactement la cible dans le propagateur de vérité (N-corps + poussée finie).
// L'écart est mesuré, et c'est ce qui fait exister la manoeuvre de correction.
#pragma once
#include <vector>
#include "fen/core/Vec3.hpp"

namespace fen::astro {

struct LambertSolution {
  Vec3 v1;              // vitesse requise en r1 [m/s]
  Vec3 v2;              // vitesse à l'arrivée en r2 [m/s]
  int revolutions{0};   // nombre de révolutions complètes
  bool left_branch{false};
  int iterations{0};
};

struct LambertResult {
  std::vector<LambertSolution> solutions; // [0] = solution 0-révolution
  bool ok{false};
  const char* error{nullptr};
};

// r1, r2 en m ; tof en s (> 0) ; mu en m^3/s^2.
// prograde : sens du transfert (h_z > 0 dans le repère de référence).
// max_revs : nombre max de révolutions complètes explorées (0 = transfert direct).
LambertResult lambert(const Vec3& r1, const Vec3& r2, double tof, double mu,
                      bool prograde = true, int max_revs = 0,
                      double tol = 1e-11, int max_iter = 30);

} // namespace fen::astro
