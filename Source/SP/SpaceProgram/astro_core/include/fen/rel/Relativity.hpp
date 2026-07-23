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
