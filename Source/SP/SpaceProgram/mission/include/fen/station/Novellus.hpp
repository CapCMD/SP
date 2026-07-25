// fen/station/Novellus.hpp — station QG [GDD 11]
//
// AUCUN module n'est un simple volume : chaque ajout ouvre une capacité, réduit
// un risque ou améliore une fonction vitale [GDD 11.2]. La station implémente
// IInfrastructureProvider (Unlock.hpp) : c'est ELLE qui lève le verrou
// d'infrastructure des capacités avancées — couplée à la branche 6 quand ses
// besoins en puissance/thermique dépassent le solaire léger [GDD 11.5].
#pragma once
#include <algorithm>
#include <string>
#include <vector>
#include "fen/tech/Unlock.hpp"

namespace fen::station {

// Les dix modules [GDD 11.2].
enum class ModuleType {
  CommandCore = 0, DockingNode, CrewHabitat, LifeSupport, Power,
  ScienceLab, Workshop, Storage, Medical, EvaAirlock,
};
inline const char* module_name(ModuleType t) {
  switch (t) {
    case ModuleType::CommandCore: return "Noyau de commandement";
    case ModuleType::DockingNode: return "Noeud d'amarrage";
    case ModuleType::CrewHabitat: return "Habitat equipage";
    case ModuleType::LifeSupport: return "Support-vie";
    case ModuleType::Power:       return "Module energetique";
    case ModuleType::ScienceLab:  return "Laboratoire scientifique";
    case ModuleType::Workshop:    return "Atelier / maintenance";
    case ModuleType::Storage:     return "Logistique / stockage";
    case ModuleType::Medical:     return "Module medical";
    default:                      return "Sas EVA";
  }
}

// Catégories [GDD 11.4].
enum class ModuleCategory { Mandatory, Advanced, Robustness };
inline ModuleCategory module_category(ModuleType t) {
  switch (t) {
    case ModuleType::ScienceLab: case ModuleType::Workshop:
      return ModuleCategory::Advanced;
    case ModuleType::Medical: case ModuleType::EvaAirlock:
      return ModuleCategory::Robustness;
    default: return ModuleCategory::Mandatory;
  }
}

// DEMANDE ÉLECTRIQUE PAR MODULE. Le GDD nomme le rôle du module énergétique —
// « grand facteur limitant de croissance » [11.2], « conditionne l'activation
// des fonctions énergivores » [11.6], et 11.5 désigne nommément le laboratoire
// lourd, l'atelier énergivore et le support-vie de grande capacité comme les
// gros consommateurs. Il ne CHIFFRE pas : ces valeurs sont donc des hypothèses
// de modèle DÉCLARÉES [GDD 6.8], calées sur les ordres de grandeur de l'ISS
// (~75-90 kW produits, ECLSS ~8 kW, charges scientifiques quelques kW).
// Sans elles, le module énergétique ne limiterait rien du tout.
inline double default_power_demand_kw(ModuleType t) {
  switch (t) {
    case ModuleType::CommandCore:  return 2.0;
    case ModuleType::DockingNode:  return 0.5;
    case ModuleType::CrewHabitat:  return 3.0;
    case ModuleType::LifeSupport:  return 8.0;   // ECLSS : le premier poste
    case ModuleType::Power:        return 0.0;   // il fournit, il ne consomme pas
    case ModuleType::ScienceLab:   return 6.0;
    case ModuleType::Workshop:     return 5.0;
    case ModuleType::Storage:      return 0.5;
    case ModuleType::Medical:      return 2.0;
    default:                       return 1.5;   // sas EVA
  }
}

struct StationModule {
  ModuleType type{};
  bool operational{true};
  int  generation{1};             // 2+ : versions haute puissance [GDD 11.5]
  double power_supply_kw{0.0};    // >0 uniquement pour Power (et générations sup.)
  double thermal_reject_kw{0.0};  // capacité de rejet apportée par le module
  // -1 = « prendre la valeur par défaut du type ». Un module peut déclarer une
  // demande propre (génération supérieure, charge utile particulière).
  double power_demand_kw{-1.0};

  double demand_kw() const {
    return power_demand_kw >= 0.0 ? power_demand_kw : default_power_demand_kw(type);
  }
};

// --- La station : un GRAPHE de modules (topologie d'amarrage) ----------------
struct DockingEdge { int a{}, b{}; };  // indices dans modules

class Station : public tech::IInfrastructureProvider {
 public:
  std::vector<StationModule> modules;
  std::vector<DockingEdge> topology;

  int count(ModuleType t) const {
    int n = 0;
    for (const auto& m : modules) if (m.type == t && m.operational) ++n;
    return n;
  }
  bool has(ModuleType t) const { return count(t) > 0; }

  // Paliers [GDD 11.3] : 1 fondation, 2 habitabilité, 3 exploitation, 4 robustesse.
  int tier() const {
    const bool t1 = has(ModuleType::CommandCore) && has(ModuleType::DockingNode)
                 && has(ModuleType::Power);
    const bool t2 = t1 && has(ModuleType::LifeSupport) && has(ModuleType::CrewHabitat)
                 && has(ModuleType::Storage);
    const bool t3 = t2 && has(ModuleType::ScienceLab) && has(ModuleType::Workshop);
    const bool t4 = t3 && has(ModuleType::Medical) && has(ModuleType::EvaAirlock);
    return t4 ? 4 : t3 ? 3 : t2 ? 2 : t1 ? 1 : 0;
  }

  // PUISSANCE PRODUITE par la station.
  double power_kw() const {
    double p = 0.0;
    for (const auto& m : modules) if (m.operational) p += m.power_supply_kw;
    return p;
  }
  // PUISSANCE CONSOMMÉE par ses modules — l'autre moitié du bilan.
  double power_demand_kw() const {
    double p = 0.0;
    for (const auto& m : modules) if (m.operational) p += m.demand_kw();
    return p;
  }
  // La marge : négative = la station ne peut pas tout alimenter [GDD 11.6].
  double power_margin_kw() const { return power_kw() - power_demand_kw(); }
  double thermal_kw() const {
    double p = 0.0;
    for (const auto& m : modules) if (m.operational) p += m.thermal_reject_kw;
    return p;
  }

  // --- IInfrastructureProvider [GDD 11.5, 5.4] -------------------------------
  bool provides(const tech::InfrastructureNeed& need) const override {
    return power_kw() >= need.power_kw
        && thermal_kw() >= need.thermal_reject_kw
        && tier() >= need.station_tier
        && !need.nuclear_test_bench     // bancs nucléaires : installations SOL,
        && !need.heavy_launch;          // cadence lourde : branche 1 — pas ici
  }
  std::string missing(const tech::InfrastructureNeed& need) const override {
    std::string s;
    if (power_kw() < need.power_kw)          s += "puissance station ";
    if (thermal_kw() < need.thermal_reject_kw) s += "rejet thermique ";
    if (tier() < need.station_tier)          s += "palier Novellus ";
    if (need.nuclear_test_bench)             s += "banc d'essai nucleaire (sol) ";
    if (need.heavy_launch)                   s += "cadence de lancement ";
    return s.empty() ? "" : ("infrastructure manquante : " + s);
  }
};

// --- Effets gameplay [GDD 11.6] ----------------------------------------------
// Chaque module produit un effet CONCRET, agrégé ici et consommé par les autres
// systèmes (recherche, fiabilité, marges) — jamais de bonus cosmétique.
// [GDD 11.6] « Chaque module produit des effets CONCRETS. » Les dix en ont un :
// il en manquait TROIS — noyau, nœud d'amarrage et module énergétique — c'est-
// à-dire précisément les trois modules OBLIGATOIRES du palier 1. Sans eux, la
// station pouvait être déclarée opérationnelle sans noyau de commandement, ce
// que 11.2 interdit explicitement.
struct StationEffects {
  // Noyau de commandement : « base minimale de pilotage — sans lui, Novellus
  // n'est pas opérationnelle » [GDD 11.2].
  bool   operational{false};
  double alert_coordination{1.0};   // < 1 : anomalies mieux contenues
  // Nœud d'amarrage : accueil des visiteurs et croissance de la station.
  int    docking_slots{0};
  bool   can_expand{false};
  // Module énergétique : conditionne l'ACTIVATION des fonctions énergivores.
  double power_margin_kw{0.0};
  bool   power_sufficient{false};
  // Les sept autres, déjà en place.
  double research_speed{1.0};       // labo : accélère ResearchQueue
  double maintenance_quality{1.0};  // atelier : Modifiers::maintenance < 1
  double crew_capacity{0.0};
  double sustainable_days{0.0};     // support-vie + stockage
  double medical_risk_factor{1.0};  // médical : réduit urgences [GDD 9.4]
  bool   eva_ops{false};
};

inline StationEffects effects(const Station& st) {
  StationEffects e;
  // --- les trois obligatoires du palier 1 ----------------------------------
  const int noyaux = st.count(ModuleType::CommandCore);
  e.operational        = noyaux > 0;
  // Supervision et redistribution des alertes : chaque noyau supplémentaire est
  // une redondance, avec un rendement décroissant DÉCLARÉ.
  e.alert_coordination = noyaux > 0 ? 1.0 / (1.0 + 0.25 * (noyaux - 1)) : 1.0;
  e.docking_slots      = 2 * st.count(ModuleType::DockingNode);
  e.can_expand         = e.docking_slots > 0;
  // La puissance produite doit couvrir la demande de TOUS les modules :
  // « grand facteur limitant de croissance » [GDD 11.2].
  e.power_margin_kw    = st.power_margin_kw();
  e.power_sufficient   = e.power_margin_kw >= 0.0;

  // --- les sept autres ------------------------------------------------------
  e.research_speed      = 1.0 + 0.25 * st.count(ModuleType::ScienceLab);
  e.maintenance_quality = st.has(ModuleType::Workshop) ? 0.85 : 1.0;
  e.crew_capacity       = 3.0 * st.count(ModuleType::CrewHabitat);
  e.sustainable_days    = st.has(ModuleType::LifeSupport)
                          ? 30.0 + 60.0 * st.count(ModuleType::Storage) : 0.0;
  e.medical_risk_factor = st.has(ModuleType::Medical) ? 0.6 : 1.0;
  e.eva_ops             = st.has(ModuleType::EvaAirlock);

  // FONCTIONS ÉNERGIVORES : sans marge, le labo et l'atelier ne tournent pas.
  // C'est le rôle exact que 11.6 assigne au module énergétique.
  if (!e.power_sufficient) {
    e.research_speed = 1.0;
    e.maintenance_quality = 1.0;
  }

  // RIEN NE FONCTIONNE SANS NOYAU. Ce n'est pas un malus : c'est la définition
  // de 11.2. Une station sans centre nerveux n'exploite rien.
  if (!e.operational) {
    e.research_speed = 1.0;
    e.maintenance_quality = 1.0;
    e.crew_capacity = 0.0;
    e.sustainable_days = 0.0;
    e.eva_ops = false;
  }
  return e;
}

} // namespace fen::station
