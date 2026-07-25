// scripts/m01_porkchop.cpp
// La carte des fenêtres Terre→Mars, et sa VALIDATION.
//
// Critère d'acceptation de la Phase 2 : le minimum de C3 doit se situer au-dessus
// de la borne de Hohmann coplanaire circulaire (8,7 km²/s²) — puisque les orbites
// réelles sont excentriques et inclinées — et rester dans la plage physique
// connue des fenêtres martiennes (8–20 km²/s²), avec un temps de transit voisin
// des 259 jours du transfert de Hohmann.
#include <cstdio>
#include <cmath>
#include <string>
#include "fen/astro/Porkchop.hpp"
#include "fen/astro/Transfers.hpp"
#include "fen/core/Epoch.hpp"

using namespace fen;
using namespace fen::cst;

int main() {
  ephem::StandishEphemeris eph;

  // --- borne théorique : Hohmann coplanaire circulaire 1 UA -> 1,524 UA -------
  const double r1 = 1.0 * AU, r2 = 1.524 * AU;
  const auto H = astro::hohmann(r1, r2, MU_SUN);
  const double v_E = astro::v_circular(r1, MU_SUN);
  const double vinf_hohmann = H.dv1;                 // dv1 = v_p - v_circ = v_inf idéal
  const double c3_hohmann = vinf_hohmann * vinf_hohmann / 1e6;
  std::printf("=====================================================================\n");
  std::printf(" M01 — PORKCHOP TERRE -> MARS\n");
  std::printf("=====================================================================\n");
  std::printf("\nBORNE THEORIQUE (Hohmann coplanaire circulaire, 1 -> 1,524 UA) :\n");
  std::printf("  v_Terre = %.3f km/s   v_inf = %.3f km/s   C3 = %.2f km^2/s^2\n",
              v_E / 1000, vinf_hohmann / 1000, c3_hohmann);
  std::printf("  temps de transit = %.1f jours\n", H.tof / DAY);

  // --- porkchop sur la fenêtre 2026-2027 -------------------------------------
  const double t0 = epoch_from_iso("2026-09-01T00:00:00").tdb;
  const double t1 = epoch_from_iso("2027-03-01T00:00:00").tdb;
  auto pc = astro::porkchop(eph, ephem::Body::EarthBary, ephem::Body::Mars,
                            t0, t1, 61, 150.0 * DAY, 400.0 * DAY, 51);

  std::printf("\nGRILLE : 61 dates de depart (sept. 2026 - mars 2027) x 51 durees (150-400 j)\n");

  // --- carte ASCII du C3 ------------------------------------------------------
  std::printf("\n  C3 [km^2/s^2] :  . <10   : <14   o <20   O <30   # <50   (vide = >50)\n");
  std::printf("  tof(j)\\dep  ");
  for (int i = 0; i < pc.n_dep; i += 6) std::printf("%-6s", epoch_to_iso(Epoch{pc.at(i,0).t_dep}).substr(2,5).c_str());
  std::printf("\n");
  for (int j = pc.n_tof - 1; j >= 0; j -= 2) {
    std::printf("  %5.0f       ", pc.at(0, j).tof / DAY);
    for (int i = 0; i < pc.n_dep; ++i) {
      const auto& p = pc.at(i, j);
      const double c3 = p.ok ? p.c3 / 1e6 : 1e9;
      char c = ' ';
      if (c3 < 10) c = '.';
      else if (c3 < 14) c = ':';
      else if (c3 < 20) c = 'o';
      else if (c3 < 30) c = 'O';
      else if (c3 < 50) c = '#';
      std::printf("%c", c);
    }
    std::printf("\n");
  }

  const auto& b = pc.best_c3;
  std::printf("\nMINIMUM DE C3 :\n");
  std::printf("  depart   : %s\n", epoch_to_iso(Epoch{b.t_dep}).c_str());
  std::printf("  arrivee  : %s\n", epoch_to_iso(Epoch{b.t_dep + b.tof}).c_str());
  std::printf("  transit  : %.1f jours\n", b.tof / DAY);
  std::printf("  C3       : %.2f km^2/s^2   (v_inf depart = %.3f km/s)\n", b.c3 / 1e6, b.vinf_dep / 1000);
  std::printf("  v_inf arrivee = %.3f km/s\n", b.vinf_arr / 1000);

  const auto& t = pc.best_total;
  std::printf("\nMINIMUM de (v_inf_depart + v_inf_arrivee)  [proxy du Delta-v total] :\n");
  std::printf("  depart %s | transit %.1f j | C3 = %.2f | v_inf_arr = %.3f km/s\n",
              epoch_to_iso(Epoch{t.t_dep}).substr(0, 10).c_str(), t.tof / DAY,
              t.c3 / 1e6, t.vinf_arr / 1000);

  // --- VALIDATION -------------------------------------------------------------
  const double c3 = b.c3 / 1e6;
  const double tof_d = b.tof / DAY;
  int fail = 0;
  std::printf("\n--- VALIDATION ------------------------------------------------------\n");
  auto chk = [&](bool ok, const char* msg) {
    std::printf("  [%s] %s\n", ok ? "OK  " : "RATE", msg);
    if (!ok) ++fail;
  };
  chk(c3 >= c3_hohmann,
      "C3_min >= borne de Hohmann (les orbites reelles sont excentriques et inclinees)");
  chk(c3 < 20.0, "C3_min < 20 km^2/s^2 : plage physique des fenetres martiennes");
  chk(tof_d > 180.0 && tof_d < 380.0, "temps de transit dans la plage des transferts de type I/II");
  chk(b.vinf_arr / 1000 > 2.0 && b.vinf_arr / 1000 < 4.5,
      "v_inf a l'arrivee entre 2 et 4,5 km/s (plage des missions martiennes reelles)");

  // Delta-v d'injection depuis une orbite de parking LEO 200 km.
  const double rp = R_EARTH + 200e3;
  const double dv_tmi = astro::dv_injection(rp, b.vinf_dep, MU_EARTH);
  std::printf("\n  Delta-v d'injection depuis LEO 200 km : %.1f m/s\n", dv_tmi);
  std::printf("  AMPLIFICATION des erreurs d'injection : v_p/v_inf = %.2f\n",
              std::sqrt(b.vinf_dep * b.vinf_dep + 2 * MU_EARTH / rp) / b.vinf_dep);
  std::printf("  -> 1 m/s d'erreur au perigee devient %.2f m/s d'erreur sur v_inf.\n",
              std::sqrt(b.vinf_dep * b.vinf_dep + 2 * MU_EARTH / rp) / b.vinf_dep);
  std::printf("     C'est l'hyperbole d'echappement qui amplifie, pas le jeu.\n");

  std::printf("\n%s\n", fail == 0 ? ">>> PORKCHOP VALIDE" : ">>> PORKCHOP INVALIDE");
  return fail;
}
