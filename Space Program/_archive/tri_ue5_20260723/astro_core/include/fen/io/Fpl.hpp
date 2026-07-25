// fen/io/Fpl.hpp
// FORMAT DE PLAN DE VOL (.fpl) — la FRONTIÈRE entre le joueur et le monde.
//
// RÈGLE ABSOLUE : tout nombre dimensionnel porte un TOKEN D'UNITÉ EXPLICITE.
// Un nombre nu = erreur de parsing, jamais un défaut silencieux.
//
// Pourquoi ici et pas ailleurs : le système de types (Units.hpp) attrape les
// erreurs DIMENSIONNELLES à la compilation, mais il est aveugle aux erreurs
// d'ÉCHELLE (lbf·s vs N·s : même dimension). Or c'est exactement à l'INTERFACE
// entre deux équipes que Mars Climate Orbiter est mort en 1999. Donc c'est à
// l'interface qu'on met la barrière, et elle est obligatoire, pas optionnelle.
//
// Le troisième type d'erreur — la bonne unité et la mauvaise formule — n'est
// attrapé par rien. Il est révélé par la propagation. C'est le jeu.
#pragma once
#include <string>
#include <vector>
#include "fen/flight/FlightPlan.hpp"

namespace fen::io {

struct Goal {
  std::string key;    // sma | ecc | inc | rp | ra | payload
  double target{};    // SI
  double tol{};       // SI
  bool   is_min{false};
};

struct FplDocument {
  flight::FlightPlan plan;
  std::vector<Goal> goals;
  double budget_musd{0.0};
  double deadline_days{0.0};
  std::vector<std::string> warnings;
};

// Lève std::runtime_error avec un message EXPLOITABLE (ligne + raison).
FplDocument parse_fpl(const std::string& path);

// Conversion d'un token "12.3km/s" -> (valeur SI, dimension reconnue).
// Lève si le token est absent ou inconnu.
double parse_quantity(const std::string& tok, const char* expected_dim, int line);
Vec3   parse_vector(const std::string& tok, const char* expected_dim, int line);

} // namespace fen::io
