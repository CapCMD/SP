// scripts/t01_tour.cpp
//
// TITAN — LE PROBLÈME DU TRANSPORT.
//
// Avant de discuter exobiologie, il faut y aller. Ce programme mesure ce que
// coûte Saturne, et pourquoi personne n'y va en ligne droite.
//
//   1. DIRECT Terre -> Saturne. On calcule le C3 exigé du lanceur.
//   2. ASSISTANCE JOVIENNE (E-J-S). Deux jambes de Lambert, une contrainte de
//      survol au milieu : |v_inf| conservé, déviation bornée par le périastre.
//   3. LE PRIX RÉEL de l'assistance : ce n'est pas du Delta-v. C'est le
//      CALENDRIER — période synodique Jupiter-Saturne : 19,86 ANS.
#include <cstdio>
#include <cmath>
#include <string>
#include <vector>
#include "fen/astro/Flyby.hpp"
#include "fen/astro/Lambert.hpp"
#include "fen/astro/Transfers.hpp"
#include "fen/ephem/Ephemeris.hpp"
#include "fen/core/Epoch.hpp"

using namespace fen;
using namespace fen::cst;
using ephem::Body;

static constexpr double R_JUPITER = 71492e3;

int main(int argc, char** argv) {
  ephem::StandishEphemeris eph;
  const double t0 = epoch_from_iso(argc > 1 ? argv[1] : "2030-01-01T00:00:00").tdb;

  std::printf("=====================================================================\n");
  std::printf(" T01 — TITAN : LE PROBLEME DU TRANSPORT\n");
  std::printf("=====================================================================\n");

  // =====================================================================
  // 0. LES BORNES THEORIQUES, calculables a la main
  // =====================================================================
  std::printf("\n--- BORNES DE HOHMANN (circulaire coplanaire) ----------------------\n");
  auto hj = astro::hohmann(1.0 * AU, 5.2029 * AU, MU_SUN);
  auto hs = astro::hohmann(1.0 * AU, 9.5367 * AU, MU_SUN);
  std::printf("  Terre -> Jupiter : C3 = %6.1f km2/s2 | %.2f ans de transit\n",
              hj.dv1 * hj.dv1 / 1e6, hj.tof / (365.25 * DAY));
  std::printf("  Terre -> Saturne : C3 = %6.1f km2/s2 | %.2f ans de transit\n",
              hs.dv1 * hs.dv1 / 1e6, hs.tof / (365.25 * DAY));
  std::printf("  (pour memoire : Terre -> Mars, C3 = 8.7 km2/s2)\n");
  std::printf("\n  >>> UN LANCEUR NE VEND PAS DE C3 A 100 km2/s2 AVEC UNE CHARGE UTILE.\n");
  std::printf("      La ligne droite vers Saturne n'est pas une option chere.\n");
  std::printf("      C'est une option INEXISTANTE.\n");

  // =====================================================================
  // 1. LE SURVOL : combien Jupiter donne-t-il, gratuitement ?
  // =====================================================================
  std::printf("\n--- CE QUE JUPITER DONNE GRATUITEMENT ------------------------------\n");
  std::printf("  Delta-v heliocentrique d'un survol non propulse : 2*v_inf*sin(delta/2)\n");
  std::printf("  Maximum theorique a un periastre r_p : sqrt(mu/r_p)\n\n");
  std::printf("  %-14s %-12s %-12s %-14s %s\n",
              "periastre", "v_inf", "deviation", "dv GRATUIT", "contexte");
  std::printf("  %s\n", std::string(68, '-').c_str());
  for (double rpj : {1.5, 5.0, 15.0, 30.0, 137.0}) {
    const double rp = rpj * R_JUPITER;
    const double vinf = 5500.0;    // v_inf typique d'une arrivee jovienne
    const double d = astro::flyby_turn(vinf, rp, MU_JUPITER);
    const double fdv = astro::flyby_free_dv(vinf, rp, MU_JUPITER);
    const char* ctx = (rpj < 2) ? "irradiation letale"
                    : (rpj < 20) ? "ceintures de radiation"
                    : (rpj < 100) ? "acceptable" : "Cassini (1er dec. 2000)";
    std::printf("  %6.1f R_J     %5.1f km/s   %6.1f deg     %7.2f km/s    %s\n",
                rpj, vinf / 1000, d / DEG, fdv / 1000, ctx);
  }
  std::printf("\n  maximum absolu a 1.5 R_J (pour v_inf = %.1f km/s) : %.1f km/s\n",
              astro::flyby_optimal_vinf(1.5 * R_JUPITER, MU_JUPITER) / 1000,
              astro::flyby_max_free_dv(1.5 * R_JUPITER, MU_JUPITER) / 1000);
  std::printf("  >>> AUCUN MOTEUR NE CONCOURT. Un survol jovien vaut plus que\n");
  std::printf("      l'injection depuis la Terre.\n");

  // =====================================================================
  // 2. RECHERCHE DE TOUR E-J-S
  // =====================================================================
  std::printf("\n--- RECHERCHE DE TOUR TERRE -> JUPITER -> SATURNE -------------------\n");
  const double RP_MIN = 30.0 * R_JUPITER;   // marge radiative
  std::printf("  contrainte de survol : r_p >= %.0f R_J (dose de radiation)\n", RP_MIN / R_JUPITER);
  std::printf("  fenetre de lancement exploree : %s + 8 ans\n",
              epoch_to_iso(Epoch{t0}).substr(0, 10).c_str());

  struct Tour {
    double t_e{}, tof1{}, tof2{};
    double c3{}, vinf_j{}, vinf_s{}, rp{}, dv_fb{}, gain{};
    bool ok{false};
  };
  Tour best;
  double best_score = 1e30;
  int n_feasible = 0, n_tested = 0;

  for (int i = 0; i < 96; ++i) {                       // date de lancement
    const double te = t0 + i * 30.0 * DAY;
    const auto E = eph.state(Body::EarthBary, Body::Sun, Epoch{te});
    for (int j = 0; j < 40; ++j) {                     // TOF Terre -> Jupiter
      const double tof1 = (500.0 + j * 40.0) * DAY;
      const double tj = te + tof1;
      const auto J = eph.state(Body::Jupiter, Body::Sun, Epoch{tj});
      auto L1 = astro::lambert(E.r, J.r, tof1, MU_SUN, true, 0);
      if (!L1.ok) continue;
      const Vec3 vinf_dep = L1.solutions[0].v1 - E.v;
      const double c3 = norm2(vinf_dep);
      if (c3 > 100e6) continue;                        // > 100 km2/s2 : hors catalogue
      const Vec3 vin = L1.solutions[0].v2 - J.v;

      for (int k = 0; k < 40; ++k) {                   // TOF Jupiter -> Saturne
        const double tof2 = (700.0 + k * 60.0) * DAY;
        const double ts = tj + tof2;
        const auto S = eph.state(Body::Saturn, Body::Sun, Epoch{ts});
        auto L2 = astro::lambert(J.r, S.r, tof2, MU_SUN, true, 0);
        if (!L2.ok) continue;
        ++n_tested;
        const Vec3 vout = L2.solutions[0].v1 - J.v;
        const Vec3 vinf_arr = L2.solutions[0].v2 - S.v;

        auto fb = astro::solve_flyby(vin, vout, MU_JUPITER, RP_MIN);
        if (!fb.feasible) continue;
        ++n_feasible;

        // Coût total : C3 (le lanceur le vend) + Delta-v du survol propulse
        //              + insertion saturnienne (proxy : v_inf d'arrivee).
        const double score = std::sqrt(c3) + fb.dv + 0.5 * norm(vinf_arr);
        if (score < best_score) {
          best_score = score;
          best = Tour{te, tof1, tof2, c3, norm(vin), norm(vinf_arr),
                      fb.rp, fb.dv, fb.gravity_gain, true};
        }
      }
    }
  }

  std::printf("  %d combinaisons testees, %d survols FAISABLES (%.1f %%)\n",
              n_tested, n_feasible, 100.0 * n_feasible / std::max(1, n_tested));

  if (!best.ok) { std::printf("\n  *** aucune solution. Deplacez la fenetre.\n"); return 1; }

  std::printf("\n--- MEILLEUR TOUR TROUVE -------------------------------------------\n");
  std::printf("  lancement      : %s\n", epoch_to_iso(Epoch{best.t_e}).substr(0, 10).c_str());
  std::printf("  survol Jupiter : %s   (+%.2f ans)\n",
              epoch_to_iso(Epoch{best.t_e + best.tof1}).substr(0, 10).c_str(),
              best.tof1 / (365.25 * DAY));
  std::printf("  arrivee Saturne: %s   (+%.2f ans au total)\n",
              epoch_to_iso(Epoch{best.t_e + best.tof1 + best.tof2}).substr(0, 10).c_str(),
              (best.tof1 + best.tof2) / (365.25 * DAY));
  std::printf("\n  C3 de lancement          : %7.2f km2/s2   (direct : %.1f)\n",
              best.c3 / 1e6, hs.dv1 * hs.dv1 / 1e6);
  std::printf("  v_inf a Jupiter          : %7.2f km/s\n", best.vinf_j / 1000);
  std::printf("  periastre du survol      : %7.1f R_J\n", best.rp / R_JUPITER);
  std::printf("  Delta-v propulsif requis : %7.1f m/s     <- ce qu'on PAIE\n", best.dv_fb);
  std::printf("  Delta-v GRATUIT du survol: %7.2f km/s    <- ce qu'on ENCAISSE\n",
              best.gain / 1000);
  std::printf("  v_inf a Saturne          : %7.2f km/s\n", best.vinf_s / 1000);

  // --- ce que l'arrivee coute vraiment : l'INSERTION SATURNIENNE ---
  // Capture dans une ellipse de 120 jours de periode, periastre a 2.5 R_S.
  const double RP_SOI = 2.5 * R_SATURN;
  const double A_CAP  = std::cbrt(MU_SATURN * std::pow(120.0 * DAY, 2.0) / (4.0 * PI * PI));
  auto dv_soi = [&](double vinf) {
    return std::sqrt(vinf * vinf + 2.0 * MU_SATURN / RP_SOI)
         - std::sqrt(MU_SATURN * (2.0 / RP_SOI - 1.0 / A_CAP));
  };
  // v_inf d'arrivee d'un Hohmann direct : le transfert arrive a son apoastre.
  const double a_t = 0.5 * (1.0 + 9.5367) * AU;
  const double v_arr_direct = std::sqrt(MU_SUN * (2.0 / (9.5367 * AU) - 1.0 / a_t));
  const double v_saturn = std::sqrt(MU_SUN / (9.5367 * AU));
  const double vinf_direct = std::fabs(v_saturn - v_arr_direct);

  const double c3_gain = hs.dv1 * hs.dv1 - best.c3;
  std::printf("\n--- LES DEUX FACTURES ----------------------------------------------\n");
  std::printf("  %-28s %12s %12s\n", "", "DIRECT", "AVEC JUPITER");
  std::printf("  %s\n", std::string(56, '-').c_str());
  std::printf("  %-28s %9.1f    %9.2f  km2/s2\n", "C3 de lancement",
              hs.dv1 * hs.dv1 / 1e6, best.c3 / 1e6);
  std::printf("  %-28s %9.2f    %9.2f  km/s\n", "v_inf a l'arrivee",
              vinf_direct / 1000, best.vinf_s / 1000);
  std::printf("  %-28s %9.0f    %9.0f  m/s\n", "Delta-v d'insertion",
              dv_soi(vinf_direct), dv_soi(best.vinf_s));
  std::printf("  %-28s %9.2f    %9.2f  ans\n", "duree de croisiere",
              hs.tof / (365.25 * DAY), (best.tof1 + best.tof2) / (365.25 * DAY));

  std::printf("\n  >>> LE SURVOL NE PAIE PAS LE LANCEMENT. IL PAIE L'ARRIVEE.\n");
  std::printf("      C3 : %.1f -> %.1f km2/s2  (-%.0f %%). Insuffisant : atteindre\n",
              hs.dv1 * hs.dv1 / 1e6, best.c3 / 1e6, 100.0 * c3_gain / (hs.dv1 * hs.dv1));
  std::printf("      Jupiter coute deja C3 = %.0f. Un seul survol ne peut pas y changer\n",
              hj.dv1 * hj.dv1 / 1e6);
  std::printf("      grand-chose : il faut des assistances INTERNES (Venus, Terre).\n");
  std::printf("      C'est exactement ce qu'a fait Cassini : V-V-E-J, C3 = 16.6 km2/s2.\n");
  std::printf("      -> Lambert multi-jambes a 4 survols : V1.\n");
  std::printf("\n      En revanche, sur l'ARRIVEE, le survol divise la facture par %.1f :\n",
              dv_soi(vinf_direct) / std::fmax(1.0, dv_soi(best.vinf_s)));
  std::printf("      %.0f m/s au lieu de %.0f. Sur une masse a l'arrivee de 2 t et un\n",
              dv_soi(best.vinf_s), dv_soi(vinf_direct));
  {
    const double ve = 320.0 * G0, m = 2000.0;
    const double mp_d = m * (1.0 - std::exp(-dv_soi(vinf_direct) / ve));
    const double mp_j = m * (1.0 - std::exp(-dv_soi(best.vinf_s) / ve));
    std::printf("      Isp de 320 s : %.0f kg d'ergols au lieu de %.0f  ->  %.0f kg LIBERES.\n",
                mp_j, mp_d, mp_d - mp_j);
  }

  // --- l'usure de l'attente ---
  const double years = (best.tof1 + best.tof2) / (365.25 * DAY);
  std::printf("\n  MAIS : %.1f ans de croisiere. Un MMRTG perd 1,6 %% par an.\n", years);
  std::printf("        Puissance a l'arrivee : %.0f %% du debut de vie (110 W -> %.0f W).\n",
              100.0 * std::exp(-0.016 * years), 110.0 * std::exp(-0.016 * years));
  std::printf("        Le Delta-v du survol est gratuit. LE TEMPS NE L'EST PAS.\n");

  // =====================================================================
  // 3. LE PRIX REEL : LE CALENDRIER
  // =====================================================================
  std::printf("\n--- LE PRIX REEL DE L'ASSISTANCE : LE CALENDRIER --------------------\n");
  const double T_E = 365.256 * DAY, T_J = 4332.6 * DAY, T_S = 10759.2 * DAY;
  std::printf("  synodique Terre-Mars     : %6.1f jours  (%.1f mois)\n",
              astro::synodic(T_E, 686.98 * DAY) / DAY,
              astro::synodic(T_E, 686.98 * DAY) / DAY / 30.44);
  std::printf("  synodique Terre-Jupiter  : %6.1f jours  (%.1f mois)\n",
              astro::synodic(T_E, T_J) / DAY, astro::synodic(T_E, T_J) / DAY / 30.44);
  std::printf("  synodique JUPITER-SATURNE: %6.1f jours  (%.1f ANS)\n",
              astro::synodic(T_J, T_S) / DAY, astro::synodic(T_J, T_S) / (365.25 * DAY));
  std::printf("\n  >>> Une fenetre E-J-S exige que Jupiter ET Saturne soient bien places.\n");
  std::printf("      Cette configuration se reproduit tous les %.1f ANS.\n",
              astro::synodic(T_J, T_S) / (365.25 * DAY));
  std::printf("      Rater une fenetre Mars, c'est attendre 26 mois.\n");
  std::printf("      Rater une fenetre EJS, c'est attendre UNE GENERATION.\n");
  std::printf("\n      Le Delta-v du survol est gratuit. Le CALENDRIER ne l'est pas.\n");
  std::printf("      Et un RTG au plutonium 238 perd 1,6 %% par an pendant l'attente.\n");
  return 0;
}
