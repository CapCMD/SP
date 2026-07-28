// tests/test_code_qualif.cpp — ORACLES DU BANC D'ESSAI [GDD 15.5, v1.2]
//
// Le banc d'essai du logiciel de vol. On vérifie les PROPRIÉTÉS structurantes du
// GDD : un modèle avec domaine de validité (pas un oracle), qui RASSURE SANS
// GARANTIR (couverture < 1), et pour lequel EXÉCUTER HORS DU DOMAINE est un
// comportement non couvert — cause d'anomalie légitime.
//
// STANDALONE UNIQUEMENT : compile avec /DSP_STANDALONE_TESTS.
#ifdef SP_STANDALONE_TESTS

#include <cmath>
#include <cstdio>

#include "fen/code/CodeQualification.hpp"

using namespace fen;
using namespace fen::code;

static int g_ok = 0, g_ko = 0;
#define CHECK(cond, nom)                                                     \
  do {                                                                       \
    if (cond) { ++g_ok; }                                                    \
    else { ++g_ko; std::printf("ECHEC : %s (ligne %d)\n", nom, __LINE__); }  \
  } while (0)

int main() {
  ValidityDomain leo;
  leo.environment = "LEO";
  leo.input_lo = 7000.0; leo.input_hi = 8200.0;   // vitesses d'entrée LEO (m/s)
  leo.degraded_profiles = true;
  leo.interfaces_tested = true;

  // ═══ 1. LE CODE DOIT COMPILER (étape 1) ═══
  {
    Certification c = run_test_bench("gnc-1", /*compiled*/ false, leo, 500.0);
    CHECK(!c.certified, "15.5 : un code qui ne compile pas n est jamais certifie");
    CHECK(c.coverage == 0.0, "15.5 : pas de couverture sans compilation");
  }

  // ═══ 2. RASSURE SANS GARANTIR : couverture croissante mais < 1 [GDD 15.5] ═══
  {
    Certification c0 = run_test_bench("gnc", true, leo, 0.0);
    Certification c1 = run_test_bench("gnc", true, leo, 100.0);
    Certification c2 = run_test_bench("gnc", true, leo, 1000.0);
    CHECK(c0.coverage == 0.0, "15.5 : aucun essai = aucune couverture");
    CHECK(c1.coverage > c0.coverage && c2.coverage > c1.coverage,
          "15.5 : tester plus augmente la couverture");
    CHECK(c2.coverage < 1.0, "15.5 : la couverture SATURE sous 1 — jamais garantie");
    CHECK(c2.coverage <= BENCH_COVERAGE_CEILING + 1e-9,
          "15.5 : la couverture ne depasse pas le plafond");
    // même exhaustivement testé, un état non imaginé peut échouer.
    Certification c_inf = run_test_bench("gnc", true, leo, 1.0e6);
    CHECK(c_inf.coverage < 1.0, "15.5 : meme un test quasi infini ne garantit rien");
  }

  // ═══ 3. LE BANC COÛTE : budget et délai [GDD 15.5] ═══
  {
    Certification c = run_test_bench("gnc", true, leo, 400.0);
    CHECK(c.budget_spent_me > 0.0, "15.5 : le banc consomme du budget");
    CHECK(bench_delay_days(c) > 0.0, "15.5 : le banc retarde la fenetre");
    Certification more = run_test_bench("gnc", true, leo, 800.0);
    CHECK(more.budget_spent_me > c.budget_spent_me && bench_delay_days(more) > bench_delay_days(c),
          "15.5 : tester plus coute plus cher et plus longtemps");
  }

  // ═══ 3 bis. ÉLARGIR LE DOMAINE DILUE LA COUVERTURE ═══ [GDD 15.5]
  // « Testé sur une plage, pas au-delà. » Déclarer qu'on couvre AUSSI les
  // profils dégradés et les interfaces agrandit l'espace d'état prétendu
  // couvert : à heures constantes, la couverture doit BAISSER. Sans cette
  // propriété, cocher les deux cases serait une montée en confiance gratuite —
  // un domaine plus large certifié au même prix.
  {
    ValidityDomain etroit = leo;
    etroit.degraded_profiles = false;
    etroit.interfaces_tested = false;
    Certification large  = run_test_bench("gnc", true, leo, 500.0);
    Certification serre  = run_test_bench("gnc", true, etroit, 500.0);
    CHECK(serre.coverage > large.coverage,
          "15.5 : a heures egales, un domaine plus LARGE est moins couvert");
    CHECK(bench_h_char(leo) > bench_h_char(etroit),
          "15.5 : un domaine plus large demande plus d heures caracteristiques");
    // ... et la dilution se RACHÈTE en heures : c'est un arbitrage, pas un mur.
    Certification paye = run_test_bench("gnc", true, leo, 500.0 * bench_h_char(leo) /
                                                          bench_h_char(etroit));
    CHECK(std::fabs(paye.coverage - serre.coverage) < 1e-9,
          "15.5 : payer les heures du domaine elargi rend la meme couverture");
  }

  // ═══ 4. LA CONFIANCE dépend de la couverture ET des interfaces/dégradés ═══
  // 2 000 h, et non 1 000 : le domaine complet (dégradés + interfaces) a des
  // heures caractéristiques élargies (voir 3 bis), donc la confiance A se paie
  // plus cher qu'au nominal. C'est le prix de ce qu'on déclare couvrir.
  {
    // Bien testé, interfaces + dégradés couverts -> A possible.
    Certification a = run_test_bench("gnc", true, leo, 2000.0);
    CHECK(a.confidence == reliability::Confidence::A,
          "15.5 : couverture haute + interfaces + degrades = confiance A");
    // Le point faible : ne pas tester les interfaces plafonne la confiance —
    // à HEURES ÉGALES, pour que l'oracle isole bien le seul drapeau d'interface.
    ValidityDomain sans_itf = leo; sans_itf.interfaces_tested = false;
    Certification b = run_test_bench("gnc", true, sans_itf, 2000.0);
    CHECK(b.confidence <= reliability::Confidence::B,
          "15.5 : sans test d interfaces, la confiance plafonne (defaut naturel)");
    // Peu testé -> confiance basse.
    Certification d = run_test_bench("gnc", true, leo, 30.0);
    CHECK(d.confidence >= reliability::Confidence::C,
          "15.5 : peu d essais = confiance basse");
  }

  // ═══ 5. LE DOMAINE DE VALIDITÉ : hors domaine = non couvert [GDD 15.5] ═══
  {
    Certification c = run_test_bench("gnc", true, leo, 800.0);
    // Dans le domaine : qualifié, probabilité = couverture.
    CHECK(c.qualifies("LEO", 7700.0, true), "15.5 : qualifie dans son environnement et sa plage");
    CHECK(code_success_prob(c, "LEO", 7700.0, true) > 0.0, "15.5 : dans le domaine, ca marche (a la couverture pres)");
    // Mauvais ENVIRONNEMENT : un code LEO n'est pas qualifié pour Mars.
    CHECK(!c.qualifies("insertion_mars", 7700.0, true), "15.5 : LEO n est pas qualifie pour Mars");
    CHECK(out_of_validity_domain(c, "insertion_mars", 7700.0, true),
          "15.5 : hors environnement = hors domaine");
    CHECK(code_success_prob(c, "insertion_mars", 7700.0, true) == 0.0,
          "15.5 : hors domaine = comportement NON COUVERT (0)");
    // Hors PLAGE d'entrée : un retour lunaire à 11 km/s dépasse la plage LEO.
    CHECK(!c.qualifies("LEO", 11000.0, true), "15.5 : hors plage d entree = non couvert");
    // Profil DÉGRADÉ non couvert si le domaine ne l'inclut pas.
    ValidityDomain nominal_only = leo; nominal_only.degraded_profiles = false;
    Certification cn = run_test_bench("gnc", true, nominal_only, 800.0);
    CHECK(cn.qualifies("LEO", 7700.0, true), "15.5 : couvre le nominal");
    CHECK(!cn.qualifies("LEO", 7700.0, false),
          "15.5 : un code valide au nominal n est PAS valide en degrade");
    CHECK(code_success_prob(cn, "LEO", 7700.0, false) == 0.0,
          "15.5 : executer un profil degrade non teste = non couvert");
  }

  std::printf("\nBANC D'ESSAI (code de vol) : %d oracles OK, %d en echec.\n", g_ok, g_ko);
  return g_ko == 0 ? 0 : 1;
}

#endif // SP_STANDALONE_TESTS
