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
#include <cmath>
#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "fen/game/GameState.hpp"
#include "fen/mission/MissionLoop.hpp"   // mission_seed : le tirage reste rejouable

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

// ═══ LE PALIER DE LA FILIÈRE ANTIMATIÈRE, LU SUR LA BRANCHE 6 ═══
// [GDD 5.12.12] « Le rendement énergétique COUPLE la production à la branche
// énergie et au budget. » Une seule lecture, dans l'ordre décroissant : le
// palier le plus haut qualifié fait foi, exactement comme le verrou le plus
// contraignant de [GDD 5.4] mais dans l'autre sens. Le débit cesse ainsi d'être
// une propriété de la station pour devenir ce que le GDD en fait : le résultat
// de ce que l'agence sait produire comme ÉNERGIE.
inline rel::AntimatterTier antimatter_tier(const game::GameState& G) {
  auto ok = [&G](const char* id) {
    const tech::TechNode* n = G.tree.find(id);
    return n && n->operational();
  };
  if (ok("antimatiere"))      return rel::AntimatterTier::Mature;
  if (ok("fusion"))           return rel::AntimatterTier::Fusion;
  if (ok("fission_spatiale")) return rel::AntimatterTier::Fission;
  return rel::AntimatterTier::None;
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
    G.character.age_bio_s = career::ENTRY_AGE_Y * career::YEAR_S;
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

  // ═══ TIRER LES ÉVÉNEMENTS DE BORD ═══ [GDD 9.5, 7.3]
  // Par FENÊTRES D'UN JOUR de calendrier, et jamais par frame : l'index de
  // fenêtre EST le numéro du jour, si bien que le tirage ne dépend ni de la
  // cadence, ni de la fréquence d'image, ni du nombre de fois où l'on a
  // sauvegardé. `jour_evenements_tire` retient jusqu'où l'on est allé — et une
  // avance de six mois rattrape ses 180 fenêtres au lieu d'en sauter 179.
  //
  // GARDE-FOU : on borne le rattrapage. Une partie qui charge après des années de
  // temps accéléré ne doit pas tirer dix mille fenêtres dans une frame ; au-delà,
  // on saute au présent, ce qui est DÉCLARÉ [GDD 6.8] — les pannes d'une période
  // que le joueur n'a pas vécue ne l'intéressent pas.
  static constexpr int MAX_FENETRES_PAR_FRAME = 400;
  static void tirer_evenements_bord(game::GameState& G, const mission::Mission& m,
                                    double maintenant) {
    const double jour = std::floor(maintenant);
    // L'ORIGINE DU TIRAGE EST LE DÉCOLLAGE, PAS LA PREMIÈRE FRAME. Ancrer sur
    // « maintenant » à la première visite rendait le tirage dépendant de l'instant
    // où l'on a regardé : une avance de 400 jours d'un seul tenant n'ouvrait
    // AUCUNE fenêtre (l'index se posait à l'arrivée), et un vol pouvait traverser
    // deux ans sans la moindre panne. La date d'entrée en vol, elle, est un FAIT
    // de la mission — même valeur quel que soit le découpage du temps.
    if (G.lived.jour_evenements_tire < 0.0)
      G.lived.jour_evenements_tire = std::floor(m.state_entered_days);
    if (jour <= G.lived.jour_evenements_tire) return;
    if (jour - G.lived.jour_evenements_tire > MAX_FENETRES_PAR_FRAME) {
      G.lived.jour_evenements_tire = jour;                // rattrapage borné
      return;
    }

    mission::EventContext ctx;
    ctx.crewed = true;                                     // on est à bord
    ctx.phase = m.phase;
    ctx.system_reliability = G.lived.fiabilite_systeme;
    // La préparation de l'équipage réduit le risque médical : c'est un effet de
    // Novellus [GDD 11.6], calculé par `station::effects` depuis toujours, écrit
    // en dur à 1,0 ICI jusqu'au 2026-07-29 — le module médical coûtait 110 M€ et
    // ne changeait aucun tirage. On lit la valeur GELÉE à l'embarquement, pas
    // l'état courant de la station : c'est un entraînement reçu avant de partir,
    // et le relire en vol rendrait les tirages dépendants de ce que l'adjoint
    // construit ou démonte pendant l'absence.
    ctx.medical_risk_factor = G.lived.facteur_risque_medical;

    const Rng rng_mission(mission::mission_seed(G.seed, m.contract.id));
    // ═══ LA GRILLE DE FENÊTRES EST ABSOLUE ═══ [GDD 18, déterminisme]
    // ⚠ HONNÊTETÉ SUR CETTE LIGNE : en cherchant pourquoi dix frames et quarante
    // frames ne consommaient plus pareil, j'ai d'abord accusé cette boucle — elle
    // partait de `jour_evenements_tire + 1.0`, que je croyais fractionnaire. Elle
    // ne l'était PAS (`jour = std::floor(maintenant)` ci-dessus, et le curseur
    // reçoit `jour`), donc la grille était déjà absolue et mon « correctif » ne
    // corrigeait rien. Il est conservé parce qu'un compteur entier dit
    // explicitement ce que le flottant garantissait par accident — mais le vrai
    // défaut était ailleurs (voir l'intégration par sous-pas, plus bas).
    const long long premier = static_cast<long long>(
        std::floor(G.lived.jour_evenements_tire)) + 1;
    const long long dernier = static_cast<long long>(std::floor(jour));
    for (long long jd = premier; jd <= dernier; ++jd) {
      const double j = static_cast<double>(jd);
      // ═══ L'ACTIVITÉ SOLAIRE EST CELLE DE LA FENÊTRE, PAS DE « MAINTENANT » ═══
      // Défaut trouvé par l'oracle de rejouabilité : évaluée une fois pour toutes
      // à l'instant du tick, elle appliquait à 400 fenêtres la valeur du DERNIER
      // jour — si bien qu'avancer d'un bloc ou par tranches ne donnait pas les
      // mêmes éruptions. Le cycle solaire a une période de 11 ans, une année de
      // fenêtres n'est pas un instant. `WorldEpoch::at` existait déjà pour ça.
      ctx.solar_activity01 = G.solar.activity01(G.clock.world_epoch().at(j));
      const auto tirages = mission::sample_events(
          rng_mission, static_cast<std::uint64_t>(j), j, 1.0, ctx);
      for (const auto& s : tirages) {
        if (s.kind == mission::EventKind::SolarParticleEvent) {
          // ═══ L'ÉRUPTION EST UN INSTANT, ET LE BLINDAGE DÉCIDE ═══ [GDD 6.6]
          // Là où le GCR résiste au blindage, le SPE y cède exponentiellement :
          // c'est CE risque-là que la masse achète, et c'est la raison d'être de
          // l'abri anti-tempête des projets réels.
          G.dose_architecte.add_acute_gy(
              mission::dose_aigue_spe_gy(s.magnitude01, G.lived.blindage));
          continue;                                        // rien à réparer
        }
        mission::Avarie av;
        av.kind = s.kind;
        av.debut_days = s.t_days;
        av.gravite01 = s.magnitude01;
        G.avaries.push_back(av);
        // ═══ ET LA MISSION S'EN SOUVIENT ═══ [GDD 3.3] — le critère « gestion de
        // crise » se juge à l'arrivée sur ce qui est TOMBÉ et ce qui a été
        // RÉPARÉ. `G.avaries` est vidé au débarquement : le compteur doit donc
        // vivre sur la mission, comme tout ce qui appartient au vol.
        for (auto& mm : G.missions)
          if (mm.contract.id == m.contract.id) { mm.crise_avaries += 1; break; }
      }
    }
    G.lived.jour_evenements_tire = jour;

    // Les réparations engagées arrivent à terme d'elles-mêmes.
    for (auto& av : G.avaries)
      if (!av.reparee && av.fin_reparation_days > av.debut_days &&
          maintenant >= av.fin_reparation_days) {
        av.reparee = true;
        // Une panne menée à réparation est le « sauvetage » de [GDD 10.3] : elle
        // compte pour le critère de gestion de crise [3.3]. On la compte À
        // L'ABOUTISSEMENT, pas à l'engagement — engager une réparation qu'on
        // n'a pas le temps de finir n'a sauvé personne.
        for (auto& mm : G.missions)
          if (mm.contract.id == m.contract.id) { mm.crise_reparees += 1; break; }
      }
  }

  // Le rapport dτ_bord / dτ_Terre à cet instant [GDD 6.7]. À terre — c'est-à-dire
  // partout sauf entre le décollage et le débrief d'un vol vécu — il vaut 1 par
  // construction, et c'est un FAIT, pas un raccourci : l'Architecte partage alors
  // l'horloge du monde. La phase de vol, elle, est déjà dérivée de la chronologie ;
  // rien de nouveau n'est à renseigner pour que les deux horloges divergent.
  static double rapport_horloge_courant(const game::GameState& G) {
    if (!G.lived.active) return 1.0;
    for (const auto& mm : G.missions)
      if (mm.contract.id == G.lived.mission_id) {
        if (mm.state < mission::MissionState::Launched ||
            mm.state > mission::MissionState::Debrief) return 1.0;
        return mission::rapport_horloge_bord(mm.phase, G.lived.horloge);
      }
    return 1.0;
  }

  template <class AgenceT>
  void avancer(AgenceT& a) {
    auto& G = *etat;
    const double delta_mois = a.mois - dernier_mois;
    const double delta_jours = delta_mois * ARES_MONTH_S / cst::DAY;

    G.clock.restore(a.mois * ARES_MONTH_S);

    // ═══ DEUX HORLOGES, ET UNE SEULE EST LE CALENDRIER ═══ [GDD 6.7, 14.4, 3.4]
    // `rel::DualClock` était déclaré sur GameState, sauvegardé, rechargé — et
    // AVANCÉ NULLE PART. Le « vieillissement différentiel qui pèse sur la carrière
    // et la passation » valait donc exactement zéro, pour toute mission, y compris
    // celle que le GDD range en régime relativiste. Et l'âge biologique avançait du
    // temps du CALENDRIER, ce qui n'est vrai que si l'on ne quitte jamais le sol.
    //
    // Le rapport vaut 1 tant qu'on est à terre : le coût quand on n'est pas à bord
    // est une comparaison de chaîne et une addition. Il n'y a pas de raison de
    // n'accumuler que pendant les vols — l'écart d'une CARRIÈRE est précisément ce
    // que [GDD 3.4] veut pouvoir opposer au moment de la passation.
    const double dt_s = delta_mois * ARES_MONTH_S;
    const double ratio_horloge = rapport_horloge_courant(G);
    G.dual_clock.advance_ratio(dt_s, ratio_horloge);
    // L'ÂGE BIOLOGIQUE SUIT LE TEMPS PROPRE, pas le temps du monde [GDD 6.7.4].
    G.character.age_by_proper_time(dt_s * ratio_horloge);

    // ═══ ET UNE VIE FINIT [GDD 3.4] ═══
    // Le vieillissement était calculé, sauvegardé, affiché nulle part et surtout
    // SANS CONSÉQUENCE : `natural_death_due()` n'avait aucun lecteur, si bien
    // qu'un Architecte de 120 ans gardait son poste. C'est ici que la fin de vie
    // se constate — au même endroit que l'âge qui la provoque, et une seule fois
    // (le drapeau garde l'idempotence quelle que soit la cadence).
    //
    // ⚠ CE N'EST PAS UNE FIN DE PARTIE. [GDD 3.4] distingue trois issues, et
    // celle-ci OUVRE une passation : le poste change de titulaire, l'agence
    // continue. Seule la mort OPÉRATIONNELLE (décidée ailleurs, dans
    // `Session::resoudre_vie_a_bord`) termine la partie, et aucune passation ne
    // l'annule jamais.
    if (!G.passation_ouverte && !G.character.operational_death) {
      const bool age = G.character.alive && G.character.natural_death_due();
      if (age || !G.character.alive) {
        G.character.alive = false;
        G.passation_ouverte = true;
        G.passation_motif = age
            ? "fin de vie naturelle a " +
                  std::to_string(static_cast<int>(G.character.age_bio_years())) + " ans"
            : "deces de l'Architecte en fonction";
        a.log("[GDD 3.4] " + G.character.name + " s'est eteint — " +
              G.passation_motif + ". ARES ouvre une passation.");
      }
    }

    // ═══ LA VIE À BORD SE CONSOMME [GDD 9.1, 9.4] ═══
    // ICI, et pas dans `GameState::tick` — qui n'a aucun appelant (voir l'avis en
    // tête de cette fonction-là). Le piège a coûté un cycle : le code compilait,
    // les oracles de `Crew.hpp` passaient, et rien ne se consommait jamais.
    //
    // PAS BESOIN DE SOUS-PAS ICI, et ce n'est pas un relâchement : la
    // consommation est LINÉAIRE en dt, donc l'appeler une fois avec le total
    // donne EXACTEMENT le même état que N fois avec dt/N. `delta_jours` est déjà
    // quantifié par `Jeu::faire_couler_le_temps` (sous-pas de 1/64 j), si bien
    // que 4 frames et 100 frames donnent le même résultat — au flottant près, et
    // c'est vérifié par oracle. (La recherche, elle, n'est pas linéaire : d'où
    // l'approximation déclarée qui la concerne, et qui ne s'applique pas ici.)
    // ET SEULEMENT UNE FOIS LE VOL PARTI : embarquer est une décision prise
    // AVANT le feu vert (voir `Session::peut_embarquer`), or l'équipage ne vit
    // pas sur ses réserves de bord pendant la qualification — il est encore à
    // terre. La condition se LIT sur l'état de la mission plutôt que sur un
    // second drapeau à tenir à jour.
    if (G.lived.active && delta_jours > 0.0) {
      for (const auto& mm : G.missions)
        if (mm.contract.id == G.lived.mission_id) {
          if (mm.state >= mission::MissionState::Launched &&
              mm.state <= mission::MissionState::Debrief) {
            const double maintenant = G.clock.now_days();

            // ═══ CE QUI PEUT ARRIVER, ARRIVE ═══ [GDD 9.5, 7.3]
            // `Events.hpp` tirait des anomalies calibrées depuis le premier jour
            // et personne ne les consommait : la bibliothèque existait sans
            // qu'aucune anomalie ne se produise jamais. Le tirage se fait par
            // FENÊTRES D'UN JOUR indexées sur le calendrier — même exigence que
            // les sous-pas du temps : le même vol rejoué donne les mêmes pannes
            // aux mêmes dates, quelle que soit la cadence ou la fréquence
            // d'image. Un tirage par frame serait un dé de plus à chaque GPU.
            tirer_evenements_bord(G, mm, maintenant);

            // ═══ ET CE QUI EST TOMBÉ EN PANNE COÛTE, CHAQUE JOUR ═══
            // Les avaries n'appliquent aucun « malus » : elles dégradent les
            // grandeurs qui existent déjà, et c'est `VitalState::consume` qui en
            // tire les conséquences. Une panne de support-vie est une hémorragie
            // de vivres, pas une icône.
            // ═══ ET ELLE S'INTÈGRE PAR SOUS-PAS, PLUS PAR FRAME ═══
            // LE COMMENTAIRE CI-DESSUS ÉNONÇAIT L'HYPOTHÈSE QUI EST TOMBÉE : « la
            // consommation est LINÉAIRE en dt, donc l'appeler une fois avec le
            // total donne exactement le même état que N fois avec dt/N ». C'était
            // vrai TANT QUE L'ÉTAT D'AVARIE NE CHANGEAIT PAS dans une frame. Il ne
            // changeait presque jamais, parce que le taux de support-vie était
            // **17 fois trop bas** ; corrigé sur la mesure de l'ISS (74 j de MTBF),
            // les avaries commencent et se réparent au MILIEU d'une frame, et
            // `effets_avaries` devient une fonction du TEMPS. Évalué une fois par
            // frame, il attribuait à toute la frame l'état d'un seul instant : dix
            // frames et quarante frames ne consommaient plus pareil, et l'oracle
            // de sous-pas fixe l'a dit.
            //
            // La grille des sous-pas est ABSOLUE (multiples de 1/64 j depuis
            // l'origine de l'horloge), pas relative au début de la frame : c'est
            // ce qui rend le découpage sans effet [GDD 18, déterminisme].
            constexpr double SOUS_PAS_J = 1.0 / 64.0;
            double reste_j = delta_jours;
            double t_j = maintenant - delta_jours;
            while (reste_j > 1e-12) {
              const double h = reste_j < SOUS_PAS_J ? reste_j : SOUS_PAS_J;
              const mission::EffetsAvaries eff =
                  mission::effets_avaries(G.avaries, t_j + h);
              G.lived.vitals.consume(
                  G.lived.n_crew, h,
                  mission::boucles_degradees(G.lived.loops, eff));
              // Fuites (brèche) et surconsommation (malade à bord) : au prorata du
              // temps écoulé, comme tout le reste.
              G.lived.vitals.o2_kg    -= eff.fuite_o2_kg_j  * h;
              G.lived.vitals.water_kg -= eff.fuite_eau_kg_j * h;
              if (eff.surconso_vivres > 1.0)
                G.lived.vitals.food_kg -=
                    (eff.surconso_vivres - 1.0) * mission::MetabolicRates{}.food_dry_kg
                    * G.lived.n_crew * h;
              // L'épuration du CO2 se consomme plus vite quand la puissance manque.
              if (eff.facteur_co2 > 1.0)
                G.lived.vitals.co2_scrub_capacity_kg -=
                    (eff.facteur_co2 - 1.0) * mission::MetabolicRates{}.co2_out_kg
                    * G.lived.n_crew * h;
              t_j += h;
              reste_j -= h;
            }

            // ═══ ET LA DOSE, QUI NE SE DÉPENSE QUE DANS UN SENS ═══ [GDD 6.6]
            // `env/Radiation.hpp` était un modèle complet, ancré sur l'Annexe B,
            // et SANS AUCUN CONSOMMATEUR : [GDD 7.7] déclare l'environnement
            // « acteur de mission », il n'était que décor. La phase de vol donne
            // la fraction de ciel ouverte, le cycle solaire la modulation GCR —
            // les deux existaient déjà, il ne manquait que cette ligne.
            G.dose_architecte.add_chronic(mission::dose_chronique_sv(
                delta_jours, mm.phase, G.lived.blindage,
                G.solar.activity01(G.clock.now_epoch())));
          }
          break;
        }
    }

    // ═══ LES DÉBRIS RETOMBENT ═══ [GDD 7.8, 10.5]
    // `add_breakup` était sur le chemin vif (via `apply_anomaly`) mais `tick` ne
    // vivait que dans `GameState::tick`, qui n'a aucun appelant : les nuages
    // s'accumulaient SANS JAMAIS DÉCROÎTRE. La promesse de 7.8 — « les couloirs
    // LEO se nettoient, les couloirs hauts restent pollués » — n'avait donc que
    // sa moitié punitive. La traînée dépend de l'activité solaire, d'où le même
    // cycle que ci-dessus.
    if (delta_jours > 0.0)
      G.debris.tick(delta_jours, G.solar.activity01(G.clock.now_epoch()));

    // ═══ L'ANTIMATIÈRE S'ACCUMULE — ET FUIT ═══ [GDD 5.12.12, 19.3]
    // Le DÉBIT n'est pas tabulé, il se dérive de la PUISSANCE de l'usine — et
    // c'est là qu'était le défaut. La puissance prise était la MARGE DE NOVELLUS
    // (38 kW au départ, 5 MW au mieux), donc AUCUNE recherche de branche 6 ne
    // pouvait déplacer le débit : le « vrai levier d'équilibrage » du GDD n'était
    // pas branché sur son levier. Or 5.12.12 dit exactement où le prendre — « le
    // rendement énergétique COUPLE la production à la branche énergie » — et une
    // usine à antimatière n'est pas un module de station.
    // ET LA FUITE NE S'ARRÊTE JAMAIS, elle : un stock mal confiné se perd que la
    // filière soit qualifiée ou non — d'où l'argument séparé.
    // Le palier est reposé À CHAQUE PASSAGE, temps arrêté compris : le poste
    // AGENCE lit `prod` pour afficher débit et plafond, et une partie en pause
    // afficherait sinon le palier d'avant la dernière recherche.
    G.antimatiere.prod = rel::AntimatterProduction::for_tier(antimatter_tier(G));
    if (delta_jours > 0.0) {
      const tech::TechNode* n_am = G.tree.find("antimatiere");
      G.antimatiere.tick(delta_jours, n_am && n_am->operational());
    }

    // ═══ LA CONFIANCE EST GELÉE PENDANT L'ABSENCE ═══ [GDD 9.3]
    // « La chaîne de fin de partie financière est suspendue et la confiance GELÉE
    // à sa valeur de départ : aucune faillite ni perte de crédibilité ne peut
    // survenir en l'absence du joueur. » On la REPOSE au lieu de garder chaque
    // écriture : l'adjoint conduit de vraies missions pendant l'absence, donc de
    // vraies anomalies passent par `apply_anomaly`. Les intercepter une par une
    // demanderait de n'en oublier aucune, et il suffirait d'en ajouter une demain
    // pour trouer la promesse ; restaurer l'état la tient par construction.
    // Posé AVANT le score et la promotion ci-dessous, pour qu'ils lisent la
    // valeur gelée et non celle que l'adjoint vient d'entamer.
    if (G.lived.active) G.career.confidence_ares = G.lived.confidence_frozen;

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

    // ═══ LE SCORE NE SE COMPTE PLUS ICI ═══ [GDD 3.3]
    // Il l'était : « +40 par réussite, −10 par échec », sur les COMPTEURS de
    // l'agence. Deux défauts, et le second est le vrai. (1) Un compteur ne sait
    // pas ce qu'une mission a coûté ni ce qu'elle a traversé, donc les deux
    // autres critères de [3.3] — respect budgétaire, gestion de crise — étaient
    // structurellement hors de portée. (2) « Pondération égale des TROIS
    // critères » n'était donc pas approximée : elle était absente aux deux tiers.
    // Le score se calcule maintenant AU DÉBRIEF, mission par mission, là où les
    // trois faits sont connus (`Session::avancer_mission`). On garde les
    // compteurs à jour pour tout le reste.
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
      // ═══ LE CISLUNAIRE, QUE LE GDD NOMMAIT QUATRE FOIS SANS QU'IL EXISTE ═══
      // [GDD 3.3] le rang Principal est DÉFINI par « vol habité cislunaire » ;
      // [5.9] parle d'« architectures lunaires », [5.10] de « missions lunaires
      // avancées », et [19.7] donne à la classe « Vol habité lunaire /
      // cislunaire » ses CINQ verrous. Le catalogue n'en avait aucune : c'était
      // un manque de CONTENU, découvert en cherchant un consommateur au modèle
      // de descente propulsée (`flight/Descent.hpp`).
      //
      // LES PRÉREQUIS SONT LES CINQ X DE LA MATRICE 19.7, un par colonne :
      //   masse       -> `lanceur_super_lourd` (la classe Saturn V)
      //   thermique   -> `eclss_habite` (« contrôle environnemental ET THERMIQUE »)
      //   radiations  -> `radioprotection` (Van Allen à la traversée, GCR hors
      //                  magnétosphère — le premier vol qui quitte l'abri)
      //   maintenance -> `automatisation_bord` (dix jours sans secours possible)
      //   puissance   -> `capsule_habitee` + `amarrage_habite` : le rendez-vous
      //                  orbital lunaire est ce SANS QUOI personne ne revient.
      tech::InfrastructureNeed inf; inf.station_tier = 2;
      entree("CAT-12", "Vol habite cislunaire et alunissage", "lunaire_habite", true,
             Rank::Principal,
             {"lanceur_super_lourd", "eclss_habite", "radioprotection",
              "automatisation_bord", "capsule_habitee", "amarrage_habite"}, inf);
    }
    {
      // ═══ L'ORBITEUR DU SYSTÈME SOLAIRE EXTERNE ═══
      // Même diagnostic que le cislunaire : le manque était du CONTENU. Toute la
      // branche de l'assistance gravitationnelle (`mission/Assistance.hpp`, quatre
      // modules d'astro_core) n'avait aucun consommateur DANS LE JEU pour une
      // raison simple — le catalogue n'avait pas une seule mission dont un tour
      // puisse servir. La seule cible externe était CAT-10, un cargo NEP : la
      // poussée continue, c'est-à-dire le régime où une assistance impulsive n'a
      // rien à faire [GDD 6.3].
      //
      // LE GDD LA NOMME TROIS FOIS. Branche 2 « orbiteurs » ; branche 6 palier 2,
      // le RTG « ouvre le SYSTÈME SOLAIRE EXTERNE robotique » ; et la table des
      // compétences met les assistances en colonne Senior pour les « transferts
      // complexes ». Les prérequis sont donc ces quatre-là, un par verrou : la
      // sonde, la source d'énergie qui survit à 5 UA, la manœuvre qui rend le
      // voyage payable, et la navigation qui le tient à une heure-lumière.
      entree("CAT-13", "Orbiteur du systeme solaire externe", "externe", false,
             Rank::Senior, {"sondes", "rtg", "gravity_assist", "nav_profonde"});
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

    // LES TERMES PHYSIQUES DU CONTRAT [GDD 4.1] — la table vit désormais dans
    // `MissionLoop.hpp` (`contract_terms_for_family`), pour qu'une mission
    // construite HORS catalogue puisse en hériter au lieu de naître avec des
    // termes nuls. Voir le commentaire là-bas : le harnais de capture souffrait
    // exactement de ça, et affichait un verrou de viabilité faux dans chaque image.
    for (auto& e : c.entries())
      e.contract.terms = mission::contract_terms_for_family(e.contract.family);

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
    // ═══ UNE FICHE PAR PIÈCE RÉELLE, ET AUCUNE POUR UNE PIÈCE QUI N'EXISTE PAS ═══
    // [GDD 12.3] La base portait TROIS fiches écrites à la main, dont une pour
    // « MTX-1 », un moteur générique que [GDD 12.1] interdit et qui n'est plus au
    // catalogue. Elle est désormais SEMÉE DEPUIS LE CATALOGUE : dix-huit pièces,
    // dix-huit fiches, et pas une de plus. La valeur vient de la même
    // correspondance statut/confiance que la courbe d'essais
    // (`reliability_curve_for`) — un seul endroit où elle puisse être fausse.
    for (const auto& p : vehicle::engine_catalog()) {
      const mission::EngineReliabilityCurve c = mission::reliability_curve_for(p);
      // La borne basse est l'écart au maximum atteignable : c'est ce que le
      // principe conservateur [GDD 12.5] consomme quand la confiance est basse.
      const double lo = c.R0 - 0.5 * (c.Rmax - c.R0);
      const Confidence conf = static_cast<Confidence>(static_cast<int>(p.confidence));
      const SourceType type = (p.status == vehicle::QualStatus::Flown)
                                ? SourceType::MissionData
                                : (p.status == vehicle::QualStatus::GroundTested)
                                    ? SourceType::Manufacturer
                                    : SourceType::Estimate;
      fiche(p.id, p.name, "moteur", lo < 0.0 ? 0.0 : lo, c.R0, c.Rmax,
            conf, type, p.source);
    }
  }
};

} // namespace fen::app
