// fen/nav/Statistics.hpp
// Statistiques du Monte-Carlo. Le jeu ne dit JAMAIS "il y a un risque".
// Il dit : "P(succès) = 0.87 ; Delta-v_99 = 68 m/s ; votre marge = 41 m/s".
#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

namespace fen::nav {

struct Stats {
  double mean{}, sigma{}, min{}, max{};
  double p50{}, p95{}, p99{}, p999{};
  std::size_t n{};
};

inline double percentile(std::vector<double> v, double p) {
  if (v.empty()) return 0.0;
  std::sort(v.begin(), v.end());
  const double x = p * (static_cast<double>(v.size()) - 1.0);
  const std::size_t i = static_cast<std::size_t>(std::floor(x));
  const double frac = x - static_cast<double>(i);
  if (i + 1 >= v.size()) return v.back();
  return v[i] * (1.0 - frac) + v[i + 1] * frac;
}

inline Stats summarize(const std::vector<double>& v) {
  Stats s;
  s.n = v.size();
  if (v.empty()) return s;
  double sum = 0.0;
  for (double x : v) sum += x;
  s.mean = sum / static_cast<double>(v.size());
  double acc = 0.0;
  for (double x : v) acc += (x - s.mean) * (x - s.mean);
  s.sigma = (v.size() > 1) ? std::sqrt(acc / (static_cast<double>(v.size()) - 1.0)) : 0.0;
  s.min = *std::min_element(v.begin(), v.end());
  s.max = *std::max_element(v.begin(), v.end());
  s.p50  = percentile(v, 0.50);
  s.p95  = percentile(v, 0.95);
  s.p99  = percentile(v, 0.99);
  s.p999 = percentile(v, 0.999);
  return s;
}

// Ellipse de dispersion 2D (plan-B) : valeurs et directions propres de la
// covariance. C'est ce qu'on dessine, et rien d'autre.
struct Ellipse2D {
  double cx{}, cy{};
  double semi_major{}, semi_minor{}, angle_rad{};
};

inline Ellipse2D covariance_ellipse(const std::vector<double>& x, const std::vector<double>& y,
                                    double k_sigma = 3.0) {
  Ellipse2D e;
  const std::size_t n = x.size();
  if (n < 2 || y.size() != n) return e;
  double mx = 0, my = 0;
  for (std::size_t i = 0; i < n; ++i) { mx += x[i]; my += y[i]; }
  mx /= static_cast<double>(n); my /= static_cast<double>(n);
  double sxx = 0, syy = 0, sxy = 0;
  for (std::size_t i = 0; i < n; ++i) {
    const double dx = x[i] - mx, dy = y[i] - my;
    sxx += dx * dx; syy += dy * dy; sxy += dx * dy;
  }
  const double d = static_cast<double>(n) - 1.0;
  sxx /= d; syy /= d; sxy /= d;
  const double tr = sxx + syy;
  const double det = sxx * syy - sxy * sxy;
  const double disc = std::sqrt(std::fmax(0.0, 0.25 * tr * tr - det));
  const double l1 = 0.5 * tr + disc;
  const double l2 = 0.5 * tr - disc;
  e.cx = mx; e.cy = my;
  e.semi_major = k_sigma * std::sqrt(std::fmax(0.0, l1));
  e.semi_minor = k_sigma * std::sqrt(std::fmax(0.0, l2));
  e.angle_rad  = 0.5 * std::atan2(2.0 * sxy, sxx - syy);
  return e;
}

} // namespace fen::nav
