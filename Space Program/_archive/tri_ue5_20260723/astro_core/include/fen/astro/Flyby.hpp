// fen/astro/Flyby.hpp
//
// ASSISTANCE GRAVITATIONNELLE.
//
// C'est le seul endroit de la mécanique céleste où l'on obtient du Delta-v SANS
// brûler un gramme. Le vaisseau ne gagne rien dans le repère de la planète :
// |v_inf| est CONSERVÉ, la trajectoire est seulement TOURNÉE d'un angle delta.
// Mais dans le repère du Soleil, tourner v_inf change v_helio = v_planete + v_inf.
// Le vaisseau vole de l'énergie orbitale à la planète — qui ralentit d'une
// quantité inobservable, parce que le rapport des masses est de 10^23.
//
//     sin(delta/2) = 1/e ,   e = 1 + r_p * v_inf^2 / mu
//     |Delta-v heliocentrique gratuit| = 2 * v_inf * sin(delta/2) = 2 * v_inf / e
//
// CONSÉQUENCE REMARQUABLE, et elle se dérive en trois lignes :
//   maximiser 2*v_inf/(1 + r_p*v_inf^2/mu) sur v_inf donne  r_p*v_inf^2/mu = 1,
//   c'est-à-dire  v_inf = sqrt(mu/r_p)  — la vitesse circulaire au périastre —
//   et alors      Delta-v_max = sqrt(mu / r_p).
//   Le gain maximal d'un survol EST la vitesse orbitale circulaire à l'altitude
//   du survol. Pour Jupiter à 1.5 R_J : 34 km/s. Aucun moteur ne concourt.
//
// CE QUI LE REND CHER, ce n'est donc pas le Delta-v : c'est le CALENDRIER.
// Il faut que les planètes soient là. Période synodique Jupiter-Saturne :
// 19,86 ANS. Rater une fenêtre EJS, ce n'est pas attendre 26 mois — c'est
// attendre une génération.
#pragma once
#include <cmath>
#include "fen/core/Vec3.hpp"
#include "fen/core/Constants.hpp"

namespace fen::astro {

// Angle de déviation d'un survol NON propulsé.
inline double flyby_turn(double vinf, double rp, double mu) {
  const double e = 1.0 + rp * vinf * vinf / mu;
  return 2.0 * std::asin(1.0 / e);
}

// Périastre requis pour obtenir une déviation delta.
inline double flyby_rp_for_turn(double vinf, double delta, double mu) {
  const double e = 1.0 / std::sin(0.5 * delta);
  return (e - 1.0) * mu / (vinf * vinf);
}

// Delta-v héliocentrique GRATUIT d'un survol non propulsé.
inline double flyby_free_dv(double vinf, double rp, double mu) {
  return 2.0 * vinf * std::sin(0.5 * flyby_turn(vinf, rp, mu));
}

// Le gain maximal possible à ce périastre, et le v_inf qui le réalise.
inline double flyby_max_free_dv(double rp, double mu) { return std::sqrt(mu / rp); }
inline double flyby_optimal_vinf(double rp, double mu) { return std::sqrt(mu / rp); }

// --- SURVOL PROPULSÉ ---------------------------------------------------------
// Quand les deux jambes de Lambert ne donnent pas le même |v_inf|, ou que la
// déviation exigée dépasse ce que la planète peut fournir, il faut une impulsion
// AU PÉRIASTRE — là où elle est la plus efficace (Oberth).
//
//   déviation totale disponible = delta_in/2 + delta_out/2, chacune fonction de r_p
//   Delta-v au périastre        = |sqrt(vo^2 + 2mu/rp) - sqrt(vi^2 + 2mu/rp)|
//
// La déviation DÉCROÎT avec r_p : on cherche donc le r_p qui fournit exactement
// la déviation demandée, et on facture l'impulsion résiduelle.
struct FlybySolution {
  double rp{};                // m
  double dv{};                // m/s — 0 si non propulsé
  double turn_required{};     // rad
  double turn_available{};    // rad, au périastre minimal
  double vinf_in{}, vinf_out{};
  // NOMENCLATURE, et elle n'est pas cosmetique :
  //   dv_helio     = |v_inf_out - v_inf_in| : le changement TOTAL de vitesse
  //                  heliocentrique. Il inclut ce que le MOTEUR a fait.
  //   gravity_gain = dv_helio - dv : ce que la PLANETE a donne, net de ce qu'on
  //                  a paye. C'est la seule quantite qu'on a le droit d'appeler
  //                  "gratuite".
  // Confondre les deux, c'est s'attribuer le travail de son propre moteur.
  double dv_helio{};
  double gravity_gain{};
  bool feasible{false};
  bool unpowered{false};      // écart de |v_inf| < 1 m/s
};

inline FlybySolution solve_flyby(const Vec3& vinf_in, const Vec3& vinf_out,
                                 double mu, double rp_min) {
  FlybySolution s;
  const double vi = norm(vinf_in), vo = norm(vinf_out);
  if (vi <= 0.0 || vo <= 0.0) return s;
  s.vinf_in = vi;
  s.vinf_out = vo;
  s.dv_helio = norm(vinf_out - vinf_in);
  s.turn_required = std::acos(std::fmin(1.0, std::fmax(-1.0,
                        dot(vinf_in, vinf_out) / (vi * vo))));

  auto turn_at = [&](double rp) {
    return 0.5 * flyby_turn(vi, rp, mu) + 0.5 * flyby_turn(vo, rp, mu);
  };
  s.turn_available = turn_at(rp_min);

  // La planète ne peut pas tourner assez : le survol est IMPOSSIBLE.
  // Ce n'est pas une pénalité — c'est une borne physique.
  if (s.turn_available < s.turn_required) { s.rp = rp_min; return s; }

  // Bissection sur r_p (la déviation décroît quand r_p croît).
  double lo = rp_min, hi = rp_min;
  for (int i = 0; i < 200 && turn_at(hi) > s.turn_required; ++i) hi *= 1.3;
  for (int i = 0; i < 120; ++i) {
    const double mid = 0.5 * (lo + hi);
    (turn_at(mid) > s.turn_required ? lo : hi) = mid;
  }
  s.rp = 0.5 * (lo + hi);
  s.dv = std::fabs(std::sqrt(vo * vo + 2.0 * mu / s.rp)
                 - std::sqrt(vi * vi + 2.0 * mu / s.rp));
  s.gravity_gain = std::fmax(0.0, s.dv_helio - s.dv);
  s.feasible = true;
  s.unpowered = (std::fabs(vi - vo) < 1.0);
  return s;
}

// Période synodique de deux corps — la vraie monnaie d'une assistance.
inline double synodic(double T1, double T2) { return 1.0 / std::fabs(1.0 / T1 - 1.0 / T2); }

} // namespace fen::astro
