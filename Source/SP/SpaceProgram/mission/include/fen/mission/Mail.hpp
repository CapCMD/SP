// fen/mission/Mail.hpp — LA BOÎTE MAIL ARES [GDD 4.1, 10.2, 15.3]
//
// « Les missions sont EXCLUSIVEMENT proposées par ARES par mail ; pas de
// catalogue libre. » [GDD 10.2] Le mail n'est donc pas de l'habillage : c'est
// le SEUL canal par lequel un contrat entre dans la partie, et le canal par
// lequel l'agence notifie ce qu'elle impose (10.3 : enquêtes, actions
// correctives ; 3.3 : promotions ; 13.2 : alertes budgétaires).
//
// Ce module manquait au portage : `MissionContract::mail_body` renvoyait à un
// « MailInbox (M6) » qui n'existait pas, si bien que le catalogue était de fait
// consultable librement — contraire à 10.2.
//
// RÈGLE STRUCTURANTE : un contrat ne devient JOUABLE que par le mail qui le
// porte. Le catalogue reste le référentiel des verrous [MissionFsm.hpp] ; la
// boîte mail est ce qui DÉCLENCHE. Tant qu'un contrat n'a pas été notifié, le
// joueur ne le voit pas — même s'il en remplit tous les prérequis.
//
// C++ pur, aucune dépendance UnrealEngine.
#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "fen/career/Career.hpp"
#include "fen/mission/MissionFsm.hpp"
#include "fen/tech/TechTree.hpp"

namespace fen::mission {

// Nature du courrier — décide du ton et de ce que le joueur peut en faire.
enum class MailKind {
  Contract = 0,     // proposition de mission : SEULE porte d'entrée d'un contrat
  Directive,        // instruction institutionnelle (actions correctives, gel)
  Inquiry,          // ouverture d'enquête interne [GDD 10.3]
  Promotion,        // changement de rang [GDD 3.3]
  BudgetAlert,      // palier de trésorerie franchi [GDD 13.2]
  Notice,           // information sans effet mécanique
};

inline const char* mail_kind_name(MailKind k) {
  switch (k) {
    case MailKind::Contract:    return "CONTRAT";
    case MailKind::Directive:   return "DIRECTIVE";
    case MailKind::Inquiry:     return "ENQUETE";
    case MailKind::Promotion:   return "PROMOTION";
    case MailKind::BudgetAlert: return "ALERTE BUDGET";
    default:                    return "INFORMATION";
  }
}

struct MailMessage {
  std::string id;
  MailKind    kind{MailKind::Notice};
  std::string from{"ARES / Direction des programmes"};
  std::string subject;
  std::string body;
  double      date_days{};
  bool        read{false};
  // Renseigné pour les mails de contrat : l'identifiant du MissionContract que
  // ce courrier autorise. Vide pour tous les autres.
  std::string contract_id;
  // Un contrat notifié attend une décision ; une fois acceptée ou déclinée, le
  // mail reste dans la boîte (mémoire du programme) mais n'est plus actionnable.
  bool        answered{false};
};

// ---------------------------------------------------------------------------
class MailInbox {
 public:
  void receive(MailMessage m) { messages_.push_back(std::move(m)); }

  // Notifier un contrat. C'est l'unique fabrique d'un mail de contrat : elle
  // garantit que `contract_id` est renseigné et que le corps vient bien du
  // contrat (aucun texte inventé côté UI).
  void notify_contract(const MissionContract& c, double now_days) {
    if (has_contract(c.id)) return;             // jamais deux fois le même
    MailMessage m;
    m.id = "MAIL-" + c.id;
    m.kind = MailKind::Contract;
    m.subject = c.title;
    m.body = c.mail_body;
    m.date_days = now_days;
    m.contract_id = c.id;
    messages_.push_back(std::move(m));
  }

  bool has_contract(const std::string& contract_id) const {
    return std::any_of(messages_.begin(), messages_.end(),
                       [&](const MailMessage& m) { return m.contract_id == contract_id; });
  }

  // LE VERROU DE 10.2 : un contrat n'est ouvert que s'il a été notifié.
  bool contract_notified(const std::string& contract_id) const {
    return has_contract(contract_id);
  }

  MailMessage* find(const std::string& id) {
    for (auto& m : messages_) if (m.id == id) return &m;
    return nullptr;
  }
  const MailMessage* find(const std::string& id) const {
    for (const auto& m : messages_) if (m.id == id) return &m;
    return nullptr;
  }

  bool mark_read(const std::string& id) {
    if (MailMessage* m = find(id)) { m->read = true; return true; }
    return false;
  }
  bool mark_answered(const std::string& id) {
    if (MailMessage* m = find(id)) { m->answered = true; m->read = true; return true; }
    return false;
  }

  int unread_count() const {
    int n = 0;
    for (const auto& m : messages_) if (!m.read) ++n;
    return n;
  }
  // Contrats notifiés en attente de décision — ce que le poste PLANIFICATION
  // doit montrer en premier.
  std::vector<const MailMessage*> pending_contracts() const {
    std::vector<const MailMessage*> v;
    for (const auto& m : messages_)
      if (m.kind == MailKind::Contract && !m.answered) v.push_back(&m);
    return v;
  }

  const std::vector<MailMessage>& messages() const { return messages_; }
  std::vector<MailMessage>& messages_mut() { return messages_; }
  void clear() { messages_.clear(); }

 private:
  std::vector<MailMessage> messages_;
};

// ---------------------------------------------------------------------------
// LE FACTEUR : parcourt le catalogue et notifie ce qui vient de devenir
// jouable. ARES propose « des missions planifiées de longue date, visibles
// conceptuellement avant d'être jouables, mais VERROUILLÉES jusqu'à obtention
// de tous leurs prérequis » [GDD 4.2] : c'est donc le franchissement des quatre
// verrous qui déclenche le courrier, jamais un minuteur.
//
// Renvoie le nombre de contrats nouvellement notifiés.
inline int deliver_unlocked_contracts(MailInbox& inbox, const MissionCatalog& catalog,
                                      const career::CareerState& career,
                                      const tech::TechTree& tree,
                                      double treasury_available,
                                      const tech::IInfrastructureProvider* infra,
                                      double now_days) {
  int n = 0;
  const auto& entries = catalog.entries();
  for (std::size_t i = 0; i < entries.size(); ++i) {
    const auto& e = entries[i];
    if (inbox.has_contract(e.contract.id)) continue;
    const auto a = catalog.check(i, career, tree, treasury_available, infra, now_days);
    if (!a.playable) continue;
    inbox.notify_contract(e.contract, now_days);
    ++n;
  }
  return n;
}

} // namespace fen::mission
