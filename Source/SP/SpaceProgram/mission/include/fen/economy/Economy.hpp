// fen/economy/Economy.hpp — trésorerie, coûts fixes, sites [GDD 13, 4.4]
//
// L'agence a des COÛTS FIXES RÉCURRENTS : accélérer le temps SANS revenus fait
// fondre la trésorerie [GDD 14.2] jusqu'à la chaîne avertissement -> gel ->
// LICENCIEMENT (fin de partie) [GDD 13.2]. Un fonds de réserve obligatoire
// encaisse les imprévus. Trois niveaux de ressources SÉPARÉS [GDD 4.4] : une
// décision de mission (marge d'ergols) et une décision de programme (recherche
// coûteuse) restent arbitrables indépendamment.
#pragma once
#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

#include "fen/core/Constants.hpp"

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

// ═══════════════════════════════════════════════════════════════════════════
// FINANCES DE L'AGENCE — ÉCHELLE RÉELLE [GDD 13, v1.2]
// ═══════════════════════════════════════════════════════════════════════════
// Refonte v1.2 : l'économie n'est plus une trésorerie abstraite mais le budget
// d'une agence spatiale mondiale. UNITÉ DE BASE : le M€ (million d'euros), pour
// que le budget d'agence (~100 000 M€ = 100 Md€) et le coût d'une mission
// (~150-3000 M€) partagent la même unité sans dérive de flottant.
//
// DEUX JAUGES DISTINCTES [GDD 13.4] : la TRÉSORERIE courante (d'où tombent les
// coûts fixes) et le FONDS DE RÉSERVE (dont les paliers déclenchent les
// alertes). Les coûts fixes drainent d'abord la trésorerie ; quand elle ne
// couvre plus, la réserve est entamée.
//
// L'INVARIANT STRUCTURANT [GDD 13.2] : les recettes GARANTIES hors activité
// (base étatique + valorisation ≈ 35 000 M€/an) sont INFÉRIEURES aux coûts fixes
// (≈ 44 000 M€/an). Accélérer le temps sans rien conduire érode donc la réserve
// (~ −9 000 M€/an). L'agence n'est jamais insolvable par construction, mais
// l'oisiveté mène à la chaîne de fin de partie. Les montants sont des ordres de
// grandeur à calibrer [GDD Annexe E] ; seul l'invariant compte.

// Le modèle de recettes [GDD 13.1]. Deux tranches sont conditionnées à
// l'ACTIVITÉ : elles ne tombent que si des programmes avancent (tranche
// programmes) et si des services sont rendus (commercial).
struct RevenueModel {
  double base_guaranteed_me_yr{30000.0};   // dotation étatique de base (toujours)
  double program_tranche_me_yr{45000.0};   // libérée par jalons programmatiques
  double commercial_me_yr{20000.0};        // lancements/télécom/observation vendus
  double valorisation_me_yr{5000.0};       // données, brevets (semi-passif)

  // Recettes annuelles pour des NIVEAUX D'ACTIVITÉ [0..1] (programmes, commercial).
  double annual(double program_activity, double commercial_activity) const {
    const double pa = std::clamp(program_activity, 0.0, 1.0);
    const double ca = std::clamp(commercial_activity, 0.0, 1.0);
    return base_guaranteed_me_yr + valorisation_me_yr +
           program_tranche_me_yr * pa + commercial_me_yr * ca;
  }
  // Recettes GARANTIES (agence inactive) : base + valorisation.
  double guaranteed_yr() const { return base_guaranteed_me_yr + valorisation_me_yr; }
};

// La chaîne de fin de partie financière [GDD 13.5] : graduée, notifiée, avec
// fenêtre de correction. Le licenciement n'arrive jamais sur un accident isolé.
enum class FinancialStage {
  Sain = 0,        // réserve saine
  Avertissement,   // réserve entamée durablement
  ContratsGeles,   // gel des contrats non prioritaires
  MiseAEcart,      // mise à l'écart : dernière fenêtre
  Licencie,        // fin de partie [GDD 3.4]
};
inline const char* financial_stage_name(FinancialStage s) {
  switch (s) {
    case FinancialStage::Sain:          return "SAIN";
    case FinancialStage::Avertissement: return "AVERTISSEMENT";
    case FinancialStage::ContratsGeles: return "CONTRATS GELES";
    case FinancialStage::MiseAEcart:    return "MISE A L'ECART";
    default:                            return "LICENCIE";
  }
}

struct AgencyFinance {
  // Deux jauges [GDD 13.4], en M€.
  double treasury_me{6000.0};             // trésorerie courante
  double reserve_me{18000.0};             // fonds de réserve
  double reserve_target_me{18000.0};      // cible du fonds (base des % d'alerte)

  // Coûts fixes annuels [GDD 13.2] : ~40 Md€ d'agence + ~4 Md€ Novellus.
  double fixed_costs_me_yr{40000.0};
  double novellus_ops_me_yr{4000.0};
  RevenueModel revenue;

  // Chaîne de fin de partie [GDD 13.5], pilotée par la durée passée en réserve
  // basse. Suspendable pendant une mission longue [GDD 9.3].
  double days_low_reserve{0.0};
  FinancialStage stage{FinancialStage::Sain};
  bool suspended{false};                  // gel de la chaîne (joueur en mission)

  double annual_fixed() const { return fixed_costs_me_yr + novellus_ops_me_yr; }
  // L'invariant du GDD 13.2 : recettes garanties < coûts fixes. Une agence qui
  // ne le respecte pas n'a aucune pression d'inactivité — c'est un bug de calibre.
  bool inactivity_pressure_holds() const {
    return revenue.guaranteed_yr() < annual_fixed();
  }
  // LE PRIX DU TEMPS QUI PASSE [GDD 13.2, 14.2] : solde annuel d'une agence
  // inactive = recettes garanties − coûts fixes. NÉGATIF par construction (c'est
  // l'invariant ci-dessus). Vit ici et pas dans l'interface : le poste AGENCE
  // l'AFFICHE au joueur avant qu'il n'accélère, il ne le recalcule pas.
  double annual_idle_balance_me() const {
    return revenue.guaranteed_yr() - annual_fixed();
  }

  double reserve_ratio() const {
    return reserve_target_me > 0.0 ? reserve_me / reserve_target_me : 0.0;
  }
  AlertLevel reserve_level() const { return alert_level(reserve_ratio()); }

  // ENGAGER un programme (commit de mission [GDD 4.1]) : prélève la trésorerie,
  // puis la réserve si nécessaire. Refuse si le total disponible ne couvre pas.
  bool engage(double cost_me) {
    if (cost_me <= 0.0) return true;
    if (cost_me > treasury_me + reserve_me) return false;
    if (cost_me <= treasury_me) { treasury_me -= cost_me; return true; }
    const double reste = cost_me - treasury_me;
    treasury_me = 0.0;
    reserve_me -= reste;
    return true;
  }
  // Encaisser (budget de contrat versé à la signature [GDD 3.1]).
  void credit(double me) { treasury_me += me; }

  // LE TICK MENSUEL. `program_activity`/`commercial_activity` ∈ [0,1] : ce que
  // l'agence produit ce mois-ci. Draine les coûts fixes, reconstitue la réserve
  // sur l'excédent, et fait avancer la chaîne de fin de partie.
  void tick_month(double program_activity, double commercial_activity) {
    if (suspended) return;                 // adjoint aux commandes [GDD 9.3]
    const double net = (revenue.annual(program_activity, commercial_activity) -
                        annual_fixed()) / 12.0;
    treasury_me += net;
    // Trésorerie négative -> on pioche dans la réserve.
    if (treasury_me < 0.0) { reserve_me += treasury_me; treasury_me = 0.0; }
    // Excédent -> on reconstitue la réserve jusqu'à sa cible.
    else if (reserve_me < reserve_target_me && treasury_me > 0.0) {
      const double top = std::min(treasury_me, reserve_target_me - reserve_me);
      treasury_me -= top; reserve_me += top;
    }
    if (reserve_me < 0.0) reserve_me = 0.0;
    advance_chain();
  }

  // La chaîne graduée : chaque palier de réserve bas prolongé fait monter d'un
  // cran ; la réserve reconstituée fait redescendre.
  void advance_chain() {
    const AlertLevel lvl = reserve_level();
    if (lvl >= AlertLevel::Tension) days_low_reserve += 30.44; // un mois
    else days_low_reserve = std::max(0.0, days_low_reserve - 30.44);

    // Seuils DÉCLARÉS (mois consécutifs sous tension).
    if (lvl <= AlertLevel::Watch)             stage = FinancialStage::Sain;
    else if (days_low_reserve < 60.0)         stage = FinancialStage::Avertissement;
    else if (days_low_reserve < 120.0)        stage = FinancialStage::ContratsGeles;
    else if (days_low_reserve < 180.0)        stage = FinancialStage::MiseAEcart;
    else                                       stage = FinancialStage::Licencie;
  }

  bool contracts_frozen() const { return stage >= FinancialStage::ContratsGeles; }
  bool dismissed() const { return stage == FinancialStage::Licencie; }
};

// ═══ CONFIANCE ARES — FILTRE D'ÉLIGIBILITÉ [GDD 13.4] ═══
// La confiance (0-100, départ 70) vit dans career::CareerState. Ce n'est PAS un
// score à maximiser mais un FILTRE : elle conditionne l'exercice effectif du
// droit que le rang autorise. Ces fonctions LISENT une valeur de confiance ;
// elles ne la stockent pas (pas de duplication avec la carrière).
enum class AccessBand {
  Flagship = 0,   // 80-100 : programmes phares, habité lointain, fin d'arbre
  Normal,         // 60-79  : tous contrats de routine
  Restricted,     // 40-59  : missions habitées suspendues
  RoboticOnly,    // 20-39  : robotique et maintenance uniquement
  Frozen,         // < 20   : aucun nouveau programme ; procédure institutionnelle
};
inline AccessBand access_band(double confidence) {
  if (confidence >= 80.0) return AccessBand::Flagship;
  if (confidence >= 60.0) return AccessBand::Normal;
  if (confidence >= 40.0) return AccessBand::Restricted;
  if (confidence >= 20.0) return AccessBand::RoboticOnly;
  return AccessBand::Frozen;
}
inline const char* access_band_name(AccessBand b) {
  switch (b) {
    case AccessBand::Flagship:    return "PROGRAMMES PHARES";
    case AccessBand::Normal:      return "ROUTINE COMPLETE";
    case AccessBand::Restricted:  return "HABITE SUSPENDU";
    case AccessBand::RoboticOnly: return "ROBOTIQUE SEULE";
    default:                      return "GELE";
  }
}
// Les droits effectifs, à croiser avec le rang [GDD 13.4].
inline bool crewed_allowed(double confidence)    { return confidence >= 60.0; }
inline bool flagship_allowed(double confidence)  { return confidence >= 80.0; }
inline bool new_program_allowed(double confidence){ return confidence >= 20.0; }

// La « procédure institutionnelle » sous 20 [GDD 13.4] : jamais une fin de
// partie, jamais un état absorbant. Elle DÉCLASSE (l'appelant baisse le rang) et
// ramène la confiance à une bande de reprise (~50), rouvrant les contrats de
// routine du rang inférieur. Renvoie la confiance de reprise.
inline double confidence_recovery_after_procedure() { return 50.0; }

// --- Sites de lancement [GDD 13.3] -------------------------------------------
// « Le coût des lancements dépend du site choisi selon ses contraintes
// géographiques RÉELLES (inclinaison atteignable, latitude, azimuts autorisés).
// Le choix du site est un arbitrage technique et budgétaire réel. »
//
// ═══ LA GÉOMÉTRIE, PAS UN MALUS ═══
// Pour une orbite d'inclinaison i lancée depuis la latitude φ, l'azimut de tir β
// (mesuré depuis le nord) obéit à la trigonométrie sphérique EXACTE :
//     cos(i) = sin(β) · cos(φ)      →      sin(β) = cos(i) / cos(φ)
// Conséquences NON négociables :
//   . une solution n'existe que si |cos(i)| ≤ cos(φ), soit i ≥ φ (prograde) :
//     on ne descend jamais sous sa propre latitude sans dog-leg coûteux ;
//   . β ≈ 90° (plein est) donne i = φ (le minimum) ; β → 0° ou 180° donne
//     i → 90° (polaire). L'azimut requis doit tomber dans le COULOIR autorisé,
//     sinon le site ne peut pas viser cette inclinaison ;
//   . le tir plein est profite de la ROTATION terrestre : gain de vitesse
//     v_gain = ω·R·cos(φ)·sin(β). Kourou (φ = 5°) en tire ~463 m/s vers le GTO —
//     d'où « GTO roi ». C'est ce gain, et non un bonus arbitraire, qui fait la
//     valeur d'un site équatorial.
// z=0 : rotation sphérique, Terre non aplatie (approximation DÉCLARÉE [6.8]).
struct LaunchSite {
  std::string name;
  double latitude_deg{};
  double azimuth_min_deg{}, azimuth_max_deg{};  // couloirs autorisés (survol)
  double cost_factor{1.0};                       // multiplicateur logistique

  double min_inclination_deg() const { return std::fabs(latitude_deg); }

  // Azimut de tir requis pour l'inclinaison `i`, côté nœud ASCENDANT (β dans
  // [0,90] ou [270,360]) ou DESCENDANT (β dans [90,270]). std::nullopt si i est
  // physiquement inatteignable depuis cette latitude (i < φ).
  std::optional<double> required_azimuth_deg(double inclination_deg,
                                             bool descending = false) const {
    const double phi = latitude_deg * cst::PI / 180.0;
    const double i = inclination_deg * cst::PI / 180.0;
    const double cphi = std::cos(phi);
    if (cphi <= 1e-9) return std::nullopt;
    const double sin_beta = std::cos(i) / cphi;
    if (sin_beta < -1.0 - 1e-9 || sin_beta > 1.0 + 1e-9) return std::nullopt;
    const double beta = std::asin(std::clamp(sin_beta, -1.0, 1.0));  // [-90,90]
    // asin donne le tir vers l'EST-nord ; le nœud descendant tire vers l'ouest.
    double deg = beta * 180.0 / cst::PI;          // ex. i=φ -> 90 (plein est)
    if (descending) deg = 180.0 - deg;            // symétrique par rapport au sud
    if (deg < 0.0) deg += 360.0;
    return deg;
  }

  bool azimuth_allowed(double azimuth_deg) const {
    // Le couloir peut chevaucher le nord (az_min négatif = « juste à l'ouest du
    // nord », ex. Kourou −10,5° = 349,5°). On teste donc aussi az−360 pour
    // rattraper cette représentation.
    auto in = [this](double a) {
      return a >= azimuth_min_deg - 1e-9 && a <= azimuth_max_deg + 1e-9;
    };
    return in(azimuth_deg) || in(azimuth_deg - 360.0);
  }

  // Atteignable = physiquement possible ET dans un couloir d'azimut autorisé
  // (ascendant OU descendant). C'est le vrai test, pas seulement i ≥ φ.
  bool reachable(double inclination_deg) const {
    if (inclination_deg < min_inclination_deg() - 1e-9) return false;
    for (bool desc : {false, true}) {
      const auto az = required_azimuth_deg(inclination_deg, desc);
      if (az && azimuth_allowed(*az)) return true;
    }
    return false;
  }

  // Gain de vitesse dû à la rotation terrestre pour ce tir (m/s). Toujours
  // évalué sur le nœud ascendant, celui qui profite de l'est.
  double rotation_velocity_gain(double inclination_deg) const {
    const auto az = required_azimuth_deg(inclination_deg, false);
    if (!az) return 0.0;
    const double phi = latitude_deg * cst::PI / 180.0;
    const double beta = *az * cst::PI / 180.0;
    return cst::OMEGA_EARTH * cst::R_EARTH * std::cos(phi) * std::sin(beta);
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
