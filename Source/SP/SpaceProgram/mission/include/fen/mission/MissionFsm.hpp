// fen/mission/MissionFsm.hpp — cycle de vie d'une mission [GDD 4.1, 4.2, 10.2]
//
// Les contrats arrivent EXCLUSIVEMENT par mail ARES : pas de catalogue libre,
// pas de négociation sur le fond [GDD 3.1]. Le catalogue montre des missions
// planifiées de longue date, VERROUILLÉES tant que l'ensemble cohérent de
// prérequis n'est pas réuni (PrerequisiteBundle = jamais une techno isolée)
// [GDD 4.2, 19.6]. La FSM est stricte : pas de saut d'état, et les états
// terminaux le restent.
#pragma once
#include <string>
#include <vector>
#include "fen/mission/Events.hpp"
#include "fen/mission/Program.hpp"
#include "fen/mission/Severity.hpp"
#include "fen/tech/Unlock.hpp"

namespace fen::mission {

// États du cycle [GDD 4.1] + terminaux.
enum class MissionState {
  Received = 0,     // contrat reçu (mail ARES)
  Prerequisites,    // vérification techno/budget/logistique/humain
  Design,           // conception terminal (trajectoire, véhicule, budgets)
  WindowSearch,     // détermination de la fenêtre (positions réelles)
  Qualification,    // essais, revue
  Launched,         // exploitation : suivi, anomalies, corrections [GDD 8]
  Debrief,          // triple débrief : technique/programmatique/institutionnel
  Completed, Failed, Aborted,
};
inline const char* state_name(MissionState s) {
  switch (s) {
    case MissionState::Received:      return "RECU";
    case MissionState::Prerequisites: return "PREREQUIS";
    case MissionState::Design:        return "CONCEPTION";
    case MissionState::WindowSearch:  return "FENETRE";
    case MissionState::Qualification: return "QUALIFICATION";
    case MissionState::Launched:      return "EXPLOITATION";
    case MissionState::Debrief:       return "DEBRIEF";
    case MissionState::Completed:     return "TERMINEE";
    case MissionState::Failed:        return "ECHOUEE";
    default:                          return "ABANDONNEE";
  }
}

// --- Le contrat étendu (mail ARES) [GDD 10.2] --------------------------------
// Enveloppe le Contract physique de Program.hpp avec l'habillage institutionnel.
struct MissionContract {
  std::string id;
  std::string title;
  std::string mail_body;              // le texte reçu dans MailInbox (M6)
  Contract    terms;                  // masse/budget/délai/P(succès) exigés
  bool        crewed{false};
  bool        priority{false};        // mission prioritaire [GDD 10.3 modif.]
  std::string family;                 // filière (suspension par famille [10.4])
  tech::Capability prerequisites;     // bundle de verrous [GDD 4.2] — évalué
                                      // par tech::evaluate_unlock (les 4 axes)
};

// --- La mission vivante ------------------------------------------------------
struct Mission {
  MissionContract contract;
  MissionState state{MissionState::Received};
  FlightPhase phase{FlightPhase::Ground};   // pilote EventSampler [Events.hpp]
  double state_entered_days{};
  std::vector<AnomalyEvent> anomalies;
  Severity worst_severity{Severity::Minor};
  bool any_anomaly{false};

  // Transitions LÉGALES uniquement. Tout le reste est un bug d'appelant.
  bool can_advance_to(MissionState next) const {
    switch (state) {
      case MissionState::Received:      return next == MissionState::Prerequisites
                                            || next == MissionState::Aborted;
      case MissionState::Prerequisites: return next == MissionState::Design
                                            || next == MissionState::Aborted;
      case MissionState::Design:        return next == MissionState::WindowSearch
                                            || next == MissionState::Aborted;
      case MissionState::WindowSearch:  return next == MissionState::Qualification
                                            || next == MissionState::Design    // re-conception
                                            || next == MissionState::Aborted;
      case MissionState::Qualification: return next == MissionState::Launched
                                            || next == MissionState::Design
                                            || next == MissionState::Aborted;
      case MissionState::Launched:      return next == MissionState::Debrief;
      case MissionState::Debrief:       return next == MissionState::Completed
                                            || next == MissionState::Failed;
      default: return false;            // états terminaux : AUCUNE sortie
    }
  }
  bool advance(MissionState next, double now_days) {
    if (!can_advance_to(next)) return false;
    state = next;
    state_entered_days = now_days;
    return true;
  }
  void record_anomaly(const AnomalyEvent& ev) {
    anomalies.push_back(ev);
    any_anomaly = true;
    if (ev.severity > worst_severity) worst_severity = ev.severity;
  }
};

// --- Le catalogue [GDD 4.2] --------------------------------------------------
// Visible conceptuellement AVANT d'être jouable : le joueur voit ce qui existe,
// et voit le verrou dominant qui le bloque (pédagogie du verrou le plus fort).
struct CatalogEntry {
  MissionContract contract;
  bool suspended{false};              // famille suspendue post-incident [10.4]
  double available_after_days{0.0};   // retard d'ouverture [10.3]
};

class MissionCatalog {
 public:
  void add(CatalogEntry e) { entries_.push_back(std::move(e)); }

  struct Availability {
    bool playable{};
    tech::UnlockVerdict verdict;      // le POURQUOI, affichable
    bool suspended{};
    bool delayed{};
  };
  Availability check(std::size_t i, const career::CareerState& career,
                     const tech::TechTree& tree, double treasury_available,
                     const tech::IInfrastructureProvider* infra,
                     double now_days) const {
    Availability a;
    const CatalogEntry& e = entries_[i];
    a.verdict = tech::evaluate_unlock(e.contract.prerequisites, career, tree,
                                      treasury_available, infra);
    a.suspended = e.suspended;
    a.delayed = now_days < e.available_after_days;
    a.playable = a.verdict.unlocked() && !a.suspended && !a.delayed;
    return a;
  }
  std::vector<CatalogEntry>& entries() { return entries_; }
  const std::vector<CatalogEntry>& entries() const { return entries_; }

 private:
  std::vector<CatalogEntry> entries_;
};

} // namespace fen::mission
