// fen/env/SpaceWeather.hpp — cycle solaire et météo spatiale [GDD 7.7]
//
// L'activité solaire est un ACTEUR DE MISSION, pas un malus abstrait. Elle
// couple DEUX effets opposés dans le même paramètre :
//   activité haute  -> plus d'éruptions (SPE) MAIS moins de GCR (modulation
//                      héliosphérique) ET plus de traînée (haute atmosphère
//                      gonflée -> durée de vie orbitale réduite, débris nettoyés).
//   activité basse  -> l'inverse.
// Modèle V1 DÉCLARÉ [GDD 6.8] : cycle sinusoïdal de 11 ans calé sur l'époque
// monde (WorldEpoch). V2 : indices F10.7/sunspot réels tabulés.
#pragma once
#include <cmath>
#include "fen/core/Constants.hpp"
#include "fen/core/Epoch.hpp"

namespace fen::env {

struct SolarCycle {
  double period_s{11.0 * 365.25 * cst::DAY};
  // Instant (TDB s depuis J2000) d'un MAXIMUM solaire de référence.
  // Cal. sur le maximum du cycle 25 (~oct. 2024 ≈ +7.82e8 s J2000).
  double t_max_ref{7.82e8};

  // Activité normalisée [0..1] : 1 = maximum solaire, 0 = minimum.
  double activity01(Epoch e) const {
    const double phase = cst::TWO_PI * (e.tdb - t_max_ref) / period_s;
    return 0.5 * (1.0 + std::cos(phase));
  }
};

// Taux d'éruptions majeures (SPE significatifs pour une dose d'équipage).
// Modèle déclaré : ~1/an au minimum, ~10/an au maximum solaire.
inline double spe_rate_per_year(double activity01) {
  return 1.0 + 9.0 * activity01;
}

// Multiplicateur de densité de haute atmosphère (traînée LEO) selon l'activité.
// Ordre de grandeur réel à 400-500 km : facteur 3-8 entre min et max ; on
// déclare 1..6 (V1). Alimente AtmosphereModel et le modèle de débris [GDD 7.8].
inline double atmo_density_factor(double activity01) {
  return 1.0 + 5.0 * activity01;
}

// Modulation GCR par l'héliosphère : le flux GCR chute quand le Soleil est
// actif. Facteur déclaré 1.0 (min solaire) -> 0.55 (max solaire).
inline double gcr_modulation(double activity01) {
  return 1.0 - 0.45 * activity01;
}

} // namespace fen::env
