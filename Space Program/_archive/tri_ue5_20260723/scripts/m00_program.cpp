// scripts/m00_program.cpp
//
// LE PROGRAMME COMPLET — une mission peut enfin echouer parce qu'on n'a pas
// les moyens, pas le temps, ou pas assez de chances.
//
// Les chiffres de NAVIGATION viennent de la mesure (docs/NAVIGATION.md) :
// chaque niveau de poursuite achete une probabilite de succes ET impose une
// marge de correction — donc des ergols, donc de la masse, donc un lanceur.
// Ici on FERME la boucle : argent -> connaissance -> masse -> argent.
#include <cstdio>
#include <cmath>
#include <string>
#include <vector>
#include <tuple>
#include "fen/mission/Program.hpp"
#include "fen/astro/Transfers.hpp"
using namespace fen;
using namespace fen::mission;

struct TrackingLevel {
  const char* name;
  double musd, days, tcm_p99, p_physics;
};

int main() {
  std::printf("=====================================================================\n");
  std::printf(" M00 — LE PROGRAMME : argent, calendrier, risque\n");
  std::printf("=====================================================================\n");

  Contract C;
  C.payload_kg = 1200.0;
  C.budget_musd = 115.0;
  C.deadline_months = 18.0;
  C.min_success_prob = 0.85;
  std::printf("\n  CONTRAT : %.0f kg en GEO | budget %.0f M$ | delai %.0f mois"
              " | P(succes) >= %.0f %%\n",
              C.payload_kg, C.budget_musd, C.deadline_months, 100 * C.min_success_prob);

  // Chiffres MESURES par m00_nav (80 tirages par scenario) — pas inventes.
  const std::vector<TrackingLevel> T = {
      {"aveugle",              0.00,  0.0,  0.0, 0.062},
      {"30 min, 1 station",    0.07,  0.0, 33.0, 0.338},
      {"3h10, 1 station",      0.47,  0.0, 27.5, 0.450},
      {"3 stations, courts",   5.05,  0.0, 51.0, 0.831},
      {"3 stations, complets",10.80,  0.0, 74.4, 0.873},
      {"+2 revolutions",      32.93,  4.0, 79.5, 0.975},
  };
  const double DV_NOMINAL = 4291.1, FINITE_LOSS = 5.4;
  const int N_BURNS = 4;

  auto run_matrix = [&](const Contract& c, bool verbose) {
    Assessment best{}; double best_margin = -1e9; std::string best_line;
    if (verbose) {
      std::printf("\n  %-13s %-21s %-4s %7s %8s %7s %7s  %s\n",
                  "moteur", "poursuite", "lanc", "m0", "cout", "delai", "P(ok)", "verdict");
      std::printf("  %s\n", std::string(96, '-').c_str());
    }
    for (std::size_t e = 0; e < engines().size(); ++e)
      for (const auto& t : T)
        for (int li = 0; li < static_cast<int>(launchers().size()); ++li) {
          Program pr;
          pr.engine_index = static_cast<int>(e);
          pr.launcher_index = li;                 // LE JOUEUR CHOISIT
          pr.tracking_musd = t.musd;
          pr.tracking_days = t.days;
          pr.dv_margin = t.tcm_p99 + 20.0;
          pr.compute_musd = 1.5;
          pr.review = true;
          pr.test_hours = (engines()[e].flight_heritage == 0) ? 900.0 : 0.0;

          auto a = assess(c, pr, N_BURNS, DV_NOMINAL, FINITE_LOSS);
          if (a.launcher_index < 0) continue;
          finalize(a, c, t.p_physics);
          if (verbose && (a.ok || (a.fits_budget && a.fits_schedule)))
            std::printf("  %-13s %-21s %-4s %6.0fkg %6.1fM$ %5.1fmo %6.1f%%  %s\n",
                        engines()[e].eng.id.c_str(), t.name,
                        launchers()[li].id.substr(0,3).c_str(), a.m0_kg,
                        a.cost_total, a.schedule_months, 100 * a.p_success,
                        a.ok ? "VIABLE" : ("ECHEC : " + a.why).c_str());
          if (a.ok) {
            const double m = c.budget_musd - a.cost_total;
            if (m > best_margin) {
              best_margin = m; best = a;
              best_line = engines()[e].eng.id + " + " + t.name + " + " + launchers()[li].id;
            }
          }
        }
    return std::make_tuple(best, best_margin, best_line);
  };

  std::printf("\n--- CONTRAT INITIAL : %.0f M$ / %.0f mois / P >= %.0f %% -----------------\n",
              C.budget_musd, C.deadline_months, 100 * C.min_success_prob);
  auto [b0, m0_, l0] = run_matrix(C, true);
  if (m0_ < -1e8) {
    std::printf("\n  >>> AUCUN PROGRAMME VIABLE. Le contrat n'est pas tenable.\n");
    std::printf("      (Aucune ligne ne tient les QUATRE contraintes a la fois. Ce n'est\n");
    std::printf("       pas une punition : c'est un resultat d'etude de mission, et\n");
    std::printf("       c'est ce qu'une vraie equipe rapporte au client.)\n");

    // SUR QUEL AXE NEGOCIER ? On cherche la plus petite concession, sur chaque axe.
    std::printf("\n--- SUR QUEL AXE NEGOCIER ? ----------------------------------------\n");

    double need_B = -1;
    for (double B = C.budget_musd; B <= 300.0; B += 0.5) {
      Contract c2 = C; c2.budget_musd = B;
      if (std::get<1>(run_matrix(c2, false)) > -1e8) { need_B = B; break; }
    }
    double need_D = -1;
    for (double D = C.deadline_months; D <= 36.0; D += 0.1) {
      Contract c2 = C; c2.deadline_months = D;
      if (std::get<1>(run_matrix(c2, false)) > -1e8) { need_D = D; break; }
    }
    double need_P = -1;
    for (double P = C.min_success_prob; P >= 0.40; P -= 0.005) {
      Contract c2 = C; c2.min_success_prob = P;
      if (std::get<1>(run_matrix(c2, false)) > -1e8) { need_P = P; break; }
    }

    if (need_B > 0) std::printf("  BUDGET     : +%.1f M$   (%.0f -> %.1f, soit +%.0f %%)\n",
                               need_B - C.budget_musd, C.budget_musd, need_B,
                               100 * (need_B / C.budget_musd - 1));
    else            std::printf("  BUDGET     : aucun budget ne suffit.\n");
    if (need_D > 0) std::printf("  CALENDRIER : +%.1f mois (%.0f -> %.1f)  = %.0f JOURS\n",
                               need_D - C.deadline_months, C.deadline_months, need_D,
                               (need_D - C.deadline_months) * 30.44);
    else            std::printf("  CALENDRIER : aucun delai ne suffit.\n");
    if (need_P > 0) std::printf("  RISQUE     : accepter P >= %.1f %% au lieu de %.0f %%\n",
                               100 * need_P, 100 * C.min_success_prob);

    // Ni le budget seul ni le delai seul ne suffisent : il faut les DEUX.
    double jb = -1, jd = -1;
    for (double B = C.budget_musd; B <= 200.0 && jb < 0; B += 1.0)
      for (double D = C.deadline_months; D <= 24.0; D += 0.1) {
        Contract c2 = C; c2.budget_musd = B; c2.deadline_months = D;
        if (std::get<1>(run_matrix(c2, false)) > -1e8) { jb = B; jd = D; break; }
      }
    if (jb > 0)
      std::printf("  CONJOINT   : +%.0f M$ ET +%.0f jours  (les deux, pas l'un OU l'autre)\n",
                  jb - C.budget_musd, (jd - C.deadline_months) * 30.44);

    std::printf("\n  >>> CE QUE LA MATRICE DIT, ET QUI DERANGE :\n");
    std::printf("      Le budget SEUL ne suffit pas. Le delai SEUL ne suffit pas.\n");
    std::printf("      Il faut +%.0f M$ ET +%.0f jours — ou bien accepter que le client\n",
                jb - C.budget_musd, (jd - C.deadline_months) * 30.44);
    std::printf("      revoie son exigence de %.0f %% a %.1f %%.\n",
                100 * C.min_success_prob, 100 * need_P);
    std::printf("\n      LA CONCESSION LA MOINS CHERE COUTE ZERO DOLLAR ET ZERO JOUR :\n");
    std::printf("      c'est la CONFIANCE DU CLIENT. 2,5 points de P(succes).\n");
    std::printf("      C'est exactement ce qui se passe dans les vrais programmes,\n");
    std::printf("      et ce n'est pas confortable a regarder.\n");

    C.min_success_prob = need_P;   // on retient la concession la moins chere
    auto [b, m, l] = run_matrix(C, false);
    b0 = b; m0_ = m; l0 = l;
  }

  auto& best = b0; auto& best_margin = m0_; auto& best_line = l0;
  std::printf("\n--- LE PROGRAMME RETENU --------------------------------------------\n");
  std::printf("  %s\n", best_line.c_str());
  std::printf("    Delta-v provisionne %.0f m/s -> ergols %.0f kg -> masse %.0f kg\n",
              best.dv_design, best.propellant_kg, best.m0_kg);
  std::printf("    lanceur %5.1f | moteur %5.1f | etage %5.1f | poursuite %5.1f\n",
              best.cost_launcher, best.cost_engine, best.cost_stage, best.cost_tracking);
  std::printf("    calcul  %5.1f | essais %5.1f | revue  %5.1f | operations %5.1f\n",
              best.cost_compute, best.cost_tests, best.cost_review, best.cost_ops);
  std::printf("    TOTAL %.1f M$ (marge %.1f) | %.1f mois | P(succes) = %.1f %%\n",
              best.cost_total, best_margin, best.schedule_months, 100 * best.p_success);
  std::printf("    risque : lanceur %.1f %% | moteur (%d allumages) %.1f %% |"
              " bevue %.1f %% | PHYSIQUE %.1f %%\n",
              100 * best.p_launcher, N_BURNS, 100 * best.p_engine,
              100 * (1 - best.p_blunder), 100 * best.p_physics);
  std::printf("\n  >>> LE MAILLON FAIBLE EST LA PHYSIQUE (%.1f %%), PAS LE MATERIEL.\n",
              100 * best.p_physics);
  std::printf("      Autrement dit : ce qui tue cette mission, ce n'est pas une panne.\n");
  std::printf("      C'est une NAVIGATION insuffisamment achetee.\n");

  // --- LA REVUE : est-ce qu'elle se paie ? ---
  std::printf("\n--- LA REVUE INDEPENDANTE SE PAIE-T-ELLE ? -------------------------\n");
  std::printf("  Sans revue : P(bevue non attrapee) = %.1f %%  -> P(succes) x %.3f\n",
              100 * P_BLUNDER_NO_REVIEW, 1 - P_BLUNDER_NO_REVIEW);
  std::printf("  Avec revue : P(bevue non attrapee) = %.1f %%  -> P(succes) x %.3f  (+%.1f M$)\n",
              100 * P_BLUNDER_REVIEW, 1 - P_BLUNDER_REVIEW, COST_REVIEW);
  const double gain = (1 - P_BLUNDER_REVIEW) / (1 - P_BLUNDER_NO_REVIEW) - 1.0;
  std::printf("  >>> +%.1f %% de P(succes) pour %.0f M$ sur un programme a %.0f M$.\n",
              100 * gain, COST_REVIEW, best.cost_total);
  std::printf("      Soit %.1f M$ de valeur esperee recuperee. Elle se paie %.1f fois.\n",
              gain * best.cost_total, gain * best.cost_total / COST_REVIEW);
  std::printf("      (Mars Climate Orbiter, 1999 : 327 M$ perdus faute d'avoir relu\n");
  std::printf("       une conversion d'unites. La revue coutait moins que ca.)\n");
  return 0;
}
