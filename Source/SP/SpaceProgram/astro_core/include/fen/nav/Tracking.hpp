// fen/nav/Tracking.hpp
//
// LA CONNAISSANCE EST UNE RESSOURCE PAYANTE.
//
// Jusqu'ici, Session::observe() rendait la vérité. C'était le dernier endroit du
// code où un axiome était violé par le moteur lui-même : la connaissance y était
// gratuite et infinie. Or on ne connaît pas sa trajectoire : on l'ESTIME, à
// partir de mesures qu'il a fallu acheter, et l'estimé a une covariance.
//
// Conséquence de jeu, entièrement dérivée : sans poursuite, le joueur ne SAIT
// PAS que son erreur d'exécution a eu lieu. Il croit que sa manoeuvre est passée
// au nominal. Sa correction est donc calculée sur un état faux — donc inutile.
//
// Mesures modélisées (2 voies cohérentes, standard réseau d'espace lointain) :
//   distance      rho    = |r - r_st|                  sigma ~ 2 m
//   vitesse rad.  rhodot = (r-r_st).(v-v_st)/rho       sigma ~ 0.1 mm/s
#pragma once
#include <string>
#include <vector>
#include <cmath>
#include "fen/core/Vec3.hpp"
#include "fen/core/Matrix.hpp"
#include "fen/core/Constants.hpp"

namespace fen::nav {

struct Station {
  std::string id;
  double lat_deg{};
  double lon_deg{};
  double alt_m{};
  double sigma_range{2.0};        // m
  double sigma_rangerate{1.0e-4}; // m/s  (0.1 mm/s)
  double elevation_mask_deg{10.0};
  double cost_musd_per_hour{0.15};
};

// Les trois complexes réels du Deep Space Network.
const std::vector<Station>& dsn_complexes();

// Position/vitesse d'une station dans le repère inertiel (ECI), à l'instant t.
// Terre = ellipsoïde WGS84 (une sphère déplacerait la station de 21 km : sans
// objet quand on mesure la distance à 2 m près).
// Rotation : GMST linéaire. Erreur ~ qq 10 m sur un an. Déclarée. V2 : IAU 2006.
Vec3 station_position_eci(const Station& s, double t_tdb);
Vec3 station_velocity_eci(const Station& s, double t_tdb);

bool station_visible(const Station& s, double t_tdb, const Vec3& r_sc);

// Une PASSE : ce que le joueur achète. Une antenne, une fenêtre, un pas.
struct Pass {
  int station{0};
  double t_start{};
  double t_end{};
  double sample_dt{60.0};
  double cost() const { return 0.0; }  // rempli par le catalogue
};

struct Measurement {
  double t{};
  int station{};
  double range{};        // m
  double range_rate{};   // m/s
  double sigma_range{};
  double sigma_rangerate{};
};

// Prédiction + partielles d'une mesure par rapport à (r, v) à l'instant t.
struct MeasPredict {
  double range{}, range_rate{};
  double H[2][6]{};   // d(rho, rhodot) / d(r, v)
};
MeasPredict predict(const Station& s, double t_tdb, const Vec3& r, const Vec3& v);

} // namespace fen::nav
