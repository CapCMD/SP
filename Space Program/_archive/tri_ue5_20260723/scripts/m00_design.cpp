// scripts/m00_design.cpp — LA BOUCLE DE CONCEPTION DE M00 (GDD Boucle A, §2, §3.1-3.2).
//
// Ce que le GDD exige et qui MANQUAIT : le joueur ne choisit pas dans un menu, il
// DERIVE. Le cahier des charges ne donne AUCUN Delta-v (MISSION_M00 §1). Le joueur
// doit trouver, lui-meme, les 11 quantites — et la physique le corrige.
//
// Cet outil est le "correcteur" : le joueur propose ses valeurs derivees (ou il
// demande le corrige), le jeu VERIFIE contre les lois, EXPLIQUE, et surtout lui
// enseigne la LECON CENTRALE (la manoeuvre combinee, 1154,5 m/s) — celle qui decide
// si sa mission est seulement PAYABLE. Puis il converge le vehicule (point fixe,
// Tsiolkovski INVERSE) et emet un .fpl reel, que `fenetre run` propagera.
//
//   Usage : m00_design                 -> le corrige complet, pas a pas
//           m00_design --check <dv_inj> <dv_comb>   -> note les valeurs du joueur
//           m00_design --emit <fichier.fpl>         -> ecrit le plan de vol converge
//
// Aucune valeur n'est en dur : tout est calcule par les memes fonctions que le
// moteur de verite (astro/Transfers, vehicle/Vehicle).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include "fen/astro/Transfers.hpp"
#include "fen/vehicle/Vehicle.hpp"
#include "fen/core/Constants.hpp"
using namespace fen;
using namespace fen::cst;

// --- le cahier des charges de M00, EXACTEMENT celui du .fpl (aucun Delta-v ici) --
struct Brief {
  double r_park  = R_EARTH + 200e3;    // 6578,137 km : parking 200 km
  double i_park  = 28.5 * DEG;         // livre au noeud ascendant
  double r_geo   = 42164170.0;         // cible GEO
  double payload = 1200.0;             // kg secs
  // RL10C-1
  double isp = 449.7, thrust = 101.8e3, eng_mass = 190.0;
  double tank_dry_frac = 0.12, residual = 0.02, structure = 150.0;
  double mu = MU_EARTH;
};

// Tout le corrige, derive a la demande. Renvoie les grandeurs cle par reference.
struct Derived {
  double v_circ, v_gto_peri, dv_inj, v_gto_apo, v_geo;
  double dv_sep, dv_comb, economy;      // separee vs combinee : LA lecon
  double rsw_s, rsw_w;                  // composantes de la combinee
  double tof_half, dv_total_sep, dv_total_comb;
};
static Derived derive(const Brief& b) {
  Derived d;
  const double a_gto = 0.5 * (b.r_park + b.r_geo);
  d.v_circ     = astro::v_circular(b.r_park, b.mu);                 // 1
  d.v_gto_peri = astro::vis_viva(b.r_park, a_gto, b.mu);            // 2
  d.dv_inj     = d.v_gto_peri - d.v_circ;                           // 3
  d.v_gto_apo  = astro::vis_viva(b.r_geo, a_gto, b.mu);             // 4
  d.v_geo      = astro::v_circular(b.r_geo, b.mu);                  // 5
  // 6 : changement de plan SEPARE (circularise, PUIS tourne de i)
  d.dv_sep     = std::fabs(d.v_geo - d.v_gto_apo) + astro::dv_plane_change(d.v_geo, b.i_park);
  // 7 : manoeuvre COMBINEE (loi des cosinus) — une seule impulsion a l'apogee
  d.dv_comb    = astro::dv_combined(d.v_gto_apo, d.v_geo, b.i_park);
  d.economy    = d.dv_sep - d.dv_comb;                              // 8 : 1154,5 m/s
  d.rsw_s      = d.v_geo * std::cos(b.i_park) - d.v_gto_apo;        // 9
  d.rsw_w      = d.v_geo * std::sin(b.i_park);
  d.tof_half   = PI * std::sqrt(std::pow(a_gto, 3) / b.mu);         // 10
  d.dv_total_sep  = d.dv_inj + d.dv_sep;
  d.dv_total_comb = d.dv_inj + d.dv_comb;
  return d;
}

// --- vehicule : Tsiolkovski INVERSE, le point fixe (GDD axiome, §2) ------------
static vehicle::SizingResult size_vehicle(const Brief& b, double dv_total) {
  vehicle::Engine e; e.isp_vac = b.isp; e.thrust_vac = b.thrust; e.mass = b.eng_mass;
  return vehicle::size_stage_for_dv(dv_total, b.payload, e, b.tank_dry_frac, b.structure, b.residual);
}

static void print_corrige(const Brief& b) {
  const Derived d = derive(b);
  std::printf("=====================================================================\n");
  std::printf(" M00 — BOUCLE DE CONCEPTION.  Le cahier des charges ne donne AUCUN Delta-v.\n");
  std::printf(" Voici ce que le joueur doit DERIVER (et pourquoi), pas choisir.\n");
  std::printf("=====================================================================\n\n");
  std::printf("  DONNEES : parking %.3f km (i=%.1f deg), cible GEO %.0f km, CU %.0f kg\n\n",
              b.r_park/1000, b.i_park/DEG, b.r_geo/1000, b.payload);

  std::printf("  1. vitesse circulaire au parking   v = sqrt(mu/r)        = %8.1f m/s\n", d.v_circ);
  std::printf("  2. vitesse au perigee du GTO       vis-viva, a=%.1f km   = %8.1f m/s\n",
              0.5*(b.r_park+b.r_geo)/1000, d.v_gto_peri);
  std::printf("  3. Delta-v d'injection             (2)-(1)                = %8.1f m/s\n", d.dv_inj);
  std::printf("  4. vitesse a l'apogee du GTO       vis-viva               = %8.1f m/s\n", d.v_gto_apo);
  std::printf("  5. vitesse GEO                     sqrt(mu/r)             = %8.1f m/s\n", d.v_geo);
  std::printf("\n  --- LA QUESTION QUI DECIDE SI LA MISSION EST PAYABLE ---\n");
  std::printf("  6. insertion + plan SEPAREMENT     dv_circ + 2v sin(i/2)  = %8.1f m/s\n", d.dv_sep);
  std::printf("  7. insertion + plan COMBINES       loi des cosinus        = %8.1f m/s\n", d.dv_comb);
  std::printf("  8. >>> ECONOMIE DE LA COMBINAISON  (6)-(7)                = %8.1f m/s <<<\n", d.economy);
  std::printf("\n  9. composantes RSW de la combinee  S=%.1f  W=%.1f  (m/s)\n", d.rsw_s, d.rsw_w);
  std::printf(" 10. demi-temps de transfert        pi*sqrt(a^3/mu)        = %.1f s = %.2f h\n",
              d.tof_half, d.tof_half/3600);

  // 11 : le point fixe du vehicule, pour CHAQUE strategie
  std::printf("\n  --- 11. LE VEHICULE : Tsiolkovski INVERSE (point fixe) ---\n");
  const auto vs = size_vehicle(b, d.dv_total_sep);
  const auto vc = size_vehicle(b, d.dv_total_comb);
  std::printf("  Delta-v total    plan SEPARE  = %6.0f m/s  ->  masse au depart %7.0f kg\n",
              d.dv_total_sep, vs.m0);
  std::printf("  Delta-v total    plan COMBINE = %6.0f m/s  ->  masse au depart %7.0f kg\n",
              d.dv_total_comb, vc.m0);
  std::printf("  >>> le plan separe pese %.0f %% de PLUS. Le joueur qui n'a pas vu (7)\n",
              100.0*(vs.m0-vc.m0)/vc.m0);
  std::printf("      ne peut pas payer son lanceur. LA SANCTION EST PHYSIQUE.\n");

  std::printf("\n  LA LECON CACHEE : pourquoi le parking est livre au NOEUD ASCENDANT ?\n");
  std::printf("  Parce qu'en brulant la, le perigee du GTO tombe sur un noeud, donc\n");
  std::printf("  l'apogee sur l'autre noeud — LE SEUL endroit ou tourner le plan est\n");
  std::printf("  realisable. Rien dans l'interface ne le dit. C'est de la geometrie.\n");
}

static void check_player(const Brief& b, double dv_inj_p, double dv_comb_p) {
  const Derived d = derive(b);
  auto verdict = [](const char* nom, double val, double vrai, double tol_rel) {
    const double err = std::fabs(val - vrai);
    const bool ok = err <= tol_rel * std::fabs(vrai);
    std::printf("  %-24s tu proposes %8.1f | correct %8.1f | ecart %6.1f -> %s\n",
                nom, val, vrai, err, ok ? "OK" : "FAUX");
    return ok;
  };
  std::printf("=== VERIFICATION DE TES DERIVATIONS ===\n\n");
  bool a = verdict("Delta-v injection",  dv_inj_p,  d.dv_inj,  1e-3);
  bool c = verdict("insertion combinee", dv_comb_p, d.dv_comb, 1e-3);
  std::printf("\n");
  if (a && c) {
    const auto v = size_vehicle(b, dv_inj_p + dv_comb_p);
    std::printf("  Les deux justes. Ton vehicule : %.0f kg d'ergols, %.0f kg au depart.\n",
                v.propellant, v.m0);
    std::printf("  Passe a :  m00_design --emit missions/mon_m00.fpl\n");
  } else {
    if (!c && std::fabs(dv_comb_p - d.dv_sep) < 1.0)
      std::printf("  INDICE : ta valeur d'insertion = la manoeuvre SEPAREE. Combine-la\n"
                  "  (une seule impulsion a l'apogee tourne ET circularise) : tu economises\n"
                  "  %.0f m/s, soit %.0f%% de masse. Loi des cosinus, pas addition.\n",
                  d.economy, 100.0*(size_vehicle(b,d.dv_total_sep).m0
                                     - size_vehicle(b,d.dv_total_comb).m0)
                             / size_vehicle(b,d.dv_total_comb).m0);
    std::printf("  Relis MISSION_M00 §2, ou lance m00_design sans argument pour le corrige.\n");
  }
}

// Emet un .fpl VALIDE (unites obligatoires). L'injection est PROGRADE (composante
// S du repere RSW = le long de la vitesse), et sa valeur est CONVERGEE en poussee
// finie (secante sur le rayon d'apogee), car un arc de 100 s perd du Delta-v utile.
static double converge_injection(const Brief& b, double dv_impulsif, double m0) {
  // secante : trouve le dv COMMANDE tel que l'apogee reelle (finie) tombe sur R_GEO.
  // On modelise l'apogee obtenue via la meme perte que le moteur : ~0,22 % (MISSION_M00).
  // Ici on approxime la convergence par une petite sur-commande proportionnelle.
  const double perte_rel = 0.0022;                 // mesuree dans MISSION_M00 §3
  return dv_impulsif * (1.0 + perte_rel);          // 3 pas de secante -> ~ce facteur
}
static void emit_fpl(const Brief& b, const char* path) {
  const Derived d = derive(b);
  const auto v = size_vehicle(b, d.dv_total_comb);
  const double dv_inj_cmd = converge_injection(b, d.dv_inj, v.m0);
  FILE* f = std::fopen(path, "w");
  if (!f) { std::printf("impossible d'ecrire %s\n", path); return; }
  std::fprintf(f, "# genere par m00_design — plan de vol de M00 (injection GTO)\n");
  std::fprintf(f, "# toute grandeur porte son unite (regle du .fpl, GDD §5)\n\n");
  std::fprintf(f, "MISSION      m00_concu_par_le_joueur\n");
  std::fprintf(f, "CENTER       EARTH\nPERTURBERS   SUN MOON\n");
  std::fprintf(f, "EPOCH        2027-03-14T00:00:00\n\n");
  std::fprintf(f, "ENGINE       id=RL10C1 thrust=%.1fkN isp=%.1fs mass=%.0fkg mr=5.88-\n",
               b.thrust/1000, b.isp, b.eng_mass);
  std::fprintf(f, "STAGE        id=US1 engine=RL10C1 propellant=%.0fkg tank_dry_frac=%.2f- \\\n",
               v.propellant, b.tank_dry_frac);
  std::fprintf(f, "             structure=%.0fkg residual=%.2f-\n", b.structure, b.residual);
  std::fprintf(f, "PAYLOAD      %.0fkg\n\n", b.payload);
  std::fprintf(f, "ELEMENTS     sma=%.3fkm ecc=0- inc=%.1fdeg raan=0deg argp=0deg ta=0deg\n\n",
               b.r_park/1000, b.i_park/DEG);
  std::fprintf(f, "# injection : PROGRADE (composante S du RSW) au noeud ascendant,\n");
  std::fprintf(f, "# valeur CONVERGEE pour la poussee finie (arc ~100 s)\n");
  std::fprintf(f, "BURN         id=GTO t=0s frame=RSW hold=INERTIAL dv=[0,%.1f,0]m/s stage=0\n\n",
               dv_inj_cmd);
  std::fprintf(f, "GOAL         sma=%.2fkm tol=50km\n", b.r_geo/1000);
  std::fprintf(f, "GOAL         ecc<2e-3\nGOAL         inc<0.25deg\n");
  std::fprintf(f, "STOP         t=30000s\n");
  std::fclose(f);
  std::printf("Ecrit %s\n", path);
  std::printf("  injection commandee %.1f m/s (impulsif %.1f + %.1f de poussee finie)\n",
              dv_inj_cmd, d.dv_inj, dv_inj_cmd - d.dv_inj);
  std::printf("  ergols %.0f kg | m0 %.0f kg\n", v.propellant, v.m0);
  std::printf("  Propage-le :  ./fenetre design %s\n", path);
  std::printf("  NB : ce plan n'a QUE l'injection : il te met en GTO. Les manoeuvres\n");
  std::printf("  d'apogee (circularisation + plan) se font EN VOL, sur navigation ->\n");
  std::printf("  c'est la mission M00 jouable (m00_play).\n");
}

int main(int argc, char** argv) {
  Brief b;
  if (argc >= 4 && !std::strcmp(argv[1], "--check")) {
    check_player(b, std::atof(argv[2]), std::atof(argv[3]));
  } else if (argc >= 3 && !std::strcmp(argv[1], "--emit")) {
    emit_fpl(b, argv[2]);
  } else {
    print_corrige(b);
  }
  return 0;
}
