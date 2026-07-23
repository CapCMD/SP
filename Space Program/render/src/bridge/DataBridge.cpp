// spr/bridge/DataBridge.cpp
#include "spr/bridge/DataBridge.hpp"

#include <cmath>
#include <cstring>

#include "fen/astro/Elements.hpp"

namespace spr {

using fen::ephem::Body;

namespace {

void put_name(char* dst, const char* src) {
  std::strncpy(dst, src, 31);
  dst[31] = '\0';
}

// Couleur d'affichage (albedo approx.) par corps. Aucune constante de gameplay :
// pure presentation visuelle.
Vec3 body_color(Body b) {
  switch (b) {
    case Body::Sun:       return {1.00f, 0.92f, 0.55f};
    case Body::Mercury:   return {0.60f, 0.57f, 0.53f};
    case Body::Venus:     return {0.90f, 0.80f, 0.55f};
    case Body::EarthBary: return {0.25f, 0.50f, 0.85f};
    case Body::Moon:      return {0.70f, 0.70f, 0.72f};
    case Body::Mars:      return {0.80f, 0.40f, 0.25f};
    case Body::Jupiter:   return {0.80f, 0.68f, 0.55f};
    case Body::Saturn:    return {0.85f, 0.78f, 0.60f};
    case Body::Titan:     return {0.75f, 0.55f, 0.30f};
    default:              return {0.7f, 0.7f, 0.7f};
  }
}

// Tag de PRESENTATION (pas de physique) : quel template de materiau le rendu
// doit employer. Meme nature que body_color(). Aucune constante de gameplay.
SurfaceType body_surface(Body b) {
  switch (b) {
    case Body::Sun:       return SurfaceType::Star;
    case Body::Mercury:   return SurfaceType::Rocky;
    case Body::Venus:     return SurfaceType::Desert;   // couverture claire, tinte
    case Body::EarthBary: return SurfaceType::EarthLike;
    case Body::Moon:      return SurfaceType::Rocky;
    case Body::Mars:      return SurfaceType::Desert;
    case Body::Jupiter:   return SurfaceType::GasGiant;
    case Body::Saturn:    return SurfaceType::GasGiant;
    case Body::Titan:     return SurfaceType::Icy;
    default:              return SurfaceType::Rocky;
  }
}

Dvec3 to_d(const fen::Vec3& v) { return Dvec3{v.x, v.y, v.z}; }

} // namespace

DataBridge::DataBridge(const fen::ephem::IEphemeris& eph, WorldConfig cfg)
    : eph_(eph), cfg_(std::move(cfg)) {}

void DataBridge::fill_bodies(RenderSnapshot& s, fen::Epoch t) const {
  int n = 0;
  // Le corps central est a l'origine du repere monde.
  if (n < RenderSnapshot::MAX_BODIES) {
    BodyView& bv = s.bodies[n++];
    bv.id = static_cast<int>(cfg_.central);
    put_name(bv.name, fen::ephem::body_name(cfg_.central));
    bv.position = Dvec3{0, 0, 0};
    bv.radius = fen::ephem::body_radius(cfg_.central);
    bv.color = body_color(cfg_.central);
    bv.is_star = (cfg_.central == Body::Sun);
    bv.surface = body_surface(cfg_.central);
  }
  // Les autres corps, positionnes par l'ephemeride RELATIVEMENT au centre.
  for (Body b : cfg_.bodies) {
    if (b == cfg_.central || n >= RenderSnapshot::MAX_BODIES) continue;
    fen::ephem::PosVel pv = eph_.state(b, cfg_.central, t);
    BodyView& bv = s.bodies[n++];
    bv.id = static_cast<int>(b);
    put_name(bv.name, fen::ephem::body_name(b));
    bv.position = to_d(pv.r);
    bv.radius = fen::ephem::body_radius(b);
    bv.color = body_color(b);
    bv.is_star = (b == Body::Sun);
    bv.surface = body_surface(b);
  }
  s.body_count = n;
}

// Echantillonne l'orbite osculatrice a l'epoque, EN APPELANT astro_core :
// rv_to_elements puis elements_to_rv sur nu variable. Le rendu ne connait pas
// Kepler ; il recoit une polyligne deja calculee par la verite.
void DataBridge::fill_orbit(OrbitView& o, bool& valid, const fen::State& st, double mu) const {
  valid = false;
  if (mu <= 0.0) return;
  const double r = fen::norm(st.r), v = fen::norm(st.v);
  if (!(r > 0.0) || !(v > 0.0)) return;

  fen::astro::Elements el = fen::astro::rv_to_elements(st.r, st.v, mu);
  if (!std::isfinite(el.e) || !std::isfinite(el.p) || el.p <= 0.0) return;

  const bool ellipse = (el.e < 1.0 - 1e-9);
  o.closed = ellipse;
  const int N = ORBIT_SAMPLES;
  o.count = N;

  double nu0, nu1;
  if (ellipse) {
    nu0 = 0.0;
    nu1 = 2.0 * 3.14159265358979323846;
  } else {
    // hyperbole : borner l'anomalie vraie a l'asymptote (acos(-1/e)).
    const double nu_inf = std::acos(-1.0 / el.e);
    nu0 = -0.95 * nu_inf;
    nu1 = 0.95 * nu_inf;
  }

  for (int i = 0; i < N; ++i) {
    const double f = (N > 1) ? static_cast<double>(i) / (N - 1) : 0.0;
    fen::astro::Elements s = el;
    s.nu = nu0 + (nu1 - nu0) * f;
    fen::Vec3 rr, vv;
    fen::astro::elements_to_rv(s, mu, rr, vv);
    o.points[i] = to_d(rr);
  }
  valid = true;
}

void DataBridge::fill_telemetry(Telemetry& tm, const fen::State& st, double mu, double R) const {
  const double r = fen::norm(st.r), v = fen::norm(st.v);
  tm.radius = r;
  tm.speed = v;
  tm.altitude = r - R;
  fen::astro::Elements el = fen::astro::rv_to_elements(st.r, st.v, mu);
  tm.sma = el.a;
  tm.ecc = el.e;
  tm.inc_deg = el.i * (180.0 / 3.14159265358979323846);
  tm.period_s = fen::astro::orbital_period(el.a, mu);  // NaN si non elliptique
  tm.apoapsis = el.ra;
  tm.periapsis = el.rp;
  tm.valid = true;
}

RenderSnapshot DataBridge::freeze(fen::Epoch t, const fen::State& vehicle) const {
  RenderSnapshot s = freeze(t);  // systeme + meta
  VehicleView& vv = s.vehicle;
  vv.valid = true;
  vv.position = to_d(vehicle.r);
  vv.velocity = to_d(vehicle.v);
  vv.mass = vehicle.m;
  fill_orbit(s.orbit, s.orbit_valid, vehicle, s.central_mu);
  fill_telemetry(s.telemetry, vehicle, s.central_mu, s.central_radius);
  return s;
}

RenderSnapshot DataBridge::freeze(fen::Epoch t) const {
  RenderSnapshot s;
  s.epoch_tdb = t.tdb;
  put_name(s.epoch_iso, fen::epoch_to_iso(t).c_str());
  s.central_body = static_cast<int>(cfg_.central);
  put_name(s.central_name, fen::ephem::body_name(cfg_.central));
  s.central_mu = fen::ephem::body_mu(cfg_.central);
  s.central_radius = fen::ephem::body_radius(cfg_.central);
  fill_bodies(s, t);
  return s;
}

} // namespace spr
