// tests/test_api_sol.cpp — ORACLES DES API `ares::sol` ET `ares::vol` [GDD 15.2-15.3]
//
// La slice MODÈLE du ch.15 : les surfaces d'API que le code du joueur appelle
// (mêmes signatures en mode Pro C++ et via les noeuds du mode Normal). On
// vérifie ici que :
//
//   . API SOL (15.2) — les helpers d'unités, l'ephemeride (bornes physiques
//     reelles), le cablage du transfert de Lambert (dv_total = injection +
//     insertion, calcule sur les VRAIES vitesses des corps), et le budget de
//     masse Tsiolkovsky du vehicule. On reproduit la CHAINE de l'exemple 15.2.
//
//   . API VOL (15.3) — on execute LITTERALEMENT la fonction `sequence_correction`
//     de l'exemple du GDD (le "code du joueur") contre un Contexte construit, et
//     on verifie que chacun de ses quatre garde-fous se declenche comme voulu :
//       A. solution degradee (3sigma > 12 km)      -> alerte + replanif, 0 burn
//       B. dans les marges (ecart < tolerance)       -> rien consomme
//       C. correction > 35% des reserves             -> alerte + differe, 0 burn
//       D. nominal                                   -> execute, reserves - dv
//
// Le calcul reste celui du coeur (Lambert, ephemeride, Tsiolkovsky), deja sous
// oracle ailleurs ; ici on qualifie la FACADE et la LOGIQUE DE VOL.
//
// STANDALONE UNIQUEMENT : compile avec /DSP_STANDALONE_TESTS.
#ifdef SP_STANDALONE_TESTS

#include <cmath>
#include <cstdio>
#include <string>

#include "ares/sol.hpp"
#include "ares/vol.hpp"
#include "fen/core/Constants.hpp"

static int g_ok = 0, g_ko = 0;
#define CHECK(cond, nom)                                                     \
  do {                                                                       \
    if (cond) { ++g_ok; }                                                    \
    else { ++g_ko; std::printf("ECHEC : %s (ligne %d)\n", nom, __LINE__); }  \
  } while (0)
#define CHECK_NEAR(a, b, tol, nom)                                           \
  do {                                                                       \
    const double d_ = std::fabs((a) - (b));                                  \
    const double s_ = std::fabs(b) > 1e-30 ? d_ / std::fabs(b) : d_;         \
    if (s_ <= (tol)) { ++g_ok; }                                             \
    else { ++g_ko; std::printf("ECHEC : %s - %.6g vs %.6g (ligne %d)\n",     \
                               nom, (double)(a), (double)(b), __LINE__); }   \
  } while (0)

// ═══════════════════════════════════════════════════════════════════════════
// LE CODE DU JOUEUR — recopie de l'exemple GDD 15.3 (strings ASCII-fies, sans
// changer une virgule de la LOGIQUE). C'est l'objet meme de l'oracle vol.
// ═══════════════════════════════════════════════════════════════════════════
namespace {
// La directive `using` reste DANS le corps : au niveau du namespace anonyme
// elle fuiterait dans la recherche globale et rendrait ambigus les helpers
// d'unites homonymes de `ares::sol` (jours/heures/km/metres).
void sequence_correction(ares::vol::Contexte& ctx) {
  using namespace ares::vol;
  Etat estime = ctx.navigation().solution();

  // Refuser d'agir sur une solution degradee
  if (estime.incertitude_3sigma() > metres(12000)) {
    ctx.alerte("Solution de navigation degradee - correction reportee");
    ctx.replanifier(heures(48));
    return;
  }

  Ecart e = ctx.cible().ecart_projete(estime);
  if (e.norme() < ctx.cible().tolerance()) {
    return;                       // dans les marges, ne rien consommer
  }

  Manoeuvre m = ctx.solveur().corriger(estime, ctx.cible());

  // Garde-fou : ne jamais engager plus de 35% des reserves sans le sol
  if (m.dv() > ctx.reserves().dv_disponible() * 0.35) {
    ctx.alerte("Correction > 35% des reserves - validation sol requise");
    ctx.differer(m);
    return;
  }

  ctx.executer(m);
  ctx.journal_bord("Correction executee : %.2f m/s", m.dv());
}
}  // namespace

// ═══════════════════════════════════════════════════════════════════════════
int main() {
  // ────────────────── API SOL 15.2 : HELPERS D'UNITES ──────────────────
  {
    using namespace ares::sol;
    CHECK_NEAR(jours(281), 281.0 * 86400.0, 1e-12, "sol : jours()");
    CHECK_NEAR(heures(6), 6.0 * 3600.0, 1e-12, "sol : heures()");
    CHECK_NEAR(minutes(90), 90.0 * 60.0, 1e-12, "sol : minutes()");
    CHECK_NEAR(km(1), 1000.0, 1e-12, "sol : km()");
    CHECK_NEAR(metres(42), 42.0, 1e-12, "sol : metres()");
  }

  // ────────────────── API SOL 15.2 : LE JOURNAL D'ANALYSE ──────────────────
  {
    using namespace ares::sol;
    journal_clear();
    CHECK(journal_lines().empty(), "sol : journal vide apres clear");
    journal("Dv requis : %8.0f m/s", 3600.0);
    journal("ratio     : %8.3f", 2.25);
    CHECK(journal_lines().size() == 2, "sol : journal accumule 2 lignes");
    CHECK(journal_lines()[0].find("3600") != std::string::npos,
          "sol : journal formate la valeur");
  }

  // ────────────────── API SOL 15.2 : EPHEMERIDE (bornes reelles) ──────────────
  // La chaine exacte de l'exemple : Terre au 2026-11-04, Mars au 2027-08-12.
  ares::sol::Corps terre, mars;
  {
    using namespace ares::sol;
    terre = ephemeride("TERRE", date("2026-11-04T00:00:00Z"));
    mars  = ephemeride("MARS",  date("2027-08-12T00:00:00Z"));

    const double r_terre = fen::norm(terre.position());
    const double v_terre = fen::norm(terre.vitesse());
    // Terre : 0.983 (perihelie) .. 1.017 UA (aphelie). Nov -> ~0.99 UA.
    CHECK(r_terre > 1.46e11 && r_terre < 1.53e11, "sol : |r Terre| ~ 1 UA");
    CHECK(v_terre > 29000.0 && v_terre < 31000.0, "sol : |v Terre| ~ 29-30 km/s");

    const double r_mars = fen::norm(mars.position());
    const double v_mars = fen::norm(mars.vitesse());
    // Mars : 1.381 (perihelie) .. 1.666 UA (aphelie) ; vitesse 21.97-26.5 km/s.
    CHECK(r_mars > 2.05e11 && r_mars < 2.50e11, "sol : |r Mars| dans 1.38-1.67 UA");
    CHECK(v_mars > 21000.0 && v_mars < 27000.0, "sol : |v Mars| ~ 22-26 km/s");
  }

  // ────────────────── API SOL 15.2 : TRANSFERT DE LAMBERT ──────────────
  {
    using namespace ares::sol;
    Transfert t = lambert(terre, mars, jours(281));
    CHECK(t.ok(), "sol : Lambert Terre->Mars converge");
    CHECK(std::isfinite(t.dv_total()), "sol : dv_total fini");
    // Cablage de la facade : dv_total = injection + insertion, chacun mesure
    // sur la VRAIE vitesse du corps (c'est la que git la rigueur physique).
    CHECK_NEAR(t.dv_total(), t.dv_depart() + t.dv_arrivee(), 1e-9,
               "sol : dv_total = depart + arrivee");
    CHECK_NEAR(t.dv_depart(), fen::norm(t.v_depart() - terre.vitesse()), 1e-9,
               "sol : dv_depart = |v1 - v_Terre|");
    CHECK_NEAR(t.dv_arrivee(), fen::norm(t.v_arrivee() - mars.vitesse()), 1e-9,
               "sol : dv_arrivee = |v2 - v_Mars|");
    // Ordre de grandeur d'une fenetre reelle : exces heliocentrique de quelques
    // km/s de chaque cote (pas un "moteur magique", pas non plus absurde).
    CHECK(t.dv_total() > 3000.0 && t.dv_total() < 20000.0,
          "sol : dv_total Terre->Mars dans l'ordre de grandeur reel");
    CHECK(t.dv_depart() > 0.0 && t.dv_arrivee() > 0.0,
          "sol : les deux impulsions sont positives");
  }

  // ────────────────── API SOL 15.2 : BUDGET DE MASSE (Tsiolkovsky) ──────────
  // charger("ARV-3") est maintenant un ETAGE REEL du catalogue (RL10C-1 + LOX/LH2),
  // pas une constante. On RECONSTRUIT ses masses depuis les memes pieces et on
  // exige l'egalite : la facade ne fabrique aucun chiffre.
  {
    using namespace ares::sol;
    using namespace fen::vehicle;
    Vehicule v = charger("ARV-3");

    const EnginePart* e = find_engine("RL10C-1");
    const TankPart*   tk = find_tank("TANK-LOX-LH2");
    CHECK(e && tk, "sol : les pieces reelles de l'ARV-3 existent au catalogue");
    const double ergols = 10000.0, structure = 400.0, payload = 2000.0;   // = architecture ARV-3
    const double ve_attendu = e->isp_vac_s * fen::cst::G0;
    const double cap_attendue = ergols * (1.0 - tk->residual_fraction);
    const double sec_attendue = e->mass_kg + ergols * tk->dry_fraction
                              + structure + ergols * tk->residual_fraction + payload;

    CHECK_NEAR(v.ve_effective(), ve_attendu, 1e-9, "sol : ve ARV-3 = Isp RL10C-1 x g0");
    CHECK_NEAR(v.capacite_ergols(), cap_attendue, 1e-9, "sol : ergols utilisables du reservoir reel");
    CHECK_NEAR(v.masse_seche(), sec_attendue, 1e-9, "sol : masse seche = inerte etage + charge utile");

    // ergols_pour(dv) DOIT etre exactement mf.(exp(dv/ve) - 1).
    for (double dv : {500.0, 2000.0, 4000.0, 6000.0}) {
      const double attendu = v.masse_seche() * (std::exp(dv / v.ve_effective()) - 1.0);
      CHECK_NEAR(v.ergols_pour(dv), attendu, 1e-9, "sol : Tsiolkovsky exact");
    }

    // CROISEMENT façade <-> coeur : a pleine charge, le Δv de Vehicule doit
    // egaler stage_dv de fen::vehicle::Vehicle (meme etage, meme charge utile).
    Vehicle vh;
    vh.payload_dry = payload;
    vh.stages.push_back(etage_reel("RL10C-1", "TANK-LOX-LH2", ergols, structure));
    const double dv_coeur = vh.total_dv();
    CHECK_NEAR(v.ergols_pour(dv_coeur), v.capacite_ergols(), 1e-6,
               "sol : a pleine charge, ergols requis == capacite (facade == coeur)");
    CHECK(v.faisable(dv_coeur), "sol : le Δv coeur est tout juste faisable");
    CHECK(!v.faisable(dv_coeur * (1.0 + 1e-6)), "sol : au-dela, infaisable");

    // Coherence du predicat de faisabilite avec la capacite (auto-coherent).
    const double dv_lim = v.ve_effective() * std::log(1.0 + v.capacite_ergols() / v.masse_seche());
    CHECK(v.faisable(dv_lim - 1.0), "sol : faisable juste sous la limite");
    CHECK(!v.faisable(dv_lim + 1.0), "sol : infaisable juste au-dessus");
  }

  // ═══════════════════ API VOL 15.3 : HELPERS + GARDE-FOUS ═══════════════════
  {
    using namespace ares::vol;
    CHECK_NEAR(metres(12000), 12000.0, 1e-12, "vol : metres()");
    CHECK_NEAR(heures(48), 48.0 * 3600.0, 1e-12, "vol : heures()");
    CHECK_NEAR(jours(1), 86400.0, 1e-12, "vol : jours()");
    CHECK_NEAR(km(1), 1000.0, 1e-12, "vol : km()");
  }

  // Cible commune : nominale a l'origine, tolerance 5 km. Solveur par defaut
  // (tau = 1 jour), donc |manoeuvre| = |ecart| / 86400 s.
  const fen::Vec3 cible_pos{0, 0, 0};
  const double tolerance = ares::vol::metres(5000);

  // ── BRANCHE A : solution degradee (3sigma = 15 km > 12 km) ──
  {
    using namespace ares::vol;
    Etat nav(Vec3{500000, 0, 0}, Vec3{0, 0, 0}, /*sigma3=*/15000.0);
    Contexte ctx(nav, Cible(cible_pos, tolerance), Reserves(100.0));
    sequence_correction(ctx);
    CHECK(ctx.alertes().size() == 1, "vol A : une alerte (solution degradee)");
    CHECK(ctx.replans().size() == 1, "vol A : une replanification");
    CHECK_NEAR(ctx.replans()[0], heures(48), 1e-9, "vol A : replanif a 48 h");
    CHECK(ctx.executees().empty(), "vol A : aucune manoeuvre executee");
    CHECK(ctx.differees().empty(), "vol A : aucune manoeuvre differee");
    CHECK_NEAR(ctx.dv_restant(), 100.0, 1e-12, "vol A : reserves intactes");
  }

  // ── BRANCHE B : dans les marges (ecart 3 km < tolerance 5 km) ──
  {
    using namespace ares::vol;
    Etat nav(Vec3{3000, 0, 0}, Vec3{0, 0, 0}, /*sigma3=*/2000.0);
    Contexte ctx(nav, Cible(cible_pos, tolerance), Reserves(100.0));
    sequence_correction(ctx);
    CHECK(ctx.alertes().empty(), "vol B : aucune alerte");
    CHECK(ctx.executees().empty(), "vol B : rien execute (dans les marges)");
    CHECK(ctx.differees().empty(), "vol B : rien differe");
    CHECK(ctx.journal().empty(), "vol B : journal vide");
    CHECK_NEAR(ctx.dv_restant(), 100.0, 1e-12, "vol B : reserves intactes");
  }

  // ── BRANCHE C : correction > 35% des reserves ──
  // ecart 5000 km -> dv = 5e6 / 86400 = 57.87 m/s ; 35% de 100 = 35 -> differe.
  {
    using namespace ares::vol;
    Etat nav(Vec3{5000000, 0, 0}, Vec3{0, 0, 0}, /*sigma3=*/2000.0);
    Contexte ctx(nav, Cible(cible_pos, tolerance), Reserves(100.0));
    sequence_correction(ctx);
    CHECK(ctx.alertes().size() == 1, "vol C : une alerte (>35%)");
    CHECK(ctx.differees().size() == 1, "vol C : une manoeuvre differee");
    CHECK(ctx.executees().empty(), "vol C : rien execute");
    CHECK_NEAR(ctx.dv_restant(), 100.0, 1e-12, "vol C : reserves intactes (differe)");
    CHECK(ctx.differees()[0].dv() > 35.0, "vol C : la manoeuvre depasse bien 35%");
  }

  // ── BRANCHE D : nominal ──
  // ecart 500 km -> dv = 5e5 / 86400 = 5.787 m/s ; > tolerance, < 35% -> execute.
  {
    using namespace ares::vol;
    Etat nav(Vec3{500000, 0, 0}, Vec3{0, 0, 0}, /*sigma3=*/2000.0);
    Contexte ctx(nav, Cible(cible_pos, tolerance), Reserves(100.0));
    const double dv_attendu = 500000.0 / 86400.0;   // = |ecart| / tau
    sequence_correction(ctx);
    CHECK(ctx.alertes().empty(), "vol D : aucune alerte");
    CHECK(ctx.differees().empty(), "vol D : rien differe");
    CHECK(ctx.executees().size() == 1, "vol D : une manoeuvre executee");
    CHECK_NEAR(ctx.executees()[0].dv(), dv_attendu, 1e-9, "vol D : dv = |ecart|/tau");
    CHECK_NEAR(ctx.dv_restant(), 100.0 - dv_attendu, 1e-9, "vol D : reserves = 100 - dv");
    CHECK(ctx.journal().size() == 1, "vol D : une ligne de journal de bord");
    CHECK(ctx.journal()[0].find("Correction executee") != std::string::npos,
          "vol D : journal de bord trace la correction");
  }

  std::printf("\ntest_api_sol : %d OK, %d ECHEC\n", g_ok, g_ko);
  return g_ko == 0 ? 0 : 1;
}

#endif  // SP_STANDALONE_TESTS
