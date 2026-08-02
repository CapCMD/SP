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
#include <cmath>
#include <string>
#include <vector>

#include "fen/env/Atmosphere.hpp"
#include "fen/mission/Rentree.hpp"
#include "fen/vehicle/Geometry.hpp"     // la coupe du véhicule [GDD 12.2, 17.2]
#include "fen/vehicle/PartsCatalog.hpp"
#include "fen/vehicle/Vehicle.hpp"

namespace fen::app {

// Un étage choisi : quelle pièce, quel réservoir, combien de Δv on lui confie.
// LE TYPE VIT DANS LE CATALOGUE (`vehicle/PartsCatalog.hpp`), avec les pièces
// qu'il désigne : c'est le vocabulaire COMMUN de l'atelier et de l'évaluation de
// mission [GDD 4.1, 12.2]. Le redéfinir ici en ferait deux véhicules, ce qui est
// exactement le défaut qu'on vient de solder.
using StagePick = vehicle::StageChoice;

// La conception en cours. `stages[0]` = étage du BAS (allumé en premier).
struct VehicleDesign {
  std::vector<StagePick> stages;
  int    capsule{-1};          // index dans capsule_catalog() ; -1 = charge nue
  double payload_kg{500.0};    // charge utile hors capsule
  // ═══ D'OÙ CE VÉHICULE REVIENT-IL ? ═══ [GDD 9.2]
  // La rentrée n'est pas un décor : c'est la vitesse d'interface qui décide si le
  // bouclier tient. 0 = le véhicule ne revient pas (sonde, satellite), et il n'y a
  // alors rien à vérifier. Défaut : retour d'orbite basse, la mission de départ.
  double v_interface_retour_ms{0.0};

  // Charge utile totale = charge nue + masse sèche de la capsule choisie.
  double payload_total_kg() const {
    double p = payload_kg;
    const auto& caps = vehicle::capsule_catalog();
    if (capsule >= 0 && capsule < static_cast<int>(caps.size()))
      p += caps[static_cast<std::size_t>(capsule)].dry_mass_kg;
    return p;
  }

  // ═══ CE QUE L'ATELIER CONÇOIT : LE VAISSEAU, PAS LE LANCEUR ═══
  // [GDD 4.1, 5.2 br.1] Découvert en branchant l'atelier sur la mission, et
  // c'était une incohérence INVISIBLE tant qu'il ne nourrissait rien : la
  // conception de départ était une FUSÉE (RD-180 au sol, puis RL10), alors que
  // le modèle de mission ACHÈTE son lanceur au catalogue et ne fait voler que ce
  // que le lanceur met en orbite. Les deux couches décrivaient donc deux objets
  // différents sous le même nom.
  // C'est la mission qui a raison — c'est ainsi qu'une agence procède : on
  // achète un Falcon 9, on construit la sonde. La pile de départ est donc un
  // VAISSEAU ORBITAL, et elle reproduit exactement le véhicule que la mission
  // dimensionnait jusqu'ici (deux étages RL10C-1 à parts égales, structure
  // 150 kg) — de sorte que brancher l'atelier ne déplace RIEN par surprise.
  static VehicleDesign starter() {
    VehicleDesign d;
    const int rl10 = index_moteur("RL10C-1");
    const int tk_lh2 = index_reservoir("TANK-LOX-LH2");
    d.stages.push_back({rl10 < 0 ? 0 : rl10, tk_lh2 < 0 ? 0 : tk_lh2, 4050.0, 150.0});
    d.stages.push_back({rl10 < 0 ? 0 : rl10, tk_lh2 < 0 ? 0 : tk_lh2, 4050.0, 150.0});
    d.payload_kg = 1500.0;
    // LE VÉHICULE DE DÉPART REVIENT D'ORBITE BASSE [GDD 9.2]. Le chiffre n'est pas
    // posé à la main : c'est la vis-viva d'une ellipse de désorbitation depuis
    // 400 km, soit 7 912 m/s à l'interface — le ~7 800 m/s publié des rentrées de
    // LEO. Tant qu'aucune capsule n'est montée, il ne décide de rien.
    d.v_interface_retour_ms = fen::mission::vitesse_interface_orbite(
        cst::R_EARTH + 400000.0, cst::MU_EARTH, cst::R_EARTH,
        fen::mission::ENTRY_INTERFACE_EARTH_M);
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
  // Ce que la filière traîne derrière elle [GDD 5.12.1, 6.5] : zéro pour le
  // chimique et le NTP, dominant pour la NEP.
  vehicle::PowerPlant power{};
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
  bool   power_ok{true};       // toute filière alimentée a une source [GDD 5.12.1]
  double powerplant_mass_kg{}; // centrales + radiateurs, tous étages [GDD 6.5]
  // LE BOUCLIER TIENT-IL ? [GDD 9.2] `evalue` faux = ce véhicule ne revient pas.
  fen::mission::BilanRentree rentree{};
  std::string warning;         // motif d'infaisabilité, s'il y en a un
};

namespace detail {
inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
}

// Équipage que la capsule choisie doit pouvoir loger — sert à ne proposer, en cas
// de refus, qu'une capsule qui tienne le MÊME nombre de personnes.
inline int cap_equipage(const VehicleDesign& d) {
  const auto& caps = vehicle::capsule_catalog();
  if (d.capsule < 0 || d.capsule >= (int)caps.size()) return 0;
  return caps[static_cast<std::size_t>(d.capsule)].crew;
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

  std::vector<StageSpec>  specs;
  std::vector<PowerPlant> plants;
  specs.reserve(d.stages.size());
  plants.reserve(d.stages.size());
  for (const auto& sp : d.stages) {
    const EnginePart& e = engs[static_cast<std::size_t>(
        detail::clampi(sp.engine, 0, (int)engs.size() - 1))];
    const TankPart& t = tanks[static_cast<std::size_t>(
        detail::clampi(sp.tank, 0, (int)tanks.size() - 1))];
    // ═══ LA CENTRALE EST DE LA MASSE SÈCHE ═══ [GDD 5.12.1, 6.1, 6.5]
    // Elle entre dans la structure de l'étage, donc Tsiolkovsky la PAIE — et
    // c'est le seul endroit où elle doit entrer : le haut Isp d'une filière
    // alimentée s'achète en masse morte, pas en rien.
    const PowerPlant pp = power_plant_for(e, sp.source);
    plants.push_back(pp);
    if (pp.source_missing) s.power_ok = false;
    s.powerplant_mass_kg += pp.total_mass_kg();

    StageSpec spec;
    spec.dv_target = std::max(0.0, sp.dv_target_ms);
    spec.engine = to_engine(e);
    spec.tank_dry_fraction = t.dry_fraction;
    spec.structure_mass = std::max(0.0, sp.structure_mass_kg) + pp.total_mass_kg();
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
    ro.power = plants[k];
    s.stages.push_back(ro);
  }

  // FAISABILITÉ AU DÉCOLLAGE [GDD 6.3] : l'étage du bas doit être IMPULSIONNEL.
  // Un propulseur électrique/NEP (régime continu) ne décolle jamais d'un sol.
  const EnginePart& bas = engs[static_cast<std::size_t>(
      detail::clampi(d.stages.front().engine, 0, (int)engs.size() - 1))];
  const PropFamilyClass* fam = prop_family(bas.family);
  s.liftoff_capable = fam && fam->regime == BurnRegime::Impulsive;

  // ═══ LE BOUCLIER EST DIMENSIONNÉ ICI, PAS DÉCORATIF ═══ [GDD 9.2, 7.6]
  // `flight/Reentry.hpp` portait 120 oracles et zéro appelant, pendant que
  // `CapsulePart` traînait cinq champs (Cd hypersonique, section, rayon de nez,
  // finesse, limite en g) qui n'existent QUE pour lui, et que l'arbre vendait
  // trois nœuds de rentrée. La masse qui rentre, c'est la capsule PLUS ce qu'elle
  // ramène : c'est ce couple qui décide, et le joueur le choisit ici.
  const auto& caps = capsule_catalog();
  if (d.capsule >= 0 && d.capsule < (int)caps.size() && d.v_interface_retour_ms > 0.0) {
    const CapsulePart& cap = caps[static_cast<std::size_t>(d.capsule)];
    const env::ExponentialAtmosphere atmo = env::earth_atmosphere(1.0);
    s.rentree = fen::mission::evaluer_rentree(
        cap, cap.dry_mass_kg + std::max(0.0, d.payload_kg), d.v_interface_retour_ms,
        atmo, cst::MU_EARTH);
  }

  // LA CAUSE LA PLUS ACTIONNABLE D'ABORD (piège n°42). Une filière alimentée
  // sans source rend TOUTES les masses fausses — la centrale y pèse zéro — donc
  // ce motif prime sur une non-convergence, qui n'en serait que le symptôme.
  if (!s.power_ok)
    s.warning = "filiere alimentee sans source d energie : choisir solaire, RTG ou reacteur [GDD 5.12.1]";
  else if (!s.converged) s.warning = "sizing non convergent (Dv trop ambitieux ?)";
  else if (!s.liftoff_capable)
    s.warning = "etage du bas en regime continu : incapable de decoller [GDD 6.3]";
  else if (s.rentree.evalue && !s.rentree.ok) {
    // UN REFUS NOMME LA DIRECTION : de combien on dépasse, et avec quoi ça passe.
    const fen::vehicle::CapsulePart* mieux = fen::mission::capsule_capable(
        d.v_interface_retour_ms, s.rentree.masse_rentree_kg,
        env::earth_atmosphere(1.0), cst::MU_EARTH, cap_equipage(d));
    // FORME COURTE, MESURÉE SUR CAPTURE : la rédaction longue (la cause complète
    // suivie de la marge et du renvoi au GDD) était TRONQUÉE au bord du panneau,
    // et c'est le nom de la capsule capable qui disparaissait — la seule partie
    // ACTIONNABLE. Deuxième fois de la journée : ce qui compte se met devant.
    s.warning = "rentree refusee : flux a "
              + std::to_string((int)std::lround(100.0 / std::max(1e-9, s.rentree.marge_flux)))
              + " % du tenable"
              + (mieux ? std::string(" ; capable : ") + mieux->id : std::string())
              + " [9.2]";
  }
  return s;
}

} // namespace fen::app
