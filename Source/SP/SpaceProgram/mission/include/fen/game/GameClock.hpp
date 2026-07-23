// fen/game/GameClock.hpp — temps de jeu et déterminisme [GDD 14, M2]
//
// TEMPS RÉEL À LA CRÉATION UNIQUEMENT [GDD 14.1] : l'état du système solaire
// est calé sur la date réelle à la création de partie (WorldEpoch), puis le
// temps devient totalement indépendant et pilotable.
// PIÈGE CLASSIQUE [carte ⚠🔴] : le déterminisme sous accélération. Réponse :
// SOUS-PAS FIXES via accumulateur — accélérer ne change pas la taille des pas,
// seulement leur NOMBRE par frame. Byte-à-byte identique à toute vitesse,
// gardé par save::state_hash en CI.
#pragma once
#include <cmath>
#include "fen/core/Epoch.hpp"

namespace fen::game {

// Époque monde : figée à la création [GDD 7.3, 14.1]. Deux joueurs démarrant à
// six mois d'écart vivent des fenêtres de lancement différentes.
struct WorldEpoch {
  Epoch creation;         // TDB au moment de la création de la partie
  // Convertit "jours de jeu depuis création" -> Epoch TDB absolue (éphémérides).
  Epoch at(double game_days) const { return creation + game_days * cst::DAY; }
};

// Accélération temporelle [GDD 14.2] : paliers discrets, PAS un slider continu
// (un facteur arbitraire casserait la reproductibilité des prélèvements).
enum class TimeRate { Paused = 0, Realtime, Day, Week, Month };
inline double rate_seconds_per_second(TimeRate r) {
  switch (r) {
    case TimeRate::Paused:   return 0.0;
    case TimeRate::Realtime: return 1.0;
    case TimeRate::Day:      return cst::DAY;                 // 1 j / s réel
    case TimeRate::Week:     return 7.0 * cst::DAY;
    default:                 return 30.44 * cst::DAY;
  }
}

// --- DeterministicStepper ----------------------------------------------------
// Le tick monde avance par SOUS-PAS FIXES de dt_step (défaut : 1/64 de jour).
// L'accumulateur absorbe le reste : la séquence des pas est identique quelle
// que soit la cadence de rendu OU l'accélération choisie.
class GameClock {
 public:
  explicit GameClock(WorldEpoch epoch, double dt_step_s = cst::DAY / 64.0)
      : epoch_(epoch), dt_step_(dt_step_s) {}

  // À appeler chaque frame avec le temps réel écoulé. Renvoie le NOMBRE de
  // sous-pas fixes à exécuter maintenant ; l'appelant boucle dessus et appelle
  // ses systèmes (économie, recherche, événements) avec dt_step_days() constant.
  int advance(double real_dt_s) {
    accumulator_ += real_dt_s * rate_seconds_per_second(rate_);
    int steps = 0;
    while (accumulator_ >= dt_step_) {
      accumulator_ -= dt_step_;
      sim_time_s_ += dt_step_;
      ++steps;
    }
    return steps;
  }

  void set_rate(TimeRate r) { rate_ = r; }
  TimeRate rate() const { return rate_; }

  double now_days() const { return sim_time_s_ / cst::DAY; }
  double dt_step_days() const { return dt_step_ / cst::DAY; }
  Epoch  now_epoch() const { return epoch_.at(now_days()); }
  const WorldEpoch& world_epoch() const { return epoch_; }

  // Restauration exacte depuis une sauvegarde (l'accumulateur ne se sauve pas :
  // c'est du temps réel non encore converti, il repart à zéro).
  void restore(double sim_time_s) { sim_time_s_ = sim_time_s; accumulator_ = 0.0; }
  double sim_time_s() const { return sim_time_s_; }

 private:
  WorldEpoch epoch_;
  double dt_step_;
  double sim_time_s_{0.0};
  double accumulator_{0.0};
  TimeRate rate_{TimeRate::Paused};
};

} // namespace fen::game
