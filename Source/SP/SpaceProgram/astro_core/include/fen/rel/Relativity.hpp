// fen/rel/Relativity.hpp — relativité restreinte [GDD 6.7, 19.4]
//
// INVARIANT : la dilatation est CALCULÉE depuis le profil de vitesse réel,
// jamais posée. En dessous du seuil (β < 0.1), γ ≈ 1 et le temps propre égale
// le temps de jeu : AUCUN effet n'est appliqué [GDD 6.7.2]. Seule une
// architecture antimatière de fin d'arbre franchit ce seuil [GDD 19.3].
//
// Isolé du chemin critique : rien ici n'est appelé par le propagateur.
// Golden tests analytiques : γ(β), τ = ∫dt/γ, m0/mf vs forme close [carte P3].
#pragma once
#include <cmath>
#include <vector>
#include "fen/core/Constants.hpp"

namespace fen::rel {

// Seuil d'activation du régime relativiste [GDD 6.7.2] : en dessous,
// γ−1 < 0.5 % — imperceptible à l'échelle d'une vie de personnage.
inline constexpr double BETA_THRESHOLD = 0.10;

// SEUIL NARRATIF [GDD 6.7.2, v1.2] : distinct de l'effet MESURABLE. Un écart de
// 4,8 % (β=0,3) est mesurable par un instrument mais représente ~5 mois sur dix
// ans — invisible à l'échelle d'une vie. L'écart ne devient perceptible dans la
// narration et la carrière que vers β ≳ 0,7. Le design ne VISE jamais ce seuil ;
// il le CONSTATE si le joueur l'atteint.
inline constexpr double BETA_NARRATIVE = 0.70;

inline double beta(double v_mps) { return v_mps / cst::C_LIGHT; }

inline double lorentz_gamma(double b) {
  if (b <= 0.0) return 1.0;
  if (b >= 1.0) return 1e300;             // jamais atteint : garde numérique
  return 1.0 / std::sqrt(1.0 - b * b);
}

// γ − 1 sans cancellation aux petits β (β ~ 1e-4 → γ−1 ~ 5e-9, illisible en
// double via lorentz_gamma() - 1). Forme exacte : β² / (√(1−β²)·(1+√(1−β²))).
inline double gamma_minus_one(double b) {
  if (b <= 0.0) return 0.0;
  if (b >= 1.0) return 1e300;
  const double s = std::sqrt(1.0 - b * b);
  return b * b / (s * (1.0 + s));
}

inline bool is_relativistic(double v_mps) { return beta(v_mps) >= BETA_THRESHOLD; }

// --- ProperTimeIntegrator ----------------------------------------------------
// τ_bord = ∫ dt / γ(t) le long du profil de vitesse [GDD 6.7.1].
// Trapèze sur échantillons ordonnés en t. Suffisant : le profil est lisse
// (accélération continue) et le résultat n'entre dans AUCUNE boucle de retour
// physique — il ne pilote que les horloges (carrière, narration).
struct VelocitySample {
  double t{};      // s (TDB, temps terrestre)
  double v{};      // m/s (norme de la vitesse héliocentrique)
};

inline double proper_time(const std::vector<VelocitySample>& profile) {
  double tau = 0.0;
  for (std::size_t i = 1; i < profile.size(); ++i) {
    const double dt = profile[i].t - profile[i - 1].t;
    const double g0 = lorentz_gamma(beta(profile[i - 1].v));
    const double g1 = lorentz_gamma(beta(profile[i].v));
    tau += 0.5 * dt * (1.0 / g0 + 1.0 / g1);
  }
  return tau;
}

// --- RelativisticRocketModel [GDD 6.7.3] -------------------------------------
// Pour une vitesse d'éjection propre ve constante :
//   m0/mf = ((1+β)/(1−β))^(c/(2·ve))
// Cœur antimatière : ve effectif ≈ c/3 (photons γ et pions neutres perdus,
// aucune contribution à la poussée dirigée). Le verrou n'est PAS le ratio de
// masse (β=0.3 → ratio ~2.5) : c'est la PRODUCTION d'antimatière [GDD 19.3].
inline constexpr double VE_ANTIMATTER_EFF = cst::C_LIGHT / 3.0;

inline double mass_ratio(double beta_final, double ve) {
  if (beta_final <= 0.0) return 1.0;
  if (beta_final >= 1.0 || ve <= 0.0) return 1e300;
  return std::pow((1.0 + beta_final) / (1.0 - beta_final),
                  cst::C_LIGHT / (2.0 * ve));
}

// Inverse exact via la rapidité φ = (ve/c)·ln(m0/mf), β = tanh(φ).
inline double beta_from_mass_ratio(double m0_over_mf, double ve) {
  if (m0_over_mf <= 1.0) return 0.0;
  return std::tanh((ve / cst::C_LIGHT) * std::log(m0_over_mf));
}

// ═══ LE VERROU DE L'ALLER-RETOUR [GDD 6.7.4, v1.2] ═══
// Un aller-retour HABITÉ exige QUATRE poussées (accélération, décélération à
// l'arrivée, accélération au retour, décélération finale). Le ratio de masse
// total est donc le ratio unitaire ÉLEVÉ À LA PUISSANCE QUATRE. À β=0,5 :
// unitaire ~5,2, aller-retour ~730 ; à β=0,9 : ~83 → ~4,7×10⁷. « Un aller-retour
// à haute vitesse sans ravitaillement est physiquement hors de portée. »
inline double round_trip_mass_ratio(double beta_cruise, double ve) {
  const double single = mass_ratio(beta_cruise, ve);
  return single * single * single * single;   // quatre poussées
}
// Nombre de poussées d'une architecture : 1 (sonde en aller simple sans
// insertion), 2 (aller simple avec insertion/freinage), 4 (aller-retour habité).
inline double mass_ratio_for_burns(double beta_per_burn, double ve, int n_burns) {
  double r = 1.0;
  const double single = mass_ratio(beta_per_burn, ve);
  for (int i = 0; i < n_burns; ++i) r *= single;
  return r;
}

// ═══ CHAÎNE ANTIMATIÈRE : masse ↔ β [GDD 5.12.12, 19.3] ═══
// Le VERROU n'est pas le ratio de masse (modeste) mais la QUANTITÉ d'antimatière
// à produire et confiner. Pour un cœur annihilant (ve ≈ c/3), le propergol est
// pour moitié de l'antimatière, pour moitié de la matière. La masse
// d'antimatière requise pour qu'un véhicule de masse sèche `m_dry_kg` atteigne
// `beta` (une poussée) : m_a = ½ · m_dry · (ratio − 1).
inline double antimatter_needed_g(double m_dry_kg, double beta_final,
                                  int n_burns = 1) {
  if (m_dry_kg <= 0.0 || beta_final <= 0.0) return 0.0;
  const double ratio = mass_ratio_for_burns(beta_final, VE_ANTIMATTER_EFF, n_burns);
  const double propellant_g = m_dry_kg * 1000.0 * (ratio - 1.0);
  return 0.5 * propellant_g;                  // moitié antimatière
}
// L'inverse : quel β un véhicule atteint avec `antimatter_g` disponibles (une
// poussée). C'est ce qui montre pourquoi β reste minuscule — quelques grammes
// n'énergisent qu'un propergol dérisoire.
inline double beta_from_antimatter(double m_dry_kg, double antimatter_g) {
  if (m_dry_kg <= 0.0 || antimatter_g <= 0.0) return 0.0;
  const double ratio = 1.0 + 2.0 * antimatter_g / (m_dry_kg * 1000.0);
  return beta_from_mass_ratio(ratio, VE_ANTIMATTER_EFF);
}

// LE MODÈLE DE PRODUCTION — le vrai paramètre d'équilibrage de la fin de jeu
// [GDD 5.12.12, v1.2]. Ce sont ces quatre paramètres, et non un β cible, qui
// décident de la vitesse maximale réellement atteignable. Ordres de grandeur
// à calibrer [GDD Annexe E] ; l'invariant est que produire est PLURIANNUEL.
struct AntimatterProduction {
  double production_g_per_yr{1.0e-3};      // débit (g/an) — infime
  double energy_j_per_g{1.0e17};           // énergie consommée par g produit
                                           // (≫ l'énergie d'annihilation : rendement infime)
  double cost_me_per_g{1.0e6};             // coût (M€/g) — hors échelle
  double confinement_capacity_g{1.0};      // masse stockable en sécurité (plafond)
  double loss_rate_per_day{1.0e-3};        // perte de confinement (risque permanent)

  // Temps (années) pour accumuler `grams` — plafonné par le confinement.
  double years_to_accumulate(double grams) const {
    if (production_g_per_yr <= 0.0) return 1e300;
    return grams / production_g_per_yr;
  }
  double energy_to_produce(double grams) const { return grams * energy_j_per_g; }
  double cost_to_produce_me(double grams) const { return grams * cost_me_per_g; }
  // Le stock UTILE ne peut pas dépasser la capacité de confinement.
  double max_usable_stock_g() const { return confinement_capacity_g; }
  // Survie du stock sur `days` (processus de perte poissonien).
  double stock_survival(double days) const {
    return std::exp(-std::max(0.0, loss_rate_per_day) * std::max(0.0, days));
  }
};

// Énergie d'annihilation disponible pour `grams` d'antimatière (avec autant de
// matière) : E = 2·m·c². ~1.8e14 J/g de PAIRE ; le GDD retient ~9e13 J par
// gramme d'ANTIMATIÈRE annihilé avec sa contrepartie [GDD Annexe B].
inline double annihilation_energy_j(double grams_antimatter) {
  return grams_antimatter * 1e-3 * cst::C_LIGHT * cst::C_LIGHT;
}

// --- DualClock [GDD 14.4, 3.4] -----------------------------------------------
// Deux horloges : Terre (temps de jeu) et bord (temps propre). Elles COÏNCIDENT
// sous le seuil ; l'écart cumulé est le vieillissement différentiel, qui pèse
// sur la carrière et la passation. Traitement multijoueur spécifique [GDD 16.3].
struct DualClock {
  double t_earth{0.0};   // s écoulées côté Terre
  double tau_board{0.0}; // s vécues à bord

  // Avance les deux horloges pendant dt (temps Terre) à vitesse bord v.
  void advance(double dt, double v_mps) {
    t_earth += dt;
    tau_board += dt / lorentz_gamma(beta(v_mps));
  }
  // Écart d'âge accumulé (>= 0) : le personnage revient PLUS JEUNE que le monde.
  double aging_gap() const { return t_earth - tau_board; }
  bool   diverged() const { return aging_gap() > 1.0; } // > 1 s : affichable
};

} // namespace fen::rel
