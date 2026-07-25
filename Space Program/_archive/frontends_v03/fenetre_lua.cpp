// app/fenetre_lua.cpp — LA CONSOLE DE SCRIPT (§3.5 du GDD, « la fonctionnalité clé »).
//
// L'API physique etait deja publique, complete et testee. Il lui manquait un
// interpreteur. Tous les `scripts/*.cpp` portent en tete le commentaire
// « ce que le joueur ecrit » : c'etaient les SPECIFICATIONS de ce fichier.
//
// +-------------------------------------------------------------------------+
// | CE QUI N'EST PAS EXPOSE, ET NE LE SERA JAMAIS                            |
// |                                                                          |
// |   Session::truth_state()        la position VRAIE du vaisseau            |
// |   Session::y()                  idem, brute                              |
// |   Session::set_gates_enabled()  desactiver les erreurs d'execution       |
// |                                                                          |
// | Ce sont des outils de POST-MORTEM. Les exposer detruirait le jeu : le    |
// | joueur n'aurait plus a ACHETER de la navigation, il LIRAIT la verite.    |
// | Toute l'economie de M00 (6,2 % -> 97,5 % de reussite, 0 -> 32,9 M$)      |
// | repose sur le fait que cette porte est FERMEE.                           |
// |                                                                          |
// | Le joueur a observe() : un ESTIME, avec sa covariance, issu des passes   |
// | de poursuite qu'il a PAYEES. C'est tout. C'est le jeu.                   |
// +-------------------------------------------------------------------------+
//
// Et il a fen.forces() : SON PROPRE modele de forces, distinct de la verite.
// C'est l'economie de fidelite de modele, mise entre ses mains.
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "fen/astro/BPlane.hpp"
#include "fen/astro/Elements.hpp"
#include "fen/astro/Flyby.hpp"
#include "fen/astro/Kepler.hpp"
#include "fen/astro/Lambert.hpp"
#include "fen/astro/Transfers.hpp"
#include "fen/core/Constants.hpp"
#include "fen/core/Epoch.hpp"
#include "fen/ephem/Ephemeris.hpp"
#include "fen/flight/Session.hpp"
#include "fen/io/Fpl.hpp"
#include "fen/nav/Gates.hpp"
#include "fen/vehicle/Vehicle.hpp"

using namespace fen;

static ephem::StandishEphemeris g_eph;   // l'ephemeride est un fait du monde

static ephem::Body body_of(const std::string& s) {
  if (s == "Sun") return ephem::Body::Sun;
  if (s == "Mercury") return ephem::Body::Mercury;
  if (s == "Venus") return ephem::Body::Venus;
  if (s == "Earth" || s == "EarthBary") return ephem::Body::EarthBary;
  if (s == "Moon") return ephem::Body::Moon;
  if (s == "Mars") return ephem::Body::Mars;
  if (s == "Jupiter") return ephem::Body::Jupiter;
  if (s == "Saturn") return ephem::Body::Saturn;
  throw std::runtime_error("corps inconnu : " + s);
}

// --- LE MODELE DU JOUEUR. Pas la verite : le SIEN. Il paie son erreur. -------
struct PlayerForces {
  force::ForceStack stack;
  void central(double mu) { stack.add(std::make_shared<force::CentralGravity>(mu)); }
  void third_body(const std::string& b, const std::string& c) {
    stack.add(std::make_shared<force::ThirdBodyGravity>(&g_eph, body_of(b), body_of(c)));
  }
  void j2(double mu, double j2c, double req) {
    stack.add(std::make_shared<force::J2Gravity>(mu, j2c, req));
  }
};

// --- LA SESSION, VUE PAR LE JOUEUR. Enveloppe DELIBEREMENT etroite. ---------
struct LuaSession {
  std::unique_ptr<flight::Session> S;
  double mu_center{};
  LuaSession(const io::FplDocument& d, std::uint64_t seed) {
    prop::PropOptions o; o.step.rtol = 1e-11; o.sample_dt = 0.0;
    S = std::make_unique<flight::Session>(d.plan, g_eph, seed, o);
    mu_center = ephem::body_mu(d.plan.center);
  }
  bool advance_to(double t) { return S->advance_to(t); }
  double advance_to_event(const std::string& n, double tm) { return S->advance_to_event(n, tm); }
  double advance_to_any_event(sol::table names, double tm) {
    std::vector<std::string> v;
    for (auto& kv : names) v.push_back(kv.second.as<std::string>());
    return S->advance_to_any_event(v, tm);
  }
  sol::table commit_burn(sol::table b, sol::this_state ts) {
    flight::BurnCmd c;
    c.id = b.get_or("id", std::string("BURN"));
    c.t  = b.get<double>("t");
    c.dv = b.get<Vec3>("dv");
    c.stage = static_cast<std::size_t>(b.get_or("stage", 0));
    c.frame = (b.get_or("frame", std::string("inertial")) == "rsw")
              ? flight::DvFrame::RSW : flight::DvFrame::Inertial;
    c.hold = force::ThrustFrame::InertialFixed;
    const auto r = S->commit_burn(c);
    sol::state_view L(ts);
    sol::table o = L.create_table();
    o["id"] = r.id;
    o["dv_commanded"] = r.dv_cmd_mag;
    o["dv_achieved"]  = r.dv_achieved_mag;
    o["finite_loss"]  = r.finite_burn_loss;
    o["propellant"]   = r.propellant_used;
    o["mass_after"]   = r.mass_after;
    o["cutoff_early"] = r.engine_cutoff_early;
    o["duration"]     = r.duration;
    return o;
  }
  // >>> L'ESTIME. Le SEUL canal d'information du joueur. <<<
  sol::table observe(sol::this_state ts) {
    const auto ob = S->observe();
    sol::state_view L(ts);
    sol::table t = L.create_table();
    t["t"] = ob.t;
    t["r"] = ob.state.r; t["v"] = ob.state.v; t["m"] = ob.state.m;
    t["sigma_pos"] = ob.sigma_pos; t["sigma_vel"] = ob.sigma_vel;
    t["n_measurements"] = ob.n_measurements;
    t["observable"] = ob.observable;
    t["rms_residual"] = ob.rms_residual;
    t["source"] = ob.source;
    return t;
  }
  void schedule_pass(sol::table p) {
    nav::Pass q;
    q.station = p.get_or("station", 0);
    q.t_start = p.get<double>("t_start");
    q.t_end   = p.get<double>("t_end");
    q.sample_dt = p.get_or("sample_dt", 60.0);
    S->schedule_pass(q);
  }
  double t() const { return S->t(); }
  bool alive() const { return S->alive(); }
  int n_measurements() const { return S->n_measurements(); }
  double tracking_cost_musd() const { return S->tracking_cost_musd(); }
  double dv_remaining(int k) const { return S->dv_remaining((std::size_t)k); }
  double mu() const { return mu_center; }
  // truth_state / y / set_gates_enabled : ABSENTS. Ce n'est pas un oubli.
};

int main(int argc, char** argv) {
  if (argc < 2) { std::printf("usage: fenetre_lua <script.lua> [args...]\n"); return 1; }
  sol::state lua;
  lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string,
                     sol::lib::table, sol::lib::io, sol::lib::os);
  sol::table F = lua.create_named_table("fen");

  // ---- Vec3 -----------------------------------------------------------------
  lua.new_usertype<Vec3>("Vec3",
      "x", &Vec3::x, "y", &Vec3::y, "z", &Vec3::z,
      "norm", [](const Vec3& v){ return norm(v); },
      sol::meta_function::addition,    [](const Vec3& a, const Vec3& b){ return a + b; },
      sol::meta_function::subtraction, [](const Vec3& a, const Vec3& b){ return a - b; },
      sol::meta_function::multiplication, [](const Vec3& a, double s){ return a * s; },
      sol::meta_function::to_string, [](const Vec3& v){
        char b[96]; std::snprintf(b, 96, "(%.6g, %.6g, %.6g)", v.x, v.y, v.z);
        return std::string(b); });
  F.set_function("vec3",  [](double x, double y, double z){ return Vec3{x,y,z}; });
  F.set_function("dot",   [](const Vec3& a, const Vec3& b){ return dot(a,b); });
  F.set_function("cross", [](const Vec3& a, const Vec3& b){ return cross(a,b); });
  F.set_function("unit",  [](const Vec3& a){ return unit(a); });
  F.set_function("norm",  [](const Vec3& a){ return norm(a); });

  // ---- constantes (SI strict, sourcees) -------------------------------------
  F["MU_SUN"]=cst::MU_SUN; F["MU_EARTH"]=cst::MU_EARTH; F["MU_MARS"]=cst::MU_MARS;
  F["MU_JUPITER"]=cst::MU_JUPITER; F["MU_SATURN"]=cst::MU_SATURN; F["MU_VENUS"]=cst::MU_VENUS;
  F["R_EARTH"]=cst::R_EARTH; F["R_SATURN"]=cst::R_SATURN; F["AU"]=cst::AU;
  F["DAY"]=cst::DAY; F["DEG"]=cst::DEG; F["PI"]=cst::PI; F["G0"]=cst::G0;
  F["J2_EARTH"]=cst::J2_EARTH; F["R_GEO"]=42164170.0;
  F.set_function("epoch_from_iso", [](const std::string& s){ return epoch_from_iso(s).tdb; });
  F.set_function("epoch_to_iso",   [](double t){ return epoch_to_iso(Epoch{t}); });

  // ---- LES CALCULATRICES ----------------------------------------------------
  // Elles repondent a « combien coute X ? ». JAMAIS a « que dois-je faire ? ».
  F.set_function("vis_viva",       &astro::vis_viva);
  F.set_function("v_circular",     &astro::v_circular);
  F.set_function("v_escape",       &astro::v_escape);
  F.set_function("orbital_period", &astro::orbital_period);
  F.set_function("dv_plane_change",&astro::dv_plane_change);
  F.set_function("dv_combined",    &astro::dv_combined);
  F.set_function("synodic_period", &astro::synodic_period);
  F.set_function("tsiolkovsky_dv", &astro::tsiolkovsky_dv);
  F.set_function("propellant_for_dv", &astro::propellant_for_dv);
  F.set_function("m0_for_dv",      &astro::m0_for_dv);
  F.set_function("C3_from_vinf",   &astro::C3_from_vinf);
  F.set_function("dv_injection",   &astro::dv_injection);
  F.set_function("dv_insertion",   &astro::dv_insertion);
  F.set_function("flyby_turn",     &astro::flyby_turn);
  F.set_function("flyby_max_free_dv", &astro::flyby_max_free_dv);
  F.set_function("b_from_rp",      &astro::b_from_rp);
  F.set_function("rp_from_b",      &astro::rp_from_b);

  F.set_function("hohmann", [&lua](double r1, double r2, double mu){
    const auto h = astro::hohmann(r1, r2, mu);
    sol::table t = lua.create_table();
    t["dv1"]=h.dv1; t["dv2"]=h.dv2; t["dv_total"]=h.dv_total; t["tof"]=h.tof; return t; });
  F.set_function("kepler_propagate", [&lua](const Vec3& r, const Vec3& v, double dt, double mu){
    const auto k = astro::kepler_propagate(r, v, dt, mu);
    sol::table t = lua.create_table();
    t["r"]=k.r; t["v"]=k.v; t["converged"]=k.converged; return t; });
  F.set_function("rv_to_elements", [&lua](const Vec3& r, const Vec3& v, double mu){
    const auto e = astro::rv_to_elements(r, v, mu);
    sol::table t = lua.create_table();
    t["a"]=e.a; t["e"]=e.e; t["i"]=e.i; t["raan"]=e.raan; t["argp"]=e.argp;
    t["nu"]=e.nu; t["rp"]=e.rp; t["ra"]=e.ra; return t; });
  F.set_function("lambert", [&lua](const Vec3& r1, const Vec3& r2, double tof, double mu,
                                   bool prograde, int revs){
    const auto L = astro::lambert(r1, r2, tof, mu, prograde, revs);
    sol::table t = lua.create_table(); t["ok"]=L.ok;
    sol::table S = lua.create_table();
    for (std::size_t i = 0; i < L.solutions.size(); ++i) {
      sol::table s = lua.create_table();
      s["v1"]=L.solutions[i].v1; s["v2"]=L.solutions[i].v2;
      s["revolutions"]=L.solutions[i].revolutions;
      S[i+1] = s;
    }
    t["solutions"] = S; return t; });
  F.set_function("b_plane", [&lua](const Vec3& r, const Vec3& v, double mu, const Vec3& ref){
    const auto b = astro::b_plane(r, v, mu, ref);
    sol::table t = lua.create_table();
    t["BdotR"]=b.BdotR; t["BdotT"]=b.BdotT; t["b"]=b.b; t["rp"]=b.rp; t["vinf"]=b.vinf;
    return t; });
  F.set_function("solve_flyby", [&lua](const Vec3& vin, const Vec3& vout, double mu, double rpmin){
    const auto f = astro::solve_flyby(vin, vout, mu, rpmin);
    sol::table t = lua.create_table();
    t["feasible"]=f.feasible; t["rp"]=f.rp; t["dv"]=f.dv;
    t["turn_required"]=f.turn_required; t["turn_available"]=f.turn_available;
    t["vinf_in"]=f.vinf_in; t["vinf_out"]=f.vinf_out; return t; });
  F.set_function("gates_sigma_total", [](double dv, sol::optional<sol::table> g){
    nav::GatesParams p;
    if (g) {
      p.sigma_mag_fixed   = g->get_or("sigma_mag_fixed",   p.sigma_mag_fixed);
      p.sigma_mag_prop    = g->get_or("sigma_mag_prop",    p.sigma_mag_prop);
      p.sigma_point_fixed = g->get_or("sigma_point_fixed", p.sigma_point_fixed);
      p.sigma_point_prop  = g->get_or("sigma_point_prop",  p.sigma_point_prop);
    }
    return nav::gates_sigma_total(dv, p); });
  F.set_function("body_state", [&lua](const std::string& b, const std::string& c, double t){
    const auto s = g_eph.state(body_of(b), body_of(c), Epoch{t});
    sol::table q = lua.create_table(); q["r"]=s.r; q["v"]=s.v; return q; });

  // ---- LE VEHICULE : le point fixe masse <-> Delta-v ------------------------
  F.set_function("size_stage_for_dv", [&lua](double dv, double mass_above, double isp,
                                             double thrust, double eng_mass,
                                             double tank_dry_frac, double structure){
    vehicle::Engine e; e.isp_vac = isp; e.thrust_vac = thrust; e.mass = eng_mass;
    const auto s = vehicle::size_stage_for_dv(dv, mass_above, e, tank_dry_frac, structure, 0.02);
    sol::table t = lua.create_table();
    t["propellant"]=s.propellant; t["stage_dry"]=s.stage_dry; t["m0"]=s.m0;
    t["converged"]=s.converged; return t; });

  // ---- LE MODELE DU JOUEUR, ET SA PROPAGATION ------------------------------
  lua.new_usertype<PlayerForces>("Forces",
      "central", &PlayerForces::central,
      "third_body", &PlayerForces::third_body,
      "j2", &PlayerForces::j2);
  F.set_function("forces", []{ return std::make_shared<PlayerForces>(); });
  F.set_function("propagate", [&lua](const std::shared_ptr<PlayerForces>& G, double t0,
                                     const Vec3& r0, const Vec3& v0, double m0, double t1,
                                     sol::optional<sol::table> evs){
    StateN y{r0.x, r0.y, r0.z, v0.x, v0.y, v0.z, m0};
    std::vector<prop::EventSpec> events;
    if (evs) for (auto& kv : *evs) {
      const auto e = kv.second.as<sol::table>();
      const std::string k = e.get<std::string>("kind");
      const double mu = e.get_or("mu", cst::MU_EARTH);
      if (k == "periapsis")     events.push_back(prop::event_periapsis(mu));
      else if (k == "apoapsis") events.push_back(prop::event_apoapsis(mu));
    }
    prop::PropOptions o; o.step.rtol = 1e-11; o.sample_dt = 0.0;
    const auto R = prop::propagate(G->stack, t0, y, t1, events, o);
    sol::table t = lua.create_table();
    t["r"] = Vec3{R.y_final[0], R.y_final[1], R.y_final[2]};
    t["v"] = Vec3{R.y_final[3], R.y_final[4], R.y_final[5]};
    t["m"] = R.y_final[6];
    sol::table E = lua.create_table();
    for (std::size_t i = 0; i < R.events.size(); ++i) {
      sol::table e = lua.create_table();
      e["name"] = R.events[i].name; e["t"] = R.events[i].t;
      e["r"] = Vec3{R.events[i].y[0], R.events[i].y[1], R.events[i].y[2]};
      e["v"] = Vec3{R.events[i].y[3], R.events[i].y[4], R.events[i].y[5]};
      e["m"] = R.events[i].y[6];
      E[i+1] = e;
    }
    t["events"] = E;
    return t; });

  // ---- LA SESSION ----------------------------------------------------------
  lua.new_usertype<LuaSession>("Session",
      "advance_to", &LuaSession::advance_to,
      "advance_to_event", &LuaSession::advance_to_event,
      "advance_to_any_event", &LuaSession::advance_to_any_event,
      "commit_burn", &LuaSession::commit_burn,
      "observe", &LuaSession::observe,             // <-- un ESTIME. Pas la verite.
      "schedule_pass", &LuaSession::schedule_pass,
      "t", &LuaSession::t,
      "alive", &LuaSession::alive,
      "n_measurements", &LuaSession::n_measurements,
      "tracking_cost_musd", &LuaSession::tracking_cost_musd,
      "dv_remaining", &LuaSession::dv_remaining,
      "mu", &LuaSession::mu);

  lua.new_usertype<io::FplDocument>("Fpl",
      "epoch0",  sol::property([](io::FplDocument& d){ return d.plan.epoch0; }),
      "t_stop",  sol::property([](io::FplDocument& d){ return d.plan.t_stop; }),
      "n_burns", sol::property([](io::FplDocument& d){ return (int)d.plan.burns.size(); }));
  F.set_function("load_fpl", [](const std::string& p){
    return std::make_shared<io::FplDocument>(io::parse_fpl(p)); });
  F.set_function("plan_burn", [&lua](const std::shared_ptr<io::FplDocument>& d, int i){
    sol::table t = lua.create_table();
    const auto& b = d->plan.burns[i-1];
    t["id"]=b.id; t["t"]=b.t; t["dv"]=b.dv; t["stage"]=(int)b.stage;
    t["frame"] = (b.frame == flight::DvFrame::RSW) ? "rsw" : "inertial";
    return t; });
  F.set_function("session", [](const std::shared_ptr<io::FplDocument>& d, std::uint64_t seed){
    return std::make_shared<LuaSession>(*d, seed); });

  sol::table A = lua.create_named_table("arg");
  for (int i = 2; i < argc; ++i) A[i-1] = std::string(argv[i]);
  try { lua.script_file(argv[1]); }
  catch (const std::exception& e) { std::printf("\n*** erreur de script : %s\n", e.what()); return 1; }
  return 0;
}
