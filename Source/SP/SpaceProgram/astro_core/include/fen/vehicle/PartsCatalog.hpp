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
#include "fen/flight/Reentry.hpp"
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
// ⚠ TOUT NOUVEAU CHAMP VA EN QUEUE (piège n°83) : ce tableau est initialisé
// POSITIONNELLEMENT sur dix-huit lignes.
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

  // ═══════════════════════════════════════════════════════════════════════
  // APPROVISIONNEMENT — CE QUE COÛTE LA PIÈCE [GDD 12.1, 12.3.2, 12.5]
  // ═══════════════════════════════════════════════════════════════════════
  // LE TRIPLET EST OBLIGATOIRE, comme pour la fiabilité [GDD 12.3.2] : « pas de
  // précision artificielle » [12.3.4]. Et il n'est pas décoratif ici — c'est un
  // FAIT SUR LE MONDE que la plupart des prix unitaires de moteurs-fusées ne
  // sont PAS publiés. Écrire un nombre nu pour le Merlin 1D serait une
  // approximation déguisée en certitude, ce que [GDD 12.5] interdit ; écrire
  // {1, 2, 5} avec la confiance C et la mention « non publié par SpaceX » dit
  // exactement ce qu'on sait.
  //
  // La CONFIANCE porte sur le prix, PAS sur la pièce : le RD-180 est un moteur
  // volé des centaines de fois (confiance A en performance) dont le prix est
  // rapporté entre 9,9 et 70 M$ selon la source (confiance B en prix).
  double cost_lo_musd, cost_musd, cost_hi_musd;
  PartConfidence cost_confidence;
  const char* cost_source;

  // ═══ LE NŒUD D'ARBRE QUI LE QUALIFIE ═══ [GDD 5.4]
  // Même rôle que `Launcher::tech_id`, et pour la même raison : sans lui, la
  // branche 6 entière serait disponible dès la première mission. Vide = état de
  // l'art, disponible au départ.
  const char* tech_id;
  // Le réservoir que son couple d'ergols IMPOSE (`tank_catalog()`). Ce n'est pas
  // une décision d'architecte : un moteur LOX/LH2 ne se monte pas sur un
  // réservoir à xénon.
  // ⚠ VIDE POUR UN SOLIDE, et ce n'est pas un oubli : **un propulseur à poudre
  // n'a pas de réservoir**. Le bloc est coulé dans l'enveloppe, et le catalogue
  // compte déjà cette enveloppe dans `mass_kg` (« masse structure à vide »,
  // 7 330 kg pour le P80). Lui donner une fraction sèche la compterait deux
  // fois. Conséquence déclarée, et VRAIE des solides : on ne redimensionne pas
  // un propulseur à poudre, on en choisit un autre.
  const char* tank_id;
};

// CE QUE LE DÉVELOPPEMENT COÛTE : **RIEN ICI, ET C'EST VOULU.** C'est l'ARBRE
// qui paie la mise au point d'une filière (`TechNode::research_cost_musd`), et
// `tech_id` ci-dessus est ce qui l'exige. Porter un second coût de
// développement sur la pièce le facturerait DEUX FOIS, une fois en recherche et
// une fois par mission. Les chiffres de l'arbre sont déjà marqués provisoires —
// [GDD 20] diffère explicitement « les coûts et durées de recherche unitaires ».

// PRIX RETENU AU CALCUL — PRINCIPE CONSERVATEUR [GDD 12.5], et c'est le MIROIR
// exact de `reliability::evaluate` : là-bas une confiance basse tire vers la
// borne PESSIMISTE, qui est le BAS pour une fiabilité ; ici la borne pessimiste
// d'un prix est le HAUT. Une estimation floue ne doit jamais rendre un
// programme moins cher qu'une donnée mesurée.
inline double effective_cost_musd(const EnginePart& p) {
  switch (p.cost_confidence) {
    case PartConfidence::A: return p.cost_musd;
    case PartConfidence::B: return 0.75 * p.cost_musd + 0.25 * p.cost_hi_musd;
    case PartConfidence::C: return 0.50 * p.cost_musd + 0.50 * p.cost_hi_musd;
    default:                return p.cost_hi_musd;
  }
}

// DÉLAI D'APPROVISIONNEMENT — DÉRIVÉ DU TRL, pas tabulé [GDD 5.3, 12.1].
// Le TRL est DÉFINI comme la distance à la maturité de vol : chaque cran en
// dessous de 9 est une campagne de qualification à mener avant de pouvoir
// commander la pièce. Le dériver évite dix-huit délais inventés, et dit la
// même chose que la donnée déjà présente sur chaque ligne.
inline double lead_months_for(const EnginePart& p) {
  if (p.trl >= 9) return 12.0;              // en production : on commande
  if (p.trl >= 8) return 18.0;
  if (p.trl >= 7) return 24.0;
  if (p.trl >= 5) return 48.0;
  if (p.trl >= 3) return 96.0;
  return 180.0;                             // TRL 1-2 : un programme, pas un achat
}

// Données publiques d'ingénierie. Chaque ligne porte sa source.
inline const std::vector<EnginePart>& engine_catalog() {
  static const std::vector<EnginePart> v = {
    {"RL10C-1", "RL10C-1", "Centaur / Atlas V, Delta IV",
     PropFamily::ChemicalLiquid, 101800.0, 449.7, 190.0, 0.0, 15,
     PartConfidence::A, QualStatus::Flown, 9, 0.01,
     "fiches ULA/Aerojet publiques ; ~500 allumages en vol",
     17.0, 18.5, 20.0, PartConfidence::B,
     "prix constructeur rapporte publiquement entre 17 et 20 M$ l unite",
     "", "TANK-LOX-LH2"},
    {"RS-25", "RS-25 (SSME)", "Navette spatiale, SLS",
     PropFamily::ChemicalLiquid, 2279000.0, 452.3, 3177.0, 366.0, 1,
     PartConfidence::A, QualStatus::Flown, 9, 0.01,
     "documentation NASA ; 135 vols navette",
     99.4, 146.0, 146.0, PartConfidence::A,
     "contrats NASA/Aerojet : 1,79 Md$ pour 18 moteurs (99,4 M$) ; 3,5 Md$ pour 24 tout compris (146 M$)",
     "", "TANK-LOX-LH2"},
    {"MERLIN-1D-VAC", "Merlin 1D Vacuum", "Falcon 9 second etage",
     PropFamily::ChemicalLiquid, 981000.0, 348.0, 470.0, 0.0, 3,
     PartConfidence::A, QualStatus::Flown, 9, 0.02,
     "fiches SpaceX publiques ; historique Falcon 9",
     1.0, 2.0, 5.0, PartConfidence::C,
     "NON PUBLIE par SpaceX ; estimations publiques de l ordre du million",
     "", "TANK-LOX-RP1"},
    {"MERLIN-1D-SL", "Merlin 1D", "Falcon 9 premier etage",
     PropFamily::ChemicalLiquid, 934000.0, 311.0, 470.0, 282.0, 1,
     PartConfidence::A, QualStatus::Flown, 9, 0.02,
     "fiches SpaceX publiques",
     1.0, 2.0, 5.0, PartConfidence::C,
     "NON PUBLIE par SpaceX ; meme lignee que la version vide",
     "", "TANK-LOX-RP1"},
    {"VULCAIN-2", "Vulcain 2", "Ariane 5 / Ariane 6 etage principal",
     PropFamily::ChemicalLiquid, 1359000.0, 431.0, 2100.0, 318.0, 1,
     PartConfidence::A, QualStatus::Flown, 9, 0.01,
     "documentation ArianeGroup/ESA",
     10.0, 15.0, 25.0, PartConfidence::C,
     "prix unitaire NON PUBLIE ; developpement de Vulcain 2 rapporte a ~500 M EUR (Snecma)",
     "", "TANK-LOX-LH2"},
    {"VINCI", "Vinci", "Ariane 6 etage superieur",
     PropFamily::ChemicalLiquid, 180000.0, 457.0, 550.0, 0.0, 5,
     PartConfidence::B, QualStatus::Flown, 9, 0.02,
     "documentation ArianeGroup ; premiers vols Ariane 6",
     8.0, 12.0, 20.0, PartConfidence::C,
     "prix unitaire NON PUBLIE ; cryogenique reallumable, production Ariane 6 en montee",
     "", "TANK-LOX-LH2"},
    {"AESTUS", "Aestus", "Ariane 5 ES etage superieur",
     PropFamily::ChemicalLiquid, 29600.0, 324.0, 111.0, 0.0, -1,
     PartConfidence::B, QualStatus::Flown, 9, 0.02,
     "donnees constructeur + vols Ariane 5 G",
     4.0, 5.0, 8.0, PartConfidence::C,
     "prix unitaire NON PUBLIE ; ergols stockables, moteur simple sans turbopompe",
     "", "TANK-STOCK"},
    {"RD-180", "RD-180", "Atlas V premier etage",
     PropFamily::ChemicalLiquid, 4152000.0, 338.0, 5480.0, 311.0, 1,
     PartConfidence::A, QualStatus::Flown, 9, 0.01,
     "documentation NPO Energomash / ULA",
     9.9, 25.0, 70.0, PartConfidence::B,
     "prix Energomash rapporte a 9,9 M$ (2018) ; prix paye par ULA ~25 M$ ; jusqu a 70 M$ selon les sources",
     "", "TANK-LOX-RP1"},
    {"F-1", "F-1", "Saturn V premier etage",
     PropFamily::ChemicalLiquid, 7770000.0, 304.0, 8400.0, 263.0, 1,
     PartConfidence::A, QualStatus::Flown, 9, 0.01,
     "rapports NASA Saturn V",
     19.0, 21.0, 25.0, PartConfidence::A,
     "contrat Rocketdyne 1964 : 76 moteurs pour 158,4 M$, soit 2,08 M$ l unite (~21 M$ 2026)",
     "", "TANK-LOX-RP1"},
    {"AJ10-190", "AJ10-190 (OMS)", "Navette spatiale, Orion",
     PropFamily::ChemicalLiquid, 26700.0, 316.0, 118.0, 0.0, -1,
     PartConfidence::A, QualStatus::Flown, 9, 0.01,
     "documentation Aerojet ; systeme de manoeuvre orbitale navette",
     5.0, 8.0, 15.0, PartConfidence::C,
     "prix unitaire NON PUBLIE ; moteur de manoeuvre orbitale, serie tres courte",
     "", "TANK-STOCK"},
    {"SRB-P80", "P80 (propulseur solide)", "Vega premier etage",
     PropFamily::ChemicalSolid, 2261000.0, 280.0, 7330.0, 0.0, 0,
     PartConfidence::B, QualStatus::Flown, 9, 0.02,
     "documentation Avio/ESA — masse structure a vide",
     6.0, 9.0, 15.0, PartConfidence::C,
     "prix unitaire NON PUBLIE ; etage solide monobloc Vega",
     "", ""},   // pas de reservoir : l enveloppe est deja dans mass_kg
    // --- électrique : la poussée est PLAFONNÉE par la puissance [GDD 6.2] ----
    {"NSTAR", "NSTAR (ion a grilles)", "Deep Space 1, Dawn",
     PropFamily::ElectricGridded, 0.092, 3120.0, 8.2, 0.0, -1,
     PartConfidence::A, QualStatus::Flown, 9, 0.02,
     "NASA Dawn / DS1 ; >50 000 h de fonctionnement cumule",
     3.0, 5.0, 10.0, PartConfidence::C,
     "prix unitaire NON PUBLIE ; livre sous contrat NASA, analogie NEXT-C",
     "electrique_avancee", "TANK-XE"},
    {"NEXT-C", "NEXT-C (ion a grilles)", "DART, lignee NSTAR",
     PropFamily::ElectricGridded, 0.236, 4190.0, 13.5, 0.0, -1,
     PartConfidence::A, QualStatus::Flown, 9, 0.03,
     "NASA GRC ; qualifie et vole sur DART",
     4.6, 9.2, 12.0, PartConfidence::B,
     "contrat NASA GRC : 18,41 M$ pour DEUX propulseurs et DEUX PPU de vol",
     "electrique_avancee", "TANK-XE"},
    {"SPT-100", "SPT-100 (Hall)", "plateformes telecom GEO",
     PropFamily::ElectricHall, 0.083, 1600.0, 3.5, 0.0, -1,
     PartConfidence::A, QualStatus::Flown, 9, 0.03,
     "Fakel / OKB ; des centaines d'unites en vol",
     0.5, 1.0, 3.0, PartConfidence::C,
     "prix unitaire NON PUBLIE ; produit de serie, des centaines d unites livrees",
     "electrique_avancee", "TANK-XE"},
    {"BHT-6000", "BHT-6000 (Hall 6 kW)", "lignee Busek/PPE",
     PropFamily::ElectricHall, 0.400, 2200.0, 12.0, 0.0, -1,
     PartConfidence::B, QualStatus::GroundTested, 7, 0.08,
     "essais au sol publies ; pas encore d'historique de vol",
     2.0, 4.0, 10.0, PartConfidence::C,
     "prix unitaire NON PUBLIE ; unite de qualification, pas de serie",
     "electrique_avancee", "TANK-XE"},
    // --- nucléaire : essais au sol historiques, jamais vole ------------------
    {"NERVA-NRX", "NERVA NRX (NTP)", "programme Rover/NERVA (essais au sol)",
     PropFamily::Ntp, 334000.0, 850.0, 6800.0, 0.0, 1,
     PartConfidence::B, QualStatus::GroundTested, 5, 0.10,
     "rapports AEC/NASA 1964-1969 ; jamais vole",
     150.0, 300.0, 600.0, PartConfidence::D,
     "AUCUN prix unitaire n existe : le programme Rover/NERVA a coute 1,4 Md$ (1973, ~10 Md$ 2026) sans jamais produire de serie",
     "ntp", "TANK-LOX-LH2"},
    // --- spéculatif : incertitude LARGE, confiance basse [GDD 12.5] ----------
    {"NEP-1MW", "Propulseur NEP 1 MW", "concept, lignee Hall haute puissance",
     PropFamily::Nep, 25.0, 5000.0, 900.0, 0.0, -1,
     PartConfidence::D, QualStatus::Speculative, 2, 0.50,
     "estimation raisonnee : aucun essai, extrapolation de puissance",
     80.0, 200.0, 600.0, PartConfidence::D,
     "AUCUN prix : la piece n existe pas. Fourchette large assumee [GDD 12.5]",
     "nep_megawatt", "TANK-XE"},
    {"FUSION-DD", "Tuyere magnetique a fusion", "concept",
     PropFamily::Fusion, 100.0, 50000.0, 25000.0, 0.0, -1,
     PartConfidence::D, QualStatus::Speculative, 1, 0.80,
     "hypothese de simulation : bilan net non demontre",
     300.0, 1000.0, 5000.0, PartConfidence::D,
     "AUCUN prix : bilan net non demontre. Fourchette d une decade assumee [GDD 12.5]",
     "fusion", "TANK-XE"},
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
  // ═══ CE POUR QUOI LE BOUCLIER A ÉTÉ QUALIFIÉ ═══ [GDD 9.2, 12.3.1]
  // AJOUTÉ EN QUEUE (piège n°83). Un bouclier thermique ne se spécifie pas par un
  // flux maximal publié — ce chiffre n'est presque jamais public — mais par
  // l'ENTRÉE qu'il a réellement survécue, qui l'est toujours. On ne déclare donc
  // qu'une vitesse d'interface, factuelle, et le flux admissible se CALCULE
  // (`mission/Rentree.hpp`). Même principe que les courbes de fiabilité dérivées
  // du statut de qualification : la donnée d'entrée est un fait, pas un réglage.
  double qual_entry_speed_ms;
  const char* qual_entry_source;
  double sutton_graves_k;      // corps d'entrée qualifié : Terre ou Mars
};

inline const std::vector<CapsulePart>& capsule_catalog() {
  static const std::vector<CapsulePart> v = {
    {"APOLLO-CM", "Module de commande Apollo", "Apollo",
     5560.0, 3, 1.35, 12.02, 4.69, 0.30, 12.0,
     PartConfidence::A, QualStatus::Flown,
     "rapports NASA Apollo ; bouclier 3,91 m",
     11030.0, "Apollo 11 : interface a 36 194 ft/s = 11,03 km/s (retour lunaire)",
     flight::SUTTON_GRAVES_EARTH},
    {"SOYUZ-SA", "Module de descente Soyouz", "Soyouz",
     2900.0, 3, 1.40, 3.80, 2.20, 0.25, 9.0,
     PartConfidence::A, QualStatus::Flown,
     "documentation RKK Energia ; rentree balistique de secours a ~9 g",
     7700.0, "retour d orbite basse uniquement — jamais qualifie au-dela",
     flight::SUTTON_GRAVES_EARTH},
    {"ORION-CM", "Module d'equipage Orion", "Orion / Artemis",
     10400.0, 4, 1.30, 19.60, 6.04, 0.30, 12.0,
     PartConfidence::A, QualStatus::Flown,
     "documentation NASA ; bouclier 5,0 m, retour lunaire Artemis I",
     10950.0, "Artemis I : rentree a 24 500 mph = 10,95 km/s (retour lunaire)",
     flight::SUTTON_GRAVES_EARTH},
    {"DRAGON-2", "Crew Dragon", "Falcon 9 / ISS",
     9500.0, 4, 1.30, 10.75, 3.20, 0.28, 10.0,
     PartConfidence::B, QualStatus::Flown,
     "fiches SpaceX publiques ; diametre 3,7 m",
     7600.0, "retours d orbite basse (ISS) ; PICA-X non qualifie en vol au-dela",
     flight::SUTTON_GRAVES_EARTH},
    {"MARS-AEROSHELL", "Aerocoque martienne", "MSL / Mars 2020",
     3400.0, 0, 1.45, 15.90, 1.13, 0.24, 15.0,
     PartConfidence::A, QualStatus::Flown,
     "NASA JPL ; aerocoque 4,5 m, la plus grande volee vers Mars",
     5845.0, "MSL : interface martienne a 5,845 km/s",
     flight::SUTTON_GRAVES_MARS},
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

// ═══════════════════════════════════════════════════════════════════════════
// UNE FILIÈRE ALIMENTÉE TRAÎNE SA CENTRALE [GDD 5.12.1, 5.12.10, 6.2, 6.5]
// ═══════════════════════════════════════════════════════════════════════════
// « Une erreur de design fréquente consiste à confondre produire de l'ÉNERGIE et
// produire de la POUSSÉE » [GDD 5.12.1]. Le modèle disait cette phrase et ne
// l'appliquait nulle part : `source_mass_kg`, `PoweredPropulsion` et tout
// `env/Thermal.hpp` n'avaient AUCUN appelant vivant. Conséquence mesurable dans
// le jeu : un étage NEP-1MW coûtait ses 900 kg de tuyère et rendait Isp 5000 s —
// onze fois le meilleur chimique POUR RIEN. Toute la branche 6 était donc une
// amélioration stricte, sans arbitrage, ce que [GDD 6.2] interdit en une ligne :
// « personne n'a haute poussée ET haut rendement sans puissance colossale ».
//
// Rien ici n'est un réglage. La puissance se DÉDUIT de la pièce (F = 2ηP/ve), la
// masse de centrale se déduit de la puissance, la chaleur perdue se déduit du
// rendement, et la surface de radiateur se déduit de Stefan-Boltzmann.
struct PowerPlant {
  bool   needs_power{false};      // la filière réclame des watts [GDD 6.2]
  bool   self_powered{false};     // la réaction EST la source (fusion, antimatière)
  bool   source_missing{false};   // filière alimentée, aucune source choisie
  double p_electric_w{0.0};
  double source_mass_kg{0.0};
  double waste_heat_w{0.0};
  double radiator_area_m2{0.0};
  double radiator_mass_kg{0.0};

  double total_mass_kg() const { return source_mass_kg + radiator_mass_kg; }
  // Masse de centrale par kW électrique : la grandeur sous laquelle la
  // littérature publie ces systèmes, donc celle qui se recoupe.
  double alpha_kg_per_kwe() const {
    return p_electric_w > 0.0 ? total_mass_kg() / (p_electric_w / 1000.0) : 0.0;
  }
};

// LA SOURCE EST UNE DÉCISION D'ARCHITECTE, jamais une déduction : solaire léger
// mais tributaire de la distance [5.12.5], RTG insensible à la distance mais
// dérisoire en puissance [5.12.6], réacteur lourd mais seul à tenir le mégawatt
// [5.12.8]. `PropTier::Chemical` = « aucune source choisie ».
inline PowerPlant power_plant_for(const EnginePart& p, PropTier source,
                                  const env::RadiatorSpec& rad = {}) {
  PowerPlant pp;
  const PropFamilyClass* f = prop_family(p.family);
  if (!f || !f->needs_power) return pp;              // chimique, NTP : rien à porter
  pp.needs_power = true;
  // Fusion et antimatière PRODUISENT leur puissance : leur « source » est la
  // pièce elle-même, déjà pesée. Il leur reste la chaleur à évacuer, qui est
  // très exactement le « facteur limitant » que [GDD 6.4] leur attribue.
  pp.self_powered = (f->tier == PropTier::Fusion || f->tier == PropTier::Antimatter);

  PoweredPropulsion sys;
  sys.eta   = jet_efficiency(p.family);
  sys.isp_s = p.isp_vac_s;
  sys.p_source_w = power_required_w(p.thrust_vac_n, p.isp_vac_s * cst::G0, sys.eta);
  pp.p_electric_w = sys.p_source_w;

  if (!pp.self_powered) {
    if (source == PropTier::Chemical) { pp.source_missing = true; return pp; }
    pp.source_mass_kg = power_plant_mass_kg(source, sys.p_source_w);
  }
  // SEUL UN CYCLE THERMIQUE EXIGE DES RADIATEURS DÉDIÉS. Un panneau solaire et
  // un RTG rejettent leur chaleur par leur propre surface — c'est ainsi qu'ils
  // sont construits. Un réacteur, non : à 30 % de rendement il jette 2,33 fois
  // ce qu'il produit, et il faut une aile pour ça [GDD 6.5].
  const EnergySourceClass* src = pp.self_powered ? nullptr : energy_source(source);
  sys.source_is_reactor = pp.self_powered || (src && src->tier == PropTier::Fission);
  if (src) sys.eta_th_source = src->eta_thermal;
  pp.waste_heat_w = sys.waste_heat_w();
  const env::RadiatorSizing rs = env::size_radiator(pp.waste_heat_w, rad);
  pp.radiator_area_m2  = rs.area_m2;
  pp.radiator_mass_kg  = rs.mass_kg;
  return pp;
}

// ═══════════════════════════════════════════════════════════════════════════
// UN ÉTAGE CHOISI — LE VOCABULAIRE COMMUN DE L'ATELIER ET DE LA MISSION
// ═══════════════════════════════════════════════════════════════════════════
// [GDD 4.1, 12.2] « Réception d'un contrat → CONCEPTION → ... → lancement. »
// L'atelier et l'évaluation de mission décrivaient chacun leur véhicule : le
// joueur choisissait un moteur DEUX FOIS, dans deux postes, et seul celui du
// poste CONTRÔLE comptait. Ce type est le vocabulaire commun ; il vit ici, avec
// les pièces qu'il désigne, pour qu'aucune des deux couches ne le redéfinise.
struct StageChoice {
  int    engine{0};            // index dans engine_catalog()
  int    tank{0};              // index dans tank_catalog()
  double dv_target_ms{3000.0}; // partage du Δv — DÉCISION du joueur
  double structure_mass_kg{300.0};
  // ═══ LA SOURCE D'ÉNERGIE — DÉCISION D'ARCHITECTE ═══ [GDD 5.12.1]
  // Sans objet pour une filière non alimentée (chimique, NTP), et pour celles
  // qui produisent leur propre puissance (fusion, antimatière). `Chemical` = le
  // choix n'est PAS fait : l'atelier le signale au lieu d'en poser un à la place
  // du joueur [anti-feature 1.5].
  PropTier source{PropTier::Chemical};

  const EnginePart& engine_part() const {
    const auto& v = engine_catalog();
    const int i = engine < 0 ? 0 : (engine >= (int)v.size() ? (int)v.size() - 1 : engine);
    return v[static_cast<std::size_t>(i)];
  }
  // `tank_dry_fraction` NE VIENT PAS DE L'INDEX SEUL : un solide n'a pas de
  // réservoir (`tank_id` vide), et son enveloppe est déjà dans la masse moteur.
  double tank_dry_fraction() const {
    const EnginePart& e = engine_part();
    if (!e.tank_id || !e.tank_id[0]) return 0.0;
    const auto& v = tank_catalog();
    const int i = tank < 0 ? 0 : (tank >= (int)v.size() ? (int)v.size() - 1 : tank);
    return v[static_cast<std::size_t>(i)].dry_fraction;
  }
  double residual_fraction() const {
    const EnginePart& e = engine_part();
    if (!e.tank_id || !e.tank_id[0]) return 0.01;
    const auto& v = tank_catalog();
    const int i = tank < 0 ? 0 : (tank >= (int)v.size() ? (int)v.size() - 1 : tank);
    return v[static_cast<std::size_t>(i)].residual_fraction;
  }
  PowerPlant power_plant() const { return power_plant_for(engine_part(), source); }
};

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
