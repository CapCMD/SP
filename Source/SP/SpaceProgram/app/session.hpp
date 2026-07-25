// app/session.hpp — LA PARTIE EN COURS, sans une ligne de rendu.
//
// Remplace `fen::ui::Interface` (ui/jeu_ecrans.hpp) lors du passage au rendu
// 100 % UE5. Ce qui vivait dans l'ancien écran ImGui se sépare en deux :
//   . ce qui est de la DONNÉE et de l'ÉTAT — routage de scène, sauvegardes,
//     publication du pont — est ici, en C++ pur, sous oracle ;
//   . ce qui est du DESSIN et de l'ENTRÉE est natif UE (UEBridge/SPHud.cpp,
//     UEBridge/SPPlayerController.cpp).
//
// C++ pur : JAMAIS d'entête UnrealEngine, JAMAIS d'ImGui.
//
// RÈGLES GDD GRAVÉES ICI (identiques à l'écran qu'il remplace) :
//   [GDD 14]  le temps est UNIQUE : la carte montre l'époque de jeu COURANTE ;
//             un vol en cours EST l'horloge la plus avancée, sinon calendrier
//             agence. Aucun curseur temporel libre.
//   [GDD 7.5] le joueur ne voit JAMAIS la vérité absolue : ce qu'on publie est
//             l'ESTIMATION de navigation, avec son corridor.
//   [GDD 8.3] TOUS les éléments de mission en service sont sur la carte.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "app/bridge_flags.hpp"
#include "app/jeu.hpp"
#include "app/postes.hpp"
#include "app/vehicle_design.hpp"
#include "fen/ephem/Ephemeris.hpp"
#include "fen/mission/MissionLoop.hpp"

namespace fen::app {

// Distance de vue par défaut pour un corps : de quoi le voir en entier.
// (Reprise telle quelle de ui/carte3d_ecran.hpp — l'entrée native l'appelle au
// changement de focus.)
inline double distance_cadrage(int body) {
  if (body < 0) return 9.0e8;                       // vue système (~6 UA)
  const double r_km = ephem::body_radius((ephem::Body)body) / 1000.0;
  return std::max(6.0 * r_km, 3000.0);
}

// Ce qui se pose PAR-DESSUS la scène sans en changer. La scène 3D continue de
// vivre derrière : le monde UE n'a pas à connaître ces états.
//   GameOver — la faillite est un GAME OVER [GDD économie stricte] : imposé par
//              le modèle, jamais par l'UI ;
//   Reglages — résolution / plein écran. ATTENTION : la référence n'a que trois
//              boutons au menu (ref_menu.png), cet écran est un AJOUT de
//              confort, atteint par une ligne discrète du panneau.
enum class Modal { Aucun = 0, GameOver, Reglages };

struct Session {
  Jeu jeu;
  SceneJeu scene{SceneJeu::Titre};
  Modal modal{Modal::Aucun};
  int  poste_ouvert{-1};             // ISS : poste de travail ouvert (-1 = aucun)
  bool quitter{false};
  std::string chemin_sauvegarde{"agence.sauvegarde.txt"};

  // La conception de véhicule en cours (poste CONCEPTION [GDD 12.2]). Vit ici
  // pour survivre à l'ouverture/fermeture du poste.
  VehicleDesign vehicule_design{VehicleDesign::starter()};

  // --- LA BOUCLE DE MISSION [GDD 4.1] (poste CONTROLE) ----------------------
  // La mission pilotée, son plan, et l'issue du dernier vol exécuté.
  int mission_pilotee{-1};              // index dans GameState::missions
  mission::MissionPlan  mission_plan;
  mission::FlightOutcome mission_outcome;
  bool mission_outcome_pret{false};

  // --- réglages d'affichage : UE les lit et applique, puis acquitte ---------
  int  res_choix{2};
  bool plein_ecran{false};
  bool appliquer_affichage{false};   // UE remet à false après application
  static constexpr int NB_RES = 5;
  int res_w() const { static const int W[NB_RES] = {1280,1360,1600,1920,2560}; return W[res_choix]; }
  int res_h() const { static const int H[NB_RES] = {720,880,900,1080,1440};    return H[res_choix]; }

  // --- sauvegardes multiples : une partie = un fichier .sav ------------------
  // Le dossier = celui de chemin_sauvegarde (Saved/ du projet). Le fichier
  // historique agence.sauvegarde.txt reste lisible (partie unique d'avant).
  struct SauvegardeItem { std::string label, chemin; };
  std::vector<SauvegardeItem> saves_listees;
  int  save_sel{0};
  bool saves_scannees{false};

  static std::string slug_agence(const std::string& nom) {
    std::string s;
    for (char c : nom) {
      const unsigned char u = static_cast<unsigned char>(c);
      if (std::isalnum(u)) s += static_cast<char>(std::tolower(u));
      else if (!s.empty() && s.back() != '_') s += '_';
    }
    while (!s.empty() && s.back() == '_') s.pop_back();
    return s.empty() ? std::string("agence") : s;
  }

  std::filesystem::path dossier_saves() const {
    const std::filesystem::path p{chemin_sauvegarde};
    return p.has_parent_path() ? p.parent_path() : std::filesystem::path{"."};
  }

  void scanner_sauvegardes() {
    saves_listees.clear();
    std::error_code ec;
    auto lire_entete = [](const std::filesystem::path& p, std::string& nom,
                          double& mois, int& reuss) {
      std::ifstream f(p); std::string ligne;
      if (!std::getline(f, ligne) || ligne.rfind("FENETRE_SAUVEGARDE", 0) != 0) return false;
      while (std::getline(f, ligne)) {
        if (ligne.rfind("nom=", 0) == 0)            nom   = ligne.substr(4);
        else if (ligne.rfind("mois=", 0) == 0)      mois  = std::atof(ligne.c_str() + 5);
        else if (ligne.rfind("reussites=", 0) == 0) reuss = std::atoi(ligne.c_str() + 10);
        else if (ligne.rfind("J ", 0) == 0)         break;   // journal : stop
      }
      return true;
    };
    auto ajouter = [&](const std::filesystem::path& p) {
      std::string nom = p.stem().string(); double mois = 0; int reuss = 0;
      if (!lire_entete(p, nom, mois, reuss)) return;
      char lb[160];
      std::snprintf(lb, sizeof lb, "%s   -   T+%.0f mois   -   %d reussite(s)",
                    nom.c_str(), mois, reuss);
      saves_listees.push_back({lb, p.string()});
    };
    if (std::filesystem::exists(dossier_saves(), ec))
      for (const auto& e : std::filesystem::directory_iterator(dossier_saves(), ec))
        if (e.path().extension() == ".sav") ajouter(e.path());
    // Partie historique (fichier unique d'avant les .sav). Après un chargement,
    // `chemin_sauvegarde` DEVIENT le .sav actif : sans ce garde, la partie
    // courante apparaissait deux fois dans la liste.
    if (std::filesystem::exists(chemin_sauvegarde, ec)) {
      const std::filesystem::path actif{chemin_sauvegarde};
      bool deja = false;
      for (const auto& s : saves_listees)
        if (std::filesystem::path{s.chemin} == actif) { deja = true; break; }
      if (!deja) ajouter(actif);
    }
    save_sel = 0;
    saves_scannees = true;
  }

  bool charger_partie(const std::string& chemin) {
    if (!jeu.charger(chemin)) return false;
    chemin_sauvegarde = chemin;            // la partie chargée devient l'active
    jeu.ares.assurer(jeu.agence, jeu.epoch_courant());
    jeu.ares.charger(chemin + ".ares");
    return true;
  }

  // Fonder une agence : la partie prend son propre fichier .sav (slug du nom),
  // dans le dossier des sauvegardes.
  void nouvelle_partie(const std::string& nom, ModeAide mode) {
    jeu.creer_agence(nom, mode);
    const std::filesystem::path f =
        dossier_saves() / (slug_agence(nom) + ".sav");
    chemin_sauvegarde = f.string();
    saves_scannees = false;
    poste_ouvert = -1;
    modal = Modal::Aucun;
    scene = SceneJeu::Station;             // on arrive À BORD, pas sur la carte
  }

  // Repartir après une faillite : le modèle est remis à zéro, on revient au
  // menu. C'est la SEULE sortie du game over avec le chargement d'une partie.
  void nouvelle_apres_faillite() {
    jeu.reinitialiser();
    modal = Modal::Aucun;
    poste_ouvert = -1;
    saves_scannees = false;
    scene = SceneJeu::Titre;
  }

  void sauvegarder_partie() {
    jeu.sauvegarder(chemin_sauvegarde);
    jeu.ares.sauvegarder(chemin_sauvegarde + ".ares");
    saves_scannees = false;                // le titre re-scannera
  }

  // ACCEPTER UN CONTRAT notifié [GDD 4.1, 10.2] : crée la Mission et répond au
  // mail. Refuse si le contrat n'a pas été notifié (10.2), s'il est déjà
  // accepté, ou si la couche ARES n'est pas prête. Le contrat entre en phase
  // PRÉREQUIS — la suite (conception, fenêtre, lancement) est la boucle de
  // mission, traitée à part.
  // Raison d'un refus d'acceptation (pour l'UI). Vide si accepté.
  std::string dernier_refus_contrat;

  bool accepter_contrat(const std::string& contract_id) {
    dernier_refus_contrat.clear();
    if (!jeu.ares.initialisee()) return false;
    auto& G = *jeu.ares.etat;
    if (!G.inbox.contract_notified(contract_id)) return false;   // [GDD 10.2]
    for (const auto& m : G.missions)
      if (m.contract.id == contract_id) return false;            // déjà acceptée
    for (const auto& e : G.catalog.entries()) {
      if (e.contract.id != contract_id) continue;

      // ═══ FILTRE DE CONFIANCE [GDD 13.4] ═══ : la crédibilité conditionne
      // l'exercice effectif du droit que le rang autorise.
      const double conf = G.career.confidence_ares;
      if (!economy::new_program_allowed(conf)) {
        dernier_refus_contrat = "confiance < 20 : aucun nouveau programme"; return false;
      }
      if (e.contract.crewed && !economy::crewed_allowed(conf)) {
        dernier_refus_contrat = "confiance < 60 : missions habitees suspendues"; return false;
      }
      const std::string& fam = e.contract.family;
      if ((fam == "mars_habite" || fam == "relativiste") && !economy::flagship_allowed(conf)) {
        dernier_refus_contrat = "confiance < 80 : programmes phares fermes"; return false;
      }
      // Contrats gelés par la chaîne financière [GDD 13.5].
      if (G.finance.contracts_frozen()) {
        dernier_refus_contrat = "contrats geles (crise budgetaire)"; return false;
      }

      mission::Mission m;
      m.contract = e.contract;
      m.state = mission::MissionState::Received;
      m.state_entered_days = G.clock.now_days();
      m.advance(mission::MissionState::Prerequisites, G.clock.now_days());
      G.missions.push_back(std::move(m));
      G.inbox.mark_answered("MAIL-" + contract_id);
      // ARES verse le budget à la signature ; ce que le joueur ne dépense pas,
      // il le garde [GDD 3.1]. Prélèvement des coûts au commit du programme.
      G.finance.credit(e.contract.terms.budget_musd);
      return true;
    }
    return false;
  }

  // ═══ PILOTER UNE MISSION [GDD 4.1] ═══ (poste CONTROLE)
  // Cible la première mission non terminale.
  void piloter_premiere_mission() {
    mission_pilotee = -1;
    mission_outcome_pret = false;
    if (!jeu.ares.initialisee()) return;
    auto& G = *jeu.ares.etat;
    for (int i = 0; i < static_cast<int>(G.missions.size()); ++i) {
      const auto st = G.missions[i].state;
      if (st != mission::MissionState::Completed && st != mission::MissionState::Failed &&
          st != mission::MissionState::Aborted) { mission_pilotee = i; return; }
    }
  }

  mission::Mission* mission_courante() {
    if (!jeu.ares.initialisee() || mission_pilotee < 0) return nullptr;
    auto& G = *jeu.ares.etat;
    if (mission_pilotee >= static_cast<int>(G.missions.size())) return nullptr;
    return &G.missions[static_cast<std::size_t>(mission_pilotee)];
  }

  void evaluer_plan() {
    if (mission::Mission* m = mission_courante()) {
      // Δv de trajectoire tiré de la géométrie RÉELLE de la fenêtre (Mars) ;
      // forfait par famille sinon. Rend le budget sensible à la fenêtre [7.3].
      mission_plan.dv_traj_override = mission::trajectory_dv_for_mission(
          *m, fen::Epoch{jeu.epoch_courant()}, jeu.eph);
      mission_plan.evaluate(*m);
    }
  }

  // Avance la mission d'UNE phase (le chemin nominal). Renvoie le gate : si le
  // refus est motivé, `reason` le dit. Gère les effets de bord de chaque
  // transition (commit financier, exécution du vol, conséquences du débrief).
  mission::GateResult avancer_mission() {
    mission::Mission* m = mission_courante();
    if (!m) return {false, "aucune mission pilotee"};
    auto& G = *jeu.ares.etat;
    using St = mission::MissionState;

    St target;
    switch (m->state) {
      case St::Received:      target = St::Prerequisites; break;
      case St::Prerequisites: target = St::Design; break;
      case St::Design:        evaluer_plan(); target = St::WindowSearch; break;
      case St::WindowSearch:  target = St::Qualification; break;
      case St::Qualification: target = St::Launched; break;
      case St::Launched:      target = St::Debrief; break;
      case St::Debrief:       target = mission_outcome.success ? St::Completed : St::Failed; break;
      default:                return {false, "mission terminee"};
    }

    const mission::GateResult g = mission::mission_gate(*m, mission_plan, target);
    if (!g.allowed) return g;

    // GATE GÉOMÉTRIQUE : on ne signe le passage en qualification (= viser une
    // fenêtre) que si le ciel est là. Positions réelles des corps [GDD 7.3].
    if (target == St::Qualification) {
      const mission::GateResult wg = mission::launch_window_gate(
          *m, fen::Epoch{jeu.epoch_courant()}, jeu.eph);
      if (!wg.allowed) return wg;
    }

    // COMMIT : le programme se paie à l'engagement du feu vert. Irréversible.
    // Prélèvement sur les FINANCES v1.2 : trésorerie puis réserve [GDD 13.4].
    if (target == St::Launched) {
      if (!G.finance.engage(mission_plan.assessment.cost_total))
        return {false, "fonds insuffisants pour engager le programme"};
    }

    m->advance(target, G.clock.now_days());

    // LE VOL S'EXÉCUTE : issue déterministe (graine agence + mission).
    if (target == St::Debrief) {
      mission_outcome = mission::fly_mission(
          *m, mission_plan,
          mission::mission_seed(jeu.agence.graine_agence, m->contract.id));
      mission_outcome_pret = true;
    }

    // DÉBRIEF : triple lecture appliquée à TOUS les systèmes [GDD 10.4].
    if (target == St::Completed || target == St::Failed) {
      if (mission_outcome.success) {
        jeu.agence.reussites += 1;
        // GAIN DE CONFIANCE [GDD 13.4] : +2..+5 nominal, davantage pour un
        // objectif difficile. Barème simple : +3, +6 si mission phare.
        const std::string& fam = m->contract.family;
        const double gain = (fam == "mars_habite" || fam == "relativiste" || m->contract.crewed) ? 6.0 : 3.0;
        G.career.confidence_ares = std::min(100.0, G.career.confidence_ares + gain);
      } else {
        jeu.agence.echecs += 1;
        if (mission_outcome.has_anomaly)
          G.apply_anomaly(*m, mission_outcome.anomaly);   // dont perte de confiance
      }
      mission_outcome_pret = false;
    }
    return {true, ""};
  }

  // Quitter la partie : on sauve, puis retour au menu.
  void retour_menu() {
    sauvegarder_partie();
    poste_ouvert = -1;
    scene = SceneJeu::Titre;
  }

  // -------------------------------------------------------------------------
  // LA FRAME : mise à jour d'état + publication du pont. Appelée une fois par
  // frame par USPGameSubsystem, AVANT que le monde UE ne lise le pont.
  void tick(double dt_reel) {
    (void)dt_reel;
    // couche ARES : création/reset/rattrapage mensuel (lecture seule sur l'agence)
    jeu.ares.assurer(jeu.agence, jeu.epoch_courant());

    // garde-fous de routage : sans agence créée, aucune scène de jeu.
    if ((scene == SceneJeu::Carte || scene == SceneJeu::Station) && !jeu.agence.creee)
      scene = SceneJeu::Titre;

    // LA FAILLITE EST UN GAME OVER, imposé par le MODÈLE [économie stricte] :
    // c'est le seul état que l'UI ne peut pas refuser. Il se pose par-dessus la
    // scène en cours ; seules « nouvelle partie » et « charger » en sortent.
    // Au menu il n'y a plus de partie à condamner : la modale y est levée, même
    // si le modèle porte encore le drapeau (le joueur a déjà quitté la partie).
    if (scene == SceneJeu::Titre) {
      if (modal == Modal::GameOver) modal = Modal::Aucun;
    } else if (jeu.game_over) {
      modal = Modal::GameOver;
    } else if (modal == Modal::GameOver) {
      modal = Modal::Aucun;
    }

    // pont rendu 3D : chaque subsystem UE s'active pour SA scène.
    g_render_bridge.scene = static_cast<int>(scene);
    g_render_bridge.carte3d_active = (scene == SceneJeu::Carte);
    // Le menu n'a plus de fond peint : c'est la carte, en retrait, qui lui sert
    // de décor (ciel étoilé + orbites ténues) — format ref_menu.png.
    g_render_bridge.menu_backdrop = (scene == SceneJeu::Titre);

    if (scene == SceneJeu::Station) publier_postes();
    // Publié dans TOUTES les scènes : la carte sert aussi de décor au menu, et
    // l'époque doit y être définie (sinon le décor est figé à J2000).
    publier_carte();

    if (!jeu.mc_en_cours && jeu.mc_resultat >= 0) jeu.encaisser_mc();
  }

  // -------------------------------------------------------------------------
  // PUBLICATION DE LA CARTE — extrait verbatim de ui/carte3d_ecran.hpp, moins
  // le dessin ImGui. Sens unique : le jeu écrit, le rendu lit, aucun recalcul
  // de physique côté rendu.
  void publier_carte() {
    auto& B = g_render_bridge;
    const auto& vi = jeu.vinterp;
    const bool vol_interp = vi.commis && !vi.fini && vi.arc_t.size() >= 2;
    const bool vol_geo = jeu.vol.commis && !jeu.vol.fini;

    // ÉPOQUE = MAINTENANT [GDD 14]. Un vol interplanétaire en cours EST l'horloge
    // de jeu la plus avancée ; sinon, calendrier agence. (Pendant un vol GEO —
    // heures/jours — les planètes ne bougent pas perceptiblement : calendrier
    // agence, déclaré suffisant.)
    const double epoch = vol_interp ? vi.t : jeu.epoch_courant();
    B.epoch_tdb = epoch;

    // --- flotte en service : éphéméride PAR ENGIN [GDD 8.3] ------------------
    // Position ESTIMÉE de chaque engin à l'époque de jeu, relative à son corps
    // de référence. Le calcul vit dans app::Jeu (modèle déclaré) — ici on publie.
    {
      int n = 0;
      for (const auto& e : jeu.flotte) {
        if (n >= RenderBridge::FleetSnap::MAX) break;
        auto& c = B.fleet.craft[n];
        c.type = e.type;
        c.parent = jeu.flotte_parent(e);
        const Vec3 p = jeu.flotte_position_rel(e, epoch);
        c.rel_m[0] = p.x; c.rel_m[1] = p.y; c.rel_m[2] = p.z;
        ++n;
      }
      B.fleet.n = n;
      B.fleet.vol_geo_actif = vol_geo;
    }

    // --- vol interplanétaire : nominale + estimé + corridor + nœuds ----------
    if (vol_interp) {
      // interpolation linéaire de l'arc nominal à l'instant t (position ESTIMÉE :
      // dans le modèle v0.6 l'estimé suit la nominale, l'incertitude vit dans
      // ellipse_km — c'est le corridor, pas un troisième tracé. Déclaré.)
      auto pos_arc = [&vi](double t, double& x_ua, double& y_ua) {
        std::size_t k = 1;
        while (k + 1 < vi.arc_t.size() && vi.arc_t[k] < t) ++k;
        const double t0 = vi.arc_t[k - 1], t1 = vi.arc_t[k];
        const double a = (t1 > t0) ? (t - t0) / (t1 - t0) : 0.0;
        const double f = a < 0.0 ? 0.0 : (a > 1.0 ? 1.0 : a);
        x_ua = vi.arc_x[k - 1] + f * (vi.arc_x[k] - vi.arc_x[k - 1]);
        y_ua = vi.arc_y[k - 1] + f * (vi.arc_y[k] - vi.arc_y[k - 1]);
      };

      // l'arc nominal est FIGÉ au commit : publié une seule fois
      const double sig = vi.t_dep + static_cast<double>(vi.arc_t.size());
      if (B.last_arc_sig != sig) {
        const int n_src = static_cast<int>(vi.arc_t.size());
        const int n = n_src < RenderBridge::VehicleSnap::MAX_PTS
                          ? n_src : RenderBridge::VehicleSnap::MAX_PTS;
        for (int i = 0; i < n; ++i) {
          const int k = (n_src - 1) * i / (n - 1);
          B.vehicle.traj_m[i][0] = vi.arc_x[k] * cst::AU;
          B.vehicle.traj_m[i][1] = vi.arc_y[k] * cst::AU;
          B.vehicle.traj_m[i][2] = 0.0;
        }
        B.vehicle.n = n;
        B.last_arc_sig = sig;
        B.vehicle.gen.fetch_add(1);
      }

      double x, y;
      pos_arc(vi.t, x, y);
      B.vehicle.pos_m[0] = x * cst::AU;
      B.vehicle.pos_m[1] = y * cst::AU;
      B.vehicle.pos_m[2] = 0.0;
      B.vehicle.corridor_3s_m = vi.ellipse_km * 1000.0;   // 3σ courante du modèle

      // nœuds de manœuvre restants [GDD 8.3] : TCM1 / TCM2 sur l'arc
      int n_nodes = 0;
      const double t_noeuds[2] = {vi.t_tcm1, vi.t_tcm2};
      const bool faits[2] = {vi.tcm1_faite, vi.tcm2_faite};
      for (int i = 0; i < 2; ++i) {
        if (t_noeuds[i] <= vi.arc_t.front() || t_noeuds[i] >= vi.arc_t.back()) continue;
        pos_arc(t_noeuds[i], x, y);
        B.vehicle.nodes_m[n_nodes][0] = x * cst::AU;
        B.vehicle.nodes_m[n_nodes][1] = y * cst::AU;
        B.vehicle.nodes_m[n_nodes][2] = 0.0;
        B.vehicle.node_done[n_nodes] = faits[i];
        ++n_nodes;
      }
      B.vehicle.n_nodes = n_nodes;
      B.vehicle.valid = true;
    } else {
      B.vehicle.valid = false;
      B.last_arc_sig = -1.0;
    }

    // --- vol GEO : trace estimée + orbite cible [GDD 8.3, 7.5] --------------
    // À l'échelle VRAIE, l'orbite GEO (42 164 km) se voit directement sur la
    // carte dès qu'on focalise la Terre.
    if (vol_geo && jeu.vol.traj_t.size() >= 2) {
      const auto& vo = jeu.vol;
      const double sig = static_cast<double>(vo.traj_t.size());
      if (B.last_geo_sig != sig) {           // la trace s'allonge : republier
        const int n_src = static_cast<int>(vo.traj_t.size());
        const int n = n_src < RenderBridge::GeoFlightSnap::MAX_PTS
                          ? n_src : RenderBridge::GeoFlightSnap::MAX_PTS;
        for (int i = 0; i < n; ++i) {
          const int k = (n_src - 1) * i / (n - 1);
          B.geo.traj_km[i][0] = vo.traj_x[k];
          B.geo.traj_km[i][1] = vo.traj_y[k];
          B.geo.traj_km[i][2] = vo.traj_z[k];
        }
        B.geo.n = n;
        B.last_geo_sig = sig;
        B.geo.gen.fetch_add(1);
      }
      const Vec3 p = jeu.vol_position_estimee();   // km — l'ESTIMÉ, pas la vérité
      B.geo.pos_km[0] = p.x;
      B.geo.pos_km[1] = p.y;
      B.geo.pos_km[2] = p.z;
      B.geo.sigma_km = vo.sigma_pos_km;
      B.geo.target_sma_km =
          (jeu.actif() ? jeu.actif()->cible_sma : 42164170.0) / 1000.0;
      B.geo.valid = true;
    } else {
      B.geo.valid = false;
      B.last_geo_sig = -1.0;
    }
  }
};

} // namespace fen::app
