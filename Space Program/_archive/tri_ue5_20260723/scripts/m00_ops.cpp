// scripts/m00_ops.cpp
//
// LA BOUCLE COMPLETE, en boucle fermee.
//   conception -> COMMIT -> propagation -> NAVIGATION -> CORRECTION -> resultat
//
// Ce que ce programme demontre :
//   - un plan rigide a 2 manoeuvres tient les objectifs 5 % du temps (mesure) ;
//   - le meme vehicule, avec la meme graine, les tient ~100 % du temps si le
//     joueur RE-CALCULE ses manoeuvres a partir des donnees de navigation.
//   - le surcout est mesurable en kg, et il doit etre budgete AVANT le commit.
//
// Aucune "aide" du moteur nulle part : Session::observe() rend un etat, et c'est
// tout. Chaque Delta-v ci-dessous sort d'une equation ecrite ici, pas du jeu.
//
// Sequence (c'est celle d'un vrai satellite GEO : injection, moteur d'apogee,
// puis manoeuvres de derive) :
//   1. GTO      : injection prograde au perigee.
//   2. AMF      : a l'apogee OBSERVEE, une seule impulsion qui (a) annule
//                 l'inclinaison et (b) place le perigee EXACTEMENT sur R_GEO.
//                 L'apogee reste ou elle est : on ne cherche pas a la corriger,
//                 ce serait payer deux fois.
//   3. TRIM     : au perigee suivant (r = R_GEO, et c'est un noeud par
//                 construction), on circularise. Cout : quelques dizaines de m/s.
#include <cstdio>
#include <cmath>
#include <vector>
#include "fen/io/Fpl.hpp"
#include "fen/flight/Session.hpp"
#include "fen/astro/Elements.hpp"
#include "fen/astro/Transfers.hpp"
#include "fen/nav/Statistics.hpp"

using namespace fen;
using namespace fen::cst;

struct Outcome { bool ok; double a, e, i, prop, dv_corr; };

static Outcome fly(const io::FplDocument& doc, const ephem::IEphemeris& eph,
                   std::uint64_t seed, bool verbose) {
  const double mu    = ephem::body_mu(doc.plan.center);
  const double R_GEO = 42164170.0;

  prop::PropOptions opt;
  opt.step.rtol = 1e-11;
  opt.sample_dt = 0.0;

  flight::Session S(doc.plan, eph, seed, opt);
  const double t0 = doc.plan.epoch0;
  Outcome out{false, 0, 0, 0, 0, 0};
  double dv_corrections = 0.0;

  auto say = [&](const char* fmt, auto... a) { if (verbose) std::printf(fmt, a...); };

  // Une seule equation, ecrite par le joueur, reutilisee trois fois :
  // "depuis l'apsis ou je suis, quelle vitesse me donne un peri-apsis a R_GEO,
  //  dans le plan equatorial, en gardant l'autre apsis la ou il est ?"
  auto target_from_apsis = [&](const Vec3& r, double r_other_apsis) {
    const double a_new = 0.5 * (norm(r) + r_other_apsis);
    const double v_mag = astro::vis_viva(norm(r), a_new, mu);
    return unit(cross(Vec3{0, 0, 1}, r)) * v_mag;   // prograde, equatorial
  };

  // PIEGE OPERATIONNEL REEL, et il faut le connaitre :
  // le point ou l'on vient de bruler RESTE un apsis. L'erreur de pointage de
  // Gates y laisse une vitesse radiale infime, donc le detecteur d'evenements
  // retrouve un apsis a quelques secondes de la, au MEME rayon. Si on l'utilise,
  // on corrige l'apsis qu'on vient de fixer au lieu de l'oppose : mesure, 41 %
  // d'echecs. L'apsis oppose est a T/2 : on saute d'abord 0.4 T.
  auto goto_opposite_apsis = [&]() -> double {
    const auto ob = S.truth_state();
    const auto e  = astro::rv_to_elements(ob.r, ob.v, mu);
    const double Torb = astro::orbital_period(e.a, mu);
    if (!S.advance_to(S.t() + 0.40 * Torb)) return std::nan("");
    return S.advance_to_any_event({"APOAPSIS", "PERIAPSIS"}, S.t() + 0.8 * Torb);
  };

  auto do_burn = [&](const char* id, double t, const Vec3& v_target, const Vec3& v_now) {
    flight::BurnCmd b;
    b.id = id; b.t = t; b.frame = flight::DvFrame::Inertial;
    b.hold = force::ThrustFrame::InertialFixed; b.stage = 0;
    b.dv = v_target - v_now;
    S.commit_burn(b);
    return norm(b.dv);
  };

  // ---- 1) GTO : injection (valeur issue de la conception hors ligne) --------
  S.commit_burn(doc.plan.burns[0]);
  if (!S.alive()) return out;

  // ---- 2) NAV -> AMF : plan + perigee, depuis l'apogee REELLE ---------------
  double t_ev = S.advance_to_event("APOAPSIS", t0 + 30000.0);
  if (!std::isfinite(t_ev) || !S.alive()) return out;
  auto o = S.truth_state();
  auto el = astro::rv_to_elements(o.r, o.v, mu);
  say("\n[NAV 1] apogee reelle t0+%.1f s : r=%.3f km (ecart %+.3f km)  i=%.5f deg\n",
      t_ev - t0, norm(o.r) / 1000, (norm(o.r) - R_GEO) / 1000, el.i / DEG);
  double dv = do_burn("AMF", t_ev, target_from_apsis(o.r, R_GEO), o.v);
  if (!S.alive()) return out;
  say("[AMF]   dv = %.2f m/s  (nominal impulsionnel : 1836.5)\n", dv);
  dv_corrections += dv - 1836.5;

  // ---- 3) NAV -> AMF2 : l'erreur d'execution de l'AMF a deplace le perigee. --
  // C'EST LA LECON : la precision finale est fixee par la taille de la DERNIERE
  // manoeuvre. On en ajoute donc une PETITE, apres avoir mesure.
  o = S.truth_state();
  el = astro::rv_to_elements(o.r, o.v, mu);
  (void)astro::orbital_period(el.a, mu);
  say("[NAV 2] apres AMF : a=%.3f km  e=%.6f  i=%.5f deg  rp=%.3f km (ecart %+.3f km)\n",
      el.a / 1000, el.e, el.i / DEG, el.rp / 1000, (el.rp - R_GEO) / 1000);

  // On vise l'apsis OPPOSE, quel qu'il soit : le tirage de Gates a pu inverser
  // perigee et apogee. Chercher "APOAPSIS" en dur, c'est se tromper de cible
  // une fois sur deux (mesure : 16.5 %% d'echecs).
  t_ev = goto_opposite_apsis();
  if (!std::isfinite(t_ev) || !S.alive()) return out;
  o = S.truth_state();
  say("[NAV 2b] apsis OPPOSE t0+%.1f s : r=%.3f km (ecart %+.3f km)\n",
      t_ev - t0, norm(o.r) / 1000, (norm(o.r) - R_GEO) / 1000);
  dv = do_burn("AMF2", t_ev, target_from_apsis(o.r, R_GEO), o.v);
  if (!S.alive()) return out;
  say("[AMF2]  dv = %.2f m/s  <- petite manoeuvre = bonne precision terminale\n", dv);
  dv_corrections += dv;

  // ---- 4) NAV -> TRIM : circularisation au perigee (r ~ R_GEO, et c'est un noeud)
  t_ev = goto_opposite_apsis();
  if (!std::isfinite(t_ev) || !S.alive()) return out;
  o = S.truth_state();
  say("[NAV 3] apsis a R_GEO, t0+%.1f s : r=%.3f km (ecart %+.3f km)\n",
      t_ev - t0, norm(o.r) / 1000, (norm(o.r) - R_GEO) / 1000);
  const Vec3 v_circ = unit(cross(Vec3{0, 0, 1}, o.r))
                      * astro::v_circular(norm(o.r), mu);
  dv = do_burn("TRIM", t_ev, v_circ, o.v);
  if (!S.alive()) return out;
  say("[TRIM]  dv = %.2f m/s\n", dv);
  dv_corrections += dv;

  S.advance_to(S.t() + 3000.0);
  o = S.truth_state();
  el = astro::rv_to_elements(o.r, o.v, mu);

  say("\n[RESULTAT] a=%.3f km (ecart %+.3f)  e=%.6f  i=%.5f deg\n",
      el.a / 1000, (el.a - R_GEO) / 1000, el.e, el.i / DEG);
  say("           ergols %.1f kg | restants %.1f kg | dv de reserve %.1f m/s\n",
      S.report().total_propellant, S.usable_propellant_remaining(0), S.dv_remaining(0));
  say("           SURCOUT TOTAL DES CORRECTIONS : %.1f m/s\n", dv_corrections);

  out.a = el.a; out.e = el.e; out.i = el.i;
  out.prop = S.report().total_propellant;
  out.dv_corr = dv_corrections;
  out.ok = std::fabs(el.a - R_GEO) < 50e3 && el.e < 2e-3 && el.i / DEG < 0.25;
  return out;
}

int main(int argc, char** argv) {
  const std::string path = (argc > 1) ? argv[1] : "missions/m00_geo_solution.fpl";
  const int N = (argc > 2) ? std::atoi(argv[2]) : 400;
  auto doc = io::parse_fpl(path);
  ephem::StandishEphemeris eph;

  std::printf("=====================================================================\n");
  std::printf(" M00 — BOUCLE FERMEE, NAVIGATION PARFAITE\n");
  std::printf(" (borne superieure NON ACHETABLE : voir m00_nav pour le vrai prix)\n");
  std::printf("=====================================================================\n");
  std::printf("\n### VOL DETAILLE (graine 4071 — celle qui faisait echouer le plan rigide)\n");
  fly(doc, eph, 4071, true);

  std::printf("\n\n### MONTE-CARLO (%d graines)\n", N);
  std::vector<double> A, E, I, P, DV;
  int ok = 0;
  for (int k = 0; k < N; ++k) {
    auto o = fly(doc, eph, 900 + static_cast<std::uint64_t>(k), false);
    if (o.prop <= 0) continue;
    A.push_back(o.a / 1000); E.push_back(o.e); I.push_back(o.i / DEG);
    P.push_back(o.prop); DV.push_back(o.dv_corr);
    if (o.ok) ++ok;
  }
  auto sa = nav::summarize(A), se = nav::summarize(E), si = nav::summarize(I);
  auto sp = nav::summarize(P), sd = nav::summarize(DV);
  std::printf("  a  : moy %10.3f km  sigma %8.3f  p99 %10.3f\n", sa.mean, sa.sigma, sa.p99);
  std::printf("  e  : moy %10.6f     sigma %8.6f  p99 %10.6f\n", se.mean, se.sigma, se.p99);
  std::printf("  i  : moy %10.5f deg sigma %8.5f  p99 %10.5f\n", si.mean, si.sigma, si.p99);
  std::printf("  ergols : moy %8.1f kg  sigma %5.1f  p99 %8.1f  (charges utiles : %.1f)\n",
              sp.mean, sp.sigma, sp.p99, doc.plan.vehicle.stages[0].tank.usable());
  std::printf("\n  COUT DES CORRECTIONS : moy %.1f m/s | p95 %.1f | p99 %.1f  <- LE BUDGET DE MARGE\n",
              sd.mean, sd.p95, sd.p99);
  std::printf("  P(objectifs tenus) = %.1f %%   (%d/%zu)\n", 100.0 * ok / (double)A.size(),
              ok, A.size());
  std::printf("\n  >>> Comparer a la boucle OUVERTE (plan rige a 2 manoeuvres) : 5.2 %%.\n");
  std::printf("  >>> Meme vehicule. Meme graine. Meme physique. La difference,\n");
  std::printf("      c'est que le joueur a RECALCULE au lieu de PRIER.\n");
  return 0;
}
