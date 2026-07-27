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
#include "fen/mission/MissionFsm.hpp"

namespace fen::mission {

// Durée réelle minimale d'une phase critique, en secondes de temps RÉEL.
// APPROXIMATION DÉCLARÉE [GDD 6.8] : c'est le seul paramètre libre de la loi.
// 20 s = le temps de lire un écran et d'agir, pas celui de subir une cinématique.
inline constexpr double OBSERVATION_MIN_S = 20.0;

// ═══ DURÉES CARACTÉRISTIQUES DES PHASES ═══ (secondes de temps de JEU)
// Ordres de grandeur RÉELS et sourcés [GDD 6.8], jamais des valeurs de confort :
//   . ascension sol -> orbite ~9 min   (Falcon 9 : SECO à T+8 min 40 ;
//                                       Navette : MECO à T+8 min 30) ;
//   . EDL ~7 min                       (MSL, « seven minutes of terror » :
//                                       entrée atmosphérique -> toucher) ;
//   . manœuvre critique ~10 min        (insertion orbitale : Apollo LOI 6 min 2 s,
//                                       ordre de grandeur de la dizaine de min).
// Les phases NON critiques n'ont pas de durée opposable : elles durent ce que la
// trajectoire dure, et rien n'y exige la présence du joueur.
inline double phase_duration_s(FlightPhase p) {
  switch (p) {
    case FlightPhase::Launch:           return 9.0 * 60.0;
    case FlightPhase::Edl:              return 7.0 * 60.0;
    case FlightPhase::CriticalManeuver: return 10.0 * 60.0;
    default:                            return 0.0;   // pas de durée opposable
  }
}

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

// ═══ LA PHASE DE VOL EST DÉRIVÉE, PAS SAISIE ═══
// `Mission::phase` existait sans que rien ne la renseigne : un drapeau qu'on ne
// pouvait que cocher à la main, donc un « malus abstrait » en puissance. Elle est
// désormais FONCTION de l'état FSM, du temps passé dans cet état et de la
// famille — déterministe, rejouable, et rien à sauvegarder.
//
// CE QUE LE MODÈLE DATE AUJOURD'HUI : l'ASCENSION, qui commence à l'instant du
// feu vert (entrée en `Launched`) et dure `phase_duration_s(Launch)`. Ensuite la
// mission croise (ou opère en LEO pour les familles proches de la Terre).
// L'INSERTION et l'EDL sont des phases critiques du modèle (Events.hpp les
// module déjà) mais ne sont pas encore DATÉES : elles le seront quand la mission
// vécue [GDD 9] portera sa chronologie de vol. DÉCLARÉ ici plutôt que simulé par
// un tirage : on ne date pas un événement qu'on ne calcule pas.
inline bool near_earth_family(const std::string& family) {
  return family == "sat" || family == "logistique" || family == "service" ||
         family == "habite";
}

inline FlightPhase flight_phase_of(const Mission& m, double now_days) {
  if (m.state != MissionState::Launched) return FlightPhase::Ground;
  const double t_s = (now_days - m.state_entered_days) * cst::DAY;
  if (t_s < phase_duration_s(FlightPhase::Launch)) return FlightPhase::Launch;
  return near_earth_family(m.contract.family) ? FlightPhase::LeoOps
                                              : FlightPhase::TransferCruise;
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
