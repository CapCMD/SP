// fen/core/State.hpp
// L'état de vérité : position, vitesse, MASSE. La masse est DANS l'état intégré,
// pas une variable annexe : m_dot = -F/(Isp*g0) est une équation différentielle
// du système, au même titre que r_dot = v. Toute implémentation qui décrémente
// la masse "après coup" viole la conservation et fausse a = F/m pendant l'arc.
#pragma once
#include <array>
#include "fen/core/Vec3.hpp"

namespace fen {

// Layout figé : [0..2]=r (m), [3..5]=v (m/s), [6]=m (kg). Ordre de sommation
// stable pour le déterminisme bit-à-bit.
using StateN = std::array<double, 7>;
inline constexpr int N_STATE = 7;

struct State {
  Vec3 r;      // m, repère inertiel centré sur le corps central
  Vec3 v;      // m/s
  double m{};  // kg

  StateN pack() const { return {r.x, r.y, r.z, v.x, v.y, v.z, m}; }
  static State unpack(const StateN& y) {
    return State{Vec3{y[0], y[1], y[2]}, Vec3{y[3], y[4], y[5]}, y[6]};
  }
};

inline Vec3 pos(const StateN& y) { return {y[0], y[1], y[2]}; }
inline Vec3 vel(const StateN& y) { return {y[3], y[4], y[5]}; }
inline double mass(const StateN& y) { return y[6]; }

// Énergie spécifique (vis-viva) — sert d'INVARIANT de test de l'intégrateur.
inline double specific_energy(const Vec3& r, const Vec3& v, double mu) {
  return 0.5 * norm2(v) - mu / norm(r);
}

} // namespace fen
