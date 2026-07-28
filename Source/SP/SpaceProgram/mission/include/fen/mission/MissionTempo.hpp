// fen/mission/MissionTempo.hpp — LE RYTHME DU TEMPS EN MISSION [GDD 14.3]
//
// « En mission vécue, certaines tâches se gèrent en temps réel (surveillance,
// expériences, corrections) ; le reste peut être accéléré. Toute manœuvre fine
// RAMÈNE le temps à un rythme lent. » [GDD 14.3, repris en 17.4]
//
// Le verbe du GDD est actif : une manœuvre fine ne SUGGÈRE pas un rythme, elle
// l'IMPOSE. Ce fichier produit donc un PLAFOND DE CADENCE opposable, et le
// moteur y ramène l'horloge de l'agence — le joueur garde le droit de ralentir
// ou de mettre en pause, jamais celui d'accélérer par-dessus.
//
// ═══ LE PLAFOND EST DÉDUIT, JAMAIS SAISI ═══ [doctrine du projet, cf. GDD 10.5]
// Un plafond écrit à la main par phase serait un chiffre magique de plus. Il se
// dérive d'un fait : à la cadence r, le temps de jeu défile de
// `rate_seconds_per_second(r)` secondes par seconde RÉELLE ; une phase de durée
// propre D disparaît donc en D / rate secondes réelles. Pour qu'elle soit
// observable ET actionnable, on exige qu'elle dure au moins OBSERVATION_MIN_S
// secondes réelles. Le plafond est le cran le plus rapide qui tient cette
// inégalité. Un seul paramètre libre dans toute la loi (OBSERVATION_MIN_S), et
// les DURÉES sont des grandeurs physiques sourcées, pas des réglages.
//
// Conséquence à l'échelle des cinq crans de `game::TimeRate` (1 s/s, puis
// 1 jour/s) : toute phase plus courte que ~10 jours retombe sur le TEMPS RÉEL.
// C'est le résultat attendu — « un rythme lent » — mais il est CALCULÉ, donc il
// se déplacera tout seul le jour où une phase durera des semaines.
//
// C++ pur, aucune dépendance UE.
#pragma once
#include <string>
#include <vector>

#include "fen/core/Constants.hpp"
#include "fen/game/GameClock.hpp"
#include "fen/mission/FlightTimeline.hpp"
#include "fen/mission/MissionFsm.hpp"

namespace fen::mission {

// Durée réelle minimale d'une phase critique, en secondes de temps RÉEL.
// APPROXIMATION DÉCLARÉE [GDD 6.8] : c'est le seul paramètre libre de la loi.
// 20 s = le temps de lire un écran et d'agir, pas celui de subir une cinématique.
inline constexpr double OBSERVATION_MIN_S = 20.0;

// Les DURÉES des phases et la CHRONOLOGIE qui les date vivent dans
// FlightTimeline.hpp : ce fichier ne porte que la LOI DU PLAFOND, qui les lit.
// La séparation n'est pas cosmétique — la loi ci-dessous serait identique si les
// durées venaient d'ailleurs, et c'est le signe qu'elle ne cache aucun réglage.

// Le cran le plus rapide qui laisse `duree_s` durer au moins OBSERVATION_MIN_S
// secondes réelles. PLANCHER : le temps réel — le modèle ne met JAMAIS le jeu en
// pause à la place du joueur [GDD 14], il l'empêche seulement d'aller trop vite.
inline game::TimeRate tempo_ceiling_for_duration(double duree_s) {
  using game::TimeRate;
  static const TimeRate crans[4] = {TimeRate::Month, TimeRate::Week,
                                    TimeRate::Day, TimeRate::Realtime};
  for (TimeRate r : crans)
    if (game::rate_seconds_per_second(r) * OBSERVATION_MIN_S <= duree_s) return r;
  return TimeRate::Realtime;
}

// Le plafond opposable à une phase : aucune contrainte hors phase critique.
inline game::TimeRate tempo_ceiling_for_phase(FlightPhase p) {
  if (!is_critical_phase(p)) return game::TimeRate::Month;   // aucun plafond
  return tempo_ceiling_for_duration(phase_duration_s(p));
}

// ═══ LE PLAFOND COURANT DE LA PARTIE ═══
// La mission la plus contraignante commande : deux vols en cours ne s'annulent
// pas, ils s'additionnent en exigence. `constrained` distingue « rien en cours »
// de « plafond au cran le plus haut », pour que l'affichage n'ait rien à deviner.
struct TempoLimit {
  game::TimeRate max_rate{game::TimeRate::Month};
  FlightPhase    phase{FlightPhase::Ground};
  bool           constrained{false};
  std::string    mission_id;            // qui impose le rythme, pour l'afficher
};

inline TempoLimit tempo_limit(const std::vector<Mission>& missions, double now_days) {
  TempoLimit lim;
  for (const Mission& m : missions) {
    const FlightPhase p = flight_phase_of(m, now_days);
    if (!is_critical_phase(p)) continue;
    const game::TimeRate r = tempo_ceiling_for_phase(p);
    if (!lim.constrained || static_cast<int>(r) < static_cast<int>(lim.max_rate)) {
      lim.max_rate = r;
      lim.phase = p;
      lim.constrained = true;
      lim.mission_id = m.contract.id;
    }
  }
  return lim;
}

} // namespace fen::mission
