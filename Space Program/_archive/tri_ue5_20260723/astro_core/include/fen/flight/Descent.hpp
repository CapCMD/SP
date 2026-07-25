// fen/flight/Descent.hpp
//
// ATTERRISSAGE SANS ATMOSPHERE (alunissage, astéroïde, corps aéride).
//
// La descente propulsée est le dernier segment du cycle de mission. Ici, PAS de
// pilotage temps réel (axiome 1) : le joueur CONÇOIT l'atterrisseur (poussée,
// Isp, ergols -> TWR et Δv) et choisit un profil de descente ; la physique
// TRANCHE. Le modèle est une VÉRITÉ intégrée, pas une formule décrétée :
//
//   - freinage à poussée constante ("plein gaz"), guidage GRAVITY-TURN
//     (poussée exactement anti-vitesse) — le guidage de descente standard,
//     déterministe, quasi-optimal en ergols ;
//   - gravité CENTRALE exacte g = μ/r² (pas de "g plat" : sur un petit corps
//     l'altitude de freinage n'est pas négligeable devant R) ;
//   - masse variable par Tsiolkovski (dm/dt = -T/(Isp·g0)).
//
// SANCTION PHYSIQUE, non inventée : le Δv de freinage tend vers v_orbital quand
// TWR -> ∞ (limite impulsionnelle), et EXPLOSE quand TWR -> 1 (pertes de gravité :
// un moteur trop faible brûle son Δv à lutter contre la pesanteur). C'est le vrai
// arbitrage d'ingénierie de tout alunisseur. Vérifié : Lune, Isp 311 s, TWR 2-3
// -> ~1730-1750 m/s de freinage (Apollo LM : ~2000 m/s marge comprise).
#pragma once
#include <cmath>
#include "fen/core/Constants.hpp"

namespace fen::flight {

struct DescentOutcome {
  double touchdown_speed{};    // m/s — |v| au contact (ou à l'épuisement des ergols)
  double dv_spent{};           // m/s — intégrale de poussée ∫(T/m)dt
  double propellant_used{};    // kg
  double duration{};           // s
  double min_altitude{};       // m — altitude minimale atteinte (0 = posé)
  bool   reached_ground{false};// a touché le sol
  bool   out_of_propellant{false}; // ergols épuisés avant le contact -> chute
};

// Intègre un freinage plein gaz (poussée `thrust` constante, guidage gravity-turn)
// depuis un parking CIRCULAIRE à l'altitude `h_pdi`, jusqu'au sol ou à l'épuisement
// des ergols `prop_available`. Semi-implicite (symplectique-ish), pas fixe `dt`.
inline DescentOutcome powered_descent(double mu, double R, double h_pdi,
                                      double thrust, double m0, double isp,
                                      double prop_available, double dt = 0.02) {
  const double ve = isp * cst::G0;
  const double m_dry = m0 - prop_available;
  double m = m0, dv = 0.0, t = 0.0;
  double rx = 0.0, ry = R + h_pdi;
  double vx = std::sqrt(mu / ry), vy = 0.0;     // circulaire prograde, horizontal
  DescentOutcome o; o.min_altitude = h_pdi;
  for (int i = 0; i < 400000; ++i) {            // borne : 8000 s à dt=0.02
    const double rn = std::sqrt(rx*rx + ry*ry);
    const double alt = rn - R;
    const double sp = std::sqrt(vx*vx + vy*vy);
    if (alt < o.min_altitude) o.min_altitude = alt;
    if (alt <= 0.0)   { o.reached_ground = true;   o.touchdown_speed = sp; break; }
    if (m <= m_dry)   { o.out_of_propellant = true; o.touchdown_speed = sp; break; }
    // ARRET AVANT LA SINGULARITE : le guidage anti-vitesse -v/|v| devient indefini
    // quand |v| -> 0 (la direction "s'emballe" a un pas de temps pres). On stoppe le
    // freinage des que |v| descend sous quelques pas de poussee -> le braquage reste
    // stable et le dv integre est propre. (v residuelle << tolerance d'atterrissage.)
    if (sp < 5.0 * (thrust / m) * dt) { o.touchdown_speed = sp; break; }   // quasi-immobile
    const double ag = -mu / (rn*rn*rn);         // gravité centrale
    const double th = thrust / m;
    vx += (ag*rx - th*vx/sp) * dt;
    vy += (ag*ry - th*vy/sp) * dt;              // poussée anti-vitesse (gravity turn)
    rx += vx*dt; ry += vy*dt;
    m  -= (thrust/ve) * dt; dv += th*dt; t += dt;
  }
  o.dv_spent = dv; o.propellant_used = m0 - m; o.duration = t;
  if (o.min_altitude < 0.0) o.min_altitude = 0.0;
  return o;
}

// Δv de freinage MINIMAL pour un atterrissage DOUX (v_touch ~ 0), pour un TWR de
// surface donné (twr0 = poussée / (m0·g_surf)). Optimise l'altitude de PDI. La
// masse est normalisée : le Δv ne dépend que de (μ, R, twr0, Isp), pas de m0.
inline double descent_dv_required(double mu, double R, double twr0, double isp,
                                  double* h_pdi_out = nullptr) {
  const double g = mu / (R * R);
  const double m0 = 1000.0;
  const double thrust = twr0 * m0 * g;
  double lo = 200.0, hi = 300000.0, best_dv = 0.0, best_h = 0.0, best_v = 1e30;
  for (int it = 0; it < 72; ++it) {
    const double h = 0.5 * (lo + hi);
    const DescentOutcome o = powered_descent(mu, R, h, thrust, m0, isp, m0 * 0.99);
    // SEULES les solutions qui ATTEIGNENT LE SOL sont des atterrissages (annuler la
    // vitesse en l'air = du vol stationnaire, pas un posé). On garde la plus douce.
    if (o.reached_ground && o.touchdown_speed < best_v) {
      best_v = o.touchdown_speed; best_dv = o.dv_spent; best_h = h;
    }
    // freiner plus HAUT (h grand) laisse plus de temps -> touche plus doucement, mais
    // trop haut -> vitesse annulée en l'air. On cherche la frontière (posé à v~0).
    if (o.reached_ground && o.touchdown_speed > 1.0) lo = h;   // touche vite -> plus haut
    else hi = h;                                               // annulé en l'air -> plus bas
  }
  if (h_pdi_out) *h_pdi_out = best_h;
  return best_dv;
}

} // namespace fen::flight
