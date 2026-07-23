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

// LA GRILLE [GDD 6.4]. Constantes de modèle, pas de gameplay : toute valeur
// hors fourchette est un bug de données, pas un choix de design.
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
  return nullptr;   // Solar/Rtg/Fission : sources pures, pas de propulseur
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
