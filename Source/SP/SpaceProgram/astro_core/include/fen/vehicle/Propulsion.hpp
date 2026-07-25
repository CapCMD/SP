// fen/vehicle/Propulsion.hpp — filières de propulsion [GDD 5.12, 6.2-6.4]
//
// DEUX INVARIANTS STRUCTURANTS [GDD 19.1] :
//   1) énergie ≠ propulsion : une SOURCE fournit des watts, un PROPULSEUR
//      convertit watts + propergol en poussée. RTG = énergie pure ;
//      chimique = propulsion pure ; NEP = couplage des deux [GDD 5.12.1].
//   2) F = 2·η·P/ve : à puissance fixée, monter l'Isp BAISSE la poussée.
//      Personne n'a haute poussée ET haut rendement sans puissance colossale.
// Le Vehicle.hpp existant modélise le chimique (Engine). Ce fichier ajoute la
// grille des filières avancées et leurs contraintes couplées (thermique,
// blindage) — les fourchettes sont celles du GDD 6.4, jamais dépassées.
#pragma once
#include <cmath>
#include <string>
#include "fen/core/Constants.hpp"
#include "fen/env/Thermal.hpp"

namespace fen::vehicle {

// Paliers énergie/propulsion [GDD 5.12.3]. L'ordre EST la progression d'arbre.
enum class PropTier {
  Chemical = 0, Solar = 1, Rtg = 2, Electric = 3, Fission = 4,
  Ntp = 5, Nep = 6, Fusion = 7, Antimatter = 8,
};

enum class PropKind { EnergySource, Thruster, Coupled };

// Régime de manœuvre [GDD 6.3] : décide du MODÈLE de trajectoire.
//   Impulsive : burns brefs (chimique, NTP) — Lambert/coniques valides.
//   Continuous: spirales, semaines de poussée (électrique, NEP) — incapable
//               de décoller, d'atterrir ou d'insérer brutalement.
enum class BurnRegime { Impulsive, Continuous };

struct PropulsionClass {
  PropTier    tier{};
  PropKind    kind{};
  BurnRegime  regime{};
  const char* name{};
  double isp_min_s{}, isp_max_s{};       // fourchette GDD 6.4 — bornes dures
  double thrust_min_n{}, thrust_max_n{}; // par moteur, ordre de grandeur
  bool   needs_power{};                  // vrai -> F plafonnée par P dispo
  bool   nuclear{};                      // vrai -> shadow shield + radiateurs
};

// LES FILIÈRES DE POUSSÉE, une par LIGNE du tableau [GDD 6.4]. La v1 en
// agrégeait plusieurs (« Chimique » couvrait solide et liquide, « Électrique »
// couvrait Hall et grille) : or le GDD les sépare parce que leurs fourchettes
// d'Isp et de poussée ne se recouvrent pas, et que le choix entre les deux EST
// un arbitrage de conception.
enum class PropFamily {
  ChemicalSolid = 0, ChemicalLiquid, ElectricHall, ElectricGridded,
  Ntp, Nep, Fusion, Antimatter,
};

// LA GRILLE [GDD 6.4]. Constantes de modèle, pas de gameplay : toute valeur
// hors fourchette est un bug de données, pas un choix de design.
struct PropFamilyClass {
  PropFamily  family{};
  PropTier    tier{};                    // palier de l'arbre [GDD 5.12.3]
  PropKind    kind{};
  BurnRegime  regime{};
  const char* name{};
  double isp_min_s{}, isp_max_s{};
  double thrust_min_n{}, thrust_max_n{}; // par moteur, ordre de grandeur
  bool   needs_power{};
  bool   nuclear{};
  const char* limiting_factor{};         // « facteur limitant dominant » [6.4]
};

inline constexpr PropFamilyClass PROP_FAMILIES[] = {
  {PropFamily::ChemicalSolid,   PropTier::Chemical,   PropKind::Thruster,
   BurnRegime::Impulsive,  "Chimique solide",          250.0,   280.0,
   1.0e5, 1.0e7, false, false, "Isp faible -> ratio de masse"},
  {PropFamily::ChemicalLiquid,  PropTier::Chemical,   PropKind::Thruster,
   BurnRegime::Impulsive,  "Chimique liquide",         300.0,   460.0,
   1.0e4, 1.0e7, false, false, "ratio de masse pour missions lointaines"},
  {PropFamily::ElectricHall,    PropTier::Electric,   PropKind::Thruster,
   BurnRegime::Continuous, "Electrique (Hall)",       1500.0,  3000.0,
   0.1,   1.0,   true,  false, "puissance disponible, poussee"},
  {PropFamily::ElectricGridded, PropTier::Electric,   PropKind::Thruster,
   BurnRegime::Continuous, "Electrique (grille/ion)", 3000.0, 10000.0,
   0.01,  0.5,   true,  false, "puissance, duree de poussee"},
  {PropFamily::Ntp,             PropTier::Ntp,        PropKind::Thruster,
   BurnRegime::Impulsive,  "NTP",                      850.0,  1000.0,
   1.0e4, 1.0e5, false, true,  "masse seche, blindage, qualification"},
  {PropFamily::Nep,             PropTier::Nep,        PropKind::Coupled,
   BurnRegime::Continuous, "NEP",                     2000.0, 10000.0,
   1.0,   50.0,  true,  true,  "rejet thermique (radiateurs), masse reacteur"},
  {PropFamily::Fusion,          PropTier::Fusion,     PropKind::Coupled,
   BurnRegime::Continuous, "Fusion",                  1.0e4,   1.0e6,
   0.1,   1.0e4, true,  true,  "bilan net, confinement, materiaux"},
  {PropFamily::Antimatter,      PropTier::Antimatter, PropKind::Coupled,
   BurnRegime::Continuous, "Antimatiere",             1.0e5,   1.0e7,
   0.1,   1.0e3, true,  true,  "production d antimatiere, confinement, cout"},
};

inline const PropFamilyClass* prop_family(PropFamily f) {
  for (const auto& c : PROP_FAMILIES) if (c.family == f) return &c;
  return nullptr;
}

// ═══ LES SOURCES D'ÉNERGIE PURES [GDD 5.12.1, 5.12.5, 5.12.6, 5.12.8] ═══
// « Une erreur de design fréquente consiste à confondre produire de l'énergie
// et produire de la poussée. » Ces trois paliers ne poussent PAS : ils
// alimentent. Ils manquaient entièrement à la grille.
struct EnergySourceClass {
  PropTier    tier{};
  const char* name{};
  double specific_power_min_w_per_kg{};  // puissance électrique par kg de source
  double specific_power_max_w_per_kg{};
  double eta_thermal{};                  // rendement de conversion
  bool   nuclear{};
  bool   falls_off_with_distance{};      // solaire : loi en 1/d² [GDD 5.12.5]
  double half_life_years{};              // 0 = pas de décroissance
  const char* limiting_factor{};
};

inline constexpr EnergySourceClass ENERGY_SOURCES[] = {
  // Panneaux + batteries : ~100-200 W/kg au niveau du générateur à 1 UA.
  {PropTier::Solar,   "Solaire + batteries",  50.0,  200.0, 0.30, false, true,  0.0,
   "distance au Soleil (1/d2), masse de stockage"},
  // MMRTG : 110 W pour ~45 kg -> ~2,4 W/kg. Pu-238 : demi-vie 87,7 ans.
  {PropTier::Rtg,     "RTG (radioisotopes)",   1.5,    5.0, 0.07, true,  false, 87.7,
   "disponibilite du Pu-238, decroissance, chaleur residuelle"},
  // Réacteur spatial : Kilopower ~10 kW pour ~1500 kg (6,7 W/kg) ; les concepts
  // de forte puissance visent 20-100 W/kg, blindage et radiateurs compris.
  {PropTier::Fission, "Reacteur de fission",   5.0,  100.0, 0.30, true,  false, 0.0,
   "blindage, radiateurs, certification"},
};

inline const EnergySourceClass* energy_source(PropTier t) {
  for (const auto& c : ENERGY_SOURCES) if (c.tier == t) return &c;
  return nullptr;
}

// Masse de la source nécessaire pour fournir `p_watts`, au meilleur et au pire
// de la fourchette. C'est ce qui entre dans le budget de masse [GDD 6.1] : une
// source n'est jamais gratuite.
inline double source_mass_kg(PropTier t, double p_watts, bool optimistic = false) {
  const EnergySourceClass* s = energy_source(t);
  if (!s || p_watts <= 0.0) return 0.0;
  const double sp = optimistic ? s->specific_power_max_w_per_kg
                               : s->specific_power_min_w_per_kg;
  return sp > 0.0 ? p_watts / sp : 0.0;
}

// Puissance d'un RTG après `years` [GDD 5.12.6] : la décroissance radioactive
// n'est pas une option de gameplay, c'est la demi-vie du Pu-238.
inline double rtg_power_after(double p0_watts, double years) {
  const EnergySourceClass* s = energy_source(PropTier::Rtg);
  if (!s || s->half_life_years <= 0.0) return p0_watts;
  return p0_watts * std::pow(0.5, years / s->half_life_years);
}

// Puissance solaire disponible à la distance `r_m` du Soleil [GDD 5.12.5] :
// loi en 1/d², la même que le flux thermique — un seul Soleil.
inline double solar_power_at(double p0_watts_at_1au, double r_m) {
  if (r_m <= 0.0) return 0.0;
  const double ratio = cst::AU / r_m;
  return p0_watts_at_1au * ratio * ratio;
}

// --- compatibilité : la vue par PALIER (une classe représentative) -----------
inline constexpr PropulsionClass PROP_CLASSES[] = {
  {PropTier::Chemical,  PropKind::Thruster,     BurnRegime::Impulsive,
   "Chimique",            250.0,   460.0, 1.0e4, 1.0e7, false, false},
  {PropTier::Electric,  PropKind::Thruster,     BurnRegime::Continuous,
   "Electrique (Hall/ion)", 1500.0, 10000.0, 0.01, 1.0,  true,  false},
  {PropTier::Ntp,       PropKind::Thruster,     BurnRegime::Impulsive,
   "NTP",                  850.0,  1000.0, 1.0e4, 1.0e5, false, true},
  {PropTier::Nep,       PropKind::Coupled,      BurnRegime::Continuous,
   "NEP",                 2000.0, 10000.0, 1.0,   50.0,  true,  true},
  {PropTier::Fusion,    PropKind::Coupled,      BurnRegime::Continuous,
   "Fusion",             1.0e4,   1.0e6,  0.1,   1.0e4, true,  true},
  {PropTier::Antimatter,PropKind::Coupled,      BurnRegime::Continuous,
   "Antimatiere",        1.0e5,   1.0e7,  0.1,   1.0e3, true,  true},
};

inline const PropulsionClass* prop_class(PropTier t) {
  for (const auto& c : PROP_CLASSES) if (c.tier == t) return &c;
  return nullptr;   // Solar/Rtg/Fission : sources pures -> voir energy_source()
}

// --- Le cœur du compromis [GDD 6.2] ------------------------------------------
// Poussée d'un propulseur alimenté : F = 2·η·P/ve. C'est une IDENTITÉ, pas un
// réglage : toute filière `needs_power` DOIT passer par ici.
inline double powered_thrust(double eta, double p_watts, double ve_mps) {
  return 2.0 * eta * p_watts / ve_mps;   // N
}
inline double jet_power(double mdot_kgs, double ve_mps) {
  return 0.5 * mdot_kgs * ve_mps * ve_mps;   // W
}

// --- Système de propulsion électrique/NEP complet ----------------------------
// La poussée ne sort JAMAIS seule : elle traîne une source, des radiateurs et
// du blindage. C'est ce bloc entier qui entre dans le budget de masse.
struct PoweredPropulsion {
  double p_source_w{};       // puissance électrique DISPONIBLE propulsion
  double eta{0.6};           // rendement jet (Hall ~0.5, ion ~0.7)
  double isp_s{};            // choix de conception dans la fourchette
  double eta_th_source{0.3}; // rendement thermique de la source (réacteur)
  bool   source_is_reactor{false};

  double ve() const     { return isp_s * cst::G0; }
  double thrust() const { return powered_thrust(eta, p_source_w, ve()); }
  double mdot() const   { return thrust() / ve(); }

  // Chaleur à rejeter : pertes du jet + (si réacteur) chaleur résiduelle amont.
  double waste_heat_w() const {
    const double jet_losses = (1.0 - eta) * p_source_w;
    const double source = source_is_reactor
        ? env::reactor_waste_heat(p_source_w, eta_th_source) : 0.0;
    return jet_losses + source;
  }
  // Masse de radiateurs induite — le facteur dominant NEP [GDD 5.12.10].
  double radiator_mass_kg(const env::RadiatorSpec& spec = {}) const {
    return env::size_radiator(waste_heat_w(), spec).mass_kg;
  }
};

// Vérification de régime [GDD 6.3] : interdit les incohérences classiques
// (un ionique qui "décolle", une NEP qui freine brutalement).
inline bool can_liftoff(double thrust_n, double mass_kg, double surface_g) {
  return thrust_n > mass_kg * surface_g;   // T/W > 1 exigé au décollage
}
inline bool impulsive_ok(double thrust_n, double mass_kg) {
  return thrust_n / (mass_kg * cst::G0) >= 0.1;   // T/W >= 0.1 [GDD 6.3]
}

} // namespace fen::vehicle
