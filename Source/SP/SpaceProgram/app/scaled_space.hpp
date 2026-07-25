// app/scaled_space.hpp — COMPRESSION DE PROFONDEUR de la carte (« scaled space »).
//
// POURQUOI. La carte est à l'ÉCHELLE VRAIE (1 unité = 1 km) : elle couvre du
// rayon d'une lune (~1e3) à la ceinture de Kuiper (~5e9), soit neuf ordres de
// grandeur. Aucun tampon de profondeur ne tient cette plage, et pousser des
// objets à 4,5e9 unités stresse le moteur pour des points sous-pixelliques.
//
// COMMENT. Au-delà d'une distance D0, on rapproche RADIALEMENT l'objet de l'œil
// et on réduit son rayon de la MÊME homothétie :
//
//     d' = D0 · (1 + ln(d / D0))        f = d' / d        rayon' = rayon · f
//
// GARANTIES (vérifiées par oracles, cf. tests) :
//   1. DIRECTION INCHANGÉE   : la position comprimée reste colinéaire et de
//      même sens que l'originale -> la position à l'écran est EXACTE.
//   2. TAILLE ANGULAIRE INCHANGÉE : rayon'/d' = rayon/d -> le corps occupe
//      exactement le même nombre de pixels.
//   3. MONOTONIE : d1 < d2  =>  d1' < d2' -> l'ordre d'occultation est préservé.
//   4. IDENTITÉ EN DEÇÀ DE D0 : la zone regardée garde sa géométrie stricte,
//      sans aucune déformation.
//
// C'est donc une approximation qui ne touche QUE la profondeur — déclarée ici,
// conformément à la traçabilité des approximations [GDD 6.8].
//
// C++ pur : aucune dépendance UnrealEngine (inclus des deux côtés du pont).
#pragma once
#include <cmath>

namespace fen::app {

// Distance en deçà de laquelle rien n'est comprimé (km) : ~ la distance
// Terre-Lune. Tout ce qu'on regarde de près est donc strictement à sa place.
inline constexpr double SCALED_SPACE_KM = 3.0e5;

// Facteur d'homothétie f = d'/d à appliquer à la position ET au rayon.
// d <= D0 -> 1 (aucune compression).
inline double scaled_space_factor(double d_km) {
  if (!(d_km > SCALED_SPACE_KM)) return 1.0;   // couvre aussi NaN et d <= 0
  return SCALED_SPACE_KM * (1.0 + std::log(d_km / SCALED_SPACE_KM)) / d_km;
}

// Distance comprimée (km).
inline double scaled_space_distance(double d_km) {
  return d_km * scaled_space_factor(d_km);
}

} // namespace fen::app
