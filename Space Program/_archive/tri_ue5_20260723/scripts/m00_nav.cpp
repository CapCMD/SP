// scripts/m00_nav.cpp
//
// L'ÉCONOMIE DE LA NAVIGATION, mesurée.
//
// Les deux chiffres déjà connus de M00 sont en fait les DEUX BORNES d'une courbe :
//   - 5,2 %   : plan rigide, aucune correction  (= aveugle)
//   - 100 %   : corrections calculées sur la VÉRITÉ (= omniscience, non achetable)
// Ce programme remplit le milieu, et met un prix dessus.
//
// RÈGLE ABSOLUE ICI : ce code ne touche JAMAIS Session::truth_state(). Il n'a
// accès qu'à Session::observe(), qui rend un ESTIMÉ + covariance issus des
// mesures ACHETÉES. Y compris pour DATER ses manoeuvres : le joueur prédit ses
// apsides en propageant SON estimé, pas la vérité. S'il navigue mal, il brûle
// aussi au mauvais moment.
#include <cstdio>
#include <cmath>
#include <vector>
#include <string>
#include "fen/io/Fpl.hpp"
#include "fen/flight/Session.hpp"
#include "fen/astro/Elements.hpp"
#include "fen/astro/Transfers.hpp"
#include "fen/nav/Statistics.hpp"

using namespace fen;
using namespace fen::cst;

static constexpr double R_GEO = 42164170.0;

struct Outcome { bool ok{false}; double a{}, e{}, i{}, dv_corr{}, cost{}; int nmeas{}; double sig{}; };

// Le modèle dynamique DU JOUEUR. Il le construit lui-même, avec l'API publique.
// (S'il choisit un modèle plus grossier — sans la Lune, par exemple — son OD sera
//  biaisée et il le paiera. C'est l'économie de fidélité de modèle appliquée à la
//  navigation. Non exploité en MVP, mais l'architecture le permet déjà.)
static force::ForceStack player_model(const ephem::IEphemeris& eph) {
  force::ForceStack g;
  g.add(std::make_shared<force::CentralGravity>(MU_EARTH));
  g.add(std::make_shared<force::ThirdBodyGravity>(&eph, ephem::Body::Sun, ephem::Body::EarthBary));
  g.add(std::make_shared<force::ThirdBodyGravity>(&eph, ephem::Body::Moon, ephem::Body::EarthBary));
  return g;
}

static Outcome fly(const io::FplDocument& doc, const ephem::IEphemeris& eph,
                   std::uint64_t seed, const std::vector<nav::Pass>& passes, bool verbose,
                   int extra_revs = 0) {
  prop::PropOptions opt;
  opt.step.rtol = 1e-11;
  opt.sample_dt = 0.0;

  flight::Session S(doc.plan, eph, seed, opt);
  for (const auto& p : passes) S.schedule_pass(p);
  const force::ForceStack G = player_model(eph);
  const double t0 = doc.plan.epoch0;
  Outcome out;
  double dv_corr = 0.0;

  auto say = [&](const char* f, auto... a) { if (verbose) std::printf(f, a...); };

  // --- LE JOUEUR PRÉDIT SES APSIDES SUR SON PROPRE ESTIMÉ ---------------------
  // Il ne peut pas demander au monde « quand est mon apogée ? ». Il propage ce
  // qu'il CROIT être son état, et il en déduit une date. Si son estimé est
  // mauvais, sa date l'est aussi.
  // Renvoie (instant, etat predit) du prochain apsis, D'APRES L'ESTIME.
  // `only` = "APOAPSIS" pour n'accepter que l'apogee ; "" = n'importe quel apsis.
  // `skip` = fraction de periode a sauter (le point ou l'on vient de bruler reste
  //          un apsis : cf. MISSION_M00.md).
  struct Pred { double t{std::nan("")}; StateN y{}; };
  auto predict_apsis = [&](const flight::Observation& o, const char* only, double skip) -> Pred {
    Pred out_p;
    const auto el = astro::rv_to_elements(o.state.r, o.state.v, MU_EARTH);
    const double T = astro::orbital_period(el.a, MU_EARTH);
    if (!std::isfinite(T) || T <= 0) return out_p;
    StateN y{o.state.r.x, o.state.r.y, o.state.r.z,
             o.state.v.x, o.state.v.y, o.state.v.z, o.state.m};
    prop::PropOptions po = opt; po.sample_dt = 0.0;
    const double t_skip = o.t + skip * T;
    auto r0 = prop::propagate(G, o.t, y, t_skip, {}, po);
    auto r = prop::propagate(G, t_skip, r0.y_final, t_skip + 1.1 * T,
                             {prop::event_periapsis(MU_EARTH), prop::event_apoapsis(MU_EARTH)}, po);
    for (const auto& ev : r.events) {
      if (only[0] && ev.name != only) continue;
      out_p.t = ev.t; out_p.y = ev.y; return out_p;    // etat PREDIT, pas la verite
    }
    return out_p;
  };

  auto target_v = [&](const Vec3& r) {
    const double a_new = 0.5 * (norm(r) + R_GEO);
    return unit(cross(Vec3{0, 0, 1}, r)) * astro::vis_viva(norm(r), a_new, MU_EARTH);
  };

  auto do_burn = [&](const char* id, const Pred& p, const Vec3& v_target) {
    flight::BurnCmd b;
    b.id = id; b.t = p.t; b.frame = flight::DvFrame::Inertial;
    b.hold = force::ThrustFrame::InertialFixed; b.stage = 0;
    b.dv = v_target - vel(p.y);              // calcule sur l'ETAT PREDIT
    const double n = norm(b.dv);
    S.commit_burn(b);
    return n;
  };

  // ---- 1) GTO : injection (valeur de conception, calculee hors ligne) --------
  S.commit_burn(doc.plan.burns[0]);
  if (!S.alive()) return out;

  // ---- 2) POURSUITE -> COUPURE -> CALCUL -> TIR ------------------------------
  // La poursuite s'arrete AVANT la manoeuvre : il faut traiter les donnees,
  // verifier, televerser. On ne brule pas sur des mesures qu'on est en train
  // d'acquerir. (C'est aussi ce qui rend l'erreur d'injection invisible pendant
  // une heure : aucune station ne voit le perigee.)
  if (!S.advance_to(t0 + 15200.0)) return out;
  auto o = S.observe();
  say("  [OBS 1] %s | sigma_pos = %.1f m | sigma_vel = %.4f m/s\n",
      o.source.c_str(), o.sigma_pos, o.sigma_vel);
  out.nmeas = o.n_measurements;
  out.sig = o.sigma_pos;

  Pred p1 = predict_apsis(o, "APOAPSIS", 0.0);
  if (!std::isfinite(p1.t)) return out;
  say("  [AMF]   apogee PREDITE t0+%.0f s | r_predit = %.1f km\n",
      p1.t - t0, norm(pos(p1.y)) / 1000);
  {
    const double dv = do_burn("AMF", p1, target_v(pos(p1.y)));
    say("          dv = %.2f m/s\n", dv);
    dv_corr += dv - 1836.5;
  }
  if (!S.alive()) return out;

  // ---- 3) re-poursuite -> AMF2 ----------------------------------------------
  auto el2 = astro::rv_to_elements(S.observe().state.r, S.observe().state.v, MU_EARTH);
  double T2 = astro::orbital_period(el2.a, MU_EARTH);
  if (!std::isfinite(T2)) return out;
  if (!S.advance_to(S.t() + 0.38 * T2)) return out;
  o = S.observe();
  say("  [OBS 2] %s | sigma_pos = %.1f m\n", o.source.c_str(), o.sigma_pos);
  Pred p2 = predict_apsis(o, "", 0.02);
  if (!std::isfinite(p2.t)) return out;
  {
    const double dv = do_burn("AMF2", p2, target_v(pos(p2.y)));
    say("  [AMF2]  t0+%.0f s | dv = %.2f m/s\n", p2.t - t0, dv);
    dv_corr += dv;
  }
  if (!S.alive()) return out;

  // ---- 4) re-poursuite -> TRIM ----------------------------------------------
  auto el3 = astro::rv_to_elements(S.observe().state.r, S.observe().state.v, MU_EARTH);
  double T3 = astro::orbital_period(el3.a, MU_EARTH);
  if (!std::isfinite(T3)) return out;
  // ATTENDRE des revolutions supplementaires en poursuivant : la geometrie
  // quasi-geostationnaire tourne lentement. Ce n'est PAS de l'argent qu'on
  // depense ici, c'est du CALENDRIER — et c'est la seule monnaie qui achete
  // le conditionnement d'une OD quasi-circulaire.
  if (!S.advance_to(S.t() + (0.38 + extra_revs) * T3)) return out;
  o = S.observe();
  say("  [OBS 3] %s | sigma_pos = %.1f m\n", o.source.c_str(), o.sigma_pos);
  Pred p3 = predict_apsis(o, "", 0.02);
  if (!std::isfinite(p3.t)) return out;
  {
    const Vec3 rp = pos(p3.y);
    const Vec3 vt = unit(cross(Vec3{0, 0, 1}, rp)) * astro::v_circular(norm(rp), MU_EARTH);
    const double dv = do_burn("TRIM", p3, vt);
    say("  [TRIM]  t0+%.0f s | dv = %.2f m/s\n", p3.t - t0, dv);
    dv_corr += dv;
  }
  if (!S.alive()) return out;
  S.advance_to(S.t() + 2000.0);

  // ---- VERDICT : ici, et seulement ici, on regarde la VÉRITÉ -----------------
  const auto tr = S.truth_state();
  const auto el = astro::rv_to_elements(tr.r, tr.v, MU_EARTH);
  out.a = el.a; out.e = el.e; out.i = el.i;
  out.dv_corr = dv_corr;
  out.cost = S.tracking_cost_musd();
  out.ok = std::fabs(el.a - R_GEO) < 50e3 && el.e < 2e-3 && el.i / DEG < 0.25;
  say("  [VERITE] a=%.3f km (%+.3f)  e=%.6f  i=%.5f deg  -> %s\n",
      el.a / 1000, (el.a - R_GEO) / 1000, el.e, el.i / DEG, out.ok ? "REUSSI" : "ECHOUE");
  return out;
}

int main(int argc, char** argv) {
  const std::string path = (argc > 1) ? argv[1] : "missions/m00_geo_solution.fpl";
  const int N = (argc > 2) ? std::atoi(argv[2]) : 120;
  auto doc = io::parse_fpl(path);
  ephem::StandishEphemeris eph;
  const double t0 = doc.plan.epoch0;

  auto P = [&](int st, double a, double b) {
    nav::Pass p; p.station = st; p.t_start = t0 + a; p.t_end = t0 + b; p.sample_dt = 60.0; return p;
  };

  struct Scenario { std::string name; std::vector<nav::Pass> passes; int extra_revs{0}; };
  std::vector<Scenario> scen = {
    {"S0 — AVEUGLE (aucune poursuite)",              {}},
    {"S1 — 30 min, 1 station",                       {P(0, 4000, 5800)}},
    {"S2 — 3h10, 1 station",                         {P(0, 3600, 15000)}},
    {"S3 — S2 + 1 arc apres chaque manoeuvre",       {P(0, 3600, 15000),
                                                      P(2, 21000, 36000),
                                                      P(2, 64000, 78000)}},
    {"S4 — S3 + 3 stations, arcs courts",            {P(0, 3600, 15000), P(1, 3600, 15000),
                                                      P(2, 3600, 15000),
                                                      P(0, 21000, 36000), P(1, 21000, 36000),
                                                      P(2, 21000, 36000),
                                                      P(0, 64000, 78000), P(1, 64000, 78000),
                                                      P(2, 64000, 78000)}},
    // S5 : on utilise TOUTE la fenetre disponible entre deux manoeuvres.
    // Ca ne coute pas plus cher en argent par heure — mais ca coute des HEURES,
    // et c'est la seule facon de conditionner une OD quasi-geostationnaire.
    {"S5 — 3 stations, arcs COMPLETS",               {P(0, 3600, 15000), P(1, 3600, 15000),
                                                      P(2, 3600, 15000),
                                                      P(0, 21000, 58000), P(1, 21000, 58000),
                                                      P(2, 21000, 58000),
                                                      P(0, 65000, 103000), P(1, 65000, 103000),
                                                      P(2, 65000, 103000)}},
    {"S6 — S5 + 2 revolutions d'attente avant TRIM", {P(0, 3600, 15000), P(1, 3600, 15000),
                                                      P(2, 3600, 15000),
                                                      P(0, 21000, 58000), P(1, 21000, 58000),
                                                      P(2, 21000, 58000),
                                                      P(0, 65000, 280000), P(1, 65000, 280000),
                                                      P(2, 65000, 280000)}, 2},
  };

  std::printf("=====================================================================\n");
  std::printf(" M00 — L'ECONOMIE DE LA NAVIGATION\n");
  std::printf(" Le joueur ne voit QUE son estime. Il date meme ses manoeuvres dessus.\n");
  std::printf("=====================================================================\n");

  std::printf("\n### VOL DETAILLE (graine 4071, scenario S3)\n");
  fly(doc, eph, 4071, scen[6].passes, true, 2);

  std::printf("\n### MONTE-CARLO (%d graines par scenario)\n\n", N);
  std::printf("%-45s %7s %9s %11s %10s %9s\n",
              "scenario", "P(ok)", "sigma_a", "sigma_pos OD", "corr. p99", "cout M$");
  std::printf("%s\n", std::string(93, '-').c_str());

  for (auto& s : scen) {
    std::vector<double> A, DV, SG;
    int ok = 0, n = 0;
    for (int k = 0; k < N; ++k) {
      auto o = fly(doc, eph, 900 + static_cast<std::uint64_t>(k), s.passes, false, s.extra_revs);
      if (o.a <= 0) continue;
      A.push_back(o.a / 1000); DV.push_back(o.dv_corr); SG.push_back(o.sig);
      if (o.ok) ++ok;
      ++n;
    }
    if (n == 0) { std::printf("%-42s   (aucun vol abouti)\n", s.name.c_str()); continue; }
    auto sa = nav::summarize(A);
    auto sd = nav::summarize(DV);
    auto ss = nav::summarize(SG);
    double cost = 0.0;
    for (const auto& p : s.passes) cost += 0.15 * (p.t_end - p.t_start) / 3600.0;
    std::printf("%-45s %6.1f%% %8.1f km %10.1f m %9.1f m/s %8.2f\n",
                s.name.c_str(), 100.0 * ok / n, sa.sigma, ss.mean, sd.p99, cost);
  }

  std::printf("\n  Lecture : sans poursuite, le joueur CROIT que sa manoeuvre est passee au\n");
  std::printf("  nominal. Sa correction est donc calculee sur un etat faux — elle ne corrige\n");
  std::printf("  rien. La connaissance n'est pas un confort : c'est le facteur limitant.\n");
  return 0;
}
