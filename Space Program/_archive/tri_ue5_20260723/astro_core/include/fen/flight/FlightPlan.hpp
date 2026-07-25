// fen/flight/FlightPlan.hpp
// LE PLAN DE VOL EST LA SEULE CHOSE QUE LE JOUEUR PRODUIT.
// Pas de pilotage, pas de manette, pas de noeud de manoeuvre auto-résolu.
// Une liste d'événements datés. Le monde l'exécute.
//
// L'EXÉCUTEUR est le lieu exact de la traduction "design -> vérité" :
//   entrée  : (t_c, dv_vec, frame, moteur)     [impulsion, ce que le joueur calcule]
//   sortie  : un arc de poussée FINIE, centré sur t_c, de durée t_b déduite de
//             Tsiolkovski et du débit massique, direction tenue dans le repère
//             choisi, Delta-v perturbé par Gates.
//
// Le Delta-v RÉALISÉ n'est jamais celui commandé. La différence (pertes de
// gravité et de braquage + erreur d'exécution) est mesurée, décomposée et
// facturée. C'est le contrat du jeu.
#pragma once
#include <string>
#include <vector>
#include "fen/core/Vec3.hpp"
#include "fen/core/State.hpp"
#include "fen/core/Rng.hpp"
#include "fen/ephem/Ephemeris.hpp"
#include "fen/force/Forces.hpp"
#include "fen/nav/Gates.hpp"
#include "fen/prop/Propagator.hpp"
#include "fen/vehicle/Vehicle.hpp"

namespace fen::flight {

enum class DvFrame { RSW, Inertial };

struct BurnCmd {
  std::string id;
  double t{};                 // s TDB depuis J2000 — INSTANT CENTRAL de l'arc
  Vec3 dv{};                  // m/s, dans `frame`, au moment t
  DvFrame frame{DvFrame::RSW};
  force::ThrustFrame hold{force::ThrustFrame::InertialFixed}; // programme d'attitude pendant l'arc
  std::size_t stage{0};       // index d'étage utilisé
};

struct FlightPlan {
  std::string mission_id;
  ephem::Body center{ephem::Body::EarthBary};
  std::vector<ephem::Body> perturbers;
  double epoch0{};            // s TDB
  State initial;              // r, v, m à epoch0
  std::vector<BurnCmd> burns; // triées par t
  // ÉVÉNEMENTS DÉFINIS PAR LE JOUEUR. Un concepteur de mission ne se contente pas
  // des périastres livrés d'usine : il veut « passage au plus près de Mars »,
  // « entrée d'ombre », « franchissement du corridor d'entrée ». Le moteur ne
  // peut pas les prévoir tous — donc il les laisse écrire.
  std::vector<prop::EventSpec> extra_events;
  double t_stop{};            // s TDB
  vehicle::Vehicle vehicle;
  nav::GatesParams gates;
};

// --- rapport d'exécution d'une manoeuvre ------------------------------------
struct BurnReport {
  std::string id;
  double t_ignition{}, t_cutoff{}, duration{};
  Vec3 dv_commanded{};        // ce que le joueur a demandé (impulsionnel)
  Vec3 dv_commanded_inertial{};
  Vec3 dv_perturbed{};        // après Gates (toujours impulsionnel : la consigne réelle)
  Vec3 dv_achieved{};         // v(t_cutoff) - v(t_ignition) - contribution gravitationnelle
  double dv_cmd_mag{}, dv_achieved_mag{};
  double finite_burn_loss{};  // |dv_cmd| - |dv_achieved_effectif|  [m/s]  >= 0 en général
  double propellant_used{};   // kg
  double mass_before{}, mass_after{};
  bool engine_cutoff_early{false}; // réservoir vide : la mission est déjà perdue
};

struct FlightReport {
  std::vector<BurnReport> burns;
  prop::PropResult truth;
  double total_propellant{};
  double total_dv_commanded{};
  double total_dv_achieved{};
  bool ok{true};
  std::string failure;
};

// Exécute le plan dans le PROPAGATEUR DE VÉRITÉ.
// seed == 0 -> exécution nominale, sans erreur (mode "conception", réversible).
// seed != 0 -> tirage Gates (mode "commit", irréversible).
FlightReport execute(const FlightPlan& plan, const ephem::IEphemeris& eph,
                     std::uint64_t seed, const prop::PropOptions& opt);

} // namespace fen::flight
