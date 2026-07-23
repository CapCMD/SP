// fen/env/Thermal.hpp — budget thermique [GDD 6.5]
//
// INVARIANT : dans le vide, la chaleur ne part QUE par rayonnement.
//   P_rejetée = ε·σ·A·(T_rad⁴ − T_env⁴)      (Stefan-Boltzmann)
// La gestion thermique est un VERROU DE MISSION au même titre que la propulsion
// [GDD 6.5] : une architecture peut être bloquée non par son moteur mais par
// l'impossibilité de dissiper sa chaleur. La surface de radiateur croît avec P
// et décroît en T⁴ — c'est LE facteur dominant de masse des filières NEP/fusion.
// Golden test : budget radiateur vs forme close (exact analytique) [carte P3].
#pragma once
#include <cmath>
#include "fen/core/Constants.hpp"

namespace fen::env {

// Flux solaire à distance r du Soleil (loi en 1/d²) [GDD 5.12.5].
inline double solar_flux(double r_m) {
  const double d = cst::AU / r_m;
  return cst::SOLAR_IRRADIANCE_1AU * d * d;   // W/m^2
}

// Température d'équilibre d'un corps sphérique passif (albédo a, émissivité ε).
inline double equilibrium_temp(double r_m, double albedo = 0.3, double eps = 1.0) {
  return std::pow(solar_flux(r_m) * (1.0 - albedo) / (4.0 * eps * cst::SIGMA_SB),
                  0.25);                       // K
}

// Puissance rayonnée par une surface A à T_rad face à un environnement T_env.
// T_env : ~3 K espace profond ; bien plus en orbite basse (Terre + albédo).
inline double radiated_power(double eps, double area_m2, double t_rad_k,
                             double t_env_k = 3.0) {
  const double t4 = t_rad_k * t_rad_k * t_rad_k * t_rad_k;
  const double e4 = t_env_k * t_env_k * t_env_k * t_env_k;
  return eps * cst::SIGMA_SB * area_m2 * (t4 - e4);   // W
}

// Surface de radiateur requise pour rejeter P — l'inverse exact de ci-dessus.
inline double radiator_area(double p_reject_w, double eps, double t_rad_k,
                            double t_env_k = 3.0) {
  const double t4 = t_rad_k * t_rad_k * t_rad_k * t_rad_k;
  const double e4 = t_env_k * t_env_k * t_env_k * t_env_k;
  return p_reject_w / (eps * cst::SIGMA_SB * (t4 - e4));  // m^2
}

// Chaleur résiduelle d'un réacteur produisant P_e électrique à rendement η_th :
// P_rejet = P_e·(1−η_th)/η_th. À η_th = 30 %, ~2.33× la puissance électrique
// produite part en chaleur [GDD 6.5]. AUCUN réacteur n'y échappe.
inline double reactor_waste_heat(double p_elec_w, double eta_th) {
  return p_elec_w * (1.0 - eta_th) / eta_th;   // W
}

// --- RadiatorSizing ----------------------------------------------------------
// Le radiateur est un COMPOSANT à part entière : masse, surface, vulnérabilité
// (micrométéorites, débris) et sa propre fiche de fiabilité [GDD 12.4].
struct RadiatorSpec {
  double t_run_k{500.0};        // température de fonctionnement du fluide
  double eps{0.85};             // émissivité de surface
  double areal_density{6.0};    // kg/m^2 (panneau + caloducs + fluide), modèle DÉCLARÉ
  double t_env_k{3.0};
  double redundancy_margin{1.15}; // surface excédentaire (perforations tolérées)
};

struct RadiatorSizing {
  double area_m2{};
  double mass_kg{};
};

inline RadiatorSizing size_radiator(double p_reject_w, const RadiatorSpec& s) {
  RadiatorSizing r;
  r.area_m2 = radiator_area(p_reject_w, s.eps, s.t_run_k, s.t_env_k)
              * s.redundancy_margin;
  r.mass_kg = r.area_m2 * s.areal_density;
  return r;
}

// --- ThermalBudget -----------------------------------------------------------
// Bilan couplé au budget de puissance : tout ce qui consomme finit en chaleur.
// `ok()` est un GATE de conception : s'il est faux, la mission est infaisable
// thermiquement, quel que soit le moteur [GDD 6.5, 19.6].
struct ThermalBudget {
  double p_reactor_waste_w{};   // reactor_waste_heat(...)
  double p_avionics_w{};        // dissipation bord (≈ conso électrique)
  double p_habitat_w{};         // métabolisme + équipements pressurisés
  double p_absorbed_solar_w{};  // flux solaire absorbé par les surfaces
  double p_radiator_capacity_w{}; // radiated_power(...) installé

  double total_load_w() const {
    return p_reactor_waste_w + p_avionics_w + p_habitat_w + p_absorbed_solar_w;
  }
  double margin_w() const { return p_radiator_capacity_w - total_load_w(); }
  bool ok() const { return margin_w() >= 0.0; }
};

} // namespace fen::env
