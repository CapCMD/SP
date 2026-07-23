#include "fen/astro/Lambert.hpp"
#include "fen/core/Constants.hpp"
#include <cmath>
#include <algorithm>
#include <limits>

namespace fen::astro {
namespace {

// 2F1(3,1,5/2,z) par sa série hypergéométrique (Battin) — utilisée près de x=1
// où la formulation générale perd sa précision.
double hyperF(double z, double tol) {
  double Sj = 1.0, Cj = 1.0, err = 1.0;
  int j = 0;
  while (err > tol && j < 200) {
    const double Cj1 = Cj * (3.0 + j) * (1.0 + j) / (2.5 + j) * z / (j + 1.0);
    const double Sj1 = Sj + Cj1;
    err = std::fabs(Cj1);
    Sj = Sj1; Cj = Cj1;
    ++j;
  }
  return Sj;
}

// Temps de vol adimensionné T(x) — expression de Lagrange (branche "sûre" à
// distance de x=1).
double x2tof_lagrange(double x, int N, double lam) {
  const double a = 1.0 / (1.0 - x * x);
  if (a > 0.0) { // ellipse
    double alfa = 2.0 * std::acos(x);
    double beta = 2.0 * std::asin(std::sqrt(lam * lam / a));
    if (lam < 0.0) beta = -beta;
    return (a * std::sqrt(a) * ((alfa - std::sin(alfa)) - (beta - std::sin(beta))
            + cst::TWO_PI * N)) * 0.5;
  }
  double alfa = 2.0 * std::acosh(x);
  double beta = 2.0 * std::asinh(std::sqrt(-lam * lam / a));
  if (lam < 0.0) beta = -beta;
  return (-a * std::sqrt(-a) * ((beta - std::sinh(beta)) - (alfa - std::sinh(alfa)))) * 0.5;
}

double x2tof(double x, int N, double lam) {
  constexpr double BATTIN = 0.01;
  constexpr double LAGRANGE = 0.2;
  const double dist = std::fabs(x - 1.0);
  if (dist < LAGRANGE && dist > BATTIN) return x2tof_lagrange(x, N, lam);

  const double K = lam * lam;
  const double E = x * x - 1.0;
  const double rho = std::fabs(E);
  const double z = std::sqrt(1.0 + K * E);

  if (dist < BATTIN) { // série de Battin
    const double eta = z - lam * x;
    const double S1 = 0.5 * (1.0 - lam - x * eta);
    double Q = hyperF(S1, 1e-11);
    Q = (4.0 / 3.0) * Q;
    return 0.5 * (eta * eta * eta * Q + 4.0 * lam * eta) + N * cst::PI / std::pow(rho, 1.5);
  }
  const double y = std::sqrt(rho);
  const double g = x * z - lam * E;
  double d;
  if (E < 0.0) {                       // ellipse
    const double l = std::acos(g);
    d = N * cst::PI + l;
  } else {                             // hyperbole
    const double f = y * (z - lam * x);
    d = std::log(f + g);
  }
  return (x - lam * z - d / y) / E;
}

struct Derivs { double dT, ddT, dddT; };

Derivs dTdx(double x, double T, double lam) {
  const double l2 = lam * lam;
  const double l3 = l2 * lam;
  const double umx2 = 1.0 - x * x;
  const double y = std::sqrt(1.0 - l2 * umx2);
  const double y2 = y * y, y3 = y2 * y, y5 = y3 * y2;
  Derivs d;
  d.dT   = (3.0 * T * x - 2.0 + 2.0 * l3 * x / y) / umx2;
  d.ddT  = (3.0 * T + 5.0 * x * d.dT + 2.0 * (1.0 - l2) * l3 / y3) / umx2;
  d.dddT = (7.0 * x * d.ddT + 8.0 * d.dT - 6.0 * (1.0 - l2) * l2 * l3 * x / y5) / umx2;
  return d;
}

// Itération de Householder d'ordre 3.
double householder(double T, double x0, int N, double lam, double tol, int max_iter, int& iters) {
  double x = x0;
  for (int it = 0; it < max_iter; ++it) {
    const double tof = x2tof(x, N, lam);
    const Derivs d = dTdx(x, tof, lam);
    const double delta = tof - T;
    const double DT2 = d.dT * d.dT;
    const double xnew = x - delta * (DT2 - delta * d.ddT * 0.5) /
                        (d.dT * (DT2 - delta * d.ddT) + d.dddT * delta * delta / 6.0);
    const double err = std::fabs(x - xnew);
    x = xnew;
    iters = it + 1;
    if (err < tol) break;
  }
  return x;
}

} // namespace

LambertResult lambert(const Vec3& r1v, const Vec3& r2v, double tof, double mu,
                      bool prograde, int max_revs, double tol, int max_iter) {
  LambertResult res;
  if (tof <= 0.0)  { res.error = "tof <= 0"; return res; }
  if (mu  <= 0.0)  { res.error = "mu <= 0";  return res; }

  const double r1 = norm(r1v), r2 = norm(r2v);
  const Vec3 cv = r2v - r1v;
  const double c = norm(cv);
  const double s = 0.5 * (r1 + r2 + c);
  if (s <= 0.0 || r1 <= 0.0 || r2 <= 0.0) { res.error = "geometrie degeneree"; return res; }

  const Vec3 ir1 = r1v / r1;
  const Vec3 ir2 = r2v / r2;
  Vec3 ih = unit(cross(ir1, ir2));
  if (norm2(ih) < 1e-20) { res.error = "r1 et r2 colineaires (plan indetermine)"; return res; }

  double lambda2 = 1.0 - c / s;
  double lambda  = std::sqrt(std::fmax(0.0, lambda2));

  Vec3 it1, it2;
  if (ih.z < 0.0) {              // angle de transfert > pi pour un prograde
    lambda = -lambda;
    it1 = cross(ir1, ih);
    it2 = cross(ir2, ih);
  } else {
    it1 = cross(ih, ir1);
    it2 = cross(ih, ir2);
  }
  it1 = unit(it1);
  it2 = unit(it2);

  if (!prograde) {
    lambda = -lambda;
    it1 = -it1;
    it2 = -it2;
  }

  const double T = std::sqrt(2.0 * mu / (s * s * s)) * tof;
  lambda2 = lambda * lambda;

  // --- nombre max de révolutions physiquement atteignables ---
  int Nmax = static_cast<int>(T / cst::PI);
  Nmax = std::min(Nmax, max_revs);

  // --- amorçage 0-rev (Izzo §4) ---
  const double T00 = std::acos(lambda) + lambda * std::sqrt(1.0 - lambda2); // T(x=0, N=0)
  const double T0  = T00;                                                   // N=0
  const double T1  = (2.0 / 3.0) * (1.0 - lambda2 * lambda);                // T(x=1) parabolique

  // T(x) est STRICTEMENT DÉCROISSANTE sur x in (-1, +inf) :
  //   x -> -1+ : ellipse "grand tour",  T -> +inf
  //   x  =  0  : ellipse d'énergie minimale, T = T0
  //   x  =  1  : parabole,                   T = T1
  //   x  >  1  : hyperbole,                  T < T1
  // Les amorçages ci-dessous respectent EXACTEMENT ces trois points d'ancrage
  // (c'est la seule façon de vérifier qu'ils sont justes sans les croire sur parole).
  std::vector<std::pair<double,int>> starts; // (x0, N)
  double x0;
  if (T >= T0) {                       // x in (-1, 0]
    x0 = -(T - T0) / (T - T0 + 4.0);   //  T=T0 -> 0 ;  T->inf -> -1
  } else if (T <= T1) {                // x >= 1 (hyperbolique)
    x0 = 2.5 * T1 * (T1 - T) / (T * (1.0 - lambda2 * lambda2 * lambda)) + 1.0; // T=T1 -> 1
  } else {                             // x in (0, 1)
    // exposant p tel que x0(T0) = 0 et x0(T1) = 1  =>  p = ln2 / ln(T0/T1)
    const double p = std::log(2.0) / std::log(T0 / T1);
    x0 = std::pow(T0 / T, p) - 1.0;
  }
  starts.emplace_back(x0, 0);

  // --- amorçages multi-rev (branches gauche et droite) ---
  for (int N = 1; N <= Nmax; ++N) {
    const double tmp = std::pow((N * cst::PI + cst::PI) / (8.0 * T), 2.0 / 3.0);
    const double xl = (tmp - 1.0) / (tmp + 1.0);                 // branche gauche
    const double tmp2 = std::pow((8.0 * T) / (N * cst::PI), 2.0 / 3.0);
    const double xr = (tmp2 - 1.0) / (tmp2 + 1.0);               // branche droite
    starts.emplace_back(xl, N);
    starts.emplace_back(xr, N);
  }

  const double gamma = std::sqrt(mu * s * 0.5);
  const double rho   = (r1 - r2) / c;
  const double sigma = std::sqrt(std::fmax(0.0, 1.0 - rho * rho));

  for (auto& [xs, N] : starts) {
    int iters = 0;
    double x = householder(T, xs, N, lambda, tol, max_iter, iters);
    double tof_check = std::isfinite(x) ? x2tof(x, N, lambda)
                                        : std::numeric_limits<double>::quiet_NaN();
    bool good = std::isfinite(tof_check) && std::fabs(tof_check - T) <= 1e-6 * std::fmax(1.0, T);

    // FILET DE SÉCURITÉ (N = 0 uniquement) : T(x) étant monotone, la racine est
    // TOUJOURS encadrable. Un solveur de conception qui échoue une fois sur cent
    // est inutilisable : il ferait échouer un porkchop entier.
    if (!good && N == 0) {
      double lo = -1.0 + 1e-12, hi = 1.0;
      while (x2tof(hi, 0, lambda) > T && hi < 1e6) hi *= 2.0;   // pousse vers l'hyperbole
      for (int it = 0; it < 200; ++it) {
        const double mid = 0.5 * (lo + hi);
        (x2tof(mid, 0, lambda) > T ? lo : hi) = mid;            // T décroissante en x
      }
      x = 0.5 * (lo + hi);
      // raffinement final par Householder depuis un point encadré
      x = householder(T, x, 0, lambda, tol, max_iter, iters);
      tof_check = x2tof(x, 0, lambda);
      good = std::isfinite(tof_check) && std::fabs(tof_check - T) <= 1e-6 * std::fmax(1.0, T);
    }
    if (!good) continue;

    const double y = std::sqrt(1.0 - lambda2 + lambda2 * x * x);
    const double vr1 =  gamma * ((lambda * y - x) - rho * (lambda * y + x)) / r1;
    const double vr2 = -gamma * ((lambda * y - x) + rho * (lambda * y + x)) / r2;
    const double vt  =  gamma * sigma * (y + lambda * x);
    const double vt1 = vt / r1;
    const double vt2 = vt / r2;

    LambertSolution sol;
    sol.v1 = ir1 * vr1 + it1 * vt1;
    sol.v2 = ir2 * vr2 + it2 * vt2;
    sol.revolutions = N;
    sol.left_branch = (N > 0 && xs < 0.0);
    sol.iterations = iters;
    res.solutions.push_back(sol);
  }

  res.ok = !res.solutions.empty();
  if (!res.ok) res.error = "aucune solution convergee";
  return res;
}

} // namespace fen::astro
