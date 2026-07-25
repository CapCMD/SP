// scripts/t01_veega.cpp
//
// TITAN — CASSER LE MUR DU C3.
//
// Établi précédemment : la ligne droite vers Saturne exige C3 = 106 km²/s². Un
// seul survol jovien le ramène à 84 — insuffisant, parce qu'atteindre Jupiter
// coûte déjà 77.
//
// La seule sortie est celle de Minovitch (1961) : GAGNER de l'énergie AVANT, sur
// les planètes internes. Vénus, Vénus, Terre, puis Jupiter. C'est ce qu'a fait
// Cassini (C3 = 16,6 km²/s²), et c'est la seule raison pour laquelle une sonde
// de 5,7 tonnes a pu atteindre Saturne.
//
// Le problème devient une OPTIMISATION GLOBALE à 11 inconnues, dans un paysage
// à des milliers de minima locaux. Ce n'est plus une équation : c'est une
// DÉPENSE DE TEMPS DE CALCUL. Le jeu la facture — comme il facture la fidélité
// de modèle, et pour la même raison.
#include <cstdio>
#include <cmath>
#include <string>
#include <vector>
#include "fen/astro/Mga.hpp"
#include "fen/core/Epoch.hpp"

using namespace fen;
using namespace fen::cst;
using ephem::Body;

static constexpr double R_VENUS   = 6051.8e3;
static constexpr double R_JUPITER = 71492e3;

static void report(const char* title, const astro::MgaProblem& p,
                   const ephem::IEphemeris& eph, const astro::MgaResult& r) {
  std::printf("\n--- %s\n", title);
  if (!r.feasible) { std::printf("  INFAISABLE\n"); return; }
  const char* nm[] = {"TERRE", "VENUS", "VENUS", "TERRE", "JUPITER", "SATURNE"};
  for (std::size_t i = 0; i < r.t.size(); ++i) {
    std::printf("  %-8s %s", (i < 6 ? nm[i] : ephem::body_name(p.seq[i])),
                epoch_to_iso(Epoch{r.t[i]}).substr(0, 10).c_str());
    if (i > 0 && i + 1 < r.t.size()) {
      const std::size_t k = i - 1;
      const double R = (p.seq[i] == Body::Jupiter) ? R_JUPITER
                     : (p.seq[i] == Body::Venus)   ? R_VENUS : R_EARTH;
      std::printf("  survol %6.2f R | v_inf %5.2f -> %5.2f km/s  (DESACCORD %+5.2f)"
                  " | dv %6.0f | GAGNE %4.2f km/s",
                  r.rp[k] / R, r.vinf_in[k] / 1000, r.vinf_out[k] / 1000,
                  (r.vinf_out[k] - r.vinf_in[k]) / 1000, r.dv_fb[k], r.gain[k] / 1000);
    }
    if (i + 1 < r.t.size()) std::printf("   [%d rev]", r.revs[i]);
    std::printf("\n");
  }
  double gained = 0;
  for (double d : r.gain) gained += d;
  std::printf("\n  C3 de lancement        : %7.2f km2/s2   (budget lanceur : %.0f)\n",
              r.c3 / 1e6, p.c3_max / 1e6);
  std::printf("  v_inf a Saturne        : %7.2f km/s\n", r.vinf_arr / 1000);
  std::printf("  Delta-v d'insertion    : %7.0f m/s\n", r.dv_insert);
  std::printf("  Delta-v des survols    : %7.0f m/s\n", r.dv_onboard - r.dv_insert);
  std::printf("  >>> Delta-v EMBARQUE   : %7.0f m/s\n", r.dv_onboard);
  std::printf("  >>> Delta-v ENCAISSE   : %7.2f km/s  (somme des survols)\n", gained / 1000);
  std::printf("  duree totale           : %7.2f ans\n", r.tof_total / (365.25 * DAY));
}

int main(int argc, char** argv) {
  ephem::StandishEphemeris eph;
  const int gens = (argc > 1) ? std::atoi(argv[1]) : 900;

  std::printf("=====================================================================\n");
  std::printf(" T01 — TITAN : CASSER LE MUR DU C3 (assistances internes)\n");
  std::printf("=====================================================================\n");

  astro::MgaProblem p;
  p.seq = {Body::EarthBary, Body::Venus, Body::Venus, Body::EarthBary,
           Body::Jupiter, Body::Saturn};
  p.rp_min = {R_VENUS + 300e3, R_VENUS + 300e3, R_EARTH + 300e3, 10.0 * R_JUPITER};
  p.t0_lo = epoch_from_iso("2030-01-01T00:00:00").tdb;
  p.t0_hi = epoch_from_iso("2040-01-01T00:00:00").tdb;
  p.tof_lo = { 50.0 * DAY, 100.0 * DAY,  50.0 * DAY,  500.0 * DAY,  900.0 * DAY};
  p.tof_hi = {400.0 * DAY, 500.0 * DAY, 450.0 * DAY, 2200.0 * DAY, 3200.0 * DAY};
  p.max_revs = 2;
  p.c3_max = 20e6;                       // 20 km2/s2 : un lanceur reel, avec charge utile
  p.tof_total_max = 9.0 * 365.25 * DAY;  // 9 ans : ce que le MMRTG supporte
  p.rp_insert = 2.5 * R_SATURN;
  p.a_insert = std::cbrt(MU_SATURN * std::pow(120.0 * DAY, 2.0) / (4.0 * PI * PI));

  std::printf("\n  sequence   : TERRE - VENUS - VENUS - TERRE - JUPITER - SATURNE\n");
  std::printf("  contraintes: C3 <= %.0f km2/s2 | duree <= %.0f ans\n",
              p.c3_max / 1e6, p.tof_total_max / (365.25 * DAY));
  std::printf("               survols : Venus/Terre >= 300 km d'altitude,\n");
  std::printf("                         Jupiter >= 10 R_J (dose de radiation)\n");
  std::printf("  inconnues  : %d (date + %d durees + %d nombres de revolutions)\n",
              astro::n_vars(p), astro::n_legs(p), astro::n_legs(p));

  const int D = astro::n_vars(p), L = astro::n_legs(p);
  std::vector<double> lo(D), hi(D);
  lo[0] = p.t0_lo; hi[0] = p.t0_hi;
  for (int i = 0; i < L; ++i) { lo[1 + i] = p.tof_lo[i]; hi[1 + i] = p.tof_hi[i]; }
  for (int i = 0; i < L; ++i) { lo[1 + L + i] = 0.0; hi[1 + L + i] = p.max_revs + 0.999; }

  auto f = [&](const std::vector<double>& x) { return astro::mga_evaluate(p, eph, x).cost; };

  std::printf("\n--- EVOLUTION DIFFERENTIELLE ---------------------------------------\n");
  std::printf("  le paysage est MULTIMODAL : pas de gradient a descendre.\n");
  astro::MgaResult best;
  double best_f = 1e300;
  long long evals = 0;
  // 8 redemarrages : un seul run de DE tombe dans un minimum local. C'est le
  // TEMPS DE CALCUL qu'on depense, et c'est une ressource du jeu.
  for (int run = 0; run < 14; ++run) {
    auto de = astro::differential_evolution(f, lo, hi, 120, gens, 20260714ull + run * 7919ull);
    evals += de.evals;
    auto r = astro::mga_evaluate(p, eph, de.x);
    std::printf("  run %d : cout = %9.1f m/s%s\n", run, de.f,
                (de.f < best_f) ? "   <- meilleur" : "");
    if (de.f < best_f && r.feasible) { best_f = de.f; best = r; }
  }
  std::printf("  %lld evaluations de chaine (5 Lambert + 4 survols chacune)\n", evals);

  if (!best.feasible) { std::printf("\n  *** aucun tour faisable trouve.\n"); return 1; }
  report("MEILLEUR TOUR TROUVE ------------------------------------------", p, eph, best);

  std::printf("\n--- LES TROIS ROUTES VERS SATURNE ----------------------------------\n");
  std::printf("  %-26s %10s %12s %10s\n", "", "C3", "dv embarque", "duree");
  std::printf("  %s\n", std::string(62, '-').c_str());
  std::printf("  %-26s %7.1f     %8s     %6.1f ans\n",
              "DIRECT (Hohmann)", 105.9, "832 m/s", 6.1);
  std::printf("  %-26s %7.1f     %8s     %6.1f ans\n",
              "1 survol Jupiter (E-J-S)", 83.6, "634 m/s", 11.6);
  std::printf("  %-26s %7.2f     %6.0f m/s     %6.2f ans\n",
              "V-V-E-J-S (ce programme)", best.c3 / 1e6, best.dv_onboard,
              best.tof_total / (365.25 * DAY));
  std::printf("  %-26s %7.1f     %8s     %6.1f ans\n",
              "Cassini (reel, VVEJGA)", 16.6, "~1400 m/s", 6.7);

  std::printf("\n  >>> LE MUR EST TOMBE. C3 : 106 -> %.1f km2/s2.\n", best.c3 / 1e6);
  std::printf("      Ce n'est plus un lanceur imaginaire : c'est un lanceur qui EXISTE.\n");
  std::printf("      Et ce que la sonde encaisse gratuitement en route :\n");
  {
    double g = 0;
    for (double d : best.gain) g += d;
    std::printf("      %.2f km/s. Un etage supplementaire aurait pese des TONNES.\n", g / 1000);
  }
  std::printf("\n      PAYE EN : %d survols a placer, %.2f ans de croisiere,\n",
              astro::n_flybys(p), best.tof_total / (365.25 * DAY));
  std::printf("               une fenetre qui n'existe pas tous les ans,\n");
  std::printf("               et %lld evaluations de trajectoire pour la trouver.\n", evals);
  std::printf("\n      LE TEMPS DE CALCUL EST UNE RESSOURCE. C'est le meme axiome que\n");
  std::printf("      l'economie de fidelite de modele — et la meme facture.\n");

  // ================= LE DIAGNOSTIC QUI COMPTE =============================
  std::printf("\n--- POURQUOI LE DELTA-V EMBARQUE RESTE GROS -------------------------\n");
  double worst = 0; std::size_t iw = 0;
  for (std::size_t k = 0; k < best.dv_fb.size(); ++k)
    if (best.dv_fb[k] > worst) { worst = best.dv_fb[k]; iw = k; }
  std::printf("  Regardez la colonne DESACCORD. Un survol NON propulse exige\n");
  std::printf("  |v_inf_entrant| = |v_inf_sortant|. Ici, au survol n%zu, l'ecart est\n", iw + 1);
  std::printf("  de %+.2f km/s — et il faut le payer AU PERIASTRE : %.0f m/s.\n",
              (best.vinf_out[iw] - best.vinf_in[iw]) / 1000, worst);
  std::printf("\n  CE DESACCORD N'EST PAS UN DEFAUT DE L'OPTIMISEUR. C'est la limite\n");
  std::printf("  structurelle du modele : deux arcs de Lambert consecutifs n'ont AUCUNE\n");
  std::printf("  raison de se raccorder avec le meme |v_inf|. Il n'y a que deux façons\n");
  std::printf("  de les reconcilier :\n");
  std::printf("    (a) tomber par chance sur des dates ou ca colle  -> tres rare ;\n");
  std::printf("    (b) ajouter une MANOEUVRE EN ESPACE PROFOND au milieu de la jambe,\n");
  std::printf("        qui deforme l'arc jusqu'a ce que les |v_inf| s'accordent.\n");
  std::printf("\n  >>> C'EST EXACTEMENT CE QU'A FAIT CASSINI : 450 m/s a l'aphelie,\n");
  std::printf("      entre les deux survols de Venus. Sans cette manoeuvre, la\n");
  std::printf("      resonance Venus-Venus ne se referme pas.\n");
  std::printf("\n      La DSM n'est donc PAS un raffinement optionnel. C'est le\n");
  std::printf("      mecanisme qui rend le tour possible. MGA-1DSM : brique suivante.\n");
  std::printf("      (Et cette conclusion, ce n'est pas moi qui l'ai decidee :\n");
  std::printf("       c'est le chiffre %.0f m/s qui l'impose.)\n", worst);
  return 0;
}
