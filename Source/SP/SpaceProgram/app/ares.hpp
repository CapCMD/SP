// app/ares.hpp — LA COUCHE ARES (GDD v1.1) branchée sur l'agence v0.6.
//
// Migration PROGRESSIVE, pas un remplacement : app::Jeu reste la vérité pour
// l'argent, le calendrier et les vols. Cette couche possède les systèmes GDD
// (carrière, arbre 6 branches, catalogue verrouillé, Novellus, fiabilité) et se
// synchronise EN LECTURE sur l'agence à chaque mois passé :
//   argent   -> miroir de agence.tresorerie (aucun double prélèvement ici) ;
//   score    -> dérivé des réussites/échecs de l'agence ;
//   horloge  -> mois d'agence convertis en jours GameClock.
// AUCUNE dépendance vers jeu.hpp (ce fichier est inclus PAR lui) : les méthodes
// qui touchent l'agence sont des templates, résolues à l'instanciation dans
// l'UI. Les ACHATS (recherche, modules) passent par jeu.payer() côté écran :
// l'économie stricte de l'agence garde le dernier mot.
#pragma once
#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "fen/game/GameState.hpp"

namespace fen::app {

inline constexpr double ARES_MONTH_S = 30.44 * cst::DAY;

// --- Offres de modules Novellus (prix DÉCLARÉS, payés via jeu.payer) ---------
struct OffreModule {
  station::ModuleType type;
  double cout_musd;
  double puissance_kw;     // apport (modules énergétiques)
  double thermique_kw;     // apport de rejet
  const char* pourquoi;
};
inline const std::vector<OffreModule>& offres_modules() {
  static const std::vector<OffreModule> v = {
      {station::ModuleType::CrewHabitat, 120.0, 0, 0,   "capacite d'equipage, sejours prolonges"},
      {station::ModuleType::LifeSupport, 140.0, 0, 0,   "duree soutenable de presence humaine"},
      {station::ModuleType::Storage,      80.0, 0, 0,   "marges : tolerance aux retards cargo"},
      {station::ModuleType::ScienceLab,  160.0, 0, 0,   "accelere la recherche (+25 %/labo)"},
      {station::ModuleType::Workshop,    130.0, 0, 0,   "maintenance : fiabilite effective en hausse"},
      {station::ModuleType::Medical,     110.0, 0, 0,   "reduit les urgences medicales en vol"},
      {station::ModuleType::EvaAirlock,   90.0, 0, 0,   "ouvre les operations externes"},
      {station::ModuleType::Power,       180.0, 100.0, 150.0, "generation 2 : fonctions energivores"},
  };
  return v;
}

// ---------------------------------------------------------------------------
struct AresLayer {
  std::unique_ptr<game::GameState> etat;
  double dernier_mois{-1.0};
  int derniers_reussites{0}, derniers_echecs{0};
  std::vector<std::string> notifications;   // consommées par l'écran ARES

  // À appeler chaque frame (UI). Gère création, nouvelle partie, et le
  // rattrapage quand l'agence a passé un ou plusieurs mois.
  template <class AgenceT>
  void assurer(AgenceT& a, double epoch_tdb_s) {
    if (!a.creee) { etat.reset(); dernier_mois = -1.0; return; }
    if (!etat || a.mois + 0.25 < dernier_mois) {   // première fois OU reset partie
      initialiser(a.graine_agence, epoch_tdb_s - a.mois * ARES_MONTH_S);
      dernier_mois = a.mois;
      derniers_reussites = a.reussites;
      derniers_echecs = a.echecs;
      sync_lecture(a);
      return;
    }
    if (a.mois > dernier_mois + 1e-9) avancer(a);
    else sync_lecture(a);            // miroir argent/confiance même sans mois passé
  }

  bool initialisee() const { return static_cast<bool>(etat); }

  // --- interne -----------------------------------------------------------------
  void initialiser(std::uint64_t graine, double epoch_creation_tdb_s) {
    etat = std::make_unique<game::GameState>(
        game::WorldEpoch{Epoch{epoch_creation_tdb_s}}, graine);
    auto& G = *etat;

    G.career.rank = career::Rank::Stagiaire;
    G.career.confidence_ares = 70.0;
    G.character.name = "L'Architecte";
    G.character.age_bio_s = 32.0 * career::YEAR_S;
    G.character.birth_world_s = -G.character.age_bio_s;

    // FINANCES v1.2 [GDD 13] : l'autorité économique native, à l'échelle réelle.
    // Les défauts du modèle (budget ~100 Md€, réserve ~18 Md€, coûts fixes
    // ~44 Md€/an) portent l'invariant de pression d'inactivité. L'agence démarre
    // avec une réserve pleine et une trésorerie modeste.
    G.finance = economy::AgencyFinance{};
    // `treasury` (M$) hérité : neutralisé (l'économie ne passe plus par lui).
    G.treasury.fixed_costs.clear();

    seed_arbre(G.tree);
    seed_station(G.station);
    seed_catalogue(G.catalog);
    seed_fiabilite(G.reliability_db);
    livrer_courrier(G);           // ARES notifie ce qui est déjà jouable au départ
  }

  // Le facteur ARES [GDD 4.2, 10.2] : notifie les contrats dont les quatre
  // verrous sont levés. Vit hors de GameState::tick (que le chemin natif
  // n'appelle pas) — c'est ici, sur passage de mois, qu'il tourne.
  static void livrer_courrier(game::GameState& G) {
    // Budget disponible pour l'axe budgétaire du déblocage : trésorerie + réserve.
    const double dispo = G.finance.treasury_me + G.finance.reserve_me;
    mission::deliver_unlocked_contracts(G.inbox, G.catalog, G.career, G.tree,
                                        dispo, &G.station, G.clock.now_days());
  }

  // Niveaux d'ACTIVITÉ pour le modèle de recettes [GDD 13.1] : proxies déclarés.
  // Programme : recherches + missions en cours. Commercial : missions en service.
  static double program_activity(const game::GameState& G) {
    int missions_actives = 0;
    for (const auto& m : G.missions)
      if (m.state != mission::MissionState::Completed &&
          m.state != mission::MissionState::Failed &&
          m.state != mission::MissionState::Aborted) ++missions_actives;
    return std::clamp((static_cast<double>(G.research.active().size()) +
                       missions_actives) / 4.0, 0.0, 1.0);
  }
  static double commercial_activity(const game::GameState& G) {
    int en_service = 0;
    for (const auto& m : G.missions)
      if (m.state == mission::MissionState::Launched ||
          m.state == mission::MissionState::Completed) ++en_service;
    return std::clamp(en_service / 3.0, 0.0, 1.0);
  }

  // sync_lecture : la CONFIANCE et l'ARGENT ne sont plus des miroirs du prototype
  // (v1.2) — ils sont autoritaires (confiance pilotée par les issues de mission,
  // argent par `finance`). Ne reste rien à mirroir ici.
  template <class AgenceT>
  void sync_lecture(const AgenceT&) {}

  template <class AgenceT>
  void avancer(AgenceT& a) {
    auto& G = *etat;
    const double delta_mois = a.mois - dernier_mois;
    const double delta_jours = delta_mois * ARES_MONTH_S / cst::DAY;

    // horloge + vieillissement (temps propre == temps de jeu hors relativiste)
    G.clock.restore(a.mois * ARES_MONTH_S);
    G.character.age_bio_s += delta_mois * ARES_MONTH_S;

    // FINANCES v1.2 [GDD 13.2, 14.2] : un tick par mois ENTIER FRANCHI. Compté
    // par FRONTIÈRE, et non plus par `round(delta_mois)` : depuis que le temps
    // COULE [GDD 14.2], cette couche est appelée avec des avances fractionnaires,
    // et un arrondi rendait 0 tick à chaque frame — l'agence vivait gratuitement,
    // exactement ce que l'invariant de pression d'inactivité interdit. Le résultat
    // est IDENTIQUE à l'ancien pour les sauts d'un mois entier.
    {
      const double pa = program_activity(G), ca = commercial_activity(G);
      const long long b0 = static_cast<long long>(std::floor(dernier_mois));
      const long long b1 = static_cast<long long>(std::floor(a.mois));
      for (long long k = b0; k < b1; ++k) G.finance.tick_month(pa, ca);
    }

    // recherche : le labo Novellus accélère [GDD 11.6]
    const auto fx = station::effects(G.station);
    for (const auto& id : G.research.tick(G.tree, delta_jours * fx.research_speed)) {
      const tech::TechNode* n = G.tree.find(id);
      const std::string msg = "ARES RECHERCHE QUALIFIEE : " + (n ? n->name : id);
      a.log(msg);
      notifier(msg);
    }

    // score de carrière dérivé des issues de mission [GDD 3.3]
    const int dr = a.reussites - derniers_reussites;
    const int de = a.echecs - derniers_echecs;
    if (dr > 0) G.career.add_score(40.0 * dr);
    if (de > 0) G.career.score = std::max(0.0, G.career.score - 10.0 * de);
    derniers_reussites = a.reussites;
    derniers_echecs = a.echecs;

    if (G.career.promotion_ready(G.clock.now_days())) {
      G.career.promote();
      const std::string msg =
          std::string("ARES PROMOTION : ") + career::rank_name(G.career.rank);
      a.log(msg);
      notifier(msg);
    }
    sync_lecture(a);
    // La trésorerie et le rang viennent de changer : de nouveaux contrats
    // peuvent avoir franchi leurs verrous. On les notifie [GDD 4.2].
    const std::size_t avant = G.inbox.messages().size();
    livrer_courrier(G);
    for (std::size_t i = avant; i < G.inbox.messages().size(); ++i)
      notifier("ARES CONTRAT : " + G.inbox.messages()[i].subject);
    dernier_mois = a.mois;
  }

  void notifier(const std::string& s) {
    notifications.push_back(s);
    if (notifications.size() > 8) notifications.erase(notifications.begin());
  }

  // --- persistance (binaire, à côté de la sauvegarde texte de l'agence) -------
  bool sauvegarder(const std::string& chemin) const {
    if (!etat) return false;
    save::Writer w;
    etat->save(w);
    const auto& G = *etat;
    w.f64(dernier_mois);
    w.i32(derniers_reussites);
    w.i32(derniers_echecs);
    w.vec(G.tree.all(), [](save::Writer& w2, const tech::TechNode& n) {
      w2.str(n.id); w2.i32(n.trl);
    });
    w.vec(G.research.active(), [](save::Writer& w2, const tech::ResearchProject& p) {
      w2.str(p.node_id); w2.f64(p.days_done); w2.f64(p.days_total);
      w2.boolean(p.priority_program);
    });
    w.vec(G.station.modules, [](save::Writer& w2, const station::StationModule& m) {
      w2.i32(static_cast<std::int32_t>(m.type)); w2.boolean(m.operational);
      w2.i32(m.generation); w2.f64(m.power_supply_kw); w2.f64(m.thermal_reject_kw);
    });
    w.vec(G.catalog.entries(), [](save::Writer& w2, const mission::CatalogEntry& e) {
      w2.str(e.contract.id); w2.boolean(e.suspended); w2.f64(e.available_after_days);
    });
    std::ofstream f(chemin, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(w.bytes().data()),
            static_cast<std::streamsize>(w.bytes().size()));
    return static_cast<bool>(f);
  }

  // Précondition : initialiser() déjà appelée (le seed reconstruit arbre/station
  // /catalogue ; on ne recharge que l'ÉTAT par-dessus, apparié par id).
  bool charger(const std::string& chemin) {
    if (!etat) return false;
    std::ifstream f(chemin, std::ios::binary);
    if (!f) return false;
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)),
                                    std::istreambuf_iterator<char>());
    save::Reader r(bytes.data(), bytes.size());
    auto& G = *etat;
    if (!G.load(r)) return false;
    dernier_mois = r.f64();
    derniers_reussites = r.i32();
    derniers_echecs = r.i32();
    struct NodeTrl { std::string id; int trl; };
    for (const auto& nt : r.vec<NodeTrl>([](save::Reader& r2) {
           NodeTrl n; n.id = r2.str(); n.trl = r2.i32(); return n;
         }))
      if (tech::TechNode* n = G.tree.find_mut(nt.id)) n->trl = nt.trl;
    auto projets = r.vec<tech::ResearchProject>([](save::Reader& r2) {
      tech::ResearchProject p;
      p.node_id = r2.str(); p.days_done = r2.f64(); p.days_total = r2.f64();
      p.priority_program = r2.boolean();
      return p;
    });
    G.research = tech::ResearchQueue{};
    for (const auto& p : projets)
      if (G.research.start(G.tree, p.node_id, career::Rank::Directeur, true))
        const_cast<tech::ResearchProject&>(G.research.active().back()).days_done = p.days_done;
    G.station.modules = r.vec<station::StationModule>([](save::Reader& r2) {
      station::StationModule m;
      m.type = static_cast<station::ModuleType>(r2.i32());
      m.operational = r2.boolean(); m.generation = r2.i32();
      m.power_supply_kw = r2.f64(); m.thermal_reject_kw = r2.f64();
      return m;
    });
    struct EtatCat { std::string id; bool susp; double apres; };
    for (const auto& ec : r.vec<EtatCat>([](save::Reader& r2) {
           EtatCat e; e.id = r2.str(); e.susp = r2.boolean(); e.apres = r2.f64();
           return e;
         }))
      for (auto& e : G.catalog.entries())
        if (e.contract.id == ec.id) { e.suspended = ec.susp; e.available_after_days = ec.apres; }
    return r.ok();
  }

  // --- contenu seed ------------------------------------------------------------
  static void seed_arbre(tech::TechTree& t) {
    using tech::Branch; using career::Rank;
    auto n = [&t](const char* id, const char* nom, Branch b, int trl,
                  double jours, double cout, Rank rang,
                  std::vector<std::string> prereqs = {}, bool transverse = false) {
      tech::TechNode x;
      x.id = id; x.name = nom; x.branch = b; x.trl = trl; x.trl_start = trl;
      x.research_days = jours; x.research_cost_musd = cout; x.min_rank = rang;
      x.prereqs = std::move(prereqs); x.transverse = transverse;
      t.add(std::move(x));
    };
    // ═══ L'ARBRE SUIT LES SOUS-BRANCHES QUE LE GDD NOMME EN 5.7–5.13 ═══
    // Le TRL de départ vient de la colonne « disponible au départ » de 5.6 : ce
    // que le monde sait déjà faire est opérationnel (TRL 9) ; le « futur
    // crédible à rechercher » démarre entre 1 et 6 selon sa maturité.
    // Coûts et durées : PROVISOIRES (`TechNode::costs_provisional`) — le GDD 20
    // renvoie les valeurs unitaires à une version ultérieure.

    // --- B1 : Accès à l'orbite [GDD 5.7] -------------------------------------
    // lanceurs · rentrée/récupération/réutilisation · rendez-vous et amarrage ·
    // transfert de propergol orbital · cadence et infrastructure de lancement
    n("lanceur_leger", "Lanceur leger", Branch::OrbitAccess, 9, 0, 0, Rank::Stagiaire);
    n("lanceur_moyen", "Lanceur moyen qualifie", Branch::OrbitAccess, 9, 0, 0, Rank::Stagiaire);
    n("guidage_inertiel", "Guidage inertiel et controle d'attitude", Branch::OrbitAccess, 9, 0, 0, Rank::Stagiaire);
    n("rentree_capsule", "Rentree capsule et recuperation partielle", Branch::OrbitAccess, 9, 0, 0, Rank::Stagiaire);
    n("lanceur_lourd", "Lanceur lourd", Branch::OrbitAccess, 8, 120, 40, Rank::Junior, {"lanceur_moyen"});
    n("rdv_automatise", "Rendez-vous automatise robuste", Branch::OrbitAccess, 6, 90, 25, Rank::Junior, {"guidage_inertiel"});
    n("insertion_precise", "Precision d'insertion accrue", Branch::OrbitAccess, 6, 150, 35, Rank::Junior, {"guidage_inertiel"});
    n("reutilisation", "Reutilisation poussee", Branch::OrbitAccess, 5, 240, 60, Rank::Junior, {"rentree_capsule"});
    n("lanceur_super_lourd", "Lanceur super-lourd", Branch::OrbitAccess, 5, 480, 220, Rank::Senior, {"lanceur_lourd", "reutilisation"});
    n("cadence_industrielle", "Cadence industrielle et infrastructure", Branch::OrbitAccess, 5, 360, 140, Rank::Senior, {"reutilisation"});
    n("transfert_ergols", "Transfert d'ergols orbital", Branch::OrbitAccess, 3, 540, 120, Rank::Senior, {"rdv_automatise"});
    n("rentree_lourde", "Rentree lourde reutilisable", Branch::OrbitAccess, 3, 660, 190, Rank::Senior, {"reutilisation", "lanceur_super_lourd"});

    // --- B2 : Exploration robotique [GDD 5.8] --------------------------------
    // sondes · orbiteurs et cartographie · atterrisseurs et EDL · rovers ·
    // prélèvement et retour d'échantillons · robotique orbitale
    n("sondes", "Sondes scientifiques", Branch::Robotic, 9, 0, 0, Rank::Stagiaire);
    n("teledetection", "Instruments et teledetection", Branch::Robotic, 9, 0, 0, Rank::Stagiaire);
    n("orbiteurs_cartographie", "Orbiteurs et cartographie", Branch::Robotic, 9, 0, 0, Rank::Stagiaire);
    n("atterrissage_robotique", "Atterrissage robotique classique", Branch::Robotic, 9, 0, 0, Rank::Stagiaire);
    n("rovers", "Rovers et mobilite de surface", Branch::Robotic, 8, 60, 20, Rank::Junior, {"atterrissage_robotique"});
    n("edl_robotique", "EDL robotique de precision", Branch::Robotic, 6, 180, 40, Rank::Junior, {"atterrissage_robotique", "avionique"});
    n("prelevement", "Prelevement et conditionnement", Branch::Robotic, 5, 240, 55, Rank::Junior, {"rovers"});
    n("autonomie_scientifique", "Autonomie scientifique embarquee", Branch::Robotic, 5, 300, 65, Rank::Senior, {"teledetection"});
    n("robotique_orbitale", "Robotique d'assemblage et de maintenance", Branch::Robotic, 4, 420, 110, Rank::Senior, {"rdv_automatise", "robotique"});
    n("retour_echantillons", "Retour d'echantillons robuste", Branch::Robotic, 3, 720, 150, Rank::Senior, {"edl_robotique", "prelevement"});

    // --- B3 : Vol habité proche Terre [GDD 5.9] ------------------------------
    // capsules et sécurité équipage · EVA · rendez-vous habité et sauvetage ·
    // stations orbitales modulaires · logistique d'équipage et rotation
    n("capsule_habitee", "Capsule habitee", Branch::CrewedLeo, 9, 0, 0, Rank::Stagiaire);
    n("eva", "Operations EVA", Branch::CrewedLeo, 9, 0, 0, Rank::Stagiaire);
    n("support_vie_court", "Support-vie court terme", Branch::CrewedLeo, 9, 0, 0, Rank::Stagiaire);
    // L'amarrage HABITÉ est disponible au départ [GDD 5.6] et ne dépend PAS du
    // rendez-vous AUTOMATISÉ, qui est une techno future de la branche 1 :
    // historiquement l'équipage a amarré à la main bien avant que l'automatisme
    // soit robuste. L'oracle 19.2 a rattrapé l'inversion.
    n("amarrage_habite", "Rendez-vous et amarrage habites", Branch::CrewedLeo, 9, 0, 0, Rank::Stagiaire);
    n("logistique_leo", "Logistique et rotation d'equipage", Branch::CrewedLeo, 8, 90, 30, Rank::Junior, {"amarrage_habite"});
    n("station_modulaire", "Stations modulaires avancees", Branch::CrewedLeo, 6, 300, 90, Rank::Senior, {"capsule_habitee", "amarrage_habite"});
    n("automatisation_bord", "Automatisation de bord", Branch::CrewedLeo, 5, 270, 60, Rank::Senior, {"support_vie_court"});
    n("sauvetage_habite", "Sauvetage orbital robuste", Branch::CrewedLeo, 4, 390, 100, Rank::Senior, {"capsule_habitee", "logistique_leo"});
    n("maintenance_humaine", "Maintenance humaine avancee", Branch::CrewedLeo, 4, 330, 80, Rank::Senior, {"eva", "station_modulaire"});

    // --- B4 : Autonomie longue durée [GDD 5.10] ------------------------------
    // recyclage · contrôle environnemental et thermique habité · médecine ·
    // facteurs humains · réparabilité, redondance, maintenance locale
    n("gestion_consommables", "Gestion des consommables", Branch::LongDuration, 9, 0, 0, Rank::Stagiaire);
    n("protocoles_medicaux", "Protocoles medicaux de base", Branch::LongDuration, 9, 0, 0, Rank::Stagiaire);
    n("redondance_base", "Redondance de base", Branch::LongDuration, 8, 0, 0, Rank::Junior);
    n("recyclage_partiel", "Recyclage partiel air/eau", Branch::LongDuration, 7, 0, 0, Rank::Junior);
    n("eclss_habite", "Controle environnemental et thermique habite", Branch::LongDuration, 6, 240, 55, Rank::Junior, {"recyclage_partiel"});
    n("facteurs_humains", "Facteurs humains : fatigue, stress, isolement", Branch::LongDuration, 5, 300, 45, Rank::Senior, {"protocoles_medicaux"});
    n("medecine_embarquee", "Medecine embarquee assistee", Branch::LongDuration, 4, 360, 70, Rank::Senior, {"protocoles_medicaux"});
    n("diagnostics_autonomes", "Diagnostics autonomes", Branch::LongDuration, 4, 300, 60, Rank::Senior, {"medecine_embarquee"});
    n("maintenance_locale", "Reparabilite et maintenance locale", Branch::LongDuration, 4, 330, 75, Rank::Senior, {"redondance_base"});
    n("recyclage_ferme", "Recyclage quasi ferme", Branch::LongDuration, 3, 900, 200, Rank::Principal, {"recyclage_partiel", "eclss_habite"});
    n("sejour_long", "Support-vie long sejour", Branch::LongDuration, 3, 780, 180, Rank::Principal, {"recyclage_ferme", "facteurs_humains", "radioprotection"});

    // --- B5 : Navigation et opérations interplanétaires [GDD 5.11] -----------
    // transferts impulsionnels · assistances gravitationnelles et multi-survols ·
    // navigation profonde · capture et séquences terminales · aérofreinage et
    // aérocapture · pré-positionnement et séquençage logistique
    n("hohmann_ops", "Transferts et corrections standards", Branch::InterplanetaryNav, 9, 0, 0, Rank::Stagiaire);
    n("capture_orbitale", "Capture orbitale et sequences terminales", Branch::InterplanetaryNav, 8, 0, 0, Rank::Junior, {"hohmann_ops"});
    n("gravity_assist", "Assistances gravitationnelles", Branch::InterplanetaryNav, 7, 0, 0, Rank::Junior);
    n("aerofreinage", "Aerofreinage", Branch::InterplanetaryNav, 8, 90, 25, Rank::Junior, {"capture_orbitale"});
    n("multi_survols", "Architectures multi-survols", Branch::InterplanetaryNav, 6, 210, 45, Rank::Senior, {"gravity_assist"});
    n("multi_impulsions", "Optimisation multi-impulsions", Branch::InterplanetaryNav, 6, 240, 50, Rank::Senior, {"hohmann_ops"});
    n("nav_profonde", "Navigation autonome profonde", Branch::InterplanetaryNav, 5, 420, 80, Rank::Senior, {"capteurs_navigation", "communications"});
    n("rdv_lointain", "Rendez-vous lointains complexes", Branch::InterplanetaryNav, 4, 480, 120, Rank::Principal, {"nav_profonde", "rdv_automatise"});
    n("prepositionnement", "Pre-positionnement et sequencage logistique", Branch::InterplanetaryNav, 4, 450, 130, Rank::Principal, {"multi_impulsions", "transfert_ergols"});
    n("aerocapture", "Aerocapture avancee", Branch::InterplanetaryNav, 3, 720, 160, Rank::Principal, {"nav_profonde", "aerofreinage"});
    // --- TRANSVERSES [GDD 5.13] ----------------------------------------------
    // « Elles ne forment pas de branche propre : elles sont DISTRIBUÉES au sein
    // de chacune des six branches » et « peuvent BLOQUER ou RALENTIR des
    // programmes entiers ». Les dix que le GDD nomme, chacune logée dans la
    // branche où elle mord le plus, toutes marquées `transverse`.
    n("avionique", "Avionique", Branch::OrbitAccess, 9, 0, 0, Rank::Stagiaire, {}, true);
    n("qualification_essais", "Qualification et essais", Branch::OrbitAccess, 8, 120, 45, Rank::Junior, {}, true);
    n("fabrication_metrologie", "Fabrication, maintenance, metrologie", Branch::OrbitAccess, 7, 180, 55, Rank::Junior, {}, true);
    n("robotique", "Robotique", Branch::Robotic, 8, 60, 25, Rank::Junior, {}, true);
    n("communications", "Communications et gestion des delais", Branch::Robotic, 8, 90, 30, Rank::Junior, {}, true);
    n("informatique_bord", "Informatique de bord", Branch::CrewedLeo, 8, 120, 35, Rank::Junior, {}, true);
    n("radioprotection", "Radioprotection", Branch::LongDuration, 5, 420, 95, Rank::Senior, {}, true);
    n("capteurs_navigation", "Capteurs et navigation", Branch::InterplanetaryNav, 8, 150, 40, Rank::Junior, {}, true);

    // --- B6 : Énergie et propulsion avancée [GDD 5.12] -----------------------
    // Les neuf paliers de 5.12.3, dans l'ordre, avec leurs verrous croisés.
    n("solaire", "Solaire + batteries", Branch::EnergyPropulsion, 9, 0, 0, Rank::Stagiaire);
    n("rtg", "RTG radio-isotopiques", Branch::EnergyPropulsion, 8, 0, 0, Rank::Junior);
    n("electrique_avancee", "Propulsion electrique avancee", Branch::EnergyPropulsion, 5, 360, 90, Rank::Senior);
    n("thermique_radiateurs", "Thermique et radiateurs", Branch::EnergyPropulsion, 4, 300, 60, Rank::Senior, {}, true);
    n("materiaux_ht", "Materiaux haute temperature", Branch::EnergyPropulsion, 3, 540, 110, Rank::Senior, {}, true);
    n("fission_spatiale", "Reacteur de fission spatial", Branch::EnergyPropulsion, 3, 900, 300, Rank::Principal, {"thermique_radiateurs", "materiaux_ht", "radioprotection", "qualification_essais"});
    n("ntp", "Propulsion nucleaire thermique", Branch::EnergyPropulsion, 2, 1100, 350, Rank::Principal, {"fission_spatiale"});
    n("nep_megawatt", "NEP megawatt", Branch::EnergyPropulsion, 2, 1300, 400, Rank::Principal,
      {"fission_spatiale", "electrique_avancee", "thermique_radiateurs", "materiaux_ht"});
    n("fusion", "Fusion pilotee spatiale", Branch::EnergyPropulsion, 1, 1800, 800, Rank::Directeur, {"nep_megawatt"});
    n("antimatiere", "Antimatiere (fin d'arbre)", Branch::EnergyPropulsion, 1, 2600, 2000, Rank::Directeur, {"fusion"});
  }

  static void seed_station(station::Station& st) {
    using station::ModuleType;
    st.modules.push_back({ModuleType::CommandCore, true, 1, 0.0, 0.0});
    st.modules.push_back({ModuleType::DockingNode, true, 1, 0.0, 0.0});
    st.modules.push_back({ModuleType::Power, true, 1, 40.0, 60.0});
    st.topology.push_back({0, 1});
    st.topology.push_back({1, 2});
  }

  static void seed_catalogue(mission::MissionCatalog& c) {
    using career::Rank;
    auto entree = [&c](const char* id, const char* titre, const char* famille,
                       bool habite, Rank rang, std::vector<std::string> technos,
                       tech::InfrastructureNeed infra = {}) {
      mission::CatalogEntry e;
      e.contract.id = id; e.contract.title = titre; e.contract.family = famille;
      e.contract.crewed = habite;
      e.contract.prerequisites.id = id;
      e.contract.prerequisites.name = titre;
      e.contract.prerequisites.min_rank = rang;
      e.contract.prerequisites.required_tech = std::move(technos);
      e.contract.prerequisites.infra = infra;
      c.add(std::move(e));
    };
    // ═══ LES DIX TYPES DE MISSION DE [GDD 10.1] ═══
    // « Couverture exhaustive de tout ce qui existe ou a existé dans l'histoire
    // spatiale réelle. » Un type par entrée, dans l'ordre du GDD, chacun avec
    // ses prérequis pris dans l'arbre et son corps de mail — car depuis
    // `mission/Mail.hpp` un contrat n'existe que porté par un courrier [10.2].
    entree("CAT-01", "Constellation d'observation LEO", "sat", false,
           Rank::Stagiaire, {"lanceur_moyen", "insertion_precise"});
    entree("CAT-02", "Sonde de reconnaissance et survol", "science", false,
           Rank::Stagiaire, {"sondes", "hohmann_ops", "communications"});
    entree("CAT-03", "Cartographie orbitale haute resolution", "science", false,
           Rank::Junior, {"orbiteurs_cartographie", "teledetection", "gravity_assist"});
    entree("CAT-04", "Rover et retour d'echantillons", "surface", false,
           Rank::Senior, {"edl_robotique", "rovers", "prelevement", "retour_echantillons"});
    {
      tech::InfrastructureNeed inf; inf.station_tier = 1;
      entree("CAT-05", "Ravitaillement et logistique orbitale", "logistique", false,
             Rank::Junior, {"rdv_automatise", "logistique_leo"}, inf);
    }
    {
      tech::InfrastructureNeed inf; inf.station_tier = 2;
      entree("CAT-06", "Station LEO habitee permanente", "habite", true,
             Rank::Principal,
             {"capsule_habitee", "eva", "station_modulaire", "recyclage_partiel"}, inf);
    }
    entree("CAT-07", "Inspection, maintenance et sauvetage orbital", "service", true,
           Rank::Senior, {"eva", "sauvetage_habite", "robotique_orbitale"});
    entree("CAT-08", "Rotation d'equipage en orbite basse", "habite", true,
           Rank::Junior, {"capsule_habitee", "amarrage_habite", "support_vie_court"});
    {
      // Vol habité lointain : [GDD 19.1] il dépend AUTANT du support-vie, de la
      // médecine et des radiations que du moteur. Les prérequis le disent.
      tech::InfrastructureNeed inf; inf.station_tier = 3;
      entree("CAT-09", "Mission habitee martienne", "mars_habite", true,
             Rank::Directeur,
             {"sejour_long", "radioprotection", "medecine_embarquee", "aerocapture",
              "ntp", "lanceur_super_lourd"}, inf);
    }
    {
      tech::InfrastructureNeed inf;
      inf.power_kw = 200.0; inf.thermal_reject_kw = 500.0;
      inf.station_tier = 3; inf.nuclear_test_bench = true;
      entree("CAT-10", "Cargo NEP vers Jupiter", "nep", false,
             Rank::Directeur, {"nep_megawatt", "nav_profonde", "rtg"}, inf);
    }
    {
      // « Missions de très fin de jeu à propulsion extrême » [10.1, dernier
      // item]. Seule filière capable d'un β mesurable [6.7.2, 19.3].
      tech::InfrastructureNeed inf;
      inf.power_kw = 5000.0; inf.thermal_reject_kw = 20000.0;
      inf.station_tier = 4; inf.nuclear_test_bench = true;
      entree("CAT-11", "Mission relativiste a propulsion extreme", "relativiste", true,
             Rank::Directeur,
             {"antimatiere", "sejour_long", "radioprotection", "nav_profonde"}, inf);
    }

    // LES TERMES PHYSIQUES DU CONTRAT [GDD 4.1] : masse à emporter, budget,
    // délai, P(succès) exigée. Sans eux, `assess()` n'a rien à évaluer et la
    // boucle de mission est creuse. Valeurs DÉCLARÉES par famille, PROVISOIRES —
    // la « matrice mission × technologies » chiffrée est différée [GDD 20].
    for (auto& e : c.entries()) {
      mission::Contract& t = e.contract.terms;
      const std::string& f = e.contract.family;
      // Budgets calés pour qu'un plan raisonnable au rang requis soit VIABLE
      // (vérifié par oracle sur le contrat de départ) — le joueur garde la
      // marge non dépensée [GDD 3.1]. Provisoires [GDD 20].
      if (f == "sat")            { t.payload_kg = 3000; t.budget_musd = 175; t.deadline_months = 30; t.min_success_prob = 0.85; }
      else if (f == "science")   { t.payload_kg = 1200; t.budget_musd = 150; t.deadline_months = 40; t.min_success_prob = 0.80; }
      else if (f == "surface")   { t.payload_kg = 1800; t.budget_musd = 240; t.deadline_months = 48; t.min_success_prob = 0.75; }
      else if (f == "logistique"){ t.payload_kg = 5000; t.budget_musd = 200; t.deadline_months = 24; t.min_success_prob = 0.90; }
      else if (f == "service")   { t.payload_kg = 2000; t.budget_musd = 190; t.deadline_months = 30; t.min_success_prob = 0.85; }
      else if (f == "habite")    { t.payload_kg = 8000; t.budget_musd = 360; t.deadline_months = 36; t.min_success_prob = 0.95; }
      else if (f == "mars")      { t.payload_kg = 2200; t.budget_musd = 380; t.deadline_months = 54; t.min_success_prob = 0.80; }
      else if (f == "mars_habite"){t.payload_kg = 20000; t.budget_musd = 1200; t.deadline_months = 72; t.min_success_prob = 0.90; }
      else if (f == "nep")       { t.payload_kg = 12000; t.budget_musd = 650; t.deadline_months = 60; t.min_success_prob = 0.85; }
      else if (f == "relativiste"){t.payload_kg = 5000; t.budget_musd = 2500; t.deadline_months = 120; t.min_success_prob = 0.80; }
      else                       { t.payload_kg = 1500; t.budget_musd = 150; t.deadline_months = 36; t.min_success_prob = 0.80; }
    }

    // Le corps du mail vient du CONTRAT, jamais de l'interface [GDD 10.2].
    // ASCII STRICT (convention du projet) : les std::string sont affichees en
    // FString sans reencodage, un caractere non-ASCII ressortirait en glyphe
    // manquant. Pas d'accents, pas de guillemets typographiques.
    for (auto& e : c.entries()) {
      if (!e.contract.mail_body.empty()) continue;
      e.contract.mail_body =
          "ARES vous confie l'architecture de la mission \"" + e.contract.title +
          "\". Le cahier des charges ne donne aucun Delta-v : a vous de le "
          "deriver, d'en budgeter les marges et d'en assumer les consequences. "
          "Les prerequis techniques sont reputes leves a la date de ce courrier.";
    }
  }

  static void seed_fiabilite(reliability::ReliabilityDatabase& db) {
    using namespace reliability;
    auto fiche = [&db](const char* id, const char* nom, const char* famille,
                       double lo, double nominal, double hi, Confidence conf,
                       SourceType type, const char* source) {
      ReliabilityRecord r;
      r.id = id; r.name = nom; r.family = famille; r.function = "propulsion etage";
      r.lo = lo; r.nominal = nominal; r.hi = hi;
      r.confidence = conf; r.source_type = type; r.source = source;
      r.date_ref = "2026-01-01";
      r.context.mission_days = 1.0;   // fiabilite PAR ALLUMAGE (duree ~ burn)
      db.add(r);
    };
    fiche("RL10C-1", "RL10C-1 (LOX/LH2)", "moteur", 0.9965, 0.9980, 0.9990,
          Confidence::A, SourceType::MissionData,
          "historique de vol Atlas/Delta, ~500 allumages");
    fiche("Aestus", "Aestus (stockables)", "moteur", 0.9900, 0.9950, 0.9970,
          Confidence::B, SourceType::Manufacturer,
          "donnees constructeur + 120 vols Ariane 5 G");
    fiche("MTX-1", "MTX-1 (methane, neuf)", "moteur", 0.8500, 0.9000, 0.9500,
          Confidence::D, SourceType::Estimate,
          "estimation raisonnee : zero vol, analogie staged-combustion");
  }
};

} // namespace fen::app
