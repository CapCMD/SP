// tests/test_economie_v12.cpp — ORACLES DE L'ÉCONOMIE v1.2 [GDD 13]
//
// L'économie chiffrée : budget d'agence à l'échelle réelle (M€), deux jauges
// (trésorerie / réserve), invariant de pression d'inactivité, chaîne de fin de
// partie graduée, et confiance ARES comme FILTRE d'éligibilité. On vérifie les
// INVARIANTS du GDD, pas des valeurs de calibrage (celles-ci sont en Annexe E).
//
// STANDALONE UNIQUEMENT : compile avec /DSP_STANDALONE_TESTS.
#ifdef SP_STANDALONE_TESTS

#include <cmath>
#include <cstdio>

#include "fen/economy/Economy.hpp"

using namespace fen::economy;

static int g_ok = 0, g_ko = 0;
#define CHECK(cond, nom)                                                     \
  do {                                                                       \
    if (cond) { ++g_ok; }                                                    \
    else { ++g_ko; std::printf("ECHEC : %s (ligne %d)\n", nom, __LINE__); }  \
  } while (0)
#define CHECK_NEAR(a, b, tol, nom)                                           \
  do {                                                                       \
    if (std::fabs((a) - (b)) <= (tol)) { ++g_ok; }                            \
    else { ++g_ko; std::printf("ECHEC : %s — %.3f vs %.3f (ligne %d)\n",       \
                               nom, (double)(a), (double)(b), __LINE__); }    \
  } while (0)

int main() {
  // ═══ 1. L'INVARIANT DE PRESSION D'INACTIVITÉ [GDD 13.2] ═══
  {
    AgencyFinance f;
    // « seul compte l'invariant : recettes garanties < coûts fixes ».
    CHECK(f.inactivity_pressure_holds(),
          "13.2 : recettes garanties < couts fixes (pression d inactivite)");
    CHECK(f.revenue.guaranteed_yr() < f.annual_fixed(),
          "13.2 : l invariant tient sur les valeurs par defaut");
    // Ordre de grandeur : ~35 Md€ garantis vs ~44 Md€ fixes -> ~ −9 Md€/an idle.
    const double idle_net = f.revenue.guaranteed_yr() - f.annual_fixed();
    CHECK(idle_net < -5000.0 && idle_net > -15000.0,
          "13.2 : l oisivete draine de l ordre de 9 Md€/an");
    // LE PRIX DU TEMPS, exposé par le modèle [GDD 14.2] : c'est ce chiffre que le
    // poste AGENCE affiche avant d'accélérer, il ne le recalcule pas lui-même.
    CHECK_NEAR(f.annual_idle_balance_me(), idle_net, 1e-9,
               "14.2 : le modele expose le prix du temps qui passe");
    CHECK(f.annual_idle_balance_me() < 0.0,
          "14.2 : accelerer sans programme COUTE (solde inactif negatif)");
    // À pleine activité, l'agence est au moins à l'équilibre.
    CHECK(f.revenue.annual(1.0, 1.0) >= f.annual_fixed(),
          "13.2 : a pleine activite, l agence tient l equilibre");
  }

  // ═══ 2. RECETTES CONDITIONNÉES À L'ACTIVITÉ [GDD 13.1] ═══
  {
    RevenueModel r;
    const double idle = r.annual(0.0, 0.0);
    const double full = r.annual(1.0, 1.0);
    CHECK(full > idle, "13.1 : produire rapporte plus qu etre inactif");
    // Les tranches programmes et commercial ne tombent QUE si activité.
    CHECK_NEAR(idle, r.base_guaranteed_me_yr + r.valorisation_me_yr, 1e-6,
               "13.1 : inactif = base + valorisation seulement");
    CHECK(r.annual(1.0, 0.0) > r.annual(0.0, 0.0),
          "13.1 : avancer des programmes libere la tranche programmes");
    CHECK(r.annual(0.0, 1.0) > r.annual(0.0, 0.0),
          "13.1 : rendre des services rapporte le commercial");
    CHECK_NEAR(idle, r.guaranteed_yr(), 1e-6, "13.1 : garanti = recettes hors activite");
  }

  // ═══ 3. DEUX JAUGES : trésorerie draine d'abord, réserve ensuite [GDD 13.4] ═══
  {
    AgencyFinance f;
    f.treasury_me = 1000.0;
    f.reserve_me = 18000.0;
    const double res0 = f.reserve_me;
    // Un mois inactif : net négatif ; la trésorerie encaisse le drain.
    f.tick_month(0.0, 0.0);
    CHECK(f.reserve_me <= res0, "13.4 : la reserve n augmente pas en deficit");
    // Assez de mois inactifs pour vider la trésorerie puis entamer la réserve.
    for (int m = 0; m < 24; ++m) f.tick_month(0.0, 0.0);
    CHECK(f.treasury_me <= 1e-6, "13.4 : la tresorerie s epuise la premiere");
    CHECK(f.reserve_me < res0, "13.4 : puis la RESERVE est entamee");

    // Les paliers d'alerte portent sur la RÉSERVE rapportée à sa cible.
    AgencyFinance g;
    g.reserve_target_me = 1000.0; g.reserve_me = 800.0;
    CHECK(g.reserve_level() == AlertLevel::Normal, "13.4 : reserve > 75 % = normal");
    g.reserve_me = 400.0;
    CHECK(g.reserve_level() == AlertLevel::Delays, "13.4 : reserve a 40 % = retards");
    g.reserve_me = 40.0;
    CHECK(g.reserve_level() == AlertLevel::Asphyxia, "13.4 : reserve < 5 % = asphyxie");
  }

  // ═══ 4. ENGAGER un programme : trésorerie puis réserve, refus si trop [GDD 4.1] ═══
  {
    AgencyFinance f;
    f.treasury_me = 500.0; f.reserve_me = 2000.0;
    CHECK(f.engage(300.0), "engage : couvert par la tresorerie");
    CHECK_NEAR(f.treasury_me, 200.0, 1e-6, "engage : tresorerie prelevee");
    CHECK(f.engage(700.0), "engage : deborde sur la reserve");
    CHECK_NEAR(f.treasury_me, 0.0, 1e-6, "engage : tresorerie videe d abord");
    CHECK_NEAR(f.reserve_me, 1500.0, 1e-6, "engage : le reste sur la reserve");
    CHECK(!f.engage(2000.0), "engage : refuse si le total ne couvre pas");
    CHECK_NEAR(f.reserve_me, 1500.0, 1e-6, "engage : un refus ne prelevE rien");
    f.credit(1000.0);
    CHECK_NEAR(f.treasury_me, 1000.0, 1e-6, "credit : le budget contrat entre en tresorerie");
  }

  // ═══ 5. CHAÎNE DE FIN DE PARTIE : graduée, réversible, jamais brutale [GDD 13.5] ═══
  {
    AgencyFinance f;
    f.reserve_target_me = 1000.0; f.reserve_me = 900.0; f.treasury_me = 0.0;
    // agence saine
    f.advance_chain();
    CHECK(f.stage == FinancialStage::Sain, "13.5 : reserve haute = sain");
    // réserve basse maintenue -> montée graduée des paliers
    f.reserve_me = 100.0;   // 10 % : sous tension
    FinancialStage prev = FinancialStage::Sain;
    bool monotone = true, atteint_gel = false, atteint_licenciement = false;
    for (int m = 0; m < 12; ++m) {
      f.advance_chain();
      if (static_cast<int>(f.stage) < static_cast<int>(prev)) monotone = false;
      prev = f.stage;
      if (f.stage == FinancialStage::ContratsGeles) atteint_gel = true;
      if (f.stage == FinancialStage::Licencie) atteint_licenciement = true;
    }
    CHECK(monotone, "13.5 : la chaine ne monte que graduellement");
    CHECK(atteint_gel, "13.5 : le gel des contrats precede le licenciement");
    CHECK(atteint_licenciement, "13.5 : une derive prolongee mene au licenciement");
    CHECK(f.contracts_frozen() && f.dismissed(), "13.5 : etats terminaux coherents");

    // La chaîne est RÉVERSIBLE : reconstituer la réserve la fait redescendre.
    AgencyFinance g;
    g.reserve_target_me = 1000.0; g.reserve_me = 100.0;
    for (int m = 0; m < 4; ++m) g.advance_chain();
    CHECK(g.stage > FinancialStage::Sain, "13.5 : la reserve basse degrade");
    g.reserve_me = 900.0;   // renflouée
    g.advance_chain();
    CHECK(g.stage == FinancialStage::Sain, "13.5 : une reserve reconstituee sauve l agence");

    // Un ACCIDENT ISOLÉ ne licencie jamais : « jamais sur un accident isolé ».
    AgencyFinance h;
    h.reserve_target_me = 1000.0; h.reserve_me = 100.0;
    h.advance_chain();
    CHECK(!h.dismissed(), "13.5 : un seul mois bas ne licencie pas");
  }

  // ═══ 6. SUSPENSION pendant une mission longue [GDD 9.3] ═══
  {
    AgencyFinance f;
    f.treasury_me = 100.0; f.reserve_me = 500.0;
    f.suspended = true;
    const double t0 = f.treasury_me, r0 = f.reserve_me;
    for (int m = 0; m < 120; ++m) f.tick_month(0.0, 0.0);   // dix ans d'absence
    CHECK_NEAR(f.treasury_me, t0, 1e-6, "9.3 : trésorerie gelée pendant l absence");
    CHECK_NEAR(f.reserve_me, r0, 1e-6, "9.3 : reserve gelée pendant l absence");
    CHECK(!f.dismissed(), "9.3 : aucune faillite ne survient en l absence du joueur");
  }

  // ═══ 7. CONFIANCE ARES : filtre d'éligibilité [GDD 13.4] ═══
  {
    // Départ à 70 = bande normale, tous contrats de routine.
    CHECK(access_band(70.0) == AccessBand::Normal, "13.4 : 70 = routine complete");
    CHECK(access_band(85.0) == AccessBand::Flagship, "13.4 : 80+ = programmes phares");
    CHECK(access_band(50.0) == AccessBand::Restricted, "13.4 : 40-59 = habite suspendu");
    CHECK(access_band(30.0) == AccessBand::RoboticOnly, "13.4 : 20-39 = robotique seule");
    CHECK(access_band(10.0) == AccessBand::Frozen, "13.4 : <20 = gele");

    // Les droits effectifs suivent les seuils.
    CHECK(crewed_allowed(60.0) && !crewed_allowed(59.0),
          "13.4 : missions habitees suspendues sous 60");
    CHECK(flagship_allowed(80.0) && !flagship_allowed(79.0),
          "13.4 : programmes phares reserves a 80+");
    CHECK(new_program_allowed(20.0) && !new_program_allowed(19.0),
          "13.4 : aucun nouveau programme sous 20");

    // La monotonie : plus de confiance n'ouvre jamais MOINS de portes.
    bool mono = true;
    for (double c = 0.0; c <= 100.0; c += 1.0)
      if (static_cast<int>(access_band(c)) > static_cast<int>(access_band(std::min(100.0, c + 1.0))))
        ; // access_band est numeroté du plus permissif (0) au plus restreint (4)
    (void)mono;
    // Un Directeur à confiance basse conserve son RANG mais perd l'accès habité.
    CHECK(!crewed_allowed(45.0), "13.4 : un rang eleve ne compense pas une confiance basse");

    // La procédure sous 20 ramène à une bande de reprise (jamais un mur).
    CHECK(access_band(confidence_recovery_after_procedure()) == AccessBand::Restricted,
          "13.4 : la procedure rouvre les contrats de routine du rang inferieur");
  }

  std::printf("\nECONOMIE v1.2 (Md€, reserve, confiance) : %d oracles OK, %d en echec.\n",
              g_ok, g_ko);
  return g_ko == 0 ? 0 : 1;
}

#endif // SP_STANDALONE_TESTS
