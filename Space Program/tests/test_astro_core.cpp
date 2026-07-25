// tests/test_astro_core.cpp
// ORACLES PHYSIQUES. Le noyau n'est pas "testé" par des assertions inventées :
// il est confronté à des invariants (énergie, réversibilité, cohérence croisée
// entre deux méthodes indépendantes) et à des valeurs de référence publiées.
//
// Un test qui passe pour une mauvaise raison est pire que pas de test.
// Chaque cas ci-dessous dit POURQUOI il ne peut pas passer par accident.
#include <cstdio>
#include <cmath>
#include <vector>
#include <string>

#include "fen/core/Constants.hpp"
#include "fen/core/Epoch.hpp"
#include "fen/core/Rng.hpp"
#include "fen/astro/Kepler.hpp"
#include "fen/astro/Elements.hpp"
#include "fen/astro/Lambert.hpp"
#include "fen/astro/Transfers.hpp"
#include "fen/astro/BPlane.hpp"
#include "fen/ephem/Ephemeris.hpp"
#include "fen/force/Forces.hpp"
#include "fen/prop/Propagator.hpp"
#include "fen/prop/Ias15.hpp"
#include "fen/vehicle/Vehicle.hpp"
#include "fen/nav/Gates.hpp"
#include "fen/nav/Statistics.hpp"
#include "fen/nav/OrbitDetermination.hpp"
#include "fen/core/Matrix.hpp"
#include "fen/astro/Porkchop.hpp"
#include "fen/astro/LaunchWindow.hpp"
#include "fen/ephem/BodyOrientation.hpp"
#include "fen/astro/Flyby.hpp"
#include "fen/astro/Mga.hpp"
#include "fen/astro/Mga1Dsm.hpp"
#include "fen/astro/LocalRefine.hpp"

using namespace fen;
using namespace fen::cst;

static int g_fail = 0, g_pass = 0;

#define CHECK(cond, msg)                                                        \
  do {                                                                          \
    if (cond) { ++g_pass; }                                                     \
    else { ++g_fail; std::printf("  [FAIL] %s  (%s:%d)\n", msg, __FILE__, __LINE__); } \
  } while (0)

#define CHECK_NEAR(a, b, tol, msg)                                              \
  do {                                                                          \
    const double _d = std::fabs((a) - (b));                                     \
    if (_d <= (tol)) { ++g_pass; }                                              \
    else { ++g_fail; std::printf("  [FAIL] %s : %.12g vs %.12g (ecart %.3g > %.3g)\n", \
                                 msg, (double)(a), (double)(b), _d, (double)(tol)); }   \
  } while (0)

static void section(const char* s) { std::printf("\n== %s ==\n", s); }

// ---------------------------------------------------------------------------
static void test_epoch() {
  section("Epoch (TDB)");
  const Epoch e = epoch_from_iso("2000-01-01T12:00:00");
  CHECK_NEAR(e.tdb, 0.0, 1e-9, "J2000 == 0 s");
  CHECK_NEAR(e.jd(), JD_J2000, 1e-9, "JD(J2000) == 2451545.0");
  const Epoch e2 = epoch_from_iso("2027-03-14T06:30:00");
  const std::string back = epoch_to_iso(e2);
  CHECK(back.substr(0, 19) == "2027-03-14T06:30:00", "aller-retour ISO<->TDB");
}

// ---------------------------------------------------------------------------
static void test_kepler() {
  section("Kepler universel");
  // Orbite elliptique quelconque, non dégénérée.
  astro::Elements el;
  el.a = 24371150.0; el.e = 0.7301; el.i = 28.5 * DEG;
  el.raan = 41.0 * DEG; el.argp = 17.0 * DEG; el.nu = 33.0 * DEG;
  Vec3 r0, v0;
  astro::elements_to_rv(el, MU_EARTH, r0, v0);

  // (1) INVARIANT : une période entière ramène exactement à l'état initial.
  //     Impossible à satisfaire par accident : ça teste Stumpff, l'amorçage,
  //     Newton, ET les coefficients de Lagrange simultanément.
  const double T = astro::orbital_period(el.a, MU_EARTH);
  auto k = astro::kepler_propagate(r0, v0, T, MU_EARTH);
  CHECK(k.converged, "convergence sur une periode");
  CHECK_NEAR(norm(k.r - r0), 0.0, 1e-4, "retour position apres 1 periode (< 0.1 mm)");
  CHECK_NEAR(norm(k.v - v0), 0.0, 1e-9, "retour vitesse apres 1 periode");

  // (2) RÉVERSIBILITÉ : propager +dt puis -dt.
  auto kf = astro::kepler_propagate(r0, v0, 0.37 * T, MU_EARTH);
  auto kb = astro::kepler_propagate(kf.r, kf.v, -0.37 * T, MU_EARTH);
  CHECK_NEAR(norm(kb.r - r0), 0.0, 1e-5, "reversibilite position");

  // (3) HYPERBOLIQUE : mêmes fonctions, aucun branchement de conique.
  Vec3 rh{7000e3, 0, 0};
  Vec3 vh{0, 12000.0, 0};                       // v > v_esc(7000 km) = 10.67 km/s
  const double E = specific_energy(rh, vh, MU_EARTH);
  CHECK(E > 0.0, "cas hyperbolique bien pose");
  auto kh = astro::kepler_propagate(rh, vh, 3600.0, MU_EARTH);
  const double E2 = specific_energy(kh.r, kh.v, MU_EARTH);
  CHECK_NEAR(E2 / E, 1.0, 1e-12, "energie conservee (hyperbole)");
}

// ---------------------------------------------------------------------------
static void test_elements() {
  section("Elements <-> etat");
  Rng rng(20260714);
  double worst_r = 0, worst_v = 0;
  for (int i = 0; i < 2000; ++i) {
    astro::Elements el;
    el.a = 7000e3 + rng.uniform01() * 5e7;
    el.e = rng.uniform01() * 0.9;
    el.i = rng.uniform01() * PI;
    el.raan = rng.uniform01() * TWO_PI;
    el.argp = rng.uniform01() * TWO_PI;
    el.nu = rng.uniform01() * TWO_PI;
    Vec3 r, v;
    astro::elements_to_rv(el, MU_EARTH, r, v);
    const astro::Elements el2 = astro::rv_to_elements(r, v, MU_EARTH);
    Vec3 r2, v2;
    astro::elements_to_rv(el2, MU_EARTH, r2, v2);
    worst_r = std::max(worst_r, norm(r2 - r) / norm(r));
    worst_v = std::max(worst_v, norm(v2 - v) / norm(v));
  }
  CHECK(worst_r < 1e-11, "aller-retour rv->coe->rv : position (2000 tirages)");
  CHECK(worst_v < 1e-11, "aller-retour rv->coe->rv : vitesse");
  std::printf("     pire ecart relatif : r=%.2e  v=%.2e\n", worst_r, worst_v);
}

// ---------------------------------------------------------------------------
static void test_lambert() {
  section("Lambert (Izzo 2014)");
  // ORACLE CROISÉ : on génère (r1, r2, tof) par propagation KÉPLÉRIENNE d'un
  // état connu, puis on demande à Lambert de retrouver v1. Les deux algorithmes
  // sont indépendants (variables universelles vs Householder sur x) : aucune
  // chance qu'un bug commun les fasse coincider.
  Rng rng(31415);
  double worst = 0.0;
  int tested = 0;
  for (int i = 0; i < 500; ++i) {
    astro::Elements el;
    el.a = 8000e3 + rng.uniform01() * 4e7;
    el.e = rng.uniform01() * 0.7;
    el.i = rng.uniform01() * 0.9 * PI;
    el.raan = rng.uniform01() * TWO_PI;
    el.argp = rng.uniform01() * TWO_PI;
    el.nu = rng.uniform01() * TWO_PI;
    Vec3 r1, v1;
    astro::elements_to_rv(el, MU_EARTH, r1, v1);
    const double T = astro::orbital_period(el.a, MU_EARTH);
    const double tof = (0.08 + 0.35 * rng.uniform01()) * T;   // < demi-periode
    auto k = astro::kepler_propagate(r1, v1, tof, MU_EARTH);

    const bool prograde = (cross(r1, v1).z > 0.0);
    auto L = astro::lambert(r1, k.r, tof, MU_EARTH, prograde, 0);
    if (!L.ok) { std::printf("  [FAIL] Lambert non converge (i=%d)\n", i); ++g_fail; continue; }
    const double err = norm(L.solutions[0].v1 - v1) / norm(v1);
    worst = std::max(worst, err);
    ++tested;
  }
  CHECK(worst < 1e-9, "Lambert retrouve v1 (oracle croise Kepler, 500 tirages)");
  std::printf("     %d cas, pire erreur relative sur v1 : %.2e\n", tested, worst);

  // Cas interplanétaire : Terre -> Mars, ordres de grandeur.
  ephem::StandishEphemeris eph;
  const Epoch t0 = epoch_from_iso("2026-11-04T00:00:00");
  const Epoch t1 = t0 + 300.0 * DAY;
  const Vec3 rE = eph.state(ephem::Body::EarthBary, ephem::Body::Sun, t0).r;
  const Vec3 vE = eph.state(ephem::Body::EarthBary, ephem::Body::Sun, t0).v;
  const Vec3 rM = eph.state(ephem::Body::Mars, ephem::Body::Sun, t1).r;
  auto Lm = astro::lambert(rE, rM, 300.0 * DAY, MU_SUN, true, 0);
  CHECK(Lm.ok, "Lambert heliocentrique Terre->Mars converge");
  if (Lm.ok) {
    const double vinf = norm(Lm.solutions[0].v1 - vE);
    const double C3 = astro::C3_from_vinf(vinf) / 1e6;   // km^2/s^2
    std::printf("     Terre->Mars, TOF=300 j : v_inf=%.3f km/s, C3=%.2f km^2/s^2\n",
                vinf / 1000.0, C3);
    CHECK(C3 > 5.0 && C3 < 60.0, "C3 dans la plage physique d'une fenetre Mars");
  }
}

// ---------------------------------------------------------------------------
static void test_transfers() {
  section("Transferts analytiques");
  const double r1 = R_EARTH + 200e3;
  const double r2 = 42164170.0;
  auto h = astro::hohmann(r1, r2, MU_EARTH);
  std::printf("     LEO(200km)->GEO Hohmann : dv1=%.4f  dv2=%.4f  total=%.4f km/s  tof=%.3f h\n",
              h.dv1 / 1000, h.dv2 / 1000, h.dv_total / 1000, h.tof / 3600);
  CHECK_NEAR(h.dv_total / 1000.0, 3.932, 0.005, "Hohmann LEO->GEO ~ 3.93 km/s (valeur de manuel)");

  const double va = astro::vis_viva(r2, h.a_transfer, MU_EARTH);
  const double vg = astro::v_circular(r2, MU_EARTH);
  const double dv_comb = astro::dv_combined(va, vg, 28.5 * DEG);
  const double dv_split = (vg - va) + astro::dv_plane_change(vg, 28.5 * DEG);
  std::printf("     Apogee : combine=%.4f km/s   separe=%.4f km/s   ECONOMIE=%.4f km/s\n",
              dv_comb / 1000, dv_split / 1000, (dv_split - dv_comb) / 1000);
  CHECK(dv_comb < dv_split, "la manoeuvre combinee est STRICTEMENT moins chere");
  CHECK_NEAR(dv_comb / 1000.0, 1.8365, 0.001, "dv combine apogee = 1.8365 km/s");
  CHECK_NEAR((h.dv1 + dv_comb) / 1000.0, 4.2911, 0.002, "total GTO+GOI combine = 4.291 km/s");

  // Tsiolkovski : cohérence directe / inverse.
  const double m0 = astro::m0_for_dv(2000.0, 4291.0, 449.7);
  const double dv = astro::tsiolkovsky_dv(m0, 2000.0, 449.7);
  CHECK_NEAR(dv, 4291.0, 1e-9, "Tsiolkovski direct/inverse coherents");

  // Périodes synodiques
  const double T_E = 365.256 * DAY, T_M = 686.980 * DAY;
  const double S = astro::synodic_period(T_E, T_M) / DAY;
  std::printf("     Periode synodique Terre-Mars : %.2f jours (%.2f mois)\n", S, S / 30.44);
  CHECK_NEAR(S, 779.9, 1.0, "synodique Terre-Mars = 779.9 j");

  // --- Injection hyperbolique (Oberth) : v_inf porkchop -> Δv reel du vehicule.
  // IDENTITE : v_inf = 0 doit redonner exactement le Δv d'echappement.
  const double v_esc_leo = astro::v_escape(r1, MU_EARTH) - astro::v_circular(r1, MU_EARTH);
  CHECK_NEAR(astro::injection_dv_from_circular(0.0, r1, MU_EARTH), v_esc_leo, 1e-9,
             "Oberth : v_inf=0 redonne le Δv d'echappement (racine2-1)*v_circ");
  CHECK_NEAR(v_esc_leo / 1000.0, 3.224, 0.01, "echappement LEO 200km ~ 3.22 km/s");

  // Depart Terre->Mars : C3 ~ 8.7 km2/s2 (v_inf ~ 2.95 km/s) depuis 200 km LEO.
  // L'effet Oberth ramene 2.95 km/s helio a ~3.6 km/s REELS (et non 2.95 ajoutes).
  const double vinf_dep = std::sqrt(8.7e6);
  const double dv_inj = astro::injection_dv_from_circular(vinf_dep, r1, MU_EARTH);
  std::printf("     Injection Terre->Mars (C3=8.7) depuis LEO : %.0f m/s\n", dv_inj);
  CHECK(dv_inj > 3400.0 && dv_inj < 3800.0, "injection Mars depuis LEO ~ 3.6 km/s (manuel)");
  CHECK(dv_inj > vinf_dep, "l'injection depasse v_inf (mais reste bien sous v_circ+v_inf) : Oberth");
  CHECK(dv_inj < astro::v_circular(r1, MU_EARTH) + vinf_dep,
        "et reste strictement sous l'ajout naif v_circ + v_inf");

  // Monotonie : plus de v_inf coute plus cher a injecter.
  CHECK(astro::injection_dv_from_circular(4000.0, r1, MU_EARTH) >
        astro::injection_dv_from_circular(2000.0, r1, MU_EARTH),
        "injection monotone croissante en v_inf");

  // Insertion martienne : v_inf_arr ~ 2.5 km/s. Une capture ELLIPTIQUE coute
  // strictement moins qu'une circularisation basse — c'est le choix reel.
  const double rp_mars = R_MARS + 400e3;
  const double ra_mars = R_MARS + 30000e3;   // orbite tres elliptique de capture
  const double dv_circ = astro::capture_dv_to_circular(2500.0, rp_mars, MU_MARS);
  const double dv_ell  = astro::capture_dv_to_ellipse(2500.0, rp_mars, ra_mars, MU_MARS);
  std::printf("     Capture Mars (v_inf=2.5) : circulaire=%.0f m/s  elliptique=%.0f m/s\n",
              dv_circ, dv_ell);
  CHECK(dv_circ > 1500.0 && dv_circ < 2500.0, "capture circulaire Mars ~ 2 km/s");
  CHECK(dv_ell < dv_circ, "capture elliptique STRICTEMENT moins chere que circulaire");
}

// ---------------------------------------------------------------------------
static void test_propagator() {
  section("Propagateur de verite (DOPRI5 + evenements)");
  astro::Elements el;
  el.a = 24371150.0; el.e = 0.7301; el.i = 28.5 * DEG;
  el.raan = 0; el.argp = 0; el.nu = 0;
  Vec3 r0, v0;
  astro::elements_to_rv(el, MU_EARTH, r0, v0);
  const double T = astro::orbital_period(el.a, MU_EARTH);

  force::ForceStack fs;
  fs.add(std::make_shared<force::CentralGravity>(MU_EARTH));

  prop::PropOptions opt;
  opt.step.rtol = 1e-13;
  opt.step.atol = 1e-6;
  opt.sample_dt = 0.0;

  // (1) ORACLE CROISÉ : DOPRI5 vs Kepler analytique sur 20 périodes.
  const StateN y0{r0.x, r0.y, r0.z, v0.x, v0.y, v0.z, 1000.0};
  auto res = prop::propagate(fs, 0.0, y0, 20.0 * T, {}, opt);
  auto kex = astro::kepler_propagate(r0, v0, 20.0 * T, MU_EARTH);
  const double dr = norm(pos(res.y_final) - kex.r);
  std::printf("     20 periodes : ecart DOPRI5 vs Kepler = %.3e m  (%lld pas)\n",
              dr, res.steps_accepted);
  CHECK(dr < 1.0, "DOPRI5 == Kepler a mieux que 1 m sur 20 periodes");

  // (2) INVARIANT : conservation de l'énergie + erreur de POSITION sur 1 an.
  //
  // HONNETETE DU CRITERE. Le critere qui compte n'est pas "dE/E < 1e-12" (chiffre
  // esthetique) : c'est "l'erreur d'integration est negligeable devant le signal
  // physique que le joueur doit gerer". Ce signal, c'est l'ellipse de dispersion
  // de Gates : ~1e4 km dans le plan-B. On exige donc que l'erreur d'integration
  // soit < 1e-3 km sur un an, soit 7 ordres de grandeur sous le signal.
  // On MESURE dE/E et on le publie ; DP5 plafonne vers 1e-9/1e-8 sur 1300 orbites.
  // Atteindre 1e-12 exigerait IAS15 / DOP853 (PHASE 1b) : l'interface ne bougera pas.
  const double E0 = specific_energy(r0, v0, MU_EARTH);
  auto res2 = prop::propagate(fs, 0.0, y0, 365.25 * DAY, {}, opt);
  const double E1 = specific_energy(pos(res2.y_final), vel(res2.y_final), MU_EARTH);
  const double dE = std::fabs((E1 - E0) / E0);
  auto kex_1y = astro::kepler_propagate(r0, v0, 365.25 * DAY, MU_EARTH);
  const double dr_1y = norm(pos(res2.y_final) - kex_1y.r);
  const int n_orb = static_cast<int>(365.25 * DAY / T);
  std::printf("     1 an (%d orbites) : |dE/E| = %.3e ; ecart position vs Kepler = %.3f m"
              " (%lld pas)\n", n_orb, dE, dr_1y, res2.steps_accepted);
  std::printf("     -> a comparer a l'ellipse de dispersion Gates : ~1e7 m. Rapport : %.1e\n",
              dr_1y / 1e7);
  CHECK(dE < 1e-8, "derive d'energie DP5 bornee (diagnostic, cible IAS15 : 1e-12)");
  CHECK(dr_1y < 1000.0, "erreur d'integration < 1 km sur 1 an : negligeable devant la dispersion");

  // (3) ÉVÉNEMENTS : périastre / apoastre détectés par racine, pas par pas.
  prop::PropOptions o3 = opt;
  auto res3 = prop::propagate(fs, 0.0, y0, 3.0 * T, {prop::event_periapsis(MU_EARTH),
                                                     prop::event_apoapsis(MU_EARTH)}, o3);
  int n_peri = 0, n_apo = 0;
  double t_first_apo = -1;
  for (const auto& e : res3.events) {
    if (e.name == "PERIAPSIS") ++n_peri;
    if (e.name == "APOAPSIS") { ++n_apo; if (t_first_apo < 0) t_first_apo = e.t; }
  }
  std::printf("     sur 3 periodes : %d periastres, %d apoastres ; 1er apoastre a t=%.6f s (T/2=%.6f)\n",
              n_peri, n_apo, t_first_apo, T / 2);
  CHECK(n_apo == 3, "3 apoastres detectes sur 3 periodes");
  CHECK_NEAR(t_first_apo, T / 2.0, 1e-4, "1er apoastre exactement a T/2 (racine, pas pas de temps)");

  // (4) RÉTRO-PROPAGATION exacte (requise par l'executeur : arc centre).
  auto back = prop::propagate(fs, 20.0 * T, res.y_final, 0.0, {}, opt);
  CHECK_NEAR(norm(pos(back.y_final) - r0), 0.0, 1.0, "retro-propagation revient a l'origine (<1 m)");
}

// ---------------------------------------------------------------------------
static void test_nbody_battin() {
  section("Tiers-corps (formulation f(q) de Battin)");
  ephem::StandishEphemeris eph;
  // Vérification que f(q) reproduit la formule naïve quand celle-ci est valide
  // (|r| comparable à |s|), et qu'elle NE perd PAS de precision quand |r| << |s|.
  const Epoch t = epoch_from_iso("2027-03-14T00:00:00");
  const Vec3 s = eph.state(ephem::Body::Sun, ephem::Body::EarthBary, t).r; // Soleil rel. Terre
  const double mu3 = MU_SUN;

  auto a_battin = [&](const Vec3& r) {
    const Vec3 d = r - s;
    const double s2 = norm2(s);
    const double q = dot(r, r - 2.0 * s) / s2;
    const double opq = 1.0 + q;
    const double fq = q * (3.0 + 3.0 * q + q * q) / (1.0 + opq * std::sqrt(opq));
    const double dn = norm(d);
    return (r + s * fq) * (-mu3 / (dn * dn * dn));
  };
  auto a_naive = [&](const Vec3& r) {
    const Vec3 d = r - s;
    const double dn = norm(d), sn = norm(s);
    return (d / (dn * dn * dn) + s / (sn * sn * sn)) * (-mu3);
  };

  // r ~ 1e7 m, |s| ~ 1.5e11 m -> rapport 1e-4 : la formule naive perd ~8 chiffres.
  const Vec3 r{7.0e6, 3.0e6, 1.0e6};
  const Vec3 ab = a_battin(r), an = a_naive(r);
  const double rel = norm(ab - an) / norm(ab);
  std::printf("     |a_Battin| = %.6e m/s^2 ; ecart relatif avec la formule naive = %.2e\n",
              norm(ab), rel);
  CHECK(norm(ab) > 1e-8 && norm(ab) < 1e-5, "ordre de grandeur de la maree solaire en LEO");
  CHECK(rel < 1e-5, "les deux formes coincident (a la precision de la naive)");

  // L'intérêt de Battin : l'accélération ne doit PAS diverger quand r -> 0.
  const Vec3 a0 = a_battin(Vec3{1.0, 0.0, 0.0});
  CHECK(norm(a0) < 1e-12, "a_tiers -> 0 quand r -> 0 (pas d'annulation catastrophique)");
}

// ---------------------------------------------------------------------------
static void test_ephemeris() {
  section("Ephemerides Standish");
  ephem::StandishEphemeris eph;
  const Epoch t = epoch_from_iso("2000-01-01T12:00:00");
  auto E = eph.state(ephem::Body::EarthBary, ephem::Body::Sun, t);
  const double rE = norm(E.r) / AU;
  const double vE = norm(E.v);
  std::printf("     Terre a J2000 : r = %.6f UA, v = %.1f m/s\n", rE, vE);
  CHECK_NEAR(rE, 0.9833, 0.002, "Terre proche du perihelie debut janvier (0.983 UA)");
  CHECK_NEAR(vE, 30290.0, 200.0, "vitesse orbitale terrestre au perihelie ~30.3 km/s");

  auto M = eph.state(ephem::Body::Mars, ephem::Body::Sun, t);
  const double rM = norm(M.r) / AU;
  std::printf("     Mars a J2000  : r = %.6f UA\n", rM);
  CHECK(rM > 1.38 && rM < 1.67, "Mars entre perihelie (1.381) et aphelie (1.666)");

  // La période synodique doit tomber de l'éphéméride elle-même.
  double last = 1e9, best_dt = 0;
  const Epoch t0 = epoch_from_iso("2026-01-01T00:00:00");
  double t_opp1 = -1, t_opp2 = -1;
  for (int d = 0; d < 1800; ++d) {
    const Epoch tt = t0 + d * DAY;
    const Vec3 re = eph.state(ephem::Body::EarthBary, ephem::Body::Sun, tt).r;
    const Vec3 rm = eph.state(ephem::Body::Mars, ephem::Body::Sun, tt).r;
    const double ang = std::acos(std::fmin(1.0, dot(unit(re), unit(rm))));
    if (ang < last) { last = ang; best_dt = d; }
    else if (last < 0.2 && t_opp1 < 0) { t_opp1 = best_dt; last = 1e9; }
    else if (last < 0.2 && t_opp1 >= 0 && t_opp2 < 0 && best_dt > t_opp1 + 500) {
      t_opp2 = best_dt; break;
    }
    if (ang > last && last < 1e8) last = ang;   // reset de la recherche de minimum
  }
  std::printf("     (oppositions detectees : t1=%.0f j, t2=%.0f j)\n", t_opp1, t_opp2);
}

// ---------------------------------------------------------------------------
static void test_bplane() {
  section("Plan-B");
  // Hyperbole d'arrivée à Mars, v_inf = 2.7 km/s, périastre visé 400 km d'altitude
  const double vinf = 2700.0;
  const double rp_target = R_MARS + 400e3;
  const double b = astro::b_from_rp(rp_target, vinf, MU_MARS);
  const double rp_back = astro::rp_from_b(b, vinf, MU_MARS);
  std::printf("     v_inf=2.7 km/s, r_p vise=%.1f km -> parametre d'impact b=%.1f km\n",
              rp_target / 1000, b / 1000);
  CHECK_NEAR(rp_back, rp_target, 1e-3, "b <-> r_p reversible");
  CHECK(b > rp_target, "b > r_p (focalisation gravitationnelle)");

  // Sensibilité : de combien bouge r_p si b bouge de 100 km ? -> c'est CE chiffre
  // que le joueur doit comparer a son ellipse de dispersion.
  const double drp = astro::rp_from_b(b + 100e3, vinf, MU_MARS) - rp_target;
  std::printf("     db = +100 km  ->  dr_p = %+.1f km   (gain %.2f)\n", drp / 1000, drp / 100e3);
  CHECK(drp > 0 && drp < 100e3, "dr_p/db < 1 : la gravite amortit l'erreur d'impact");
}

// ---------------------------------------------------------------------------
static void test_gates_and_rng() {
  section("Gates + RNG deterministe");
  Rng a(12345), b(12345);
  bool same = true;
  for (int i = 0; i < 1000; ++i) if (a.normal() != b.normal()) same = false;
  CHECK(same, "meme graine -> meme sequence, BIT A BIT");

  nav::GatesParams gp;   // 0.02 m/s + 0.2 % ; 0.02 m/s + 0.15 %
  const Vec3 dv{0.0, 2454.6, 0.0};
  std::vector<double> mags;
  Rng rng(777);
  for (int i = 0; i < 20000; ++i) {
    const Vec3 e = nav::apply_gates(dv, gp, rng) - dv;
    mags.push_back(norm(e));
  }
  auto st = nav::summarize(mags);
  const double sig_theory = nav::gates_sigma_total(norm(dv), gp);
  std::printf("     |dv|=2454.6 m/s : erreur moyenne=%.3f m/s, p99=%.3f m/s ; sigma_theorique=%.3f m/s\n",
              st.mean, st.p99, sig_theory);
  // Pour un vecteur gaussien 3D anisotrope, E[|e|] ~ 1.6 sigma_moyen ; on vérifie
  // seulement la cohérence d'ordre de grandeur et la MONOTONIE en |dv|.
  CHECK(st.p99 > st.mean && st.mean > 0, "distribution non degeneree");
  const double sig_small = nav::gates_sigma_total(10.0, gp);
  const double sig_big   = nav::gates_sigma_total(2454.6, gp);
  std::printf("     sigma(10 m/s)=%.3f   sigma(2454.6 m/s)=%.3f   -> ratio %.1f\n",
              sig_small, sig_big, sig_big / sig_small);
  CHECK(sig_big > 10.0 * sig_small,
        "le dv statistique croit avec le dv nominal (raison economique de minimiser dv)");
}

// ---------------------------------------------------------------------------
static void test_vehicle() {
  section("Vehicule / dimensionnement");
  vehicle::Engine rl10;
  rl10.id = "RL10C-1"; rl10.thrust_vac = 101800.0; rl10.isp_vac = 449.7;
  rl10.mass = 190.0; rl10.mixture_ratio = 5.88;

  // Point fixe masse <-> Delta-v : le joueur DOIT le converger lui-meme.
  auto s = vehicle::size_stage_for_dv(4450.0, 1200.0, rl10, 0.12, 150.0, 0.02);
  std::printf("     dv=4450 m/s, CU=1200 kg, LH2/LOX : ergols=%.0f kg, sec=%.0f kg, m0=%.0f kg\n",
              s.propellant, s.stage_dry, s.m0);
  CHECK(s.converged, "point fixe de dimensionnement converge");

  // Vérification par Tsiolkovski direct.
  const double mf = s.m0 - s.propellant * (1.0 - 0.02);
  const double dv_check = astro::tsiolkovsky_dv(s.m0, mf, rl10.isp_vac);
  CHECK_NEAR(dv_check, 4450.0, 1e-6, "le dimensionnement reproduit exactement Tsiolkovski");

  // Sanction physique : avec le changement de plan SÉPARÉ (5.60 km/s) la masse explose.
  auto s2 = vehicle::size_stage_for_dv(5600.0, 1200.0, rl10, 0.12, 150.0, 0.02);
  std::printf("     dv=5600 m/s (plan separe) : m0=%.0f kg  ->  +%.0f kg (+%.0f %%)\n",
              s2.m0, s2.m0 - s.m0, 100.0 * (s2.m0 / s.m0 - 1.0));
  CHECK(s2.m0 > 1.4 * s.m0, "le mauvais design coute >40 % de masse : la sanction est physique");
}

// ---------------------------------------------------------------------------
static void test_stm_and_od() {
  section("Matrice de transition d'etat + determination d'orbite");
  astro::Elements el;
  el.a = 24371150.0; el.e = 0.7301; el.i = 28.5 * DEG;
  el.raan = 0; el.argp = 0; el.nu = 0;
  Vec3 r0, v0;
  astro::elements_to_rv(el, MU_EARTH, r0, v0);
  const StateN y0{r0.x, r0.y, r0.z, v0.x, v0.y, v0.z, 2000.0};
  const double T = astro::orbital_period(el.a, MU_EARTH);

  force::ForceStack fs;
  fs.add(std::make_shared<force::CentralGravity>(MU_EARTH));
  prop::PropOptions opt;
  opt.step.rtol = 1e-12;

  const double t1 = 0.37 * T;
  const Mat6 Phi = nav::stm(fs, 0.0, y0, t1, opt);

  // ORACLE 1 — SYMPLECTICITE. Le flot d'un systeme hamiltonien preserve la
  // forme symplectique : Phi^T J Phi = J, avec J = [[0, I], [-I, 0]].
  // C'est un invariant de STRUCTURE : une STM fausse ne peut pas le satisfaire
  // par accident. Il teste simultanement le gradient de gravite, l'integrateur
  // et le schema de differences finies.
  Mat6 J = Mat6::zero();
  for (int i = 0; i < 3; ++i) { J.m[i][3 + i] = 1.0; J.m[3 + i][i] = -1.0; }
  const Mat6 R = Phi.transpose() * J * Phi;
  double worst = 0.0;
  for (int i = 0; i < 6; ++i)
    for (int j = 0; j < 6; ++j)
      worst = std::max(worst, std::fabs(R.m[i][j] - J.m[i][j]));
  std::printf("     |Phi^T J Phi - J|_max = %.3e   (symplecticite)\n", worst);
  CHECK(worst < 1e-4, "la STM est SYMPLECTIQUE (invariant hamiltonien)");

  // ORACLE 2 — la STM predit bien une perturbation finie.
  StateN yp = y0;
  yp[0] += 300.0; yp[4] += 0.3;
  const auto ref = prop::propagate(fs, 0.0, y0, t1, {}, opt);
  const auto per = prop::propagate(fs, 0.0, yp, t1, {}, opt);
  Vec6 dx0{}; dx0[0] = 300.0; dx0[4] = 0.3;
  const Vec6 pred = Phi * dx0;
  double err = 0.0, mag = 0.0;
  for (int i = 0; i < 3; ++i) {
    const double truth = per.y_final[i] - ref.y_final[i];
    err = std::max(err, std::fabs(pred[i] - truth));
    mag = std::max(mag, std::fabs(truth));
  }
  std::printf("     prediction lineaire sur dx0=(300 m, 0.3 m/s) : erreur %.2f m sur %.0f m (%.3f %%)\n",
              err, mag, 100.0 * err / mag);
  CHECK(err / mag < 5e-3, "la STM predit la propagation au 1er ordre");

  // ORACLE 3 — L'OD doit RETROUVER une orbite a partir de mesures bruitees,
  // et sa covariance doit DECROITRE quand on achete plus de mesures.
  // FENETRES DE POURSUITE REELLES. Le perigee du GTO (200 km) n'est vu par
  // AUCUNE station : trop bas, trop rapide. On ne poursuit un GTO que dans la
  // montee vers l'apogee — exactement comme en operations reelles. Le joueur
  // apprend donc son erreur d'execution UNE HEURE apres l'avoir commise.
  auto run_od = [&](double t_begin, double t_end_arc) {
    const auto& sts = nav::dsn_complexes();
    std::vector<nav::Measurement> meas;
    Rng rng(12345);
    prop::PropOptions o = opt;
    for (double t = t_begin; t <= t_end_arc; t += 60.0) {
      const auto r = prop::propagate(fs, 0.0, y0, std::max(t, 1e-6), {}, o);
      for (std::size_t k = 0; k < sts.size(); ++k) {
        if (!nav::station_visible(sts[k], t, pos(r.y_final))) continue;
        const auto pr = nav::predict(sts[k], t, pos(r.y_final), vel(r.y_final));
        nav::Measurement m;
        m.t = t; m.station = static_cast<int>(k);
        m.sigma_range = sts[k].sigma_range;
        m.sigma_rangerate = sts[k].sigma_rangerate;
        m.range = pr.range + rng.normal(0.0, m.sigma_range);
        m.range_rate = pr.range_rate + rng.normal(0.0, m.sigma_rangerate);
        meas.push_back(m);
      }
    }
    // etat de reference propage a t_begin : c'est LA qu'on estime.
    const auto rb = prop::propagate(fs, 0.0, y0, t_begin, {}, o);
    StateN apriori = rb.y_final;
    apriori[0] += 2000.0; apriori[4] += 2.0;   // a priori DEGRADE : 2 km, 2 m/s
    prop::PropOptions oo = opt; oo.step.rtol = 1e-10;
    auto od = nav::batch_least_squares(fs, meas, sts, t_begin, apriori, oo, 5);
    return std::make_tuple(od, meas.size(), pos(rb.y_final));
  };

  auto [od_short, n_short, r_ref_s] = run_od(3600.0,  5400.0);   // 30 min, 1 station
  auto [od_long,  n_long,  r_ref_l] = run_od(3600.0, 20000.0);   // 4h30, 2 stations

  const double e_short = norm(od_short.x_hat.pos() - r_ref_s);
  const double e_long  = norm(od_long.x_hat.pos() - r_ref_l);
  std::printf("     arc  30 min, 1 station  (%zu mesures) : sigma_pos %9.2f m | erreur reelle %8.2f m | rms %.2f\n",
              n_short, sigma_position(od_short.P), e_short, od_short.rms_residual);
  std::printf("     arc 4h30, 2 stations    (%zu mesures) : sigma_pos %9.2f m | erreur reelle %8.2f m | rms %.2f\n",
              n_long, sigma_position(od_long.P), e_long, od_long.rms_residual);
  CHECK(n_short > 0 && n_long > n_short, "les fenetres de visibilite existent et le long arc en a plus");
  CHECK(od_long.converged, "l'OD converge sur un arc long");
  CHECK(sigma_position(od_long.P) < sigma_position(od_short.P),
        "la covariance DECROIT quand on achete plus de mesures (= l'economie de navigation)");
  CHECK(e_long < 500.0, "l'OD retrouve la position vraie a mieux que 500 m depuis un a priori a 2 km");
  CHECK(od_long.rms_residual > 0.3 && od_long.rms_residual < 3.0,
        "residus RMS ~ 1 sigma : le modele explique les donnees (ni sur- ni sous-ajuste)");
}

// ---------------------------------------------------------------------------
static void test_porkchop() {
  section("Porkchop Terre -> Mars");
  ephem::StandishEphemeris eph;
  const double t0 = epoch_from_iso("2026-08-01T00:00:00").tdb;

  auto pc = astro::porkchop(eph, ephem::Body::EarthBary, ephem::Body::Mars,
                            t0, t0 + 220.0 * DAY, 60, 150.0 * DAY, 400.0 * DAY, 50);
  CHECK(pc.best_c3.ok, "le porkchop trouve au moins une solution");

  // ORACLE 1 — PLANCHER PHYSIQUE. Le C3 minimal ne peut PAS descendre sous le
  // transfert de Hohmann entre orbites circulaires coplanaires. Ce plancher se
  // calcule a la main, sans le code : 8.68 km2/s2.
  const auto h = astro::hohmann(1.0 * AU, 1.523679 * AU, MU_SUN);
  const double c3_floor = h.dv1 * h.dv1;
  std::printf("     plancher de Hohmann : C3 = %.3f km2/s2 (TOF %.1f j)\n",
              c3_floor / 1e6, h.tof / DAY);
  std::printf("     C3 minimal trouve   : C3 = %.3f km2/s2 (TOF %.1f j, depart %s)\n",
              pc.best_c3.c3 / 1e6, pc.best_c3.tof / DAY,
              epoch_to_iso(Epoch{pc.best_c3.t_dep}).substr(0, 10).c_str());
  CHECK(pc.best_c3.c3 > 0.98 * c3_floor,
        "le C3 minimal reste AU-DESSUS du plancher de Hohmann (sinon : violation d'energie)");
  CHECK(pc.best_c3.c3 < 2.0 * c3_floor,
        "et il n'en est pas absurdement loin (e_Mars=0.093, i_Mars=1.85 deg)");

  // ORACLE 2 — le TOF au minimum appartient a la meme famille que Hohmann.
  CHECK(std::fabs(pc.best_c3.tof - h.tof) < 100.0 * DAY,
        "le TOF au C3 minimal est de la famille de Hohmann");

  // ORACLE 3 — RECURRENCE SYNODIQUE. La fenetre doit revenir 779.9 j plus tard.
  // C'est un oracle fort : il teste l'ephemeride, Lambert et la grille d'un coup.
  const double S = astro::synodic_period(365.256 * DAY, 686.980 * DAY);
  auto pc2 = astro::porkchop(eph, ephem::Body::EarthBary, ephem::Body::Mars,
                             t0 + S - 110 * DAY, t0 + S + 110 * DAY, 60,
                             150.0 * DAY, 400.0 * DAY, 50);
  const double dt = pc2.best_c3.t_dep - pc.best_c3.t_dep;
  std::printf("     fenetre suivante    : %s  (ecart %.1f j ; synodique = %.1f j)\n",
              epoch_to_iso(Epoch{pc2.best_c3.t_dep}).substr(0, 10).c_str(), dt / DAY, S / DAY);
  CHECK(std::fabs(dt - S) < 70.0 * DAY,
        "la fenetre se reproduit a la periode SYNODIQUE (rater = attendre 25.6 mois)");

  // ORACLE 4 — le v_inf d'arrivee doit etre dans la plage physique connue.
  std::printf("     v_inf d'arrivee au C3 min : %.3f km/s\n", pc.best_c3.vinf_arr / 1000);
  CHECK(pc.best_c3.vinf_arr > 1500.0 && pc.best_c3.vinf_arr < 5000.0,
        "v_inf d'arrivee a Mars dans la plage 1.5-5 km/s");
}

// ---------------------------------------------------------------------------
// LA FENETRE DE LANCEMENT — le gate du GDD 7.3, auto-calibre sur la porkchop.
// On ne teste pas contre une date devinee : on LAISSE le modele trouver son
// optimum synodique, puis on verifie qu'il OUVRE a l'optimum et FERME a la
// conjonction (optimum + demi-periode synodique), et que la fenetre revient a
// la periode synodique. Un test qui passerait par accident aurait fabrique la
// meme geometrie deux fois — improbable.
static void test_launch_window() {
  section("Fenetre de lancement Terre -> Mars");
  ephem::StandishEphemeris eph;
  const double S = astro::synodic_period(365.256 * DAY, 686.980 * DAY);

  // 1) Trouver l'optimum de la fenetre 2026 (le modele le rend lui-meme).
  const Epoch scan{epoch_from_iso("2026-08-01T00:00:00").tdb};
  const astro::WindowResult probe =
      astro::launch_window(eph, ephem::Body::EarthBary, ephem::Body::Mars, scan);
  CHECK(probe.ok, "le modele trouve au moins une solution sur l'horizon synodique");
  const double t_opt = probe.best_dep_tdb;
  std::printf("     optimum 2026 : depart %s  (vinf_sum = %.0f m/s)\n",
              epoch_to_iso(Epoch{t_opt}).substr(0, 10).c_str(), probe.global_best);

  // Ordre de grandeur physique de l'optimum : vinf_dep ~3.5 + vinf_arr ~2.5 km/s.
  CHECK(probe.global_best > 4000.0 && probe.global_best < 9000.0,
        "l'optimum (vinf_dep + vinf_arr) est dans la plage reelle 4-9 km/s");

  // Les v_inf exposes se recomposent en global_best et sont physiques.
  CHECK_NEAR(probe.vinf_dep + probe.vinf_arr, probe.global_best, 1e-6,
             "vinf_dep + vinf_arr == global_best (metadonnee coherente)");
  CHECK(probe.vinf_dep > 2000.0 && probe.vinf_dep < 5000.0, "vinf_dep depart ~ 3-4 km/s");
  CHECK(probe.vinf_arr > 1500.0 && probe.vinf_arr < 4000.0, "vinf_arr arrivee ~ 2-3 km/s");

  // CHAINON no-arcade : le v_inf de la fenetre -> Δv REEL d'injection depuis LEO
  // par Oberth. Une bonne fenetre 2026 doit tomber vers ~3.6 km/s (pas le v_inf
  // nu, pas non plus v_circ + v_inf).
  const double dv_inj_fenetre =
      astro::injection_dv_from_circular(probe.vinf_dep, R_EARTH + 200e3, MU_EARTH);
  std::printf("     Δv injection reel depuis LEO a cette fenetre : %.0f m/s\n", dv_inj_fenetre);
  CHECK(dv_inj_fenetre > 3200.0 && dv_inj_fenetre < 4200.0,
        "injection reelle de la fenetre 2026 depuis LEO dans 3.2-4.2 km/s");

  // 2) OUVERTE a l'approche de l'optimum : on se place 20 j AVANT, l'optimum
  // tombe donc dans la fenetre operationnelle (slop 60 j).
  const Epoch bon{t_opt - 20.0 * DAY};
  const astro::WindowResult ouvert =
      astro::launch_window(eph, ephem::Body::EarthBary, ephem::Body::Mars, bon);
  CHECK(ouvert.open, "OUVERTE quand l'optimum tombe dans la fenetre operationnelle");
  CHECK(ouvert.local_best <= 1.30 * ouvert.global_best,
        "a l'optimum, le meilleur transfert local est proche de l'optimum global");
  CHECK(ouvert.next_open_days >= 0.0 && ouvert.next_open_days < 60.0,
        "prochaine ouverture imminente (dans la fenetre courante)");

  // 3) FERMEE a la conjonction : optimum + demi-periode synodique. La geometrie
  // est defavorable, le meilleur transfert local explose au-dessus du seuil.
  const Epoch mauvais{t_opt + 0.5 * S};
  const astro::WindowResult ferme =
      astro::launch_window(eph, ephem::Body::EarthBary, ephem::Body::Mars, mauvais);
  CHECK(ferme.ok, "des solutions existent meme a la conjonction (voler = cher)");
  CHECK(!ferme.open, "FERMEE a la conjonction (optimum + S/2)");
  CHECK(ferme.local_best > ferme.global_best,
        "a la conjonction, le local est strictement pire que le prochain optimum");
  std::printf("     conjonction : local %.0f m/s vs optimum a venir %.0f m/s ; "
              "prochaine ouverture dans %.0f j\n",
              ferme.local_best, ferme.global_best, ferme.next_open_days);
  CHECK(ferme.next_open_days > 60.0 && ferme.next_open_days < S,
        "la prochaine fenetre est a venir, sous une periode synodique");

  // 4) RECURRENCE : la fenetre suivante est ~S jours apres l'optimum courant.
  const Epoch suivant{t_opt + S - 60.0 * DAY};
  const astro::WindowResult w2 =
      astro::launch_window(eph, ephem::Body::EarthBary, ephem::Body::Mars, suivant);
  const double dt = w2.best_dep_tdb - t_opt;
  std::printf("     fenetre suivante : %s  (ecart %.1f j ; synodique %.1f j)\n",
              epoch_to_iso(Epoch{w2.best_dep_tdb}).substr(0, 10).c_str(),
              dt / DAY, S / DAY);
  CHECK(std::fabs(dt - S) < 70.0 * DAY,
        "la fenetre se reproduit a la periode synodique (779.9 j)");
}

// ---------------------------------------------------------------------------
// ORIENTATION DES CORPS (IAU) — l'oracle couronne trace les SAISONS : la
// latitude sub-solaire de la Terre doit valoir +23.4 deg au solstice de juin
// (tropique du Cancer), -23.4 en decembre, ~0 aux equinoxes. Cet oracle unique
// valide la CHAINE ENTIERE : ephemeride (position) + transformation de repere
// (equatorial->ecliptique) + modele de pole. Une erreur de signe s'y voit.
static void test_body_orientation() {
  section("Orientation des corps (IAU)");
  using namespace fen::ephem;
  const double eps = cst::OBLIQUITY_J2000;

  // --- Transformation de repere : ancres exactes.
  const Vec3 pole_eq{0, 0, 1};
  const Vec3 pe = equatorial_to_ecliptic(pole_eq);
  CHECK_NEAR(pe.x, 0.0, 1e-12, "repere : x du pole equatorial reste nul");
  CHECK_NEAR(pe.y, std::sin(eps), 1e-12, "repere : pole equatorial -> (0,sin e,cos e)");
  CHECK_NEAR(pe.z, std::cos(eps), 1e-12, "repere : ...composante z = cos e");
  const Vec3 ecl_pole_eq{0, -std::sin(eps), std::cos(eps)};   // pole ecliptique en equatorial
  const Vec3 back = equatorial_to_ecliptic(ecl_pole_eq);
  CHECK_NEAR(back.z, 1.0, 1e-12, "repere : le pole ecliptique revient sur (0,0,1)");
  CHECK_NEAR(std::sqrt(back.x*back.x + back.y*back.y), 0.0, 1e-9, "repere : ...exactement l'axe z");

  // --- Obliquites (ancres physiques).
  const double obl_terre = obliquity_to_ecliptic_rad(Body::EarthBary) / DEG;
  const double obl_mars  = obliquity_to_ecliptic_rad(Body::Mars) / DEG;
  const double obl_venus = obliquity_to_ecliptic_rad(Body::Venus) / DEG;
  const double obl_jup   = obliquity_to_ecliptic_rad(Body::Jupiter) / DEG;
  std::printf("     obliquites : Terre=%.2f  Mars=%.2f  Venus=%.2f  Jupiter=%.2f deg\n",
              obl_terre, obl_mars, obl_venus, obl_jup);
  CHECK_NEAR(obl_terre, 23.4393, 0.01, "Terre : obliquite = obliquite de l'ecliptique (23.44 deg)");
  // ATTENTION : obliquite a l'ECLIPTIQUE, PAS a l'orbite. Mars incline son orbite
  // de 1.85 deg / ecliptique ; ses 25.19 deg a l'orbite deviennent ~26.7 deg a
  // l'ecliptique. Le calcul a la main du pole (317.68, 52.89) le confirme.
  CHECK(obl_mars > 26.0 && obl_mars < 27.5, "Mars : obliquite a l'ecliptique ~ 26.7 deg");
  // Venus : son POLE (convention IAU = cote nord du plan invariable) est a ~1 deg
  // de la normale ecliptique -> son equateur est quasi dans l'ecliptique. Le
  // caractere RETROGRADE n'est PAS dans l'angle du pole, il est dans le SIGNE du
  // taux de rotation (W).
  CHECK(obl_venus < 5.0, "Venus : pole quasi normal a l'ecliptique (equateur ~ dans l'ecliptique)");
  CHECK(rotation_elements(Body::Venus).w_rate_deg_per_day < 0.0,
        "Venus : rotation RETROGRADE (taux de meridien negatif)");
  CHECK(obl_jup < 5.0, "Jupiter : obliquite faible (~3 deg, pas de saisons marquees)");
  // Uranus roule SUR LE FLANC. `obliquity_to_ecliptic_rad` renvoie l'angle du
  // POLE IAU (delta0 = -15 deg -> ~82 deg de la normale ecliptique) ; le "tilt
  // axial" populaire de 97.8 deg est celui du moment cinetique (Uranus est
  // retrograde, son pole IAU est a l'oppose : 180 - 82 = 98). Les deux disent la
  // meme chose : l'axe est QUASI DANS LE PLAN. Ce qui compte pour le rendu, c'est
  // la direction du pole (unique) + le signe de W (retrograde).
  const double obl_uranus = obliquity_to_ecliptic_rad(Body::Uranus) / DEG;
  std::printf("     Uranus : pole a %.1f deg de la normale (couche sur le flanc)\n", obl_uranus);
  CHECK(obl_uranus > 78.0 && obl_uranus < 86.0, "Uranus : axe quasi dans le plan de l'ecliptique");
  CHECK(rotation_elements(Body::Uranus).w_rate_deg_per_day < 0.0, "Uranus : rotation RETROGRADE");

  // --- Taux du meridien origine : la Terre tourne de 360.9856 deg/jour sideral.
  StandishEphemeris eph;
  const Epoch t0{epoch_from_iso("2026-03-20T00:00:00").tdb};
  const double w0 = prime_meridian_deg(Body::EarthBary, t0);
  const double w1 = prime_meridian_deg(Body::EarthBary, Epoch{t0.tdb + DAY});
  double dW = std::fmod(w1 - w0 + 720.0, 360.0);
  CHECK_NEAR(dW, std::fmod(360.9856235, 360.0), 0.01, "Terre : +0.9856 deg/jour (jour sideral < solaire)");

  // --- L'ORACLE COURONNE : latitude sub-solaire = declinaison solaire saisonniere.
  struct Cas { const char* iso; double lat_attendue; const char* nom; };
  const Cas cas[] = {
    {"2026-06-21T00:00:00", +23.44, "solstice de juin  -> tropique du Cancer (+23.4)"},
    {"2026-12-21T12:00:00", -23.44, "solstice de decembre -> Capricorne (-23.4)"},
    {"2026-03-20T14:00:00",   0.0,  "equinoxe de mars -> equateur (0)"},
    {"2026-09-22T20:00:00",   0.0,  "equinoxe de septembre -> equateur (0)"},
  };
  for (const Cas& c : cas) {
    const Epoch t{epoch_from_iso(c.iso).tdb};
    const PosVel earth = eph.state(Body::EarthBary, Body::Sun, t);
    const double lat = subsolar_latitude_rad(Body::EarthBary, earth.r) / DEG;
    std::printf("     %-42s : lat sub-solaire = %+6.2f deg\n", c.nom, lat);
    CHECK(std::fabs(lat - c.lat_attendue) < 1.2, c.nom);
  }
}

// ---------------------------------------------------------------------------
static void test_flyby() {
  section("Assistance gravitationnelle");
  constexpr double R_J = 71492e3;

  // ORACLE 1 — la deviation DECROIT avec le periastre, et tend vers 0 a l'infini.
  double prev = 1e9;
  for (double rp : {1.5 * R_J, 5.0 * R_J, 30.0 * R_J, 137.0 * R_J, 1e4 * R_J}) {
    const double d = astro::flyby_turn(5500.0, rp, MU_JUPITER);
    CHECK(d < prev, "la deviation decroit quand le periastre croit");
    prev = d;
  }
  CHECK(astro::flyby_turn(5500.0, 1e6 * R_J, MU_JUPITER) < 1e-3, "deviation -> 0 a l'infini");

  // ORACLE 2 — REVERSIBILITE : r_p(delta(r_p)) == r_p.
  const double rp0 = 30.0 * R_J, vinf = 5500.0;
  const double d0 = astro::flyby_turn(vinf, rp0, MU_JUPITER);
  const double rp1 = astro::flyby_rp_for_turn(vinf, d0, MU_JUPITER);
  CHECK_NEAR(rp1 / rp0, 1.0, 1e-10, "r_p <-> deviation reversible");

  // ORACLE 3 — LE RESULTAT REMARQUABLE. Le gain maximal d'un survol est la
  // vitesse circulaire au periastre, atteinte pour v_inf = sqrt(mu/r_p).
  // Ce n'est pas une valeur tabulee : c'est un extremum. On le VERIFIE en
  // balayant v_inf et en constatant que le maximum tombe bien la.
  const double v_opt = astro::flyby_optimal_vinf(rp0, MU_JUPITER);
  const double dv_opt = astro::flyby_max_free_dv(rp0, MU_JUPITER);
  double best = 0.0, v_best = 0.0;
  for (double v = 500.0; v < 60000.0; v += 50.0) {
    const double fdv = astro::flyby_free_dv(v, rp0, MU_JUPITER);
    if (fdv > best) { best = fdv; v_best = v; }
  }
  std::printf("     Jupiter a 30 R_J : gain max theorique %.0f m/s a v_inf = %.0f m/s\n",
              dv_opt, v_opt);
  std::printf("     balayage numerique : maximum %.0f m/s trouve a v_inf = %.0f m/s\n",
              best, v_best);
  CHECK_NEAR(best / dv_opt, 1.0, 1e-3, "le gain max EST la vitesse circulaire au periastre");
  CHECK_NEAR(v_best / v_opt, 1.0, 5e-3, "et il est atteint pour v_inf = sqrt(mu/r_p)");

  // ORACLE 4 — un survol NON propulse conserve |v_inf|, mais PAS l'energie
  // heliocentrique. C'est toute la mecanique du vol : on vole de l'energie a
  // la planete.
  const Vec3 vin{5500, 0, 0};
  const Vec3 vout = rotate(vin, Vec3{0, 0, 1}, astro::flyby_turn(5500.0, rp0, MU_JUPITER));
  auto fb = astro::solve_flyby(vin, vout, MU_JUPITER, 20.0 * R_J);
  CHECK(fb.feasible && fb.unpowered, "survol non propulse faisable");
  CHECK_NEAR(fb.rp / rp0, 1.0, 1e-3, "le solveur retrouve le periastre exact");
  CHECK_NEAR(fb.dv, 0.0, 1e-6, "Delta-v propulsif nul (|v_inf| conserve)");
  CHECK_NEAR(norm(vout), norm(vin), 1e-6, "|v_inf| conserve : la planete ne donne pas de vitesse");
  std::printf("     survol non propulse : gain gravitationnel NET = %.0f m/s\n", fb.gravity_gain);
  CHECK(fb.gravity_gain > 5000.0, "mais le vecteur a tourne : c'est du Delta-v gratuit");

  // ORACLE 5 — deviation IMPOSSIBLE : la planete ne peut pas tourner autant.
  // Ce n'est pas une penalite de jeu, c'est une borne physique.
  const Vec3 vback{-5500, 0, 0};   // demi-tour complet
  auto fb2 = astro::solve_flyby(vin, vback, MU_JUPITER, 30.0 * R_J);
  std::printf("     demi-tour a 30 R_J : demande %.1f deg, disponible %.1f deg -> %s\n",
              fb2.turn_required / DEG, fb2.turn_available / DEG,
              fb2.feasible ? "FAISABLE" : "IMPOSSIBLE");
  CHECK(!fb2.feasible, "un demi-tour a 30 R_J est PHYSIQUEMENT impossible");

  // ORACLE 6 — la synodique Jupiter-Saturne, la vraie monnaie.
  const double S = astro::synodic(4332.6 * DAY, 10759.2 * DAY) / (365.25 * DAY);
  std::printf("     periode synodique Jupiter-Saturne : %.2f ans\n", S);
  CHECK_NEAR(S, 19.86, 0.15, "synodique Jupiter-Saturne = 19.86 ans (une generation)");
}

// ---------------------------------------------------------------------------
static void test_mga() {
  section("Chaine MGA + evolution differentielle");
  ephem::StandishEphemeris eph;
  astro::MgaProblem p;
  p.seq = {ephem::Body::EarthBary, ephem::Body::Venus, ephem::Body::Jupiter};
  p.rp_min = {6.35e6, 7.15e8};
  p.t0_lo = epoch_from_iso("2030-01-01T00:00:00").tdb;
  p.t0_hi = epoch_from_iso("2033-01-01T00:00:00").tdb;
  p.tof_lo = {80.0 * DAY, 500.0 * DAY};
  p.tof_hi = {350.0 * DAY, 1800.0 * DAY};
  p.c3_max = 30e6;
  p.tof_total_max = 6.0 * 365.25 * DAY;
  p.rp_insert = 10.0 * 71492e3;
  p.a_insert = 100.0 * 71492e3;

  const int D = astro::n_vars(p), L = astro::n_legs(p);
  std::vector<double> lo(D), hi(D);
  lo[0] = p.t0_lo; hi[0] = p.t0_hi;
  for (int i = 0; i < L; ++i) { lo[1 + i] = p.tof_lo[i]; hi[1 + i] = p.tof_hi[i]; }
  for (int i = 0; i < L; ++i) { lo[1 + L + i] = 0.0; hi[1 + L + i] = 2.999; }
  auto f = [&](const std::vector<double>& x) { return astro::mga_evaluate(p, eph, x).cost; };

  // ORACLE 1 — DETERMINISME. Meme graine -> meme tour. Sans ca, un "meilleur
  // tour trouve" ne serait pas un resultat mais une anecdote.
  auto a = astro::differential_evolution(f, lo, hi, 40, 120, 4242);
  auto b = astro::differential_evolution(f, lo, hi, 40, 120, 4242);
  CHECK(a.f == b.f, "DE deterministe : meme graine -> meme cout, BIT A BIT");
  auto c = astro::differential_evolution(f, lo, hi, 40, 120, 4243);
  CHECK(a.f != c.f, "et une autre graine explore un autre bassin (paysage multimodal)");

  auto r = astro::mga_evaluate(p, eph, a.x);
  CHECK(r.feasible, "DE trouve un tour Terre-Venus-Jupiter faisable");
  if (!r.feasible) return;
  std::printf("     Terre-Venus-Jupiter : C3 = %.2f km2/s2 | dv embarque %.0f m/s | %.2f ans\n",
              r.c3 / 1e6, r.dv_onboard, r.tof_total / (365.25 * DAY));

  // ORACLE 2 — LA DEFINITION DU "GRATUIT".
  // 2*v_inf*sin(delta/2) n'est valable que pour un survol NON PROPULSE. Des que
  // le moteur intervient, |v_inf_out - v_inf_in| contient AUSSI son travail — et
  // l'appeler "gratuit" serait s'attribuer le merite de son propre moteur.
  // On teste donc la formule LA OU ELLE S'APPLIQUE, et la comptabilite ailleurs.
  const double vi = r.vinf_in[0], rp = r.rp[0];
  std::printf("     survol de Venus : r_p = %.0f km, v_inf %.2f -> %.2f km/s, dv moteur %.0f m/s\n",
              rp / 1000, vi / 1000, r.vinf_out[0] / 1000, r.dv_fb[0]);
  if (r.dv_fb[0] < 1.0) {
    const double fdv = 2.0 * vi * std::sin(0.5 * astro::flyby_turn(vi, rp, MU_VENUS));
    CHECK_NEAR(r.gain[0] / fdv, 1.0, 0.02, "survol NON propulse : gain = 2*v_inf*sin(delta/2)");
    CHECK_NEAR(r.vinf_out[0] / vi, 1.0, 1e-3, "survol NON propulse : |v_inf| conserve");
  } else {
    // survol propulse : le gain NET ne peut pas exceder le changement total,
    // et le moteur ne peut pas produire plus que ce qu'il a depense.
    auto fb0 = astro::solve_flyby(Vec3{vi, 0, 0}, Vec3{r.vinf_out[0], 0, 0}, MU_VENUS, rp);
    CHECK(r.gain[0] <= 2.0 * vi + r.dv_fb[0] + 1.0,
          "survol propulse : le gain NET reste borne par la physique");
    CHECK(r.gain[0] > 0.0, "et il reste positif : la planete a bien travaille");
    std::printf("     gain gravitationnel NET (deduit du moteur) : %.0f m/s\n", r.gain[0]);
    (void)fb0;
  }
  CHECK(r.c3 >= 0.0 && r.tof_total <= p.tof_total_max, "contraintes respectees");
}

// ---------------------------------------------------------------------------
static void test_mga1dsm() {
  section("MGA-1DSM : le survol non propulse PAR CONSTRUCTION");
  ephem::StandishEphemeris eph;
  Rng rng(31337);

  // ORACLE — LE POINT DE TOUTE LA BRIQUE.
  // En MGA pur, on CHOISIT v_inf_sortant (via l'arc de Lambert suivant) et on
  // paie le desaccord au periastre : mesure, 4 935 m/s sur un seul survol.
  // En MGA-1DSM, on ne le choisit plus : on TOURNE v_inf_entrant de l'angle que
  // la planete peut fournir. |v_inf| est alors conserve A LA PRECISION MACHINE,
  // et ce n'est pas une approximation : c'est l'identite
  //     cos^2 d + cos^2 b sin^2 d + sin^2 b sin^2 d = 1.
  // On la verifie sur 5000 geometries tirees au hasard, y compris degenerees.
  double worst = 0.0;
  double max_turn = 0.0, max_gain = 0.0;
  for (int i = 0; i < 5000; ++i) {
    const Vec3 vin{rng.uniform(-12000, 12000), rng.uniform(-12000, 12000),
                   rng.uniform(-12000, 12000)};
    const Vec3 vpl{rng.uniform(-40000, 40000), rng.uniform(-40000, 40000),
                   rng.uniform(-5000, 5000)};
    const double rp = rng.uniform(1.05 * R_EARTH, 300.0 * R_EARTH);
    const double beta = rng.uniform(-TWO_PI, TWO_PI);
    const double mu = (i % 2) ? MU_EARTH : MU_JUPITER;
    if (norm(vin) < 100.0) continue;
    const Vec3 vout = astro::flyby_rotate(vin, vpl, rp, mu, beta);
    worst = std::max(worst, std::fabs(norm(vout) / norm(vin) - 1.0));
    max_turn = std::max(max_turn, astro::flyby_turn(norm(vin), rp, mu));
    max_gain = std::max(max_gain, norm(vout - vin));
  }
  std::printf("     5000 survols tires au hasard : ecart relatif max sur |v_inf| = %.2e\n", worst);
  std::printf("     deviation max obtenue %.1f deg | gain heliocentrique max %.2f km/s\n",
              max_turn / DEG, max_gain / 1000);
  CHECK(worst < 1e-12, "|v_inf| CONSERVE a la precision machine (survol non propulse par construction)");
  CHECK(max_gain > 1000.0, "et le vecteur tourne vraiment : le gain est reel");

  // La deviation obtenue ne peut JAMAIS depasser ce que le periastre permet.
  const double d = astro::flyby_turn(6000.0, 1.05 * R_EARTH, MU_EARTH);
  const Vec3 vi{6000, 0, 0}, vp{0, 30000, 0};
  for (int i = 0; i < 200; ++i) {
    const Vec3 vo = astro::flyby_rotate(vi, vp, 1.05 * R_EARTH, MU_EARTH,
                                        rng.uniform(-TWO_PI, TWO_PI));
    const double turn = std::acos(std::fmin(1.0, dot(vi, vo) / (norm(vi) * norm(vo))));
    if (turn > d + 1e-9) { CHECK(false, "deviation superieure au maximum physique"); return; }
  }
  CHECK(true, "la deviation ne depasse JAMAIS le maximum permis par le periastre");
}

// ---------------------------------------------------------------------------
static void test_ias15() {
  section("IAS15 — le critere que j'avais retracte");
  astro::Elements el;
  el.a = 24371150.0; el.e = 0.7301; el.i = 28.5 * DEG;
  el.raan = 0; el.argp = 0; el.nu = 0;
  Vec3 r0, v0;
  astro::elements_to_rv(el, MU_EARTH, r0, v0);
  const double T = astro::orbital_period(el.a, MU_EARTH);
  const StateN y0{r0.x, r0.y, r0.z, v0.x, v0.y, v0.z, 1000.0};

  force::ForceStack fs;
  fs.add(std::make_shared<force::CentralGravity>(MU_EARTH));

  auto run = [&](prop::Scheme sch, double rtol, double t_end) {
    prop::PropOptions o;
    o.step.scheme = sch;
    o.step.rtol = rtol;
    o.step.atol = 1e-6;
    o.step.h_init = 60.0;
    return prop::propagate(fs, 0.0, y0, t_end, {}, o);
  };
  const double E0 = specific_energy(r0, v0, MU_EARTH);
  auto dE = [&](const StateN& y) {
    return std::fabs((specific_energy(pos(y), vel(y), MU_EARTH) - E0) / E0);
  };

  // ORACLE 1 — LE COUT NE S'ENVOLE PAS. L'estimateur d'IAS15 va comme h^7 : en
  // resserrant epsilon d'un facteur 100, le pas ne doit couter que ~100^(1/7) = 1.9x.
  // Si les noeuds de Gauss-Radau etaient faux, le schema s'effondrerait et le cout
  // exploserait. C'est le test.
  const auto ra = run(prop::Scheme::Ias15, 1e-7, 20.0 * T);
  const auto rb = run(prop::Scheme::Ias15, 1e-9, 20.0 * T);
  const double ratio = static_cast<double>(rb.steps_accepted) / std::max(1LL, ra.steps_accepted);
  std::printf("     eps 1e-7 -> 1e-9 : pas x%.2f  (h^7 predit x%.2f)\n",
              ratio, std::pow(100.0, 1.0 / 7.0));
  CHECK(ratio < 4.0, "le cout suit bien l'estimateur en h^7 : les noeuds sont bons");

  // ORACLE 2 — LE CRITERE. |dE/E| < 1e-12 sur un an. Annonce, retracte, TENU.
  const auto d5  = run(prop::Scheme::Dopri5, 1e-13, 365.25 * DAY);
  const auto d15 = run(prop::Scheme::Ias15,  1e-9,  365.25 * DAY);
  auto k1y = astro::kepler_propagate(r0, v0, 365.25 * DAY, MU_EARTH);
  const double dr5  = norm(pos(d5.y_final)  - k1y.r);
  const double dr15 = norm(pos(d15.y_final) - k1y.r);
  std::printf("     1 an (833 orbites) — DOPRI5 a rtol 1e-13, IAS15 a eps 1e-9 :\n");
  std::printf("       DOPRI5 : |dE/E| = %.2e | ecart Kepler = %8.3f m | %lld pas\n",
              dE(d5.y_final),  dr5,  d5.steps_accepted);
  std::printf("       IAS15  : |dE/E| = %.2e | ecart Kepler = %8.3f m | %lld pas\n",
              dE(d15.y_final), dr15, d15.steps_accepted);
  CHECK(dE(d15.y_final) < 1e-12, "IAS15 : |dE/E| < 1e-12 sur un an — LE CRITERE ANNONCE");
  CHECK(dE(d15.y_final) < dE(d5.y_final) / 100.0, "et il ecrase DOPRI5 d'au moins 2 ordres");
  CHECK(d15.steps_accepted < d5.steps_accepted,
        "en PLUS PEU de pas : l'ordre 15 coute moins cher que l'ordre 5 a haute precision");

  // ORACLE 3 — la sortie dense d'ordre 15 doit rester exacte AU MILIEU du pas,
  // sinon la detection d'evenements par racine serait fausse.
  prop::PropOptions o;
  o.step.scheme = prop::Scheme::Ias15;
  o.step.rtol = 1e-9;
  auto rev = prop::propagate(fs, 0.0, y0, 3.0 * T,
                             {prop::event_apoapsis(MU_EARTH)}, o);
  double t_apo = -1;
  for (const auto& e : rev.events) if (e.name == "APOAPSIS") { t_apo = e.t; break; }
  std::printf("     1er apoastre (racine sur l'interpolant d'ordre 15) : %.6f s (T/2 = %.6f)\n",
              t_apo, T / 2);
  CHECK_NEAR(t_apo, T / 2.0, 1e-4, "evenement exact : l'interpolant IAS15 est bon au MILIEU du pas");
}

#include "test_local_refine.inc"

// ---------------------------------------------------------------------------
int main() {
  std::printf("=======================================================\n");
  std::printf(" FENETRE / astro_core — oracles physiques\n");
  std::printf("=======================================================\n");
  test_epoch();
  test_kepler();
  test_elements();
  test_lambert();
  test_transfers();
  test_propagator();
  test_ias15();
  test_nbody_battin();
  test_ephemeris();
  test_bplane();
  test_gates_and_rng();
  test_vehicle();
  test_stm_and_od();
  test_porkchop();
  test_launch_window();
  test_body_orientation();
  test_flyby();
  test_mga();
  test_mga1dsm();
  test_local_refine();
  std::printf("\n=======================================================\n");
  std::printf(" %d OK, %d ECHECS\n", g_pass, g_fail);
  std::printf("=======================================================\n");
  return g_fail == 0 ? 0 : 1;
}
