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

  // LA DURÉE DE TRANSIT VISÉE, capturée AU FEU VERT depuis la géométrie réelle
  // de la fenêtre (`astro::launch_window`). C'est la seule grandeur que la
  // chronologie de vol (FlightTimeline.hpp) ne sait pas dériver d'elle-même :
  // elle demande l'éphéméride, et surtout elle doit être FIGÉE au décollage —
  // recalculée plus tard, elle ferait glisser la date d'arrivée d'un vol déjà
  // parti. 0 = cible non nommée par le contrat, croisière non datée [GDD 6.8].
  double tof_days{0.0};

  // ═══ LA NAVIGATION RÉELLEMENT OBTENUE ═══ [GDD 8.1, 8.2]
  // Le Δv de correction que l'erreur d'injection RÉELLEMENT COMMISE exige.
  // Tirée au feu vert sur un sous-flux de la graine de mission (donc rejouable),
  // c'est un FAIT du vol, pas une probabilité : l'issue de navigation cesse
  // d'être un dé et devient la comparaison de ce nombre à la marge provisionnée.
  // `nav_evaluee` distingue « pas de navigation calculée » (familles sans cible
  // nommée, missions d'oracle) de « calculée et nulle ».
  bool   nav_evaluee{false};
  double nav_dv_required{0.0};   // m/s réellement dépensés en corrections
  double nav_miss_km{0.0};       // manque au but de la trajectoire RÉELLEMENT volée

  // ═══ LE VOL PILOTÉ PAR LE JOUEUR [GDD 7.4, 8.4] ═══
  // L'état VRAI (jamais montré), ce que la poursuite lui en a révélé, et ce
  // qu'il a dépensé en corrections. Le modèle ne corrige plus à sa place : il
  // applique ce qui est commandé, et rien d'autre.
  bool   vol_vrai_valide{false};
  double vol_vrai_t_days{0.0};
  double vol_vrai_r[3]{}, vol_vrai_v[3]{};
  double nav_connu_dv[3]{};      // écart de vitesse révélé par la poursuite
  double nav_sigma_r{0.0}, nav_sigma_v{0.0};
  double tcm_dv_depense{0.0};    // cumul, à comparer à la marge provisionnée
  int    tcm_faits{0};

  // ═══ LE RYTHME DE MESURE EST UN CHOIX DU JOUEUR ═══ [GDD 8.6]
  // « Le joueur choisit son rythme de mesure ; trop rare laisse dériver, trop
  // fréquent coûte des ressources et du temps. » La poursuite était achetée UNE
  // FOIS à la conception (`Program::tracking_days`) et ne bougeait plus : il n'y
  // avait donc aucun rythme à choisir. `poursuite_jours` est ce qu'il achète EN
  // VOL, en plus — des jours d'écoute que le DSN accorde à ce vaisseau.
  //
  // ON N'ACHÈTE PAS LE PASSÉ, ET ENCORE MOINS L'AVENIR : l'arc réellement
  // exploitable est borné par le temps ÉCOULÉ depuis l'injection (voir
  // `Session::arc_poursuite_disponible`). Acheter n'accélère rien — ça autorise
  // les antennes à continuer d'écouter.
  double poursuite_jours{0.0};   // arc ACHETÉ en vol, en plus du programme
  double arc_poursuite_j{0.0};   // arc réellement EXPLOITÉ par la solution courante

  // ═══ LE LOGICIEL DE VOL PART AVEC LE VÉHICULE [GDD 15.5, 18] ═══
  // Ce qui est à bord au feu vert y reste : dans ce modèle on ne téléverse pas
  // depuis le sol en cours de route. Ces deux faits sont donc figés au feu vert,
  // comme l'erreur d'injection — et sauvegardés pour la même raison.
  // `code_non_couvert` reprend le mot de [GDD 15.5] : le vol entamé sort de ce
  // que le banc d'essai a réellement exercé, et le comportement du code y est
  // NON COUVERT. C'est ce qui donne son prix à la qualification : sans cette
  // porte, acheter des heures d'essai ne changeait rien à l'issue.
  bool   code_embarque{false};
  bool   code_non_couvert{false};
  // CE QUE LE BANC COUVRAIT AU DÉCOLLAGE. Figée ici et pas relue de la fiche :
  // le joueur peut rouvrir son éditeur pendant la croisière, et un vol déjà
  // parti ne doit pas voir sa couverture bouger sous lui. C'est contre CE
  // nombre, et lui seul, que se tire la tenue des rendez-vous en vol — « le banc
  // rassure sans garantir » [GDD 15.5].
  double code_couverture{0.0};

  // ═══ QUI A CONDUIT LE VOL ═══
  // Un mécanisme que le joueur ne voit pas ne lui apprend rien (piège n°42) :
  // au débrief il doit lire « personne n'a tenu vos rendez-vous », pas un manque
  // au but inexpliqué. C'est un FAIT du vol, donc il vit ici et se sauvegarde.
  //   0 personne · 1 le joueur, de sa main · 2 le logiciel de bord · 3 l'adjoint
  int    vol_conduit_par{0};

  // ═══ L'ISSUE DU VOL APPARTIENT À LA MISSION ═══
  // Elle vivait sur la session d'UI (`Session::mission_outcome`), qui ne se
  // sauvegarde pas : un vol exécuté puis rechargé perdait son résultat, et le
  // débrief concluait « échec » sur une mission réussie. Le vol est un FAIT du
  // modèle — il est donc consigné ici, et c'est ce que le débrief lit.
  bool flight_flown{false};       // le vol a été exécuté, l'issue est connue
  bool flight_success{false};
  bool flight_has_anomaly{false};
  AnomalyEvent flight_anomaly;    // renseignée si échec, à passer à apply_anomaly

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
