// fen/env/Atmosphere.hpp — MODÈLES D'ATMOSPHÈRE [GDD 7.1, 7.6, 7.7, 7.8]
//
// UNE SEULE source de vérité pour la densité de l'air : la traînée orbitale
// (`force/Drag.hpp`), la durée de vie des débris (`env/Debris.hpp`), la rentrée
// et l'aérofreinage (`flight/Reentry.hpp`) lisent tous ce fichier. Deux tables
// divergentes seraient une incohérence de simulation, pas un détail.
//
// ═══ MODÈLE ET DOMAINE DE VALIDITÉ [GDD 6.8] ═══
// Atmosphère EXPONENTIELLE PAR MORCEAUX : sur chaque palier,
//     ρ(h) = ρ_i · exp( −(h − h_i) / H_i )
// C'est l'ajustement classique (Vallado, tables US Standard 1976 étendues).
// Domaine : 0–1000 km pour la Terre, 0–120 km pour Mars.
//   . erreur attendue : facteur ~2 en haute atmosphère, dominée par l'activité
//     solaire elle-même (qui fait varier ρ d'un facteur 5 à 10 à 400 km) ;
//   . ne modélise PAS : marée diurne, effet géomagnétique court terme,
//     variation latitudinale. V2 : NRLMSISE-00 tabulé.
// C'est une approximation DÉCLARÉE, cohérente avec son usage : elle sert à
// décider « ce couloir se nettoie ou non », « ce corridor de rentrée passe ou
// non », jamais à prédire une date à la journée près.
//
// COUPLAGE À L'ACTIVITÉ SOLAIRE [GDD 7.7] : la table terrestre est calée sur le
// MINIMUM solaire, et `SpaceWeather::atmo_density_factor` (1 → 6) la gonfle.
// C'est le même couplage qui module la dose GCR : un seul cycle, deux effets
// opposés.
#pragma once
#include <cmath>
#include <vector>

#include "fen/core/Constants.hpp"

namespace fen::env {

struct AtmoLayer { double alt_km, rho0, scale_h_km; };

// Point tabulé : altitude et densité. RIEN D'AUTRE.
struct AtmoPoint { double alt_km, rho; };

// ═══ LA HAUTEUR D'ÉCHELLE EST DÉRIVÉE DE LA TABLE, PAS TABULÉE À PART ═══
// Publier à la fois (ρ_i, H_i) laisse les deux se contredire : l'exponentielle
// du palier i n'atteint alors pas ρ_{i+1} au palier suivant, et la densité
// REMONTE à la frontière. Constaté par oracle : saut de +39 % à 50 km, ce qui
// rendrait la traînée non monotone en altitude — physiquement faux.
// On impose donc la continuité en calculant
//     H_i = (h_{i+1} − h_i) / ln(ρ_i / ρ_{i+1})
// L'exponentielle de chaque palier passe alors EXACTEMENT par ses deux bornes,
// et la densité est strictement décroissante par construction. `top_scale_h_km`
// sert à l'extrapolation au-delà du dernier point.
inline std::vector<AtmoLayer> build_layers(const std::vector<AtmoPoint>& pts,
                                           double top_scale_h_km) {
  std::vector<AtmoLayer> out;
  out.reserve(pts.size());
  for (std::size_t i = 0; i < pts.size(); ++i) {
    double h = top_scale_h_km;
    if (i + 1 < pts.size()) {
      const double dz = pts[i + 1].alt_km - pts[i].alt_km;
      const double ratio = pts[i].rho / pts[i + 1].rho;
      if (dz > 0.0 && ratio > 1.0) h = dz / std::log(ratio);
    }
    out.push_back({pts[i].alt_km, pts[i].rho, h});
  }
  return out;
}

// Interface : tout ce dont la traînée et la rentrée ont besoin d'un corps.
class IAtmosphere {
 public:
  virtual ~IAtmosphere() = default;
  // Densité (kg/m³) à l'altitude `alt_m` AU-DESSUS DU RAYON DE RÉFÉRENCE.
  virtual double density(double alt_m) const = 0;
  // Hauteur d'échelle locale (m) — la rentrée et la durée de vie en dépendent.
  virtual double scale_height(double alt_m) const = 0;
  // Rayon de référence du corps (m) : altitude = |r| − ce rayon.
  virtual double body_radius() const = 0;
  // Vitesse de rotation du corps (rad/s) : l'atmosphère TOURNE AVEC LUI, et la
  // traînée s'applique à la vitesse RELATIVE — ~5 % de la traînée en LEO.
  virtual double body_omega() const = 0;
};

// ---------------------------------------------------------------------------
// Modèle exponentiel par morceaux, générique.
class ExponentialAtmosphere final : public IAtmosphere {
 public:
  ExponentialAtmosphere(const std::vector<AtmoLayer>* layers, double radius_m,
                        double omega_rad_s, double density_scale = 1.0)
      : layers_(layers), radius_(radius_m), omega_(omega_rad_s),
        scale_(density_scale) {}

  double density(double alt_m) const override {
    const double alt_km = alt_m * 1.0e-3;
    const AtmoLayer& l = layer_at(alt_km);
    return scale_ * l.rho0 * std::exp(-(alt_km - l.alt_km) / l.scale_h_km);
  }
  double scale_height(double alt_m) const override {
    return layer_at(alt_m * 1.0e-3).scale_h_km * 1000.0;
  }
  double body_radius() const override { return radius_; }
  double body_omega() const override { return omega_; }

  // Le facteur d'échelle porte l'activité solaire : on le change sans
  // reconstruire le modèle.
  void set_density_scale(double s) { scale_ = s; }
  double density_scale() const { return scale_; }

 private:
  const AtmoLayer& layer_at(double alt_km) const {
    const auto& L = *layers_;
    std::size_t k = 0;
    for (std::size_t i = 0; i + 1 < L.size(); ++i)
      if (alt_km >= L[i].alt_km) k = i;
    return L[k];
  }
  const std::vector<AtmoLayer>* layers_;
  double radius_, omega_, scale_;
};

// ---------------------------------------------------------------------------
// TERRE. Table calée sur le MINIMUM solaire (cf. SOLAR_MIN_SCALE) : c'est le
// zéro de `SpaceWeather::atmo_density_factor`, qui la multiplie ensuite.
inline const std::vector<AtmoLayer>& earth_layers() {
  static const std::vector<AtmoLayer> v = build_layers(
      {
          {   0.0, 1.225},
          {  25.0, 3.899e-2},
          {  50.0, 1.057e-3},
          {  75.0, 3.206e-5},
          { 100.0, 5.297e-7},
          { 150.0, 2.070e-9},
          { 200.0, 2.789e-10},
          { 250.0, 7.248e-11},
          { 300.0, 2.418e-11},
          { 350.0, 9.518e-12},
          { 400.0, 3.725e-12},
          { 450.0, 1.585e-12},
          { 500.0, 6.967e-13},
          { 600.0, 1.454e-13},
          { 700.0, 3.614e-14},
          { 800.0, 1.170e-14},
          { 900.0, 5.245e-15},
          {1000.0, 3.019e-15},
      },
      268.00);
  return v;
}

// CALAGE. Les valeurs ci-dessus sont l'ajustement exponentiel classique, qui
// correspond à une atmosphère de MOYENNE activité. Or `atmo_density_factor` va
// de 1 (minimum solaire) à 6 (maximum) : l'appliquer tel quel compterait
// l'activité deux fois. On ramène donc la table au minimum solaire.
inline constexpr double SOLAR_MIN_SCALE = 0.25;

// MARS. Exponentielle simple : c'est le modèle de premier ordre universel des
// études EDL martiennes. ρ0 ≈ 0,020 kg/m³ au niveau de référence, H ≈ 11,1 km.
// L'atmosphère martienne varie énormément (saison, tempêtes de poussière) :
// approximation DÉCLARÉE, à raffiner quand la branche EDL martienne s'ouvrira.
inline const std::vector<AtmoLayer>& mars_layers() {
  static const std::vector<AtmoLayer> v = build_layers(
      {
          {  0.0, 2.0e-2},
          { 25.0, 2.2e-3},
          { 50.0, 2.4e-4},
          { 75.0, 2.0e-5},
          {100.0, 1.4e-6},
      },
      12.00);
  return v;
}

inline constexpr double R_MARS_ATMO_REF = 3396200.0;    // rayon équatorial
inline constexpr double OMEGA_MARS = 7.088218e-5;       // rad/s (sol 24 h 37 min)

// Fabriques. `activity01` vient de `SolarCycle::activity01` [GDD 7.7].
inline ExponentialAtmosphere earth_atmosphere(double atmo_density_factor_value) {
  return ExponentialAtmosphere(&earth_layers(), cst::R_EARTH, cst::OMEGA_EARTH,
                               SOLAR_MIN_SCALE * atmo_density_factor_value);
}
inline ExponentialAtmosphere mars_atmosphere() {
  return ExponentialAtmosphere(&mars_layers(), R_MARS_ATMO_REF, OMEGA_MARS, 1.0);
}

} // namespace fen::env
