// scripts/m00_iterate.cpp
//
// CE QUE LE JOUEUR ECRIT.
//
// Ceci n'est PAS une fonctionnalite du jeu : c'est un programme utilisateur qui
// consomme l'API publique d'astro_core. En V1 le meme code s'ecrira en Lua/Python
// dans la console embarquee. Il est ici pour prouver deux choses :
//   1. l'API est suffisante pour resoudre une mission sans aucune "aide" du jeu ;
//   2. la boucle de convergence conception -> verite -> correction EST le gameplay.
//
// Le probleme : la conception impulsionnelle (Hohmann + manoeuvre combinee) ne
// tombe PAS juste dans le propagateur de verite. Trois raisons, toutes physiques :
//   - la poussee est finie : l'arc perd ~5 m/s de dv utile (braquage + gravite) ;
//   - l'apogee reelle n'arrive donc pas a T/2 ;
//   - l'etat reel a l'apogee n'est pas l'etat nominal.
// Le jeu ne corrige rien. Le joueur itere. C'est tout.
#include <cstdio>
#include <cmath>
#include "fen/io/Fpl.hpp"
#include "fen/astro/Elements.hpp"
#include "fen/astro/Transfers.hpp"

using namespace fen;
using namespace fen::cst;

int main(int argc, char** argv) {
  const std::string path = (argc > 1) ? argv[1] : "missions/m00_geo.fpl";
  auto doc = io::parse_fpl(path);
  ephem::StandishEphemeris eph;
  const double mu = ephem::body_mu(doc.plan.center);

  const double R_GEO  = 42164170.0;
  const double V_GEO  = astro::v_circular(R_GEO, mu);

  prop::PropOptions opt;
  opt.step.rtol = 1e-12;
  opt.sample_dt = 0.0;

  std::printf("Cible : a = %.3f km, e = 0, i = 0 deg   (v_GEO = %.3f m/s)\n\n",
              R_GEO / 1000, V_GEO);

  // =====================================================================
  // ETAPE 1 — accorder dv1 pour que l'APOGEE REELLE tombe sur R_GEO.
  // Methode : secante sur dv1. Une seule inconnue, une seule equation.
  // =====================================================================
  std::printf("--- ETAPE 1 : accorder dv1 sur l'apogee reelle (secante) ---\n");
  double dv1 = norm(doc.plan.burns[0].dv);
  double dv1_prev = dv1, f_prev = 0.0;

  auto apogee_of = [&](double dv1_try, double& t_apo, double& r_apo,
                       StateN& y_apo) -> bool {
    flight::FlightPlan p = doc.plan;
    p.burns.resize(1);                        // on ne garde que l'injection
    p.burns[0].dv = Vec3{0, dv1_try, 0};
    p.t_stop = p.epoch0 + 25000.0;
    auto rep = flight::execute(p, eph, 0, opt);
    if (!rep.ok) return false;
    for (const auto& e : rep.truth.events)
      if (e.name == "APOAPSIS") {
        t_apo = e.t; y_apo = e.y; r_apo = norm(pos(e.y));
        return true;
      }
    return false;
  };

  double t_apo = 0, r_apo = 0;
  StateN y_apo{};
  for (int it = 0; it < 12; ++it) {
    if (!apogee_of(dv1, t_apo, r_apo, y_apo)) { std::printf("  echec\n"); return 1; }
    const double f = r_apo - R_GEO;
    std::printf("  it %2d : dv1 = %10.4f m/s  ->  r_apo = %12.3f km  (ecart %+9.3f km)\n",
                it, dv1, r_apo / 1000, f / 1000);
    if (std::fabs(f) < 1.0) break;            // 1 m : bien au-dela du besoin
    double dv1_next;
    if (it == 0) {
      // derivee analytique de depart : dr_a/dv_p = 2 a^2 / (r_p * ... ) ->
      // approximation robuste par vis-viva, raffinee ensuite par la secante.
      dv1_next = dv1 + 0.005 * (-f) / 1000.0 * 1000.0 / 1000.0;
      dv1_next = dv1 - f * 5.0e-5;            // amorcage grossier
    } else {
      dv1_next = dv1 - f * (dv1 - dv1_prev) / (f - f_prev);
    }
    dv1_prev = dv1; f_prev = f; dv1 = dv1_next;
  }

  // =====================================================================
  // ETAPE 2 — lire l'ETAT REEL a l'apogee et calculer la manoeuvre combinee.
  //  Le jeu fournit l'etat (donnee de navigation). Il ne fournit PAS le dv.
  // =====================================================================
  std::printf("\n--- ETAPE 2 : manoeuvre combinee sur l'etat REEL a l'apogee ---\n");
  const Vec3 r_a = pos(y_apo), v_a = vel(y_apo);
  const auto el_gto = astro::rv_to_elements(r_a, v_a, mu);
  const Basis3 B = rsw_basis(r_a, v_a);
  const double va_mag = norm(v_a);
  const double inc = el_gto.i;

  std::printf("  t_apogee   = t0 + %.3f s   (impulsionnel : %.3f s)\n",
              t_apo - doc.plan.epoch0, 18931.9);
  std::printf("  r_apogee   = %.3f km       v_apogee = %.3f m/s\n", norm(r_a) / 1000, va_mag);
  std::printf("  inclinaison reelle = %.6f deg\n", inc / DEG);
  std::printf("  composante radiale de v a l'apogee : %.4f m/s (doit etre ~0)\n",
              dot(v_a, B.R));

  // Vitesse cible : circulaire equatoriale, meme sens, meme rayon.
  const Vec3 v_target = unit(cross(Vec3{0, 0, 1}, r_a)) * V_GEO;
  const Vec3 dv2_in   = v_target - v_a;
  const Vec3 dv2_rsw  = inertial_to_rsw(B, dv2_in);

  std::printf("  dv2 (RSW)  = [%.3f, %.3f, %.3f] m/s   |dv2| = %.3f m/s\n",
              dv2_rsw.x, dv2_rsw.y, dv2_rsw.z, norm(dv2_rsw));
  std::printf("  verification analytique sqrt(va^2+vgeo^2-2 va vgeo cos i) = %.3f m/s\n",
              astro::dv_combined(va_mag, V_GEO, inc));

  // =====================================================================
  // ETAPE 3 — VERIFIER contre le CONTRAT, pas contre un chiffre esthetique.
  //
  // Piege dans lequel on tombe naturellement : chercher e = 0 exactement.
  // C'est IMPOSSIBLE avec un seul arc fini : la poussee s'etale sur ~48 s
  // autour de l'apogee, elle laisse donc une signature irreductible
  // (e ~ 7e-5, i ~ 0.07 deg). Aucune mise a l'echelle de |dv2| ne l'annulera :
  // il faudrait aussi bouger la direction ET l'instant, i.e. resoudre un
  // probleme a 4 inconnues. Or le CONTRAT demande e < 2e-3 et i < 0.25 deg.
  // La physique dit "assez". On s'arrete.
  std::printf("\n--- ETAPE 3 : verification contre le contrat ---\n");
  flight::FlightPlan p = doc.plan;
  p.burns[0].dv = Vec3{0, dv1, 0};
  p.burns[1].t  = t_apo;
  p.burns[1].dv = dv2_rsw;
  p.t_stop = t_apo + 6000.0;
  auto rep = flight::execute(p, eph, 0, opt);
  if (!rep.ok) { std::printf("  echec : %s\n", rep.failure.c_str()); return 1; }
  const auto el = astro::rv_to_elements(pos(rep.truth.y_final), vel(rep.truth.y_final), mu);
  std::printf("  a = %11.3f km  (cible 42164.170 +/- 50)      %s\n", el.a / 1000,
              std::fabs(el.a - R_GEO) < 50e3 ? "OK" : "RATE");
  std::printf("  e = %.6f      (cible 0 +/- 0.002)            %s\n", el.e,
              el.e < 2e-3 ? "OK" : "RATE");
  std::printf("  i = %.5f deg  (cible 0 +/- 0.25)             %s\n", el.i / DEG,
              el.i / DEG < 0.25 ? "OK" : "RATE");
  std::printf("  ergols consommes : %.1f kg   restants (utilisables) : %.1f kg\n",
              rep.total_propellant,
              doc.plan.vehicle.stages[0].tank.usable() - rep.total_propellant);
  const flight::FlightPlan& best = p;

  // =====================================================================
  std::printf("\n=== PLAN DE VOL CONVERGE ===========================================\n");
  std::printf("BURN id=GTO t=0s frame=RSW hold=INERTIAL dv=[0,%.1f,0]m/s stage=0\n", dv1);
  std::printf("BURN id=GOI t=%.1fs frame=RSW hold=INERTIAL dv=[%.1f,%.1f,%.1f]m/s stage=0\n",
              t_apo - doc.plan.epoch0,
              best.burns[1].dv.x, best.burns[1].dv.y, best.burns[1].dv.z);
  std::printf("dv total commande : %.1f m/s   (impulsionnel ideal : 4291.1 m/s)\n",
              dv1 + norm(best.burns[1].dv));
  std::printf("SURCOUT DE LA POUSSEE FINIE : %.1f m/s\n",
              dv1 + norm(best.burns[1].dv) - 4291.1);
  return 0;
}
