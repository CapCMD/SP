// scripts/m01_corridor.cpp
//
// LA SANCTION VISIBLE.
//
// Le plan-B n'est pas un diagramme d'illustration : c'est LA surface de décision
// d'une arrivée interplanétaire. Deux objets s'y superposent, et deux seulement :
//
//   LE CORRIDOR   — la zone admissible. Elle n'est pas dessinée par un designer :
//                   elle est bornée en dedans par l'atmosphère (on brûle) et en
//                   dehors par le BUDGET D'INSERTION (Oberth : plus on passe haut,
//                   plus l'insertion coûte). C'est un ANNEAU, et sa largeur est
//                   une conséquence, pas un réglage.
//
//   L'ELLIPSE     — la dispersion 3-sigma de livraison, produit des erreurs
//                   d'exécution (Gates) amplifiées par le temps de vol restant.
//
// Si l'ellipse déborde de l'anneau, une fraction calculable des tirages échoue.
// Le joueur n'a pas besoin d'un chiffre : il le VOIT. Puis il paie — en TCM plus
// tardives, en poursuite, ou en marge.
//
// LA LEÇON MESURÉE ICI :
//   une TCM tardive corrige moins de dérive mais injecte moins d'erreur
//   (sa propre faute de Gates est amplifiée par un temps de vol plus court).
//   Une TCM précoce fait l'inverse. L'optimum est une ÉCHELLE de corrections
//   de plus en plus petites et de plus en plus tardives — et c'est exactement
//   ce que font les vraies missions (TCM-1 grosse, TCM-4/5 minuscules).
#include <cstdio>
#include <cmath>
#include <fstream>
#include <string>
#include <vector>
#include "fen/astro/Porkchop.hpp"
#include "fen/astro/BPlane.hpp"
#include "fen/astro/Transfers.hpp"
#include "fen/flight/FlightPlan.hpp"
#include "fen/nav/Statistics.hpp"
#include "fen/core/Epoch.hpp"

using namespace fen;
using namespace fen::cst;

static constexpr double RP_TARGET   = R_MARS + 400e3;
static constexpr double THETA_AIM   = 30.0 * DEG;
static constexpr double R_PARK      = R_EARTH + 200e3;
static constexpr double HANDOFF_DAYS = 20.0;
static constexpr double A_TARGET_ORBIT = 20446e3;  // demi-grand axe d'une orbite d'1 sol
static constexpr double MOI_BUDGET  = 950.0;       // m/s provisionnés pour l'insertion
static constexpr double RP_ATMO     = R_MARS + 150e3;

struct Departure { Vec3 r_park, v_park, dv_tmi, h_hat; double inc; };

static Departure departure_geometry(const Vec3& vinf, double rp, double mu) {
  Departure d;
  const double vn = norm(vinf);
  const Vec3 u = unit(vinf);
  const Vec3 z{0, 0, 1};
  d.h_hat = unit(z - u * dot(z, u));
  const double e = 1.0 + rp * vn * vn / mu;
  const double phi = std::atan2(std::sqrt(1.0 - 1.0 / (e * e)), -1.0 / e);
  const Vec3 e_hat = rotate(u, d.h_hat, -phi);
  const Vec3 w_hat = cross(d.h_hat, e_hat);
  d.r_park = e_hat * rp;
  d.v_park = w_hat * std::sqrt(mu / rp);
  d.dv_tmi = w_hat * (std::sqrt(vn * vn + 2.0 * mu / rp) - std::sqrt(mu / rp));
  d.inc = std::acos(std::fmin(1.0, std::fabs(d.h_hat.z)));
  return d;
}

struct Arrival { bool ok{false}; double t_ca{}, BdotT{}, BdotR{}, rp{}, vinf{}; };
struct Tcm { double t{}; Vec3 dv{}; };

// LA VÉRITÉ : géocentrique -> changement d'origine EXACT -> héliocentrique.
// `nominal_tcm` = MODELE DE CONCEPTION : l'erreur d'execution du TMI est bien la
// (elle a EU LIEU, on la subit), mais les TCM qu'on est en train de concevoir
// s'executent parfaitement. C'est le modele sur lequel un navigateur calcule.
// `nominal_tcm = false` = LA VERITE : la TCM subit Gates comme tout le reste.
// Confondre les deux, c'est iterer contre le tirage aleatoire lui-meme — c'est-a-dire
// tricher. Le jeu ne peut pas se le permettre, donc le code non plus.
static Arrival fly(const ephem::IEphemeris& eph, double t_dep, const Departure& dep,
                   const Vec3& dv_tmi, const vehicle::Vehicle& veh,
                   std::uint64_t seed, const std::vector<Tcm>& tcms, double t_stop,
                   bool nominal_tcm = false) {
  Arrival A;
  prop::PropOptions opt;
  opt.step.rtol = 1e-10;
  opt.step.h_max = 5.0 * DAY;

  flight::FlightPlan p1;
  p1.center = ephem::Body::EarthBary;
  p1.perturbers = {ephem::Body::Sun, ephem::Body::Moon, ephem::Body::Jupiter};
  p1.epoch0 = t_dep;
  p1.initial = State{dep.r_park, dep.v_park, veh.total_mass()};
  p1.vehicle = veh;
  p1.t_stop = t_dep + HANDOFF_DAYS * DAY;
  flight::BurnCmd tmi;
  tmi.id = "TMI"; tmi.t = t_dep; tmi.frame = flight::DvFrame::Inertial;
  tmi.hold = force::ThrustFrame::InertialFixed; tmi.stage = 0; tmi.dv = dv_tmi;
  p1.burns = {tmi};
  auto r1 = flight::execute(p1, eph, seed, opt);
  if (!r1.ok) return A;

  const double t_h = r1.truth.t_final;
  const auto E = eph.state(ephem::Body::EarthBary, ephem::Body::Sun, Epoch{t_h});
  const State s1 = State::unpack(r1.truth.y_final);

  flight::FlightPlan p2;
  p2.center = ephem::Body::Sun;
  p2.perturbers = {ephem::Body::EarthBary, ephem::Body::Mars, ephem::Body::Jupiter,
                   ephem::Body::Venus};
  p2.epoch0 = t_h;
  p2.initial = State{s1.r + E.r, s1.v + E.v, s1.m};
  p2.vehicle = veh;
  p2.t_stop = t_stop;
  for (std::size_t k = 0; k < tcms.size(); ++k) {
    flight::BurnCmd b;
    b.id = "TCM" + std::to_string(k + 1);
    b.t = tcms[k].t; b.dv = tcms[k].dv;
    b.frame = flight::DvFrame::Inertial;
    b.hold = force::ThrustFrame::InertialFixed;
    b.stage = 0;
    p2.burns.push_back(b);
  }
  p2.extra_events.push_back(prop::EventSpec{
      "MARS_CA",
      [&eph](double t, const StateN& y) {
        const auto M = eph.state(ephem::Body::Mars, ephem::Body::Sun, Epoch{t});
        return dot(pos(y) - M.r, vel(y) - M.v);
      },
      +1, false});

  const std::uint64_t seed2 = nominal_tcm ? 0ull : (seed ? seed + 777777ull : 0ull);
  auto r2 = flight::execute(p2, eph, seed2, opt);
  if (!r2.ok) return A;
  for (const auto& ev : r2.truth.events) {
    if (ev.name != "MARS_CA") continue;
    const auto M = eph.state(ephem::Body::Mars, ephem::Body::Sun, Epoch{ev.t});
    const auto bp = astro::b_plane(pos(ev.y) - M.r, vel(ev.y) - M.v, MU_MARS);
    if (!bp.hyperbolic) return A;
    A.ok = true; A.t_ca = ev.t;
    A.BdotT = bp.BdotT; A.BdotR = bp.BdotR; A.rp = bp.rp; A.vinf = bp.vinf;
    return A;
  }
  return A;
}

// Delta-v d'insertion vers une orbite de demi-grand axe a, depuis un périastre rp.
static double dv_moi(double rp, double vinf, double a) {
  const double v_hyp = std::sqrt(vinf * vinf + 2.0 * MU_MARS / rp);
  const double v_cap = std::sqrt(MU_MARS * (2.0 / rp - 1.0 / a));
  return v_hyp - v_cap;
}

int main(int argc, char** argv) {
  const int N = (argc > 1) ? std::atoi(argv[1]) : 60;
  ephem::StandishEphemeris eph;

  std::printf("=====================================================================\n");
  std::printf(" M01 — LE CORRIDOR DU PLAN-B : la sanction, rendue visible\n");
  std::printf("=====================================================================\n");

  // ---- fenêtre + conception N-corps ciblée ---------------------------------
  const double t0 = epoch_from_iso("2026-08-01T00:00:00").tdb;
  auto pc = astro::porkchop(eph, ephem::Body::EarthBary, ephem::Body::Mars,
                            t0, t0 + 220.0 * DAY, 40, 200.0 * DAY, 360.0 * DAY, 40);
  const auto& W = pc.best_c3;
  const auto E0 = eph.state(ephem::Body::EarthBary, ephem::Body::Sun, Epoch{W.t_dep});
  const Vec3 vinf_dep = W.v1 - E0.v;
  auto dep = departure_geometry(vinf_dep, R_PARK, MU_EARTH);
  const double t_dep = W.t_dep;
  const double t_arr = W.t_dep + W.tof;
  const double t_stop = t_arr + 40.0 * DAY;

  vehicle::Engine big;
  big.id = "TMI"; big.thrust_vac = 66700.0; big.isp_vac = 325.0; big.mass = 120.0;
  vehicle::Stage st;
  st.id = "CRUISE"; st.engine = big;
  st.tank.dry_fraction = 0.08; st.tank.residual_fraction = 0.02; st.structure_mass = 180.0;
  auto sz = vehicle::size_stage_for_dv(norm(dep.dv_tmi) + 150.0, 900.0, big, 0.08, 180.0, 0.02);
  st.tank.propellant_mass = sz.propellant;
  vehicle::Vehicle veh; veh.payload_dry = 900.0; veh.stages = {st};

  std::printf("\n  fenetre : depart %s, TOF %.0f j, C3 %.2f km2/s2\n",
              epoch_to_iso(Epoch{t_dep}).substr(0, 10).c_str(), W.tof / DAY, W.c3 / 1e6);

  // --- point de visée dans le plan-B ---
  const double vinf_arr = W.vinf_arr;
  const double b_aim = astro::b_from_rp(RP_TARGET, vinf_arr, MU_MARS);
  const double BT_aim = b_aim * std::cos(THETA_AIM);
  const double BR_aim = b_aim * std::sin(THETA_AIM);

  // ---- CIBLAGE N-CORPS (Newton 3x3 par différences finies) ------------------
  Vec3 dv = dep.dv_tmi;
  std::printf("\n--- CIBLAGE N-CORPS ------------------------------------------------\n");
  for (int it = 0; it < 8; ++it) {
    auto A = fly(eph, t_dep, dep, dv, veh, 0, {}, t_stop);
    if (!A.ok) { std::printf("  echec de propagation\n"); return 1; }
    const double f[3] = {A.BdotT - BT_aim, A.BdotR - BR_aim, A.t_ca - t_arr};
    const double err = std::hypot(f[0], f[1]);
    std::printf("  it %d : |ecart plan-B| = %10.1f km\n", it, err / 1000);
    if (err < 30e3) break;
    double J[3][3];
    const double h = 0.05;
    for (int j = 0; j < 3; ++j) {
      Vec3 dvp = dv; dvp[j] += h;
      auto Ap = fly(eph, t_dep, dep, dvp, veh, 0, {}, t_stop);
      if (!Ap.ok) return 1;
      J[0][j] = (Ap.BdotT - A.BdotT) / h;
      J[1][j] = (Ap.BdotR - A.BdotR) / h;
      J[2][j] = (Ap.t_ca  - A.t_ca)  / h;
    }
    // Cramer 3x3
    auto det3 = [](double m[3][3]) {
      return m[0][0]*(m[1][1]*m[2][2]-m[1][2]*m[2][1])
           - m[0][1]*(m[1][0]*m[2][2]-m[1][2]*m[2][0])
           + m[0][2]*(m[1][0]*m[2][1]-m[1][1]*m[2][0]);
    };
    const double D = det3(J);
    if (std::fabs(D) < 1e-30) break;
    double x[3];
    for (int c = 0; c < 3; ++c) {
      double M[3][3];
      for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) M[i][j] = (j == c) ? -f[i] : J[i][j];
      x[c] = det3(M) / D;
    }
    Vec3 step{x[0], x[1], x[2]};
    const double sn = norm(step);
    if (sn > 30.0) step = step * (30.0 / sn);   // limitation de pas
    dv += step;
  }
  const Vec3 dv_tmi_star = dv;

  // ---- LE CORRIDOR : borné par l'atmosphère et par le budget d'insertion -----
  double rp_max = RP_TARGET;
  for (double r = RP_TARGET; r < 40 * R_MARS; r *= 1.002) {
    if (dv_moi(r, vinf_arr, A_TARGET_ORBIT) > MOI_BUDGET) break;
    rp_max = r;
  }
  const double b_min = astro::b_from_rp(RP_ATMO, vinf_arr, MU_MARS);
  const double b_max = astro::b_from_rp(rp_max, vinf_arr, MU_MARS);

  std::printf("\n--- LE CORRIDOR (personne ne l'a dessine : il est deduit) -----------\n");
  std::printf("  v_inf d'arrivee = %.3f km/s\n", vinf_arr / 1000);
  std::printf("  BORNE INTERIEURE — l'atmosphere : r_p >= %.0f km  ->  |B| >= %.0f km\n",
              RP_ATMO / 1000, b_min / 1000);
  std::printf("  BORNE EXTERIEURE — le budget d'insertion (%.0f m/s pour une orbite\n", MOI_BUDGET);
  std::printf("      d'1 sol) : r_p <= %.0f km  ->  |B| <= %.0f km\n", rp_max / 1000, b_max / 1000);
  std::printf("      (dv_MOI au perigee vise = %.0f m/s ; a r_p_max = %.0f m/s)\n",
              dv_moi(RP_TARGET, vinf_arr, A_TARGET_ORBIT),
              dv_moi(rp_max, vinf_arr, A_TARGET_ORBIT));
  std::printf("  >>> LARGEUR DU CORRIDOR : %.0f km.  C'est une CONSEQUENCE, pas un reglage.\n",
              (b_max - b_min) / 1000);
  std::printf("  point de visee : |B| = %.0f km  (B.T = %.0f, B.R = %.0f)\n",
              b_aim / 1000, BT_aim / 1000, BR_aim / 1000);

  // ---- SENSIBILITES : dB / d(dv_TCM) a plusieurs dates ----------------------
  struct Strategy { const char* name; double t_tcm; double M[2][3]; };
  std::vector<Strategy> S = {
      {"TCM a L+30 j",         t_dep +  30.0 * DAY, {}},
      {"TCM a L+120 j",        t_dep + 120.0 * DAY, {}},
      {"TCM a l'arrivee -15 j", t_arr -  15.0 * DAY, {}},
  };
  auto A0 = fly(eph, t_dep, dep, dv_tmi_star, veh, 0, {}, t_stop);
  std::printf("\n--- SENSIBILITE dB/d(dv) : le levier d'une correction ---------------\n");
  for (auto& s : S) {
    const double h = 0.2;   // m/s
    for (int j = 0; j < 3; ++j) {
      Vec3 e{}; e[j] = h;
      auto Ap = fly(eph, t_dep, dep, dv_tmi_star, veh, 0, {{s.t_tcm, e}}, t_stop);
      if (!Ap.ok) { std::printf("  %s : echec\n", s.name); continue; }
      s.M[0][j] = (Ap.BdotT - A0.BdotT) / h;
      s.M[1][j] = (Ap.BdotR - A0.BdotR) / h;
    }
    const double lev = std::sqrt(s.M[0][0]*s.M[0][0] + s.M[0][1]*s.M[0][1] + s.M[0][2]*s.M[0][2]
                               + s.M[1][0]*s.M[1][0] + s.M[1][1]*s.M[1][1] + s.M[1][2]*s.M[1][2]);
    std::printf("  %-22s (T-%5.0f j) : |dB/dv| = %8.1f km par (m/s)\n",
                s.name, (A0.t_ca - s.t_tcm) / DAY, lev / 1000);
  }
  std::printf("  -> une TCM tardive a MOINS de levier : elle corrige moins...\n");
  std::printf("     ...mais son PROPRE bruit d'execution est aussi moins amplifie.\n");

  // ---- MONTE-CARLO : la dispersion LIVREE ----------------------------------
  // Une correction ne se calcule PAS en un coup de pseudo-inverse : le Delta-v
  // requis (des dizaines de m/s) sort du domaine de validite du jacobien. On
  // ITERE — methode de la corde, jacobien de reference reutilise. C'est ce que
  // fait une equipe de navigation, et ca coute des propagations.
  auto pinv_step = [](const double M[2][3], double r0, double r1, Vec3& d) {
    double MMt[2][2] = {{0, 0}, {0, 0}};
    for (int a = 0; a < 2; ++a)
      for (int b2 = 0; b2 < 2; ++b2)
        for (int j = 0; j < 3; ++j) MMt[a][b2] += M[a][j] * M[b2][j];
    const double det = MMt[0][0] * MMt[1][1] - MMt[0][1] * MMt[1][0];
    if (std::fabs(det) < 1e-12) return false;
    const double inv[2][2] = {{ MMt[1][1] / det, -MMt[0][1] / det},
                              {-MMt[1][0] / det,  MMt[0][0] / det}};
    const double y[2] = {-(inv[0][0] * r0 + inv[0][1] * r1),
                         -(inv[1][0] * r0 + inv[1][1] * r1)};
    d += Vec3{M[0][0] * y[0] + M[1][0] * y[1],
              M[0][1] * y[0] + M[1][1] * y[1],
              M[0][2] * y[0] + M[1][2] * y[1]};
    return true;
  };

  // CONCEPTION : on itere sur le MODELE (TCM a execution nominale). On ne voit
  // JAMAIS le tirage de Gates de la TCM qu'on est en train de calculer.
  auto design_tcm = [&](std::uint64_t seed, const std::vector<Tcm>& prefix, double t_k,
                        const double M[2][3]) {
    Vec3 d{};
    for (int it = 0; it < 5; ++it) {
      auto tcms = prefix;
      tcms.push_back({t_k, d});
      auto A = fly(eph, t_dep, dep, dv_tmi_star, veh, seed, tcms, t_stop, true); // MODELE
      if (!A.ok) break;
      const double r0 = A.BdotT - BT_aim, r1 = A.BdotR - BR_aim;
      if (std::hypot(r0, r1) < 5e3) break;
      if (!pinv_step(M, r0, r1, d)) break;
    }
    return d;
  };

  std::printf("\n--- MONTE-CARLO (%d tirages) ---------------------------------------\n", N);
  std::ofstream f("bplane.csv");
  f << "strategy,BT_km,BR_km,rp_km,dv_tcm_ms,inside\n";
  f.precision(8);

  struct Res { std::vector<double> BT, BR, dvt; int in{0}, n{0}; nav::Ellipse2D ell; };
  const char* names[4] = {"SANS TCM", "TCM PRECOCE seule (L+30)",
                          "TCM TARDIVE seule (A-15)", "ECHELLE : L+30 puis A-15"};
  std::vector<Res> R(4);

  for (int k = 0; k < N; ++k) {
    const std::uint64_t seed = 5000 + static_cast<std::uint64_t>(k);
    auto record = [&](Res& r, const Arrival& A, double dvt, const char* nm) {
      r.BT.push_back(A.BdotT / 1000); r.BR.push_back(A.BdotR / 1000);
      r.dvt.push_back(dvt);
      const double b = std::hypot(A.BdotT, A.BdotR);
      const bool in = (b >= b_min && b <= b_max);
      if (in) ++r.in;
      ++r.n;
      f << nm << ',' << A.BdotT / 1000 << ',' << A.BdotR / 1000 << ','
        << A.rp / 1000 << ',' << dvt << ',' << (in ? 1 : 0) << '\n';
    };

    // 0) sans correction
    auto Au = fly(eph, t_dep, dep, dv_tmi_star, veh, seed, {}, t_stop);
    if (!Au.ok) continue;
    record(R[0], Au, 0.0, names[0]);

    // 1) TCM PRECOCE seule : concue sur le modele, puis EXECUTEE (Gates).
    Vec3 d1 = design_tcm(seed, {}, S[0].t_tcm, S[0].M);
    auto A1 = fly(eph, t_dep, dep, dv_tmi_star, veh, seed, {{S[0].t_tcm, d1}}, t_stop);
    if (A1.ok) record(R[1], A1, norm(d1), names[1]);

    // 2) TCM TARDIVE seule
    Vec3 d2 = design_tcm(seed, {}, S[2].t_tcm, S[2].M);
    auto A2 = fly(eph, t_dep, dep, dv_tmi_star, veh, seed, {{S[2].t_tcm, d2}}, t_stop);
    if (A2.ok) record(R[2], A2, norm(d2), names[2]);

    // 3) ECHELLE — et c'est ici que se joue toute la mission.
    //
    //    a) on CONCOIT la TCM-1 sur le modele, et on l'EXECUTE (Gates la deforme) ;
    //    b) on NAVIGUE : on mesure ce que la TCM-1 a REELLEMENT produit ;
    //    c) on concoit la TCM-2 sur ce residu — qui est PETIT, donc la correction
    //       lineaire suffit (pas d'iteration : on est dans le domaine du jacobien) ;
    //    d) on l'execute.
    //
    //    Concevoir la TCM-2 sur un modele ou la TCM-1 est parfaite reviendrait a
    //    ne rien avoir a corriger. C'est le piege, et il est mortel.
    Vec3 e1 = design_tcm(seed, {}, S[0].t_tcm, S[0].M);
    auto Amid = fly(eph, t_dep, dep, dv_tmi_star, veh, seed, {{S[0].t_tcm, e1}}, t_stop);
    if (Amid.ok) {
      Vec3 e2{};
      pinv_step(S[2].M, Amid.BdotT - BT_aim, Amid.BdotR - BR_aim, e2);
      auto A3 = fly(eph, t_dep, dep, dv_tmi_star, veh, seed,
                    {{S[0].t_tcm, e1}, {S[2].t_tcm, e2}}, t_stop);
      if (A3.ok) record(R[3], A3, norm(e1) + norm(e2), names[3]);
    }
  }

  std::printf("\n%-26s %11s %11s %11s %12s\n",
              "strategie", "ellipse 3s", "(petit axe)", "dv_TCM p99", "P(corridor)");
  std::printf("%s\n", std::string(78, '-').c_str());
  for (std::size_t i = 0; i < R.size(); ++i) {
    if (R[i].n == 0) continue;
    R[i].ell = nav::covariance_ellipse(R[i].BT, R[i].BR, 3.0);
    auto sd = nav::summarize(R[i].dvt);
    std::printf("%-26s %9.0f km %9.0f km %9.1f m/s %10.1f %%\n",
                names[i], R[i].ell.semi_major, R[i].ell.semi_minor, sd.p99,
                100.0 * R[i].in / R[i].n);
  }
  std::printf("\n  CORRIDOR : %.0f <= |B| <= %.0f km   ->  LARGEUR %.0f km\n",
              b_min / 1000, b_max / 1000, (b_max - b_min) / 1000);
  std::printf("\n  >>> L'ECHELLE N'EST PAS UN CHOIX DE STYLE. Elle est FORCEE :\n");
  std::printf("      - la TCM precoce a un GROS levier : elle corrige l'erreur du TMI\n");
  std::printf("        pour quelques dizaines de m/s... mais sa propre faute d'execution\n");
  std::printf("        est amplifiee par 264 jours de vol restant.\n");
  std::printf("      - la TCM tardive a un PETIT levier : sa faute n'est presque pas\n");
  std::printf("        amplifiee... mais corriger l'erreur du TMI a ce levier-la\n");
  std::printf("        couterait des CENTAINES de m/s.\n");
  std::printf("      Aucune des deux ne suffit. Les deux ensemble, oui.\n");

  // ---- paramètres de l'ellipse, pour le tracé -------------------------------
  std::printf("\n--- PARAMETRES POUR LE TRACE (plan-B, km) --------------------------\n");
  std::printf("MARS_RP_ATMO %.1f\nB_MIN %.1f\nB_MAX %.1f\nB_AIM_T %.1f\nB_AIM_R %.1f\n",
              RP_ATMO / 1000, b_min / 1000, b_max / 1000, BT_aim / 1000, BR_aim / 1000);
  for (std::size_t i = 0; i < R.size(); ++i) {
    if (R[i].n == 0) continue;
    std::printf("ELLIPSE %zu cx=%.1f cy=%.1f a=%.1f b=%.1f angle=%.4f  \"%s\"\n",
                i, R[i].ell.cx, R[i].ell.cy, R[i].ell.semi_major, R[i].ell.semi_minor,
                R[i].ell.angle_rad, names[i]);
  }
  std::printf("\n  nuage complet ecrit : bplane.csv\n");
  return 0;
}
