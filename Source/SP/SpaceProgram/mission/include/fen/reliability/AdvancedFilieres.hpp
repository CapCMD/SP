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
#include "fen/env/Micrometeoroid.hpp"

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
// VIE DE CŒUR À PLEINE PUISSANCE — la grandeur sous laquelle un réacteur spatial
// se spécifie réellement. SP-100 était dimensionné pour 7 années-pleine-puissance,
// Kilopower vise 15, les concepts de puissance de surface 10. On retient la
// borne BASSE et documentée [GDD 12.5] : un réacteur qui tourne 7 ans à pleine
// charge a consommé sa vie nominale.
inline constexpr double CORE_FULL_POWER_YEARS = 7.0;

// Fraction de vie consommée par une mission qui fait tourner le réacteur en
// continu pendant `days`. Un NTP, lui, ne brûle presque rien : il tire quelques
// minutes — c'est son CYCLAGE THERMIQUE qui le tue, et il est déjà dans `is_ntp`.
inline double burnup_from_days(double days, bool continuous) {
  if (!continuous || days <= 0.0) return 0.0;
  return days / (CORE_FULL_POWER_YEARS * 365.25);
}

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

// Forme PRIMITIVE : la densité de particules est donnée. C'est elle qui se
// branche, parce que le consommateur (`assess_multistage`) est une fonction PURE
// — lui passer l'objet d'environnement entier ferait entrer l'état de l'agence
// dans une évaluation de conception.
inline double radiator_capacity_at_density(const RadiatorWear& rad,
                                           double density_per_m3,
                                           double exposure_days,
                                           double v_rel_ms = 1.0e4) {
  if (rad.area_m2 <= 0.0 || exposure_days <= 0.0 || density_per_m3 <= 0.0) return 1.0;
  // Perforations attendues = flux de rencontres × vulnérabilité.
  const double punctures =
      density_per_m3 * rad.area_m2 * v_rel_ms * exposure_days * cst::DAY * rad.vulnerability;
  return std::exp(-punctures);
}

inline double radiator_capacity_fraction(const RadiatorWear& rad,
                                          const env::DebrisEnvironment& debris,
                                          const env::Corridor& corridor,
                                          double exposure_days,
                                          double v_rel_ms = 1.0e4) {
  return radiator_capacity_at_density(rad, debris.spatial_density(corridor),
                                      exposure_days, v_rel_ms);
}

// Un radiateur érodé rejette moins : si la capacité tombe sous la charge
// thermique à évacuer, la mission est thermiquement bloquée [GDD 6.5].
inline bool thermal_still_ok(double capacity_fraction, double nominal_reject_w,
                             double heat_load_w) {
  return capacity_fraction * nominal_reject_w >= heat_load_w;
}

// ═══ 2 bis. PERFORATION SUB-MILLIMÉTRIQUE [GDD 12.4, 6.5] ═══
// L'AUTRE mécanisme radiateur de 12.4, celui qui était déclaré non modélisé
// « faute d'un modèle de flux que rien dans le dépôt ne porte ». Il en porte un
// maintenant : `env/Micrometeoroid.hpp` (Grün 1985 + Cour-Palais). Ce qui suit
// n'est plus de la physique — c'est la CONSÉQUENCE de mission.
//
// Le radiateur a été TAILLÉ pour une endurance : sa surface excédentaire tolère
// un nombre calculé de circuits morts. La question de mission n'est donc pas
// « combien en meurt-il » mais « en meurt-il plus que ce qu'on a payé ». Le
// nombre de morts est poissonien ; on répond par sa queue.

// P(K ≤ k) pour K ~ Poisson(moyenne). Somme exacte tant qu'elle a un sens,
// approximation normale au-delà — la somme de 200 termes ne dit rien de plus.
inline double poisson_cdf(double mean, double k) {
  if (k < 0.0) return 0.0;
  if (mean <= 0.0) return 1.0;
  const double kf = std::floor(k);
  if (mean > 200.0) {
    const double z = (kf + 0.5 - mean) / std::sqrt(mean);
    // ERFC, PAS 1 + ERF. Avec `0,5·(1 + erf(z/√2))`, erf sature à −1,0 dès
    // |z| > 6 et la queue basse rend un ZÉRO EXACT — c'est-à-dire un verdict
    // binaire déguisé en probabilité, exactement ce que [GDD 12.4] interdit
    // (« jamais un verdict binaire décrété »). Mesuré sur un vol de 23 ans :
    // z = −22,8, la vraie valeur est ~1e−115, largement représentable, et
    // l'annulation la ramenait à 0,000e+00. erfc ne s'annule pas.
    const double a = -z / std::sqrt(2.0);
    return z < 0.0 ? 0.5 * std::erfc(a) : 1.0 - 0.5 * std::erfc(-a);
  }
  double terme = std::exp(-mean);
  double somme = terme;
  const long long imax = static_cast<long long>(kf);
  for (long long i = 1; i <= imax; ++i) {
    terme *= mean / static_cast<double>(i);
    somme += terme;
    if (terme < 1.0e-18 * somme) break;
  }
  return std::clamp(somme, 0.0, 1.0);
}

// PROBABILITÉ QUE LE RADIATEUR TIENNE SA CHARGE jusqu'au bout d'un vol de
// `flight_days`, sachant qu'il a été dimensionné pour `design_days`.
// À flight_days = design_days elle vaut ~0,999 (c'est le sens du 3σ de
// dimensionnement) ; elle s'effondre quand on vole nettement plus longtemps que
// ce pour quoi on a construit. Un vol PLUS COURT ne rapporte rien : on ne rembourse
// pas la marge, on la garde.
inline double radiator_load_survival(double area_m2, double flight_days,
                                     double design_days, double wall_mm,
                                     double segment_area_m2 = env::RADIATOR_SEGMENT_AREA_M2,
                                     const env::WallMaterial* material = nullptr) {
  if (area_m2 <= 0.0 || flight_days <= 0.0) return 1.0;
  const double toleres = env::radiator_tolerated_dead(area_m2, design_days, wall_mm,
                                                      segment_area_m2, material);
  const double n = env::radiator_segment_count(area_m2, segment_area_m2);
  const double q = 1.0 - env::radiator_capacity_after(flight_days, wall_mm,
                                                      segment_area_m2, material);
  return poisson_cdf(n * q, toleres);
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
