// tests/test_ares_modules.cpp — ORACLES de la couche ARES (GDD v1.1).
// Meme philosophie que test_astro_core.cpp : invariants et valeurs de reference,
// jamais d'assertion inventee. Chaque cas dit pourquoi il ne passe pas par accident.
//
// STANDALONE UNIQUEMENT : compile avec /DSP_STANDALONE_TESTS (hors UE, l'UBT
// compile tous les .cpp du module — sans la macro, ce TU est vide).
#ifdef SP_STANDALONE_TESTS

#include <cstdio>
#include <cstdint>
#include <cmath>
#include <string>
#include <vector>

#include "app/ares.hpp"
#include "fen/env/Radiation.hpp"
#include "fen/env/SpaceWeather.hpp"
#include "fen/env/Thermal.hpp"
#include "fen/game/GameState.hpp"
#include "fen/mission/Crew.hpp"
#include "fen/mission/Events.hpp"
#include "fen/rel/Relativity.hpp"

using namespace fen;

static int g_fail = 0, g_pass = 0;

#define CHECK(cond, msg)                                                        \
  do {                                                                          \
    if (cond) { ++g_pass; }                                                     \
    else { ++g_fail; std::printf("  [FAIL] %s  (%s:%d)\n", msg, __FILE__, __LINE__); } \
  } while (0)

#define CHECK_NEAR(a, b, tol, msg)                                              \
  do {                                                                          \
    const double _d = std::fabs((a) - (b));                                     \
    if (_d <= (tol)) { ++g_pass; }                                              \
    else { ++g_fail; std::printf("  [FAIL] %s : %.12g vs %.12g (ecart %.3g > %.3g)\n", \
                                 msg, (double)(a), (double)(b), _d, (double)(tol)); }   \
  } while (0)

static void section(const char* s) { std::printf("\n== %s ==\n", s); }

// ---------------------------------------------------------------------------
static void test_relativite() {
  section("Relativite [GDD 6.7] — formes closes");
  // gamma(0.5c) : valeur tabulee, pas de degre de liberte.
  CHECK_NEAR(rel::lorentz_gamma(0.5), 1.0 / std::sqrt(0.75), 1e-15, "gamma(0.5)");
  // petits beta : gamma_minus_one doit rendre beta^2/2 sans cancellation.
  CHECK_NEAR(rel::gamma_minus_one(1e-4), 0.5e-8, 1e-12, "gamma-1 a beta=1e-4");
  // vitesse constante : tau = T/gamma EXACTEMENT (le trapeze est exact ici).
  std::vector<rel::VelocitySample> prof = {{0.0, 0.5 * cst::C_LIGHT},
                                           {1000.0, 0.5 * cst::C_LIGHT}};
  CHECK_NEAR(rel::proper_time(prof), 1000.0 * std::sqrt(0.75), 1e-9, "tau = T/gamma");
  // fusee relativiste : aller-retour exact par la rapidite.
  const double R = rel::mass_ratio(0.3, rel::VE_ANTIMATTER_EFF);
  CHECK_NEAR(rel::beta_from_mass_ratio(R, rel::VE_ANTIMATTER_EFF), 0.3, 1e-14,
             "m0/mf <-> beta round-trip");
  // limite photon ve=c : forme close sqrt((1+b)/(1-b)).
  CHECK_NEAR(rel::mass_ratio(0.6, cst::C_LIGHT), std::sqrt(4.0), 1e-12,
             "limite photon beta=0.6");
  CHECK(!rel::is_relativistic(3.0e4), "30 km/s (chimique) : PAS relativiste");
}

// ---------------------------------------------------------------------------
// LES DEUX HORLOGES [GDD 6.7, 14.4] — CONTRE LA RÉALITÉ MESURÉE, PAS CONTRE
// SOI-MÊME. Un modèle d'horloge relativiste a ceci d'exceptionnel qu'il existe
// des valeurs PUBLIÉES auxquelles le confronter : la correction des horloges GPS
// est connue à mieux que 1 %, et c'est elle qui décide qu'un récepteur tombe à
// 11 km d'erreur par jour si on l'ignore. On vérifie donc le modèle sur des
// nombres que personne ici n'a choisis.
static void test_horloges() {
  section("Horloges [GDD 6.7, 14.4] — vs valeurs publiees");
  const double J = 86400.0;
  const double us_j = 1.0e6 * J;   // (ratio-1) -> microsecondes par jour

  // ═══ LES DEUX IDENTITÉS KÉPLÉRIENNES EXACTES ═══
  // v_rms = sqrt(mu/a) doit redonner la vitesse orbitale MOYENNE de la Terre,
  // 29 784,8 m/s — valeur d'almanach, aucun degre de liberte.
  CHECK_NEAR(rel::v_rms_kepler(cst::AU, cst::MU_SUN), 29784.8, 1.0,
             "6.7 : v_rms Terre = 29 784,8 m/s (almanach)");
  // Le COROLLAIRE : |<phi>|/c2 vaut EXACTEMENT le double de <v2>/(2c2). Ce n'est
  // pas une coincidence numerique mais l'identite <1/r> = 1/a ; si elle casse,
  // c'est que les moyennes ont ete remplacees par une approximation.
  const double c2 = cst::C_LIGHT * cst::C_LIGHT;
  const double phi = -rel::phi_moyen_kepler(cst::AU, cst::MU_SUN) / c2;
  const double cin = rel::v_rms_kepler(cst::AU, cst::MU_SUN) *
                     rel::v_rms_kepler(cst::AU, cst::MU_SUN) / (2.0 * c2);
  CHECK_NEAR(phi / cin, 2.0, 1e-12, "6.7 : |<phi>| = 2 x <v2>/2 (exact)");

  // ═══ GPS : +38,6 us/jour ═══ LA valeur de reference du domaine (45,9 de
  // potentiel moins 7,2 de vitesse). Si le modele ne la retrouve pas, il ne
  // modelise pas des horloges.
  const double gps = (rel::rapport_horloge_orbite_terrestre(26.560e6) - 1.0) * us_j;
  CHECK_NEAR(gps, 38.6, 0.2, "6.7 : GPS = +38,6 us/jour (valeur publiee)");
  // ═══ ISS : -25 us/jour ═══ En orbite BASSE le signe s'inverse : la vitesse
  // l'emporte sur l'altitude. Deux regimes, une seule formule.
  const double iss = (rel::rapport_horloge_orbite_terrestre(cst::R_EARTH + 400.0e3)
                      - 1.0) * us_j;
  CHECK_NEAR(iss, -24.6, 0.5, "6.7 : ISS = -24,6 us/jour (valeur publiee)");
  CHECK(iss < 0.0 && gps > 0.0,
        "6.7 : le signe s inverse entre orbite basse et orbite haute");
  // Le RAYON DE BASCULE est un fait de la formule : 3.mu/(2rc2) = W0/c2.
  const double r_neutre = 1.5 * cst::MU_EARTH / (c2 * rel::W0_SUR_C2);
  CHECK_NEAR(rel::rapport_horloge_orbite_terrestre(r_neutre), 1.0, 1e-15,
             "6.7 : il existe une altitude ou l orbite bat comme le sol");
  CHECK(r_neutre > 9.0e6 && r_neutre < 10.0e6, "6.7 : bascule vers 9 550 km");

  // ═══ CROISIÈRE HÉLIOCENTRIQUE : LE SIGNE EST UNE SURPRISE, ET IL EST JUSTE ═══
  // Sur un transfert vers Mars, le vaisseau est PLUS HAUT dans le potentiel
  // solaire ET PLUS LENT que la Terre : les deux termes vont dans le MEME sens,
  // et son horloge GAGNE. Le voyageur revient PLUS VIEUX que s'il etait reste —
  // l'inverse du cliche relativiste, qui ne vaut que si la vitesse domine.
  const double a_transfert = 0.5 * (cst::AU + 1.523679 * cst::AU);
  const double rc = rel::rapport_horloges_kepler(a_transfert, cst::AU, cst::MU_SUN);
  CHECK(rc > 1.0, "6.7 : en croisiere vers Mars, l horloge de bord GAGNE");
  // Ordre de grandeur : ~0,1 s par an. Mesure, pas suppose.
  CHECK_NEAR((rc - 1.0) * 365.25 * J, 0.097, 0.005,
             "6.7 : croisiere Mars = +0,097 s par an");
  // ET C'EST PRECISEMENT POURQUOI [GDD 6.7.2] DIT QUE C'EST IMPERCEPTIBLE : un
  // aller-retour complet reste sous la seconde. Le GDD l'affirmait ; le modele
  // le DEMONTRE, et c'est desormais un chiffre opposable.
  const double tof = cst::PI * std::sqrt(a_transfert * a_transfert * a_transfert
                                         / cst::MU_SUN);
  const double rs = rel::rapport_horloges_kepler(1.523679 * cst::AU, cst::AU,
                                                 cst::MU_SUN);
  const double ecart = (rc - 1.0) * 2.0 * tof + (rs - 1.0) * (779.9 * J - 2.0 * tof);
  CHECK(ecart > 0.2 && ecart < 0.3, "6.7 : aller-retour Mars = +0,25 s au total");
  CHECK(std::fabs(ecart) < 1.0,
        "6.7.2 : sous la seconde — imperceptible, comme le GDD l affirme");
  // Le transfert de Hohmann doit durer les 259 jours du manuel : c'est la
  // verification que `a_transfert` est bien le demi-grand axe qu'on croit.
  CHECK_NEAR(tof / J, 258.9, 1.0, "6.7 : Hohmann Terre-Mars = 259 jours");

  // ═══ ET LA OU CA COMPTE VRAIMENT ═══ Le regime relativiste, lui, n'a rien
  // d'imperceptible : le meme code, a beta=0,7 (seuil NARRATIF du GDD), rend
  // presque trois ans d'ecart sur dix. Une seule formule couvre les deux bouts.
  rel::DualClock dc;
  dc.advance(10.0 * 365.25 * J, rel::BETA_NARRATIVE * cst::C_LIGHT);
  CHECK(dc.aging_gap() / (365.25 * J) > 2.5,
        "6.7.2 : a beta=0,7 l ecart depasse 2,5 ans sur 10 — la, on le voit");
  CHECK(dc.diverged(), "6.7 : au-dela de la seconde, l ecart est affichable");
  // Le rapport, lui, est neutre par construction quand on ne bouge pas.
  rel::DualClock sol;
  sol.advance_ratio(1000.0, 1.0);
  CHECK_NEAR(sol.aging_gap(), 0.0, 1e-12, "6.7 : au sol, aucune divergence");
}

// ---------------------------------------------------------------------------
// L'ANTIMATIÈRE [GDD 5.12.12, 19.3] — LE VERROU DE LA FIN DE JEU, ENFIN EXÉCUTÉ.
// Quatre paramètres décrivaient un processus que rien ne faisait tourner : pas
// un gramme n'existait nulle part. Ces oracles vérifient le PROCESSUS (un stock
// qui fuit est un équilibre, pas un cumul) et ils CONSIGNENT le verdict que la
// mesure rend — verdict qui est une donnée de calibration [Annexe E], pas un bug.
static void test_antimatiere() {
  section("Antimatiere [GDD 5.12.12, 19.3] — production, fuite, verdict");
  using Prod = rel::AntimatterProduction;

  // ═══ LE PLANCHER DE PRODUCTION EST UNE LOI ═══ [GDD 19.6]
  // On ne fait pas un antiproton sans son proton : E_min = 2mc^2 par gramme
  // d'ANTImatiere. Aucun rendement ne passe dessous, et le modele doit le tenir
  // meme si on lui demande l'impossible.
  CHECK_NEAR(rel::ANTIMATTER_PAIR_ENERGY_J_PER_G, 1.7975e14, 1e10,
             "19.6 : le plancher de production vaut 2mc^2 = 1,80e14 J/g");
  Prod parfait; parfait.production_efficiency = 1.0;
  CHECK_NEAR(parfait.energy_j_per_g(), rel::ANTIMATTER_PAIR_ENERGY_J_PER_G, 1e-3,
             "19.6 : a rendement 1, l energie par gramme EST le plancher");
  Prod triche; triche.production_efficiency = 1000.0;   // mouvement perpetuel
  CHECK(triche.energy_j_per_g() >= rel::ANTIMATTER_PAIR_ENERGY_J_PER_G - 1e-3,
        "19.6 : un rendement > 1 est borne, jamais accepte");
  // ET L'HYPOTHESE MUETTE QU'ON VIENT DE DECLARER : l'ancien defaut de 1e17 J/g
  // supposait un rendement de 1,8e-3, soit six ordres au-dessus du reel.
  CHECK_NEAR(rel::ANTIMATTER_PAIR_ENERGY_J_PER_G / 1.0e17, 1.8e-3, 1e-4,
             "12.5 : l ancien 1e17 J/g supposait un rendement de 1,8e-3");
  CHECK(rel::ANTIMATTER_EFFICIENCY_TODAY < 1.0e-3 / 1.0e5,
        "12.5 : ... cinq ordres au-dessus de ce que le CERN sait faire");

  // ═══ LE DEBIT EST DE LA PUISSANCE — ET IL VIENT DE LA BRANCHE 6 ═══
  // Le defaut corrige : la puissance etait la MARGE DE NOVELLUS, si bien qu'aucune
  // recherche de branche 6 ne pouvait deplacer le debit. Elle appartient
  // desormais a l'usine, donc au palier.
  const rel::AntimatterTier paliers[] = {rel::AntimatterTier::None,
                                         rel::AntimatterTier::Fission,
                                         rel::AntimatterTier::Fusion,
                                         rel::AntimatterTier::Mature};
  double debit_prec = -1.0;
  for (rel::AntimatterTier t : paliers) {
    const Prod p = Prod::for_tier(t);
    CHECK(p.rate_g_yr() > debit_prec, "5.12.12 : chaque palier de branche 6 augmente le debit");
    debit_prec = p.rate_g_yr();
  }
  CHECK(Prod::for_tier(rel::AntimatterTier::None).rate_g_yr() == 0.0,
        "5.12.12 : sans usine, aucune production");
  // Linearite en puissance : doubler la centrale double le debit.
  Prod pm = Prod::for_tier(rel::AntimatterTier::Mature);
  CHECK_NEAR(pm.rate_from_power_g_yr(2.0 * pm.plant_power_w), 2.0 * pm.rate_g_yr(), 1e-6,
             "5.12.12 : le debit est lineaire en puissance d usine");

  // ═══ UN STOCK QUI FUIT EST UN EQUILIBRE, PAS UN CUMUL ═══
  rel::AntimatterStock S; S.prod = pm;
  const double s_inf = S.equilibrium_g();
  CHECK_NEAR(s_inf, (pm.rate_g_yr() / 365.25) / pm.loss_rate_per_day, 1e-6,
             "5.12.12 : l equilibre vaut m_point / lambda");
  CHECK(S.borne_par_la_fuite(),
        "5.12.12 : c est la FUITE qui borne, pas le confinement");
  rel::AntimatterStock L = S;
  L.tick(1.0e8, true);
  CHECK_NEAR(L.grams, s_inf, 1e-6 * s_inf, "5.12.12 : le stock CONVERGE vers l equilibre");
  CHECK(L.grams <= pm.confinement_capacity_g + 1e-12,
        "5.12.12 : le confinement reste un plafond dur");

  // ═══ L'INTEGRATION EST EXACTE, DONC LE DECOUPAGE N'A AUCUN EFFET ═══
  rel::AntimatterStock A, B; A.prod = pm; B.prod = pm;
  A.tick(3650.0, true);
  for (int i = 0; i < 365; ++i) B.tick(10.0, true);
  CHECK(std::fabs(A.grams - B.grams) < 1e-12 * A.grams,
        "14.2 : 1 bloc de 3650 j == 365 tranches de 10 j (integration exacte)");

  // ═══ LA FUITE NE S'ARRETE PAS QUAND LA FILIERE N'EST PAS QUALIFIEE ═══
  rel::AntimatterStock F; F.prod = pm; F.grams = 1.0e-3;
  F.tick(365.0, /*qualifiee*/ false);
  CHECK(F.grams < 1.0e-3, "5.12.12 : un stock mal confine se perd, qualifie ou non");
  CHECK(F.grams > 0.0, "5.12.12 : ... exponentiellement, jamais d un coup");

  // ═══════════════════════════════════════════════════════════════════════
  // L'INVERSION DU GDD — LES TROIS ENONCES QUI CALIBRENT [Annexe E]
  // ═══════════════════════════════════════════════════════════════════════
  // (i) [5.12.11] la FUSION doit rester pre-relativiste : « de tres grandes
  //     vitesses SANS ENCORE rendre la dilatation significative ».
  rel::AntimatterStock Sf; Sf.prod = Prod::for_tier(rel::AntimatterTier::Fusion);
  const double b_fusion = rel::beta_from_antimatter(Prod::CALIB_DRY_MASS_KG,
                                                    Sf.equilibrium_g());
  CHECK(b_fusion < rel::BETA_THRESHOLD,
        "5.12.11 : au palier FUSION, beta reste sous le seuil de mesurabilite");

  // (ii) [6.7.2] « seule l'ANTIMATIERE franchit beta >= 0,3 » — donc au palier
  //      abouti, la cible doit etre ATTEIGNABLE, et c'est ce qui tranche la
  //      question laissee ouverte : un confinement de 1 g n'etait pas un plafond
  //      [5.12.12] mais un mur.
  //      ET LA CIBLE EST UN VOL HABITE (decision de l'utilisateur, 2026-07-29) :
  //      « le relativisme n'a d'interet que pour les vols habites ». Six
  //      personnes, ALLER-RETOUR, donc quatre poussees [6.7.4].
  const double cible = rel::antimatter_needed_g(Prod::CALIB_DRY_MASS_KG,
                                                Prod::CALIB_TARGET_BETA,
                                                Prod::CALIB_BURNS);
  CHECK(cible < pm.confinement_capacity_g,
        "5.12.12 : le confinement PLAFONNE le stock utile, il ne l interdit pas");
  CHECK(!S.hors_atteinte(cible),
        "6.7.2 : au palier abouti, beta = 0,3 est ATTEIGNABLE");
  const double ans = S.years_to_reach(cible);

  // (iii) [3.4, 3.5] « atteindre la fin de la branche 6 demande souvent PLUSIEURS
  //       VIES », et la mort naturelle vient vers 85 ans : l'accumulation doit se
  //       compter en vies, ni en decennie ni en millenaire.
  CHECK(ans > 60.0, "3.5 : l accumulation depasse une carriere — plusieurs vies");
  CHECK(ans < 400.0, "5.12.12 : ... et reste une DUREE, pas une impossibilite");
  std::printf("     antimatiere : palier abouti -> %.3e g/an, equilibre %.3e g ; "
              "beta=0,3 ALLER-RETOUR habite (%.0f t seches) en demande %.3e g -> %.0f ans\n",
              pm.rate_g_yr(), s_inf, Prod::CALIB_DRY_MASS_KG / 1000.0, cible, ans);
  // La coherence de cette date : on avance de ce temps-la, on doit y etre.
  rel::AntimatterStock D; D.prod = pm;
  D.tick(ans * 365.25, true);
  CHECK_NEAR(D.grams, cible, 1e-6 * cible,
             "5.12.12 : la date annoncee est celle ou le stock y arrive vraiment");
  CHECK(rel::beta_from_antimatter(Prod::CALIB_DRY_MASS_KG, D.grams)
            >= Prod::CALIB_TARGET_BETA - 1e-9,
        "6.7.2 : ... et le stock accumule ACHETE bien le beta vise");

  // ═══════════════════════════════════════════════════════════════════════
  // LES DEUX FONCTIONS SONT INVERSES L'UNE DE L'AUTRE — POUR TOUT n
  // ═══════════════════════════════════════════════════════════════════════
  // `antimatter_needed_g` connaissait le nombre de poussees ; `beta_from_antimatter`
  // NON, et rendait toujours le beta d'un aller simple. Une asymetrie entre deux
  // inverses dans le meme fichier — donc tout lecteur de beta le lisait surestime,
  // et d'autant plus que l'architecture devait freiner ou revenir [GDD 6.7.4].
  // L'oracle est l'aller-retour exact : partir d'un beta, en deduire la masse,
  // puis la relire, doit redonner le beta de depart.
  for (int n : {1, 2, 4}) {
    for (double b : {0.05, 0.3, 0.7}) {
      const double g = rel::antimatter_needed_g(5000.0, b, n);
      CHECK_NEAR(rel::beta_from_antimatter(5000.0, g, n), b, 1e-12,
                 "6.7.4 : masse -> beta -> masse est exact pour tout nombre de poussees");
    }
  }
  // ET LE VERROU MORD DANS LE BON SENS : a stock EGAL, plus de poussees = moins
  // de vitesse. C'est le ratio a la puissance n, lu a l'envers.
  double b_prec = 1.0;
  for (int n : {1, 2, 4}) {
    const double bn = rel::beta_from_antimatter(5000.0, s_inf, n);
    CHECK(bn < b_prec, "6.7.4 : a stock egal, chaque poussee supplementaire coute de la vitesse");
    b_prec = bn;
  }
  std::printf("     antimatiere : a stock egal sur 5 t — survol %.3f, aller simple %.3f, "
              "aller-retour %.3f\n",
              rel::beta_from_antimatter(5000.0, s_inf, 1),
              rel::beta_from_antimatter(5000.0, s_inf, 2),
              rel::beta_from_antimatter(5000.0, s_inf, 4));
  // LE NOMBRE DE POUSSEES SE LIT SUR L'ARCHITECTURE, il ne se choisit pas : un
  // equipage revient, une sonde qui se pose freine, un survol ne freine pas.
  CHECK(rel::burns_for_architecture(/*crewed*/ true) == rel::BURNS_ROUND_TRIP,
        "6.7.4 : un vol habite est un ALLER-RETOUR — quatre poussees");
  CHECK(rel::burns_for_architecture(false, true) == rel::BURNS_ONE_WAY,
        "6.7.4 : une sonde qui s arrete en demande deux");
  CHECK(rel::burns_for_architecture(false, false) == rel::BURNS_FLYBY,
        "6.7.4 : un survol n en demande qu une");

  // ═══════════════════════════════════════════════════════════════════════
  // LA MISSION RELATIVISTE A UNE DESTINATION [GDD 3.4, 9.3]
  // ═══════════════════════════════════════════════════════════════════════
  // La distance est un FAIT MESURE (Proxima, parallaxe Gaia DR3), donc elle se
  // verifie contre sa source et non contre elle-meme.
  CHECK_NEAR(rel::PROXIMA_DISTANCE_M / rel::LIGHT_YEAR_M, 4.2465, 1e-4,
             "3.4 : Proxima est a 4,2465 al (Gaia DR3)");
  CHECK_NEAR(rel::LIGHT_YEAR_M, 299792458.0 * 365.25 * 86400.0, 1.0,
             "l annee-lumiere est c fois l annee julienne, exactement");
  // La lumiere met 4,2465 ANS a la parcourir : c'est la definition, et c'est
  // l'oracle qui attrape une erreur d'unite.
  CHECK_NEAR(rel::relativistic_transit(rel::PROXIMA_DISTANCE_M, 1.0 - 1e-12).t_earth_s
                 / (365.25 * 86400.0),
             4.2465, 1e-3, "6.7 : a beta -> 1, le transit tend vers 4,2465 ans");

  // ET LES DEUX HORLOGES DIVERGENT, sur le vrai integrateur [GDD 6.7.1].
  // `rel::proper_time` etait le DERNIER modele sans consommateur : il en a un.
  {
    const rel::RelativisticTransit tr =
        rel::relativistic_transit(rel::PROXIMA_DISTANCE_M, 0.9);
    const double an = 365.25 * 86400.0;
    // tau = t/gamma, mais CALCULE par l'integrale et non par la division : les
    // deux doivent coincider a profil constant, ce qui valide l'integrateur.
    CHECK_NEAR(tr.tau_ship_s, tr.t_earth_s / rel::lorentz_gamma(0.9), 1e-6,
               "6.7.1 : tau integre == t/gamma a beta constant");
    std::printf("     relativiste : Proxima a beta=0,9 -> %.2f ans Terre, %.2f ans a bord, "
                "ecart %.2f ans\n", tr.t_earth_s / an, tr.tau_ship_s / an,
                tr.age_gap_s() / an);
    CHECK(tr.t_earth_s / an > 4.2465, "6.7 : ... et le vol dure PLUS que la lumiere");
    // ═══ ET C'EST LE CHIFFRE DU GDD, MAIS SUR L'ALLER-RETOUR ═══
    // [GDD 3.4] « beta ~ 0,9 -> ~5 ans d'ecart ». L'aller seul en donne 2,66 :
    // c'est l'ALLER-RETOUR qui rend 5,32. La MESURE a donc tranche deux choses
    // d'un coup, et aucune n'etait supposee au depart :
    //   . la cible est bien l'etoile la plus proche — a 2 al ou a 8, le chiffre
    //     du GDD ne tomberait pas ;
    //   . et le voyage dont il parle est un ALLER-RETOUR, ce que [GDD 6.7.4] dit
    //     par ailleurs en comptant QUATRE poussees.
    // Le GDD ne nommait pas sa destination ; ses propres nombres la designaient.
    const double ar_ans = 2.0 * tr.age_gap_s() / an;
    std::printf("     relativiste : aller-retour a beta=0,9 -> ecart d age %.2f ans "
                "(le GDD 3.4 dit ~5)\n", ar_ans);
    CHECK(ar_ans > 4.5 && ar_ans < 6.0,
          "3.4 : l ALLER-RETOUR a beta = 0,9 donne bien ~5 ans d ecart d age");
  }
  // Sous le seuil, les deux horloges se rejoignent — et le vol devient absurde de
  // longueur, ce qui est le vrai verrou d'un voyage interstellaire.
  {
    const rel::RelativisticTransit lent =
        rel::relativistic_transit(rel::PROXIMA_DISTANCE_M, 0.01);
    const double an = 365.25 * 86400.0;
    CHECK(lent.t_earth_s / an > 400.0,
          "6.7.2 : a beta = 0,01 le voyage dure des siecles");
    CHECK(lent.age_gap_s() / an < 0.03,
          "6.7.2 : ... et l ecart d age y est negligeable");
  }
  // Le transit est refuse pour ce qui n'a pas de sens (beta nul, superluminique).
  CHECK(rel::relativistic_transit(rel::PROXIMA_DISTANCE_M, 0.0).t_earth_s == 0.0,
        "sans vitesse, aucun transit");
  CHECK(rel::relativistic_transit(rel::PROXIMA_DISTANCE_M, 1.0).t_earth_s == 0.0,
        "19.1 : beta = 1 est refuse, jamais approxime");

  // ═══════════════════════════════════════════════════════════════════════
  // LE VOL HABITE RELATIVISTE EST UN POINT FIXE QUI DIVERGE [GDD 19.1]
  // ═══════════════════════════════════════════════════════════════════════
  // « Le vol habite lointain depend AUTANT du support-vie, de la medecine et des
  // radiations que du moteur. » Le modele le DEMONTRE : alourdir le vaisseau le
  // ralentit, ce qui allonge le voyage, ce qui demande plus de vivres. La boucle
  // se referme sur elle-meme et, vers l'etoile la plus proche, elle DIVERGE.
  {
    const auto loops = mission::RecyclingLoops::advanced();   // fin d'arbre
    // ═══ LE VOL HABITE EXISTE — ET L'ANCRE DE CALIBRATION EST CE POINT FIXE ═══
    // « Le relativisme n'a d'interet que pour les vols habites » : la calibration
    // vise donc SIX PERSONNES en aller-retour. `CALIB_DRY_MASS_KG` est la masse
    // que le point fixe rend pour cette architecture ; elle est ecrite dans
    // `astro_core` (qui ne peut pas appeler `mission/`) et cet oracle interdit
    // aux deux de diverger en silence.
    const double coque = mission::masse_habitat_kg(6, 25.0);
    const double blind = mission::masse_blindage_kg(6, 20.0, 25.0);
    const mission::BilanRelativiste ref =
        mission::bilan_relativiste(coque + blind + 2000.0, s_inf, 6, loops);
    CHECK(ref.converge, "3.4 : le vol HABITE relativiste converge — il peut partir");
    CHECK_NEAR(ref.masse_seche_kg, rel::AntimatterProduction::CALIB_DRY_MASS_KG,
               0.05 * rel::AntimatterProduction::CALIB_DRY_MASS_KG,
               "6.8 : l ancre de calibration EST le point fixe mesure (a 5 %)");
    CHECK(ref.beta >= rel::AntimatterProduction::CALIB_TARGET_BETA,
          "6.7.2 : ... et l architecture habitee atteint bien beta = 0,3");
    {
      const double an = 365.25, g = rel::lorentz_gamma(ref.beta);
      const double ar = ref.duree_jours / an;
      std::printf("     relativiste HABITE : beta %.3f | %.0f t | AR %.1f ans Terre, "
                  "%.1f vecus | ecart %.2f an | age 32 -> %.0f\n",
                  ref.beta, ref.masse_seche_kg / 1000.0, ar, ar / g, ar - ar / g,
                  32.0 + ar / g);
      // [GDD 3.4] « beta ~ 0,25 -> ~1 an d'ecart sur une decennie (invisible) ».
      // C'est LE point de donnee du GDD pour un vol habite, et il tombe.
      CHECK(ar - ar / g > 0.5 && ar - ar / g < 3.0,
            "3.4 : l ecart d age d un aller-retour habite est de l ordre de l annee");
      // ET IL DOIT TENIR DANS UNE VIE [GDD 3.4] : mort naturelle vers 85 ans.
      CHECK(32.0 + ar / g < 85.0,
            "3.4 : ... et l architecte RENTRE VIVANT — le vol tient dans une carriere");
    }

    // SOUS LE SEUIL, LE MEME VOL N'EXISTE PAS. Une coque de 20 t avec un stock
    // cent fois trop faible : le point fixe DIVERGE, et le refus nomme la cause.
    const mission::BilanRelativiste hab =
        mission::bilan_relativiste(2.0e4, s_inf / 1.0e3, 6, loops);
    CHECK(!hab.converge, "19.1 : sous le seuil, le bilan de masse DIVERGE");
    CHECK(std::string(hab.pourquoi).find("DIVERGE") != std::string::npos,
          "19.1 : ... et le refus nomme la CAUSE, pas le symptome de masse");
    std::printf("     relativiste habite : %s (en %d iterations)\n",
                hab.pourquoi, hab.iterations);
    // ET LE VERDICT EST BIEN CELUI DU SUPPORT-VIE, pas du moteur : la MEME
    // structure, SANS equipage a nourrir, converge. C'est ce qui prouve que la
    // divergence vient des vivres et non de la masse.
    const mission::BilanRelativiste sonde =
        mission::bilan_relativiste(2.0e4, s_inf, /*n_crew*/ 0, loops);
    CHECK(!sonde.converge && std::string(sonde.pourquoi).find("DIVERGE") == std::string::npos,
          "19.1 : sans equipage, il n y a pas de boucle a fermer — donc pas de divergence");
    // Un equipage minuscule sur une structure enorme ne diverge pas : le critere
    // est le RAPPORT, pas une famille de mission.
    const mission::BilanRelativiste petit =
        mission::bilan_relativiste(1.0e6, s_inf, 1, loops);
    std::printf("     relativiste habite : 1 personne sur 1000 t -> converge=%d\n",
                (int)petit.converge);
    // Les entrees absurdes sont refusees, jamais approximees.
    CHECK(!mission::bilan_relativiste(0.0, s_inf, 6, loops).converge,
          "une structure de masse nulle est refusee");
    CHECK(!mission::bilan_relativiste(2.0e4, 0.0, 6, loops).converge,
          "sans antimatiere, aucun bilan relativiste");

    // ═══ ET LE REFUS REMONTE JUSQU AU JOUEUR, AVEC SA CAUSE ═══
    // Un modele sans consommateur est un defaut : `MissionPlan::evaluate`
    // court-circuite `assess_multistage` quand le point fixe diverge, sinon le
    // joueur lirait « AUCUN LANCEUR NE SOULEVE CETTE MASSE » — exact, et
    // inutilisable (piege n°42).
    {
      mission::Mission mr;
      mr.contract.id = "CAT-11";
      mr.contract.family = "relativiste";
      mr.contract.crewed = true;
      mr.contract.terms = mission::contract_terms_for_family("relativiste");
      mission::MissionPlan pl;
      pl.crew_loops = loops;
      pl.crew_round_trip_days = 3.0e5;      // le vol DURE, il ne sejourne pas
      // Stock SOUS le seuil d existence (3,0e8 g) : c'est la que la boucle
      // diverge, et c'est ce refus-la qu'on veut voir remonter au joueur.
      pl.antimatiere_g = s_inf / 1.0e3;
      pl.evaluate(mr);
      CHECK(!pl.assessment.ok, "19.1 : le plan d un vol habite relativiste est REFUSE");
      CHECK(pl.assessment.why.find("DIVERGE") != std::string::npos,
            "19.1 : ... et le poste affiche la CAUSE, pas « aucun lanceur ne souleve »");
      std::printf("     relativiste habite : le plan refuse — \"%s\"\n",
                  pl.assessment.why.c_str());
      // SANS antimatiere, le meme plan retombe sur l evaluation ORDINAIRE : le
      // court-circuit ne doit mordre QUE la ou la boucle existe.
      mission::MissionPlan pl0 = pl;
      pl0.antimatiere_g = 0.0;
      pl0.evaluate(mr);
      CHECK(pl0.assessment.why.find("DIVERGE") == std::string::npos,
            "19.1 : sans antimatiere embarquee, le court-circuit ne s applique pas");
    }
  }

  // ═══ CE QUI RESTE HORS D'ATTEINTE, ET C'EST LE GDD QUI LE VEUT ═══
  // [6.7.4] le verrou de l'aller-retour : quatre poussees, donc le ratio a la
  // puissance quatre. Meme au palier abouti, l'aller-retour a 0,3 est un mur.
  // Le SURCOUT de l'aller-retour, a architecture EGALE : c'est le ratio a la
  // puissance quatre contre la puissance un.
  const double ar_vs_simple =
      rel::antimatter_needed_g(Prod::CALIB_DRY_MASS_KG, Prod::CALIB_TARGET_BETA, 4)
    / rel::antimatter_needed_g(Prod::CALIB_DRY_MASS_KG, Prod::CALIB_TARGET_BETA, 1);
  CHECK(ar_vs_simple > 20.0,
        "6.7.4 : l aller-retour coute plus de vingt fois l aller simple");
  // ET LE VERROU DE [6.7.4] TIENT LA OU IL COMPTE : beta = 0,9 en ALLER-RETOUR
  // HABITE reste hors d atteinte de cinq ordres — le ratio^4 ne se rattrape pas.
  CHECK(S.hors_atteinte(rel::antimatter_needed_g(Prod::CALIB_DRY_MASS_KG, 0.9,
                                                 Prod::CALIB_BURNS)),
        "6.7.4 : beta = 0,9 en aller-retour HABITE reste hors d atteinte");
  // [6.7.2] « seule une antimatiere TRES ABOUTIE approche 0,9 ». Elle l'est
  // desormais — mais seulement pour une SONDE DEPOUILLEE, ce qui est exactement
  // la decision 10 (« beta decoule de l ARCHITECTURE »). Le meme stock donne 0,9
  // a 5 t depouillees et 0,37 a six personnes en aller-retour.
  const double cible_09_sonde = rel::antimatter_needed_g(5000.0, 0.9, 1);
  CHECK(!S.hors_atteinte(cible_09_sonde),
        "6.7.2 : ... et une antimatiere aboutie APPROCHE 0,9 sur une sonde de 5 t");
  std::printf("     antimatiere : beta=0,9 -> %.3e g sur une sonde de 5 t (atteignable), "
              "%.3e g en aller-retour habite (hors d atteinte)\n",
              cible_09_sonde,
              rel::antimatter_needed_g(Prod::CALIB_DRY_MASS_KG, 0.9, Prod::CALIB_BURNS));
}

// ---------------------------------------------------------------------------
// L'ASSEMBLAGE EN ORBITE [GDD 5.2 branche 1] — L'ARBITRAGE, PAS LE CONTOURNEMENT.
// Le GDD nommait « transfert de propergol orbital, rendez-vous automatisé
// robuste » depuis toujours, l'arbre portait les nœuds, et rien ne les
// consommait. Ces oracles verifient que la masse s'ACHETE — en prix, en risque
// et en temps — et jamais qu'elle se contourne.
static void test_assemblage() {
  section("Assemblage orbital [GDD 5.2 br.1] — la masse s achete");
  using namespace fen::mission;
  CapaciteAssemblage plein; plein.rdv_automatise = true;
  CapaciteAssemblage aucune;

  // ═══ LA FORME CLOSE DE L EBULLITION EST EXACTE ═══
  // Les ergols arrivent PROGRESSIVEMENT : la charge i attend (n-i).dt, et la
  // moyenne des survies est une serie geometrique. On la verifie contre la somme
  // brute, terme a terme — si la forme close derive, elle derive ici.
  const double lam = EBULLITION_PAR_JOUR, dt = INTERVALLE_TIRS_JOURS;
  for (int n : {1, 2, 5, 9, 20}) {
    double somme = 0.0;
    for (int k = 0; k < n; ++k) somme += std::exp(-lam * k * dt);
    CHECK_NEAR(fraction_ergols_survivante(n, lam, dt), somme / n, 1e-12,
               "5.2 : la forme close egale la somme des sejours, terme a terme");
  }
  CHECK_NEAR(fraction_ergols_survivante(1, lam, dt), 1.0, 1e-15,
             "5.2 : un lancement unique n attend pas — aucune ebullition");
  CHECK(fraction_ergols_survivante(9, lam, dt) < fraction_ergols_survivante(2, lam, dt),
        "5.2 : plus la campagne est longue, plus il s en evapore");
  // Stabilite numerique quand lambda.dt -> 0 (expm1, pas de 0/0).
  CHECK_NEAR(fraction_ergols_survivante(10, 1e-14, dt), 1.0, 1e-9,
             "5.2 : a taux nul la forme close rend 1, sans division par zero");

  // ═══ SANS RENDEZ-VOUS AUTOMATISE, PAS D ASSEMBLAGE ═══ [GDD 5.4]
  // On ne rejoint pas un element en orbite par chance. Et le refus NOMME le
  // noeud a rechercher (piege n°42).
  const PlanAssemblage nu = planifier_assemblage(30000.0, 60000.0, 22800.0, 0.98, aucune);
  CHECK(!nu.possible, "5.4 : sans rdv_automatise, aucun assemblage");
  CHECK(nu.why.find("rdv_automatise") != std::string::npos,
        "5.4 : ... et le refus NOMME le noeud a rechercher");
  // Ce qui tient en UN tir ne demande rien : le comportement d avant, intact.
  const PlanAssemblage un = planifier_assemblage(5000.0, 10000.0, 22800.0, 0.98, aucune);
  CHECK(un.possible && un.n_lancements == 1 && un.duree_jours == 0.0,
        "5.2 : un vol qui tient d un coup n a ni campagne ni attente");
  CHECK_NEAR(un.p_segment, 0.98, 1e-15, "5.2 : ... et sa fiabilite est celle du tir");
  CHECK(un.ergols_evapores_kg == 0.0, "5.2 : ... et rien ne s evapore");

  // ═══ LA MASSE S ACHETE EN RISQUE, ET C EST EXPONENTIEL ═══
  const PlanAssemblage gros = planifier_assemblage(40000.0, 140000.0, 22800.0, 0.98, plein);
  CHECK(gros.possible && gros.n_lancements > 1, "5.2 : la campagne existe");
  std::printf("     assemblage : %d tirs, %.0f j, P=%.3f, %.1f t evaporees\n",
              gros.n_lancements, gros.duree_jours, gros.p_segment,
              gros.ergols_evapores_kg / 1000.0);
  CHECK_NEAR(gros.p_segment,
             std::pow(0.98, gros.n_lancements) * std::pow(P_AMARRAGE, gros.n_lancements - 1),
             1e-12, "5.2 : P = R_lanceur^N . R_amarrage^(N-1), exactement");
  CHECK(gros.p_segment < 0.98,
        "5.2 : une campagne est MOINS sure qu un tir unique — jamais l inverse");
  CHECK_NEAR(gros.duree_jours, (gros.n_lancements - 1) * INTERVALLE_TIRS_JOURS, 1e-12,
             "5.2 : la duree est la cadence du pas de tir, pas un forfait");
  CHECK(gros.ergols_evapores_kg > 0.0,
        "5.2 : les ergols cryogeniques s evaporent pendant l attente");
  CHECK(gros.masse_lancee_kg > 40000.0 + 140000.0,
        "5.2 : il faut donc en lancer PLUS que necessaire");

  // ═══ LA ROBOTIQUE ACHETE DU TEMPS ET DE LA FIABILITE, PAS DE LA MASSE ═══
  CapaciteAssemblage robot = plein; robot.robotique_orbitale = true;
  const PlanAssemblage gr = planifier_assemblage(40000.0, 140000.0, 22800.0, 0.98, robot);
  CHECK(gr.duree_jours < gros.duree_jours,
        "5.2 : la robotique d assemblage RACCOURCIT la campagne");
  CHECK(gr.p_segment > gros.p_segment, "5.2 : ... et fiabilise l amarrage");
  CHECK(gr.ergols_evapores_kg < gros.ergols_evapores_kg,
        "5.2 : ... donc il s en evapore moins, par consequence et non par regle");

  // ═══ LE TRANSFERT D ERGOLS CHANGE LA NATURE DU PROBLEME ═══ [GDD 5.2 br.1]
  // Sans lui les ergols attendent toute la campagne ; avec lui on assemble sec
  // et on ravitaille en dernier. C est ce que la technologie ACHETE.
  CapaciteAssemblage ergo = plein; ergo.transfert_ergols = true;
  const PlanAssemblage ge = planifier_assemblage(40000.0, 140000.0, 22800.0, 0.98, ergo);
  CHECK(ge.ergols_evapores_kg < gros.ergols_evapores_kg,
        "5.2 : le transfert d ergols orbital reduit l evaporation");
  CHECK(ge.masse_lancee_kg < gros.masse_lancee_kg,
        "5.2 : ... donc il y a moins a lancer, donc moins de tirs");

  // ═══ ET LE POINT FIXE PEUT DIVERGER — C EST UN VRAI MODE D ECHEC ═══
  // Une campagne trop longue evapore plus vite qu on ne remplit. Le refus
  // distingue « trop lourd » de « l ebullition gagne », parce que la seconde ne
  // se resout PAS en ajoutant des tirs.
  const PlanAssemblage divergent =
      planifier_assemblage(1.0e6, 5.0e6, 22800.0, 0.98, plein);
  CHECK(!divergent.possible, "5.2 : une campagne hors d echelle est refusee");
  CHECK(divergent.n_lancements <= MAX_LANCEMENTS + 1,
        "5.2 : ... et le point fixe est BORNE, il ne boucle pas indefiniment");
  std::printf("     assemblage : refus hors d echelle -> \"%s\"\n", divergent.why.c_str());
}

static void test_thermique() {
  section("Thermique [GDD 6.5] — Stefan-Boltzmann exact");
  const double A = env::radiator_area(1.0e6, 0.85, 500.0);
  CHECK_NEAR(env::radiated_power(0.85, A, 500.0), 1.0e6, 1e-3, "aire <-> puissance");
  // eta=30 % -> 2.333x la puissance electrique en chaleur. C'est une identite.
  CHECK_NEAR(env::reactor_waste_heat(3.0e5, 0.30), 7.0e5, 1e-6, "chaleur reacteur");
  // temperature d'equilibre terrestre (albedo 0.3) : ~255 K, valeur connue.
  const double Teq = env::equilibrium_temp(cst::AU, 0.3);
  CHECK(Teq > 250.0 && Teq < 260.0, "Teq Terre ~255 K");
}

static void test_radiations() {
  section("Radiations [GDD 6.6, Annexe B]");
  // AR martien ~500 j de croisiere non blinde, minimum solaire : 0.3..0.7 Sv.
  const double dose = env::gcr_dose_rate_sv_day(1.0, env::Shielding{}) * 500.0;
  CHECK(dose >= 0.3 && dose <= 0.7, "AR Mars GCR dans la fourchette GDD");
  // le blindage ATTENUE, monotone.
  env::Shielding s10{10.0, 1.0}, s30{30.0, 1.0};
  CHECK(env::spe_transmission(s30) < env::spe_transmission(s10), "SPE : monotone");
  CHECK(env::gcr_transmission(s30) < env::gcr_transmission(s10), "GCR : monotone");
  // GCR : plancher physique — 100 g/cm2 H-riche n'apporte pas 10x plus que 30.
  CHECK(env::gcr_transmission(env::Shielding{100.0, 1.0}) > 0.55, "GCR : plancher");
  env::DoseAccumulator acc;
  acc.add_acute_gy(5.0);
  CHECK(acc.acute_lethal(), "5 Gy aigus : letal");
  CHECK(acc.career_exceeded(), "5 Gy -> carriere terminee aussi");
}

static void test_fiabilite() {
  section("Fiabilite [GDD 12.3] — invariants de la base");
  reliability::ReliabilityDatabase db;
  reliability::ReliabilityRecord sans_source;
  sans_source.id = "X"; sans_source.nominal = 0.99; sans_source.lo = 0.98; sans_source.hi = 0.995;
  CHECK(!db.add(sans_source), "fiche SANS PROVENANCE refusee");
  reliability::ReliabilityRecord ok = sans_source;
  ok.source = "rapport"; ok.confidence = reliability::Confidence::A;
  ok.context.mission_days = 30.0;
  CHECK(db.add(ok), "fiche complete acceptee");
  CHECK(!db.add(ok), "pas d'ecrasement silencieux");
  // revision : l'historique GROSSIT, jamais ne retrecit.
  reliability::Revision rev;
  rev.date_iso = "2026-06-01"; rev.nominal = 0.985; rev.lo = 0.97; rev.hi = 0.99;
  rev.source = "anomalie vol 12"; rev.confidence = reliability::Confidence::B;
  CHECK(db.revise("X", rev), "revision appliquee");
  CHECK(db.find("X")->history.size() == 2, "archive + revision = 2 entrees");
  // conservateur : confiance D part de la borne basse.
  reliability::ReliabilityRecord d = ok;
  d.id = "D"; d.confidence = reliability::Confidence::D;
  const auto eff_a = reliability::evaluate(ok, {}, 30.0);
  const auto eff_d = reliability::evaluate(d, {}, 30.0);
  CHECK(eff_d.p_success < eff_a.p_success, "D plus prudent que A");
  // modificateur : environnement severe DEGRADE (monotonie).
  reliability::Modifiers sev; sev.environment = 2.0;
  CHECK(reliability::evaluate(ok, sev, 30.0).p_success < eff_a.p_success,
        "modificateur monotone");
  // rollup vs calcul main.
  CHECK_NEAR(reliability::rollup_series({0.9, 0.9}), 0.81, 1e-15, "serie");
  CHECK_NEAR(reliability::rollup_parallel({0.9, 0.9}), 0.99, 1e-15, "parallele");
  CHECK_NEAR(reliability::rollup_k_of_n(2, 3, 0.9), 0.972, 1e-12, "2 parmi 3");
}

static void test_verrous() {
  section("Deblocage [GDD 5.4] — le verrou le plus fort");
  tech::TechTree tree;
  app::AresLayer::seed_arbre(tree);
  career::CareerState carriere;   // Stagiaire
  tech::Capability cap;
  cap.min_rank = career::Rank::Directeur;
  cap.required_tech = {"nep_megawatt"};
  cap.cost_musd = 10.0;
  const auto v = tech::evaluate_unlock(cap, carriere, tree, 1000.0, nullptr);
  CHECK(!v.unlocked(), "NEP verrouillee pour un stagiaire");
  CHECK(v.dominant == tech::LockAxis::Trl, "la SCIENCE domine le rang");
  // techno operationnelle + rang suffisant + budget -> debloque.
  tech::Capability facile;
  facile.required_tech = {"lanceur_moyen"};
  CHECK(tech::evaluate_unlock(facile, carriere, tree, 100.0, nullptr).unlocked(),
        "capacite de depart accessible");
}

static void test_novellus() {
  section("Novellus [GDD 11.3] — paliers");
  station::Station st;
  app::AresLayer::seed_station(st);
  CHECK(st.tier() == 1, "seed = palier 1 (fondation)");
  st.modules.push_back({station::ModuleType::LifeSupport, true, 1, 0, 0});
  st.modules.push_back({station::ModuleType::CrewHabitat, true, 1, 0, 0});
  st.modules.push_back({station::ModuleType::Storage, true, 1, 0, 0});
  CHECK(st.tier() == 2, "habitabilite = palier 2");
  tech::InfrastructureNeed besoin;
  besoin.power_kw = 200.0;
  CHECK(!st.provides(besoin), "40 kW ne fournissent pas 200 kW");
}

static void test_severite() {
  section("Gravite [GDD 10.3] — modificateurs de palier");
  mission::SeverityModifiers m;
  m.human_lethal_exposure = true;
  m.player_error_causal = true;
  CHECK(mission::apply_modifiers(mission::Severity::Minor, m) ==
        mission::Severity::Major, "mineur + 2 aggravations = majeur");
  mission::SeverityModifiers r;
  r.brilliant_recovery = true;
  CHECK(mission::apply_modifiers(mission::Severity::Moderate, r) ==
        mission::Severity::Minor, "sauvetage brillant : -1/2 palier");
  CHECK(mission::consequences_for(
            {"", "", mission::Severity::Major, {}, {}, 0.0}).mission_family_suspended,
        "niveau 3 : famille suspendue");
}

static void test_evenements() {
  section("Evenements [GDD 9.4] — determinisme");
  Rng rng(777);
  mission::EventContext ctx;
  ctx.crewed = true;
  const auto a = mission::sample_events(rng, 5, 0.0, 200.0, ctx);
  const auto b = mission::sample_events(rng, 5, 0.0, 200.0, ctx);
  CHECK(a.size() == b.size(), "meme graine + meme fenetre = memes tirages");
  bool memes = a.size() == b.size();
  for (std::size_t i = 0; memes && i < a.size(); ++i)
    memes = a[i].kind == b[i].kind && a[i].t_days == b[i].t_days;
  CHECK(memes, "tirages identiques bit-a-bit");
  CHECK(env::spe_rate_per_year(1.0) > env::spe_rate_per_year(0.0),
        "plus d'eruptions au maximum solaire");
}

static void test_habite() {
  section("Habite [GDD 9.3, 9.5]");
  const auto sans = mission::vital_budget(4, 500.0, mission::RecyclingLoops::none());
  const auto iss = mission::vital_budget(4, 500.0, mission::RecyclingLoops::iss());
  CHECK(iss.total_kg() < sans.total_kg(), "le recyclage REDUIT");
  CHECK(iss.o2_kg > 0 && iss.water_kg > 0, "...sans jamais annuler");
  // Mars a 2.25e11 m : ~750 s aller simple. Identite d = c*t.
  CHECK_NEAR(mission::comms_delay_s(2.25e11), 750.49, 0.5, "delai lumiere Mars");
  CHECK(!mission::ground_loop_realtime(2.25e11), "pas de pilotage sol a Mars");
}

static void test_fsm() {
  section("Mission FSM [GDD 4.1] — transitions strictes");
  mission::Mission m;
  CHECK(!m.advance(mission::MissionState::Launched, 0.0), "pas de saut RECU->VOL");
  CHECK(m.advance(mission::MissionState::Prerequisites, 0.0), "RECU->PREREQUIS ok");
  CHECK(m.advance(mission::MissionState::Design, 0.0), "->CONCEPTION ok");
  m.state = mission::MissionState::Completed;
  CHECK(!m.advance(mission::MissionState::Design, 0.0), "terminal : aucune sortie");
}

static void test_couche_ares() {
  section("AresLayer — integration agence (duck-typing du template)");
  struct FauxAgence {
    bool creee{true};
    double mois{0.0}, tresorerie{400.0}, confiance{0.7};
    int reussites{0}, echecs{0};
    std::uint64_t graine_agence{123};
    std::vector<std::string> journal;
    void log(const std::string& s) { journal.push_back(s); }
  } a;
  app::AresLayer L;
  L.assurer(a, 0.0);
  CHECK(L.initialisee(), "creation au premier assurer()");
  auto& G = *L.etat;
  CHECK(G.tree.find("antimatiere") != nullptr, "arbre seede");
  CHECK(G.station.tier() == 1, "station seedee");
  CHECK(G.reliability_db.find("MTX-1") != nullptr, "base fiabilite seedee");
  // une recherche de 90 j finit en 4 mois, et le journal l'apprend a l'agence.
  CHECK(G.research.start(G.tree, "rdv_automatise", career::Rank::Directeur),
        "recherche demarree");
  a.mois = 4.0;
  L.assurer(a, a.mois * app::ARES_MONTH_S);
  CHECK(G.tree.find("rdv_automatise")->operational(), "TRL 7 apres 4 mois");
  CHECK(!a.journal.empty(), "l'agence est notifiee dans SON journal");
  // le score derive des reussites -> promotion Stagiaire -> Junior a 100 pts.
  a.reussites = 3; a.mois = 5.0;
  L.assurer(a, a.mois * app::ARES_MONTH_S);
  CHECK(G.career.rank == career::Rank::Junior, "3 reussites (120 pts) -> Junior");
  // persistance : save -> load dans une couche neuve = meme hash.
  const std::string tmp = "test_ares.sav.tmp";
  CHECK(L.sauvegarder(tmp), "sauvegarde ecrite");
  app::AresLayer L2;
  FauxAgence a2 = a;
  L2.assurer(a2, a2.mois * app::ARES_MONTH_S);
  CHECK(L2.charger(tmp), "sauvegarde relue");
  CHECK(L2.etat->hash() == L.etat->hash(), "hash identique apres save->load");
  CHECK(L2.etat->tree.find("rdv_automatise")->operational(), "TRL restaure");
  std::remove(tmp.c_str());
  // nouvelle partie : mois qui recule = reset.
  a.mois = 0.0; a.reussites = 0;
  L.assurer(a, 0.0);
  CHECK(L.etat->career.rank == career::Rank::Stagiaire, "reset sur nouvelle partie");
}

int main() {
  test_relativite();
  test_horloges();
  test_antimatiere();
  test_assemblage();
  test_thermique();
  test_radiations();
  test_fiabilite();
  test_verrous();
  test_novellus();
  test_severite();
  test_evenements();
  test_habite();
  test_fsm();
  test_couche_ares();
  std::printf("\n%d OK, %d FAIL\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}

#endif // SP_STANDALONE_TESTS
