// app/vehicle_design.hpp — L'ATELIER D'ASSEMBLAGE [GDD 12.2]
//
// « Éditeur en vue schématique technique façon bureau d'études : sélection de
// pièces en coupe, tableau de masse/delta-v recalculé automatiquement selon
// Tsiolkovsky à chaque modification en mode Normal ; aucune aide en mode Pro. »
//
// Ce fichier est la PARTIE MODÈLE de l'atelier (le poste CONCEPTION, côté UE,
// n'en est que la vue). C++ pur, sous oracle : le joueur CHOISIT ses pièces et
// le partage du Δv entre étages ; la physique (Vehicle.hpp, PartsCatalog.hpp)
// TRANCHE la masse. Aucun raccourci arcade — le dimensionnement est le point
// fixe de Tsiolkovsky, exactement celui de `size_multistage_for_dv`.
//
// RÈGLE : le modèle ne DÉCIDE jamais à la place du joueur (pas d'optimisation
// automatique du partage de Δv, ce serait l'anti-feature de 1.5). Il ne fait que
// recalculer les conséquences d'un choix — et signaler quand ce choix est
// physiquement infaisable (étage continu au décollage, non-convergence).
#pragma once
#include <algorithm>
#include <string>
#include <vector>

#include "fen/vehicle/PartsCatalog.hpp"
#include "fen/vehicle/Vehicle.hpp"

namespace fen::app {

// Un étage choisi : quelle pièce, quel réservoir, combien de Δv on lui confie.
struct StagePick {
  int    engine{0};            // index dans vehicle::engine_catalog()
  int    tank{0};              // index dans vehicle::tank_catalog()
  double dv_target_ms{3000.0}; // partage du Δv — DÉCISION du joueur
  double structure_mass_kg{300.0};
};

// La conception en cours. `stages[0]` = étage du BAS (allumé en premier).
struct VehicleDesign {
  std::vector<StagePick> stages;
  int    capsule{-1};          // index dans capsule_catalog() ; -1 = charge nue
  double payload_kg{500.0};    // charge utile hors capsule

  // Charge utile totale = charge nue + masse sèche de la capsule choisie.
  double payload_total_kg() const {
    double p = payload_kg;
    const auto& caps = vehicle::capsule_catalog();
    if (capsule >= 0 && capsule < static_cast<int>(caps.size()))
      p += caps[static_cast<std::size_t>(capsule)].dry_mass_kg;
    return p;
  }

  // Une conception de départ raisonnable : deux étages chimiques liquides.
  static VehicleDesign starter() {
    VehicleDesign d;
    // RD-180 en bas (fort), RL10 en haut (haut Isp). Indices dans le catalogue.
    const int rd180 = index_moteur("RD-180");
    const int rl10 = index_moteur("RL10C-1");
    const int tk_rp1 = index_reservoir("TANK-LOX-RP1");
    const int tk_lh2 = index_reservoir("TANK-LOX-LH2");
    d.stages.push_back({rd180 < 0 ? 0 : rd180, tk_rp1 < 0 ? 0 : tk_rp1, 3500.0, 1200.0});
    d.stages.push_back({rl10 < 0 ? 0 : rl10, tk_lh2 < 0 ? 0 : tk_lh2, 4600.0, 400.0});
    d.payload_kg = 1500.0;
    return d;
  }

  static int index_moteur(const char* id) {
    const auto& v = vehicle::engine_catalog();
    for (std::size_t i = 0; i < v.size(); ++i) if (id == std::string(v[i].id)) return (int)i;
    return -1;
  }
  static int index_reservoir(const char* id) {
    const auto& v = vehicle::tank_catalog();
    for (std::size_t i = 0; i < v.size(); ++i) if (id == std::string(v[i].id)) return (int)i;
    return -1;
  }
};

// Le résultat par étage, tel que le joueur le lit dans le tableau.
struct StageReadout {
  std::string engine_name;
  std::string tank_name;
  double dv_target_ms{};
  double propellant_kg{};
  double dry_kg{};
  double wet_kg{};
  bool   converged{};
};

// Le bilan complet, RECALCULÉ à chaque modification [GDD 12.2].
struct DesignSummary {
  std::vector<StageReadout> stages;
  double total_dv_ms{};        // somme des Δv confiés aux étages
  double liftoff_mass_kg{};    // masse au décollage (m0 de l'étage du bas)
  double payload_kg{};
  double payload_fraction{};   // charge utile / masse au décollage
  bool   converged{false};     // le point fixe de sizing a convergé
  bool   valid{false};         // au moins un étage
  bool   liftoff_capable{false}; // l'étage du bas peut-il décoller ? [GDD 6.3]
  std::string warning;         // motif d'infaisabilité, s'il y en a un
};

namespace detail {
inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
}

// Évalue une conception : c'est le CŒUR de l'atelier. Il n'optimise rien ; il
// applique Tsiolkovsky au partage de Δv choisi et remonte les masses.
inline DesignSummary evaluate_design(const VehicleDesign& d) {
  using namespace vehicle;
  DesignSummary s;
  s.payload_kg = d.payload_total_kg();
  if (d.stages.empty()) { s.warning = "aucun etage"; return s; }
  s.valid = true;

  const auto& engs = engine_catalog();
  const auto& tanks = tank_catalog();

  std::vector<StageSpec> specs;
  specs.reserve(d.stages.size());
  for (const auto& sp : d.stages) {
    const EnginePart& e = engs[static_cast<std::size_t>(
        detail::clampi(sp.engine, 0, (int)engs.size() - 1))];
    const TankPart& t = tanks[static_cast<std::size_t>(
        detail::clampi(sp.tank, 0, (int)tanks.size() - 1))];
    StageSpec spec;
    spec.dv_target = std::max(0.0, sp.dv_target_ms);
    spec.engine = to_engine(e);
    spec.tank_dry_fraction = t.dry_fraction;
    spec.structure_mass = std::max(0.0, sp.structure_mass_kg);
    spec.residual_fraction = t.residual_fraction;
    specs.push_back(spec);
    s.total_dv_ms += spec.dv_target;
  }

  const MultiSizingResult r = size_multistage_for_dv(specs, s.payload_kg);
  s.converged = r.converged;
  s.liftoff_mass_kg = r.m0;
  s.payload_fraction = r.m0 > 0.0 ? s.payload_kg / r.m0 : 0.0;

  // détail par étage, pour le tableau
  for (std::size_t k = 0; k < d.stages.size(); ++k) {
    const EnginePart& e = engs[static_cast<std::size_t>(
        detail::clampi(d.stages[k].engine, 0, (int)engs.size() - 1))];
    const TankPart& t = tanks[static_cast<std::size_t>(
        detail::clampi(d.stages[k].tank, 0, (int)tanks.size() - 1))];
    StageReadout ro;
    ro.engine_name = e.name;
    ro.tank_name = t.name;
    ro.dv_target_ms = specs[k].dv_target;
    ro.propellant_kg = r.stages[k].propellant;
    ro.dry_kg = r.stages[k].stage_dry;
    ro.wet_kg = r.stages[k].m0 - (k == 0 ? s.payload_kg : 0.0);  // approx d'affichage
    ro.converged = r.stages[k].converged;
    s.stages.push_back(ro);
  }

  // FAISABILITÉ AU DÉCOLLAGE [GDD 6.3] : l'étage du bas doit être IMPULSIONNEL.
  // Un propulseur électrique/NEP (régime continu) ne décolle jamais d'un sol.
  const EnginePart& bas = engs[static_cast<std::size_t>(
      detail::clampi(d.stages.front().engine, 0, (int)engs.size() - 1))];
  const PropFamilyClass* fam = prop_family(bas.family);
  s.liftoff_capable = fam && fam->regime == BurnRegime::Impulsive;

  if (!s.converged) s.warning = "sizing non convergent (Dv trop ambitieux ?)";
  else if (!s.liftoff_capable)
    s.warning = "etage du bas en regime continu : incapable de decoller [GDD 6.3]";
  return s;
}

} // namespace fen::app
