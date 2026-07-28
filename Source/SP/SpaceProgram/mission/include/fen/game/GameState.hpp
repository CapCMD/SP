// fen/game/GameState.hpp — l'état complet de la partie [carte M2/M7]
//
// LE POINT D'ASSEMBLAGE : tous les systèmes à état vivent ici, et NULLE PART
// ailleurs. C'est ce que la frontière UE5 (SPGameSubsystem) possède, ce que la
// sauvegarde sérialise, ce que le hash d'état couvre. Le ConsequenceEngine
// [GDD 10.4] est ici parce qu'il écrit dans PLUSIEURS systèmes à la fois
// (budget, carrière, catalogue, arbre) — aucun module ne connaît les autres.
#pragma once
#include <cstdint>
#include "fen/career/Career.hpp"
#include "fen/economy/Economy.hpp"
#include "fen/env/Debris.hpp"
#include "fen/env/SpaceWeather.hpp"
#include "fen/game/GameClock.hpp"
#include "fen/mission/Crew.hpp"
#include "fen/mission/Mail.hpp"
#include "fen/mission/MissionFsm.hpp"
#include "fen/reliability/Reliability.hpp"
#include "fen/rel/Relativity.hpp"
#include "fen/save/Save.hpp"
#include "fen/station/Novellus.hpp"
#include "fen/tech/TechTree.hpp"

namespace fen::game {

// Modes de difficulté [GDD 2] : SEULEMENT le niveau d'assistance, JAMAIS la
// physique. Bascule Normal -> Pro unidirectionnelle et irréversible [GDD 2.3].
struct DifficultyMode {
  bool pro{false};
  void switch_to_pro() { pro = true; }   // pas de chemin inverse : assumé
};

struct GameState {
  std::uint64_t seed{};              // graine racine (Rng::substream par système)
  GameClock clock;
  DifficultyMode difficulty;

  career::CareerState career;
  career::Character   character;
  career::Notebook    notebook;

  // FINANCES v1.2 [GDD 13] : l'autorité économique, à l'échelle réelle (M€).
  // budget d'agence, réserve, chaîne de fin de partie. `treasury` (M$) reste en
  // place pour les modules hérités mais ne pilote plus l'économie native.
  economy::AgencyFinance  finance;
  economy::Treasury       treasury;
  economy::ResourceLedger ledger;

  tech::TechTree      tree;
  tech::ResearchQueue research;

  station::Station station;

  mission::MissionCatalog     catalog;
  std::vector<mission::Mission> missions;
  mission::CrewMissionSlot    crew_slot;
  // LE SEUL CANAL D'ARRIVÉE D'UN CONTRAT [GDD 10.2] : sans mail notifié, un
  // contrat du catalogue reste invisible même si ses quatre verrous sont levés.
  mission::MailInbox          inbox;

  reliability::ReliabilityDatabase reliability_db;

  // L'environnement comme ACTEUR de mission [GDD 7.7, 7.8] : le cycle solaire
  // pilote la traînée, la traînée nettoie (ou non) les couloirs pollués.
  env::SolarCycle        solar;
  env::DebrisEnvironment debris;

  rel::DualClock dual_clock;         // bord/Terre — divergent en fin de jeu seulement

  explicit GameState(WorldEpoch epoch, std::uint64_t seed_)
      : seed(seed_), clock(epoch) {}

  // --- LE TICK MONDE ---------------------------------------------------------
  // Appelé par la frontière UE5 avec le temps réel de la frame. Chaque système
  // avance en SOUS-PAS FIXES — l'accélération ne change que le nombre de pas.
  void tick(double real_dt_s) {
    const int steps = clock.advance(real_dt_s);
    const double dt_days = clock.dt_step_days();
    const auto fx = station::effects(station);
    for (int i = 0; i < steps; ++i) {
      const double now = clock.now_days();
      treasury.tick(now);
      research.tick(tree, dt_days * fx.research_speed);
      // Hors mission relativiste : temps propre == temps de jeu [GDD 6.7.4].
      character.age_by_proper_time(dt_days * cst::DAY);
      // L'environnement vieillit avec le monde : les couloirs LEO se nettoient,
      // les couloirs hauts restent pollués [GDD 7.8].
      debris.tick(dt_days, solar.activity01(clock.now_epoch()));
      if (treasury.dismissed) { /* fin de partie : licenciement [GDD 13.2] */ }
    }
    // ARES notifie ce qui vient de devenir jouable [GDD 4.2, 10.2]. Hors
    // sous-pas : le courrier est un événement de programme, pas de physique.
    mission::deliver_unlocked_contracts(inbox, catalog, career, tree,
                                        treasury.available_musd(), &station,
                                        clock.now_days());
  }

  // --- ConsequenceEngine [GDD 10.3-10.4] -------------------------------------
  // Anomalie -> triple lecture appliquée à TOUS les systèmes concernés.
  void apply_anomaly(mission::Mission& m, const mission::AnomalyEvent& raw) {
    mission::AnomalyEvent ev = raw;
    // DÉBRIS D'ABORD : la fragmentation est un FAIT physique, et c'est elle qui
    // détermine si le modificateur « création massive de débris » s'applique
    // [GDD 10.3]. On ne coche donc pas la case à la main : on compte.
    if (ev.breakup_mass_kg > 0.0 && ev.breakup_alt_km > 0.0) {
      const auto kind = ev.breakup_is_collision ? env::BreakupKind::Collision
                                                : env::BreakupKind::Explosion;
      debris.add_breakup(m.contract.id, ev.breakup_alt_km, ev.breakup_mass_kg,
                         kind, clock.now_days());
      const double n = env::fragment_count(ev.breakup_mass_kg, kind);
      // Seuil DÉCLARÉ : au-delà de 100 objets catalogables, ou dès qu'ils sont
      // déposés dans un couloir qui ne se nettoiera pas, la création est
      // « massive » au sens du GDD.
      const double act = solar.activity01(clock.now_epoch());
      const bool durable =
          env::orbital_lifetime_days(ev.breakup_alt_km, env::B_FRAGMENT_DEFAULT,
                                     act) > 60.0 * 365.25;
      if (n >= 100.0 || (n >= 10.0 && durable)) ev.modifiers.massive_debris = true;
    }
    ev.severity = mission::apply_modifiers(ev.severity, ev.modifiers);
    m.record_anomaly(ev);
    const auto c = mission::consequences_for(ev);

    // programmatique : budget + confiance + disponibilité des contrats.
    // La pénalité s'impute sur les FINANCES v1.2 (autorité), via l'engagement
    // (trésorerie puis réserve) — un échec coûte réellement [GDD 10.3].
    finance.engage(c.budget_penalty_frac * m.contract.terms.budget_musd);
    treasury.income(-c.budget_penalty_frac * m.contract.terms.budget_musd);  // hérité
    mission::apply_to_career(c, career, clock.now_days());
    for (auto& e : catalog.entries()) {
      if (e.contract.family != m.contract.family) continue;
      if (c.mission_family_suspended) e.suspended = true;
      if (c.contract_delay_days > 0.0)
        e.available_after_days = clock.now_days() + c.contract_delay_days;
    }
    // technique : requalification des nœuds impliqués [GDD 10.4]
    for (const auto& id : c.requalify_tech) tech::requalify(tree, id);
    // humain : décès opérationnel du PERSONNAGE = Game Over irrévocable
    if (ev.severity == mission::Severity::Catastrophe && c.game_over) {
      character.alive = false;
      character.operational_death = true;
    }
  }

  // --- Sauvegarde [M7] -------------------------------------------------------
  // V1 : les systèmes cœur. Les blocs Session/FlightPlan (vols en cours) ont
  // leur propre archive (io/Fpl) et se raccrochent ici en V2.
  void save(save::Writer& w) const {
    w.u64(seed);
    w.f64(clock.sim_time_s());
    w.f64(clock.world_epoch().creation.tdb);
    w.boolean(difficulty.pro);
    w.i32(static_cast<std::int32_t>(career.rank));
    w.f64(career.score);
    w.f64(career.confidence_ares);
    w.boolean(career.promotion_frozen);
    w.f64(career.frozen_until_days);
    w.f64(character.age_bio_s);
    w.f64(character.birth_world_s);
    w.boolean(character.alive);
    w.boolean(character.operational_death);
    w.f64(treasury.balance_musd);
    w.f64(treasury.target_musd);
    w.f64(treasury.reserve_musd);
    w.f64(treasury.days_in_crisis);
    // FINANCES v1.2 [GDD 13] : l'économie native persiste.
    w.f64(finance.treasury_me);
    w.f64(finance.reserve_me);
    w.f64(finance.reserve_target_me);
    w.f64(finance.days_low_reserve);
    w.i32(static_cast<std::int32_t>(finance.stage));
    w.boolean(finance.suspended);
    w.f64(dual_clock.t_earth);
    w.f64(dual_clock.tau_board);
    w.vec(notebook.entries, [](save::Writer& w2, const career::NotebookEntry& e) {
      w2.str(e.title); w2.str(e.body); w2.f64(e.date_days); w2.str(e.mission_ref);
    });
    // La POLLUTION ORBITALE est un état persistant du monde [GDD 10.4] : elle
    // survit à la sauvegarde, sinon un échec grave s'effacerait au rechargement.
    w.vec(debris.clouds(), [](save::Writer& w2, const env::DebrisCloud& c) {
      w2.str(c.origin); w2.f64(c.alt_km); w2.f64(c.n_objects);
      w2.f64(c.ballistic_coef); w2.f64(c.created_days); w2.f64(c.n_initial);
    });
    // La boîte mail EST la mémoire des contrats notifiés : sans elle, un
    // rechargement rouvrirait des contrats déjà traités [GDD 10.2].
    w.vec(inbox.messages(), [](save::Writer& w2, const mission::MailMessage& m) {
      w2.str(m.id); w2.i32(static_cast<std::int32_t>(m.kind)); w2.str(m.from);
      w2.str(m.subject); w2.str(m.body); w2.f64(m.date_days);
      w2.boolean(m.read); w2.str(m.contract_id); w2.boolean(m.answered);
    });
    // ═══ LES MISSIONS EN COURS [GDD 4.1] ═══
    // Elles ne se sauvegardaient PAS. Tant qu'un vol se déroulait entre deux
    // clics, la fenêtre pour s'en apercevoir était nulle ; depuis que le vol
    // DURE (FlightTimeline.hpp), une croisière vers Mars couvre 259 jours de
    // jeu — quitter au menu (qui sauvegarde) effaçait la mission en silence.
    // Ne sont écrits que les FAITS : l'identité du contrat (le catalogue est
    // reconstruit par la graine, on réapparie par id), l'état FSM et sa date,
    // la durée de transit figée au feu vert, et l'issue du vol. `phase` n'y est
    // pas : elle se DÉRIVE de la chronologie à chaque frame.
    w.vec(missions, [](save::Writer& w2, const mission::Mission& m) {
      w2.str(m.contract.id);
      w2.i32(static_cast<std::int32_t>(m.state));
      w2.f64(m.state_entered_days);
      w2.f64(m.tof_days);
      w2.i32(static_cast<std::int32_t>(m.worst_severity));
      w2.boolean(m.any_anomaly);
      w2.boolean(m.flight_flown);
      w2.boolean(m.flight_success);
      w2.boolean(m.flight_has_anomaly);
      // La navigation RÉELLEMENT obtenue est un fait du vol au même titre que
      // l'issue : tirée une fois au feu vert, elle ne se retire pas.
      w2.boolean(m.nav_evaluee);
      w2.f64(m.nav_dv_required);
      w2.f64(m.nav_miss_km);
      // ═══ L'ÉTAT VRAI D'UN VOL EN COURS ═══ [GDD 7.4, 8.4]
      // Il ne s'écrivait pas, et tant que la campagne de correction était
      // conduite automatiquement au feu vert ça ne se voyait pas : l'issue était
      // déjà décidée, les deux chiffres ci-dessus la portaient. Depuis que les
      // rendez-vous se tiennent EN VOL, c'est cet état qui porte l'issue — une
      // croisière rechargée perdrait sinon les corrections que le joueur a
      // commandées de sa main, et quitter au menu (qui sauvegarde) les
      // effacerait. Même piège que les missions en vol non sérialisées, qu'on ne
      // repaiera pas une troisième fois.
      w2.boolean(m.vol_vrai_valide);
      w2.f64(m.vol_vrai_t_days);
      for (int k = 0; k < 3; ++k) w2.f64(m.vol_vrai_r[k]);
      for (int k = 0; k < 3; ++k) w2.f64(m.vol_vrai_v[k]);
      for (int k = 0; k < 3; ++k) w2.f64(m.nav_connu_dv[k]);
      w2.f64(m.nav_sigma_r);
      w2.f64(m.nav_sigma_v);
      w2.f64(m.tcm_dv_depense);
      w2.i32(static_cast<std::int32_t>(m.tcm_faits));
      // LE RYTHME DE MESURE CHOISI [GDD 8.6] : ce qui a été acheté en vol, et
      // l'arc que la solution courante exploite. Sans eux, un vol rechargé
      // rembourserait l'écoute déjà payée et repartirait plus aveugle.
      w2.f64(m.poursuite_jours);
      w2.f64(m.arc_poursuite_j);
      // Le logiciel EMBARQUÉ est un fait du vol au même titre que la navigation :
      // figé au feu vert, il décide de l'issue [GDD 15.5]. Ne pas l'écrire ferait
      // qu'un vol rechargé volerait sans le code qu'il transporte.
      w2.boolean(m.code_embarque);
      w2.boolean(m.code_non_couvert);
      w2.f64(m.code_couverture);
      w2.i32(static_cast<std::int32_t>(m.vol_conduit_par));
      save_anomaly(w2, m.flight_anomaly);
    });
  }
  // L'anomalie EN ATTENTE DE DÉBRIEF est le seul événement dont les
  // conséquences n'ont pas encore été appliquées : elle doit survivre, sinon un
  // échec rechargé se conclurait sans dommage. Le JOURNAL des anomalies passées
  // (`Mission::anomalies`) n'est pas écrit — DÉCLARÉ [GDD 6.8] : ses effets ont
  // déjà été appliqués aux systèmes, et ceux-là persistent (débris, confiance,
  // carrière). Ce qu'on perdrait est une trace d'affichage, pas un fait.
  static void save_anomaly(save::Writer& w, const mission::AnomalyEvent& a) {
    w.str(a.mission_id); w.str(a.what);
    w.i32(static_cast<std::int32_t>(a.severity));
    const mission::SeverityModifiers& x = a.modifiers;
    w.boolean(x.human_lethal_exposure); w.boolean(x.primary_objective_lost);
    w.boolean(x.unique_vehicle_lost);   w.boolean(x.massive_debris);
    w.boolean(x.player_error_causal);   w.boolean(x.repeated_anomaly);
    w.boolean(x.brilliant_recovery);
    w.vec(a.tech_involved, [](save::Writer& w2, const std::string& s) { w2.str(s); });
    w.f64(a.date_days);
    w.f64(a.breakup_mass_kg); w.f64(a.breakup_alt_km);
    w.boolean(a.breakup_is_collision);
  }
  static mission::AnomalyEvent load_anomaly(save::Reader& r) {
    mission::AnomalyEvent a;
    a.mission_id = r.str(); a.what = r.str();
    a.severity = static_cast<mission::Severity>(r.i32());
    mission::SeverityModifiers& x = a.modifiers;
    x.human_lethal_exposure = r.boolean(); x.primary_objective_lost = r.boolean();
    x.unique_vehicle_lost = r.boolean();   x.massive_debris = r.boolean();
    x.player_error_causal = r.boolean();   x.repeated_anomaly = r.boolean();
    x.brilliant_recovery = r.boolean();
    a.tech_involved = r.vec<std::string>([](save::Reader& r2) { return r2.str(); });
    a.date_days = r.f64();
    a.breakup_mass_kg = r.f64(); a.breakup_alt_km = r.f64();
    a.breakup_is_collision = r.boolean();
    return a;
  }
  bool load(save::Reader& r) {
    if (!r.ok()) return false;
    seed = r.u64();
    const double sim_s = r.f64();
    (void)r.f64();                       // creation epoch : déjà dans clock
    clock.restore(sim_s);
    difficulty.pro = r.boolean();
    career.rank = static_cast<career::Rank>(r.i32());
    career.score = r.f64();
    career.confidence_ares = r.f64();
    career.promotion_frozen = r.boolean();
    career.frozen_until_days = r.f64();
    character.age_bio_s = r.f64();
    character.birth_world_s = r.f64();
    character.alive = r.boolean();
    character.operational_death = r.boolean();
    treasury.balance_musd = r.f64();
    treasury.target_musd = r.f64();
    treasury.reserve_musd = r.f64();
    treasury.days_in_crisis = r.f64();
    finance.treasury_me = r.f64();
    finance.reserve_me = r.f64();
    finance.reserve_target_me = r.f64();
    finance.days_low_reserve = r.f64();
    finance.stage = static_cast<economy::FinancialStage>(r.i32());
    finance.suspended = r.boolean();
    dual_clock.t_earth = r.f64();
    dual_clock.tau_board = r.f64();
    notebook.entries = r.vec<career::NotebookEntry>([](save::Reader& r2) {
      career::NotebookEntry e;
      e.title = r2.str(); e.body = r2.str();
      e.date_days = r2.f64(); e.mission_ref = r2.str();
      return e;
    });
    debris.clouds_mut() = r.vec<env::DebrisCloud>([](save::Reader& r2) {
      env::DebrisCloud c;
      c.origin = r2.str(); c.alt_km = r2.f64(); c.n_objects = r2.f64();
      c.ballistic_coef = r2.f64(); c.created_days = r2.f64(); c.n_initial = r2.f64();
      return c;
    });
    inbox.messages_mut() = r.vec<mission::MailMessage>([](save::Reader& r2) {
      mission::MailMessage m;
      m.id = r2.str();
      m.kind = static_cast<mission::MailKind>(r2.i32());
      m.from = r2.str(); m.subject = r2.str(); m.body = r2.str();
      m.date_days = r2.f64(); m.read = r2.boolean();
      m.contract_id = r2.str(); m.answered = r2.boolean();
      return m;
    });
    // LES MISSIONS EN COURS. Le CONTRAT n'est pas relu : il est RÉAPPARIÉ depuis
    // le catalogue par son id — le catalogue est reconstruit par la graine avant
    // le chargement, et c'est déjà la doctrine employée pour son état
    // (`suspended`/`available_after_days`). Une mission dont le contrat n'existe
    // plus dans le catalogue est ABANDONNÉE plutôt que ressuscitée à moitié :
    // une mission sans contrat n'a ni objectif, ni budget, ni prérequis.
    struct MissionSave {
      std::string id; std::int32_t state; double entered, tof;
      std::int32_t worst; bool any, flown, success, has_anom;
      bool nav_eval; double nav_dv, nav_miss;
      bool vrai_ok; double vrai_t, vrai_r[3], vrai_v[3], connu_dv[3];
      double sig_r, sig_v, tcm_dv; std::int32_t tcm_n;
      double poursuite_j, arc_j;
      bool code_bord, code_non_couvert; double code_cov; std::int32_t conduite;
      mission::AnomalyEvent anom;
    };
    const auto sauves = r.vec<MissionSave>([](save::Reader& r2) {
      MissionSave s;
      s.id = r2.str(); s.state = r2.i32(); s.entered = r2.f64(); s.tof = r2.f64();
      s.worst = r2.i32(); s.any = r2.boolean(); s.flown = r2.boolean();
      s.success = r2.boolean(); s.has_anom = r2.boolean();
      s.nav_eval = r2.boolean(); s.nav_dv = r2.f64(); s.nav_miss = r2.f64();
      s.vrai_ok = r2.boolean(); s.vrai_t = r2.f64();
      for (int k = 0; k < 3; ++k) s.vrai_r[k] = r2.f64();
      for (int k = 0; k < 3; ++k) s.vrai_v[k] = r2.f64();
      for (int k = 0; k < 3; ++k) s.connu_dv[k] = r2.f64();
      s.sig_r = r2.f64(); s.sig_v = r2.f64();
      s.tcm_dv = r2.f64(); s.tcm_n = r2.i32();
      s.poursuite_j = r2.f64(); s.arc_j = r2.f64();
      s.code_bord = r2.boolean(); s.code_non_couvert = r2.boolean();
      s.code_cov = r2.f64(); s.conduite = r2.i32();
      s.anom = load_anomaly(r2);
      return s;
    });
    missions.clear();
    for (const auto& s : sauves) {
      const mission::CatalogEntry* src = nullptr;
      for (const auto& e : catalog.entries())
        if (e.contract.id == s.id) { src = &e; break; }
      if (!src) continue;
      mission::Mission m;
      m.contract = src->contract;
      m.state = static_cast<mission::MissionState>(s.state);
      m.state_entered_days = s.entered;
      m.tof_days = s.tof;
      m.worst_severity = static_cast<mission::Severity>(s.worst);
      m.any_anomaly = s.any;
      m.flight_flown = s.flown;
      m.flight_success = s.success;
      m.flight_has_anomaly = s.has_anom;
      m.nav_evaluee = s.nav_eval;
      m.nav_dv_required = s.nav_dv;
      m.nav_miss_km = s.nav_miss;
      m.vol_vrai_valide = s.vrai_ok;
      m.vol_vrai_t_days = s.vrai_t;
      for (int k = 0; k < 3; ++k) {
        m.vol_vrai_r[k] = s.vrai_r[k];
        m.vol_vrai_v[k] = s.vrai_v[k];
        m.nav_connu_dv[k] = s.connu_dv[k];
      }
      m.nav_sigma_r = s.sig_r;
      m.nav_sigma_v = s.sig_v;
      m.tcm_dv_depense = s.tcm_dv;
      m.tcm_faits = static_cast<int>(s.tcm_n);
      m.poursuite_jours = s.poursuite_j;
      m.arc_poursuite_j = s.arc_j;
      m.code_embarque = s.code_bord;
      m.code_non_couvert = s.code_non_couvert;
      m.code_couverture = s.code_cov;
      m.vol_conduit_par = static_cast<int>(s.conduite);
      m.flight_anomaly = s.anom;
      // `phase` reste dérivée : elle sera recalculée dès le premier tick.
      missions.push_back(std::move(m));
    }
    return r.ok();
  }
  std::uint64_t hash() const {
    save::Writer w;
    save(w);
    return save::state_hash(w);
  }
};

} // namespace fen::game
