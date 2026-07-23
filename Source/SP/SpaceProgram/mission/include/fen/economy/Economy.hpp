// fen/economy/Economy.hpp — trésorerie, coûts fixes, sites [GDD 13, 4.4]
//
// L'agence a des COÛTS FIXES RÉCURRENTS : accélérer le temps SANS revenus fait
// fondre la trésorerie [GDD 14.2] jusqu'à la chaîne avertissement -> gel ->
// LICENCIEMENT (fin de partie) [GDD 13.2]. Un fonds de réserve obligatoire
// encaisse les imprévus. Trois niveaux de ressources SÉPARÉS [GDD 4.4] : une
// décision de mission (marge d'ergols) et une décision de programme (recherche
// coûteuse) restent arbitrables indépendamment.
#pragma once
#include <cmath>
#include <string>
#include <vector>

namespace fen::economy {

// --- Paliers d'alerte [GDD 13.2] --------------------------------------------
enum class AlertLevel {
  Normal = 0,        // > 75 %
  Watch = 1,         // 75-50 % : surveillance renforcée
  Delays = 2,        // 50-30 % : activités secondaires retardées
  Tension = 3,       // 30-15 % : capacité d'initiative réduite
  Crisis = 4,        // 15-5 %  : gel des projets non prioritaires
  Asphyxia = 5,      // < 5 %   : restructuration ou arrêt partiel
};
inline AlertLevel alert_level(double ratio01) {
  if (ratio01 > 0.75) return AlertLevel::Normal;
  if (ratio01 > 0.50) return AlertLevel::Watch;
  if (ratio01 > 0.30) return AlertLevel::Delays;
  if (ratio01 > 0.15) return AlertLevel::Tension;
  if (ratio01 > 0.05) return AlertLevel::Crisis;
  return AlertLevel::Asphyxia;
}

// --- Coûts fixes récurrents [GDD 13.2] ---------------------------------------
enum class Period { Monthly, Quarterly, Semiannual };
inline double period_days(Period p) {
  switch (p) {
    case Period::Monthly:   return 30.44;
    case Period::Quarterly: return 91.31;
    default:                return 182.62;
  }
}
struct FixedCost {
  std::string label;          // salaires, énergie, maintenance, conformité...
  double amount_musd{};
  Period period{Period::Monthly};
  double next_due_days{};     // temps de jeu (jours) du prochain prélèvement
};

// --- Trésorerie --------------------------------------------------------------
struct Treasury {
  double balance_musd{500.0};
  double target_musd{500.0};        // niveau cible : base des % d'alerte
  double reserve_musd{75.0};        // fonds de réserve OBLIGATOIRE [GDD 13.2]
  std::vector<FixedCost> fixed_costs;

  // Jours consécutifs passés sous le seuil de crise -> chaîne de fin de partie.
  double days_in_crisis{0.0};
  bool   contracts_frozen{false};
  bool   dismissed{false};          // licenciement : Game Over [GDD 3.4]

  double ratio() const { return target_musd > 0 ? balance_musd / target_musd : 0.0; }
  AlertLevel level() const { return alert_level(ratio()); }
  // Ce qui est ENGAGEABLE : le fonds de réserve n'est pas dépensable librement.
  double available_musd() const { return std::max(0.0, balance_musd - reserve_musd); }

  bool spend(double musd) {
    if (musd > available_musd()) return false;
    balance_musd -= musd;
    return true;
  }
  void income(double musd) { balance_musd += musd; }

  // Avance l'horloge économique : prélève les coûts fixes échus, met à jour la
  // chaîne avertissement -> gel (30 j de crise) -> licenciement (120 j).
  void tick(double now_days) {
    for (auto& c : fixed_costs) {
      while (now_days >= c.next_due_days) {
        balance_musd -= c.amount_musd;         // les coûts fixes, EUX, tombent
        c.next_due_days += period_days(c.period);
      }
    }
    if (level() >= AlertLevel::Crisis) days_in_crisis += 1.0; else days_in_crisis = 0.0;
    contracts_frozen = days_in_crisis > 30.0;
    dismissed = dismissed || days_in_crisis > 120.0;
  }
};

// --- Sites de lancement [GDD 13.3] -------------------------------------------
// La géographie est une CONTRAINTE PHYSIQUE : l'inclinaison minimale atteignable
// sans dog-leg est la latitude du site ; les azimuts autorisés bornent le reste.
struct LaunchSite {
  std::string name;
  double latitude_deg{};
  double azimuth_min_deg{}, azimuth_max_deg{};  // couloirs autorisés (survol)
  double cost_factor{1.0};                       // multiplicateur logistique
  double min_inclination_deg() const { return std::fabs(latitude_deg); }
  bool reachable(double inclination_deg) const {
    return inclination_deg >= min_inclination_deg() - 1e-9;
  }
};
inline const std::vector<LaunchSite>& launch_sites() {
  static const std::vector<LaunchSite> v = {
      {"Kourou",         5.24,  -10.5, 93.5, 1.00},  // quasi-équatorial : GTO roi
      {"Cape Canaveral", 28.45,  35.0, 120.0, 1.05},
      {"Baikonour",      45.96,  27.0, 90.0,  0.90},  // haut lat., mais pas cher
  };
  return v;
}

// --- Trois niveaux de ressources [GDD 4.4] -----------------------------------
// PROGRAMME : capacité globale d'ARES. RECHERCHE : maturation des technos.
// MISSION : contraintes physiques d'UNE architecture de vol.
struct ProgramResources {
  double launch_capacity_per_year{4.0};
  double launches_booked{0.0};
};
struct ResearchResources {
  int labs{1};
  int test_benches{0};
  bool nuclear_bench{false};        // requis palier 4+ [GDD 5.12.8]
};
struct MissionResources {           // par mission, PAS mutualisé
  double propellant_margin_kg{};
  double power_margin_w{};
  double consumables_margin_days{};
  double data_budget_gb{};
};
struct ResourceLedger {
  ProgramResources program;
  ResearchResources research;
  // Les MissionResources vivent dans chaque mission active, pas ici.
};

} // namespace fen::economy
