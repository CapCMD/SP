#include "fen/nav/OrbitDetermination.hpp"
#include "fen/nav/Gates.hpp"
#include "fen/core/Epoch.hpp"
#include <algorithm>
#include <cmath>

namespace fen::nav {
using namespace fen::cst;

// ============================ STATIONS =====================================
const std::vector<Station>& dsn_complexes() {
  static const std::vector<Station> v = {
      {"DSS-14 Goldstone",  35.4267, -116.8900, 1036.0, 2.0, 1e-4, 10.0, 0.15},
      {"DSS-63 Madrid",     40.4314,   -4.2480,  865.0, 2.0, 1e-4, 10.0, 0.15},
      {"DSS-43 Canberra",  -35.4014,  148.9819,  689.0, 2.0, 1e-4, 10.0, 0.15},
  };
  return v;
}

namespace {
// GMST linéaire (Meeus, terme du 1er ordre). Erreur ~ quelques dizaines de m sur
// la position de station à l'horizon d'un an. DÉCLARÉE. V2 : IAU 2006/2000A.
double gmst_rad(double t_tdb) {
  const double d = t_tdb / DAY;                       // jours depuis J2000
  double th = 280.46061837 + 360.98564736629 * d;     // degrés
  th = std::fmod(th, 360.0);
  if (th < 0) th += 360.0;
  return th * DEG;
}

// Géodésique -> ECEF, ellipsoïde WGS84. Une Terre sphérique déplacerait la
// station de 21 km : rigoureusement absurde quand on mesure la distance à 2 m.
Vec3 geodetic_to_ecef(double lat_deg, double lon_deg, double h) {
  constexpr double a = 6378137.0;
  constexpr double f = 1.0 / 298.257223563;
  constexpr double e2 = f * (2.0 - f);
  const double phi = lat_deg * DEG, lam = lon_deg * DEG;
  const double sphi = std::sin(phi), cphi = std::cos(phi);
  const double N = a / std::sqrt(1.0 - e2 * sphi * sphi);
  return Vec3{(N + h) * cphi * std::cos(lam),
              (N + h) * cphi * std::sin(lam),
              (N * (1.0 - e2) + h) * sphi};
}
} // namespace

Vec3 station_position_eci(const Station& s, double t) {
  const Vec3 ecef = geodetic_to_ecef(s.lat_deg, s.lon_deg, s.alt_m);
  const double th = gmst_rad(t);
  const double c = std::cos(th), si = std::sin(th);
  return Vec3{c * ecef.x - si * ecef.y, si * ecef.x + c * ecef.y, ecef.z};
}

Vec3 station_velocity_eci(const Station& s, double t) {
  const Vec3 r = station_position_eci(s, t);
  return cross(Vec3{0, 0, OMEGA_EARTH}, r);   // v = omega x r
}

bool station_visible(const Station& s, double t, const Vec3& r_sc) {
  const Vec3 r_st = station_position_eci(s, t);
  const Vec3 up = unit(r_st);                        // ~ zénith local (sphérique : suffisant ici)
  const Vec3 los = r_sc - r_st;
  const double sin_el = dot(unit(los), up);
  return sin_el > std::sin(s.elevation_mask_deg * DEG);
}

MeasPredict predict(const Station& s, double t, const Vec3& r, const Vec3& v) {
  MeasPredict m;
  const Vec3 r_st = station_position_eci(s, t);
  const Vec3 v_st = station_velocity_eci(s, t);
  const Vec3 d = r - r_st;
  const Vec3 dv = v - v_st;
  const double rho = norm(d);
  const Vec3 u = d / rho;
  const double rhodot = dot(d, dv) / rho;
  m.range = rho;
  m.range_rate = rhodot;
  // d(rho)/dr = u ; d(rho)/dv = 0
  m.H[0][0] = u.x; m.H[0][1] = u.y; m.H[0][2] = u.z;
  m.H[0][3] = 0;   m.H[0][4] = 0;   m.H[0][5] = 0;
  // d(rhodot)/dr = (dv - rhodot*u)/rho ; d(rhodot)/dv = u
  const Vec3 g = (dv - u * rhodot) / rho;
  m.H[1][0] = g.x; m.H[1][1] = g.y; m.H[1][2] = g.z;
  m.H[1][3] = u.x; m.H[1][4] = u.y; m.H[1][5] = u.z;
  return m;
}

// ============================ STM ==========================================
namespace {
// Echelle relative optimale, mesuree (cf. commentaire de l'en-tete).
void auto_steps(const StateN& y0, double& h_r, double& h_v) {
  constexpr double REL = 5.0e-7;
  if (h_r <= 0.0) h_r = std::fmax(REL * norm(pos(y0)), 1.0);
  if (h_v <= 0.0) h_v = std::fmax(REL * norm(vel(y0)), 1.0e-4);
}
} // namespace

Mat6 stm(const force::ForceStack& forces, double t0, const StateN& y0, double t1,
         const prop::PropOptions& opt_in, double h_r, double h_v) {
  auto_steps(y0, h_r, h_v);
  Mat6 Phi;
  prop::PropOptions o = opt_in;
  o.sample_dt = 0.0;
  o.sample_times.clear();
  o.breakpoints.clear();

  for (int j = 0; j < 6; ++j) {
    const double h = (j < 3) ? h_r : h_v;
    StateN yp = y0, ym = y0;
    yp[j] += h;
    ym[j] -= h;
    const auto rp = prop::propagate(forces, t0, yp, t1, {}, o);
    const auto rm = prop::propagate(forces, t0, ym, t1, {}, o);
    for (int i = 0; i < 6; ++i)
      Phi.m[i][j] = (rp.y_final[i] - rm.y_final[i]) / (2.0 * h);
  }
  return Phi;
}

// ====================== MOINDRES CARRÉS PAR LOTS ============================
OdResult batch_least_squares(const force::ForceStack& forces,
                             const std::vector<Measurement>& meas,
                             const std::vector<Station>& stations,
                             double t_ref, const StateN& x0_apriori,
                             const prop::PropOptions& opt_in, int max_iter) {
  OdResult out;
  out.t_ref = t_ref;
  out.mass = x0_apriori[6];
  out.n_measurements = static_cast<int>(meas.size());
  out.x_hat = Vec6::from(pos(x0_apriori), vel(x0_apriori));
  out.P = Mat6::identity();
  if (meas.empty()) { out.observable = false; return out; }

  StateN x0 = x0_apriori;

  prop::PropOptions o = opt_in;
  o.sample_dt = 0.0;
  o.breakpoints.clear();
  o.sample_times.clear();
  for (const auto& m : meas) o.sample_times.push_back(m.t);
  std::sort(o.sample_times.begin(), o.sample_times.end());
  const double t_end = o.sample_times.back();

  for (int it = 0; it < max_iter; ++it) {
    // --- trajectoire nominale, échantillonnée AUX instants de mesure ---
    const auto nom = prop::propagate(forces, t_ref, x0, t_end, {}, o);
    if (!nom.ok) { out.observable = false; return out; }

    auto state_at = [&](double t) -> StateN {
      for (const auto& s : nom.samples)
        if (std::fabs(s.t - t) < 1e-6) return s.y;
      return nom.y_final;
    };

    // --- STM cumulée : Phi(t_i, t_ref), une seule série de 12 propagations ---
    // (perturbées, échantillonnées aux mêmes instants)
    std::vector<std::vector<StateN>> pert(12);
    double hr = -1.0, hv = -1.0;
    auto_steps(x0, hr, hv);
    for (int j = 0; j < 6; ++j) {
      const double h = (j < 3) ? hr : hv;
      for (int sgn = 0; sgn < 2; ++sgn) {
        StateN y = x0;
        y[j] += (sgn == 0 ? +h : -h);
        const auto r = prop::propagate(forces, t_ref, y, t_end, {}, o);
        pert[2 * j + sgn] = std::vector<StateN>();
        pert[2 * j + sgn].reserve(nom.samples.size());
        for (const auto& s : r.samples) pert[2 * j + sgn].push_back(s.y);
      }
    }
    auto phi_at = [&](std::size_t idx) {
      Mat6 P;
      for (int j = 0; j < 6; ++j) {
        const double h = (j < 3) ? hr : hv;
        for (int i = 0; i < 6; ++i)
          P.m[i][j] = (pert[2 * j][idx][i] - pert[2 * j + 1][idx][i]) / (2.0 * h);
      }
      return P;
    };
    auto index_of = [&](double t) -> std::size_t {
      for (std::size_t i = 0; i < nom.samples.size(); ++i)
        if (std::fabs(nom.samples[i].t - t) < 1e-6) return i;
      return nom.samples.size() ? nom.samples.size() - 1 : 0;
    };

    // --- équations normales ---
    Mat6 HtWH = Mat6::zero();
    Vec6 HtWy{};
    double chi2 = 0.0;
    int n_used = 0;

    for (const auto& m : meas) {
      const std::size_t idx = index_of(m.t);
      const StateN y = state_at(m.t);
      const auto pr = predict(stations[m.station], m.t, pos(y), vel(y));
      const Mat6 Phi = phi_at(idx);

      const double dz[2] = {m.range - pr.range, m.range_rate - pr.range_rate};
      const double w[2]  = {1.0 / (m.sigma_range * m.sigma_range),
                            1.0 / (m.sigma_rangerate * m.sigma_rangerate)};

      for (int k = 0; k < 2; ++k) {
        // H_tilde * Phi  ->  partielles par rapport à x(t_ref)
        double Hk[6];
        for (int j = 0; j < 6; ++j) {
          double s = 0.0;
          for (int a = 0; a < 6; ++a) s += pr.H[k][a] * Phi.m[a][j];
          Hk[j] = s;
        }
        for (int i = 0; i < 6; ++i) {
          HtWy[i] += Hk[i] * w[k] * dz[k];
          for (int j = 0; j < 6; ++j) HtWH.m[i][j] += Hk[i] * w[k] * Hk[j];
        }
        chi2 += dz[k] * dz[k] * w[k];
        ++n_used;
      }
    }

    Mat6 Pcov;
    if (!invert6(HtWH, Pcov)) {
      // La matrice normale est SINGULIÈRE : avec les mesures achetées, l'orbite
      // n'est pas observable. Ce n'est pas un bug. C'est la réponse.
      out.observable = false;
      out.rms_residual = std::sqrt(chi2 / std::max(1, n_used));
      return out;
    }

    const Vec6 dx = Pcov * HtWy;
    for (int i = 0; i < 6; ++i) x0[i] += dx[i];

    out.P = Pcov;
    out.x_hat = Vec6::from(pos(x0), vel(x0));
    out.iterations = it + 1;
    out.rms_residual = std::sqrt(chi2 / std::max(1, n_used));

    const double step = std::sqrt(dx[0]*dx[0] + dx[1]*dx[1] + dx[2]*dx[2]);
    if (step < 1e-3) { out.converged = true; break; }   // 1 mm
  }
  return out;
}

StateEstimate propagate_estimate(const force::ForceStack& forces,
                                 const StateEstimate& e, double t1,
                                 const prop::PropOptions& opt_in) {
  StateEstimate out;
  out.t = t1;
  out.mass = e.mass;
  prop::PropOptions o = opt_in;
  o.sample_dt = 0.0; o.sample_times.clear(); o.breakpoints.clear();

  StateN y{e.x[0], e.x[1], e.x[2], e.x[3], e.x[4], e.x[5], e.mass};
  const auto r = prop::propagate(forces, e.t, y, t1, {}, o);
  out.x = Vec6::from(pos(r.y_final), vel(r.y_final));

  const Mat6 Phi = stm(forces, e.t, y, t1, o);
  out.P = Phi * e.P * Phi.transpose();
  return out;
}

Mat6 gates_covariance(const Vec3& dv, const GatesParams& g) {
  Mat6 Q = Mat6::zero();
  const double dvn = norm(dv);
  if (dvn <= 0.0) return Q;
  const Vec3 u = dv / dvn;
  const double s_mag = std::sqrt(g.sigma_mag_fixed * g.sigma_mag_fixed
                                 + (g.sigma_mag_prop * dvn) * (g.sigma_mag_prop * dvn));
  const double s_perp = std::sqrt(g.sigma_point_fixed * g.sigma_point_fixed
                                  + (g.sigma_point_prop * dvn) * (g.sigma_point_prop * dvn));
  Vec3 a = (std::fabs(u.z) < 0.9) ? Vec3{0, 0, 1} : Vec3{1, 0, 0};
  const Vec3 e1 = unit(cross(u, a));
  const Vec3 e2 = cross(u, e1);
  // Q_v = s_mag^2 u u^T + s_perp^2 (e1 e1^T + e2 e2^T)
  const Vec3 basis[3] = {u, e1, e2};
  const double var[3] = {s_mag * s_mag, s_perp * s_perp, s_perp * s_perp};
  for (int k = 0; k < 3; ++k)
    for (int i = 0; i < 3; ++i)
      for (int j = 0; j < 3; ++j)
        Q.m[3 + i][3 + j] += var[k] * basis[k][i] * basis[k][j];
  return Q;
}

} // namespace fen::nav
