// scripts/t01_facture.cpp — LA FACTURE DU CIEL.
//
// « Concretement, qu'est-ce que ca implique de ne pas passer sous 2 000 m/s ? »
// Ce n'est pas une opinion, c'est Tsiolkovski. Le ciel se traduit en kilos, et
// les kilos sont la SCIENCE qu'on n'emporte pas. Axiome 5, applique au calendrier.
//
// Les deux SEULS chiffres injectes ici sont MESURES (voir docs/OPTIMISEUR.md) :
//   915 m/s  = le meilleur tour V-V-E-J-S trouve dans la fenetre 1996-2001
//  2892 m/s  = le meilleur tour V-V-E-J-S trouve dans la fenetre 2030-2040
// Tout le reste est calcule par le code du JEU (size_stage_for_dv), pas par moi.
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include "fen/vehicle/Vehicle.hpp"
#include "fen/mission/Program.hpp"
using namespace fen;

int main(int argc, char** argv) {
  const double science = (argc > 1) ? std::atof(argv[1]) : 350.0;   // kg de charge utile
  const double DV[2] = {915.0, 2892.0};
  const char* NM[2] = {"1997 (le ciel de Cassini)", "2030-2040 (le tien)"};
  const auto& E = mission::engines()[0];      // le moteur du catalogue du jeu

  std::printf("=====================================================================\n");
  std::printf(" LA FACTURE DU CIEL — %.0f m/s de geometrie ratee\n", DV[1] - DV[0]);
  std::printf(" Meme vaisseau, meme moteur (%s, Isp %.0f s). SEUL LE CIEL CHANGE.\n",
              E.eng.id.c_str(), E.eng.isp_vac);
  std::printf("=====================================================================\n");

  // --- 1) A SCIENCE EGALE : combien faut-il emporter d'ergols ? ---------------
  std::printf("\n--- a SCIENCE EGALE (%.0f kg) : ce qu'il faut emporter --------------\n", science);
  std::printf("  %-28s %9s %10s %10s\n", "fenetre", "dv (m/s)", "ergols kg", "m0 kg");
  double m0[2], mp[2];
  for (int k = 0; k < 2; ++k) {
    auto s = vehicle::size_stage_for_dv(DV[k], science, E.eng, E.tank_dry_fraction, 150.0, 0.02);
    m0[k] = s.m0; mp[k] = s.propellant;
    std::printf("  %-28s %9.0f %10.0f %10.0f\n", NM[k], DV[k], s.propellant, s.m0);
  }
  std::printf("  >>> ergols x%.2f   |   masse au decollage x%.2f\n", mp[1]/mp[0], m0[1]/m0[0]);

  // --- 2) A MASSE AU DECOLLAGE EGALE : que reste-t-il de science ? ------------
  // C'est la lecture qui compte : le lanceur est ce qu'il est. La masse ne bouge
  // pas. Ce qui bouge, c'est la CHARGE UTILE — donc la mission elle-meme.
  std::printf("\n--- a MASSE AU DECOLLAGE EGALE (%.0f kg) : ce qu'il RESTE -----------\n", m0[0]);
  double lo = 0.0, hi = science;
  for (int i = 0; i < 80; ++i) {                     // dichotomie sur la charge utile
    const double mid = 0.5 * (lo + hi);
    auto s = vehicle::size_stage_for_dv(DV[1], mid, E.eng, E.tank_dry_fraction, 150.0, 0.02);
    if (s.m0 <= m0[0]) lo = mid; else hi = mid;
  }
  std::printf("  1997        : %6.0f kg de science\n", science);
  std::printf("  2030-2040   : %6.0f kg de science      <<< il en reste %.0f %%\n",
              lo, 100.0 * lo / science);
  std::printf("\n  >>> A LANCEUR IDENTIQUE, LE CIEL DES ANNEES 2030 TE PREND %.0f kg DE\n",
              science - lo);
  std::printf("      CHARGE UTILE SCIENTIFIQUE. Aucun calcul ne te les rendra.\n");
  std::printf("      Rater la fenetre ne rend pas la mission plus CHERE.\n");
  std::printf("      Elle la rend plus PETITE. Et a la limite, elle la rend NULLE.\n");
  std::printf("\n      Trois issues, toutes chiffrables :\n");
  std::printf("        1. ATTENDRE la fenetre  (elle a une date. Il faut la mesurer.)\n");
  std::printf("        2. CHANGER de sequence  (V-E-E-J-S ? V-V-E-E-J-S ?)\n");
  std::printf("        3. PAYER                (lanceur plus lourd, ou meilleur Isp)\n");
  return 0;
}
