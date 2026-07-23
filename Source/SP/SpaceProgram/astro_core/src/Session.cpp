#include "fen/flight/Session.hpp"
#include "fen/astro/Transfers.hpp"
#include "fen/astro/Elements.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>

namespace fen::flight {
using namespace fen::force;

namespace {
ForceStack build_gravity(const FlightPlan& plan, const ephem::IEphemeris& eph) {
  ForceStack fs;
  fs.add(std::make_shared<CentralGravity>(ephem::body_mu(plan.center)));
  for (ephem::Body b : plan.perturbers)
    fs.add(std::make_shared<ThirdBodyGravity>(&eph, b, plan.center));
  return fs;
}
} // namespace

Session::Session(FlightPlan plan, const ephem::IEphemeris& eph,
                 std::uint64_t seed, prop::PropOptions opt)
    : plan_(std::move(plan)), eph_(eph), seed_(seed), opt_(std::move(opt)) {
  gravity_ = build_gravity(plan_, eph_);
  const double mu = ephem::body_mu(plan_.center);
  const double Rb = ephem::body_radius(plan_.center);
  coast_events_ = {prop::event_periapsis(mu), prop::event_apoapsis(mu)};
  if (Rb > 0.0) coast_events_.push_back(prop::event_impact(Rb));
  for (const auto& e : plan_.extra_events) coast_events_.push_back(e);
  t_ = plan_.epoch0;
  y_ = plan_.initial.pack();
  rep_.truth.samples.push_back({t_, y_});

  stations_ = nav::dsn_complexes();
  seed_gates_ = seed_;
  seed_meas_  = seed_;

  // Etat initial CONNU : le lanceur a livre l'orbite de parking et elle a ete
  // determinee au sol avant la separation. Covariance a priori petite mais NON
  // NULLE (100 m, 0.1 m/s) : personne ne connait sa position au millimetre.
  est_.t = t_;
  est_.mass = mass(y_);
  est_.x = Vec6::from(pos(y_), vel(y_));
  est_.P = Mat6::zero();
  for (int i = 0; i < 3; ++i) est_.P.m[i][i] = 100.0 * 100.0;
  for (int i = 3; i < 6; ++i) est_.P.m[i][i] = 0.1 * 0.1;
}

void Session::schedule_pass(const nav::Pass& p) {
  passes_.push_back(p);
  const auto& st = stations_[p.station];
  tracking_cost_ += st.cost_musd_per_hour * (p.t_end - p.t_start) / 3600.0;
}

// Genere les mesures des passes reservees dont la fenetre tombe dans (t_from, t_to].
// Elles sont tirees de la VERITE, bruitees, sur un sous-flux DEDIE et STABLE :
// ajouter une passe ne doit pas decaler les tirages des autres.
void Session::collect_measurements(double t_from, double t_to) {
  if (passes_.empty()) return;

  // Instants de mesure candidats dans l'arc.
  struct Cand { double t; int station; };
  std::vector<Cand> cand;
  for (const auto& p : passes_)
    for (double tm = p.t_start; tm <= p.t_end + 1e-9; tm += p.sample_dt)
      if (tm > t_from + 1e-9 && tm <= t_to + 1e-9) cand.push_back({tm, p.station});
  if (cand.empty()) return;
  std::sort(cand.begin(), cand.end(), [](const Cand& a, const Cand& b) { return a.t < b.t; });

  // UNE seule propagation, lue sur l'interpolant dense aux instants de mesure.
  prop::PropOptions o = opt_;
  o.sample_dt = 0.0; o.breakpoints.clear(); o.sample_times.clear();
  for (const auto& c : cand) o.sample_times.push_back(c.t);
  auto r = prop::propagate(gravity_, t_from, y_from_cache_, t_to, {}, o);

  Rng master(seed_meas_);
  for (const auto& c : cand) {
    const StateN* y = nullptr;
    for (const auto& sm : r.samples)
      if (std::fabs(sm.t - c.t) < 1e-6) { y = &sm.y; break; }
    if (!y) continue;
    const auto& st = stations_[c.station];
    const Vec3 rt = pos(*y), vt = vel(*y);
    if (!nav::station_visible(st, c.t, rt)) continue;      // <- la geometrie decide, pas le joueur
    const auto pr = nav::predict(st, c.t, rt, vt);
    nav::Measurement m;
    m.t = c.t; m.station = c.station;
    m.sigma_range = st.sigma_range;
    m.sigma_rangerate = st.sigma_rangerate;
    if (seed_meas_ == 0) { m.range = pr.range; m.range_rate = pr.range_rate; }
    else {
      // sous-flux DEDIE et STABLE : ajouter une passe ne decale pas les autres tirages
      const std::uint64_t key = 5000ull + static_cast<std::uint64_t>(std::llround(c.t)) * 8ull
                                + static_cast<std::uint64_t>(c.station);
      Rng rn = master.substream(key);
      m.range = pr.range + rn.normal(0.0, st.sigma_range);
      m.range_rate = pr.range_rate + rn.normal(0.0, st.sigma_rangerate);
    }
    meas_.push_back(m);
  }
}

void Session::sync_estimate_to(double t) {
  if (t > est_.t + 1e-9) est_ = nav::propagate_estimate(gravity_, est_, t, opt_);
}

double Session::dry_floor(std::size_t k) const {
  double m = plan_.vehicle.payload_dry;
  for (std::size_t i = k; i < plan_.vehicle.stages.size(); ++i)
    m += (i == k) ? plan_.vehicle.stages[i].dry_mass() : plan_.vehicle.stages[i].wet_mass();
  return m;
}

double Session::usable_propellant_remaining(std::size_t k) const {
  return std::max(0.0, mass(y_) - dry_floor(k));
}

double Session::dv_remaining(std::size_t k) const {
  const double m0 = mass(y_);
  const double mf = dry_floor(k);
  if (mf <= 0.0 || m0 <= mf) return 0.0;
  return plan_.vehicle.stages[k].engine.ve() * std::log(m0 / mf);
}

Observation Session::observe() {
  Observation obs;
  obs.t = t_;

  if (meas_.empty()) {
    // ---- NAVIGATION A L'ESTIME -------------------------------------------
    // Le joueur n'a acquis aucune mesure depuis sa derniere manoeuvre. Il croit
    // donc que sa manoeuvre est passee au NOMINAL. Il se trompe, et il ne le
    // sait pas. Sa covariance enfle, c'est tout ce que le jeu lui dit.
    sync_estimate_to(t_);
    obs.state = State{est_.x.pos(), est_.x.vel(), est_.mass};
    obs.covariance = est_.P;
    obs.sigma_pos = sigma_position(est_.P);
    obs.sigma_vel = sigma_velocity(est_.P);
    obs.n_measurements = 0;
    obs.source = "ESTIME (aucune poursuite achetee)";
    return obs;
  }

  // ---- DETERMINATION D'ORBITE -------------------------------------------
  const double t_ref = meas_.front().t;
  sync_estimate_to(t_ref);
  StateN apriori{est_.x[0], est_.x[1], est_.x[2], est_.x[3], est_.x[4], est_.x[5], est_.mass};

  prop::PropOptions o = opt_;
  o.step.rtol = 1e-10;                 // l'OD n'a pas besoin de 1e-12
  auto od = nav::batch_least_squares(gravity_, meas_, stations_, t_ref, apriori, o, 4);

  if (!od.observable) {
    // Matrice normale singuliere : les mesures achetees NE DETERMINENT PAS
    // l'orbite. Ce n'est pas un bug. C'est la reponse, et elle est facturee.
    sync_estimate_to(t_);
    obs.state = State{est_.x.pos(), est_.x.vel(), est_.mass};
    obs.covariance = est_.P;
    obs.sigma_pos = sigma_position(est_.P);
    obs.sigma_vel = sigma_velocity(est_.P);
    obs.n_measurements = od.n_measurements;
    obs.observable = false;
    obs.source = "NON OBSERVABLE (geometrie insuffisante)";
    return obs;
  }

  nav::StateEstimate e;
  e.t = t_ref; e.x = od.x_hat; e.P = od.P; e.mass = est_.mass;
  est_ = e;
  sync_estimate_to(t_);

  obs.state = State{est_.x.pos(), est_.x.vel(), est_.mass};
  obs.covariance = est_.P;
  obs.sigma_pos = sigma_position(est_.P);
  obs.sigma_vel = sigma_velocity(est_.P);
  obs.n_measurements = od.n_measurements;
  obs.rms_residual = od.rms_residual;
  obs.observable = true;
  obs.source = "OD (" + std::to_string(od.n_measurements) + " mesures, "
               + std::to_string(od.iterations) + " iterations)";
  return obs;
}

bool Session::advance_to(double t_target) {
  if (!rep_.ok) return false;
  if (t_target <= t_ + 1e-9) return true;
  const double t_from = t_;
  y_from_cache_ = y_;
  prop::PropOptions o = opt_;
  o.breakpoints.clear();
  auto r = prop::propagate(gravity_, t_, y_, t_target, coast_events_, o);
  rep_.truth.samples.insert(rep_.truth.samples.end(), r.samples.begin(), r.samples.end());
  rep_.truth.events.insert(rep_.truth.events.end(), r.events.begin(), r.events.end());
  rep_.truth.steps_accepted += r.steps_accepted;
  rep_.truth.steps_rejected += r.steps_rejected;
  t_ = r.t_final;
  y_ = r.y_final;
  rep_.truth.t_final = t_;
  rep_.truth.y_final = y_;
  if (r.terminated_by_event) {
    rep_.ok = false;
    rep_.failure = "TERMINE : " + r.termination_reason;
    return false;
  }
  collect_measurements(t_from, t_);
  return true;
}

double Session::advance_to_event(const std::string& name, double t_max) {
  return advance_to_any_event({name}, t_max);
}

double Session::advance_to_any_event(const std::vector<std::string>& names, double t_max) {
  // La session doit se RETROUVER A L'EVENEMENT, pas a t_max : sinon observe()
  // renverrait l'etat d'un autre instant, et le joueur calculerait sa manoeuvre
  // sur une donnee de navigation fausse. (C'est exactement le genre d'erreur que
  // le jeu est cense sanctionner ; le moteur n'a pas le droit de la commettre.)
  const std::size_t n_ev = rep_.truth.events.size();
  const std::size_t n_sa = rep_.truth.samples.size();
  if (!advance_to(t_max)) return std::numeric_limits<double>::quiet_NaN();

  for (std::size_t i = n_ev; i < rep_.truth.events.size(); ++i) {
    if (std::find(names.begin(), names.end(), rep_.truth.events[i].name) == names.end())
      continue;
    const double te = rep_.truth.events[i].t;
    const StateN ye = rep_.truth.events[i].y;   // etat issu de l'interpolant dense : exact

    // on jette tout ce qui a ete enregistre APRES l'evenement
    rep_.truth.events.resize(i + 1);
    while (rep_.truth.samples.size() > n_sa && rep_.truth.samples.back().t > te)
      rep_.truth.samples.pop_back();

    t_ = te;
    y_ = ye;
    rep_.truth.t_final = t_;
    rep_.truth.y_final = y_;
    return te;
  }
  return std::numeric_limits<double>::quiet_NaN();
}

BurnReport Session::commit_burn(const BurnCmd& b) {
  BurnReport br;
  br.id = b.id;
  if (!rep_.ok || b.stage >= plan_.vehicle.stages.size()) return br;

  const vehicle::Stage& st = plan_.vehicle.stages[b.stage];
  const double mdot = st.engine.mdot();
  const double ve   = st.engine.ve();
  const double mfloor = dry_floor(b.stage);

  if (!advance_to(b.t)) return br;
  const double m_center = mass(y_);

  // --- impulsion commandee -> consigne inertielle ---
  Vec3 dv_cmd_in = b.dv;
  if (b.frame == DvFrame::RSW)
    dv_cmd_in = rsw_to_inertial(rsw_basis(pos(y_), vel(y_)), b.dv);

  Vec3 dv_exec = dv_cmd_in;
  if (seed_gates_ != 0 && gates_enabled_) {
    Rng master(seed_gates_);
    Rng sub = master.substream(1000 + burn_index_);  // sous-flux DEDIE a cette manoeuvre
    dv_exec = nav::apply_gates(dv_cmd_in, plan_.gates, sub);
  }
  ++burn_index_;
  const double dv_mag = norm(dv_exec);

  // --- duree de l'arc (Tsiolkovski) ---
  const double mp_needed = m_center * (1.0 - std::exp(-dv_mag / ve));
  const double mp_avail  = m_center - mfloor;
  const bool   shortfall = (mp_needed > mp_avail);
  const double mp_burn   = shortfall ? std::max(0.0, mp_avail) : mp_needed;
  const double t_burn    = mp_burn / mdot;
  const double t_ign = b.t - 0.5 * t_burn;
  const double t_cut = b.t + 0.5 * t_burn;

  // --- recul EXACT jusqu'a l'allumage (arc centre) ---
  prop::PropOptions bo = opt_; bo.sample_dt = 0.0; bo.breakpoints.clear();
  StateN y_ign = y_;
  if (t_burn > 1e-12) {
    auto rback = prop::propagate(gravity_, t_, y_, t_ign, {}, bo);
    if (!rback.ok) { rep_.ok = false; rep_.failure = "recul impossible"; return br; }
    y_ign = rback.y_final;
  }
  const Vec3 v_ign = vel(y_ign);
  const double m_ign = mass(y_ign);

  Vec3 hold_dir = unit(dv_exec);
  if (b.hold == ThrustFrame::RswFixed)
    hold_dir = inertial_to_rsw(rsw_basis(pos(y_ign), v_ign), unit(dv_exec));

  // --- integration de l'arc (VERITE) ---
  ForceStack ts = build_gravity(plan_, eph_);
  ts.add(std::make_shared<FiniteThrust>(t_ign, t_cut, st.engine.thrust_vac,
                                        st.engine.isp_vac, hold_dir, b.hold, mfloor));
  prop::PropOptions to = opt_;
  to.breakpoints = {t_ign, t_cut};
  std::vector<prop::EventSpec> be;
  if (ephem::body_radius(plan_.center) > 0.0)
    be.push_back(prop::event_impact(ephem::body_radius(plan_.center)));

  auto rb = prop::propagate(ts, t_ign, y_ign, t_cut, be, to);
  rep_.truth.samples.insert(rep_.truth.samples.end(), rb.samples.begin(), rb.samples.end());
  rep_.truth.steps_accepted += rb.steps_accepted;
  t_ = rb.t_final;
  y_ = rb.y_final;

  // --- contrefactuel gravitationnel : isoler l'effet du moteur ---
  prop::PropOptions co = opt_; co.sample_dt = 0.0; co.breakpoints.clear();
  auto rc = prop::propagate(gravity_, t_ign, y_ign, t_cut, {}, co);
  const Vec3 v_grav = vel(rc.y_final);

  const double m_cut = mass(y_);
  const double dv_propulsif = (m_cut > 0.0) ? ve * std::log(m_ign / m_cut) : 0.0;
  const Vec3   dv_utile_vec = vel(y_) - v_grav;

  br.t_ignition = t_ign;
  br.t_cutoff = t_cut;
  br.duration = t_burn;
  br.dv_commanded = b.dv;
  br.dv_commanded_inertial = dv_cmd_in;
  br.dv_perturbed = dv_exec;
  br.dv_cmd_mag = norm(dv_cmd_in);
  br.dv_achieved = dv_utile_vec;
  br.dv_achieved_mag = norm(dv_utile_vec);
  br.finite_burn_loss = dv_propulsif - br.dv_achieved_mag;
  br.mass_before = m_ign;
  br.mass_after = m_cut;
  br.propellant_used = m_ign - m_cut;
  br.engine_cutoff_early = shortfall;

  rep_.burns.push_back(br);
  rep_.total_propellant += br.propellant_used;
  rep_.total_dv_commanded += br.dv_cmd_mag;
  rep_.total_dv_achieved += dv_propulsif;
  rep_.truth.t_final = t_;
  rep_.truth.y_final = y_;

  // ---- MISE A JOUR DE L'ESTIME DU JOUEUR --------------------------------
  // Il applique le Delta-v qu'il a COMMANDE (il ne connait pas celui qui a eu
  // lieu), et il AJOUTE la covariance d'execution de Gates. Sans poursuite
  // ulterieure, cette incertitude reste — et se propage.
  sync_estimate_to(b.t);
  est_.x[3] += dv_cmd_in.x;
  est_.x[4] += dv_cmd_in.y;
  est_.x[5] += dv_cmd_in.z;
  est_.P = est_.P + nav::gates_covariance(dv_cmd_in, plan_.gates);
  est_.mass = m_cut;                    // la masse, elle, est TELEMESUREE : exacte
  est_.t = b.t;
  sync_estimate_to(t_);
  meas_.clear();                        // nouvel arc de determination d'orbite

  if (rb.terminated_by_event) {
    rep_.ok = false;
    rep_.failure = "TERMINE PENDANT LA POUSSEE : " + rb.termination_reason;
  } else if (shortfall) {
    char buf[224];
    std::snprintf(buf, sizeof(buf),
                  "ERGOLS EPUISES pendant '%s' : %.1f kg manquants, %.1f m/s non delivres",
                  b.id.c_str(), mp_needed - mp_avail, dv_mag - dv_propulsif);
    rep_.ok = false;
    rep_.failure = buf;
  }
  return br;
}

// --- pilote batch : execute un plan entierement pre-calcule -------------------
// Utilise pour la CONCEPTION (nominal, reversible) et le MONTE-CARLO.
// En vol reel, on passe par Session : cf. doctrine en tete de Session.hpp.
FlightReport execute(const FlightPlan& plan, const ephem::IEphemeris& eph,
                     std::uint64_t seed, const prop::PropOptions& opt) {
  Session s(plan, eph, seed, opt);
  std::vector<BurnCmd> burns = plan.burns;
  std::sort(burns.begin(), burns.end(),
            [](const BurnCmd& a, const BurnCmd& b) { return a.t < b.t; });
  for (const auto& b : burns) {
    s.commit_burn(b);
    if (!s.alive()) return s.report();
  }
  s.advance_to(plan.t_stop);
  return s.report();
}

} // namespace fen::flight
