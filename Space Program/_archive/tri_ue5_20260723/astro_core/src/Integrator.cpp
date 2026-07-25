#include "fen/prop/Integrator.hpp"
#include <cmath>
#include <algorithm>

namespace fen::prop {
namespace {

// --- tableau de Butcher Dormand-Prince 5(4), 7 étages, FSAL ------------------
constexpr double C2 = 1.0 / 5.0, C3 = 3.0 / 10.0, C4 = 4.0 / 5.0, C5 = 8.0 / 9.0;

constexpr double A21 = 1.0 / 5.0;
constexpr double A31 = 3.0 / 40.0,      A32 = 9.0 / 40.0;
constexpr double A41 = 44.0 / 45.0,     A42 = -56.0 / 15.0,     A43 = 32.0 / 9.0;
constexpr double A51 = 19372.0 / 6561.0, A52 = -25360.0 / 2187.0,
                 A53 = 64448.0 / 6561.0, A54 = -212.0 / 729.0;
constexpr double A61 = 9017.0 / 3168.0, A62 = -355.0 / 33.0, A63 = 46732.0 / 5247.0,
                 A64 = 49.0 / 176.0,    A65 = -5103.0 / 18656.0;
// solution d'ordre 5 (= a7j, FSAL)
constexpr double B1 = 35.0 / 384.0, B3 = 500.0 / 1113.0, B4 = 125.0 / 192.0,
                 B5 = -2187.0 / 6784.0, B6 = 11.0 / 84.0;
// estimateur d'erreur : b - b_hat (ordre 4)
constexpr double E1 = 71.0 / 57600.0, E3 = -71.0 / 16695.0, E4 = 71.0 / 1920.0,
                 E5 = -17253.0 / 339200.0, E6 = 22.0 / 525.0, E7 = -1.0 / 40.0;
// coefficients de l'interpolant dense (Shampine)
constexpr double D1 = -12715105075.0 / 11282082432.0;
constexpr double D3 = 87487479700.0 / 32700410799.0;
constexpr double D4 = -10690763975.0 / 1880347072.0;
constexpr double D5 = 701980252875.0 / 199316789632.0;
constexpr double D6 = -1453857185.0 / 822651844.0;
constexpr double D7 = 69997945.0 / 29380423.0;

inline void axpy(StateN& out, const StateN& y, double h, const StateN& k, double c) {
  for (int i = 0; i < N_STATE; ++i) out[i] = y[i] + h * c * k[i];
}

} // namespace

bool Dopri5::step(const Deriv& f, double& t, StateN& y, double& h, DenseSegment& seg) {
  StateN k1, k2, k3, k4, k5, k6, k7, tmp;

  if (have_k1_ && t == t_k1_) k1 = k1_;
  else { f(t, y, k1); have_k1_ = true; }

  // Pas SIGNÉ : h < 0 = intégration rétrograde (exigée par la détermination
  // d'orbite et par le recul jusqu'à l'allumage d'un arc centré).
  const double sgn = (h >= 0.0) ? 1.0 : -1.0;
  h = sgn * std::clamp(std::fabs(h), ctl_.h_min, ctl_.h_max);

  for (int attempt = 0; attempt < 60; ++attempt) {
    axpy(tmp, y, h, k1, A21);                                     f(t + C2 * h, tmp, k2);
    for (int i = 0; i < N_STATE; ++i) tmp[i] = y[i] + h * (A31 * k1[i] + A32 * k2[i]);
    f(t + C3 * h, tmp, k3);
    for (int i = 0; i < N_STATE; ++i) tmp[i] = y[i] + h * (A41 * k1[i] + A42 * k2[i] + A43 * k3[i]);
    f(t + C4 * h, tmp, k4);
    for (int i = 0; i < N_STATE; ++i)
      tmp[i] = y[i] + h * (A51 * k1[i] + A52 * k2[i] + A53 * k3[i] + A54 * k4[i]);
    f(t + C5 * h, tmp, k5);
    for (int i = 0; i < N_STATE; ++i)
      tmp[i] = y[i] + h * (A61 * k1[i] + A62 * k2[i] + A63 * k3[i] + A64 * k4[i] + A65 * k5[i]);
    f(t + h, tmp, k6);

    StateN y1;
    for (int i = 0; i < N_STATE; ++i)
      y1[i] = y[i] + h * (B1 * k1[i] + B3 * k3[i] + B4 * k4[i] + B5 * k5[i] + B6 * k6[i]);
    f(t + h, y1, k7);   // FSAL

    // --- erreur locale, norme RMS pondérée ---
    double err2 = 0.0;
    for (int i = 0; i < N_STATE; ++i) {
      const double e = h * (E1 * k1[i] + E3 * k3[i] + E4 * k4[i] + E5 * k5[i]
                            + E6 * k6[i] + E7 * k7[i]);
      const double sc = ctl_.atol + ctl_.rtol * std::max(std::fabs(y[i]), std::fabs(y1[i]));
      const double r = e / sc;
      err2 += r * r;
    }
    const double err = std::sqrt(err2 / N_STATE);

    const double fac = (err > 0.0)
        ? std::clamp(ctl_.safety * std::pow(1.0 / err, 0.2), ctl_.fac_min, ctl_.fac_max)
        : ctl_.fac_max;

    if (err <= 1.0 || std::fabs(h) <= ctl_.h_min * 1.0000001) {
      // --- accepté : construction de l'interpolant dense ---
      seg.t0 = t;
      seg.h = h;
      for (int i = 0; i < N_STATE; ++i) {
        const double ydiff = y1[i] - y[i];
        const double bspl = h * k1[i] - ydiff;
        seg.c[0][i] = y[i];
        seg.c[1][i] = ydiff;
        seg.c[2][i] = bspl;
        seg.c[3][i] = ydiff - h * k7[i] - bspl;
        seg.c[4][i] = h * (D1 * k1[i] + D3 * k3[i] + D4 * k4[i] + D5 * k5[i]
                           + D6 * k6[i] + D7 * k7[i]);
      }
      t += h;
      y = y1;
      k1_ = k7;
      t_k1_ = t;
      h = sgn * std::clamp(std::fabs(h) * fac, ctl_.h_min, ctl_.h_max);
      ++n_acc_;
      return true;
    }
    h = sgn * std::clamp(std::fabs(h) * fac, ctl_.h_min, ctl_.h_max);
    ++n_rej_;
  }
  return false; // échec : pas minimal atteint sans convergence
}

} // namespace fen::prop
