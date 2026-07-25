// fen/reliability/AdvancedFilieres.hpp — DÉGRADATION DES FILIÈRES AVANCÉES [GDD 12.4]
//
// [GDD 12.4] « La base intègre les nouvelles filières de la branche 6, avec
// leurs mécanismes de dégradation PROPRES. » Le module de fiabilité de base
// (`Reliability.hpp`) porte des modificateurs GÉNÉRIQUES (vieillissement,
// environnement, durée). Ce fichier ajoute les mécanismes NOMMÉMENT désignés
// par 12.4, chacun avec sa physique — pas une pénalité forfaitaire :
//
//   . vieillissement des cœurs nucléaires (fission, NTP) ;
//   . dégradation des radiateurs (érosion, micrométéorites, cycles thermiques —
//     critique pour NEP/fusion, voir 6.5) ;
//   . sensibilité des systèmes de confinement antimatière (perte = catastrophe).
//
// Chaque fonction produit soit un MODIFICATEUR de fiabilité (à multiplier dans
// reliability::Modifiers), soit une PROBABILITÉ d'événement — jamais un verdict
// binaire décrété. Principe conservateur [GDD 12.5] : à information égale,
// l'hypothèse la moins flatteuse. Tous les paramètres sont DÉCLARÉS [GDD 6.8].
//
// Le mécanisme des radiateurs se BRANCHE sur l'environnement de débris déjà
// modélisé (env/Debris.hpp) : la même population qui menace une collision
// perfore aussi les tubes de radiateur. Un seul modèle d'environnement.
#pragma once
#include <algorithm>
#include <cmath>

#include "fen/core/Constants.hpp"
#include "fen/env/Debris.hpp"

namespace fen::reliability {

// ═══ 1. VIEILLISSEMENT DES CŒURS NUCLÉAIRES [GDD 12.4, 5.12.8-9] ═══
// Un cœur de fission perd sa marge de réactivité avec le BURNUP (énergie
// thermique intégrée) et, plus lentement, avec le temps calendaire (corrosion,
// fluage sous flux neutronique). Modèle : fiabilité multiplicative qui décroît
// de façon exponentielle en fraction de vie consommée.
//   burnup_fraction : énergie produite / énergie nominale de fin de vie [0..1] ;
//   calendar_years  : âge du cœur, indépendamment de l'usage.
// Rendu : facteur de fiabilité (1 = neuf, décroît). NTP dégrade plus vite que la
// fission de puissance : les cycles de chauffe/refroidissement de l'ergol
// fissurent le cœur (déclaré via `is_ntp`).
inline double nuclear_core_reliability(double burnup_fraction, double calendar_years,
                                       bool is_ntp = false) {
  const double b = std::clamp(burnup_fraction, 0.0, 2.0);
  // Constante de burnup : à pleine vie (b=1) le NTP a perdu bien plus que la
  // fission de puissance (cyclage thermique de l'ergol).
  const double k_burn = is_ntp ? 1.2 : 0.5;
  const double k_cal  = 0.02;                 // ~2 %/an de vieillissement calendaire
  return std::exp(-(k_burn * b + k_cal * std::max(0.0, calendar_years)));
}

// ═══ 2. DÉGRADATION DES RADIATEURS [GDD 12.4, 6.5] ═══
// « Facteur DOMINANT de masse et de vulnérabilité » pour NEP et fusion. Chaque
// micrométéorite ou débris qui perce un panneau retire sa capacité de rejet.
// La FRACTION DE CAPACITÉ RESTANTE suit une décroissance de survie poissonienne
// sur le flux de perforation :
//     capacité(t) = exp( − flux_perforation · aire · vulnérabilité · t )
// Le flux vient de l'environnement de débris (même population que les
// collisions). `vulnerability` = fraction de la surface dont une perforation
// tue le tube (radiateur bien compartimenté -> faible).
struct RadiatorWear {
  double area_m2{};
  double vulnerability{0.15};   // 0,15 = perte locale, panneaux redondants
};

inline double radiator_capacity_fraction(const RadiatorWear& rad,
                                          const env::DebrisEnvironment& debris,
                                          const env::Corridor& corridor,
                                          double exposure_days,
                                          double v_rel_ms = 1.0e4) {
  if (rad.area_m2 <= 0.0 || exposure_days <= 0.0) return 1.0;
  const double n = debris.spatial_density(corridor);        // objets/m³
  // Perforations attendues = flux de rencontres × vulnérabilité.
  const double punctures =
      n * rad.area_m2 * v_rel_ms * exposure_days * cst::DAY * rad.vulnerability;
  return std::exp(-punctures);
}

// Un radiateur érodé rejette moins : si la capacité tombe sous la charge
// thermique à évacuer, la mission est thermiquement bloquée [GDD 6.5].
inline bool thermal_still_ok(double capacity_fraction, double nominal_reject_w,
                             double heat_load_w) {
  return capacity_fraction * nominal_reject_w >= heat_load_w;
}

// ═══ 3. CONFINEMENT ANTIMATIÈRE [GDD 12.4, 5.12.12, 19.3] ═══
// « Perte de confinement = ÉVÉNEMENT CATASTROPHIQUE. » On ne modélise donc PAS
// une fiabilité qui grignote une performance : on modélise la PROBABILITÉ
// qu'aucune perte n'ait lieu sur la durée de mission, comme un processus de
// Poisson. Une seule défaillance déclenche une catastrophe (Severity 5).
//   base_rate_per_day : taux de perte de confinement d'un système NEUF ;
//   quality [0..1]     : maturité du confinement (1 = état de l'art de fin
//                        d'arbre). Un confinement médiocre multiplie le taux.
// Rendu : probabilité de SURVIE (aucune perte) sur la mission.
inline double antimatter_confinement_survival(double mission_days,
                                              double quality,
                                              double base_rate_per_day = 1.0e-3) {
  if (mission_days <= 0.0) return 1.0;
  const double q = std::clamp(quality, 0.05, 1.0);
  const double rate = base_rate_per_day / q;     // qualité basse -> taux élevé
  return std::exp(-rate * mission_days);
}

// Probabilité de la CATASTROPHE de confinement sur la mission — le complément.
inline double antimatter_confinement_loss_prob(double mission_days, double quality,
                                               double base_rate_per_day = 1.0e-3) {
  return 1.0 - antimatter_confinement_survival(mission_days, quality, base_rate_per_day);
}

} // namespace fen::reliability
