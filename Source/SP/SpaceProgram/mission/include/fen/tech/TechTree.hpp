// fen/tech/TechTree.hpp — arbre technologique [GDD 5]
//
// Six branches, DAG de nœuds à prérequis multiples. Une mission avancée ne
// dépend JAMAIS d'une techno isolée mais d'un ensemble cohérent de maturités
// [GDD 4.2, 19.6]. Les dépendances TRANSVERSES (thermique, matériaux HT...)
// sont des nœuds distribués qui BLOQUENT des programmes entiers [GDD 5.13] :
// une NEP mégawatt reste verrouillée sans thermique/radiateurs ni matériaux HT,
// même si le réacteur est mûr.
// Le déblocage final (rang/TRL/budget/infra) est dans Unlock.hpp — l'arbre ne
// répond qu'à UNE question : qu'est-ce que le MONDE sait faire (TRL) ?
#pragma once
#include <algorithm>
#include <string>
#include <vector>
#include "fen/career/Career.hpp"

namespace fen::tech {

// Les six branches [GDD 5.2].
enum class Branch {
  OrbitAccess = 0, Robotic = 1, CrewedLeo = 2, LongDuration = 3,
  InterplanetaryNav = 4, EnergyPropulsion = 5,
};
inline const char* branch_name(Branch b) {
  switch (b) {
    case Branch::OrbitAccess:       return "Acces a l'orbite";
    case Branch::Robotic:           return "Exploration robotique";
    case Branch::CrewedLeo:         return "Vol habite proche Terre";
    case Branch::LongDuration:      return "Autonomie longue duree";
    case Branch::InterplanetaryNav: return "Navigation interplanetaire";
    default:                        return "Energie et propulsion avancee";
  }
}

// TRL 1-9 [GDD 5.4] : 1-3 recherche, 4-6 démonstration/qualification, 7-9 opérationnel.
// Une techno n'est UTILISABLE en mission qu'à TRL >= 7 (qualification comprise).
inline constexpr int TRL_OPERATIONAL = 7;

struct TechNode {
  std::string id;                    // ex: "nep_megawatt"
  std::string name;
  Branch branch{};
  int trl{1};                        // état COURANT de maturité mondiale
  int trl_start{1};                  // TRL au début de partie
  std::vector<std::string> prereqs;  // ids — TOUS requis (DAG)
  double research_cost_musd{};
  double research_days{};            // durée nominale [GDD 4.3]
  career::Rank min_rank{career::Rank::Stagiaire};  // filtre institutionnel
  bool transverse{false};            // transverse [GDD 5.13] : bloque plusieurs branches

  // COÛTS ET DURÉES : PROVISOIRES PAR DÉFAUT. Le GDD renvoie explicitement à
  // « une version ultérieure » les « sous-arbres nominatifs et CHIFFRÉS de
  // chaque branche, avec coûts et durées de recherche unitaires » [GDD 20].
  // Les valeurs portées ici sont donc des ordres de grandeur d'attente, calés
  // sur les fourchettes de maturité de 4.3 (« quelques jours à quelques
  // semaines » pour l'état de l'art, « plusieurs années » pour une percée).
  // Elles sont MARQUÉES comme telles pour qu'on ne les prenne jamais pour du
  // design validé — c'est la même exigence de traçabilité que 6.8 et 12.3.
  bool costs_provisional{true};

  bool operational() const { return trl >= TRL_OPERATIONAL; }
};

struct ResearchProject {
  std::string node_id;
  double days_done{};
  double days_total{};
  bool   priority_program{false};    // priorité mission critique [GDD 4.3]
  double progress() const { return days_total > 0 ? days_done / days_total : 0.0; }
  bool   done() const { return days_done >= days_total; }
};

class TechTree {
 public:
  void add(TechNode n) { nodes_.push_back(std::move(n)); }
  const TechNode* find(const std::string& id) const {
    for (const auto& n : nodes_) if (n.id == id) return &n;
    return nullptr;
  }
  TechNode* find_mut(const std::string& id) {
    for (auto& n : nodes_) if (n.id == id) return &n;
    return nullptr;
  }
  // Prérequis satisfaits = tous les parents opérationnels. C'est la SEULE
  // condition côté arbre ; le reste (rang, budget, infra) vit dans Unlock.hpp.
  bool researchable(const std::string& id) const {
    const TechNode* n = find(id);
    if (!n || n->operational()) return false;
    for (const auto& p : n->prereqs) {
      const TechNode* pn = find(p);
      if (!pn || !pn->operational()) return false;
    }
    return true;
  }
  const std::vector<TechNode>& all() const { return nodes_; }

 private:
  std::vector<TechNode> nodes_;
};

// --- File de recherche [GDD 4.3] ---------------------------------------------
// Parallélisme LIMITÉ par le rang : c'est la capacité institutionnelle d'ARES,
// jamais une file infinie. Priorisation : science < programme < institution —
// les travaux liés à une fenêtre proche passent devant.
class ResearchQueue {
 public:
  // Démarre si un slot est libre au rang courant. Renvoie faux sinon.
  bool start(const TechTree& tree, const std::string& node_id,
             career::Rank rank, bool critical_program = false) {
    if (!tree.researchable(node_id)) return false;
    for (const auto& p : active_) if (p.node_id == node_id) return false;
    const int cap = career::max_parallel_research(rank, critical_program);
    if (static_cast<int>(active_.size()) >= cap) return false;
    const TechNode* n = tree.find(node_id);
    active_.push_back(ResearchProject{node_id, 0.0, n->research_days,
                                      critical_program});
    return true;
  }
  // Avance toutes les recherches de dt jours ; les nœuds terminés passent
  // TRL -> 7 (qualification incluse dans research_days, V1).
  std::vector<std::string> tick(TechTree& tree, double dt_days) {
    std::vector<std::string> completed;
    for (auto& p : active_) p.days_done += dt_days;
    for (auto it = active_.begin(); it != active_.end();) {
      if (it->done()) {
        if (TechNode* n = tree.find_mut(it->node_id)) n->trl = TRL_OPERATIONAL;
        completed.push_back(it->node_id);
        it = active_.erase(it);
      } else ++it;
    }
    return completed;
  }
  const std::vector<ResearchProject>& active() const { return active_; }

 private:
  std::vector<ResearchProject> active_;
};

// Requalification après incident [GDD 10.4] : un nœud impliqué dans un échec
// RÉGRESSE sous le seuil opérationnel — il faudra re-payer la qualification.
inline void requalify(TechTree& tree, const std::string& node_id) {
  if (TechNode* n = tree.find_mut(node_id))
    n->trl = std::min(n->trl, TRL_OPERATIONAL - 1);
}

} // namespace fen::tech
