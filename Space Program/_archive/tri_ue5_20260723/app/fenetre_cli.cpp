// app/fenetre_cli.cpp
// Le binaire en ligne de commande EXISTE AVANT TOUTE UI. C'est une règle du
// projet : si le jeu n'est pas jouable au clavier avec un fichier texte et un
// CSV, aucune interface graphique ne le sauvera. Inversement, s'il l'est,
// l'UI n'est plus qu'une couche de confort.
//
//   fenetre design <plan.fpl>              propagation NOMINALE (reversible)
//   fenetre run    <plan.fpl> --seed N     COMMIT : un vol, une graine, irreversible
//   fenetre mc     <plan.fpl> --n 1000     dispersion Monte-Carlo (payante en jeu)
//
// Options : --csv <fichier>   trajectoire echantillonnee
//           --dt  <s>         pas d'echantillonnage (defaut 60 s)
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <fstream>

#include "fen/io/Fpl.hpp"
#include "fen/astro/Elements.hpp"
#include "fen/astro/Transfers.hpp"
#include "fen/nav/Statistics.hpp"

using namespace fen;
using namespace fen::cst;

static void print_vehicle(const flight::FlightPlan& p) {
  std::printf("\n--- VEHICULE ------------------------------------------------------\n");
  std::printf("  charge utile (contractuelle) : %10.1f kg\n", p.vehicle.payload_dry);
  for (std::size_t k = 0; k < p.vehicle.stages.size(); ++k) {
    const auto& s = p.vehicle.stages[k];
    std::printf("  etage %zu '%s' : moteur %s  F=%.1f kN  Isp=%.1f s\n",
                k, s.id.c_str(), s.engine.id.c_str(), s.engine.thrust_vac / 1000,
                s.engine.isp_vac);
    std::printf("      ergols charges %8.1f kg | residuels %5.1f kg | sec %7.1f kg\n",
                s.tank.propellant_mass, s.tank.propellant_mass * s.tank.residual_fraction,
                s.dry_mass());
    std::printf("      dv IDEAL de l'etage (impulsionnel, sans pertes) : %8.1f m/s\n",
                p.vehicle.stage_dv(k));
    std::printf("      debit massique %.2f kg/s  ->  %.1f s de poussee a plein\n",
                s.engine.mdot(), s.tank.usable() / s.engine.mdot());
  }
  std::printf("  masse au depart : %.1f kg\n", p.vehicle.total_mass());
}

static void print_orbit(const char* label, const Vec3& r, const Vec3& v, double mu) {
  const auto el = astro::rv_to_elements(r, v, mu);
  std::printf("  %-22s a=%12.3f km  e=%9.6f  i=%8.4f deg  rp=%11.3f km  ra=%11.3f km\n",
              label, el.a / 1000, el.e, el.i / DEG, el.rp / 1000, el.ra / 1000);
}

static int evaluate_goals(const io::FplDocument& doc, const flight::FlightReport& rep, double mu) {
  const auto el = astro::rv_to_elements(pos(rep.truth.y_final), vel(rep.truth.y_final), mu);
  std::printf("\n--- OBJECTIFS -----------------------------------------------------\n");
  int fails = 0;
  for (const auto& g : doc.goals) {
    double val = 0, tgt = g.target, tol = g.tol;
    const char* unit = "";
    double scale = 1.0;
    if (g.key == "sma") { val = el.a;  scale = 1000; unit = "km"; }
    else if (g.key == "ecc") { val = el.e; scale = 1; unit = "-"; }
    else if (g.key == "inc") { val = el.i; scale = DEG; unit = "deg"; }
    else if (g.key == "rp")  { val = el.rp; scale = 1000; unit = "km"; }
    else if (g.key == "ra")  { val = el.ra; scale = 1000; unit = "km"; }
    else if (g.key == "payload") { val = rep.ok ? 1e30 : 0; scale = 1; unit = "kg"; }
    const double err = std::fabs(val - tgt);
    const bool ok = (err <= tol);
    if (!ok) ++fails;
    std::printf("  [%s] %-4s : atteint %12.5f %-3s | cible %12.5f +/- %.5f | ecart %.5f\n",
                ok ? "OK  " : "RATE", g.key.c_str(), val / scale, unit,
                tgt / scale, tol / scale, err / scale);
  }
  return fails;
}

static void write_csv(const std::string& path, const flight::FlightReport& rep, double mu) {
  std::ofstream f(path);
  f << "t_tdb_s,x_m,y_m,z_m,vx_ms,vy_ms,vz_ms,m_kg,r_km,v_kms,sma_km,ecc,inc_deg\n";
  f.precision(12);
  for (const auto& s : rep.truth.samples) {
    const Vec3 r = pos(s.y), v = vel(s.y);
    const auto el = astro::rv_to_elements(r, v, mu);
    f << s.t << ',' << r.x << ',' << r.y << ',' << r.z << ','
      << v.x << ',' << v.y << ',' << v.z << ',' << mass(s.y) << ','
      << norm(r) / 1000 << ',' << norm(v) / 1000 << ','
      << el.a / 1000 << ',' << el.e << ',' << el.i / DEG << '\n';
  }
}

static int cmd_run(const io::FplDocument& doc, std::uint64_t seed,
                   const std::string& csv, double dt) {
  ephem::StandishEphemeris eph;
  const double mu = ephem::body_mu(doc.plan.center);

  prop::PropOptions opt;
  opt.step.rtol = 1e-12;
  opt.sample_dt = dt;

  print_vehicle(doc.plan);
  std::printf("\n--- MODELE DE VERITE ----------------------------------------------\n");
  std::printf("  centre : %s | perturbateurs :", ephem::body_name(doc.plan.center));
  for (auto b : doc.plan.perturbers) std::printf(" %s", ephem::body_name(b));
  std::printf("\n  ephemeride : %s\n", eph.model_name());
  std::printf("  integrateur : DOPRI5(4) adaptatif, rtol=%.0e, sortie dense ordre 5\n",
              opt.step.rtol);
  std::printf("  poussee : FINIE (arc centre sur l'instant commande)\n");
  std::printf("  erreurs d'execution : %s\n",
              seed ? "GATES ACTIF (COMMIT — irreversible)" : "DESACTIVEES (mode conception)");
  if (seed) std::printf("  graine : %llu\n", (unsigned long long)seed);

  print_orbit("orbite initiale", doc.plan.initial.r, doc.plan.initial.v, mu);

  auto rep = flight::execute(doc.plan, eph, seed, opt);

  std::printf("\n--- MANOEUVRES ----------------------------------------------------\n");
  for (const auto& b : rep.burns) {
    std::printf("  %-6s allumage t0+%9.2f s | duree %7.2f s\n",
                b.id.c_str(), b.t_ignition - doc.plan.epoch0, b.duration);
    std::printf("         dv commande   %9.3f m/s  [%.3f, %.3f, %.3f]\n",
                b.dv_cmd_mag, b.dv_commanded.x, b.dv_commanded.y, b.dv_commanded.z);
    if (seed)
      std::printf("         dv apres Gates %9.3f m/s  (ecart %+.3f m/s)\n",
                  norm(b.dv_perturbed), norm(b.dv_perturbed) - b.dv_cmd_mag);
    std::printf("         dv PROPULSIF depense %9.3f m/s | dv UTILE obtenu %9.3f m/s\n",
                b.dv_achieved_mag + b.finite_burn_loss, b.dv_achieved_mag);
    std::printf("         PERTE DE POUSSEE FINIE %6.3f m/s  (%.2f %% du dv commande)\n",
                b.finite_burn_loss, 100.0 * b.finite_burn_loss / std::max(1.0, b.dv_cmd_mag));
    std::printf("         ergols %8.2f kg | masse %9.1f -> %9.1f kg\n",
                b.propellant_used, b.mass_before, b.mass_after);
  }

  std::printf("\n--- EVENEMENTS (racines sur interpolant dense) --------------------\n");
  for (const auto& e : rep.truth.events) {
    const Vec3 r = pos(e.y);
    std::printf("  %-16s t0+%10.3f s | r = %11.3f km\n",
                e.name.c_str(), e.t - doc.plan.epoch0, norm(r) / 1000);
  }

  std::printf("\n--- BILAN ---------------------------------------------------------\n");
  print_orbit("orbite finale", pos(rep.truth.y_final), vel(rep.truth.y_final), mu);
  std::printf("  ergols consommes : %.1f kg\n", rep.total_propellant);
  double loaded = 0;
  for (const auto& s : doc.plan.vehicle.stages) loaded += s.tank.usable();
  std::printf("  ergols UTILISABLES restants : %.1f kg  (marge %.1f m/s au dv actuel)\n",
              loaded - rep.total_propellant,
              astro::tsiolkovsky_dv(mass(rep.truth.y_final),
                                    mass(rep.truth.y_final) - (loaded - rep.total_propellant),
                                    doc.plan.vehicle.stages[0].engine.isp_vac));
  std::printf("  pas d'integration : %lld acceptes / %lld rejetes\n",
              rep.truth.steps_accepted, rep.truth.steps_rejected);

  if (!rep.ok) std::printf("\n  *** ECHEC : %s\n", rep.failure.c_str());

  const int fails = evaluate_goals(doc, rep, mu);
  std::printf("\n>>> %s\n", (rep.ok && fails == 0) ? "MISSION REUSSIE" : "MISSION ECHOUEE");

  if (!csv.empty()) { write_csv(csv, rep, mu); std::printf("    trajectoire ecrite : %s\n", csv.c_str()); }
  return (rep.ok && fails == 0) ? 0 : 1;
}

static int cmd_mc(const io::FplDocument& doc, int n, std::uint64_t seed0) {
  ephem::StandishEphemeris eph;
  const double mu = ephem::body_mu(doc.plan.center);
  prop::PropOptions opt;
  opt.step.rtol = 1e-11;
  opt.sample_dt = 0.0;

  std::vector<double> sma, ecc, inc, prop_used;
  int success = 0, dry = 0;
  std::printf("\n--- MONTE-CARLO (%d tirages) --------------------------------------\n", n);
  for (int i = 0; i < n; ++i) {
    auto rep = flight::execute(doc.plan, eph, seed0 + static_cast<std::uint64_t>(i), opt);
    if (!rep.ok) { ++dry; continue; }
    const auto el = astro::rv_to_elements(pos(rep.truth.y_final), vel(rep.truth.y_final), mu);
    sma.push_back(el.a / 1000);
    ecc.push_back(el.e);
    inc.push_back(el.i / DEG);
    prop_used.push_back(rep.total_propellant);
    int fails = 0;
    for (const auto& g : doc.goals) {
      double val = 0;
      if (g.key == "sma") val = el.a;
      else if (g.key == "ecc") val = el.e;
      else if (g.key == "inc") val = el.i;
      else continue;
      if (std::fabs(val - g.target) > g.tol) ++fails;
    }
    if (fails == 0) ++success;
  }
  auto sa = nav::summarize(sma), se = nav::summarize(ecc), si = nav::summarize(inc);
  auto sp = nav::summarize(prop_used);
  std::printf("  a   : moy %11.3f km  sigma %9.3f  [p1..p99] %11.3f .. %11.3f\n",
              sa.mean, sa.sigma, nav::percentile(sma, 0.01), sa.p99);
  std::printf("  e   : moy %11.6f     sigma %9.6f  p99 %.6f\n", se.mean, se.sigma, se.p99);
  std::printf("  i   : moy %11.4f deg sigma %9.4f  p99 %.4f\n", si.mean, si.sigma, si.p99);
  std::printf("  ergols : moy %8.1f kg  sigma %6.1f  p99 %8.1f\n", sp.mean, sp.sigma, sp.p99);
  std::printf("\n  P(objectifs tenus)   = %.1f %%   (%d/%d)\n", 100.0 * success / n, success, n);
  std::printf("  P(ergols epuises)    = %.1f %%   (%d/%d)\n", 100.0 * dry / n, dry, n);
  std::printf("\n  >>> Ces chiffres sont votre BUDGET DE MARGE. Le jeu ne les corrige pas :\n");
  std::printf("      il vous dit ce que votre conception coute au 99e percentile.\n");
  return 0;
}

int main(int argc, char** argv) {
  if (argc < 3) {
    std::printf("usage: fenetre <design|run|mc> <plan.fpl> [--seed N] [--n N] [--csv f] [--dt s]\n");
    return 2;
  }
  const std::string cmd = argv[1];
  const std::string file = argv[2];
  std::uint64_t seed = 0;
  int n = 500;
  std::string csv;
  double dt = 60.0;
  for (int i = 3; i < argc - 1; ++i) {
    if (!std::strcmp(argv[i], "--seed")) seed = std::strtoull(argv[i + 1], nullptr, 10);
    if (!std::strcmp(argv[i], "--n"))    n = std::atoi(argv[i + 1]);
    if (!std::strcmp(argv[i], "--csv"))  csv = argv[i + 1];
    if (!std::strcmp(argv[i], "--dt"))   dt = std::atof(argv[i + 1]);
  }
  try {
    auto doc = io::parse_fpl(file);
    std::printf("===================================================================\n");
    std::printf(" FENETRE — mission '%s'\n", doc.plan.mission_id.c_str());
    std::printf("===================================================================\n");
    for (const auto& w : doc.warnings) std::printf("  [avertissement] %s\n", w.c_str());
    if (cmd == "design") return cmd_run(doc, 0, csv, dt);
    if (cmd == "run")    return cmd_run(doc, seed ? seed : 1, csv, dt);
    if (cmd == "mc")     return cmd_mc(doc, n, seed ? seed : 1);
    std::printf("commande inconnue : %s\n", cmd.c_str());
    return 2;
  } catch (const std::exception& e) {
    std::printf("\n*** ERREUR : %s\n", e.what());
    return 3;
  }
}
