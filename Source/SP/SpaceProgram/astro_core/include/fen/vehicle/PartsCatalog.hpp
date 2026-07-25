// fen/vehicle/PartsCatalog.hpp — CATALOGUE DE PIÈCES [GDD 12.1]
//
// « Assemblage à partir d'un catalogue de pièces RÉELLES ou extrapolées à
// partir de LIGNÉES RÉELLES (moteurs type Merlin, RS-25, réservoirs, capsules),
// pas de pièces génériques abstraites. Les composants spéculatifs ne sont
// introduits que TARDIVEMENT, avec un niveau de confiance explicite, une
// incertitude, un domaine de validité et un statut de qualification. »
//
// Ce catalogue n'existait pas : les rares pièces réelles du projet étaient
// éparpillées (le RL10C-1 apparaissait dans `app/ares.hpp` pour sa fiche de
// fiabilité et dans `mission/Program.hpp` pour ses performances, sans lien).
//
// ═══ RÈGLES DE CE FICHIER ═══
// 1. Toute pièce porte sa LIGNÉE (l'engin réel dont elle est tirée) et sa
//    SOURCE. Une pièce sans provenance est invalide — même exigence que la base
//    de fiabilité [GDD 12.3.1].
// 2. Le NIVEAU DE CONFIANCE reprend l'échelle A–D de 12.3.2 : A = performances
//    publiées et volées, D = hypothèse de simulation.
// 3. Le STATUT DE QUALIFICATION est distinct de la maturité du monde : une
//    pièce peut exister sans être qualifiée pour un emploi donné [GDD 5.3].
// 4. Les pièces SPÉCULATIVES portent une incertitude explicite et un TRL bas.
//    Principe conservateur [GDD 12.5] : à information égale, l'hypothèse la
//    moins flatteuse.
// 5. Les valeurs sont des données PUBLIQUES d'ingénierie (poussée, Isp, masse),
//    pas du game design. Elles ne relèvent donc pas du report du chapitre 20.
#pragma once
#include <string>
#include <vector>

#include "fen/core/Constants.hpp"
#include "fen/vehicle/Propulsion.hpp"
#include "fen/vehicle/Vehicle.hpp"

namespace fen::vehicle {

// Échelle de confiance — identique à `reliability::Confidence` [GDD 12.3.2],
// dupliquée ici pour qu'astro_core reste indépendant de `mission/`.
enum class PartConfidence { A = 0, B = 1, C = 2, D = 3 };

enum class QualStatus {
  Flown = 0,        // volée, historique de vol exploitable
  GroundTested,     // qualifiée au banc, jamais volée
  Design,           // sur plan, non qualifiée
  Speculative,      // concept : incertitude large, TRL bas
};

// --- MOTEURS ----------------------------------------------------------------
struct EnginePart {
  const char* id;
  const char* name;
  const char* lineage;        // l'engin réel dont la pièce est tirée
  PropFamily  family;
  double thrust_vac_n;
  double isp_vac_s;
  double mass_kg;
  double isp_sl_s;            // 0 si non allumable au sol
  int    max_restarts;        // -1 = illimité en pratique
  PartConfidence confidence;
  QualStatus     status;
  int    trl;
  double perf_uncertainty;    // incertitude relative sur Isp/poussée
  const char* source;
};

// Données publiques d'ingénierie. Chaque ligne porte sa source.
inline const std::vector<EnginePart>& engine_catalog() {
  static const std::vector<EnginePart> v = {
    {"RL10C-1", "RL10C-1", "Centaur / Atlas V, Delta IV",
     PropFamily::ChemicalLiquid, 101800.0, 449.7, 190.0, 0.0, 15,
     PartConfidence::A, QualStatus::Flown, 9, 0.01,
     "fiches ULA/Aerojet publiques ; ~500 allumages en vol"},
    {"RS-25", "RS-25 (SSME)", "Navette spatiale, SLS",
     PropFamily::ChemicalLiquid, 2279000.0, 452.3, 3177.0, 366.0, 1,
     PartConfidence::A, QualStatus::Flown, 9, 0.01,
     "documentation NASA ; 135 vols navette"},
    {"MERLIN-1D-VAC", "Merlin 1D Vacuum", "Falcon 9 second etage",
     PropFamily::ChemicalLiquid, 981000.0, 348.0, 470.0, 0.0, 3,
     PartConfidence::A, QualStatus::Flown, 9, 0.02,
     "fiches SpaceX publiques ; historique Falcon 9"},
    {"MERLIN-1D-SL", "Merlin 1D", "Falcon 9 premier etage",
     PropFamily::ChemicalLiquid, 934000.0, 311.0, 470.0, 282.0, 1,
     PartConfidence::A, QualStatus::Flown, 9, 0.02,
     "fiches SpaceX publiques"},
    {"VULCAIN-2", "Vulcain 2", "Ariane 5 / Ariane 6 etage principal",
     PropFamily::ChemicalLiquid, 1359000.0, 431.0, 2100.0, 318.0, 1,
     PartConfidence::A, QualStatus::Flown, 9, 0.01,
     "documentation ArianeGroup/ESA"},
    {"VINCI", "Vinci", "Ariane 6 etage superieur",
     PropFamily::ChemicalLiquid, 180000.0, 457.0, 550.0, 0.0, 5,
     PartConfidence::B, QualStatus::Flown, 9, 0.02,
     "documentation ArianeGroup ; premiers vols Ariane 6"},
    {"AESTUS", "Aestus", "Ariane 5 ES etage superieur",
     PropFamily::ChemicalLiquid, 29600.0, 324.0, 111.0, 0.0, -1,
     PartConfidence::B, QualStatus::Flown, 9, 0.02,
     "donnees constructeur + vols Ariane 5 G"},
    {"RD-180", "RD-180", "Atlas V premier etage",
     PropFamily::ChemicalLiquid, 4152000.0, 338.0, 5480.0, 311.0, 1,
     PartConfidence::A, QualStatus::Flown, 9, 0.01,
     "documentation NPO Energomash / ULA"},
    {"F-1", "F-1", "Saturn V premier etage",
     PropFamily::ChemicalLiquid, 7770000.0, 304.0, 8400.0, 263.0, 1,
     PartConfidence::A, QualStatus::Flown, 9, 0.01,
     "rapports NASA Saturn V"},
    {"AJ10-190", "AJ10-190 (OMS)", "Navette spatiale, Orion",
     PropFamily::ChemicalLiquid, 26700.0, 316.0, 118.0, 0.0, -1,
     PartConfidence::A, QualStatus::Flown, 9, 0.01,
     "documentation Aerojet ; systeme de manoeuvre orbitale navette"},
    {"SRB-P80", "P80 (propulseur solide)", "Vega premier etage",
     PropFamily::ChemicalSolid, 2261000.0, 280.0, 7330.0, 0.0, 0,
     PartConfidence::B, QualStatus::Flown, 9, 0.02,
     "documentation Avio/ESA — masse structure a vide"},
    // --- électrique : la poussée est PLAFONNÉE par la puissance [GDD 6.2] ----
    {"NSTAR", "NSTAR (ion a grilles)", "Deep Space 1, Dawn",
     PropFamily::ElectricGridded, 0.092, 3120.0, 8.2, 0.0, -1,
     PartConfidence::A, QualStatus::Flown, 9, 0.02,
     "NASA Dawn / DS1 ; >50 000 h de fonctionnement cumule"},
    {"NEXT-C", "NEXT-C (ion a grilles)", "DART, lignee NSTAR",
     PropFamily::ElectricGridded, 0.236, 4190.0, 13.5, 0.0, -1,
     PartConfidence::A, QualStatus::Flown, 9, 0.03,
     "NASA GRC ; qualifie et vole sur DART"},
    {"SPT-100", "SPT-100 (Hall)", "plateformes telecom GEO",
     PropFamily::ElectricHall, 0.083, 1600.0, 3.5, 0.0, -1,
     PartConfidence::A, QualStatus::Flown, 9, 0.03,
     "Fakel / OKB ; des centaines d'unites en vol"},
    {"BHT-6000", "BHT-6000 (Hall 6 kW)", "lignee Busek/PPE",
     PropFamily::ElectricHall, 0.400, 2200.0, 12.0, 0.0, -1,
     PartConfidence::B, QualStatus::GroundTested, 7, 0.08,
     "essais au sol publies ; pas encore d'historique de vol"},
    // --- nucléaire : essais au sol historiques, jamais vole ------------------
    {"NERVA-NRX", "NERVA NRX (NTP)", "programme Rover/NERVA (essais au sol)",
     PropFamily::Ntp, 334000.0, 850.0, 6800.0, 0.0, 1,
     PartConfidence::B, QualStatus::GroundTested, 5, 0.10,
     "rapports AEC/NASA 1964-1969 ; jamais vole"},
    // --- spéculatif : incertitude LARGE, confiance basse [GDD 12.5] ----------
    {"NEP-1MW", "Propulseur NEP 1 MW", "concept, lignee Hall haute puissance",
     PropFamily::Nep, 25.0, 5000.0, 900.0, 0.0, -1,
     PartConfidence::D, QualStatus::Speculative, 2, 0.50,
     "estimation raisonnee : aucun essai, extrapolation de puissance"},
    {"FUSION-DD", "Tuyere magnetique a fusion", "concept",
     PropFamily::Fusion, 100.0, 50000.0, 25000.0, 0.0, -1,
     PartConfidence::D, QualStatus::Speculative, 1, 0.80,
     "hypothese de simulation : bilan net non demontre"},
  };
  return v;
}

inline const EnginePart* find_engine(const std::string& id) {
  for (const auto& e : engine_catalog()) if (id == e.id) return &e;
  return nullptr;
}

// Conversion vers le moteur du budget de masse [GDD 6.1] : le catalogue DÉCRIT,
// `Vehicle.hpp` CALCULE. Aucune duplication de la physique.
inline Engine to_engine(const EnginePart& p) {
  Engine e;
  e.id = p.id;
  e.thrust_vac = p.thrust_vac_n;
  e.isp_vac = p.isp_vac_s;
  e.mass = p.mass_kg;
  e.max_restarts = p.max_restarts;
  e.heritage = (p.status == QualStatus::Flown) ? 1.0
             : (p.status == QualStatus::GroundTested) ? 0.5
             : (p.status == QualStatus::Design) ? 0.2 : 0.05;
  return e;
}

// --- RÉSERVOIRS -------------------------------------------------------------
// Un réservoir se décrit par sa FRACTION SÈCHE (masse structure / masse
// d'ergols) : c'est l'invariant d'échelle, et c'est ce qui entre dans
// Tsiolkovsky via la masse sèche [GDD 6.1].
struct TankPart {
  const char* id;
  const char* name;
  const char* lineage;
  const char* propellant;
  double dry_fraction;        // structure / ergols
  double density_kg_m3;       // densité moyenne du couple, pondérée par le MR
  double residual_fraction;   // imbrûlés + ullage
  PartConfidence confidence;
  const char* source;
};

inline const std::vector<TankPart>& tank_catalog() {
  static const std::vector<TankPart> v = {
    {"TANK-LOX-LH2", "Reservoir cryogenique LOX/LH2", "Centaur, DCSS",
     "LOX/LH2", 0.11, 360.0, 0.02, PartConfidence::A,
     "fractions structurelles publiees des etages cryogeniques"},
    {"TANK-LOX-RP1", "Reservoir LOX/RP-1", "Falcon 9, Atlas",
     "LOX/RP-1", 0.045, 1030.0, 0.02, PartConfidence::A,
     "etages LOX/kerosene : fraction seche ~4-5 %"},
    {"TANK-LOX-CH4", "Reservoir LOX/methane", "lignee Raptor / BE-4",
     "LOX/CH4", 0.055, 830.0, 0.02, PartConfidence::B,
     "extrapolation des etages LOX/RP-1, densite du couple ajustee"},
    {"TANK-STOCK", "Reservoir stockable", "Ariane 5 EPS, plateformes GEO",
     "MMH/NTO", 0.09, 1180.0, 0.03, PartConfidence::A,
     "etages a ergols stockables : plus lourds, mais stockage long"},
    {"TANK-XE", "Reservoir de xenon", "Dawn, plateformes electriques",
     "Xe", 0.25, 1600.0, 0.01, PartConfidence::A,
     "reservoirs haute pression : fraction seche elevee"},
  };
  return v;
}

inline const TankPart* find_tank(const std::string& id) {
  for (const auto& t : tank_catalog()) if (id == t.id) return &t;
  return nullptr;
}

inline Tank to_tank(const TankPart& p, double propellant_mass_kg) {
  Tank t;
  t.propellant_mass = propellant_mass_kg;
  t.dry_fraction = p.dry_fraction;
  t.propellant_density = p.density_kg_m3;
  t.residual_fraction = p.residual_fraction;
  return t;
}

// --- CAPSULES ET HABITATS ---------------------------------------------------
// Le rayon de nez et le coefficient de traînée alimentent DIRECTEMENT
// `flight/Reentry.hpp` : une capsule n'est pas un décor, c'est un corps de
// rentrée avec son corridor.
struct CapsulePart {
  const char* id;
  const char* name;
  const char* lineage;
  double dry_mass_kg;
  int    crew;
  double cd_hypersonic;
  double area_m2;
  double nose_radius_m;
  double lift_to_drag;
  double max_entry_g;          // limite structurale/équipage retenue
  PartConfidence confidence;
  QualStatus status;
  const char* source;
};

inline const std::vector<CapsulePart>& capsule_catalog() {
  static const std::vector<CapsulePart> v = {
    {"APOLLO-CM", "Module de commande Apollo", "Apollo",
     5560.0, 3, 1.35, 12.02, 4.69, 0.30, 12.0,
     PartConfidence::A, QualStatus::Flown,
     "rapports NASA Apollo ; bouclier 3,91 m"},
    {"SOYUZ-SA", "Module de descente Soyouz", "Soyouz",
     2900.0, 3, 1.40, 3.80, 2.20, 0.25, 9.0,
     PartConfidence::A, QualStatus::Flown,
     "documentation RKK Energia ; rentree balistique de secours a ~9 g"},
    {"ORION-CM", "Module d'equipage Orion", "Orion / Artemis",
     10400.0, 4, 1.30, 19.60, 6.04, 0.30, 12.0,
     PartConfidence::A, QualStatus::Flown,
     "documentation NASA ; bouclier 5,0 m, retour lunaire Artemis I"},
    {"DRAGON-2", "Crew Dragon", "Falcon 9 / ISS",
     9500.0, 4, 1.30, 10.75, 3.20, 0.28, 10.0,
     PartConfidence::B, QualStatus::Flown,
     "fiches SpaceX publiques ; diametre 3,7 m"},
    {"MARS-AEROSHELL", "Aerocoque martienne", "MSL / Mars 2020",
     3400.0, 0, 1.45, 15.90, 1.13, 0.24, 15.0,
     PartConfidence::A, QualStatus::Flown,
     "NASA JPL ; aerocoque 4,5 m, la plus grande volee vers Mars"},
  };
  return v;
}

inline const CapsulePart* find_capsule(const std::string& id) {
  for (const auto& c : capsule_catalog()) if (id == c.id) return &c;
  return nullptr;
}

// --- INVARIANTS DU CATALOGUE ------------------------------------------------
// Une pièce doit rester dans l'enveloppe de sa filière [GDD 6.4]. MAIS les deux
// colonnes du tableau 6.4 n'ont pas le même statut, et le GDD le dit :
//   . « Isp (s) » est donné comme une FOURCHETTE -> borne dure ;
//   . « Poussée (ORDRE) » est donné comme un ordre de grandeur -> une décade
//     de tolérance de chaque côté.
// Ce n'est pas une commodité : deux pièces RÉELLES du catalogue tombent hors
// des bornes littérales de la colonne poussée — le SPT-100 pousse 83 mN quand
// le tableau écrit 0,1 N pour le Hall, et le NERVA NRX poussait 334 kN quand le
// tableau écrit 10-100 kN pour le NTP. Entre une donnée mesurée et un ordre de
// grandeur rédactionnel, c'est la donnée qui gagne [GDD 12.3.1 : hiérarchie des
// sources]. Durcir la colonne poussée reviendrait à falsifier du matériel réel
// pour faire plaisir à un tableau.
inline constexpr double THRUST_ORDER_TOLERANCE = 10.0;

inline bool engine_within_family_envelope(const EnginePart& p) {
  const PropFamilyClass* f = prop_family(p.family);
  if (!f) return false;
  const bool isp_ok = p.isp_vac_s >= f->isp_min_s && p.isp_vac_s <= f->isp_max_s;
  const bool thrust_ok =
      p.thrust_vac_n >= f->thrust_min_n / THRUST_ORDER_TOLERANCE &&
      p.thrust_vac_n <= f->thrust_max_n * THRUST_ORDER_TOLERANCE;
  return isp_ok && thrust_ok;
}

// Vrai si la pièce sort des bornes LITTÉRALES de la colonne poussée : à
// afficher au joueur avancé, qui doit savoir quand le matériel réel déborde de
// la classification.
inline bool engine_outside_literal_thrust_band(const EnginePart& p) {
  const PropFamilyClass* f = prop_family(p.family);
  if (!f) return false;
  return p.thrust_vac_n < f->thrust_min_n || p.thrust_vac_n > f->thrust_max_n;
}

// [GDD 12.1, 12.5] : le spéculatif est TARDIF et INCERTAIN. Une pièce de
// confiance D qui prétendrait à une incertitude serrée serait une fausse
// précision — précisément ce que 12.3.4 interdit.
inline bool part_confidence_consistent(const EnginePart& p) {
  if (p.status == QualStatus::Speculative)
    return p.trl <= 4 && p.confidence >= PartConfidence::C && p.perf_uncertainty >= 0.20;
  if (p.status == QualStatus::Flown)
    return p.trl >= 8 && p.confidence <= PartConfidence::B;
  return true;
}

} // namespace fen::vehicle
