// fen/nav/Gates.hpp
// MODÈLE D'ERREUR D'EXÉCUTION DE GATES (1963) — le mécanisme de SANCTION du jeu.
//
// Une manoeuvre commandée Delta-v n'est jamais exécutée exactement. L'erreur se
// décompose en quatre termes indépendants :
//    - magnitude fixe        sigma_mf   [m/s]   (bruit de l'accéléromètre, résolution)
//    - magnitude proportionnelle sigma_mp [-]   (erreur d'étalonnage de la poussée)
//    - pointage fixe         sigma_pf   [m/s]   (bruit d'attitude au démarrage)
//    - pointage proportionnel sigma_pp  [-]     (dérive d'attitude pendant l'arc)
//
//    sigma_mag  = sqrt( sigma_mf^2 + (sigma_mp * |dv|)^2 )
//    sigma_perp = sqrt( sigma_pf^2 + (sigma_pp * |dv|)^2 )   (par axe transverse)
//
// Conséquence de jeu, entièrement dérivée : le Delta-v STATISTIQUE (la marge de
// correction nécessaire au 99e percentile) croît avec |dv|. Une conception qui
// empile les gros Delta-v paie DEUX fois : en ergols nominaux, et en marge.
// Aucun designer n'a besoin d'inventer ça — c'est dans les équations.
//
// Valeurs typiques (étage supérieur chimique, 3-sigma) :
//    sigma_mf = 0.02 m/s ; sigma_mp = 0.002 (0.2 %)
//    sigma_pf = 0.02 m/s ; sigma_pp = 0.0015 (~0.09 deg)
#pragma once
#include <cmath>
#include "fen/core/Vec3.hpp"
#include "fen/core/Rng.hpp"

namespace fen::nav {

struct GatesParams {
  double sigma_mag_fixed{0.02};   // m/s   (1-sigma)
  double sigma_mag_prop{0.002};   // -
  double sigma_point_fixed{0.02}; // m/s   (1-sigma, par axe transverse)
  double sigma_point_prop{0.0015};// -
};

// Perturbe un Delta-v commandé. `rng` DOIT être un sous-flux dédié à cette
// manoeuvre (voir Rng::substream) pour que l'ajout d'autres sources d'aléa ne
// décale pas ce tirage.
inline Vec3 apply_gates(const Vec3& dv_cmd, const GatesParams& p, Rng& rng) {
  const double dvn = norm(dv_cmd);
  if (dvn <= 0.0) return dv_cmd;
  const Vec3 u = dv_cmd / dvn;

  const double s_mag  = std::sqrt(p.sigma_mag_fixed * p.sigma_mag_fixed
                                  + (p.sigma_mag_prop * dvn) * (p.sigma_mag_prop * dvn));
  const double s_perp = std::sqrt(p.sigma_point_fixed * p.sigma_point_fixed
                                  + (p.sigma_point_prop * dvn) * (p.sigma_point_prop * dvn));

  // base orthonormée transverse
  Vec3 a = (std::fabs(u.z) < 0.9) ? Vec3{0, 0, 1} : Vec3{1, 0, 0};
  const Vec3 e1 = unit(cross(u, a));
  const Vec3 e2 = cross(u, e1);

  const double dmag = rng.normal(0.0, s_mag);
  const double d1   = rng.normal(0.0, s_perp);
  const double d2   = rng.normal(0.0, s_perp);

  return u * (dvn + dmag) + e1 * d1 + e2 * d2;
}

// Delta-v statistique 1-sigma d'UNE manoeuvre (norme de l'erreur vectorielle).
inline double gates_sigma_total(double dv, const GatesParams& p) {
  const double s_mag  = std::sqrt(p.sigma_mag_fixed * p.sigma_mag_fixed
                                  + (p.sigma_mag_prop * dv) * (p.sigma_mag_prop * dv));
  const double s_perp = std::sqrt(p.sigma_point_fixed * p.sigma_point_fixed
                                  + (p.sigma_point_prop * dv) * (p.sigma_point_prop * dv));
  return std::sqrt(s_mag * s_mag + 2.0 * s_perp * s_perp);
}

} // namespace fen::nav
