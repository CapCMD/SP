// scripts/t01_dsm.cpp — MGA-1DSM : le Delta-v se DEPLACE, il ne disparait pas.
#include <cstdio>
#include <cmath>
#include <vector>
#include "fen/astro/Mga1Dsm.hpp"
#include "fen/astro/Mga.hpp"
#include "fen/core/Epoch.hpp"
using namespace fen; using namespace fen::cst; using ephem::Body;
static constexpr double R_VENUS = 6051.8e3, R_JUPITER = 71492e3;

int main(int argc, char** argv) {
  const int gens = (argc > 1) ? std::atoi(argv[1]) : 1200;
  ephem::StandishEphemeris eph;
  std::printf("=====================================================================\n");
  std::printf(" T01 — MGA-1DSM : la manoeuvre en espace profond\n");
  std::printf(" (brique IMPOSEE par la mesure : 87 %% du dv venait d'un survol\n");
  std::printf("  mal raccorde. Ici, les survols sont non propulses PAR CONSTRUCTION.)\n");
  std::printf("=====================================================================\n");

  astro::Mga1DsmProblem p;
  p.seq = {Body::EarthBary, Body::Venus, Body::Venus, Body::EarthBary, Body::Jupiter, Body::Saturn};
  p.rp_min = {1.05 * R_VENUS, 1.05 * R_VENUS, 1.05 * R_EARTH, 6.0 * R_JUPITER};
  p.rp_max = {6.0 * R_VENUS, 6.0 * R_VENUS, 6.5 * R_EARTH, 200.0 * R_JUPITER};
  p.t0_lo = epoch_from_iso("2030-01-01T00:00:00").tdb;
  p.t0_hi = epoch_from_iso("2040-01-01T00:00:00").tdb;
  p.vinf_lo = 2800.0; p.vinf_hi = 4500.0;       // C3 = 7.8 a 20.3 km2/s2
  p.tof_lo = { 80.0*DAY, 150.0*DAY,  60.0*DAY,  500.0*DAY, 1000.0*DAY};
  p.tof_hi = {400.0*DAY, 600.0*DAY, 450.0*DAY, 2000.0*DAY, 3000.0*DAY};
  p.tof_total_max = 9.0 * 365.25 * DAY;
  p.c3_max = 20e6;
  p.rp_insert = 2.5 * R_SATURN;
  p.a_insert = std::cbrt(MU_SATURN * std::pow(120.0 * DAY, 2.0) / (4.0 * PI * PI));

  // --- LA CONTRAINTE QUI ELAGUE, et qu'on DERIVE au lieu de la deviner --------
  // Pour ouvrir Jupiter depuis la Terre : |v_helio| = v_Hohmann(1 -> 5.203 UA).
  const auto hj = astro::hohmann(1.0 * AU, 5.2029 * AU, MU_SUN);
  const double v_earth_orb = std::sqrt(MU_SUN / AU);
  const double vinf_earth_min = (v_earth_orb + hj.dv1) - v_earth_orb;   // = hj.dv1
  p.vinf_min = {0.0, 0.0, vinf_earth_min, 0.0};   // sur le survol TERRESTRE
  std::printf("\n--- LA CONTRAINTE QU'ON DERIVE AU LIEU DE LA DEVINER ----------------\n");
  std::printf("  v_helio requis pour ouvrir Jupiter (Hohmann) : %.2f km/s\n",
              (v_earth_orb + hj.dv1) / 1000);
  std::printf("  vitesse orbitale terrestre                   : %.2f km/s\n", v_earth_orb / 1000);
  std::printf("  >>> |v_inf| AU SURVOL TERRESTRE >= %.2f km/s. Borne DURE.\n",
              vinf_earth_min / 1000);
  std::printf("      Un tour qui ne la respecte pas ne peut PAS atteindre Jupiter,\n");
  std::printf("      et paie l'impossibilite en DSM geante (mesure : 7 890 m/s).\n");
  std::printf("      La CONNAISSANCE PHYSIQUE ACHETE DU TEMPS DE CALCUL.\n");

  const int F = astro::d1_flybys(p), D = astro::d1_nvars(p);
  std::vector<double> lo(D), hi(D);
  lo[0]=p.t0_lo;   hi[0]=p.t0_hi;
  lo[1]=p.vinf_lo; hi[1]=p.vinf_hi;
  lo[2]=0.0;       hi[2]=1.0;
  lo[3]=0.0;       hi[3]=1.0;
  lo[4]=0.02;      hi[4]=0.90;
  lo[5]=p.tof_lo[0]; hi[5]=p.tof_hi[0];
  for (int k = 1; k <= F; ++k) {
    const int b = 6 + 4*(k-1);
    lo[b+0]=-TWO_PI;      hi[b+0]=TWO_PI;          // angle du plan-B
    lo[b+1]=p.rp_min[k-1];hi[b+1]=p.rp_max[k-1];   // periastre du survol
    lo[b+2]=0.02;         hi[b+2]=0.90;            // fraction eta de la DSM
    lo[b+3]=p.tof_lo[k];  hi[b+3]=p.tof_hi[k];     // duree de la jambe
  }
  std::printf("\n  inconnues : %d  (t0, v_inf, 2 angles, puis par survol :\n", D);
  std::printf("              angle plan-B, periastre, position de la DSM, duree)\n");

  auto f = [&](const std::vector<double>& x){ return astro::mga1dsm_evaluate(p, eph, x).cost; };

  // ===================================================================
  // ETAGE 1 — LE MODELE PAS CHER FOURNIT LE BASSIN.
  // Chercher directement dans 22 dimensions multimodales, c'est jeter du temps
  // de calcul : mesure faite, 1,9 M d'evaluations donnent 24 km/s de DSM, soit
  // une recherche NON CONVERGEE. Un MGA pur (11 variables) trouve les BONNES
  // DATES pour 10x moins cher. On s'en sert.
  // C'est l'economie de fidelite de modele, appliquee a la RECHERCHE elle-meme.
  // ===================================================================
  std::printf("\n--- ETAGE 1 : MGA pur (11 variables) trouve le BASSIN ---------------\n");
  astro::MgaProblem pm;
  pm.seq = p.seq; pm.rp_min = p.rp_min;
  pm.t0_lo = p.t0_lo; pm.t0_hi = p.t0_hi;
  pm.tof_lo = p.tof_lo; pm.tof_hi = p.tof_hi;
  pm.max_revs = 2; pm.c3_max = p.c3_max;
  pm.tof_total_max = p.tof_total_max;
  pm.rp_insert = p.rp_insert; pm.a_insert = p.a_insert;
  const int Dm = astro::n_vars(pm), Lm = astro::n_legs(pm);
  std::vector<double> mlo(Dm), mhi(Dm);
  mlo[0]=pm.t0_lo; mhi[0]=pm.t0_hi;
  for (int i=0;i<Lm;++i){ mlo[1+i]=pm.tof_lo[i]; mhi[1+i]=pm.tof_hi[i]; }
  for (int i=0;i<Lm;++i){ mlo[1+Lm+i]=0.0; mhi[1+Lm+i]=2.999; }
  auto fm = [&](const std::vector<double>& x){ return astro::mga_evaluate(pm, eph, x).cost; };
  astro::MgaResult rm; double bm = 1e300; std::vector<double> xm;
  for (int run = 0; run < 10; ++run) {
    auto de = astro::differential_evolution(fm, mlo, mhi, 120, 800, 777ull + run*7919ull);
    auto r = astro::mga_evaluate(pm, eph, de.x);
    if (de.f < bm && r.feasible) { bm = de.f; rm = r; xm = de.x; }
  }
  if (!rm.feasible) { std::printf("  *** pas de bassin.\n"); return 1; }
  std::printf("  bassin : lancement %s, C3 = %.2f km2/s2, %.2f ans\n",
              epoch_to_iso(Epoch{rm.t[0]}).substr(0,10).c_str(), rm.c3/1e6,
              rm.tof_total/(365.25*DAY));

  // --- traduction MGA -> MGA-1DSM : les DATES, pas les Delta-v ---
  std::vector<double> xs(D, 0.0);
  xs[0] = rm.t[0];
  xs[1] = std::clamp(std::sqrt(rm.c3), p.vinf_lo, p.vinf_hi);
  xs[2] = 0.5; xs[3] = 0.5;
  xs[4] = 0.4;
  xs[5] = rm.t[1] - rm.t[0];
  for (int k = 1; k <= F; ++k) {
    const int b = 6 + 4*(k-1);
    xs[b+0] = 0.0;
    xs[b+1] = std::clamp(rm.rp[k-1], p.rp_min[k-1], p.rp_max[k-1]);
    xs[b+2] = 0.4;
    xs[b+3] = std::clamp(rm.t[k+1] - rm.t[k], p.tof_lo[k], p.tof_hi[k]);
  }
  // On RESSERRE la boite autour du bassin : c'est la ou l'affinage a un sens.
  lo[0] = std::fmax(p.t0_lo, xs[0] - 150.0*DAY);
  hi[0] = std::fmin(p.t0_hi, xs[0] + 150.0*DAY);
  lo[5] = std::fmax(p.tof_lo[0], xs[5]*0.6); hi[5] = std::fmin(p.tof_hi[0], xs[5]*1.4);
  for (int k = 1; k <= F; ++k) {
    const int b = 6 + 4*(k-1);
    lo[b+3] = std::fmax(p.tof_lo[k], xs[b+3]*0.6);
    hi[b+3] = std::fmin(p.tof_hi[k], xs[b+3]*1.4);
  }
  std::printf("  boite resserree autour du bassin -> l'affinage a un sens.\n");

  // ===================================================================
  // ETAGE 2 — LE MODELE CHER AFFINE.
  // ===================================================================
  astro::Mga1DsmResult best; double bf = 1e300; long long ev = 0;
  std::vector<double> xbest = xs;
  std::printf("\n--- ETAGE 2 : MGA-1DSM (22 var.) — DE amorcee puis BASIN HOPPING ----\n");
  for (int run = 0; run < 6; ++run) {
    auto de = astro::differential_evolution(f, lo, hi, 200, gens, 424242ull + run*104729ull,
                                            0.7, 0.9, &xs);
    ev += de.evals;
    if (de.f < bf) { bf = de.f; xbest = de.x; }
  }
  std::printf("  apres DE amorcee     : %8.1f m/s  (%lld evaluations)\n", bf, ev);

  // MBH : sauter de bassin en bassin, n'accepter que les ameliorations.
  auto refine = [&](const std::vector<double>& l2, const std::vector<double>& h2,
                    const std::vector<double>& xseed, std::uint64_t sd) {
    return astro::differential_evolution(f, l2, h2, 60, 220, sd, 0.7, 0.9, &xseed);
  };
  auto mbh = astro::basin_hopping(f, lo, hi, xbest, 60, 0.18, 987654321ull, refine);
  ev += mbh.evals;
  std::printf("  apres BASIN HOPPING  : %8.1f m/s  (%d sauts, %lld evaluations au total)\n",
              mbh.f, mbh.hops, ev);
  if (mbh.f < bf) { bf = mbh.f; xbest = mbh.x; }
  best = astro::mga1dsm_evaluate(p, eph, xbest);
  if (!best.feasible) { std::printf("\n  *** rien trouve.\n"); return 1; }

  const char* nm[] = {"TERRE","VENUS","VENUS","TERRE","JUPITER","SATURNE"};
  std::printf("\n--- MEILLEUR TOUR --------------------------------------------------\n");
  for (std::size_t i = 0; i < best.t.size(); ++i) {
    std::printf("  %-8s %s", nm[i], epoch_to_iso(Epoch{best.t[i]}).substr(0,10).c_str());
    if (i>0 && i+1<best.t.size()) {
      const std::size_t k=i-1;
      const double R = (p.seq[i]==Body::Jupiter)?R_JUPITER:(p.seq[i]==Body::Venus)?R_VENUS:R_EARTH;
      std::printf("  survol %7.2f R | v_inf %5.2f km/s | deviation %5.1f deg | GAGNE %5.2f km/s",
                  best.rp[k]/R, best.vinf_fb[k]/1000, best.turn[k]/DEG, best.gain[k]/1000);
    }
    std::printf("\n");
    if (i < best.dsm.size())
      std::printf("           |  DSM %s : %7.1f m/s\n",
                  epoch_to_iso(Epoch{best.t_dsm[i]}).substr(0,10).c_str(), best.dsm[i]);
  }
  double g=0; for (double x2 : best.gain) g += x2;
  std::printf("\n  C3 de lancement       : %7.2f km2/s2\n", best.c3/1e6);
  std::printf("  somme des DSM         : %7.0f m/s\n", best.dv_dsm_total);
  std::printf("  Delta-v des SURVOLS   : %7.0f m/s     <- ZERO. Par construction.\n", 0.0);
  std::printf("  insertion a Saturne   : %7.0f m/s   (v_inf = %.2f km/s)\n",
              best.dv_insert, best.vinf_arr/1000);
  std::printf("  >>> Delta-v EMBARQUE  : %7.0f m/s\n", best.dv_onboard);
  std::printf("  >>> Delta-v ENCAISSE  : %7.2f km/s\n", g/1000);
  std::printf("  duree                 : %7.2f ans\n", best.tof_total/(365.25*DAY));

  std::printf("\n--- LES QUATRE ROUTES ----------------------------------------------\n");
  std::printf("  %-28s %9s %13s %9s\n", "", "C3", "dv embarque", "duree");
  std::printf("  %s\n", std::string(64,'-').c_str());
  std::printf("  %-28s %7.1f      %7s      %5.1f ans\n","DIRECT (Hohmann)",105.9,"832 m/s",6.1);
  std::printf("  %-28s %7.1f      %7s      %5.1f ans\n","1 survol Jupiter",83.6,"634 m/s",11.6);
  std::printf("  %-28s %7.2f     %6.0f m/s     %5.2f ans\n","V-V-E-J-S, MGA pur",
              rm.c3/1e6, rm.dv_onboard, rm.tof_total/(365.25*DAY));
  std::printf("  %-28s %7.2f     %6.0f m/s     %5.2f ans\n","V-V-E-J-S, MGA-1DSM",
              best.c3/1e6, best.dv_onboard, best.tof_total/(365.25*DAY));
  std::printf("  %-28s %7.1f      %7s      %5.1f ans\n","Cassini (reel)",16.6,"~1400 m/s",6.7);
  std::printf("\n  >>> Le Delta-v n'a pas DISPARU : il s'est DEPLACE.\n");
  std::printf("      Du periastre — ou l'on payait un desaccord brutal de 7,2 km/s —\n");
  std::printf("      vers l'espace profond, ou l'on ne paie qu'une mise en forme.\n");
  std::printf("      Et le survol, lui, ne coute plus RIEN : |v_inf| est conserve\n");
  std::printf("      a la precision machine, parce qu'on ne le CHOISIT plus.\n");
  return 0;
}
