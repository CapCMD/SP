// fen/astro/Transfers.hpp
// OUTILS ANALYTIQUES DE CONCEPTION (modèle 2 corps, impulsions).
//
// Statut doctrinal : ce sont des CALCULATRICES, pas des solveurs de gameplay.
// Elles répondent à "combien coûte X ?", jamais à "que dois-je faire ?".
// Il n'existe nulle part dans le code de fonction `circularize()` ou
// `auto_target()` : ce serait faire le travail du joueur.
#pragma once
#include <cmath>
#include <limits>
#include "fen/core/Constants.hpp"

namespace fen::astro {

// --- vis-viva ---------------------------------------------------------------
inline double vis_viva(double r, double a, double mu) {
  return std::sqrt(mu * (2.0 / r - 1.0 / a));
}
inline double v_circular(double r, double mu) { return std::sqrt(mu / r); }
inline double v_escape(double r, double mu)   { return std::sqrt(2.0 * mu / r); }

// --- Hohmann ----------------------------------------------------------------
struct Hohmann {
  double dv1{}, dv2{}, dv_total{}, tof{}, a_transfer{};
};
inline Hohmann hohmann(double r1, double r2, double mu) {
  Hohmann h;
  h.a_transfer = 0.5 * (r1 + r2);
  const double v1 = v_circular(r1, mu);
  const double v2 = v_circular(r2, mu);
  const double vp = vis_viva(r1, h.a_transfer, mu);
  const double va = vis_viva(r2, h.a_transfer, mu);
  h.dv1 = vp - v1;
  h.dv2 = v2 - va;
  h.dv_total = std::fabs(h.dv1) + std::fabs(h.dv2);
  h.tof = cst::PI * std::sqrt(h.a_transfer * h.a_transfer * h.a_transfer / mu);
  return h;
}

// --- bi-elliptique (avantageux si r2/r1 > ~11.94) ---------------------------
struct BiElliptic { double dv1{}, dv2{}, dv3{}, dv_total{}, tof{}; };
inline BiElliptic bi_elliptic(double r1, double r2, double rb, double mu) {
  BiElliptic b;
  const double a1 = 0.5 * (r1 + rb);
  const double a2 = 0.5 * (r2 + rb);
  b.dv1 = vis_viva(r1, a1, mu) - v_circular(r1, mu);
  b.dv2 = vis_viva(rb, a2, mu) - vis_viva(rb, a1, mu);
  b.dv3 = v_circular(r2, mu)   - vis_viva(r2, a2, mu);
  b.dv_total = std::fabs(b.dv1) + std::fabs(b.dv2) + std::fabs(b.dv3);
  b.tof = cst::PI * (std::sqrt(a1 * a1 * a1 / mu) + std::sqrt(a2 * a2 * a2 / mu));
  return b;
}

// --- changement de plan pur -------------------------------------------------
inline double dv_plane_change(double v, double di_rad) {
  return 2.0 * v * std::sin(0.5 * di_rad);
}
// --- manoeuvre combinée (rotation + variation de module) --------------------
// C'est LA découverte imposée au joueur dans la mission tutoriel :
// combiner l'insertion GEO et le changement de plan économise ~1.15 km/s,
// et c'est la différence entre une mission qui boucle et une qui ne boucle pas.
inline double dv_combined(double v_initial, double v_final, double di_rad) {
  return std::sqrt(v_initial * v_initial + v_final * v_final
                   - 2.0 * v_initial * v_final * std::cos(di_rad));
}

// --- phasage ----------------------------------------------------------------
inline double synodic_period(double T1, double T2) {
  return 1.0 / std::fabs(1.0 / T1 - 1.0 / T2);
}
// Angle de phase de départ pour un transfert de Hohmann entre orbites
// circulaires coplanaires : phi = pi - n2 * t_transfert  [rad].
inline double hohmann_phase_angle(double r1, double r2, double mu) {
  const double a_t = 0.5 * (r1 + r2);
  const double tof = cst::PI * std::sqrt(a_t * a_t * a_t / mu);
  const double n2  = std::sqrt(mu / (r2 * r2 * r2));
  return cst::PI - n2 * tof;
}

// --- équation de la fusée ---------------------------------------------------
inline double tsiolkovsky_dv(double m0, double mf, double isp) {
  return isp * cst::G0 * std::log(m0 / mf);
}
inline double propellant_for_dv(double m0, double dv, double isp) {
  return m0 * (1.0 - std::exp(-dv / (isp * cst::G0)));
}
inline double m0_for_dv(double mf, double dv, double isp) {
  return mf * std::exp(dv / (isp * cst::G0));
}
inline double burn_duration(double m0, double dv, double isp, double thrust) {
  const double mp = propellant_for_dv(m0, dv, isp);
  const double mdot = thrust / (isp * cst::G0);
  return mp / mdot;
}

// --- énergie caractéristique de lancement -----------------------------------
inline double C3_from_vinf(double vinf) { return vinf * vinf; }
// Delta-v d'injection depuis une orbite de parking circulaire de rayon r_park.
inline double dv_injection(double r_park, double vinf, double mu) {
  return std::sqrt(vinf * vinf + 2.0 * mu / r_park) - v_circular(r_park, mu);
}
// Delta-v d'insertion depuis une hyperbole d'arrivée v_inf vers une ellipse
// (r_p, r_a) autour du corps cible.
inline double dv_insertion(double rp, double ra, double vinf, double mu) {
  const double v_hyp = std::sqrt(vinf * vinf + 2.0 * mu / rp);
  const double a_cap = 0.5 * (rp + ra);
  const double v_cap = vis_viva(rp, a_cap, mu);
  return v_hyp - v_cap;
}

// --- sphère d'influence (OUTIL patched-conic UNIQUEMENT) --------------------
// AVERTISSEMENT DOCTRINAL : la SOI n'a AUCUNE existence dans le propagateur de
// vérité. C'est une frontière de modèle, pas une frontière physique. Elle sert
// à découper un problème en coniques, et l'erreur de ce découpage est facturée.
inline double soi_radius(double a_body, double m_body, double m_central) {
  return a_body * std::pow(m_body / m_central, 0.4);
}
inline double hill_radius(double a_body, double m_body, double m_central) {
  return a_body * std::cbrt(m_body / (3.0 * m_central));
}

} // namespace fen::astro
