// fen/mission/Events.hpp — événements aléatoires calibrés [GDD 9.4]
//
// La probabilité n'est JAMAIS un dé nu : elle est fonction de la durée de
// mission, de la fiabilité effective des composants (reliability::evaluate)
// et de la phase de vol (accrue en manœuvre critique) [GDD 9.4, 7.3].
// Déterminisme : chaque source d'aléa tire d'un SUBSTREAM dédié du Rng —
// ajouter un événement ne décale pas les autres [Rng.hpp].
#pragma once
#include <cmath>
#include <string>
#include <vector>
#include "fen/core/Rng.hpp"
#include "fen/env/SpaceWeather.hpp"

namespace fen::mission {

// Phase de vol courante — pilote les taux [carte M3.a PhaseContext].
enum class FlightPhase {
  Ground = 0, Launch, LeoOps, TransferCruise, CriticalManeuver, Edl, SurfaceOps,
};

enum class EventKind {
  Micrometeorite, SolarParticleEvent, LifeSupportFault, MedicalEmergency,
  CommLoss, PowerFault,
};
inline const char* event_name(EventKind k) {
  switch (k) {
    case EventKind::Micrometeorite:     return "impact micrometeorite";
    case EventKind::SolarParticleEvent: return "eruption solaire (SPE)";
    case EventKind::LifeSupportFault:   return "panne support-vie";
    case EventKind::MedicalEmergency:   return "urgence medicale";
    case EventKind::CommLoss:           return "perte de communication";
    default:                            return "defaut electrique";
  }
}

// Une entrée de bibliothèque : taux de base par JOUR, modulé ensuite.
// Les taux sont des hypothèses de modèle DÉCLARÉES [GDD 6.8].
struct EventSpec {
  EventKind kind{};
  double base_rate_per_day{};     // λ0, mission robotique nominale en croisière
  bool   crewed_only{false};
  double phase_factor_critical{3.0};  // multiplicateur en manœuvre/EDL
};

inline const std::vector<EventSpec>& event_library() {
  static const std::vector<EventSpec> v = {
      {EventKind::Micrometeorite,     1.0e-4, false, 1.0},
      {EventKind::SolarParticleEvent, 0.0,    false, 1.0},  // taux via SpaceWeather
      {EventKind::LifeSupportFault,   8.0e-4, true,  2.0},
      {EventKind::MedicalEmergency,   5.0e-4, true,  1.5},
      {EventKind::CommLoss,           6.0e-4, false, 2.0},
      {EventKind::PowerFault,         4.0e-4, false, 2.0},
  };
  return v;
}

struct SampledEvent {
  EventKind kind{};
  double t_days{};                // date d'occurrence dans la fenêtre
  double magnitude01{};           // 0..1 : sévérité brute, interprétée par kind
};

// --- EventSampler ------------------------------------------------------------
// Tire les événements d'une fenêtre [t0, t0+dt] jours. Processus de Poisson
// par type : P(au moins un) = 1 - exp(-λ_eff · dt), tiré sur substream dédié.
struct EventContext {
  bool crewed{false};
  FlightPhase phase{FlightPhase::TransferCruise};
  double system_reliability{0.98};  // rollup mission (reliability::) sur la fenêtre
  double solar_activity01{0.5};     // SolarCycle::activity01
  double medical_risk_factor{1.0};  // station::StationEffects (préparation équipage)
};

inline double effective_rate(const EventSpec& s, const EventContext& c) {
  double lambda = s.base_rate_per_day;
  if (s.kind == EventKind::SolarParticleEvent)
    lambda = env::spe_rate_per_year(c.solar_activity01) / 365.25;
  // La fiabilité effective du vaisseau module les pannes internes : un système
  // fiable à 99.9 % ne casse pas comme un système à 90 %.
  if (s.kind == EventKind::LifeSupportFault || s.kind == EventKind::PowerFault)
    lambda *= (1.0 - c.system_reliability) / 0.02;   // normalisé à R=0.98
  if (s.kind == EventKind::MedicalEmergency)
    lambda *= c.medical_risk_factor;
  const bool critical = (c.phase == FlightPhase::CriticalManeuver ||
                         c.phase == FlightPhase::Edl ||
                         c.phase == FlightPhase::Launch);
  if (critical) lambda *= s.phase_factor_critical;
  return lambda;
}

inline std::vector<SampledEvent> sample_events(const Rng& mission_rng,
                                               std::uint64_t window_id,
                                               double t0_days, double dt_days,
                                               const EventContext& ctx) {
  std::vector<SampledEvent> out;
  int stream = 0;
  for (const auto& spec : event_library()) {
    Rng r = mission_rng.substream(0x4556454E00000000ull ^ window_id * 64 + stream++);
    if (spec.crewed_only && !ctx.crewed) continue;
    const double lambda = effective_rate(spec, ctx);
    if (lambda <= 0.0) continue;
    // Tirages successifs des temps d'attente exponentiels dans la fenêtre.
    double t = t0_days;
    for (;;) {
      double u;
      do { u = r.uniform01(); } while (u < 1e-300);
      t += -std::log(u) / lambda;
      if (t >= t0_days + dt_days) break;
      out.push_back(SampledEvent{spec.kind, t, r.uniform01()});
    }
  }
  return out;
}

// Dose SPE brute d'un événement tiré : magnitude01 -> Gy non blindés (0.1..5).
// Log-uniforme : les monstres sont rares mais existent [GDD 6.6].
inline double spe_unshielded_gy(double magnitude01) {
  return 0.1 * std::pow(50.0, magnitude01);
}

} // namespace fen::mission
