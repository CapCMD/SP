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

struct StationModule {
  ModuleType type{};
  bool operational{true};
  int  generation{1};             // 2+ : versions haute puissance [GDD 11.5]
  double power_supply_kw{0.0};    // >0 uniquement pour Power (et générations sup.)
  double thermal_reject_kw{0.0};  // capacité de rejet apportée par le module
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

  double power_kw() const {
    double p = 0.0;
    for (const auto& m : modules) if (m.operational) p += m.power_supply_kw;
    return p;
  }
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
struct StationEffects {
  double research_speed{1.0};       // labo : accélère ResearchQueue
  double maintenance_quality{1.0};  // atelier : Modifiers::maintenance < 1
  double crew_capacity{0.0};
  double sustainable_days{0.0};     // support-vie + stockage
  double medical_risk_factor{1.0};  // médical : réduit urgences [GDD 9.4]
  bool   eva_ops{false};
};

inline StationEffects effects(const Station& st) {
  StationEffects e;
  e.research_speed      = 1.0 + 0.25 * st.count(ModuleType::ScienceLab);
  e.maintenance_quality = st.has(ModuleType::Workshop) ? 0.85 : 1.0;
  e.crew_capacity       = 3.0 * st.count(ModuleType::CrewHabitat);
  e.sustainable_days    = st.has(ModuleType::LifeSupport)
                          ? 30.0 + 60.0 * st.count(ModuleType::Storage) : 0.0;
  e.medical_risk_factor = st.has(ModuleType::Medical) ? 0.6 : 1.0;
  e.eva_ops             = st.has(ModuleType::EvaAirlock);
  return e;
}

} // namespace fen::station
