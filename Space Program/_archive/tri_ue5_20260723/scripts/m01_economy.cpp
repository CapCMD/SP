// scripts/m01_economy.cpp
//
// L'ÉCONOMIE DE FIDÉLITÉ DE MODÈLE — le système signature, mis à l'épreuve.
//
// La doctrine AFFIRME : « concevoir dans un modèle grossier -> dispersion à
// l'arrivée plus grande -> correction plus grosse -> plus d'ergols -> plus de
// masse -> plus cher ». C'est une affirmation. Ce programme la MESURE, et rien
// ne garantit qu'elle survivra.
//
// PROTOCOLE
//   Conception A — CONIQUES RACCORDÉES : Lambert héliocentrique 2 corps vers un
//                  point de visée dans le plan-B de Mars. C'est tout ce que fait
//                  un porkchop. Aucune gravité terrestre au départ, aucune
//                  gravité martienne, pas de Jupiter, pas de poussée finie.
//   Conception B — N-CORPS : on part de A, et on CORRIGE DIFFÉRENTIELLEMENT dans
//                  le propagateur de vérité (Newton sur le Delta-v d'injection,
//                  jacobien par différences finies) jusqu'à toucher la cible.
//                  Ça coûte du TEMPS DE CALCUL — une ressource du jeu.
//
//   VÉRITÉ (identique pour les deux) : deux phases, sans changement de modèle.
//     phase 1  géocentrique  : orbite de parking -> TMI (poussée FINIE + Gates)
//                              -> 20 jours (bien au-delà de la sphere de Hill)
//     changement d'origine EXACT (translation par l'éphéméride — ce n'est PAS un
//     raccord de coniques : le modèle de forces ne change pas d'un iota)
//     phase 2  héliocentrique : croisière -> passage au plus près de Mars
//     Corps : Soleil, Terre, Lune, Mars, Jupiter, Vénus — des deux côtés.
#include <cstdio>
#include <cmath>
#include <vector>
#include "fen/astro/Porkchop.hpp"
#include "fen/astro/BPlane.hpp"
#include "fen/astro/Transfers.hpp"
#include "fen/flight/Session.hpp"
#include "fen/nav/Statistics.hpp"

using namespace fen;
using namespace fen::cst;

static constexpr double RP_MARS_TARGET = R_MARS + 400e3;   // périastre visé : 400 km
static constexpr double THETA_AIM      = 30.0 * DEG;       // orientation du point de visée
static constexpr double R_PARK         = R_EARTH + 200e3;
static constexpr double HANDOFF_DAYS   = 20.0;

// ---------------------------------------------------------------------------
// Géométrie de départ : l'orbite de parking d'INCLINAISON MINIMALE dont le plan
// contient l'asymptote sortante. C'est ce que fait un planificateur de lancement :
// i_min = |déclinaison de l'asymptote|. On n'a pas le choix du plan.
struct Departure { Vec3 r_park, v_park, v_hyp, dv_tmi, h_hat; double inc, dv; };

static Departure departure_geometry(const Vec3& vinf, double rp, double mu) {
  Departure d;
  const double vn = norm(vinf);
  const Vec3 u = unit(vinf);
  const Vec3 z{0, 0, 1};
  d.h_hat = unit(z - u * dot(z, u));            // normale : minimise l'inclinaison
  const double e = 1.0 + rp * vn * vn / mu;
  const double phi = std::atan2(std::sqrt(1.0 - 1.0 / (e * e)), -1.0 / e);
  const Vec3 e_hat = rotate(u, d.h_hat, -phi);  // direction du périastre
  const Vec3 w_hat = cross(d.h_hat, e_hat);
  const double v_p = std::sqrt(vn * vn + 2.0 * mu / rp);
  const double v_c = std::sqrt(mu / rp);
  d.r_park = e_hat * rp;
  d.v_park = w_hat * v_c;
  d.v_hyp  = w_hat * v_p;
  d.dv_tmi = d.v_hyp - d.v_park;
  d.dv = v_p - v_c;
  d.inc = std::acos(std::fmin(1.0, std::fabs(d.h_hat.z)));
  return d;
}

// ---------------------------------------------------------------------------
struct Arrival {
  bool ok{false};
  double t_ca{};
  double BdotT{}, BdotR{}, b{}, rp{}, vinf{};
  double prop_used{};
  std::string why;
};

struct Tcm { double t{}; Vec3 dv{}; };

// LA VÉRITÉ. Deux phases, un seul modèle de forces.
static Arrival fly(const ephem::IEphemeris& eph, double t_dep, const Departure& dep,
                   const Vec3& dv_tmi, double m0, const vehicle::Vehicle& veh,
                   std::uint64_t seed, const std::vector<Tcm>& tcms, double t_stop) {
  Arrival A;
  prop::PropOptions opt;
  opt.step.rtol = 1e-11;
  opt.step.h_max = 5.0 * DAY;
  opt.sample_dt = 0.0;

  // ---- phase 1 : géocentrique ----------------------------------------------
  flight::FlightPlan p1;
  p1.mission_id = "m01_dep";
  p1.center = ephem::Body::EarthBary;
  p1.perturbers = {ephem::Body::Sun, ephem::Body::Moon, ephem::Body::Mars,
                   ephem::Body::Jupiter, ephem::Body::Venus};
  p1.epoch0 = t_dep;
  p1.initial = State{dep.r_park, dep.v_park, m0};
  p1.vehicle = veh;
  p1.t_stop = t_dep + HANDOFF_DAYS * DAY;
  flight::BurnCmd tmi;
  tmi.id = "TMI"; tmi.t = t_dep; tmi.frame = flight::DvFrame::Inertial;
  tmi.hold = force::ThrustFrame::InertialFixed; tmi.stage = 0;
  tmi.dv = dv_tmi;
  p1.burns = {tmi};

  auto r1 = flight::execute(p1, eph, seed, opt);
  if (!r1.ok) { A.why = r1.failure; return A; }

  // ---- changement d'ORIGINE (exact) ----------------------------------------
  const double t_h = r1.truth.t_final;
  const auto E = eph.state(ephem::Body::EarthBary, ephem::Body::Sun, Epoch{t_h});
  const State s1 = State::unpack(r1.truth.y_final);
  const State s2{s1.r + E.r, s1.v + E.v, s1.m};

  // ---- phase 2 : héliocentrique --------------------------------------------
  flight::FlightPlan p2;
  p2.mission_id = "m01_cruise";
  p2.center = ephem::Body::Sun;
  p2.perturbers = {ephem::Body::EarthBary, ephem::Body::Mars, ephem::Body::Jupiter,
                   ephem::Body::Venus};
  p2.epoch0 = t_h;
  p2.initial = s2;
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
  // événement : passage au plus près de MARS (racine de (r-r_M).(v-v_M))
  p2.extra_events.push_back(prop::EventSpec{
      "MARS_CA",
      [&eph](double t, const StateN& y) {
        const auto M = eph.state(ephem::Body::Mars, ephem::Body::Sun, Epoch{t});
        return dot(pos(y) - M.r, vel(y) - M.v);
      },
      +1, false});

  auto r2 = flight::execute(p2, eph, seed ? seed + 777777ull : 0ull, opt);
  A.prop_used = r1.total_propellant + r2.total_propellant;
  if (!r2.ok) { A.why = r2.failure; return A; }

  for (const auto& ev : r2.truth.events) {
    if (ev.name != "MARS_CA") continue;
    const auto M = eph.state(ephem::Body::Mars, ephem::Body::Sun, Epoch{ev.t});
    const Vec3 rr = pos(ev.y) - M.r;
    const Vec3 vv = vel(ev.y) - M.v;
    const auto bp = astro::b_plane(rr, vv, MU_MARS);
    if (!bp.hyperbolic) { A.why = "capture 2-corps a Mars (impossible sans MOI)"; return A; }
    A.ok = true;
    A.t_ca = ev.t;
    A.BdotT = bp.BdotT; A.BdotR = bp.BdotR; A.b = bp.b;
    A.rp = bp.rp; A.vinf = bp.vinf;
    return A;
  }
  A.why = "Mars jamais approchee";
  return A;
}

// ---------------------------------------------------------------------------
int main(int argc, char** argv) {
  const int NMC = (argc > 1) ? std::atoi(argv[1]) : 30;
  ephem::StandishEphemeris eph;

  std::printf("=====================================================================\n");
  std::printf(" M01 — ECONOMIE DE FIDELITE DE MODELE : coniques raccordees vs N-corps\n");
  std::printf("=====================================================================\n");

  // ---- fenêtre issue du porkchop -------------------------------------------
  const double t_dep = epoch_from_iso("2026-10-31T08:00:00").tdb;
  const double tof0  = 295.0 * DAY;
  const double t_arr = t_dep + tof0;

  const auto E0 = eph.state(ephem::Body::EarthBary, ephem::Body::Sun, Epoch{t_dep});
  const auto M1 = eph.state(ephem::Body::Mars, ephem::Body::Sun, Epoch{t_arr});

  // ---- CONCEPTION A : coniques raccordées ----------------------------------
  // Lambert vers le POINT DE VISEE (Mars + offset dans le plan-B), pas vers le
  // centre de Mars : viser le centre, c'est viser une collision.
  Vec3 r_aim = M1.r;
  Vec3 v1_pc, v2_pc;
  double b_target = 0, BT_target = 0, BR_target = 0;
  for (int it = 0; it < 3; ++it) {
    auto L = astro::lambert(E0.r, r_aim, tof0, MU_SUN, true, 0);
    if (!L.ok) { std::printf("Lambert echoue\n"); return 1; }
    v1_pc = L.solutions[0].v1;
    v2_pc = L.solutions[0].v2;
    const Vec3 vinf_arr = v2_pc - M1.v;
    const double vinf_n = norm(vinf_arr);
    const Vec3 S = unit(vinf_arr);
    const Vec3 T = unit(cross(S, Vec3{0, 0, 1}));
    const Vec3 R = cross(S, T);
    b_target  = astro::b_from_rp(RP_MARS_TARGET, vinf_n, MU_MARS);
    BT_target = b_target * std::cos(THETA_AIM);
    BR_target = b_target * std::sin(THETA_AIM);
    r_aim = M1.r + T * BT_target + R * BR_target;
  }
  const Vec3 vinf_dep_pc = v1_pc - E0.v;
  const Departure dep = departure_geometry(vinf_dep_pc, R_PARK, MU_EARTH);

  std::printf("\n--- CONCEPTION A : CONIQUES RACCORDEES (Lambert 2 corps) ------------\n");
  std::printf("  depart  %s   arrivee (visee) %s\n",
              epoch_to_iso(Epoch{t_dep}).substr(0, 16).c_str(),
              epoch_to_iso(Epoch{t_arr}).substr(0, 16).c_str());
  std::printf("  C3 = %.3f km^2/s^2 | v_inf depart = %.3f km/s\n",
              norm(vinf_dep_pc) * norm(vinf_dep_pc) / 1e6, norm(vinf_dep_pc) / 1000);
  std::printf("  orbite de parking imposee : inclinaison MINIMALE = %.2f deg\n", dep.inc / DEG);
  std::printf("  Delta-v TMI = %.1f m/s\n", dep.dv);
  std::printf("  point de visee : b = %.1f km  (B.T = %.1f, B.R = %.1f km)  -> r_p = %.1f km\n",
              b_target / 1000, BT_target / 1000, BR_target / 1000, RP_MARS_TARGET / 1000);

  // ---- véhicule ------------------------------------------------------------
  vehicle::Engine eng;
  eng.id = "R-4D"; eng.thrust_vac = 490.0; eng.isp_vac = 312.0; eng.mass = 5.2;
  vehicle::Engine big;
  big.id = "TMI"; big.thrust_vac = 66700.0; big.isp_vac = 325.0; big.mass = 120.0;
  vehicle::Stage st;
  st.id = "CRUISE"; st.engine = big;
  st.tank.dry_fraction = 0.08;
  st.tank.residual_fraction = 0.02;
  st.structure_mass = 180.0;

  // DIMENSIONNEMENT PAR POINT FIXE : le joueur vise le TMI + une marge de
  // correction. La marge, il ne la connait pas encore — c'est TOUT le probleme
  // de ce programme. On provisionne 120 m/s (chiffre a priori, typique d'un
  // budget de TCM interplanetaire) et on verra si ca suffit.
  const double DV_MARGIN = 120.0;
  const double dv_needed = norm(dep.dv_tmi) + DV_MARGIN;
  auto sz = vehicle::size_stage_for_dv(dv_needed, 900.0, big, 0.08, 180.0, 0.02);
  st.tank.propellant_mass = sz.propellant;

  vehicle::Vehicle veh;
  veh.payload_dry = 900.0;
  veh.stages = {st};
  const double m0 = veh.total_mass();
  std::printf("  dimensionnement : TMI %.1f + marge %.0f m/s -> point fixe converge en %d iterations\n",
              norm(dep.dv_tmi), DV_MARGIN, sz.iterations);
  std::printf("  vehicule : CU %.0f kg | ergols %.0f kg | masse au depart %.0f kg | dv ideal %.0f m/s\n",
              veh.payload_dry, st.tank.propellant_mass, m0, veh.stage_dv(0));

  const double t_stop = t_arr + 40.0 * DAY;

  // ---- VOL NOMINAL de la conception A --------------------------------------
  auto A = fly(eph, t_dep, dep, dep.dv_tmi, m0, veh, 0, {}, t_stop);
  if (!A.ok) { std::printf("\n  *** vol A impossible : %s\n", A.why.c_str()); return 1; }

  const double missT_A = A.BdotT - BT_target;
  const double missR_A = A.BdotR - BR_target;
  const double miss_A = std::hypot(missT_A, missR_A);
  std::printf("\n  >>> VOL DANS LE PROPAGATEUR DE VERITE (execution nominale, sans Gates)\n");
  std::printf("      B.T = %+10.1f km  (cible %+9.1f)   ecart %+10.1f km\n",
              A.BdotT / 1000, BT_target / 1000, missT_A / 1000);
  std::printf("      B.R = %+10.1f km  (cible %+9.1f)   ecart %+10.1f km\n",
              A.BdotR / 1000, BR_target / 1000, missR_A / 1000);
  std::printf("      periastre atteint : %.0f km  (vise %.0f km)\n", A.rp / 1000, RP_MARS_TARGET / 1000);
  std::printf("      arrivee : %s  (visee %s)\n",
              epoch_to_iso(Epoch{A.t_ca}).substr(0, 16).c_str(),
              epoch_to_iso(Epoch{t_arr}).substr(0, 16).c_str());
  std::printf("\n      *** ERREUR PURE DE MODELE : %.0f km dans le plan-B ***\n", miss_A / 1000);
  std::printf("      (aucune erreur d'execution : c'est le PRIX DES CONIQUES RACCORDEES)\n");

  // ---- CONCEPTION B : correction differentielle N-corps ---------------------
  // Newton sur les 3 composantes du Delta-v d'injection, jacobien par differences
  // finies sur LE PROPAGATEUR DE VERITE. Coût : 4 vols par iteration.
  std::printf("\n--- CONCEPTION B : CORRECTION DIFFERENTIELLE N-CORPS -----------------\n");
  std::printf("  Newton 3x3 sur le dv d'injection ; cibles (B.T, B.R, t_arrivee).\n");
  Vec3 dv_b = dep.dv_tmi;
  int flights = 0;
  for (int it = 0; it < 8; ++it) {
    auto F = fly(eph, t_dep, dep, dv_b, m0, veh, 0, {}, t_stop); ++flights;
    if (!F.ok) { std::printf("  echec : %s\n", F.why.c_str()); break; }
    const double f[3] = {F.BdotT - BT_target, F.BdotR - BR_target, (F.t_ca - t_arr) * 1000.0};
    const double res = std::hypot(f[0], f[1]);
    std::printf("  it %d : |ecart plan-B| = %10.1f km | dt_arrivee = %+8.1f s | |dv| = %.3f m/s\n",
                it, res / 1000, F.t_ca - t_arr, norm(dv_b));
    if (res < 20e3) break;                       // 20 km : bien en deçà du besoin

    double Jm[3][3];
    const double h = 0.05;                       // 5 cm/s
    for (int j = 0; j < 3; ++j) {
      Vec3 dvp = dv_b;
      if (j == 0) dvp.x += h; else if (j == 1) dvp.y += h; else dvp.z += h;
      auto Fp = fly(eph, t_dep, dep, dvp, m0, veh, 0, {}, t_stop); ++flights;
      if (!Fp.ok) { std::printf("  jacobien : vol perturbe impossible\n"); return 1; }
      Jm[0][j] = (Fp.BdotT - F.BdotT) / h;
      Jm[1][j] = (Fp.BdotR - F.BdotR) / h;
      Jm[2][j] = ((Fp.t_ca - F.t_ca) * 1000.0) / h;
    }
    // resolution 3x3 (Cramer)
    const double det = Jm[0][0]*(Jm[1][1]*Jm[2][2]-Jm[1][2]*Jm[2][1])
                     - Jm[0][1]*(Jm[1][0]*Jm[2][2]-Jm[1][2]*Jm[2][0])
                     + Jm[0][2]*(Jm[1][0]*Jm[2][1]-Jm[1][1]*Jm[2][0]);
    if (std::fabs(det) < 1e-12) { std::printf("  jacobien singulier\n"); break; }
    double sol[3];
    for (int c = 0; c < 3; ++c) {
      double Mx[3][3];
      for (int r = 0; r < 3; ++r) for (int k = 0; k < 3; ++k) Mx[r][k] = Jm[r][k];
      for (int r = 0; r < 3; ++r) Mx[r][c] = -f[r];
      const double d = Mx[0][0]*(Mx[1][1]*Mx[2][2]-Mx[1][2]*Mx[2][1])
                     - Mx[0][1]*(Mx[1][0]*Mx[2][2]-Mx[1][2]*Mx[2][0])
                     + Mx[0][2]*(Mx[1][0]*Mx[2][1]-Mx[1][1]*Mx[2][0]);
      sol[c] = d / det;
    }
    dv_b += Vec3{sol[0], sol[1], sol[2]};
  }
  auto B = fly(eph, t_dep, dep, dv_b, m0, veh, 0, {}, t_stop);
  const double miss_B = std::hypot(B.BdotT - BT_target, B.BdotR - BR_target);
  std::printf("\n  >>> CONVERGE en %d vols du propagateur de verite\n", flights);
  std::printf("      ecart plan-B residuel : %.1f km   (contre %.0f km en coniques raccordees)\n",
              miss_B / 1000, miss_A / 1000);
  std::printf("      dv d'injection : %.2f m/s  (contre %.2f)  -> ecart %+.2f m/s\n",
              norm(dv_b), norm(dep.dv_tmi), norm(dv_b) - norm(dep.dv_tmi));
  std::printf("      COUT : %d propagations completes. C'est le TEMPS DE CALCUL,\n", flights);
    // ================= CE QUE COUTE DE NE RIEN FAIRE =========================
  // L'ecart dans le plan-B n'est pas une imprecision : c'est un PERIASTRE
  // d'arrivee. Et le cout de l'insertion depend du periastre par l'effet
  // Oberth : plus on passe bas, moins l'insertion coute.
  //   dv_capture_min(rp) = sqrt(vinf^2 + 2mu/rp) - sqrt(2mu/rp)
  auto dv_capture = [](double rp, double vinf) {
    return std::sqrt(vinf * vinf + 2.0 * MU_MARS / rp) - std::sqrt(2.0 * MU_MARS / rp);
  };
  const double dvc_target = dv_capture(RP_MARS_TARGET, A.vinf);
  const double dvc_actual = dv_capture(A.rp, A.vinf);
  std::printf("\n--- ET SI ON NE CORRIGEAIT PAS ? (l'effet Oberth se venge) ---------\n");
  std::printf("  Le plan-B n'est pas une imprecision : c'est un PERIASTRE d'arrivee.\n");
  std::printf("  Insertion minimale (atteindre juste la vitesse de liberation) :\n");
  std::printf("    au periastre VISE   (%8.0f km) : dv = %7.1f m/s\n",
              RP_MARS_TARGET / 1000, dvc_target);
  std::printf("    au periastre ATTEINT (%8.0f km) : dv = %7.1f m/s\n", A.rp / 1000, dvc_actual);
  std::printf("    >>> SURCOUT D'INSERTION : %+.0f m/s  (soit %.0f %% du TMI lui-meme)\n",
              dvc_actual - dvc_target, 100.0 * (dvc_actual - dvc_target) / norm(dep.dv_tmi));
  {
    const double m_arr = 1616.0;  // ordre de grandeur de la masse a l'arrivee
    const double ve = big.isp_vac * G0;
    const double mp_t = m_arr * (1.0 - std::exp(-dvc_target / ve));
    const double mp_a = m_arr * (1.0 - std::exp(-dvc_actual / ve));
    std::printf("    en ergols (masse a l'arrivee ~%.0f kg) : %.0f kg  ->  %.0f kg   (+%.0f kg)\n",
                m_arr, mp_t, mp_a, mp_a - mp_t);
    std::printf("    la charge utile fait %.0f kg. Le surcout la MANGE.\n", veh.payload_dry);
  }
  std::printf("\n  MORALE : corriger tot coute des DIZAINES de m/s. Ne pas corriger en\n");
  std::printf("  coute des MILLIERS. Ce n'est pas une penalite de jeu : c'est Oberth.\n");

std::printf("      et c'est une ressource du jeu.\n");

  // ---- LA FACTURE : Delta-v de correction (TCM a L+30 j) --------------------
  // Newton 3x3 sur le dv de la TCM pour ramener le plan-B sur la cible.
  auto solve_tcm = [&](const Vec3& dv_tmi_used, std::uint64_t seed) -> double {
    const double t_tcm = t_dep + 30.0 * DAY;
    Vec3 dv{0, 0, 0};
    for (int it = 0; it < 6; ++it) {
      auto F = fly(eph, t_dep, dep, dv_tmi_used, m0, veh, seed, {{t_tcm, dv}}, t_stop);
      if (!F.ok) return -1.0;
      const double f[3] = {F.BdotT - BT_target, F.BdotR - BR_target, (F.t_ca - t_arr) * 1000.0};
      if (std::hypot(f[0], f[1]) < 30e3) return norm(dv);
      double Jm[3][3];
      const double h = 0.02;
      for (int j = 0; j < 3; ++j) {
        Vec3 dp = dv;
        if (j == 0) dp.x += h; else if (j == 1) dp.y += h; else dp.z += h;
        auto Fp = fly(eph, t_dep, dep, dv_tmi_used, m0, veh, seed, {{t_tcm, dp}}, t_stop);
        if (!Fp.ok) return -1.0;
        Jm[0][j] = (Fp.BdotT - F.BdotT) / h;
        Jm[1][j] = (Fp.BdotR - F.BdotR) / h;
        Jm[2][j] = ((Fp.t_ca - F.t_ca) * 1000.0) / h;
      }
      const double det = Jm[0][0]*(Jm[1][1]*Jm[2][2]-Jm[1][2]*Jm[2][1])
                       - Jm[0][1]*(Jm[1][0]*Jm[2][2]-Jm[1][2]*Jm[2][0])
                       + Jm[0][2]*(Jm[1][0]*Jm[2][1]-Jm[1][1]*Jm[2][0]);
      if (std::fabs(det) < 1e-12) return -1.0;
      double sol[3];
      for (int c = 0; c < 3; ++c) {
        double Mx[3][3];
        for (int r = 0; r < 3; ++r) for (int k = 0; k < 3; ++k) Mx[r][k] = Jm[r][k];
        for (int r = 0; r < 3; ++r) Mx[r][c] = -f[r];
        const double d = Mx[0][0]*(Mx[1][1]*Mx[2][2]-Mx[1][2]*Mx[2][1])
                       - Mx[0][1]*(Mx[1][0]*Mx[2][2]-Mx[1][2]*Mx[2][0])
                       + Mx[0][2]*(Mx[1][0]*Mx[2][1]-Mx[1][1]*Mx[2][0]);
        sol[c] = d / det;
      }
      dv += Vec3{sol[0], sol[1], sol[2]};
    }
    return norm(dv);
  };

  std::printf("\n--- LA FACTURE : Delta-v de la manoeuvre de correction (TCM a L+30 j) --\n");
  std::printf("  Execution NOMINALE (aucune erreur de Gates) : la TCM ne paie que le MODELE.\n");
  const double tcmA_nom = solve_tcm(dep.dv_tmi, 0);
  const double tcmB_nom = solve_tcm(dv_b, 0);
  std::printf("    coniques raccordees : TCM = %7.2f m/s\n", tcmA_nom);
  std::printf("    N-corps             : TCM = %7.2f m/s\n", tcmB_nom);
  std::printf("    PRIX DU MODELE GROSSIER : %+.2f m/s\n", tcmA_nom - tcmB_nom);

  std::printf("\n  Avec ERREURS D'EXECUTION (Gates), %d tirages :\n", NMC);
  std::vector<double> tA, tB;
  for (int k = 0; k < NMC; ++k) {
    const std::uint64_t s = 6100 + static_cast<std::uint64_t>(k);
    const double a = solve_tcm(dep.dv_tmi, s);
    const double b = solve_tcm(dv_b, s);
    if (a > 0) tA.push_back(a);
    if (b > 0) tB.push_back(b);
  }
  auto sA = nav::summarize(tA);
  auto sB = nav::summarize(tB);
  std::printf("    %-22s %8s %8s %8s %8s\n", "conception", "moy", "sigma", "p95", "p99");
  std::printf("    %-22s %8.2f %8.2f %8.2f %8.2f  m/s\n",
              "coniques raccordees", sA.mean, sA.sigma, sA.p95, sA.p99);
  std::printf("    %-22s %8.2f %8.2f %8.2f %8.2f  m/s\n",
              "N-corps", sB.mean, sB.sigma, sB.p95, sB.p99);

  // ---- CONVERSION EN KILOGRAMMES -------------------------------------------
  const double m_arr = m0 - A.prop_used;
  const double kgA = astro::propellant_for_dv(m_arr, sA.p99, big.isp_vac);
  const double kgB = astro::propellant_for_dv(m_arr, sB.p99, big.isp_vac);
  std::printf("\n  MARGE A PROVISIONNER (p99), convertie en ergols (masse a l'arrivee %.0f kg) :\n", m_arr);
  std::printf("    coniques raccordees : %6.1f kg\n", kgA);
  std::printf("    N-corps             : %6.1f kg\n", kgB);
  std::printf("    ECART               : %+6.1f kg  <- LA FACTURE DU MODELE GROSSIER\n", kgA - kgB);
  return 0;
}
