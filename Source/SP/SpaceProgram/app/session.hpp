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
  if (body == FOCUS_STATION) return 1.0;            // Novellus (~55 m) cadré de près (km)
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

// ═══ VOL DE CAMÉRA [GDD v1.2 ch.8.3, 17.4] ═══
// [M] n'est pas une bascule sèche mais un VOL continu entre le plan bord et le
// plan système, ancré sur la Terre (là où est Novellus). La caméra du système
// existe déjà et sait « voler » (lissage log de la distance de vue) : ce modèle
// PILOTE simplement sa distance cible le long d'un chemin lissé, en C++ pur donc
// sous oracle. La coexistence VISUELLE des deux rendus (voir l'intérieur grandir
// dans le zoom) demande le moteur ch.18 (incrément 3) — ici, un seul plan rend à
// la fois, mais le MOUVEMENT de caméra est continu, plus une coupure sèche.
enum class SensVol { VersSysteme = 0, VersBord = 1 };

struct VolCamera {
  bool     actif{false};
  SensVol  sens{SensVol::VersSysteme};
  double   progres{0.0};           // 0..1
  double   duree_s{0.9};
  double   dist_depart_km{0.0};
  double   dist_arrivee_km{0.0};

  // Distance de vue courante : interpolation LOGARITHMIQUE (du rayon terrestre à
  // 6 UA il y a des ordres de grandeur) sous un lissage smoothstep (départ et
  // arrivée sans à-coup).
  double dist_courante_km() const {
    const double p = progres < 0.0 ? 0.0 : (progres > 1.0 ? 1.0 : progres);
    const double e = p * p * (3.0 - 2.0 * p);
    const double a = std::log(dist_depart_km);
    const double b = std::log(dist_arrivee_km);
    return std::exp(a + (b - a) * e);
  }
  void avancer(double dt) {
    if (!actif || duree_s <= 0.0) { progres = 1.0; return; }
    progres += dt / duree_s;
    if (progres > 1.0) progres = 1.0;
  }
  bool fini() const { return progres >= 1.0; }
};

struct Session {
  Jeu jeu;
  SceneJeu scene{SceneJeu::Titre};
  // Cadrage de la caméra DANS le Monde unique [GDD v1.2 ch.17.4] : à bord
  // (ambulation 1re personne) ou tiré au plan système (ex-« carte »). [M] le
  // bascule ; ce n'est jamais un changement de scène.
  Cadrage  cadrage{Cadrage::Bord};
  VolCamera vol_cam;                  // vol de caméra [M] en cours (voir plus bas)
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
    scene = SceneJeu::Monde;               // on entre DANS le monde unique...
    cadrage = Cadrage::Bord;               // ...à bord de Novellus [GDD 1.4, 11.1]
  }

  // Repartir après une faillite : le modèle est remis à zéro, on revient au
  // menu. C'est la SEULE sortie du game over avec le chargement d'une partie.
  void nouvelle_apres_faillite() {
    jeu.reinitialiser();
    modal = Modal::Aucun;
    poste_ouvert = -1;
    cadrage = Cadrage::Bord;
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

  // ═══ VOL DE CAMÉRA [M] ═══ (voir VolCamera plus haut)
  static constexpr double DIST_SYSTEME_KM = 9.0e8;   // ~6 UA : le système interne cadré
  // Distance « bord » : au ras de la Terre (là où orbite Novellus), Terre plein
  // cadre. Même borne serrée que le zoom manuel de la carte (rayon × 1.15).
  double dist_bord_km() const {
    return ephem::body_radius(ephem::Body::EarthBary) / 1000.0 * 1.15;
  }

  // [M] : lance le vol continu entre bord et système, ancré sur la Terre.
  void demarrer_vol_cadrage() {
    if (vol_cam.actif || scene != SceneJeu::Monde) return;   // un vol à la fois
    vol_cam.actif = true;
    vol_cam.progres = 0.0;
    if (cadrage == Cadrage::Bord) {
      // On quitte le bord : la vue système s'ouvre AU RAS de la Terre puis recule.
      cadrage = Cadrage::Systeme;                 // le plan système rend dès maintenant
      vol_cam.sens = SensVol::VersSysteme;
      vol_cam.dist_depart_km  = dist_bord_km();
      vol_cam.dist_arrivee_km = DIST_SYSTEME_KM;
    } else {
      // On rentre à bord : la caméra plonge vers la Terre, la main passe à la 1re
      // personne À L'ARRIVÉE (fin du vol).
      vol_cam.sens = SensVol::VersBord;
      vol_cam.dist_depart_km  = std::max(1.0, g_render_bridge.cam.dist_km.load());
      vol_cam.dist_arrivee_km = dist_bord_km();
    }
    g_render_bridge.focus_body = static_cast<int>(ephem::Body::EarthBary);
    g_render_bridge.cam.dist_km = vol_cam.dist_depart_km;
  }

  // Échap depuis le système : retour IMMÉDIAT à bord (coupe tout vol en cours).
  void retour_bord_immediat() {
    vol_cam.actif = false;
    cadrage = Cadrage::Bord;
  }

  // -------------------------------------------------------------------------
  // LA FRAME : mise à jour d'état + publication du pont. Appelée une fois par
  // frame par USPGameSubsystem, AVANT que le monde UE ne lise le pont.
  void tick(double dt_reel) {
    // couche ARES : création/reset/rattrapage mensuel (lecture seule sur l'agence)
    jeu.ares.assurer(jeu.agence, jeu.epoch_courant());

    // garde-fous de routage : sans agence créée, on ne peut pas être dans le Monde.
    if (scene == SceneJeu::Monde && !jeu.agence.creee)
      scene = SceneJeu::Titre;
    if (scene != SceneJeu::Monde) vol_cam.actif = false;   // pas de vol hors du Monde

    // ═══ VOL DE CAMÉRA [M] ═══ : avance la transition et PILOTE la distance de
    // vue cible (le lissage côté rendu la suit). À l'arrivée d'un vol de RETOUR,
    // la main passe à la 1re personne (cadrage Bord).
    if (vol_cam.actif) {
      vol_cam.avancer(dt_reel);
      g_render_bridge.focus_body = static_cast<int>(ephem::Body::EarthBary);
      g_render_bridge.cam.dist_km = vol_cam.dist_courante_km();
      if (vol_cam.fini()) {
        if (vol_cam.sens == SensVol::VersBord) cadrage = Cadrage::Bord;
        vol_cam.actif = false;
      }
    }

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

    // pont rendu 3D : le Monde est unique ; le CADRAGE dit quel plan la caméra
    // occupe. Le rendu station s'active au cadrage Bord, le rendu système au
    // cadrage Systeme — les deux sont le MÊME monde [GDD v1.2 ch.17.3].
    g_render_bridge.scene = static_cast<int>(scene);
    g_render_bridge.carte3d_active =
        (scene == SceneJeu::Monde && cadrage == Cadrage::Systeme);
    // Le menu n'a plus de fond peint : c'est le monde vu au plan système, en
    // retrait, qui lui sert de décor (ciel étoilé + orbites ténues) — ref_menu.png.
    g_render_bridge.menu_backdrop = (scene == SceneJeu::Titre);

    // Les postes ne sont publiés qu'à bord (cadrage Bord) : c'est là qu'on les
    // approche à pied.
    if (scene == SceneJeu::Monde && cadrage == Cadrage::Bord) publier_postes();
    // Publié dans TOUTES les scènes : la carte sert aussi de décor au menu, et
    // l'époque doit y être définie (sinon le décor est figé à J2000).
    publier_carte();
  }

  // -------------------------------------------------------------------------
  // PUBLICATION DE LA CARTE — extrait verbatim de ui/carte3d_ecran.hpp, moins
  // le dessin ImGui. Sens unique : le jeu écrit, le rendu lit, aucun recalcul
  // de physique côté rendu.
  void publier_carte() {
    auto& B = g_render_bridge;

    // ÉPOQUE = MAINTENANT [GDD 14] : le calendrier de l'agence pilote l'état du
    // monde. (Les anciens vols 2D — GEO/interplanétaire — qui avançaient jadis
    // cette horloge ont été retirés avec la mécanique de vol héritée.)
    const double epoch = jeu.epoch_courant();
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
      B.fleet.vol_geo_actif = false;   // vol GEO 2D retiré (mécanique héritée)
    }

    // --- NOVELLUS dans le monde [GDD v1.2 11.1, 17.3] -----------------------
    // La station a une position RÉELLE dans le monde unique : orbite circulaire
    // LEO (418 km) autour de la Terre, plan écliptique. On RÉUTILISE le helper de
    // la flotte (aucune physique dupliquée côté rendu) : un relais GEO n'est
    // qu'un cercle képlérien rel. Terre — Novellus est le même modèle, en LEO.
    {
      EnginFlotte nv;
      nv.type   = EnginFlotte::RelaisGeo;   // -> parent Terre, cercle en plan écliptique
      nv.sma_m  = ephem::body_radius(ephem::Body::EarthBary) + 418000.0;  // R_Terre + 418 km
      nv.phase0 = 0.0;
      nv.t0     = 0.0;
      const Vec3 p = jeu.flotte_position_rel(nv, epoch);
      B.station.rel_m[0] = p.x; B.station.rel_m[1] = p.y; B.station.rel_m[2] = p.z;
      B.station.altitude_km = 418.0;
      B.station.envergure_m = 109.0;         // envergure RÉELLE de l'ISS (~109 m, arrays comprises)
      B.station.valid = true;
    }

    // Les tracés de vol 2D (interplanétaire + GEO) ont été retirés avec la
    // mécanique de vol héritée de `app::Jeu`. Le pont garde ses champs
    // `vehicle`/`geo` invalides : le rendu les ignore (ils sont réintroduits par
    // la boucle de mission vécue, GDD 9, quand elle publiera une vraie trace).
    B.vehicle.valid = false;
    B.geo.valid = false;
  }
};

} // namespace fen::app
