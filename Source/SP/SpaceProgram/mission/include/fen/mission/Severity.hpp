// fen/mission/Severity.hpp — gravité et conséquences [GDD 10.3-10.4]
//
// Un échec n'est JAMAIS un malus abstrait. Cinq niveaux, des modificateurs de
// palier, et une TRIPLE LECTURE : technique / programmatique / humain — une
// mission peut être techniquement sauvée et programmatiquement désastreuse.
// Le ConsequenceEngine écrit dans : budget, confiance ARES, carrière,
// disponibilité des contrats, requalification techno [GDD 10.4].
// INVARIANT : la mort du personnage (niveau 5) est un Game Over sans annulation.
#pragma once
#include <algorithm>
#include <string>
#include <vector>
#include "fen/career/Career.hpp"

namespace fen::mission {

enum class Severity { Minor = 1, Moderate = 2, Major = 3, Critical = 4, Catastrophe = 5 };

// Modificateurs de palier [GDD 10.3] : le contexte aggrave l'issue.
struct SeverityModifiers {
  bool human_lethal_exposure{false};   // présence humaine exposée à un risque létal
  bool primary_objective_lost{false};  // objectif primaire d'une mission prioritaire
  bool unique_vehicle_lost{false};     // véhicule unique ou irremplaçable
  bool massive_debris{false};          // création massive de débris [GDD 10.5]
  bool player_error_causal{false};     // erreur du joueur documentée cause racine
  bool repeated_anomaly{false};        // anomalie déjà signalée, non corrigée
  bool brilliant_recovery{false};      // sauvetage brillant : -1/2 palier
};

// Palier final = base + aggravations (+1 chacune, saturé à 5), - arrondi du
// demi-palier de rétrogradation si sauvetage brillant SANS perte durable.
inline Severity apply_modifiers(Severity base, const SeverityModifiers& m) {
  int lvl = static_cast<int>(base) * 2;             // travaille en demi-paliers
  if (m.human_lethal_exposure)  lvl += 2;
  if (m.primary_objective_lost) lvl += 2;
  if (m.unique_vehicle_lost)    lvl += 2;
  if (m.massive_debris)         lvl += 2;
  if (m.player_error_causal)    lvl += 2;
  if (m.repeated_anomaly)       lvl += 2;
  if (m.brilliant_recovery)     lvl -= 1;           // -1/2 palier
  lvl = std::clamp(lvl, 2, 10);
  return static_cast<Severity>(lvl / 2);
}

// --- L'anomalie --------------------------------------------------------------
struct AnomalyEvent {
  std::string mission_id;
  std::string what;                  // description technique
  Severity severity{Severity::Minor};
  SeverityModifiers modifiers;
  std::vector<std::string> tech_involved;  // nœuds candidats à requalification
  double date_days{};

  // CONTEXTE ORBITAL DE LA RUPTURE [GDD 7.8, 10.5]. Renseigné uniquement quand
  // l'anomalie a fragmenté quelque chose EN ORBITE : le nuage de débris qui en
  // résulte est alors calculé (env::Debris), pas décrété. `breakup_mass_kg = 0`
  // signifie « aucune fragmentation » — un échec au sol ne pollue rien.
  double breakup_mass_kg{0.0};
  double breakup_alt_km{0.0};
  bool   breakup_is_collision{false};   // sinon : explosion (rupture interne)
};

// --- Triple lecture [GDD 10.4] -----------------------------------------------
struct Consequences {
  // technique
  std::vector<std::string> requalify_tech;  // TRL régresse (tech::requalify)
  bool root_cause_investigation{false};     // enquête interne obligatoire
  bool corrective_actions_required{false};  // à financer avant retour en vol
  // programmatique
  double budget_penalty_frac{};             // sur le prochain contrat du programme
  double confidence_loss{};                 // points ARES
  double contract_delay_days{};             // retard d'ouverture de la filière
  bool   mission_family_suspended{false};
  // humain / carrière
  bool   promotion_blocked{false};
  double promotion_freeze_days{};
  bool   game_over{false};                  // décès du personnage
};

// Barèmes du GDD 10.3 — milieux de fourchette, déclarés.
inline Consequences consequences_for(const AnomalyEvent& ev) {
  Consequences c;
  switch (ev.severity) {
    case Severity::Minor:
      c.budget_penalty_frac = 0.03; c.confidence_loss = 2.0;
      break;
    case Severity::Moderate:
      c.budget_penalty_frac = 0.10; c.confidence_loss = 6.0;
      c.contract_delay_days = 60.0;
      break;
    case Severity::Major:
      c.budget_penalty_frac = 0.25; c.confidence_loss = 15.0;
      c.mission_family_suspended = true;
      c.root_cause_investigation = true; c.corrective_actions_required = true;
      c.promotion_freeze_days = 365.0;
      c.requalify_tech = ev.tech_involved;
      break;
    case Severity::Critical:
      c.budget_penalty_frac = 0.50; c.confidence_loss = 30.0;
      c.mission_family_suspended = true;
      c.root_cause_investigation = true; c.corrective_actions_required = true;
      c.promotion_blocked = true;
      c.requalify_tech = ev.tech_involved;
      break;
    case Severity::Catastrophe:
      c.budget_penalty_frac = 0.70; c.confidence_loss = 45.0;
      c.mission_family_suspended = true;
      c.root_cause_investigation = true; c.corrective_actions_required = true;
      c.promotion_blocked = true;
      c.requalify_tech = ev.tech_involved;
      // game_over est décidé par l'appelant : SEUL le décès du PERSONNAGE
      // (pas d'un équipage PNJ) termine la partie [GDD 10.3 niveau 5].
      break;
  }
  return c;
}

// Application côté carrière — l'économie et l'arbre sont servis par l'appelant
// (ConsequenceEngine du GameState) pour garder les modules découplés.
inline void apply_to_career(const Consequences& c, career::CareerState& cs,
                            double now_days) {
  cs.confidence_ares = std::max(0.0, cs.confidence_ares - c.confidence_loss);
  if (c.promotion_blocked) { cs.promotion_frozen = true; cs.frozen_until_days = 1e18; }
  else if (c.promotion_freeze_days > 0.0) {
    cs.promotion_frozen = true;
    cs.frozen_until_days = now_days + c.promotion_freeze_days;
  }
}

} // namespace fen::mission
