// fen/tech/Unlock.hpp — règle du verrou le plus fort [GDD 5.4, 19.2]
//
// Une capacité n'est débloquée que par l'INTERSECTION de quatre axes :
//   RANG   droit institutionnel     (career::Rank)
//   TRL    le monde sait faire      (tech::TechTree, TOUS les nœuds requis)
//   BUDGET capacité à financer      (economy::Treasury, via valeur passée)
//   INFRA  moyens physiques         (IInfrastructureProvider — Novellus + branche 6)
// LE PLUS CONTRAIGNANT FAIT FOI. Le rang ne remplace JAMAIS la science ; une
// techno mûre hors rang reste inaccessible ; une capacité qualifiée sans
// radiateurs ou sans budget reste au sol. Aucun "saut" possible.
//
// NB : l'interface d'infrastructure est déclarée ICI (Phase 7) et implémentée
// par la station (Phase 10) — résolution de la back-dépendance [carte P4].
#pragma once
#include <string>
#include <vector>
#include "fen/career/Career.hpp"
#include "fen/tech/TechTree.hpp"

namespace fen::tech {

// Besoins physiques d'une capacité. Étendu au fil des filières (branche 6).
struct InfrastructureNeed {
  double power_kw{0.0};            // puissance disponible requise
  double thermal_reject_kw{0.0};   // capacité de rejet thermique requise
  int    station_tier{0};          // palier Novellus minimal [GDD 11.3]
  bool   nuclear_test_bench{false};// bancs d'essai nucléaires [GDD 5.12.8]
  bool   heavy_launch{false};      // cadence/lanceur lourd (branche 1)
};

// Implémenté par station::Station (Phase 10) et par les installations sol.
class IInfrastructureProvider {
 public:
  virtual ~IInfrastructureProvider() = default;
  virtual bool provides(const InfrastructureNeed& need) const = 0;
  virtual std::string missing(const InfrastructureNeed& need) const = 0;
};

// --- La capacité à déverrouiller ---------------------------------------------
struct Capability {
  std::string id;
  std::string name;
  career::Rank min_rank{career::Rank::Stagiaire};
  std::vector<std::string> required_tech;   // TOUS opérationnels (TRL >= 7)
  double cost_musd{0.0};                    // engagement financier au déblocage
  InfrastructureNeed infra;
};

// --- Verdict -----------------------------------------------------------------
enum class LockAxis { None = 0, Rank, Trl, Budget, Infrastructure };
inline const char* lock_name(LockAxis a) {
  switch (a) {
    case LockAxis::None: return "DEBLOQUE";
    case LockAxis::Rank: return "RANG";
    case LockAxis::Trl:  return "MATURITE (TRL)";
    case LockAxis::Budget: return "BUDGET";
    default: return "INFRASTRUCTURE";
  }
}

struct UnlockVerdict {
  bool rank_ok{}, trl_ok{}, budget_ok{}, infra_ok{};
  LockAxis dominant{LockAxis::None};  // le verrou le plus fort (pour l'UI)
  std::vector<std::string> reasons;   // toutes les raisons, lisibles
  bool unlocked() const { return rank_ok && trl_ok && budget_ok && infra_ok; }
};

// --- Évaluateur --------------------------------------------------------------
// Ordre de dominance pour l'affichage : TRL > INFRA > RANG > BUDGET.
// (Un manque de science est plus structurel qu'un manque d'argent.)
inline UnlockVerdict evaluate_unlock(const Capability& cap,
                                     const career::CareerState& career,
                                     const TechTree& tree,
                                     double treasury_available_musd,
                                     const IInfrastructureProvider* infra) {
  UnlockVerdict v;

  v.rank_ok = career.rank >= cap.min_rank;
  if (!v.rank_ok)
    v.reasons.push_back(std::string("rang requis : ") + career::rank_name(cap.min_rank));

  v.trl_ok = true;
  for (const auto& id : cap.required_tech) {
    const TechNode* n = tree.find(id);
    if (!n || !n->operational()) {
      v.trl_ok = false;
      v.reasons.push_back("techno non qualifiee : " + (n ? n->name : id));
    }
  }

  v.budget_ok = treasury_available_musd >= cap.cost_musd;
  if (!v.budget_ok) v.reasons.push_back("budget insuffisant");

  v.infra_ok = (infra == nullptr) ? (cap.infra.power_kw <= 0.0 &&
                                     cap.infra.station_tier <= 0 &&
                                     !cap.infra.nuclear_test_bench &&
                                     !cap.infra.heavy_launch)
                                  : infra->provides(cap.infra);
  if (!v.infra_ok)
    v.reasons.push_back(infra ? infra->missing(cap.infra) : "aucune infrastructure");

  if      (!v.trl_ok)    v.dominant = LockAxis::Trl;
  else if (!v.infra_ok)  v.dominant = LockAxis::Infrastructure;
  else if (!v.rank_ok)   v.dominant = LockAxis::Rank;
  else if (!v.budget_ok) v.dominant = LockAxis::Budget;
  return v;
}

} // namespace fen::tech
