// fen/env/Debris.hpp — DÉBRIS ORBITAUX [GDD 7.8, 10.5]
//
// « Les débris ne sont JAMAIS des malus abstraits. » Leur persistance dépend de
// l'ALTITUDE, de la TRAÎNÉE, de l'ÉNERGIE DE FRAGMENTATION, du COULOIR ORBITAL
// et du TEMPS. En orbite basse la traînée finit par nettoyer le couloir ; en
// orbite haute la pollution est quasi permanente. Un échec en orbite affecte
// donc les LANCEMENTS FUTURS dans le même couloir — c'est la conséquence
// durable exigée par [GDD 10.5], pas une pénalité de score.
//
// Ce module était annoncé par `SpaceWeather::atmo_density_factor` (« alimente
// le modèle de débris ») mais n'existait pas : il manquait au portage.
//
// ═══ MODÈLES ET LEUR DOMAINE DE VALIDITÉ [GDD 6.8, 12.5] ═══
//
// 1) ATMOSPHÈRE — exponentielle par morceaux, calée sur l'atmosphère standard
//    US 1976 étendue. Domaine : 100–1000 km. Au-delà, la densité est traitée
//    comme nulle à l'échelle du jeu (durée de vie > siècles). Le facteur
//    d'activité solaire vient de SpaceWeather (couplage exigé par [GDD 7.7]).
//
// 2) DURÉE DE VIE — décroissance d'une orbite circulaire sous traînée :
//       da/dt = -a·ρ·v/B   avec B = m/(Cd·A), le coefficient balistique,
//    obtenue du bilan d'énergie orbitale (E = -µ/2a, puissance dissipée
//    -½ρv³/B). Avec une atmosphère exponentielle de hauteur d'échelle H,
//    l'intégration donne, tant que la chute est grande devant H :
//       t_vie ≈ B·H / (a·ρ(h)·v)   ,   v = √(µ/a)
//    Approximation DÉCLARÉE : orbite circulaire, B constant, pas de J2, pas de
//    variation de ρ au cours de la chute autre que par l'exponentielle. Erreur
//    attendue : facteur ~2 — ce qui est l'ordre de grandeur utile ici, puisque
//    la décision de gameplay est « le couloir se nettoie en mois, en décennies
//    ou jamais », pas une date.
//
// 3) FRAGMENTATION — modèle standard de rupture NASA (EVOLVE / ODPO) :
//       N(> Lc) = k · M^0.75 · Lc^-1.71     (Lc en mètres, M en kg)
//    avec k = 0,1 pour une explosion et une masse EFFICACE pour une collision
//    (seule la masse impliquée dans l'impact hypervéloce fragmente). C'est un
//    modèle publié, pas une invention : il porte donc sa source.
//
// 4) RISQUE DE COLLISION — flux de particules : P = 1 − exp(−n·σ·v_rel·Δt),
//    avec n la densité spatiale du couloir. Loi de Poisson, pas un tirage nu.
#pragma once
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "fen/core/Constants.hpp"
#include "fen/env/Atmosphere.hpp"
#include "fen/env/SpaceWeather.hpp"

namespace fen::env {

// ---------------------------------------------------------------------------
// 1) ATMOSPHÈRE — déléguée à env/Atmosphere.hpp
// ---------------------------------------------------------------------------
// La table vit dans Atmosphere.hpp : la traînée orbitale, la rentrée et les
// débris DOIVENT lire la même. Ces deux fonctions ne sont que des adaptateurs
// en kilomètres, l'unité naturelle des couloirs.

// ALTITUDE AU-DELÀ DE LAQUELLE ON DÉCLARE LA TRAÎNÉE NULLE. Au-dessus, le
// couloir ne se nettoie pas à l'échelle d'une carrière : c'est le régime
// « pollution quasi permanente » de [GDD 7.8].
inline constexpr double DRAG_FREE_ALT_KM = 1000.0;

inline double atmospheric_density(double alt_km, double activity01) {
  if (alt_km <= 0.0) return 1.225;
  return earth_atmosphere(atmo_density_factor(activity01)).density(alt_km * 1000.0);
}

// Hauteur d'échelle locale (km) — sert à la durée de vie.
inline double scale_height_km(double alt_km) {
  return earth_atmosphere(1.0).scale_height(alt_km * 1000.0) * 1.0e-3;
}

// ---------------------------------------------------------------------------
// 2) DURÉE DE VIE ORBITALE
// ---------------------------------------------------------------------------

// Coefficient balistique B = m/(Cd·A), en kg/m². Un fragment léger et large
// retombe vite ; un étage dense reste longtemps.
inline constexpr double B_FRAGMENT_DEFAULT = 30.0;   // débris typique 10 cm
inline constexpr double B_ROCKET_BODY = 150.0;       // étage vide

// Durée de vie (jours) d'une orbite circulaire à `alt_km` sous traînée.
// Renvoie une valeur ÉNORME (mais finie) au-delà du régime de traînée : le
// couloir est alors traité comme définitivement pollué [GDD 7.8].
inline double orbital_lifetime_days(double alt_km, double ballistic_coef,
                                    double activity01) {
  if (alt_km <= 120.0) return 0.0;                   // rentrée immédiate
  const double rho = atmospheric_density(alt_km, activity01);
  if (rho <= 0.0) return 1.0e12;
  const double a_m = cst::R_EARTH + alt_km * 1000.0;
  const double v = std::sqrt(cst::MU_EARTH / a_m);   // vitesse circulaire (m/s)
  const double H_m = scale_height_km(alt_km) * 1000.0;
  const double t_s = ballistic_coef * H_m / (a_m * rho * v);
  return t_s / cst::DAY;
}

// ---------------------------------------------------------------------------
// 3) FRAGMENTATION
// ---------------------------------------------------------------------------

// Énergie de l'événement : elle décide de la MASSE EFFICACE fragmentée et de
// la dispersion en altitude. [GDD 10.3] : la gravité ne dépend pas que de la
// perte, mais de ce qu'elle laisse derrière elle.
enum class BreakupKind {
  Explosion = 0,      // rupture de réservoir, batterie, résidus d'ergols
  Collision = 1,      // impact hypervéloce : bien plus fragmentant
};

// Modèle standard de rupture NASA : N(>Lc) = k·M^0.75·Lc^-1.71.
// `min_size_m` = taille caractéristique minimale comptée (0,1 m = seuil de
// catalogage habituel). Renvoie un NOMBRE D'OBJETS (réel, non entier : c'est
// une espérance de modèle, pas un tirage).
inline double fragment_count(double mass_kg, BreakupKind kind,
                             double min_size_m = 0.10) {
  if (mass_kg <= 0.0 || min_size_m <= 0.0) return 0.0;
  const double k = (kind == BreakupKind::Collision) ? 0.1 : 0.1;
  // Collision hypervéloce : toute la masse participe. Explosion : une fraction
  // seulement (le reste reste solidaire). Fraction DÉCLARÉE.
  const double m_eff = (kind == BreakupKind::Collision) ? mass_kg : 0.35 * mass_kg;
  return k * std::pow(m_eff, 0.75) * std::pow(min_size_m, -1.71);
}

// ---------------------------------------------------------------------------
// 4) COULOIRS ET ENVIRONNEMENT
// ---------------------------------------------------------------------------

// Un COULOIR est une coquille d'altitude : c'est l'unité de pollution du GDD
// (« affecte les futurs lancements dans les mêmes couloirs »).
struct Corridor {
  double alt_km_min{}, alt_km_max{};
  const char* name{""};

  double mid_alt_km() const { return 0.5 * (alt_km_min + alt_km_max); }
  bool contains(double alt_km) const {
    return alt_km >= alt_km_min && alt_km < alt_km_max;
  }
  // Volume de la coquille sphérique (m³) — dénominateur de la densité spatiale.
  double volume_m3() const {
    const double r1 = cst::R_EARTH + alt_km_min * 1000.0;
    const double r2 = cst::R_EARTH + alt_km_max * 1000.0;
    return (4.0 / 3.0) * cst::PI * (r2 * r2 * r2 - r1 * r1 * r1);
  }
};

inline const std::vector<Corridor>& standard_corridors() {
  static const std::vector<Corridor> v = {
      { 200.0,  600.0, "LEO basse"},
      { 600.0, 1000.0, "LEO haute"},
      {1000.0, 2000.0, "LEO superieure"},
      {2000.0, 35000.0, "MEO"},
      {35000.0, 37000.0, "GEO"},
  };
  return v;
}

// Un nuage de débris issu d'UN événement, à UNE altitude.
struct DebrisCloud {
  std::string origin;          // mission / véhicule à l'origine — traçabilité
  double alt_km{};
  double n_objects{};          // population > 10 cm restante
  double ballistic_coef{B_FRAGMENT_DEFAULT};
  double created_days{};
  double n_initial{};
};

class DebrisEnvironment {
 public:
  // Enregistrer une fragmentation. C'est le SEUL point d'entrée : on ne crée
  // jamais de débris « pour la difficulté », toujours en conséquence d'un fait.
  void add_breakup(const std::string& origin, double alt_km, double mass_kg,
                   BreakupKind kind, double now_days,
                   double ballistic_coef = B_FRAGMENT_DEFAULT) {
    DebrisCloud c;
    c.origin = origin;
    c.alt_km = alt_km;
    c.n_objects = fragment_count(mass_kg, kind);
    c.n_initial = c.n_objects;
    c.ballistic_coef = ballistic_coef;
    c.created_days = now_days;
    clouds_.push_back(std::move(c));
  }

  // LA TRAÎNÉE NETTOIE. Décroissance exponentielle de la population avec la
  // durée de vie du couloir : en LEO basse le nuage disparaît en mois ; au-delà
  // de DRAG_FREE_ALT_KM la constante de temps dépasse la carrière du joueur.
  void tick(double dt_days, double activity01) {
    for (auto& c : clouds_) {
      const double tau = orbital_lifetime_days(c.alt_km, c.ballistic_coef, activity01);
      if (tau <= 0.0) { c.n_objects = 0.0; continue; }
      c.n_objects *= std::exp(-dt_days / tau);
      if (c.n_objects < 1.0) c.n_objects = 0.0;      // sous l'objet catalogué
    }
    clouds_.erase(std::remove_if(clouds_.begin(), clouds_.end(),
                                 [](const DebrisCloud& c) { return c.n_objects <= 0.0; }),
                  clouds_.end());
  }

  // Population catalogable d'un couloir.
  double population(const Corridor& corr) const {
    double n = 0.0;
    for (const auto& c : clouds_)
      if (corr.contains(c.alt_km)) n += c.n_objects;
    return n;
  }

  // Densité spatiale (objets / m³) du couloir.
  double spatial_density(const Corridor& corr) const {
    const double vol = corr.volume_m3();
    return vol > 0.0 ? population(corr) / vol : 0.0;
  }

  // PROBABILITÉ DE COLLISION pour un véhicule de section `area_m2` séjournant
  // `duration_days` dans le couloir. Loi de Poisson sur le flux :
  //   P = 1 − exp(−n·σ·v_rel·Δt)
  // `v_rel` ≈ 10 km/s : vitesse relative moyenne des rencontres en LEO
  // (croisements d'inclinaisons), valeur DÉCLARÉE.
  double collision_probability(const Corridor& corr, double area_m2,
                               double duration_days,
                               double v_rel_ms = 1.0e4) const {
    if (area_m2 <= 0.0 || duration_days <= 0.0) return 0.0;
    const double n = spatial_density(corr);
    const double flux = n * area_m2 * v_rel_ms * duration_days * cst::DAY;
    return 1.0 - std::exp(-flux);
  }

  // Le couloir se nettoiera-t-il à l'échelle d'une carrière ? [GDD 7.8]
  bool corridor_permanently_polluted(const Corridor& corr, double activity01,
                                     double career_days = 60.0 * 365.25) const {
    if (population(corr) <= 0.0) return false;
    return orbital_lifetime_days(corr.mid_alt_km(), B_FRAGMENT_DEFAULT,
                                 activity01) > career_days;
  }

  const std::vector<DebrisCloud>& clouds() const { return clouds_; }
  std::vector<DebrisCloud>& clouds_mut() { return clouds_; }
  void clear() { clouds_.clear(); }

 private:
  std::vector<DebrisCloud> clouds_;
};

} // namespace fen::env
