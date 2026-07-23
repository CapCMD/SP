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

    // L'argent VRAI est celui de l'agence (miroir). Pas de coûts fixes ici :
    // Jeu::passer_mois() prélève déjà l'entretien — pas de double peine.
    G.treasury.target_musd = 40.0;
    G.treasury.reserve_musd = 6.0;
    G.treasury.fixed_costs.clear();

    seed_arbre(G.tree);
    seed_station(G.station);
    seed_catalogue(G.catalog);
    seed_fiabilite(G.reliability_db);
  }

  template <class AgenceT>
  void sync_lecture(const AgenceT& a) {
    auto& G = *etat;
    G.treasury.balance_musd = a.tresorerie;
    G.career.confidence_ares = 100.0 * a.confiance;
  }

  template <class AgenceT>
  void avancer(AgenceT& a) {
    auto& G = *etat;
    const double delta_mois = a.mois - dernier_mois;
    const double delta_jours = delta_mois * ARES_MONTH_S / cst::DAY;

    // horloge + vieillissement (temps propre == temps de jeu hors relativiste)
    G.clock.restore(a.mois * ARES_MONTH_S);
    G.character.age_bio_s += delta_mois * ARES_MONTH_S;

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
    // B1 — Accès à l'orbite
    n("lanceur_moyen", "Lanceur moyen qualifie", Branch::OrbitAccess, 9, 0, 0, Rank::Stagiaire);
    n("rdv_automatise", "Rendez-vous automatise robuste", Branch::OrbitAccess, 6, 90, 25, Rank::Junior);
    n("reutilisation", "Reutilisation poussee", Branch::OrbitAccess, 5, 240, 60, Rank::Junior);
    n("transfert_ergols", "Transfert d'ergols orbital", Branch::OrbitAccess, 3, 540, 120, Rank::Senior, {"rdv_automatise"});
    // B2 — Exploration robotique
    n("sondes", "Sondes scientifiques", Branch::Robotic, 9, 0, 0, Rank::Stagiaire);
    n("edl_robotique", "EDL robotique de precision", Branch::Robotic, 6, 180, 40, Rank::Junior);
    n("retour_echantillons", "Retour d'echantillons robuste", Branch::Robotic, 3, 720, 150, Rank::Senior, {"edl_robotique"});
    // B3 — Vol habité proche Terre
    n("capsule_habitee", "Capsule habitee", Branch::CrewedLeo, 9, 0, 0, Rank::Stagiaire);
    n("eva", "Operations EVA", Branch::CrewedLeo, 9, 0, 0, Rank::Stagiaire);
    n("station_modulaire", "Stations modulaires avancees", Branch::CrewedLeo, 6, 300, 90, Rank::Senior, {"capsule_habitee"});
    // B4 — Autonomie longue durée
    n("recyclage_partiel", "Recyclage partiel air/eau", Branch::LongDuration, 7, 0, 0, Rank::Junior);
    n("medecine_embarquee", "Medecine embarquee assistee", Branch::LongDuration, 4, 360, 70, Rank::Senior);
    n("recyclage_ferme", "Recyclage quasi ferme", Branch::LongDuration, 3, 900, 200, Rank::Principal, {"recyclage_partiel"});
    // B5 — Navigation interplanétaire
    n("hohmann_ops", "Transferts et corrections standards", Branch::InterplanetaryNav, 9, 0, 0, Rank::Stagiaire);
    n("gravity_assist", "Assistances gravitationnelles", Branch::InterplanetaryNav, 7, 0, 0, Rank::Junior);
    n("nav_profonde", "Navigation autonome profonde", Branch::InterplanetaryNav, 5, 420, 80, Rank::Senior);
    n("aerocapture", "Aerocapture avancee", Branch::InterplanetaryNav, 3, 720, 160, Rank::Principal, {"nav_profonde"});
    // B6 — Énergie et propulsion avancée [GDD 5.12] + transverses [GDD 5.13]
    n("solaire", "Solaire + batteries", Branch::EnergyPropulsion, 9, 0, 0, Rank::Stagiaire);
    n("rtg", "RTG radio-isotopiques", Branch::EnergyPropulsion, 8, 0, 0, Rank::Junior);
    n("electrique_avancee", "Propulsion electrique avancee", Branch::EnergyPropulsion, 5, 360, 90, Rank::Senior);
    n("thermique_radiateurs", "Thermique et radiateurs", Branch::EnergyPropulsion, 4, 300, 60, Rank::Senior, {}, true);
    n("materiaux_ht", "Materiaux haute temperature", Branch::EnergyPropulsion, 3, 540, 110, Rank::Senior, {}, true);
    n("fission_spatiale", "Reacteur de fission spatial", Branch::EnergyPropulsion, 3, 900, 300, Rank::Principal, {"thermique_radiateurs", "materiaux_ht"});
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
    entree("CAT-01", "Constellation d'observation LEO", "sat", false,
           Rank::Stagiaire, {"lanceur_moyen"});
    entree("CAT-02", "Orbiteur martien nouvelle generation", "mars", false,
           Rank::Senior, {"sondes", "hohmann_ops", "nav_profonde"});
    {
      tech::InfrastructureNeed inf; inf.station_tier = 2;
      entree("CAT-03", "Station LEO habitee permanente", "habite", true,
             Rank::Principal, {"capsule_habitee", "eva", "station_modulaire", "recyclage_partiel"}, inf);
    }
    {
      tech::InfrastructureNeed inf;
      inf.power_kw = 200.0; inf.thermal_reject_kw = 500.0;
      inf.station_tier = 3; inf.nuclear_test_bench = true;
      entree("CAT-04", "Cargo NEP vers Jupiter", "nep", false,
             Rank::Directeur, {"nep_megawatt", "nav_profonde", "rtg"}, inf);
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
