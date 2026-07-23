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
