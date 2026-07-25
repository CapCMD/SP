#include "fen/astro/Kepler.hpp"
#include "fen/core/Constants.hpp"
#include <cmath>

namespace fen::astro {

double stumpff_C(double z) {
  if (z > 1e-6) {
    const double s = std::sqrt(z);
    return (1.0 - std::cos(s)) / z;
  }
  if (z < -1e-6) {
    const double s = std::sqrt(-z);
    return (std::cosh(s) - 1.0) / (-z);
  }
  // série : 1/2 - z/24 + z^2/720 - z^3/40320 + z^4/3628800
  return 0.5 + z * (-1.0 / 24.0 + z * (1.0 / 720.0 + z * (-1.0 / 40320.0 + z / 3628800.0)));
}

double stumpff_S(double z) {
  if (z > 1e-6) {
    const double s = std::sqrt(z);
    return (s - std::sin(s)) / (s * s * s);
  }
  if (z < -1e-6) {
    const double s = std::sqrt(-z);
    return (std::sinh(s) - s) / (s * s * s);
  }
  // série : 1/6 - z/120 + z^2/5040 - z^3/362880 + z^4/39916800
  return 1.0 / 6.0 + z * (-1.0 / 120.0 + z * (1.0 / 5040.0 + z * (-1.0 / 362880.0 + z / 39916800.0)));
}

KeplerResult kepler_propagate(const Vec3& r0, const Vec3& v0, double dt, double mu,
                              double tol, int max_iter) {
  KeplerResult out;
  if (dt == 0.0) { out.r = r0; out.v = v0; out.converged = true; return out; }

  const double sqmu = std::sqrt(mu);
  const double r0n  = norm(r0);
  const double v0n2 = norm2(v0);
  const double rdotv = dot(r0, v0);
  const double alpha = 2.0 / r0n - v0n2 / mu; // = 1/a  (>0 ellipse, <0 hyperbole)

  // --- amorçage de chi (Vallado, algo 8) ---
  double chi;
  if (alpha > 1e-12) {                       // elliptique
    chi = sqmu * dt * alpha;
    // garde-fou : proche du cas circulaire chi ~ sqrt(mu)*dt*alpha est excellent
  } else if (std::fabs(alpha) < 1e-12) {     // parabolique (Barker)
    const Vec3 h = cross(r0, v0);
    const double p = norm2(h) / mu;
    const double s = 0.5 * std::atan(1.0 / (3.0 * std::sqrt(mu / (p * p * p)) * dt));
    const double w = std::atan(std::cbrt(std::tan(s)));
    chi = std::sqrt(p) * 2.0 / std::tan(2.0 * w);
  } else {                                   // hyperbolique
    const double a = 1.0 / alpha;            // < 0
    const double sgn = (dt >= 0.0) ? 1.0 : -1.0;
    const double num = -2.0 * mu * alpha * dt;
    const double den = rdotv + sgn * std::sqrt(-mu * a) * (1.0 - r0n * alpha);
    chi = sgn * std::sqrt(-a) * std::log(num / den);
  }

  // --- Newton sur l'équation de Kepler universelle ---
  double z = 0.0, C = 0.0, S = 0.0, r = r0n;
  for (int i = 0; i < max_iter; ++i) {
    z = alpha * chi * chi;
    C = stumpff_C(z);
    S = stumpff_S(z);
    r = chi * chi * C + (rdotv / sqmu) * chi * (1.0 - z * S) + r0n * (1.0 - z * C);
    const double F = (rdotv / sqmu) * chi * chi * C + (1.0 - alpha * r0n) * chi * chi * chi * S
                     + r0n * chi - sqmu * dt;
    if (std::fabs(F) < tol * (1.0 + std::fabs(sqmu * dt))) {
      out.iterations = i; out.converged = true; break;
    }
    const double dchi = F / r; // dF/dchi == r  (identité universelle)
    chi -= dchi;
    out.iterations = i + 1;
  }

  z = alpha * chi * chi;
  C = stumpff_C(z);
  S = stumpff_S(z);
  r = chi * chi * C + (rdotv / sqmu) * chi * (1.0 - z * S) + r0n * (1.0 - z * C);

  // --- coefficients de Lagrange ---
  const double f    = 1.0 - (chi * chi / r0n) * C;
  const double g    = dt - (chi * chi * chi / sqmu) * S;
  const double gdot = 1.0 - (chi * chi / r) * C;
  const double fdot = (sqmu / (r * r0n)) * chi * (z * S - 1.0);

  out.r = r0 * f + v0 * g;
  out.v = r0 * fdot + v0 * gdot;
  return out;
}

double solve_kepler_elliptic(double M, double e, double tol, int max_iter) {
  M = std::fmod(M, cst::TWO_PI);
  if (M < 0) M += cst::TWO_PI;
  // amorçage de Danby : robuste jusqu'à e -> 1
  double E = M + 0.85 * e * ((M > cst::PI) ? -1.0 : 1.0);
  for (int i = 0; i < max_iter; ++i) {
    const double s = e * std::sin(E), c = e * std::cos(E);
    const double f = E - s - M;
    if (std::fabs(f) < tol) break;
    const double fp = 1.0 - c;
    const double fpp = s;
    const double fppp = c;
    const double d1 = -f / fp;
    const double d2 = -f / (fp + 0.5 * d1 * fpp);
    const double d3 = -f / (fp + 0.5 * d2 * fpp + d2 * d2 * fppp / 6.0);
    E += d3;
  }
  return E;
}

double solve_kepler_hyperbolic(double M, double e, double tol, int max_iter) {
  double H = (std::fabs(M) > 6.0) ? std::copysign(std::log(2.0 * std::fabs(M) / e + 1.8), M)
                                  : M / (e - 1.0);
  for (int i = 0; i < max_iter; ++i) {
    const double f = e * std::sinh(H) - H - M;
    if (std::fabs(f) < tol) break;
    H -= f / (e * std::cosh(H) - 1.0);
  }
  return H;
}

} // namespace fen::astro
