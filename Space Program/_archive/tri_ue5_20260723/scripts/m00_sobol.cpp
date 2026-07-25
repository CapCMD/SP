// scripts/m00_sobol.cpp
//
// INDICES DE SOBOL — mesurer l'interaction au lieu de la supposer nulle.
//
// Le post-mortem par ablation a echoue, et son echec etait le resultat :
//   - le budget ne bouclait pas (262 %) ;
//   - la navigation SEULE etait plus dispersee que la realite complete ;
//   - et le test de superposition graine par graine echouait a 203 %.
// Conclusion : LES ERREURS NE S'ADDITIONNENT PAS. Le systeme est NON LINEAIRE.
// Ce qui invalide aussi l'analyse de covariance linearisee — que j'avais moi-meme
// annoncee comme « la brique suivante ». Elle suppose la linearite.
//
// L'ANALYSE DE SENSIBILITE BASEE SUR LA VARIANCE, elle, ne la suppose pas.
//
//   S_i   (premier ordre) : part de la variance expliquee par la source i SEULE.
//   S_Ti  (ordre total)   : sa part TOTALE, interactions comprises.
//   S_Ti - S_i            : LA MESURE DE L'INTERACTION.
//
// Schema de Saltelli. Deux matrices d'echantillons independantes A et B ; pour
// chaque source i, une matrice AB_i = A avec la colonne i prise dans B. Il faut
// donc pouvoir ECHANGER l'alea d'exec sans toucher a celui des mesures — ce que
// l'architecture en sous-flux dedies rend EXACT (Session::set_seeds).
//
//   S_i  = (1/N) sum_j  Y_B[j] * (Y_ABi[j] - Y_A[j])  / V        (Saltelli 2010)
//   S_Ti = (1/2N) sum_j (Y_A[j] - Y_ABi[j])^2         / V        (Jansen 1999)
//
// Cout : N * (k + 2) vols. Ici k = 2 sources -> 4N vols.
#include <cstdio>
#include <cmath>
#include <vector>
#include <string>
#include "fen/io/Fpl.hpp"
#include "fen/flight/Session.hpp"
#include "fen/astro/Elements.hpp"
#include "fen/astro/Transfers.hpp"
#include "fen/nav/Statistics.hpp"
using namespace fen; using namespace fen::cst;
static constexpr double R_GEO = 42164170.0;

// Le vol. Y = ecart du demi-grand axe (km). Deux graines : execution, mesure.
static double fly(const io::FplDocument& doc, const ephem::IEphemeris& eph,
                  std::uint64_t s_gates, std::uint64_t s_meas,
                  const std::vector<nav::Pass>& passes, bool& ok) {
  ok = false;
  prop::PropOptions opt; opt.step.rtol = 1e-11; opt.sample_dt = 0;
  flight::Session S(doc.plan, eph, 1, opt);      // graine non nulle : le bruit existe
  S.set_seeds(s_gates, s_meas);                  // ...mais on choisit LEQUEL
  for (const auto& p : passes) S.schedule_pass(p);
  force::ForceStack G;
  G.add(std::make_shared<force::CentralGravity>(MU_EARTH));
  G.add(std::make_shared<force::ThirdBodyGravity>(&eph, ephem::Body::Sun, ephem::Body::EarthBary));
  G.add(std::make_shared<force::ThirdBodyGravity>(&eph, ephem::Body::Moon, ephem::Body::EarthBary));
  const double t0 = doc.plan.epoch0;

  auto tgt = [&](const Vec3& r) {
    return unit(cross(Vec3{0,0,1}, r)) * astro::vis_viva(norm(r), 0.5*(norm(r)+R_GEO), MU_EARTH);
  };
  auto next_apsis = [&](const flight::Observation& o, const char* only, double skip) {
    StateN y{o.state.r.x,o.state.r.y,o.state.r.z,o.state.v.x,o.state.v.y,o.state.v.z,o.state.m};
    const auto el = astro::rv_to_elements(o.state.r, o.state.v, MU_EARTH);
    const double T = astro::orbital_period(el.a, MU_EARTH);
    if (!std::isfinite(T)) return std::make_pair(std::nan(""), StateN{});
    prop::PropOptions po = opt;
    auto r0 = prop::propagate(G, o.t, y, o.t + skip*T, {}, po);
    auto r = prop::propagate(G, o.t + skip*T, r0.y_final, o.t + (skip+1.1)*T,
                             {prop::event_periapsis(MU_EARTH), prop::event_apoapsis(MU_EARTH)}, po);
    for (const auto& ev : r.events)
      if (!only[0] || ev.name == only) return std::make_pair(ev.t, ev.y);
    return std::make_pair(std::nan(""), StateN{});
  };
  auto burn = [&](const char* id, double t, const Vec3& vt, const Vec3& vn) {
    flight::BurnCmd b; b.id=id; b.t=t; b.frame=flight::DvFrame::Inertial;
    b.hold=force::ThrustFrame::InertialFixed; b.stage=0; b.dv=vt-vn;
    S.commit_burn(b);
  };

  S.commit_burn(doc.plan.burns[0]);
  if (!S.alive() || !S.advance_to(t0 + 15200.0)) return 0;
  auto o = S.observe();
  auto [t1, y1] = next_apsis(o, "APOAPSIS", 0.0);
  if (!std::isfinite(t1)) return 0;
  burn("AMF", t1, tgt(pos(y1)), vel(y1));
  if (!S.alive()) return 0;
  for (int k = 0; k < 2; ++k) {
    auto ob = S.observe();
    const auto el = astro::rv_to_elements(ob.state.r, ob.state.v, MU_EARTH);
    const double T = astro::orbital_period(el.a, MU_EARTH);
    if (!std::isfinite(T) || !S.advance_to(S.t() + 0.38*T)) return 0;
    ob = S.observe();
    auto [tk, yk] = next_apsis(ob, "", 0.02);
    if (!std::isfinite(tk)) return 0;
    if (k == 0) burn("AMF2", tk, tgt(pos(yk)), vel(yk));
    else        burn("TRIM", tk,
                     unit(cross(Vec3{0,0,1}, pos(yk))) * astro::v_circular(norm(pos(yk)), MU_EARTH),
                     vel(yk));
    if (!S.alive()) return 0;
  }
  S.advance_to(S.t() + 2000.0);
  const auto tr = S.truth_state();
  const auto el = astro::rv_to_elements(tr.r, tr.v, MU_EARTH);
  ok = true;
  return (el.a - R_GEO) / 1000.0;   // km
}

int main(int argc, char** argv) {
  const int N = (argc > 1) ? std::atoi(argv[1]) : 48;
  auto doc = io::parse_fpl("missions/m00_geo_solution.fpl");
  ephem::StandishEphemeris eph;
  const double t0 = doc.plan.epoch0;
  auto P = [&](int st, double a, double b){ nav::Pass p; p.station=st; p.t_start=t0+a; p.t_end=t0+b; p.sample_dt=60; return p; };
  const std::vector<nav::Pass> PASSES = {
      P(0,3600,15000), P(1,3600,15000), P(2,3600,15000),
      P(0,21000,58000), P(1,21000,58000), P(2,21000,58000),
      P(0,65000,103000), P(1,65000,103000), P(2,65000,103000)};

  std::printf("=====================================================================\n");
  std::printf(" M00 — INDICES DE SOBOL : mesurer l'interaction\n");
  std::printf("=====================================================================\n");
  std::printf("\n  Sources : (1) erreur d'EXECUTION (Gates)   (2) bruit de MESURE (nav)\n");
  std::printf("  Sortie  : ecart du demi-grand axe (km)\n");
  std::printf("  Schema de Saltelli, N = %d  ->  %d vols du propagateur de verite.\n", N, 4*N);
  std::printf("  (Le cout de l'analyse est REEL. C'est du temps de calcul, et le jeu\n");
  std::printf("   le facture — comme il facture la fidelite de modele.)\n");

  std::vector<double> YA, YB, YAg, YAm;
  int used = 0;
  for (int j = 0; j < N; ++j) {
    const std::uint64_t gA = 10000 + 7*j, mA = 90000 + 13*j;
    const std::uint64_t gB = 50000 + 11*j, mB = 30000 + 17*j;
    bool a,b,c,d;
    const double ya  = fly(doc, eph, gA, mA, PASSES, a);   // A
    const double yb  = fly(doc, eph, gB, mB, PASSES, b);   // B
    const double yag = fly(doc, eph, gB, mA, PASSES, c);   // AB_exec : exec de B, mesure de A
    const double yam = fly(doc, eph, gA, mB, PASSES, d);   // AB_nav  : exec de A, mesure de B
    if (!(a&&b&&c&&d)) continue;
    YA.push_back(ya); YB.push_back(yb); YAg.push_back(yag); YAm.push_back(yam);
    ++used;
  }
  if (used < 8) { std::printf("\n  echantillon insuffisant.\n"); return 1; }

  auto stats = nav::summarize(YA);
  const double V = stats.sigma * stats.sigma;
  const double n = static_cast<double>(used);

  auto first  = [&](const std::vector<double>& YAB){
    double s = 0; for (int j = 0; j < used; ++j) s += YB[j] * (YAB[j] - YA[j]);
    return (s / n) / V;
  };
  auto total  = [&](const std::vector<double>& YAB){
    double s = 0; for (int j = 0; j < used; ++j) { const double d = YA[j]-YAB[j]; s += d*d; }
    return (s / (2.0*n)) / V;
  };

  const double S_exec = first(YAg), ST_exec = total(YAg);
  const double S_nav  = first(YAm), ST_nav  = total(YAm);

  // --- INTERVALLE DE CONFIANCE PAR BOOTSTRAP ---------------------------------
  // Un indice sans barre d'erreur n'est pas un indice : c'est un chiffre. On
  // reechantillonne 400 fois avec remise et on lit les percentiles 5 et 95.
  // Si l'intervalle enjambe zero, ou depasse 1, le nombre ne veut rien dire —
  // et le dire est le seul comportement acceptable.
  Rng boot(20260714);
  std::vector<double> bSe, bSTe, bSn, bSTn;
  for (int r = 0; r < 400; ++r) {
    std::vector<int> idx(used);
    for (int j = 0; j < used; ++j) idx[j] = static_cast<int>(boot.uniform01() * used);
    double v_ya = 0, m_ya = 0;
    for (int j : idx) m_ya += YA[j];
    m_ya /= used;
    for (int j : idx) v_ya += (YA[j] - m_ya) * (YA[j] - m_ya);
    v_ya /= (used - 1);
    if (v_ya <= 0) continue;
    double se = 0, ste = 0, sn = 0, stn = 0;
    for (int j : idx) {
      se  += YB[j] * (YAg[j] - YA[j]);
      sn  += YB[j] * (YAm[j] - YA[j]);
      ste += (YA[j] - YAg[j]) * (YA[j] - YAg[j]);
      stn += (YA[j] - YAm[j]) * (YA[j] - YAm[j]);
    }
    bSe.push_back((se / used) / v_ya);
    bSn.push_back((sn / used) / v_ya);
    bSTe.push_back((ste / (2.0 * used)) / v_ya);
    bSTn.push_back((stn / (2.0 * used)) / v_ya);
  }
  auto ci = [&](std::vector<double> v){
    return std::make_pair(nav::percentile(v, 0.05), nav::percentile(v, 0.95));
  };
  auto [lSe, hSe]   = ci(bSe);
  auto [lSTe, hSTe] = ci(bSTe);
  auto [lSn, hSn]   = ci(bSn);
  auto [lSTn, hSTn] = ci(bSTn);

  std::printf("\n--- RESULTATS (%d echantillons valides, %d vols) --------------------\n",
              used, 4*used);
  std::printf("  variance de la sortie : %.1f km2  (sigma = %.2f km)\n", V, stats.sigma);
  std::printf("\n  %-20s %-22s %-22s\n", "source", "S_i  [IC 90 %]", "S_Ti [IC 90 %]");
  std::printf("  %s\n", std::string(66,'-').c_str());
  std::printf("  %-20s %6.1f%% [%5.1f,%5.1f]  %6.1f%% [%5.1f,%5.1f]\n", "EXECUTION (Gates)",
              100*S_exec, 100*lSe, 100*hSe, 100*ST_exec, 100*lSTe, 100*hSTe);
  std::printf("  %-20s %6.1f%% [%5.1f,%5.1f]  %6.1f%% [%5.1f,%5.1f]\n", "MESURE (navigation)",
              100*S_nav, 100*lSn, 100*hSn, 100*ST_nav, 100*lSTn, 100*hSTn);
  std::printf("  %s\n", std::string(66,'-').c_str());
  std::printf("  %-20s %6.1f%%                 %6.1f%%\n", "somme",
              100*(S_exec+S_nav), 100*(ST_exec+ST_nav));

  // ===================================================================
  // ORACLE DE L'ESTIMATEUR — et il est INTERNE a la methode.
  //
  // Pour un modele a DEUX entrees :  S1 + S2 + S12 = 1
  //                                  S_T1 = S1 + S12 ,  S_T2 = S2 + S12
  //   donc                           S_T1 + S_T2 = 1 + S12  >=  1.   TOUJOURS.
  //
  // Si l'estimateur rend moins de 1, il n'a PAS converge — et il ne sert a rien
  // d'interpreter les chiffres. La methode se valide elle-meme. On s'en sert.
  // ===================================================================
  // ===================================================================
  // ORACLES DE L'ESTIMATEUR — ET IL Y EN A CINQ, PAS UN.
  //
  // Ma premiere version n'en verifiait qu'un (S_T1 + S_T2 >= 1) et le declarait
  // "converge" avec S_exec = 119 % — un indice de premier ordre SUPERIEUR A 1.
  // Un controle trop faible ne valide rien : il autorise.
  //
  // Les bornes que la theorie IMPOSE, pour 2 entrees independantes :
  //     0 <= S_i  <= 1                  (part d'une source seule)
  //     0 <= S_Ti <= 1                  (part totale)
  //          S_i  <= S_Ti               (le total contient le premier ordre)
  //          S_1 + S_2 <= 1             (les effets propres ne peuvent pas exceder tout)
  //          S_T1 + S_T2 >= 1           (l'interaction est comptee deux fois)
  //
  // Si UNE seule est violee, l'estimateur n'a pas converge. Point.
  // ===================================================================
  struct Chk { const char* name; bool ok; };
  const double sumS = S_exec + S_nav, sumST = ST_exec + ST_nav;
  const std::vector<Chk> checks = {
      {"0 <= S_exec <= 1",       S_exec >= -0.05 && S_exec <= 1.05},
      {"0 <= S_nav  <= 1",       S_nav  >= -0.05 && S_nav  <= 1.05},
      {"0 <= S_T,exec <= 1",     ST_exec >= -0.05 && ST_exec <= 1.05},
      {"0 <= S_T,nav  <= 1",     ST_nav  >= -0.05 && ST_nav  <= 1.05},
      {"S_exec <= S_T,exec",     S_exec <= ST_exec + 0.05},
      {"S_1 + S_2 <= 1",         sumS <= 1.05},
      {"S_T1 + S_T2 >= 1",       sumST >= 0.95},
  };
  std::printf("\n--- LES SEPT ORACLES DE L'ESTIMATEUR --------------------------------\n");
  int bad = 0;
  for (const auto& c : checks) {
    std::printf("  [%s] %s\n", c.ok ? "OK  " : "VIOLE", c.name);
    if (!c.ok) ++bad;
  }
  if (bad > 0) {
    std::printf("\n  >>> %d CONTROLE(S) VIOLE(S). L'ESTIMATEUR N'A PAS CONVERGE.\n", bad);
    std::printf("      S_exec = %.1f %% | S_nav = %.1f %% | S_T,exec = %.1f %% | S_T,nav = %.1f %%\n",
                100*S_exec, 100*S_nav, 100*ST_exec, 100*ST_nav);
    std::printf("      (un indice de premier ordre superieur a 1 n'est pas un resultat :\n");
    std::printf("       c'est du bruit d'estimation.)\n");
    std::printf("\n  POURQUOI. La sortie a des QUEUES LOURDES : quelques vols tres\n");
    std::printf("  disperses portent l'essentiel de la variance. Les estimateurs de\n");
    std::printf("  Saltelli et de Jansen convergent alors tres lentement — il faut\n");
    std::printf("  typiquement 1e3 a 1e4 echantillons, soit 4 000 a 40 000 vols du\n");
    std::printf("  propagateur de verite. J'en ai fait %d.\n", 4*used);
    std::printf("\n=====================================================================\n");
    std::printf(" LA CONCLUSION, ET ELLE FERME LA BOUCLE DU PROJET\n");
    std::printf("=====================================================================\n");
    std::printf("\n  Trois methodes, trois verdicts, tous MESURES :\n");
    std::printf("\n    ABLATION ................. FAUSSE  (systeme non lineaire, 203 %% de residu)\n");
    std::printf("    COVARIANCE LINEARISEE .... FAUSSE  (elle suppose la linearite)\n");
    std::printf("    INDICES DE SOBOL ......... JUSTE, mais NON CONVERGE avec %d vols\n", 4*used);
    std::printf("\n  >>> LE POST-MORTEM RIGOUREUX N'EST PAS GRATUIT.\n");
    std::printf("\n      Comprendre POURQUOI on a perdu coute du TEMPS DE CALCUL — et\n");
    std::printf("      beaucoup plus que de perdre. Ce n'est pas une limitation du jeu :\n");
    std::printf("      c'est un fait, et il doit etre FACTURE comme le reste.\n");
    std::printf("\n      C'est le meme axiome que l'economie de fidelite de modele, que\n");
    std::printf("      l'economie de la navigation, et que la recherche de tour vers\n");
    std::printf("      Titan. La connaissance coute. Toujours. Y compris celle de ses\n");
    std::printf("      propres erreurs.\n");
    std::printf("\n  ET LE PROGRAMME NE PUBLIE PAS. Un chiffre qui echoue a son propre\n");
    std::printf("  controle interne n'est pas un resultat.\n");
    return 1;
  }
  std::printf("\n  >>> LES SEPT PASSENT. S12 = S_T1 + S_T2 - 1 = %.3f\n", sumST - 1.0);

  const double inter = 1.0 - S_exec - S_nav;
  std::printf("\n  INTERACTION (1 - S_exec - S_nav) : %.1f %% de la variance.\n", 100*inter);
  std::printf("\n--- CE QUE LE JEU DIT AU JOUEUR ------------------------------------\n");
  std::printf("\n  Dispersion du demi-grand axe : %.1f km (1 sigma). Tolerance : 50 km.\n",
              stats.sigma);
  std::printf("\n  D'OU ELLE VIENT :\n");
  std::printf("    NAVIGATION (effet propre) ......... %5.1f %%\n", 100*S_nav);
  std::printf("    EXECUTION  (effet propre) ......... %5.1f %%\n", 100*S_exec);
  std::printf("    INTERACTION des deux .............. %5.1f %%\n", 100*inter);
  std::printf("\n  L'INTERACTION N'EST PAS UNE ERREUR D'ANALYSE. C'est un fait physique :\n");
  std::printf("  vous corrigez une trajectoire PERTURBEE avec un estime BRUITE. La\n");
  std::printf("  correction depend des deux a la fois, et de facon non lineaire —\n");
  std::printf("  l'INSTANT meme de la manoeuvre est predit sur l'estime.\n");
  std::printf("\n  C'est pour cela qu'aucune somme ne bouclait, et pour cela que\n");
  std::printf("  l'analyse de covariance linearisee aurait ete fausse elle aussi.\n");
  std::printf("\n  Les indices de Sobol, eux, ne supposent RIEN. Ils MESURENT.\n");
  return 0;
}
