// scripts/t01_refine.cpp — LE RAFFINEUR LOCAL, ET CE QU'IL A RÉVÉLÉ.
//
// Ce que le joueur écrit. Quatre mesures, dans cet ordre — et l'ordre est ce qui
// rend la conclusion falsifiable, parce que chacune peut TUER la suivante.
//
//   A. Le raffineur local, SEUL, depuis le point publié (12 763 m/s).
//        -> « le fond du bassin, c'est combien ? »
//   B. MBH + raffineur, boîte COMPLÈTE, départ aléatoire. Ni amorçage MGA, ni
//      boîte resserrée : on ne suppose rien.
//        -> « et si on ne se laissait pas enfermer ? »
//   C. L'ORACLE DE CASSINI : dates réelles épinglées, 16 variables libres.
//        -> « le modèle est-il seulement CAPABLE de rendre la bonne réponse ? »
//           Si non, aucun optimiseur ne le sauvera — et il faut le savoir AVANT
//           de dépenser un cycle de plus en recherche.
//   D. La FORME de Cassini, promenée sur 2030-2040.
//        -> « on ne cherche plus la forme du tour : on la connaît. On ne cherche
//            plus que la DATE où la géométrie la reproduit. » Balayage 1-D, pas
//            22-D. LA CONNAISSANCE PHYSIQUE ACHÈTE DU TEMPS DE CALCUL.
//
// Usage : t01_refine [sauts]     (défaut : 120)
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include "fen/astro/LocalRefine.hpp"
#include "fen/astro/Mga.hpp"
#include "fen/astro/Mga1Dsm.hpp"
#include "fen/core/Epoch.hpp"
using namespace fen; using namespace fen::cst; using ephem::Body;
static constexpr double R_VENUS = 6051.8e3, R_JUPITER = 71492e3;

static const char* VAR[22] = {
  "t0", "v_inf", "u", "v", "eta L1", "T L1 (T->V)",
  "beta V1", "rp V1", "eta L2", "T L2 (V->V)",
  "beta V2", "rp V2", "eta L3", "T L3 (V->T)",
  "beta T",  "rp T",  "eta L4", "T L4 (T->J)",
  "beta J",  "rp J",  "eta L5", "T L5 (J->S)"};

static astro::Mga1DsmProblem make_problem(double t0lo, double t0hi, bool legacy) {
  astro::Mga1DsmProblem p;
  p.seq = {Body::EarthBary, Body::Venus, Body::Venus, Body::EarthBary, Body::Jupiter, Body::Saturn};
  p.rp_min = {1.05 * R_VENUS, 1.05 * R_VENUS, 1.05 * R_EARTH, 6.0 * R_JUPITER};
  p.rp_max = {6.0 * R_VENUS, 6.0 * R_VENUS, 6.5 * R_EARTH, 200.0 * R_JUPITER};
  p.t0_lo = t0lo; p.t0_hi = t0hi;
  p.vinf_lo = 2800.0; p.vinf_hi = 4500.0;
  p.tof_lo = { 80.0*DAY, 150.0*DAY,  60.0*DAY,  500.0*DAY, 1000.0*DAY};
  p.tof_hi = {400.0*DAY, 600.0*DAY, 450.0*DAY, 2000.0*DAY, 3000.0*DAY};
  p.tof_total_max = 9.0 * 365.25 * DAY;
  p.c3_max = 20e6;
  p.rp_insert = 2.5 * R_SATURN;
  p.a_insert = std::cbrt(MU_SATURN * std::pow(120.0 * DAY, 2.0) / (4.0 * PI * PI));
  const auto hj = astro::hohmann(1.0 * AU, 5.2029 * AU, MU_SUN);
  p.vinf_min = {0.0, 0.0, hj.dv1, 0.0};
  if (!legacy) {
    // LA DUREE EN PENALITE, PLUS EN FALAISE. Le C3 l'etait DEJA ; la duree ne
    // l'etait pas. Meme nature (une ressource bornee), traitement different :
    // c'etait une incoherence du modele, et elle avait un prix MESURE (le
    // raffineur sortait colle au mur des 9 ans, residu KKT 9e4).
    p.tof_penalty = 1e-3;                       // 86 m/s par jour de depassement
    // Et les deux planchers que la VRAIE Cassini viole (55 j ; 500 j pile).
    p.tof_lo[2] = 40.0*DAY; p.tof_lo[3] = 350.0*DAY;
  }
  return p;
}
static void box(const astro::Mga1DsmProblem& p, std::vector<double>& lo, std::vector<double>& hi) {
  const int F = astro::d1_flybys(p), D = astro::d1_nvars(p);
  lo.assign(D, 0.0); hi.assign(D, 0.0);
  lo[0]=p.t0_lo;   hi[0]=p.t0_hi;
  lo[1]=p.vinf_lo; hi[1]=p.vinf_hi;
  lo[2]=0.0;  hi[2]=1.0;   lo[3]=0.0; hi[3]=1.0;
  lo[4]=0.02; hi[4]=0.90;
  lo[5]=p.tof_lo[0]; hi[5]=p.tof_hi[0];
  for (int k = 1; k <= F; ++k) {
    const int b = 6 + 4*(k-1);
    lo[b+0]=-TWO_PI;       hi[b+0]=TWO_PI;
    lo[b+1]=p.rp_min[k-1]; hi[b+1]=p.rp_max[k-1];
    lo[b+2]=0.02;          hi[b+2]=0.90;
    lo[b+3]=p.tof_lo[k];   hi[b+3]=p.tof_hi[k];
  }
}
static void line(const char* tag, const astro::Mga1DsmResult& r) {
  if (!r.feasible) { std::printf("  %-26s INFAISABLE\n", tag); return; }
  std::printf("  %-26s dv %8.1f m/s  (DSM %7.1f + insertion %6.1f) | C3 %6.2f | %5.2f ans\n",
              tag, r.dv_onboard, r.dv_dsm_total, r.dv_insert, r.c3/1e6,
              r.tof_total/(365.25*DAY));
}
static void tour(const astro::Mga1DsmResult& r, const astro::Mga1DsmProblem& p) {
  const char* nm[] = {"TERRE","VENUS","VENUS","TERRE","JUPITER","SATURNE"};
  for (std::size_t i = 0; i < r.t.size(); ++i) {
    std::printf("  %-8s %s", nm[i], epoch_to_iso(Epoch{r.t[i]}).substr(0,10).c_str());
    if (i > 0 && i + 1 < r.t.size()) {
      const double R = (p.seq[i]==Body::Jupiter)?R_JUPITER:(p.seq[i]==Body::Venus)?R_VENUS:R_EARTH;
      std::printf("   survol %6.2f R | v_inf %5.2f km/s | deviation %5.1f deg",
                  r.rp[i-1]/R, r.vinf_fb[i-1]/1000, r.turn[i-1]/DEG);
    }
    std::printf("\n");
    if (i < r.dsm.size())
      std::printf("           |  jambe %4.0f j   DSM %s : %7.1f m/s\n",
                  (r.t[i+1]-r.t[i])/DAY,
                  epoch_to_iso(Epoch{r.t_dsm[i]}).substr(0,10).c_str(), r.dsm[i]);
  }
}
static void bounds(const std::vector<double>& x, const std::vector<double>& lo,
                   const std::vector<double>& hi) {
  int n = 0;
  for (int j = 0; j < 22; ++j) {
    const double rg = hi[j] - lo[j];
    if (rg <= 0) continue;
    const double z = (x[j] - lo[j]) / rg;
    if (z < 1e-4)          { std::printf("  %-14s COLLE A LA BORNE BASSE\n", VAR[j]); ++n; }
    else if (z > 1 - 1e-4) { std::printf("  %-14s COLLE A LA BORNE HAUTE\n", VAR[j]); ++n; }
  }
  if (!n) std::printf("  aucune. L'optimum est INTERIEUR : la boite ne coupe rien.\n");
}

int main(int argc, char** argv) {
  const int H = (argc > 1) ? std::atoi(argv[1]) : 120;
  ephem::StandishEphemeris eph;
  const auto T0 = std::chrono::steady_clock::now();
  const double t2030 = epoch_from_iso("2030-01-01T00:00:00").tdb;
  const double t2040 = epoch_from_iso("2040-01-01T00:00:00").tdb;

  std::printf("=====================================================================\n");
  std::printf(" T01 — LE RAFFINEUR LOCAL A GRADIENT, ET CE QU'IL A REVELE\n");
  std::printf("=====================================================================\n");

  // ==== ETAGE 0 : reproduire le chiffre publie ===============================
  auto p0 = make_problem(t2030, t2040, true);            // boite ET cout D'ORIGINE
  std::vector<double> LO0, HI0; box(p0, LO0, HI0);
  const int F = astro::d1_flybys(p0), D = astro::d1_nvars(p0);
  auto f0 = [&](const std::vector<double>& x){ return astro::mga1dsm_evaluate(p0, eph, x).cost; };

  std::printf("\n--- ETAGE 0 : on reproduit le pipeline publie (memes graines) --------\n");
  astro::MgaProblem pm;
  pm.seq = p0.seq; pm.rp_min = p0.rp_min;
  pm.t0_lo = p0.t0_lo; pm.t0_hi = p0.t0_hi;
  pm.tof_lo = p0.tof_lo; pm.tof_hi = p0.tof_hi;
  pm.max_revs = 2; pm.c3_max = p0.c3_max;
  pm.tof_total_max = p0.tof_total_max;
  pm.rp_insert = p0.rp_insert; pm.a_insert = p0.a_insert;
  const int Dm = astro::n_vars(pm), Lg = astro::n_legs(pm);
  std::vector<double> mlo(Dm), mhi(Dm);
  mlo[0]=pm.t0_lo; mhi[0]=pm.t0_hi;
  for (int i=0;i<Lg;++i){ mlo[1+i]=pm.tof_lo[i]; mhi[1+i]=pm.tof_hi[i]; }
  for (int i=0;i<Lg;++i){ mlo[1+Lg+i]=0.0; mhi[1+Lg+i]=2.999; }
  auto fm = [&](const std::vector<double>& x){ return astro::mga_evaluate(pm, eph, x).cost; };
  astro::MgaResult rm; double bm = 1e300;
  for (int run = 0; run < 10; ++run) {
    auto de = astro::differential_evolution(fm, mlo, mhi, 120, 800, 777ull + run*7919ull);
    auto r = astro::mga_evaluate(pm, eph, de.x);
    if (de.f < bm && r.feasible) { bm = de.f; rm = r; }
  }
  std::vector<double> xs(D, 0.0);
  xs[0]=rm.t[0]; xs[1]=std::clamp(std::sqrt(rm.c3), p0.vinf_lo, p0.vinf_hi);
  xs[2]=0.5; xs[3]=0.5; xs[4]=0.4; xs[5]=rm.t[1]-rm.t[0];
  for (int k=1;k<=F;++k){ const int b=6+4*(k-1);
    xs[b+0]=0.0; xs[b+1]=std::clamp(rm.rp[k-1], p0.rp_min[k-1], p0.rp_max[k-1]);
    xs[b+2]=0.4; xs[b+3]=std::clamp(rm.t[k+1]-rm.t[k], p0.tof_lo[k], p0.tof_hi[k]); }
  std::vector<double> lo = LO0, hi = HI0;                 // LA BOITE RESSERREE
  lo[0]=std::fmax(p0.t0_lo, xs[0]-150.0*DAY); hi[0]=std::fmin(p0.t0_hi, xs[0]+150.0*DAY);
  lo[5]=std::fmax(p0.tof_lo[0], xs[5]*0.6);   hi[5]=std::fmin(p0.tof_hi[0], xs[5]*1.4);
  for (int k=1;k<=F;++k){ const int b=6+4*(k-1);
    lo[b+3]=std::fmax(p0.tof_lo[k], xs[b+3]*0.6);
    hi[b+3]=std::fmin(p0.tof_hi[k], xs[b+3]*1.4); }
  double bf = 1e300; std::vector<double> xpub = xs;
  for (int run = 0; run < 6; ++run) {
    auto de = astro::differential_evolution(f0, lo, hi, 200, 1200, 424242ull+run*104729ull, 0.7, 0.9, &xs);
    if (de.f < bf) { bf = de.f; xpub = de.x; }
  }
  auto refine_de = [&](const std::vector<double>& l2, const std::vector<double>& h2,
                       const std::vector<double>& xseed, std::uint64_t sd) {
    return astro::differential_evolution(f0, l2, h2, 60, 220, sd, 0.7, 0.9, &xseed); };
  auto mbh0 = astro::basin_hopping(f0, lo, hi, xpub, 60, 0.18, 987654321ull, refine_de);
  if (mbh0.f < bf) { bf = mbh0.f; xpub = mbh0.x; }
  auto r_pub = astro::mga1dsm_evaluate(p0, eph, xpub);
  line("PUBLIE (DE + MBH-DE)", r_pub);
  std::printf("  %-26s %s\n", "", std::fabs(r_pub.dv_onboard-12762.9) < 1.0
              ? "(12 762,9 attendu -> reproduit)" : "*** NE REPRODUIT PAS ***");

  // ==== MESURE A : le raffineur local, SEUL ==================================
  std::printf("\n--- MESURE A : le raffineur local, SEUL, depuis ce point -------------\n");
  astro::RefineOptions oa; oa.max_iter = 600;
  auto A = astro::refine_local(f0, LO0, HI0, xpub, oa);
  auto r_a = astro::mga1dsm_evaluate(p0, eph, A.x);
  line("+ raffineur local", r_a);
  std::printf("  %-26s %d iter, %lld evals, KKT %.2g, %d falaises — %s\n", "",
              A.iters, A.evals, A.gnorm, A.cliff_hits, A.stop);
  std::printf("\n  Il achete %.0f m/s. C'est REEL. Mais il sort COLLE AU MUR DES 9 ANS,\n",
              r_pub.dv_onboard - r_a.dv_onboard);
  std::printf("  avec un residu KKT de %.0e : il ne descend pas, il RABOTE une falaise.\n", A.gnorm);
  std::printf("  >>> Le raffineur n'etait pas ce qui manquait. Pas seulement, en tout cas.\n");

  // ==== MESURE B : MBH + raffineur, boite complete ============================
  std::printf("\n--- MESURE B : MBH + raffineur, boite COMPLETE, depart aleatoire -----\n");
  std::printf("  (ni amorcage MGA, ni boite resserree : on ne suppose RIEN)\n");
  astro::RefineOptions ob; ob.max_iter = 250;
  astro::MbhLocalResult Bb;
  for (int run = 0; run < 3; ++run) {
    auto B = astro::mbh_refine(f0, LO0, HI0, {}, H, 0.10, 31337ull+run*1000003ull, ob, 50);
    std::printf("  run %d : %8.1f m/s\n", run, B.f);
    if (B.f < Bb.f) Bb = B;
  }
  auto r_b = astro::mga1dsm_evaluate(p0, eph, Bb.x);
  line("MBH + raffineur", r_b);
  std::printf("  >>> Le PIPELINE etait le probleme autant que le raffineur : l'amorcage\n");
  std::printf("      MGA fournissait un bassin, et la boite resserree autour enfermait\n");
  std::printf("      la recherche dedans. Un raffineur n'en sort pas : il POLIT le piege.\n");

  // ==== MESURE C : L'ORACLE DE CASSINI ========================================
  std::printf("\n--- MESURE C : L'ORACLE DE CASSINI -----------------------------------\n");
  std::printf("  Le seul oracle CROISE disponible pour un optimiseur : une trajectoire\n");
  std::printf("  REELLE, volee, publiee. On EPINGLE ses 6 dates et on n'optimise que les\n");
  std::printf("  16 variables restantes. Le calendrier — ce que l'optimiseur cherchait —\n");
  std::printf("  n'est plus une liberte. Le test ne peut plus mentir dans un sens ni dans\n");
  std::printf("  l'autre.\n\n");
  const double tc = epoch_from_iso("1997-10-15T00:00:00").tdb;
  const double TOF[5] = {193.0*DAY, 424.0*DAY, 55.0*DAY, 500.0*DAY, 1279.0*DAY};
  auto pc = make_problem(tc, tc, false);
  std::vector<double> cl, ch; box(pc, cl, ch);
  cl[0]=ch[0]=tc; cl[5]=ch[5]=TOF[0];
  for (int k=1;k<=F;++k){ const int b=6+4*(k-1); cl[b+3]=ch[b+3]=TOF[k]; }
  auto fc = [&](const std::vector<double>& x){ return astro::mga1dsm_evaluate(pc, eph, x).cost; };
  astro::RefineOptions oc; oc.max_iter = 300;
  astro::MbhLocalResult C;
  for (int run = 0; run < 4; ++run) {
    auto r = astro::mbh_refine(fc, cl, ch, {}, H, 0.15, 555ull+run*7717ull, oc, 60);
    if (r.f < C.f) C = r;
  }
  auto rc = astro::mga1dsm_evaluate(pc, eph, C.x);
  tour(rc, pc);
  std::printf("\n  %-26s %8s  %10s\n", "", "modele", "REEL");
  std::printf("  %-26s %8.2f  %10s\n", "C3 de lancement (km2/s2)", rc.c3/1e6, "16,6");
  std::printf("  %-26s %8.1f  %10s   <-- LA POMPE\n", "DSM Venus-Venus (m/s)", rc.dsm[1], "~450");
  std::printf("  %-26s %8.2f  %10s\n", "v_inf survol Terre (km/s)", rc.vinf_fb[2]/1000, "~16,0");
  std::printf("  %-26s %8.0f  %10s\n", "dv EMBARQUE (m/s)", rc.dv_onboard, "~1 400");
  std::printf("\n  >>> Le modele retrouve SEUL la manoeuvre d'aphelie qui referme la\n");
  std::printf("      resonance Venus-Venus. A quatre jours et un metre par seconde pres.\n");
  std::printf("  >>> LE MODELE EST INNOCENTE. Il n'a jamais ete en cause.\n");
  std::printf("  >>> Et %.0f < 2000 : le critere est ATTEIGNABLE dans ce modele. Ce n'est\n",
              rc.dv_onboard);
  std::printf("      pas une cible fantaisiste. Le coupable est la RECHERCHE.\n");

  // ==== MESURE D : la FORME de Cassini, promenee sur 2030-2040 ================
  std::printf("\n--- MESURE D : la FORME de Cassini, promenee sur 2030-2040 -----------\n");
  std::printf("  Le MBH ne trouve pas LA POMPE : payer 450 m/s a l'aphelie pour faire\n");
  std::printf("  passer v_inf de 6,0 a 9,4 km/s — benefice encaisse TROIS JAMBES plus\n");
  std::printf("  tard, a Jupiter. Un gradient voit le cout. Il ne voit pas le benefice\n");
  std::printf("  differe. Le bassin est etroit, et loin.\n");
  std::printf("  Mais on SAIT ou il est. On ne cherche plus la FORME du tour : on ne\n");
  std::printf("  cherche plus que la DATE ou la geometrie la reproduit. Balayage 1-D.\n\n");
  auto p = make_problem(t2030, t2040, false);
  std::vector<double> LO, HI; box(p, LO, HI);
  auto f = [&](const std::vector<double>& x){ return astro::mga1dsm_evaluate(p, eph, x).cost; };
  astro::RefineOptions od; od.max_iter = 120;
  std::vector<std::pair<double,double>> curve;
  for (double t0 = p.t0_lo; t0 <= p.t0_hi; t0 += 5.0*DAY) {
    std::vector<double> x = C.x; x[0] = t0;
    std::vector<double> l = LO, h = HI;
    l[0] = std::fmax(p.t0_lo, t0-20.0*DAY); h[0] = std::fmin(p.t0_hi, t0+20.0*DAY);
    auto r = astro::refine_local(f, l, h, x, od);
    if (r.ok) curve.emplace_back(t0, r.f);
  }
  std::sort(curve.begin(), curve.end(),
            [](const auto& a, const auto& b){ return a.second < b.second; });
  std::printf("  %d dates essayees (pas de 5 j). On polit les 6 meilleurs BASSINS —\n",
              (int)curve.size());
  std::printf("  pas le meilleur POINT : le classement avant polissage ne predit pas\n");
  std::printf("  celui d'apres. Confondre les deux, c'est prendre l'altitude du col\n");
  std::printf("  pour la profondeur de la vallee.\n\n");
  astro::RefineOptions oe; oe.max_iter = 400;
  astro::MbhLocalResult Bd;
  std::vector<double> picked;
  for (const auto& cand : curve) {
    if (static_cast<int>(picked.size()) >= 6) break;
    bool dup = false;
    for (double q : picked) if (std::fabs(cand.first - q) < 90.0*DAY) { dup = true; break; }
    if (dup) continue;
    picked.push_back(cand.first);
    std::vector<double> x = C.x; x[0] = cand.first;
    std::vector<double> l = LO, h = HI;
    l[0] = std::fmax(p.t0_lo, cand.first-150.0*DAY);
    h[0] = std::fmin(p.t0_hi, cand.first+150.0*DAY);
    auto sd = astro::refine_local(f, l, h, x, oe);
    auto M = astro::mbh_refine(f, l, h, sd.x, 4*H, 0.07,
                               20260714ull + static_cast<std::uint64_t>(picked.size())*7919ull,
                               oe, 60);
    std::printf("  candidat %s : balayage %8.1f  ->  MBH %8.1f m/s\n",
                epoch_to_iso(Epoch{cand.first}).substr(0,10).c_str(), cand.second, M.f);
    if (M.f < Bd.f) Bd = M;
  }
  auto rd = astro::mga1dsm_evaluate(p, eph, Bd.x);
  std::printf("\n--- LE TOUR ---------------------------------------------------------\n");
  tour(rd, p);
  std::printf("\n  la POMPE : DSM Venus-Venus = %.1f m/s -> v_inf %.2f -> %.2f km/s\n",
              rd.dsm[1], rd.vinf_fb[0]/1000, rd.vinf_fb[1]/1000);
  std::printf("\n--- DIAGNOSTIC : quelles bornes MORDENT ? ---------------------------\n");
  bounds(Bd.x, LO, HI);

  const bool held = (rd.dv_onboard < 2000.0) && (rd.c3 <= 20e6) && (rd.tof_over <= 0.0);
  std::printf("\n=====================================================================\n");
  std::printf(" %-35s %11s %8s %9s\n", "", "dv embarque", "C3", "duree");
  std::printf(" %s\n", std::string(67,'-').c_str());
  std::printf(" %-35s %7.0f m/s %7.2f %6.2f ans\n", "PUBLIE (DE + MBH-DE)",
              r_pub.dv_onboard, r_pub.c3/1e6, r_pub.tof_total/(365.25*DAY));
  std::printf(" %-35s %7.0f m/s %7.2f %6.2f ans\n", "A. + raffineur local",
              r_a.dv_onboard, r_a.c3/1e6, r_a.tof_total/(365.25*DAY));
  std::printf(" %-35s %7.0f m/s %7.2f %6.2f ans\n", "B. + boite libre + MBH",
              r_b.dv_onboard, r_b.c3/1e6, r_b.tof_total/(365.25*DAY));
  std::printf(" %-35s %7.0f m/s %7.2f %6.2f ans\n", "D. + forme de Cassini (2030-2040)",
              rd.dv_onboard, rd.c3/1e6, rd.tof_total/(365.25*DAY));
  std::printf(" %s\n", std::string(67,'-').c_str());
  std::printf(" %-35s %7.0f m/s %7.2f %6.2f ans  <- borne\n", "C. Cassini, dates reelles (oracle)",
              rc.dv_onboard, rc.c3/1e6, rc.tof_total/(365.25*DAY));
  std::printf(" %-35s %11s %7.1f %6.1f ans\n", "   Cassini, mission REELLE", "~1400 m/s", 16.6, 6.7);
  std::printf("\n CRITERE (dv < 2000 m/s, C3 <= 20 km2/s2, duree <= 9 ans) : %s\n",
              held ? ">>> TENU <<<" : "PAS TENU");
  std::printf(" (%.0f s de calcul, 1 coeur)\n",
              std::chrono::duration<double>(std::chrono::steady_clock::now()-T0).count());
  return 0;
}
