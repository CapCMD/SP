// tests/test_mission_loop.cpp — ORACLES DE LA BOUCLE DE MISSION [GDD 4.1]
//
// Le cycle qui RELIE les systèmes. On vérifie que chaque GATE tient (on ne passe
// pas à la phase suivante sans sa condition réelle), que l'issue du vol est
// DÉTERMINISTE et cohérente avec la P(succès) évaluée, et que le débrief propage
// ses conséquences [GDD 10.4].
//
// STANDALONE UNIQUEMENT : compile avec /DSP_STANDALONE_TESTS.
#ifdef SP_STANDALONE_TESTS

#include <cmath>
#include <cstdio>

#include "app/session.hpp"
#include "fen/mission/MissionLoop.hpp"

using namespace fen;

static int g_ok = 0, g_ko = 0;
#define CHECK(cond, nom)                                                     \
  do {                                                                       \
    if (cond) { ++g_ok; }                                                    \
    else { ++g_ko; std::printf("ECHEC : %s (ligne %d)\n", nom, __LINE__); }  \
  } while (0)

// Construit un plan VIABLE pour la mission donnée : RL10 en 2 étages, revue +
// essais. Vérifié dans l'oracle « viabilite » ci-dessous.
static mission::MissionPlan plan_viable() {
  mission::MissionPlan p;
  p.program.engine_index = 0;      // RL10C-1
  p.program.launcher_index = -1;   // auto : le moins cher qui souleve
  p.program.review = true;
  p.program.test_hours = 100.0;
  p.program.dv_margin = 200.0;
  p.n_stages = 2;
  return p;
}

int main() {
  // ═══ 1. ISSUE DU VOL : déterministe et cohérente ═══
  {
    mission::Mission m;
    m.contract.id = "M-TEST";
    m.contract.family = "science";
    mission::MissionPlan plan;
    // Assessment forgé : succès CERTAIN.
    plan.assessment.p_success = 1.0;
    plan.assessment.p_launcher = 1.0; plan.assessment.p_engine = 1.0;
    plan.assessment.p_blunder = 0.0; plan.assessment.p_physics = 1.0;
    const auto a = mission::fly_mission(m, plan, 12345);
    CHECK(a.success, "issue : p_success=1 -> succes garanti");
    CHECK(!a.has_anomaly, "issue : un succes ne produit pas d anomalie");

    // Échec CERTAIN, attribué au lanceur (seule cause non nulle).
    mission::MissionPlan pf;
    pf.assessment.p_success = 0.0;
    pf.assessment.p_launcher = 0.0;   // 1-p = 1 : toute la faute au lanceur
    pf.assessment.p_engine = 1.0; pf.assessment.p_blunder = 0.0; pf.assessment.p_physics = 1.0;
    pf.assessment.m0_kg = 5000.0;
    const auto b = mission::fly_mission(m, pf, 999);
    CHECK(!b.success, "issue : p_success=0 -> echec garanti");
    CHECK(b.has_anomaly, "issue : un echec produit une anomalie [GDD 10.4]");
    CHECK(b.anomaly.mission_id == "M-TEST", "issue : l anomalie est tracee a la mission");
    CHECK(b.anomaly.breakup_mass_kg > 0.0, "issue : un echec lanceur cree des debris");
    CHECK(b.severity >= mission::Severity::Major, "issue : perte du lanceur = grave");

    // DÉTERMINISME : même graine, même issue.
    const auto b2 = mission::fly_mission(m, pf, 999);
    CHECK(b.success == b2.success && b.severity == b2.severity,
          "issue : meme graine -> meme resultat (rejouable)");

    // Une mission HABITÉE aggrave le palier (exposition humaine [GDD 10.3]).
    mission::Mission mc = m; mc.contract.crewed = true;
    const auto c = mission::fly_mission(mc, pf, 999);
    CHECK(c.anomaly.modifiers.human_lethal_exposure,
          "issue : un echec habite expose l equipage");

    // ═══ LE LOGICIEL DE VOL HORS DE SON DOMAINE [GDD 15.5, ch.10] ═══
    // « Executer hors du domaine = comportement NON COUVERT = cause d'anomalie
    // legitime. » C'est ce qui donne son prix au banc d'essai : sans cette
    // porte, acheter des heures de qualification ne changeait rien a l'issue.
    {
      // Un vol PARFAIT par ailleurs : succes certain, aucune autre cause.
      mission::Mission mcode = m;
      mcode.code_embarque = true;
      mcode.code_non_couvert = true;
      const auto d = mission::fly_mission(mcode, plan, 12345);
      CHECK(!d.success,
            "code embarque : hors domaine, un vol par ailleurs parfait ECHOUE [15.5]");
      CHECK(d.has_anomaly && d.anomaly.what.find("domaine de validite") != std::string::npos,
            "code embarque : ... et l anomalie NOMME la cause");
      CHECK(d.anomaly.modifiers.player_error_causal,
            "code embarque : ... imputee au joueur — la fiche disait ce qu elle couvrait");
      CHECK(d.severity >= mission::Severity::Major,
            "code embarque : un comportement non couvert est GRAVE");

      // DANS son domaine : le logiciel ne coute rien. Ecrire du code n'est pas
      // une penalite — c'est l'embarquer sur un vol qu'il n'a jamais vu au banc
      // qui se paie.
      mission::Mission mbon = m;
      mbon.code_embarque = true;
      mbon.code_non_couvert = false;
      const auto e = mission::fly_mission(mbon, plan, 12345);
      CHECK(e.success && !e.has_anomaly,
            "code embarque : dans son domaine, le logiciel ne coute RIEN");

      // Sans logiciel a bord, le drapeau ne mord pas : un vol sans code ne peut
      // pas etre hors du domaine d'un code qui n'existe pas.
      mission::Mission msans = m;
      msans.code_embarque = false;
      msans.code_non_couvert = true;   // incoherent : doit rester sans effet
      CHECK(mission::fly_mission(msans, plan, 12345).success,
            "code embarque : sans code a bord, le hors-domaine n a pas de sens");

      // UN EQUIPAGE AGGRAVE LE PALIER, ici comme ailleurs [GDD 10.3].
      mission::Mission mhab = mcode; mhab.contract.crewed = true;
      CHECK(mission::fly_mission(mhab, plan, 12345).anomaly.modifiers.human_lethal_exposure,
            "code embarque : un vol habite hors domaine expose l equipage");
    }

    // La graine dérivée dépend de l'identité de la mission (rejouabilité).
    CHECK(mission::mission_seed(42, "A") != mission::mission_seed(42, "B"),
          "graine : deux missions differentes -> graines differentes");
    CHECK(mission::mission_seed(42, "A") == mission::mission_seed(42, "A"),
          "graine : stable pour une meme mission");
  }

  // ═══ 2. LES GATES : aucune transition gratuite ═══
  {
    mission::Mission m;
    m.contract.family = "science";
    m.state = mission::MissionState::Design;
    mission::MissionPlan plan;   // NON évalué

    // On ne cherche pas de fenêtre sans conception évaluée et viable.
    auto g = mission::mission_gate(m, plan, mission::MissionState::WindowSearch);
    CHECK(!g.allowed, "gate : pas de fenetre sans conception evaluee");
    CHECK(!g.reason.empty(), "gate : le refus est motive");

    // Une conception non viable bloque aussi.
    plan.assessment.ok = false; plan.assessment.why = "BUDGET";
    plan.evaluated = true;
    g = mission::mission_gate(m, plan, mission::MissionState::WindowSearch);
    CHECK(!g.allowed, "gate : une conception non viable bloque la fenetre");

    plan.assessment.ok = true;
    g = mission::mission_gate(m, plan, mission::MissionState::WindowSearch);
    CHECK(g.allowed, "gate : une conception viable ouvre la fenetre");

    // On ne LANCE pas sans qualification (revue ou essais).
    mission::Mission mq; mq.state = mission::MissionState::Qualification;
    mission::MissionPlan pq;
    CHECK(!mission::mission_gate(mq, pq, mission::MissionState::Launched).allowed,
          "gate : pas de lancement sans qualification");
    pq.program.review = true;
    CHECK(mission::mission_gate(mq, pq, mission::MissionState::Launched).allowed,
          "gate : une revue qualifie le lancement");

    // La FSM reste stricte : on ne saute pas d'états.
    CHECK(!mission::mission_gate(m, plan, mission::MissionState::Launched).allowed,
          "gate : depuis CONCEPTION, on ne saute pas au lancement");
  }

  // ═══ 3. VIABILITÉ du plan de référence ═══
  {
    fen::app::Session s;
    s.nouvelle_partie("Oracle", fen::app::ModeAide::Normal);
    s.tick(0.016);
    auto& G = *s.jeu.ares.etat;
    // accepter le contrat "science" (CAT-02).
    CHECK(s.accepter_contrat("CAT-02"), "viabilite : contrat science accepte");
    s.piloter_premiere_mission();
    mission::Mission* m = s.mission_courante();
    CHECK(m != nullptr, "viabilite : une mission est pilotee");
    s.mission_plan = plan_viable();
    s.evaluer_plan();
    CHECK(s.mission_plan.assessment.fits_mass, "viabilite : le lanceur souleve la masse");
    CHECK(s.mission_plan.assessment.fits_budget, "viabilite : dans le budget");
    CHECK(s.mission_plan.assessment.fits_schedule, "viabilite : dans les delais");
    CHECK(s.mission_plan.assessment.fits_risk, "viabilite : P(succes) suffisante");
    CHECK(s.mission_plan.assessment.ok, "viabilite : programme VIABLE");
    (void)G;
  }

  // ═══ 4. DRIVE COMPLET jusqu'à TERMINEE, budget compris ═══
  {
    fen::app::Session s;
    s.nouvelle_partie("Oracle", fen::app::ModeAide::Normal);
    s.tick(0.016);
    auto& G = *s.jeu.ares.etat;

    auto& F = s.jeu.ares.etat->finance;
    const double tresor0 = F.treasury_me;
    CHECK(s.accepter_contrat("CAT-02"), "drive : contrat accepte");
    // Le budget du contrat a ete verse a la signature (finances v1.2).
    CHECK(F.treasury_me > tresor0, "drive : le budget est verse a l acceptation");

    s.piloter_premiere_mission();
    s.mission_plan = plan_viable();

    using St = mission::MissionState;
    // Received -> Prerequisites (deja fait a l'acceptation) ; on part de Prerequisites.
    CHECK(s.mission_courante()->state == St::Prerequisites, "drive : depart en PREREQUIS");
    CHECK(s.avancer_mission().allowed, "drive : PREREQUIS -> CONCEPTION");
    CHECK(s.mission_courante()->state == St::Design, "drive : en CONCEPTION");
    CHECK(s.avancer_mission().allowed, "drive : CONCEPTION -> FENETRE (plan viable)");
    CHECK(s.mission_courante()->state == St::WindowSearch, "drive : en FENETRE");
    CHECK(s.avancer_mission().allowed, "drive : FENETRE -> QUALIFICATION");
    // COMMIT : on capture les fonds (trésorerie + réserve) juste avant le feu vert.
    const double fonds_avant = F.treasury_me + F.reserve_me;
    const double cout = s.mission_plan.assessment.cost_total;
    CHECK(s.avancer_mission().allowed, "drive : QUALIFICATION -> LANCEMENT (qualifie + paye)");
    CHECK(s.mission_courante()->state == St::Launched, "drive : LANCE");
    CHECK(std::fabs((fonds_avant - (F.treasury_me + F.reserve_me)) - cout) < 1e-6,
          "drive : le commit a preleve EXACTEMENT le cout du programme");
    CHECK(s.avancer_mission().allowed, "drive : LANCEMENT -> DEBRIEF (le vol s execute)");
    CHECK(s.mission_courante()->state == St::Debrief, "drive : en DEBRIEF");
    CHECK(s.mission_outcome_pret, "drive : l issue du vol est prete");

    const int r0 = s.jeu.agence.reussites, e0 = s.jeu.agence.echecs;
    CHECK(s.avancer_mission().allowed, "drive : DEBRIEF -> etat terminal");
    const auto fin = s.mission_courante()->state;
    CHECK(fin == St::Completed || fin == St::Failed, "drive : la mission est close");
    if (fin == St::Completed)
      CHECK(s.jeu.agence.reussites == r0 + 1, "drive : succes -> reussites +1");
    else
      CHECK(s.jeu.agence.echecs == e0 + 1, "drive : echec -> echecs +1");

    // La mission close ne se re-pilote plus.
    s.piloter_premiere_mission();
    CHECK(s.mission_courante() == nullptr || s.mission_courante()->state != St::Debrief,
          "drive : une mission close sort du pilotage");
    (void)G;
  }

  // ═══ 4. GATE DE FENÊTRE DE LANCEMENT ═══ [GDD 7.3]
  // La géométrie commande : une mission Mars ne passe en qualification que si la
  // fenêtre synodique est ouverte. Les dates de référence viennent de l'oracle
  // porkchop (test_astro_core) : optimum 2026 le 31/10, conjonction ~mi-2027.
  {
    ephem::StandishEphemeris eph;
    const Epoch bon{epoch_from_iso("2026-10-01T00:00:00").tdb};   // ~1 mois avant l'optimum
    const Epoch mauvais{epoch_from_iso("2027-06-01T00:00:00").tdb}; // entre deux fenetres

    mission::Mission mars;
    mars.contract.id = "M-MARS";
    mars.contract.family = "mars";

    const auto g_ouvert = mission::launch_window_gate(mars, bon, eph);
    CHECK(g_ouvert.allowed, "fenetre : Mars AUTORISE quand la fenetre est ouverte");

    const auto g_ferme = mission::launch_window_gate(mars, mauvais, eph);
    CHECK(!g_ferme.allowed, "fenetre : Mars REFUSE hors fenetre (conjonction)");
    CHECK(g_ferme.reason.find("fermee") != std::string::npos,
          "fenetre : le refus chiffre l'attente");

    // Une mission near-Earth n'est jamais bloquee par la geometrie interplanetaire.
    mission::Mission sat;
    sat.contract.id = "M-SAT";
    sat.contract.family = "sat";
    CHECK(mission::launch_window_gate(sat, mauvais, eph).allowed,
          "fenetre : near-Earth (sat) a une fenetre permanente");

    // NEP (poussee continue) : pas de fenetre impulsive etroite -> permanente.
    mission::Mission nep;
    nep.contract.id = "M-NEP";
    nep.contract.family = "nep";
    CHECK(mission::launch_window_gate(nep, mauvais, eph).allowed,
          "fenetre : NEP (poussee continue) n'est pas soumise au porkchop impulsif");

    // ── Δv de trajectoire REEL (Oberth) pour Mars, forfait sinon. ──
    // Familles sans fenetre : le forfait par famille est conserve a l'identique.
    mission::Mission sci;
    sci.contract.family = "science";
    CHECK(std::fabs(mission::trajectory_dv_for_mission(sci, bon, eph)
                    - mission::trajectory_dv_for_family("science")) < 1e-9,
          "traj : famille sans fenetre garde son forfait");

    // Mars a une bonne fenetre : injection LEO (~3.6) + insertion Mars + marge.
    const double dv_mars = mission::trajectory_dv_for_mission(mars, bon, eph);
    std::printf("     Δv trajectoire Mars (fenetre reelle) : %.0f m/s\n", dv_mars);
    CHECK(dv_mars > 4000.0 && dv_mars < 5600.0,
          "traj : Mars reel dans une plage physique (injection + insertion + marge)");
    CHECK(std::fabs(dv_mars - mission::trajectory_dv_for_family("mars")) > 1.0,
          "traj : le Δv Mars est CALCULE, pas le forfait");

    // Le plan Mars evalue avec l'override reflete ce Δv (pas le forfait).
    mission::MissionPlan pm = plan_viable();
    pm.dv_traj_override = dv_mars;
    mars.contract.terms.payload_kg = 2200;
    mars.contract.terms.budget_musd = 380;
    mars.contract.terms.deadline_months = 54;
    mars.contract.terms.min_success_prob = 0.80;
    pm.evaluate(mars);
    CHECK(pm.evaluated, "traj : le plan Mars s'evalue avec l'override");
    CHECK(pm.assessment.dv_design >= dv_mars,
          "traj : dv_design du plan inclut le Δv reel de la fenetre (+ marges)");
  }

  std::printf("\nBOUCLE DE MISSION : %d oracles OK, %d en echec.\n", g_ok, g_ko);
  return g_ko == 0 ? 0 : 1;
}

#endif // SP_STANDALONE_TESTS
