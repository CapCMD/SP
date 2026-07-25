// fen/flight/Session.hpp
//
// L'EXECUTEUR PAS-A-PAS. C'est ici que vivent les etapes 4 et 5 de la boucle
// (navigation, correction), et il n'est PAS optionnel.
//
// Pourquoi le mode "batch" (tout le plan commis d'avance) ne suffit pas :
// un vrai transfert GTO->GEO ne se pilote pas comme ca. Ariane injecte, PUIS le
// satellite fait 3 a 5 manoeuvres d'apogee, CHACUNE recalculee a partir d'une
// determination d'orbite fraiche. Interdire le re-calcul entre deux manoeuvres,
// ce serait interdire la navigation — et rendre la mission mathematiquement
// insoluble des que Gates entre en jeu (verifie : cf. m00, graine 4071).
//
// LA REGLE RESTE INTACTE : le COMMIT gele la graine, le vehicule, l'argent et la
// date de lancement. Ce qui reste ouvert, c'est ce qui l'est dans la vraie vie :
// la valeur des manoeuvres futures, calculee par le joueur a partir des donnees
// de navigation qu'il a payees. Le jeu fournit un ETAT (ou un ESTIME). Il ne
// fournit JAMAIS un Delta-v.
//
// V1 : Session::observe() ne renverra plus la verite mais un ESTIME + covariance,
//      fonction du nombre de passes DSN achetees. La verite restera cachee
//      jusqu'au post-mortem. L'interface ci-dessous ne bougera pas.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "fen/flight/FlightPlan.hpp"
#include "fen/nav/OrbitDetermination.hpp"
#include "fen/nav/Tracking.hpp"

namespace fen::flight {

// CE QUE LE JOUEUR VOIT. Ce n'est PAS la verite : c'est un ESTIME, avec sa
// covariance, obtenu a partir des mesures qu'il a PAYEES. S'il n'en a achete
// aucune, c'est une navigation a l'estime — et il ignore que son erreur
// d'execution a eu lieu.
struct Observation {
  double t{};
  State  state;                 // ESTIME
  Mat6   covariance;            // m^2 / (m/s)^2
  double sigma_pos{0.0};        // m    (trace de la sous-matrice position)
  double sigma_vel{0.0};        // m/s
  int    n_measurements{0};
  bool   observable{true};      // false : les mesures achetees ne determinent pas l'orbite
  double rms_residual{0.0};     // en sigmas ; ~1 = le modele explique les donnees
  std::string source;
};

class Session {
 public:
  Session(FlightPlan plan, const ephem::IEphemeris& eph,
          std::uint64_t seed, prop::PropOptions opt);

  // Avance la VERITE jusqu'a t (TDB). Enregistre les evenements rencontres.
  bool advance_to(double t);

  // Avance jusqu'au prochain evenement nomme (ex: "APOAPSIS"), au plus t_max.
  // Renvoie l'instant de l'evenement, ou NaN s'il n'est pas atteint.
  double advance_to_event(const std::string& event_name, double t_max);

  // Avance jusqu'au PREMIER evenement de la liste. Necessaire parce que l'erreur
  // d'execution INVERSE l'identite des apsides : selon le signe du tirage, la
  // cible se retrouve au perigee ou a l'apogee. Un code qui cherche toujours
  // "PERIAPSIS" vise a cote une fois sur deux.
  double advance_to_any_event(const std::vector<std::string>& names, double t_max);

  // Execute une manoeuvre. Le Delta-v est CELUI DU JOUEUR : le moteur du jeu ne
  // le calcule pas, ne le corrige pas, ne le suggere pas. Il l'execute avec ses
  // erreurs et rend des comptes.
  BurnReport commit_burn(const BurnCmd& b);

  // ---- NAVIGATION : la connaissance est une ressource payante --------------
  // Reserver une passe de poursuite. Cela coute de l'argent AVANT le vol
  // (on reserve une antenne du reseau des semaines a l'avance, comme en vrai).
  void schedule_pass(const nav::Pass& p);
  double tracking_cost_musd() const { return tracking_cost_; }
  int    n_measurements() const { return static_cast<int>(meas_.size()); }

  // Donnee de navigation. SEUL canal d'information vers le joueur.
  // Lance la determination d'orbite sur les mesures acquises depuis la derniere
  // manoeuvre. Sans mesure : navigation a l'estime, covariance qui enfle.
  Observation observe();

  // VERITE. Reservee au post-mortem et au Monte-Carlo. Le joueur n'y a pas acces
  // pendant le vol : c'est tout l'objet de l'exercice.
  State truth_state() const { return State::unpack(y_); }

  // INTERRUPTEUR D'ABLATION — POST-MORTEM UNIQUEMENT.
  // Eteindre Gates SANS eteindre le bruit de mesure. Sans cette separation, on
  // ne peut pas isoler la contribution de l'EXECUTION de celle de la NAVIGATION :
  // une seule graine coupait les deux, et la decomposition sommait a 197 %.
  // Un budget d'erreur qui ne somme pas a 100 % n'est pas un budget : c'est une
  // opinion. Cet interrupteur n'existe QUE pour le post-mortem, jamais en vol.
  void set_gates_enabled(bool e) { gates_enabled_ = e; }

  // DEUX GRAINES INDEPENDANTES — POST-MORTEM / ANALYSE DE SENSIBILITE.
  //
  // L'erreur d'EXECUTION et le bruit de MESURE sont deux sources physiquement
  // distinctes. Les faire dependre d'une seule graine les rend indissociables :
  // on ne peut plus repondre a « laquelle domine ? », ni surtout a « comment
  // interagissent-elles ? ».
  //
  // Avec deux graines, on peut ECHANGER l'une sans toucher a l'autre — et c'est
  // exactement ce qu'exige le schema de Saltelli pour estimer les indices de
  // Sobol. L'architecture en sous-flux dedies (Rng::substream) rend cela exact :
  // changer la graine de Gates ne decale pas d'un bit les tirages de mesure.
  void set_seeds(std::uint64_t gates, std::uint64_t meas) {
    seed_gates_ = gates;
    seed_meas_ = meas;
  }

  double t() const { return t_; }
  const StateN& y() const { return y_; }   // verite brute (post-mortem)
  const FlightReport& report() const { return rep_; }
  bool alive() const { return rep_.ok; }

  // Delta-v encore disponible (Tsiolkovski sur l'etat courant). Ce n'est PAS un
  // attribut du vaisseau : c'est une fonction de la masse restante.
  double dv_remaining(std::size_t stage) const;
  double usable_propellant_remaining(std::size_t stage) const;

 private:
  double dry_floor(std::size_t k) const;

  FlightPlan plan_;
  const ephem::IEphemeris& eph_;
  std::uint64_t seed_;
  prop::PropOptions opt_;
  force::ForceStack gravity_;
  std::vector<prop::EventSpec> coast_events_;
  void sync_estimate_to(double t);
  void collect_measurements(double t_from, double t_to);

  double t_{};
  StateN y_{};
  FlightReport rep_;
  std::size_t burn_index_{0};

  // --- navigation ---
  std::vector<nav::Station> stations_;
  std::vector<nav::Pass>    passes_;
  std::vector<nav::Measurement> meas_;   // depuis la DERNIERE manoeuvre
  nav::StateEstimate est_;               // estime courant du joueur
  double tracking_cost_{0.0};
  StateN y_from_cache_{};                // etat au debut de l'arc courant (pour les mesures)
  bool gates_enabled_{true};             // ablation (post-mortem seulement)
  std::uint64_t seed_gates_{0}, seed_meas_{0};
};

} // namespace fen::flight
