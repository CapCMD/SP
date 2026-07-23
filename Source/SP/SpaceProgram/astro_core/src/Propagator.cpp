#include "fen/prop/Propagator.hpp"
#include "fen/prop/Ias15.hpp"
#include "fen/core/Constants.hpp"
#include <algorithm>
#include <cmath>

namespace fen::prop {

EventSpec event_periapsis(double) {
  // r·v = 0 avec passage de négatif à positif (on remonte après le périastre)
  return EventSpec{"PERIAPSIS",
                   [](double, const StateN& y) { return dot(pos(y), vel(y)); },
                   +1, false};
}
EventSpec event_apoapsis(double) {
  return EventSpec{"APOAPSIS",
                   [](double, const StateN& y) { return dot(pos(y), vel(y)); },
                   -1, false};
}
EventSpec event_altitude(double r_body, double alt, int direction, bool terminal) {
  const double rt = r_body + alt;
  return EventSpec{"ALT_" + std::to_string(static_cast<long long>(alt)),
                   [rt](double, const StateN& y) { return norm(pos(y)) - rt; },
                   direction, terminal};
}
EventSpec event_impact(double r_body) {
  return EventSpec{"IMPACT",
                   [r_body](double, const StateN& y) { return norm(pos(y)) - r_body; },
                   -1, true};
}
EventSpec event_soi_crossing(const ephem::IEphemeris* eph, ephem::Body body,
                             ephem::Body center, double r_soi) {
  return EventSpec{std::string("SOI_") + ephem::body_name(body),
                   [eph, body, center, r_soi](double t, const StateN& y) {
                     const Vec3 rb = eph->state(body, center, Epoch{t}).r;
                     return norm(pos(y) - rb) - r_soi;
                   },
                   0, false};
}
EventSpec event_propellant_exhausted(double dry_mass) {
  return EventSpec{"PROP_EXHAUSTED",
                   [dry_mass](double, const StateN& y) { return mass(y) - dry_mass; },
                   -1, false};
}

namespace {

// Racine sur l'interpolant dense : bissection + sécante (robuste et sans dérivée).
double find_root(const DenseSegment& seg, const EventSpec& ev, double ta, double tb, double tol) {
  double ga = ev.g(ta, seg.eval(ta));
  double gb = ev.g(tb, seg.eval(tb));
  for (int i = 0; i < 100; ++i) {
    if (tb - ta < tol) break;
    // sécante, avec repli sur la bissection si l'itéré sort de [ta,tb]
    double tm = (std::fabs(gb - ga) > 0.0) ? ta - ga * (tb - ta) / (gb - ga)
                                           : 0.5 * (ta + tb);
    if (!(tm > ta && tm < tb)) tm = 0.5 * (ta + tb);
    const double gm = ev.g(tm, seg.eval(tm));
    if (gm == 0.0) return tm;
    if ((ga < 0.0) != (gm < 0.0)) { tb = tm; gb = gm; }
    else                          { ta = tm; ga = gm; }
  }
  return 0.5 * (ta + tb);
}

} // namespace

PropResult propagate(const force::ForceStack& forces, double t0, const StateN& y0,
                     double t_end, const std::vector<EventSpec>& events,
                     const PropOptions& opt) {
  PropResult res;
  res.t_final = t0;
  res.y_final = y0;

  const Deriv f = [&forces](double t, const StateN& y, StateN& dy) {
    Vec3 a; double mdot;
    forces.derivative(t, pos(y), vel(y), mass(y), a, mdot);
    dy[0] = y[3]; dy[1] = y[4]; dy[2] = y[5];
    dy[3] = a.x;  dy[4] = a.y;  dy[5] = a.z;
    dy[6] = mdot;
  };

  // --- sens d'intégration (le recul est un mode de fonctionnement, pas un hack) ---
  const double dir = (t_end >= t0) ? 1.0 : -1.0;

  // --- grille de points de rupture, ordonnée DANS LE SENS DE MARCHE ---
  std::vector<double> bps;
  for (double b : opt.breakpoints)
    if (dir * (b - t0) > 1e-9 && dir * (t_end - b) > 1e-9) bps.push_back(b);
  std::sort(bps.begin(), bps.end(),
            [dir](double a, double b) { return dir * a < dir * b; });
  bps.erase(std::unique(bps.begin(), bps.end()), bps.end());
  bps.push_back(t_end);

  // LE CHOIX DU SCHEMA NE CHANGE RIEN AU RESTE. C'etait la promesse faite quand
  // DOPRI5 etait seul ; c'est ici qu'on la tient ou qu'on la casse.
  Dopri5 integ5(opt.step);
  Ias15  integ15(opt.step);
  const bool use15 = (opt.step.scheme == Scheme::Ias15);
  auto do_step = [&](double& tt, StateN& yy, double& hh, DenseSegment& sg) {
    return use15 ? integ15.step(f, tt, yy, hh, sg) : integ5.step(f, tt, yy, hh, sg);
  };
  double t = t0;
  StateN y = y0;
  double h = dir * opt.step.h_init;

  std::vector<double> g_prev(events.size());
  for (std::size_t k = 0; k < events.size(); ++k) g_prev[k] = events[k].g(t, y);

  double next_sample = (opt.sample_dt > 0.0) ? t0 : 1e300;
  if (opt.sample_dt > 0.0) res.samples.push_back({t0, y0});

  // instants de mesure explicites, triés dans le sens de marche
  std::vector<double> st;
  for (double s : opt.sample_times)
    if (dir * (s - t0) >= -1e-9 && dir * (t_end - s) >= -1e-9) st.push_back(s);
  std::sort(st.begin(), st.end(), [dir](double a, double b) { return dir * a < dir * b; });
  std::size_t i_st = 0;

  long long guard = 0;
  for (double seg_end : bps) {
    // le pas ne franchit JAMAIS un point de rupture (allumage / extinction)
    while (dir * (seg_end - t) > 1e-12) {
      if (++guard > opt.step.max_steps) {
        res.ok = false; res.termination_reason = "max_steps"; goto done;
      }
      double h_try = dir * std::min(std::fabs(h), std::fabs(seg_end - t));
      DenseSegment seg;
      const double t_before = t;
      if (!do_step(t, y, h_try, seg)) {
        res.ok = false; res.termination_reason = "step_failed (h_min atteint)"; goto done;
      }
      h = h_try;

      // --- échantillonnage sur l'interpolant (indépendant du pas) ---
      if (opt.sample_dt > 0.0 && dir > 0.0) {
        while (next_sample <= t + 1e-12) {
          if (next_sample >= t_before) res.samples.push_back({next_sample, seg.eval(next_sample)});
          next_sample += opt.sample_dt;
        }
      }
      // --- instants explicites (mesures) : lus sur l'interpolant dense ---
      while (i_st < st.size() && dir * (t - st[i_st]) >= -1e-9) {
        res.samples.push_back({st[i_st], seg.eval(st[i_st])});
        ++i_st;
      }

      // --- événements : RACINE sur l'interpolant dense, jamais un test de pas ---
      for (std::size_t k = 0; k < events.size(); ++k) {
        const double g_now = events[k].g(t, y);
        const bool sign_change = (g_prev[k] < 0.0) != (g_now < 0.0);
        if (sign_change) {
          // pente PAR RAPPORT AU TEMPS (indépendante du sens d'intégration)
          const int slope = (dir * (g_now - g_prev[k]) > 0.0) ? +1 : -1;
          if (events[k].direction == 0 || events[k].direction == slope) {
            const double ta = std::min(t_before, t);
            const double tb = std::max(t_before, t);
            const double te = find_root(seg, events[k], ta, tb, opt.event_tol);
            EventHit hit{events[k].name, te, seg.eval(te)};
            res.events.push_back(hit);
            if (events[k].terminal) {
              t = te; y = hit.y;
              res.terminated_by_event = true;
              res.termination_reason = events[k].name;
              goto done;
            }
          }
        }
        g_prev[k] = g_now;
      }
    }
  }

done:
  res.t_final = t;
  res.y_final = y;
  res.steps_accepted = use15 ? integ15.n_accepted() : integ5.n_accepted();
  res.steps_rejected = use15 ? integ15.n_rejected() : integ5.n_rejected();
  return res;
}

} // namespace fen::prop
