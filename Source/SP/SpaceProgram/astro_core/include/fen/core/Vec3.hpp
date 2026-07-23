// fen/core/Vec3.hpp
// Vecteur 3D en double, SI STRICT (m, m/s, m/s^2). Pas d'unités fortes ici :
// voir DOCTRINE dans Units.hpp — les unités fortes vivent AUX INTERFACES,
// pas dans les boucles chaudes de l'intégrateur.
#pragma once
#include <cmath>

namespace fen {

struct Vec3 {
  double x{}, y{}, z{};
  constexpr Vec3() = default;
  constexpr Vec3(double a, double b, double c) : x(a), y(b), z(c) {}

  constexpr Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
  constexpr Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
  constexpr Vec3 operator-() const { return {-x, -y, -z}; }
  constexpr Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
  constexpr Vec3 operator/(double s) const { return {x / s, y / s, z / s}; }
  Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
  Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
  Vec3& operator*=(double s) { x *= s; y *= s; z *= s; return *this; }

  constexpr double operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }
  double& operator[](int i) { return i == 0 ? x : (i == 1 ? y : z); }
};

constexpr Vec3 operator*(double s, const Vec3& v) { return v * s; }
constexpr double dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
constexpr Vec3 cross(const Vec3& a, const Vec3& b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline double norm2(const Vec3& v) { return dot(v, v); }
inline double norm(const Vec3& v) { return std::sqrt(dot(v, v)); }
inline Vec3 unit(const Vec3& v) { const double n = norm(v); return n > 0.0 ? v / n : Vec3{}; }

// --- Repère RSW (Radial / along-track S / cross-track W) ---------------------
// C'est LE repère dans lequel le joueur exprime ses Delta-v. Il est défini par
// l'état courant : R = r_hat, W = h_hat, S = W x R.  (Vallado, §3.4)
struct Basis3 { Vec3 R, S, W; };

inline Basis3 rsw_basis(const Vec3& r, const Vec3& v) {
  const Vec3 Rh = unit(r);
  const Vec3 Wh = unit(cross(r, v));
  const Vec3 Sh = cross(Wh, Rh);
  return {Rh, Sh, Wh};
}
inline Vec3 rsw_to_inertial(const Basis3& b, const Vec3& dv_rsw) {
  return b.R * dv_rsw.x + b.S * dv_rsw.y + b.W * dv_rsw.z;
}
inline Vec3 inertial_to_rsw(const Basis3& b, const Vec3& v_in) {
  return {dot(v_in, b.R), dot(v_in, b.S), dot(v_in, b.W)};
}

// Rotation d'un vecteur autour d'un axe unitaire (Rodrigues).
inline Vec3 rotate(const Vec3& v, const Vec3& axis_unit, double angle_rad) {
  const double c = std::cos(angle_rad), s = std::sin(angle_rad);
  return v * c + cross(axis_unit, v) * s + axis_unit * (dot(axis_unit, v) * (1.0 - c));
}

} // namespace fen
