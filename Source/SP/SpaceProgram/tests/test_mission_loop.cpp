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

  // ═══ CE QUE PÈSE UN ÉQUIPAGE [GDD 9.4, 6.1, 5.10] ═══
  // `Crew.hpp` calculait un budget vital que RIEN ne consommait : un vol habité
  // de deux ans emportait le poids d'un vol de deux semaines. Ces oracles
  // vérifient que la masse existe, qu'elle est DÉRIVÉE, et qu'elle se paie.
  {
    ephem::StandishEphemeris eph;
    const Epoch t2026{epoch_from_iso("2026-01-01T00:00:00").tdb};

    // ── L'ALLER-RETOUR EST LA PÉRIODE SYNODIQUE, ET ELLE SE CALCULE ──
    mission::Mission mh;
    mh.contract.id = "M-CREW";
    mh.contract.family = "mars_habite";
    mh.contract.crewed = true;
    const double rt = mission::crew_round_trip_days(mh, t2026, eph);
    std::printf("     aller-retour habite Terre-Mars : %.1f jours\n", rt);
    // Période synodique Terre-Mars publiée : 779,9 j. On l'exige à 1 %, et elle
    // n'est écrite NULLE PART dans le code — elle sort des deux états
    // héliocentriques par vis-viva puis Kepler.
    CHECK(std::fabs(rt - 779.9) / 779.9 < 0.01,
          "equipage : l'aller-retour retrouve la periode synodique publiee (779,9 j)");

    // Une mission ROBOTIQUE n'a pas d'aller-retour d'équipage : personne à bord.
    mission::Mission mr;
    mr.contract.family = "mars";
    mr.contract.crewed = false;
    CHECK(mission::crew_round_trip_days(mr, t2026, eph) == 0.0,
          "equipage : une mission robotique n'a pas de duree d'occupation");

    // ── LA MASSE : 6 personnes pendant ~780 jours, ce n'est pas rien ──
    const mission::VitalBudget sans =
        mission::crew_consumables("mars_habite", rt, mission::RecyclingLoops::none());
    const mission::VitalBudget iss =
        mission::crew_consumables("mars_habite", rt, mission::RecyclingLoops::iss());
    const mission::VitalBudget ferme =
        mission::crew_consumables("mars_habite", rt, mission::RecyclingLoops::advanced());
    std::printf("     vivres Mars habite (6 pers, %.0f j) : %.1f t sans recyclage,"
                " %.1f t ISS, %.1f t quasi ferme\n",
                rt, sans.total_kg() / 1000.0, iss.total_kg() / 1000.0,
                ferme.total_kg() / 1000.0);
    CHECK(sans.total_kg() > 25000.0 && sans.total_kg() < 40000.0,
          "equipage : sans recyclage, les vivres pesent des dizaines de tonnes");
    CHECK(iss.total_kg() < sans.total_kg() * 0.55,
          "equipage : le recyclage ISS divise la masse par presque deux [GDD 5.10]");
    CHECK(ferme.total_kg() < iss.total_kg(),
          "equipage : le recyclage quasi ferme fait encore mieux");
    // LA NOURRITURE NE SE RECYCLE PAS : c'est le plancher, et il reste identique
    // dans les trois cas — un modèle qui ferait tomber la nourriture serait faux.
    CHECK(std::fabs(sans.food_kg - ferme.food_kg) < 1e-6,
          "equipage : la nourriture ne se recycle jamais [GDD 9.4]");

    // Une mission robotique ne porte pas un gramme de vivres.
    CHECK(mission::crew_consumables("mars", rt, mission::RecyclingLoops::none()).total_kg() == 0.0,
          "equipage : une mission robotique n'emporte aucun consommable");

    // ── ET TSIOLKOVSKY LA PAIE ──
    // LES TERMES SONT CEUX DE LA FAMILLE, y compris l EFFECTIF que l objectif
    // demande [GDD 3.1]. Les poser a la main un par un laissait `crew_required`
    // a zero — un equipage fantome : pas de vivres, pas de dose, des reserves
    // inepuisables. Cinq oracles de ce fichier ont mordu la-dessus le jour ou
    // l effectif a quitte la table par famille pour rejoindre le contrat.
    mh.contract.terms = mission::contract_terms_for_family("mars_habite");

    mission::MissionPlan sec = plan_viable();   // équipage ignoré (l'ancien monde)
    sec.crew_round_trip_days = 0.0;
    sec.crew_loops = mission::RecyclingLoops::none();
    sec.evaluate(mh);
    // La famille n'a PAS de séjour datée ici ⇒ repli sur le séjour par défaut :
    // le plan « sec » emporte quand même un équipage, mais pour 30 jours.
    mission::MissionPlan avec = plan_viable();
    avec.crew_round_trip_days = rt;
    avec.crew_loops = mission::RecyclingLoops::iss();
    avec.evaluate(mh);
    std::printf("     masse au decollage : %.1f t (sejour 30 j) -> %.1f t (aller-retour reel)\n",
                sec.assessment.m0_kg / 1000.0, avec.assessment.m0_kg / 1000.0);
    CHECK(avec.vital.total_kg() > sec.vital.total_kg(),
          "equipage : une mission plus longue emporte plus de vivres");
    CHECK(avec.assessment.m0_kg > sec.assessment.m0_kg,
          "equipage : les vivres pesent au decollage — Tsiolkovsky les paie [GDD 6.1]");

    // LE RECYCLAGE ACHÈTE DE LA MASSE AU DÉCOLLAGE : c'est ce qui rend la
    // branche 4 « aussi importante que le moteur » [GDD 5.10, 19.1].
    mission::MissionPlan avec_ferme = plan_viable();
    avec_ferme.crew_round_trip_days = rt;
    avec_ferme.crew_loops = mission::RecyclingLoops::advanced();
    avec_ferme.evaluate(mh);
    std::printf("     recyclage quasi ferme : %.1f t au decollage (%.1f t gagnees)\n",
                avec_ferme.assessment.m0_kg / 1000.0,
                (avec.assessment.m0_kg - avec_ferme.assessment.m0_kg) / 1000.0);
    CHECK(avec_ferme.assessment.m0_kg < avec.assessment.m0_kg,
          "equipage : rechercher le recyclage quasi ferme ALLEGE le decollage");

    // LE CONTRAT N'EST PAS TOUCHÉ : les vivres, la coque et le blindage sont des
    // conséquences de l'architecture, pas des exigences du client. On compare a
    // LA TABLE et non a un littéral — la valeur a change le jour ou l habitat a
    // quitte le contrat pour rejoindre les decisions d architecte [GDD 3.1], et
    // un oracle qui epinglait « 20000 » testait le chiffre au lieu de la regle.
    CHECK(mh.contract.terms.payload_kg ==
              mission::contract_terms_for_family("mars_habite").payload_kg,
          "equipage : la charge utile du CONTRAT reste celle du client");
    CHECK(avec.masse_habitat_kg_ > mh.contract.terms.payload_kg,
          "3.1 : ... et la coque que l architecte deduit pese PLUS qu elle");

    // ═══ LE VERROU DES RADIATIONS [GDD 6.6, 19.1, 19.7] ═══
    // `env/Radiation.hpp` etait un modele complet, ancre sur l'Annexe B, et sans
    // AUCUN consommateur : [GDD 7.7] declare l'environnement « acteur de
    // mission », il n'etait que decor.
    {
      // (a) LA SURFACE A BLINDER EST DERIVEE, pas saisie : 6 personnes x 25 m3,
      // cylindre L = 2D. On verifie la geometrie, pas un chiffre magique.
      const double s6 = mission::surface_habitat_m2(6);
      const double v6 = 6 * mission::VOLUME_HABITABLE_M3_PAR_PERSONNE;
      const double r6 = std::cbrt(v6 / (4.0 * cst::PI));
      CHECK(std::fabs(s6 - (2.0 * cst::PI * r6 * r6 + 8.0 * cst::PI * r6 * r6)) < 1e-9,
            "radiations : la surface d habitat est celle du cylindre L=2D");
      CHECK(mission::surface_habitat_m2(0) == 0.0,
            "radiations : pas d equipage, rien a blinder");
      // Elle croit comme n^(2/3) : doubler l equipage ne double pas la surface.
      CHECK(mission::surface_habitat_m2(12) < 2.0 * s6,
            "radiations : la surface croit en n^(2/3), pas lineairement");

      // (b) LE BLINDAGE PESE, ET C'EST BRUTAL — la raison pour laquelle [GDD 6.6]
      // parle d'un VERROU et non d'une option.
      const double m20 = mission::masse_blindage_kg(6, 20.0);
      const double m5  = mission::masse_blindage_kg(6, 5.0);
      std::printf("     blindage 6 pers : surface %.0f m2 -> %.1f t a 5 g/cm2,"
                  " %.1f t a 20 g/cm2\n", s6, m5 / 1000.0, m20 / 1000.0);
      CHECK(m20 > 25000.0 && m20 < 45000.0,
            "radiations : 20 g/cm2 autour de 6 personnes pesent des dizaines de tonnes");
      CHECK(std::fabs(m20 - 4.0 * m5) < 1e-6,
            "radiations : la masse est LINEAIRE en densite surfacique");
      CHECK(mission::masse_blindage_kg(6, 0.0) == 0.0,
            "radiations : ne rien blinder ne coute rien (et c est un choix)");

      // (c) LA DOSE D'UN ALLER-RETOUR MARTIEN retrouve l'ANNEXE B, qui annonce
      // ~0,3-0,7 Sv de GCR sans blindage lourd. On integre au minimum solaire
      // (le PIRE cas pour les GCR : l'heliosphere ne les repousse plus).
      const double act_min = 0.0;                       // minimum solaire
      const env::Shielding nu{0.0, 0.5};
      const env::Shielding leger{5.0, 1.0};             // 5 g/cm2, polyethylene
      const env::Shielding lourd{20.0, 1.0};
      const double d_nu = mission::dose_chronique_sv(rt, mission::FlightPhase::TransferCruise,
                                                     nu, act_min);
      const double d_leg = mission::dose_chronique_sv(rt, mission::FlightPhase::TransferCruise,
                                                      leger, act_min);
      const double d_lourd = mission::dose_chronique_sv(rt, mission::FlightPhase::TransferCruise,
                                                        lourd, act_min);
      std::printf("     dose aller-retour Mars (%.0f j, minimum solaire) : %.2f Sv nu,"
                  " %.2f Sv a 5 g/cm2, %.2f Sv a 20 g/cm2  (limite carriere %.1f Sv)\n",
                  rt, d_nu, d_leg, d_lourd, env::CAREER_DOSE_LIMIT_SV);
      CHECK(d_nu > 0.7 && d_nu < 1.1,
            "radiations : un aller-retour nu frole la limite de carriere");
      CHECK(d_lourd < d_leg && d_leg < d_nu,
            "radiations : blinder REDUIT la dose, monotone");
      // ET LE GCR NE SE BLINDE QUASI PAS : quadrupler l'epaisseur ne divise pas
      // la dose par quatre — c'est le plancher de `gcr_transmission`, et c'est ce
      // qui rend le probleme insoluble par la seule masse.
      CHECK(d_lourd > 0.5 * d_nu,
            "radiations : le GCR resiste au blindage (plancher de spallation)");

      // (d) LE CYCLE SOLAIRE COMPTE, et dans le BON SENS : au maximum solaire
      // l'heliosphere repousse les GCR, donc la dose chronique BAISSE.
      const double d_max = mission::dose_chronique_sv(
          rt, mission::FlightPhase::TransferCruise, nu, 1.0);
      CHECK(d_max < d_nu,
            "radiations : au MAXIMUM solaire, la dose GCR est plus BASSE [anti-correlation]");

      // (e) OU L'ON EST DECIDE CE QU'ON PREND.
      CHECK(mission::facteur_geometrie_ciel(mission::FlightPhase::Ground) == 0.0,
            "radiations : au sol, l atmosphere protege");
      CHECK(mission::facteur_geometrie_ciel(mission::FlightPhase::LeoOps) <
                mission::facteur_geometrie_ciel(mission::FlightPhase::TransferCruise),
            "radiations : le LEO est sous la magnetosphere, la croisiere ne l est pas");
      CHECK(mission::facteur_geometrie_ciel(mission::FlightPhase::SurfaceOps) < 1.0,
            "radiations : un sol planetaire masque un hemisphere");

      // (f) ET IL PESE DANS TSIOLKOVSKY, avec les vivres [GDD 6.6 arbitrage].
      mission::MissionPlan nu_plan = plan_viable();
      nu_plan.crew_round_trip_days = rt;
      nu_plan.crew_loops = mission::RecyclingLoops::iss();
      nu_plan.evaluate(mh);
      mission::MissionPlan blinde = nu_plan;
      blinde.blindage = lourd;
      blinde.evaluate(mh);
      std::printf("     masse au decollage : %.1f t sans blindage -> %.1f t a 20 g/cm2\n",
                  nu_plan.assessment.m0_kg / 1000.0, blinde.assessment.m0_kg / 1000.0);
      CHECK(blinde.masse_blindage_kg_ > 0.0 && nu_plan.masse_blindage_kg_ == 0.0,
            "radiations : le blindage du plan a une masse");
      CHECK(blinde.assessment.m0_kg > nu_plan.assessment.m0_kg,
            "radiations : protetger l equipage se paie en ergols [GDD 6.6]");

      // (g bis) ═══ LA CIBLE DE CALIBRATION DES ERUPTIONS ═══ [Annexe B, Annexe E]
      // Les parametres SPE etaient INCOHERENTS avec l'ancrage du GDD : magnitude
      // uniforme + loi log-uniforme = 41 % d'evenements au-dessus du gray, d'ou
      // ~20 Gy d'aigu sur une seule croisiere martienne (MESURE). Recalibres, ils
      // doivent tenir une cible EXPLICITE : derriere la seule coque, la dose SPE
      // d'un aller-retour martien est du MEME ORDRE que la dose GCR du meme
      // trajet. Cet oracle EST la cible — si l'un des trois parametres bouge, il
      // le dit.
      {
        const env::Shielding coque = mission::blindage_effectif(env::Shielding{0.0, 0.0});
        // Esperance de dose aigue par evenement, integree sur la magnitude.
        const int N = 20000;
        double somme = 0.0;
        for (int i = 0; i < N; ++i)
          somme += mission::dose_aigue_spe_gy((i + 0.5) / N, coque);
        const double e_par_evt = somme / N;
        // Nombre d'evenements attendus sur l'aller-retour, au taux moyen du cycle.
        const double n_evt = env::spe_rate_per_year(0.5) * (rt / 365.25);
        const double spe_sv = e_par_evt * n_evt * env::SPE_QUALITY_FACTOR;
        const double gcr_sv = mission::dose_chronique_sv(
            rt, mission::FlightPhase::TransferCruise, coque, 0.0);
        std::printf("     calibration SPE : %.1f eruptions attendues, %.3f Gy/evt derriere la coque"
                    " -> %.2f Sv de SPE contre %.2f Sv de GCR sur l aller-retour\n",
                    n_evt, e_par_evt, spe_sv, gcr_sv);
        CHECK(spe_sv > 0.25 * gcr_sv && spe_sv < 4.0 * gcr_sv,
              "calibration : la dose SPE est du MEME ORDRE que la dose GCR [Annexe B]");
        // Et les monstres sont VRAIMENT rares, ce que la version precedente
        // promettait sans le faire.
        int au_dessus = 0;
        for (int i = 0; i < N; ++i)
          if (mission::spe_unshielded_gy((i + 0.5) / N) > 1.0) ++au_dessus;
        const double p_gray = double(au_dessus) / N;
        std::printf("     calibration SPE : %.1f %% des eruptions depassent 1 Gy non blindees"
                    " (41 %% avant recalibration)\n", p_gray * 100.0);
        CHECK(p_gray < 0.10,
              "calibration : moins d une eruption sur dix depasse le gray [GDD 6.6]");
        CHECK(mission::spe_unshielded_gy(1.0) == mission::SPE_DOSE_MAX_GY,
              "calibration : la magnitude maximale reste l evenement de classe 1972");
      }

      // (g) LE COMPTEUR DE CARRIERE NE REDESCEND JAMAIS [GDD 6.6].
      env::DoseAccumulator acc;
      acc.add_chronic(0.6);
      CHECK(!acc.career_exceeded(), "radiations : 0,6 Sv, encore apte");
      acc.mission_sv = 0.0;                  // nouvelle mission
      acc.add_chronic(0.5);
      CHECK(acc.career_exceeded(),
            "radiations : deux missions cumulees depassent la limite de CARRIERE");
    }

    // Les boucles se déduisent de l'arbre, dans le bon ordre de préséance.
    CHECK(mission::loops_from_tech(false, false).water_recovery == 0.0,
          "equipage : sans recherche, aucun recyclage");
    CHECK(mission::loops_from_tech(true, false).water_recovery == 0.87,
          "equipage : recyclage_partiel -> boucles ISS");
    CHECK(mission::loops_from_tech(true, true).o2_recovery == 0.85,
          "equipage : recyclage_ferme prime sur recyclage_partiel");
  }

  std::printf("\nBOUCLE DE MISSION : %d oracles OK, %d en echec.\n", g_ok, g_ko);
  return g_ko == 0 ? 0 : 1;
}

#endif // SP_STANDALONE_TESTS
