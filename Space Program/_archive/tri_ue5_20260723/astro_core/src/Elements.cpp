#include "fen/astro/Elements.hpp"
#include "fen/astro/Kepler.hpp"
#include "fen/core/Constants.hpp"
#include <cmath>
#include <limits>

namespace fen::astro {

static constexpr double TOL_E = 1e-11;
static constexpr double TOL_I = 1e-11;

Elements rv_to_elements(const Vec3& r, const Vec3& v, double mu) {
  Elements el;
  const double rn = norm(r);
  const double vn = norm(v);
  const Vec3 h = cross(r, v);
  const double hn = norm(h);
  const Vec3 n = cross(Vec3{0, 0, 1}, h);        // ligne des noeuds
  const double nn = norm(n);

  const Vec3 evec = (r * (vn * vn - mu / rn) - v * dot(r, v)) / mu;
  el.e = norm(evec);
  el.p = hn * hn / mu;

  const double energy = 0.5 * vn * vn - mu / rn;
  if (std::fabs(el.e - 1.0) > 1e-10) {
    el.a  = -mu / (2.0 * energy);
    el.rp = el.a * (1.0 - el.e);
    el.ra = (el.e < 1.0) ? el.a * (1.0 + el.e) : std::numeric_limits<double>::quiet_NaN();
  } else {
    el.a  = std::numeric_limits<double>::infinity();
    el.rp = el.p / 2.0;
    el.ra = std::numeric_limits<double>::quiet_NaN();
  }

  el.i = std::acos(std::fmin(1.0, std::fmax(-1.0, h.z / hn)));
  el.circular   = (el.e < TOL_E);
  el.equatorial = (nn < TOL_I * hn);

  if (!el.equatorial) {
    el.raan = std::atan2(n.y, n.x);
    if (el.raan < 0) el.raan += cst::TWO_PI;
  } else {
    el.raan = 0.0; // convention : noeud placé sur +X
  }

  if (!el.circular) {
    if (!el.equatorial) {
      el.argp = std::acos(std::fmin(1.0, std::fmax(-1.0, dot(n, evec) / (nn * el.e))));
      if (evec.z < 0) el.argp = cst::TWO_PI - el.argp;
    } else {
      // longitude vraie du périastre
      el.argp = std::atan2(evec.y, evec.x);
      if (h.z < 0) el.argp = cst::TWO_PI - el.argp; // rétrograde équatoriale
      if (el.argp < 0) el.argp += cst::TWO_PI;
    }
    el.nu = std::acos(std::fmin(1.0, std::fmax(-1.0, dot(evec, r) / (el.e * rn))));
    if (dot(r, v) < 0) el.nu = cst::TWO_PI - el.nu;
  } else {
    el.argp = 0.0;
    if (!el.equatorial) {
      // argument de latitude u
      el.nu = std::acos(std::fmin(1.0, std::fmax(-1.0, dot(n, r) / (nn * rn))));
      if (r.z < 0) el.nu = cst::TWO_PI - el.nu;
    } else {
      // longitude vraie
      el.nu = std::atan2(r.y, r.x);
      if (h.z < 0) el.nu = cst::TWO_PI - el.nu;
      if (el.nu < 0) el.nu += cst::TWO_PI;
    }
  }
  return el;
}

void elements_to_rv(const Elements& el, double mu, Vec3& r, Vec3& v) {
  const double p = (el.e != 1.0 && std::isfinite(el.a)) ? el.a * (1.0 - el.e * el.e) : el.p;
  const double cnu = std::cos(el.nu), snu = std::sin(el.nu);
  const double rmag = p / (1.0 + el.e * cnu);

  // dans le repère périfocal (P,Q,W)
  const Vec3 r_pqw{rmag * cnu, rmag * snu, 0.0};
  const double sp = std::sqrt(mu / p);
  const Vec3 v_pqw{-sp * snu, sp * (el.e + cnu), 0.0};

  const double cO = std::cos(el.raan), sO = std::sin(el.raan);
  const double cw = std::cos(el.argp), sw = std::sin(el.argp);
  const double ci = std::cos(el.i),    si = std::sin(el.i);

  // R = Rz(-raan) Rx(-i) Rz(-argp)
  const double m11 =  cO * cw - sO * sw * ci;
  const double m12 = -cO * sw - sO * cw * ci;
  const double m13 =  sO * si;
  const double m21 =  sO * cw + cO * sw * ci;
  const double m22 = -sO * sw + cO * cw * ci;
  const double m23 = -cO * si;
  const double m31 =  sw * si;
  const double m32 =  cw * si;
  const double m33 =  ci;

  r = Vec3{m11 * r_pqw.x + m12 * r_pqw.y + m13 * r_pqw.z,
           m21 * r_pqw.x + m22 * r_pqw.y + m23 * r_pqw.z,
           m31 * r_pqw.x + m32 * r_pqw.y + m33 * r_pqw.z};
  v = Vec3{m11 * v_pqw.x + m12 * v_pqw.y + m13 * v_pqw.z,
           m21 * v_pqw.x + m22 * v_pqw.y + m23 * v_pqw.z,
           m31 * v_pqw.x + m32 * v_pqw.y + m33 * v_pqw.z};
}

double orbital_period(double a, double mu) {
  if (a <= 0.0 || !std::isfinite(a)) return std::numeric_limits<double>::quiet_NaN();
  return cst::TWO_PI * std::sqrt(a * a * a / mu);
}

double nu_to_M(double nu, double e) {
  if (e < 1.0) {
    const double E = 2.0 * std::atan2(std::sqrt(1.0 - e) * std::sin(0.5 * nu),
                                      std::sqrt(1.0 + e) * std::cos(0.5 * nu));
    return E - e * std::sin(E);
  }
  const double H = 2.0 * std::atanh(std::sqrt((e - 1.0) / (e + 1.0)) * std::tan(0.5 * nu));
  return e * std::sinh(H) - H;
}

double M_to_nu(double M, double e) {
  if (e < 1.0) {
    const double E = solve_kepler_elliptic(M, e);
    return 2.0 * std::atan2(std::sqrt(1.0 + e) * std::sin(0.5 * E),
                            std::sqrt(1.0 - e) * std::cos(0.5 * E));
  }
  const double H = solve_kepler_hyperbolic(M, e);
  return 2.0 * std::atan(std::sqrt((e + 1.0) / (e - 1.0)) * std::tanh(0.5 * H));
}

} // namespace fen::astro
