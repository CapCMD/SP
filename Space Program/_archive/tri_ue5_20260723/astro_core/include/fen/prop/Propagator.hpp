// fen/prop/Propagator.hpp
// LE PROPAGATEUR DE VÉRITÉ. Il n'y en a qu'un. Tout le reste (Kepler,
// patched-conic, Hohmann) est un outil de conception que le joueur choisit.
//
// Trois propriétés non négociables :
//  1. Événements par RECHERCHE DE RACINE sur l'interpolant dense, pas par test
//     de signe à chaque pas. L'instant d'un périastre ne doit pas dépendre du
//     pas de l'intégrateur.
//  2. POINTS DE RUPTURE (breakpoints) : l'intégrateur ne franchit jamais un
//     allumage/extinction moteur à l'intérieur d'un pas. La discontinuité de
//     a(t) et mdot(t) tuerait l'estimateur d'erreur et rendrait le résultat
//     dépendant du hasard du pas.
//  3. Aucun RNG ici. L'aléa est injecté en amont (Delta-v perturbé par Gates),
//     jamais tiré pendant l'intégration. => rejouabilité bit-à-bit.
#pragma once
#include <functional>
#include <string>
#include <vector>
#include "fen/core/State.hpp"
#include "fen/force/Forces.hpp"
#include "fen/prop/Integrator.hpp"

namespace fen::prop {

struct EventSpec {
  std::string name;
  // g(t, y) : l'événement est la racine de g. Doit être continue et C1.
  std::function<double(double, const StateN&)> g;
  int direction{0};       // +1 montant, -1 descendant, 0 les deux
  bool terminal{false};   // arrête la propagation
};

struct EventHit {
  std::string name;
  double t{};
  StateN y{};
};

struct Sample { double t{}; StateN y{}; };

struct PropOptions {
  StepControl step{};
  double sample_dt{0.0};              // 0 = pas d'échantillonnage régulier
  std::vector<double> sample_times{}; // instants EXPLICITES (instants de mesure).
                                      // Évalués sur l'interpolant dense : la mesure
                                      // ne dépend donc pas du pas de l'intégrateur.
  std::vector<double> breakpoints{};  // instants à ne PAS franchir (allumages, etc.)
  double event_tol{1e-9};             // s
};

struct PropResult {
  double t_final{};
  StateN y_final{};
  std::vector<Sample> samples;
  std::vector<EventHit> events;
  bool terminated_by_event{false};
  std::string termination_reason;
  long long steps_accepted{0}, steps_rejected{0};
  bool ok{true};
};

// --- événements standard (fabriques) ---------------------------------------
EventSpec event_periapsis(double mu);        // d(r·v)/dt = 0, montant
EventSpec event_apoapsis(double mu);
EventSpec event_altitude(double r_body, double alt, int direction, bool terminal);
EventSpec event_impact(double r_body);       // altitude 0, descendant, terminal
EventSpec event_soi_crossing(const ephem::IEphemeris* eph, ephem::Body body,
                             ephem::Body center, double r_soi);
EventSpec event_propellant_exhausted(double dry_mass);

PropResult propagate(const force::ForceStack& forces, double t0, const StateN& y0,
                     double t_end, const std::vector<EventSpec>& events,
                     const PropOptions& opt);

} // namespace fen::prop
