// fen/core/Rng.hpp
//
// CONTRAINTE NON NÉGOCIABLE : déterminisme BIT-À-BIT.
// Le mécanisme de commit du jeu ("une seule graine, un seul vol") n'a de sens
// que si la même graine produit exactement le même vol, sur toutes les machines.
//
// C'est pourquoi on N'UTILISE PAS <random> :
//  - std::mt19937_64 est spécifié par la norme (OK),
//  - mais std::normal_distribution / uniform_real_distribution NE LE SONT PAS :
//    leur implémentation (donc leur sortie) diffère entre libstdc++, libc++ et
//    MSVC. Un save-game deviendrait irreproductible entre plateformes.
// => générateur + transformations écrits ici, entièrement spécifiés.
#pragma once
#include <cstdint>
#include <cmath>
#include "fen/core/Constants.hpp"
#include "fen/core/Vec3.hpp"

namespace fen {

class Rng {
 public:
  explicit Rng(std::uint64_t seed) : state_(seed ? seed : 0x9E3779B97F4A7C15ull), seed0_(seed) {}

  std::uint64_t next_u64() { // splitmix64 (Vigna) — 64 bits d'état, sortie spécifiée
    std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
  }

  // [0,1) sur 53 bits — reproductible exactement (pas de dépendance libstdc++)
  double uniform01() { return static_cast<double>(next_u64() >> 11) * 0x1.0p-53; }

  double uniform(double a, double b) { return a + (b - a) * uniform01(); }

  // N(0,1) — Box-Muller polaire, cache d'une valeur.
  double normal() {
    if (has_cached_) { has_cached_ = false; return cached_; }
    double u1;
    do { u1 = uniform01(); } while (u1 < 1e-300);
    const double u2 = uniform01();
    const double r  = std::sqrt(-2.0 * std::log(u1));
    const double th = cst::TWO_PI * u2;
    cached_ = r * std::sin(th);
    has_cached_ = true;
    return r * std::cos(th);
  }

  double normal(double mean, double sigma) { return mean + sigma * normal(); }

  // Sous-flux indépendant et reproductible : chaque source d'aléa (exécution de
  // la poussée n°k, panne n°j, bruit de mesure DSN) tire d'un flux DÉDIÉ, pour
  // que l'ajout d'une source ne décale pas les autres (variance reduction +
  // rejouabilité stricte du post-mortem).
  Rng substream(std::uint64_t id) const {
    std::uint64_t z = seed0_ ^ (id * 0xD1B54A32D192ED03ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return Rng(z ^ (z >> 31));
  }

  Vec3 normal_vec3() { return Vec3{normal(), normal(), normal()}; }

 private:
  std::uint64_t state_;
  std::uint64_t seed0_;
  double cached_{0.0};
  bool has_cached_{false};
};

} // namespace fen
