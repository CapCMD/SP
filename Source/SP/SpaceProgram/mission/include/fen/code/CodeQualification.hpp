// fen/code/CodeQualification.hpp — BANC D'ESSAI & CERTIFICATION [GDD 15.5, v1.2]
//
// La v1.2 fait du TERMINAL un environnement de développement : le joueur écrit
// du logiciel de vol (API `ares::vol`), et TOUT code de vol passe par un banc
// d'essai avant téléversement. Ce fichier est la PARTIE MODÈLE de ce banc — le
// slice testable, indépendant de l'éditeur, de la toolchain embarquée et du bac
// à sable (qui viendront ensuite).
//
// LE POINT STRUCTURANT [GDD 15.5] : le banc N'EST PAS UN ORACLE. C'est un
// MODÈLE, donc une APPROXIMATION, avec un DOMAINE DE VALIDITÉ déclaré — comme
// toute approximation du moteur [6.8] et toute donnée de fiabilité [12.3.3].
//   . un code qualifié en orbite basse n'est PAS qualifié pour Mars ;
//   . un code validé sur profil nominal n'est PAS validé pour une trajectoire
//     dégradée ; testé sur une plage, pas au-delà ;
//   . EXÉCUTER HORS DU DOMAINE = comportement non couvert = cause d'anomalie
//     légitime au sens du chapitre 10.
//
// Le banc RASSURE SANS GARANTIR : la couverture croît avec les heures d'essai
// mais SATURE sous 1 — un état non imaginé passe toujours. C'est la source de
// tension du logiciel de vol.
//
// C++ pur. La certification produit une fiche assimilable à une fiche de
// fiabilité (identifiant, environnement, plages, confiance, date, historique).
#pragma once
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "fen/reliability/Reliability.hpp"

namespace fen::code {

// Le domaine de validité d'un code de vol — ce sur quoi le banc l'a exercé.
struct ValidityDomain {
  std::string environment;         // "LEO", "insertion_mars", "croisiere", ...
  double input_lo{0.0};            // plage d'entrée couverte (ex : vitesse d'entrée,
  double input_hi{0.0};            //   incertitude de nav, altitude...)
  bool degraded_profiles{false};   // le banc a-t-il exercé des profils DÉGRADÉS ?
  bool interfaces_tested{false};   // les interfaces inter-modules testées ?
                                   // [GDD 15.5 : le point faible naturel]
  bool covers(const std::string& env, double input, bool nominal) const {
    if (environment != env) return false;                 // LEO ≠ Mars
    if (input < input_lo || input > input_hi) return false; // hors plage
    if (!nominal && !degraded_profiles) return false;     // dégradé non couvert
    return true;
  }
};

// La fiche de certification produite par le banc [GDD 15.5], calquée sur une
// fiche de fiabilité [12.3.1].
struct Certification {
  std::string code_id;
  ValidityDomain domain;
  double coverage{0.0};            // fraction de l'espace d'état couverte [0,1)
  reliability::Confidence confidence{reliability::Confidence::D};
  double test_hours{0.0};
  double budget_spent_me{0.0};
  std::string date_ref;
  bool compiled{false};            // étape 1 : la compilation a réussi ?
  bool certified{false};           // le banc a produit une certification ?

  // Ce code est-il qualifié pour CETTE situation de vol ?
  bool qualifies(const std::string& env, double input, bool nominal) const {
    return certified && domain.covers(env, input, nominal);
  }
};

// Paramètres du banc [GDD Annexe E — à calibrer]. La couverture suit une
// saturation exponentielle : tester plus rassure, jamais ne garantit.
inline constexpr double BENCH_H_CHAR = 200.0;            // heures caractéristiques
inline constexpr double BENCH_COVERAGE_CEILING = 0.98;  // plafond < 1 [15.5]
inline constexpr double BENCH_COST_ME_PER_H = 0.05;     // coût du banc (M€/h)
inline constexpr double BENCH_DAYS_PER_H = 0.02;        // délai (retarde la fenêtre)

// ═══ ÉLARGIR LE DOMAINE DILUE LA COUVERTURE ═══ [GDD 15.5, Annexe E — à calibrer]
// Déclarer qu'on a exercé les profils DÉGRADÉS et les INTERFACES agrandit
// l'espace d'état que le banc prétend couvrir. À heures constantes, la même
// campagne s'étale alors sur plus grand : la couverture BAISSE.
// Sans cela, cocher les deux cases serait une montée en confiance GRATUITE —
// un domaine plus large certifié au même prix, c'est-à-dire une déclaration
// que rien ne paie. La rigueur s'achète en heures, comme le reste.
inline constexpr double BENCH_ELARG_DEGRADE = 2.0;      // les profils dégradés
inline constexpr double BENCH_ELARG_INTERFACES = 1.5;   // les interfaces inter-modules

inline double bench_h_char(const ValidityDomain& d) {
  double h = BENCH_H_CHAR;
  if (d.degraded_profiles) h *= BENCH_ELARG_DEGRADE;
  if (d.interfaces_tested) h *= BENCH_ELARG_INTERFACES;
  return h;
}

// LE BANC D'ESSAI. Rejoue le code contre un environnement simulé sous le domaine
// visé, produit une certification. Le code doit d'abord COMPILER (étape 1) — un
// code qui ne compile pas n'est jamais certifié.
inline Certification run_test_bench(const std::string& code_id, bool compiled,
                                    const ValidityDomain& target_domain,
                                    double test_hours) {
  Certification c;
  c.code_id = code_id;
  c.domain = target_domain;
  c.test_hours = std::max(0.0, test_hours);
  c.compiled = compiled;
  c.budget_spent_me = BENCH_COST_ME_PER_H * c.test_hours;
  if (!compiled) { c.certified = false; return c; }   // ne compile pas -> rien

  // COUVERTURE : saturation sous le plafond, sur l'étendue RÉELLEMENT visée
  // (voir `bench_h_char`). Sans essai, couverture nulle.
  c.coverage = BENCH_COVERAGE_CEILING *
               (1.0 - std::exp(-c.test_hours / bench_h_char(target_domain)));
  c.certified = true;

  // La CONFIANCE suit la couverture ET le fait d'avoir exercé les profils
  // dégradés et les interfaces (les défauts naturels [15.5]). Un code très
  // testé mais jamais sur ses interfaces ne dépasse pas B.
  const bool complet = target_domain.degraded_profiles && target_domain.interfaces_tested;
  if (c.coverage > 0.9 && complet)       c.confidence = reliability::Confidence::A;
  else if (c.coverage > 0.75)            c.confidence = reliability::Confidence::B;
  else if (c.coverage > 0.4)             c.confidence = reliability::Confidence::C;
  else                                    c.confidence = reliability::Confidence::D;
  return c;
}

// Le délai (jours) et le coût (M€) qu'a coûté ce banc — un arbitrage permanent
// [GDD 15.5] : tester exhaustivement retarde la fenêtre et consomme le budget.
inline double bench_delay_days(const Certification& c) { return BENCH_DAYS_PER_H * c.test_hours; }

// PROBABILITÉ que le code se comporte correctement en vol.
//   . HORS de son domaine de validité : comportement NON COUVERT -> 0 (anomalie
//     légitime [ch.10]) ;
//   . DANS son domaine : la couverture (rassure sans garantir : même à couverture
//     élevée, un état non imaginé peut échouer).
inline double code_success_prob(const Certification& c, const std::string& env,
                                double input, bool nominal) {
  if (!c.qualifies(env, input, nominal)) return 0.0;   // non couvert
  return c.coverage;
}

// EXÉCUTER HORS DU DOMAINE est la cause d'anomalie la plus fréquente du logiciel
// de vol [GDD 15.5] : ce prédicat sert à la lever dans la boucle de mission.
inline bool out_of_validity_domain(const Certification& c, const std::string& env,
                                   double input, bool nominal) {
  return !c.qualifies(env, input, nominal);
}

} // namespace fen::code
