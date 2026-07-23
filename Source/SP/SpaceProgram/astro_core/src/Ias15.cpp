#include "fen/prop/Ias15.hpp"
#include <cmath>
#include <algorithm>

namespace fen::prop {
namespace {

// --- transformation g -> b, CALCULEE a partir des noeuds ---------------------
// La serie d'acceleration s'ecrit dans deux bases :
//   Newton     : a(tau) = F0 + sum_k g_k * N_k(tau),  N_k = tau * PROD_{j=1..k}(tau - h_j)
//   puissances : a(tau) = F0 + sum_i b_i * tau^(i+1)
// On developpe donc N_k dans la base des puissances.
//
// Ces coefficients NE SONT PAS RECOPIES D'UNE TABLE. Ils sont calcules par
// multiplication de polynomes. Une table recopiee de travers est indetectable
// a l'oeil ; un polynome developpe a partir des noeuds, non. C'est le meme
// principe que partout ailleurs dans ce noyau : on derive, on ne recopie pas.
struct NewtonToPower {
  double M[7][7]{};   // M[k][i] = coefficient de tau^(i+1) dans N_k
  NewtonToPower() {
    for (int k = 0; k < 7; ++k) {
      double p[9] = {1, 0, 0, 0, 0, 0, 0, 0, 0};   // p = PROD_{j=1..k} (tau - h_j)
      int deg = 0;
      for (int j = 1; j <= k; ++j) {
        double q[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
        for (int i = 0; i <= deg; ++i) {
          q[i + 1] += p[i];                       // multiplication par tau
          q[i]     -= IAS15_H[j] * p[i];          // multiplication par (-h_j)
        }
        ++deg;
        for (int i = 0; i <= deg; ++i) p[i] = q[i];
      }
      for (int i = 0; i <= deg && i < 7; ++i) M[k][i] = p[i];   // N_k = tau * p
    }
  }
};
const NewtonToPower& n2p() { static const NewtonToPower x; return x; }

// Serie de vitesse et de position, integrales EXACTES de a(tau).
inline Vec3 sum_v(const Vec3& a0, const std::array<Vec3, 7>& b, double x) {
  return a0 + (b[0] * (1.0 / 2) + (b[1] * (1.0 / 3) + (b[2] * (1.0 / 4)
       + (b[3] * (1.0 / 5) + (b[4] * (1.0 / 6) + (b[5] * (1.0 / 7)
       +  b[6] * (x / 8)) * x) * x) * x) * x) * x) * x;
}
inline Vec3 sum_r(const Vec3& a0, const std::array<Vec3, 7>& b, double x) {
  return a0 * 0.5 + (b[0] * (1.0 / 6) + (b[1] * (1.0 / 12) + (b[2] * (1.0 / 20)
       + (b[3] * (1.0 / 30) + (b[4] * (1.0 / 42) + (b[5] * (1.0 / 56)
       +  b[6] * (x / 72)) * x) * x) * x) * x) * x) * x;
}

} // namespace

bool Ias15::step(const Deriv& f, double& t, StateN& y, double& h, DenseSegment& seg) {
  const double sgn = (h >= 0.0) ? 1.0 : -1.0;
  h = sgn * std::clamp(std::fabs(h), ctl_.h_min, ctl_.h_max);

  StateN dy{};
  f(t, y, dy);
  const Vec3 r0 = pos(y), v0 = vel(y);
  const Vec3 a0{dy[3], dy[4], dy[5]};
  const double m0 = mass(y);
  const double mdot = dy[6];   // CONSTANT sur le pas : les points de rupture le garantissent

  std::array<Vec3, 7> b = have_b_ ? b_ : std::array<Vec3, 7>{};

  for (int attempt = 0; attempt < 40; ++attempt) {
    // Mise a l'echelle des b du pas precedent (Rein & Spiegel, §3) : b_k ~ h^(k+1)
    if (have_b_ && h_prev_ != 0.0 && attempt == 0) {
      const double q = h / h_prev_;
      double qk = 1.0;
      for (int k = 0; k < 7; ++k) { qk *= q; b[k] = b_[k] * qk; }  // b_k ~ h^(k+1)
    }

    std::array<Vec3, 8> F{};
    std::array<Vec3, 7> g{};
    F[0] = a0;

    // ---- PREDICTEUR-CORRECTEUR sur les 7 noeuds internes ----
    int iters = 0;
    for (int iter = 0; iter < 20; ++iter) {
      const std::array<Vec3, 7> b_old = b;

      for (int n = 1; n < 8; ++n) {
        const double x = IAS15_H[n];
        const Vec3 vn = v0 + sum_v(a0, b, x) * (h * x);
        const Vec3 rn = r0 + v0 * (h * x) + sum_r(a0, b, x) * (h * h * x * x);
        const StateN yn{rn.x, rn.y, rn.z, vn.x, vn.y, vn.z, m0 + mdot * h * x};
        StateN dyn{};
        f(t + h * x, yn, dyn);
        F[n] = Vec3{dyn[3], dyn[4], dyn[5]};
      }

      // ---- table des differences divisees de Newton : F -> g ----
      Vec3 D[8][8];
      for (int i = 0; i < 8; ++i) D[i][0] = F[i];
      for (int j = 1; j < 8; ++j)
        for (int i = j; i < 8; ++i)
          D[i][j] = (D[i][j - 1] - D[i - 1][j - 1]) / (IAS15_H[i] - IAS15_H[i - j]);
      for (int k = 0; k < 7; ++k) g[k] = D[k + 1][k + 1];

      // ---- g -> b ----
      const auto& M = n2p().M;
      for (int i = 0; i < 7; ++i) {
        Vec3 s{};
        for (int k = i; k < 7; ++k) s += g[k] * M[k][i];
        b[i] = s;
      }

      ++iters;
      double dmax = 0.0, bmax = 1e-300;
      for (int k = 0; k < 7; ++k) {
        dmax = std::max(dmax, norm(b[k] - b_old[k]));
        bmax = std::max(bmax, norm(b[k]));
      }
      if (iter >= 2 && dmax <= 1e-13 * bmax) break;
    }

    // ---- controle de pas : le dernier coefficient PORTE l'erreur ----
    const double an = std::max(1e-300, norm(a0));
    const double err = norm(b[6]) / an;
    // On BORNE la tolerance par le plancher de bruit de l'estimateur. Demander
    // moins, ce n'est pas etre exigeant : c'est demander a un thermometre de
    // mesurer sa propre resolution. Le pas s'effondrerait pour rien.
    const double tol = std::max(ctl_.rtol, IAS15_EPS_FLOOR);
    const double fac = std::clamp(0.85 * std::pow(tol / std::max(err, 1e-300), 1.0 / 7.0),
                                  ctl_.fac_min, ctl_.fac_max);

    if (err <= tol || std::fabs(h) <= ctl_.h_min * 1.0000001) {
      seg.kind = DenseKind::Ias15;
      seg.t0 = t; seg.h = h;
      seg.r0 = r0; seg.v0 = v0; seg.a0 = a0;
      seg.b = b; seg.m0 = m0; seg.mdot = mdot;
      y = seg.eval(t + h);
      t += h;
      b_ = b; have_b_ = true; h_prev_ = h;
      h = sgn * std::clamp(std::fabs(h) * fac, ctl_.h_min, ctl_.h_max);
      ++n_acc_;
      return true;
    }
    h = sgn * std::clamp(std::fabs(h) * std::min(fac, 1.0), ctl_.h_min, ctl_.h_max);
    ++n_rej_;
  }
  return false;
}

} // namespace fen::prop
