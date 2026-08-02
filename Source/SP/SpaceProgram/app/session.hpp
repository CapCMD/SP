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
#include "app/novellus_orbite.hpp"
#include "app/postes.hpp"
#include "app/vehicle_design.hpp"
#include "fen/career/Carnet.hpp"        // ce que le carnet retient [GDD 15.4]
#include "fen/mission/Assistance.hpp"   // les tours d'assistance [GDD 5.11]
#include "fen/code/CodeQualification.hpp"
#include "fen/code/Toolchain.hpp"
#include "fen/ephem/Ephemeris.hpp"
#include "fen/mission/Crew.hpp"          // comms_delay_s / ground_loop_closes
#include "fen/mission/FlightTrace.hpp"
#include "fen/mission/MissionLoop.hpp"
#include "fen/mission/Graphe.hpp"
#include "fen/mission/Manoeuvre.hpp"
#include "fen/mission/NavSolution.hpp"
#include "fen/mission/Navigation.hpp"

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
// `Passation` [GDD 3.4, 3.5] : l'Architecte s'est éteint de mort NATURELLE. Ce
// n'est pas une fin de partie — c'est un changement de titulaire, et le joueur
// doit le constater avant de reprendre. Distincte de `GameOver`, qui est sec.
enum class Modal { Aucun = 0, GameOver, Reglages, Passation };

// ═══ VOL DE CAMÉRA [GDD v1.2 ch.8.3, 17.4] ═══
// [M] n'est pas une bascule sèche mais un VOL continu entre le plan bord et le
// plan système, ancré sur NOVELLUS elle-même (c'est vers la station qu'on plonge,
// c'est d'elle qu'on s'éloigne). La caméra du système existe déjà et sait
// « voler » (lissage log de la distance de vue) : ce modèle PILOTE sa distance ET
// sa direction le long d'un chemin lissé, en C++ pur donc sous oracle.
//
// LE HANDOFF (incr. 3c-3) : le vol de retour ne s'arrête pas « quelque part
// au-dessus de la Terre » puis coupe — il AMARRE la caméra sur la position et le
// regard de l'œil du pawn. Deux seuils, distincts :
//   . enveloppe de la station franchie -> la géométrie INTÉRIEURE rend (le
//     modèle extérieur s'efface) : bascule de LOD, faite là où elle est le moins
//     visible, au passage de la coque ;
//   . fin du vol -> la MAIN passe à la 1re personne, alors que la caméra est
//     déjà, au pixel, celle du pawn : la reprise est invisible.
enum class SensVol { VersSysteme = 0, VersBord = 1 };

struct VolCamera {
  bool     actif{false};
  SensVol  sens{SensVol::VersSysteme};
  double   progres{0.0};           // 0..1
  double   duree_s{0.9};
  double   dist_depart_km{0.0};
  double   dist_arrivee_km{0.0};
  // ORBITE de caméra : pour amarrer l'œil sur celui du pawn, la DIRECTION compte
  // autant que la distance — la distance seule laisserait la caméra arriver par
  // n'importe quel côté de la station.
  double   yaw_depart{0.0},   yaw_arrivee{0.0};
  double   pitch_depart{0.0}, pitch_arrivee{0.0};

  // Départ et arrivée sans à-coup (smoothstep).
  static double lissage(double p) {
    const double c = p < 0.0 ? 0.0 : (p > 1.0 ? 1.0 : p);
    return c * c * (3.0 - 2.0 * c);
  }

  // Distance de vue courante : interpolation LOGARITHMIQUE (de l'intérieur d'un
  // module à 6 UA il y a dix ordres de grandeur).
  double dist_courante_km() const {
    const double a = std::log(dist_depart_km);
    const double b = std::log(dist_arrivee_km);
    return std::exp(a + (b - a) * lissage(progres));
  }
  // Le yaw se parcourt par le PLUS COURT chemin : sans repli dans ±π, un vol
  // pouvait faire presque un tour complet autour de la station.
  double yaw_courant() const {
    // UNE INTERPOLATION DOIT ATTERRIR EXACTEMENT SUR SON EXTRÉMITÉ. Le repli
    // dans ±π donne le plus court chemin, mais il fait arriver à
    // `yaw_arrivee ± 2π` quand l'écart de départ franchit un demi-tour : le même
    // angle à l'écran, un chiffre différent — donc un oracle qui tombe un jour
    // et pas un autre, l'attitude de Novellus dépendant de l'ÉPOQUE RÉELLE
    // [GDD 14.1] (piège n°67).
    if (progres >= 1.0) return yaw_arrivee;
    constexpr double DEUX_PI = 6.283185307179586476925287;
    double d = yaw_arrivee - yaw_depart;
    while (d >  DEUX_PI * 0.5) d -= DEUX_PI;
    while (d < -DEUX_PI * 0.5) d += DEUX_PI;
    return yaw_depart + d * lissage(progres);
  }
  double pitch_courant() const {
    return pitch_depart + (pitch_arrivee - pitch_depart) * lissage(progres);
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
  // LE DÉTAIL DU DERNIER SCORE [GDD 3.3] : le joueur doit pouvoir lire LEQUEL des
  // trois critères l'a fait progresser — un total nu n'apprend rien. Vit sur la
  // session (c'est de l'affichage), le cumul étant sur `career.score`.
  career::MissionScore dernier_score_mission{};
  mission::MissionPlan  mission_plan;
  mission::FlightOutcome mission_outcome;
  bool mission_outcome_pret{false};

  // ═══ LE TOUR D'ASSISTANCE, CALCULÉ À LA DEMANDE ═══ [GDD 5.11, 7.4]
  // Résoudre les époques d'un tour coûte 0,5 s (un survol) à 1,8 s (trois) :
  // c'est un calcul de PLANIFICATION, pas de rafraîchissement d'écran. Il ne
  // tourne donc QUE sur demande explicite du joueur (`choisir_tour`), et son
  // résultat vit ici — exactement comme une trajectoire qu'un bureau d'études
  // calcule une fois et garde. `evaluer_plan`, qui est rappelé à chaque
  // reconstruction du poste, ne fait que LIRE ce bilan.
  mission::BilanTour tour_bilan{};
  // ═══ L'ALTITUDE MINIMALE EXIGÉE DU SURVOL ═══ [GDD 3.1, 8.5]
  // Décision d'architecte, au même titre que la marge de correction ou le
  // blindage : l'optimiseur colle toujours le périastre à sa borne basse, donc
  // c'est cette borne qui décide. Viser plus haut élargit le corridor du plan-B
  // et se paie en Δv. 0 = l'altitude du vol de référence (celle du catalogue).
  double alt_survol_min_km{0.0};
  // CE QUE LE SURVOL EXIGE [GDD 8.4] — corridor en paramètre d'impact, dispersion
  // résiduelle et P(survol). Nul pour un transfert direct, qui n'en a pas.
  mission::SurvolNav nav_survol_{};
  std::string        tour_bilan_id;      // le tour auquel ce bilan correspond
  std::string        tour_bilan_mission; // et la mission pour laquelle il a été calculé
  double             tour_bilan_epoch{0.0};   // l'époque à laquelle il a été calculé

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

  // Dispersion de navigation du dernier plan évalué, gardée pour l'affichage :
  // c'est elle qui explique le P(succès) au lieu de le décréter.
  mission::NavDispersion nav_disp;

  // LE Δv COMMANDÉ PAR LE JOUEUR, en repère RSW (m/s). Vit sur la session
  // parce que c'est une saisie en cours, pas un fait du vol : il ne devient un
  // fait qu'à l'exécution.
  double tcm_commande[3]{0.0, 0.0, 0.0};

  // LE GRAPHE que le joueur assemble (mode Normal) [GDD 2.2]. Vit sur la
  // session : c'est un raisonnement en cours, pas un fait du vol — et il ne se
  // sauvegarde pas, puisque [GDD 2.4] veut qu'on le REFASSE a chaque analyse.
  std::vector<mission::TypeNoeud> graphe;

  // ═══ LE PASSAGE NORMAL → PRO [GDD 2.3] ═══
  // « Le passage Normal → Pro est possible et UNIDIRECTIONNEL ; les graphes
  // existants sont archivés en lecture seule dans le carnet (consultables, non
  // exécutables), le joueur devant réécrire en C++. Cette perte est
  // INTENTIONNELLE. »
  //
  // Le mode n'était écrit qu'à la création d'une partie : la bascule n'existait
  // pas, alors que `GameState.hpp` la déclarait en commentaire depuis toujours.
  // Trois effets, dans cet ordre, et le second est le prix du premier :
  //   1. le graphe courant part au CARNET, en texte — donc consultable et
  //      inexécutable par construction, pas par interdiction ;
  //   2. il est VIDÉ : en Pro, le calcul se réécrit ;
  //   3. le mode bascule, et rien ne le ramène.
  // Rend false si l'on est déjà en Pro : c'est le sens de « unidirectionnel ».
  bool basculer_en_pro() {
    if (jeu.agence.mode == ModeAide::Pro) return false;
    if (!jeu.ares.initialisee()) return false;
    auto& G = *jeu.ares.etat;
    const double now = G.clock.now_days();
    G.notebook.write(career::archive_graphe(graphe, now));
    // LES MAN PAGES SUIVENT L'ARCHIVE, et c'est le bon moment : le joueur perd
    // ses nœuds à l'instant précis où il doit apprendre les fonctions qu'ils
    // étaient. La page les NOMME toutes [GDD 2.2, 15.4].
    G.notebook.write(career::man_pages_api(now));
    graphe.clear();
    jeu.agence.mode = ModeAide::Pro;
    return true;
  }

  // ═══════════════════════════════════════════════════════════════════════════
  // LE LOGICIEL DE VOL DU JOUEUR — mode PRO [GDD 15.1, 15.5, 18]
  // ═══════════════════════════════════════════════════════════════════════════
  // En PRO, il n'y a pas de graphe : le joueur ÉCRIT le code qui décidera à sa
  // place. La chaîne est celle de [GDD 15.5], dans cet ordre et sans raccourci :
  //   1. COMPILER      — coût nul, instantané ; les diagnostics tels quels ;
  //   2. BANC D'ESSAI  — coûte du budget ET des jours, rend une CERTIFICATION
  //                      avec son domaine de validité (il RASSURE, il ne
  //                      garantit pas) ;
  //   3. TÉLÉVERSER    — seul un code CERTIFIÉ monte à bord ;
  //   4. en vol, le code s'EXÉCUTE sur la solution de navigation réelle et sa
  //      manœuvre atterrit dans `tcm_commande`.
  //
  // UN TEXTE MODIFIÉ N'EST PLUS LE TEXTE QUALIFIÉ. On garde donc une copie de
  // ce qui a été compilé et de ce qui a été certifié : éditer une ligne après
  // le banc invalide la fiche, sinon la certification porterait sur un code que
  // personne n'a jamais exercé.
  // LE POSTE CONTRÔLE A DEUX FACES en mode Pro : la conduite de mission et
  // l'atelier logiciel. Le cadre d'un poste CLIPPE le texte (piège n°42) — un
  // éditeur, ses diagnostics et sa fiche de qualification ne tiennent pas sous
  // dix lignes de vol. Ce drapeau vit sur la session, à côté de `poste_ouvert`
  // et pour la même raison : une vue qu'aucune capture ne peut atteindre est une
  // vue que personne ne vérifie.
  bool atelier_logiciel = false;
  std::string source_vol = code::squelette_vol();
  std::string source_compilee;              // le texte de la dernière compilation
  std::string source_certifiee;             // le texte que le banc a exercé
  std::string source_bord;                  // le texte RÉELLEMENT à bord
  code::ToolchainConfig toolchain;          // chemins FOURNIS par la plateforme
  // OÙ DÉPOSER LE COMPILATEUR quand il manque [GDD 18]. Renseigné par la couche
  // plateforme, qui seule sait où le jeu est installé. Sans lui, l'atelier
  // refuserait sans dire ce qu'il attend — une panne aux yeux du joueur alors
  // que la réparation est à sa portée (piège n°42).
  std::string toolchain_depot;
  code::ResultatToolchain resultat_vol;     // dernière compilation / exécution
  bool        resultat_vol_lu = false;      // a-t-on déjà lancé la chaîne ?
  code::Certification cert_vol;
  bool        code_a_bord = false;
  // Les paramètres de la campagne d'essai — ce que le joueur DÉCLARE avoir
  // testé. Ils fixent le domaine de validité, donc ce que la fiche vaut.
  double banc_heures = 200.0;
  double banc_borne_sigma3_m = 12000.0;     // plage d'incertitude 3σ couverte
  bool   banc_degrade = false;              // profils dégradés exercés ?
  bool   banc_interfaces = false;           // interfaces inter-modules exercées ?

  bool source_vol_compilee() const {
    return resultat_vol_lu && source_compilee == source_vol;
  }
  bool source_vol_certifiee() const {
    return cert_vol.certified && source_certifiee == source_vol;
  }
  bool source_vol_a_bord() const { return code_a_bord && source_bord == source_vol; }

  // L'ENVIRONNEMENT que ce code doit couvrir. Il n'est pas choisi dans un menu :
  // il DÉCOULE du profil de vol de la mission. C'est ce qui donne son mordant à
  // [GDD 15.5] — « un code qualifié en orbite basse n'est PAS qualifié pour
  // Mars » : qualifier sur une mission et voler sur une autre se voit.
  static const char* env_vol(const mission::Mission& m) {
    switch (mission::flight_profile_of(m.contract.family)) {
      case mission::FlightProfile::LeoRendezvous:  return "orbite_basse";
      case mission::FlightProfile::GeoTransfer:    return "transfert_geo";
      case mission::FlightProfile::Surface:        return "surface";
      case mission::FlightProfile::Continuous:     return "poussee_continue";
      default:                                     return "croisiere";
    }
  }

  code::ValidityDomain domaine_vise(const mission::Mission& m) const {
    code::ValidityDomain d;
    d.environment = env_vol(m);
    d.input_lo = 0.0;
    d.input_hi = banc_borne_sigma3_m;
    d.degraded_profiles = banc_degrade;
    d.interfaces_tested = banc_interfaces;
    return d;
  }

  // CE QUE LE CODE DE VOL REÇOIT. Rien de plus que ce que le joueur voit
  // lui-même : la solution de navigation, jamais la vérité [GDD 7.5].
  code::EntreesVol entrees_vol(const mission::Mission& m) {
    code::EntreesVol e;
    const mission::VueNavigation vn = vue_vol(m);
    if (!vn.ok) return e;
    e.pos = vn.r_estime;
    e.vel = vn.v_estime;
    e.sigma3_m = 3.0 * vn.sigma_r;
    // Le point de visée RAMENÉ À MAINTENANT (voir `EntreesVol::cible`) : l'écart
    // que l'API rendra est alors exactement le manque au but projeté.
    e.cible = vn.r_estime - vn.manque_projete;
    e.tolerance_m = mission::ARRIVEE_TOLERANCE_KM * 1000.0;
    e.dv_disponible = std::max(0.0, mission_plan.program.dv_margin - m.tcm_dv_depense);
    e.tau_s = std::max(1.0, vn.reste_jours * cst::DAY);
    return e;
  }

  // ÉTAPE 1 — COMPILER. Coût nul [GDD 15.5]. Hors vol, on n'a pas de solution de
  // navigation : on compile alors contre un jeu d'entrées NEUTRE, ce qui suffit
  // à dire si le texte est du C++ valide — la seule question de cette étape.
  bool compiler_vol(const mission::Mission* m) {
    code::EntreesVol e;
    if (m) e = entrees_vol(*m);
    resultat_vol = code::compiler_et_executer(source_vol, e, toolchain);
    resultat_vol_lu = true;
    source_compilee = source_vol;
    // Une compilation réussie invalide toute certification plus ancienne : la
    // fiche appartient au texte, pas au joueur.
    if (source_certifiee != source_vol) cert_vol = {};
    return resultat_vol.issue != code::IssueCode::ErreurCompilation &&
           resultat_vol.issue != code::IssueCode::Indisponible;
  }

  // ÉTAPE 2 — LE BANC D'ESSAI. Il COÛTE : du budget, et des jours de calendrier
  // qui repoussent la fenêtre. Refuse si l'agence ne peut pas payer — un banc
  // qu'on ne facture pas ne serait pas un arbitrage [GDD 15.5].
  bool banc_essai_vol(const mission::Mission& m) {
    if (!jeu.ares.initialisee()) return false;
    if (!source_vol_compilee() || !resultat_vol.ok()) return false;
    const code::Certification c = code::run_test_bench(
        m.contract.id, /*compiled*/ true, domaine_vise(m), banc_heures);
    if (!jeu.ares.etat->finance.engage(c.budget_spent_me)) return false;
    cert_vol = c;
    source_certifiee = source_vol;
    jeu.avancer_temps(code::bench_delay_days(c));
    return true;
  }

  // ÉTAPE 3 — TÉLÉVERSER. « TOUT code de vol passe par un banc d'essai avant
  // téléversement » [GDD 15.5] : sans fiche valide pour CE texte, rien ne monte.
  bool televerser_vol() {
    if (!source_vol_certifiee()) return false;
    source_bord = source_vol;
    code_a_bord = true;
    return true;
  }

  // EXÉCUTER HORS DU DOMAINE DE VALIDITÉ [GDD 15.5] — la cause d'anomalie la
  // plus fréquente du logiciel de vol. On ne l'empêche pas : on la DIT.
  bool code_hors_domaine(const mission::Mission& m) {
    const mission::VueNavigation vn = vue_vol(m);
    if (!vn.ok || !cert_vol.certified) return true;
    return code::out_of_validity_domain(cert_vol, env_vol(m), 3.0 * vn.sigma_r,
                                        /*nominal*/ true);
  }

  // ÉTAPE 4 — LE CODE DÉCIDE. Il tourne sur la solution de navigation du moment,
  // dans son processus, avec son délai ; sa manœuvre — exprimée en inertiel —
  // est ramenée dans le repère RSW où le joueur commande, et déposée dans
  // `tcm_commande`. Le code PROPOSE ; c'est encore le joueur qui exécute.
  bool executer_code_vol(const mission::Mission& m) {
    if (!source_vol_a_bord()) return false;
    const mission::VueNavigation vn = vue_vol(m);
    if (!vn.ok) return false;
    resultat_vol = code::compiler_et_executer(source_bord, entrees_vol(m), toolchain);
    resultat_vol_lu = true;
    source_compilee = source_bord;
    if (!resultat_vol.ok()) return false;
    if (!resultat_vol.decisions.execute) {
      tcm_commande[0] = tcm_commande[1] = tcm_commande[2] = 0.0;
      return true;   // ne rien exécuter EST une décision, et elle se voit
    }
    const Basis3 b = rsw_basis(vn.r_estime, vn.v_estime);
    const Vec3 dv = resultat_vol.decisions.dv;
    tcm_commande[0] = dot(dv, b.R);
    tcm_commande[1] = dot(dv, b.S);
    tcm_commande[2] = dot(dv, b.W);
    return true;
  }

  // ═══════════════════════════════════════════════════════════════════════════
  // L'ASSISTANCE GRAVITATIONNELLE COMME DÉCISION D'ARCHITECTE [GDD 5.11, 3.1]
  // ═══════════════════════════════════════════════════════════════════════════
  // « Navigation et opérations interplanétaires : ... ASSISTANCES », colonne
  // Senior → Directeur. C'était une capacité de joueur que rien ne permettait de
  // prendre : `mission/Assistance.hpp` et les quatre modules d'astro_core qu'il
  // consomme n'avaient d'appelant QUE dans les oracles. Le manque n'était pas un
  // appelant — c'était une MISSION (voir CAT-13) et une PORTE. Voici la porte.

  mission::CapaciteAssistance capacites_assistance() const {
    mission::CapaciteAssistance c;
    c.gravity_assist = techno_operationnelle("gravity_assist");
    c.multi_survols  = techno_operationnelle("multi_survols");
    return c;
  }

  // Les tours OFFERTS à une mission : ceux dont le corps d'arrivée est celui que
  // le contrat vise. Un tour vers Jupiter n'a rien à proposer à un vol martien —
  // et une famille sans cible nommée n'a pas de tour du tout.
  std::vector<const mission::TourType*> tours_offerts(const mission::Mission& m) const {
    std::vector<const mission::TourType*> v;
    const mission::WindowTarget wt = mission::window_target_for_family(m.contract.family);
    if (!wt.impose) return v;
    for (const auto& t : mission::tour_catalog())
      if (!t.seq.empty() && t.seq.back() == wt.arr && t.seq.front() == wt.dep)
        v.push_back(&t);
    return v;
  }

  // LE Δv DU TRANSFERT DIRECT, pour que le troc soit CHIFFRÉ des deux côtés.
  double dv_direct_courant(const mission::Mission& m) const {
    return mission::trajectory_dv_for_mission(m, fen::Epoch{jeu.epoch_courant()}, jeu.eph);
  }

  // Le bilan vaut-il encore pour cette mission et ce tour ?
  bool tour_bilan_valide(const mission::Mission& m) const {
    return !m.tour_id.empty() && tour_bilan_id == m.tour_id
        && tour_bilan_mission == m.contract.id && tour_bilan.faisable;
  }

  // ═══ LA DURÉE DE TRANSIT RÉELLEMENT VISÉE ═══
  // Un tour ne change pas que le Δv : il change le TEMPS, et c'est l'autre
  // plateau de la balance [GDD 5.11]. Une seule fonction le dit, pour que
  // l'évaluation du plan et le feu vert ne puissent pas diverger.
  double duree_transit_jours(const mission::Mission& m) const {
    if (tour_bilan_valide(m)) return tour_bilan.tof_ans * 365.25;
    return mission::transfer_tof_days(m, fen::Epoch{jeu.epoch_courant()}, jeu.eph);
  }

  // CHOISIR — c'est ici que le calcul coûteux a lieu, et nulle part ailleurs.
  // "" = transfert direct (et le bilan est jeté). Rend faux si le tour est
  // refusé ; `tour_bilan.cause` porte alors le POURQUOI, affichable tel quel.
  bool choisir_tour(const std::string& id) {
    mission::Mission* m = mission_courante();
    if (!m) return false;
    if (id.empty()) {
      m->tour_id.clear();
      tour_bilan = mission::BilanTour{};
      tour_bilan_id.clear(); tour_bilan_mission.clear();
      evaluer_plan();
      return true;
    }
    const mission::TourType* t = nullptr;
    for (const auto* c : tours_offerts(*m)) if (id == c->id) t = c;
    if (!t) {
      tour_bilan = mission::BilanTour{};
      tour_bilan.evalue = true;
      tour_bilan.cause = "ce tour ne mene pas ou va cette mission";
      tour_bilan_id.clear(); tour_bilan_mission.clear();
      return false;
    }
    // LA GARDE CONTRE LE MENSONGE est celle du modèle : un tour qui ne bat pas
    // le transfert direct est refusé, pas vendu moins cher qu'il ne coûte.
    tour_bilan = mission::evaluer_tour_utile(
        *t, jeu.eph, fen::Epoch{jeu.epoch_courant()}, FENETRE_TOUR_JOURS,
        mission::parking_radius_m(), jeu.ares.etat->career.rank,
        dv_direct_courant(*m), C3_MAX_TOUR_M2S2, capacites_assistance(),
        alt_survol_min_km * 1000.0);
    tour_bilan_epoch = jeu.epoch_courant();
    if (!tour_bilan.faisable) {
      tour_bilan_id.clear(); tour_bilan_mission.clear();
      return false;
    }
    tour_bilan_id = t->id;
    tour_bilan_mission = m->contract.id;
    m->tour_id = t->id;
    evaluer_plan();
    return true;
  }

  // LARGEUR DE LA FENÊTRE DE DÉPART OFFERTE À L'OPTIMISEUR. Trois ans : la
  // synodique Terre-Jupiter fait 398,9 j, celle de Terre-Vénus 583,9 — il faut
  // couvrir plusieurs retours de géométrie pour qu'un tour à plusieurs survols
  // ait une chance de se refermer. DÉCLARÉ [GDD 6.8].
  static constexpr double FENETRE_TOUR_JOURS = 1095.0;
  // CE QUE LE LANCEUR VEND. Le plafond de C3 est une contrainte DURE ici, et il
  // encadre la gamme réelle : un Atlas V 551 vend ~31 km²/s² à faible masse.
  static constexpr double C3_MAX_TOUR_M2S2 = 30.0e6;
  // LARGEUR DU « MAINTENANT » POUR LE DÉPART D'UN TOUR. Cinq jours : c'est
  // l'ordre de grandeur d'une fenêtre de tir quotidienne réelle, et c'est
  // cohérent avec le pas de balayage de la carte porkchop (10 j) qui a produit la
  // date. Au-delà, on ne part pas « à peu près » sur une trajectoire calculée
  // pour une date précise (piège n°94).
  static constexpr double TOLERANCE_DEPART_TOUR_J = 5.0;

  void evaluer_plan() {
    if (mission::Mission* m = mission_courante()) {
      // Δv de trajectoire tiré de la géométrie RÉELLE de la fenêtre (Mars) ;
      // forfait par famille sinon. Rend le budget sensible à la fenêtre [7.3].
      mission_plan.dv_traj_override = mission::trajectory_dv_for_mission(
          *m, fen::Epoch{jeu.epoch_courant()}, jeu.eph);
      // ═══ ET SI L'ARCHITECTE A CHOISI UN TOUR, C'EST LUI QUI VOLE ═══
      // [GDD 5.11] Le tour remplace le Δv de trajectoire par le sien : départ
      // (C3 payé depuis le parking), manœuvres en espace profond, insertion. Le
      // bilan est LU, jamais recalculé ici — voir `choisir_tour`.
      if (tour_bilan_valide(*m)) mission_plan.dv_traj_override = tour_bilan.dv_total_ms;
      // ═══ P(NAVIGATION) N'EST PLUS 0,985 [GDD 7.5, 8.4] ═══
      // On évalue le vol QU'ON FERAIT EN PARTANT MAINTENANT : erreur d'injection
      // (Gates), amplifiée par Oberth, propagée par la matrice de transition de
      // l'arc jusqu'à l'arrivée, puis ramenée au Δv de correction que ce manque
      // au but exigerait. La probabilité de succès de navigation est celle que
      // la MARGE PROVISIONNÉE le couvre — donc une conséquence des choix de
      // conception, plus une constante.
      nav_disp = evaluer_navigation(*m);
      if (nav_disp.ok) mission_plan.p_physics = nav_disp.p_marge;
      // ═══ ET UN SURVOL EST UN RENDEZ-VOUS DE PRÉCISION ═══ [GDD 8.4, 5.11]
      // La navigation d'un tour ne se juge pas seulement à « la marge couvre-t-elle
      // la correction » : il faut ENCORE toucher le corridor du survol, sous peine
      // de rentrer dans l'atmosphère à 9 km/s ou de repartir sans la déviation
      // qu'on était venu chercher. Les deux conditions se multiplient parce
      // qu'elles sont indépendantes, et la seconde est NOMMÉE (un chiffre sans
      // cause n'est pas actionnable).
      nav_survol_ = mission::SurvolNav{};
      if (nav_disp.ok && tour_bilan_valide(*m) && !tour_bilan.rp_survol_m.empty()) {
        const mission::FlightTrace tr = trace_prospective(*m);
        const auto corps = tour_courant(*m) ? tour_courant(*m)->seq[1] : ephem::Body::EarthBary;
        // LE BORD DU CORRIDOR EST L'ATMOSPHÈRE, pas la borne de recherche : sous
        // l'interface, le véhicule ne survole plus, il rentre.
        const double rp_limite =
            ephem::body_radius(corps)
            + (corps == ephem::Body::Mars ? mission::ENTRY_INTERFACE_MARS_M
                                          : mission::ENTRY_INTERFACE_EARTH_M);
        nav_survol_ = mission::nav_survol(
            tr, nav_disp, /*arc du survol*/ 1, tour_bilan.rp_survol_m[0], rp_limite,
            tour_bilan.vinf_survol_ms.empty() ? 0.0 : tour_bilan.vinf_survol_ms[0],
            ephem::body_mu(corps));
        if (nav_survol_.ok) mission_plan.p_physics *= nav_survol_.p_survol;
      }
      // ═══ ET CE QUE PÈSE L'ÉQUIPAGE ═══ [GDD 9.4, 5.10]
      // Mêmes deux grandeurs que le Δv : elles viennent du MONDE (le ciel et
      // l'arbre), pas d'une saisie, et le plan pur ne peut pas les atteindre.
      mission_plan.crew_round_trip_days = mission::crew_round_trip_days(
          *m, fen::Epoch{jeu.epoch_courant()}, jeu.eph);
      mission_plan.crew_loops = mission::loops_from_tech(
          techno_operationnelle("recyclage_partiel"),
          techno_operationnelle("recyclage_ferme"));
      // ═══ ET LES LANCEURS QUE L'AGENCE SAIT FAIRE VOLER ═══ [GDD 5.4]
      // La branche 1 « Accès à l'orbite » ne gardait RIEN : quatre nœuds à
      // rechercher, et toute la gamme disponible dès la première mission. Même
      // prédicat que partout ailleurs (`techno_operationnelle`, TRL >= 7), pas
      // une seconde règle qui pourrait en diverger.
      mission_plan.lanceurs_qualifies = [this](const mission::Launcher& L) {
        return L.tech_id.empty() || techno_operationnelle(L.tech_id.c_str());
      };
      // ═══ ET LES MOTEURS ═══ [GDD 5.4] — même prédicat, même raison. Les
      // dix-huit pièces réelles sont commandables ; celles de la branche 6
      // (électrique, NTP, NEP, fusion) demandent leur nœud, sinon toute la
      // branche serait disponible dès la première mission.
      mission_plan.moteurs_qualifies = [this](const mission::EngineOption& E) {
        return E.tech_id.empty() || techno_operationnelle(E.tech_id.c_str());
      };
      // ═══ ET LE VÉHICULE QUE LE JOUEUR A CONÇU ═══ [GDD 4.1, 12.2]
      // Le poste CONCEPTION empilait des pièces réelles dans son coin et la
      // mission volait avec autre chose : le joueur choisissait un moteur DEUX
      // FOIS, dans deux postes, et seul l'autre comptait. C'est ici que la
      // boucle de [GDD 4.1] se referme — « contrat -> CONCEPTION -> ... ->
      // lancement ». La conception de départ reproduit exactement le véhicule
      // que la mission dimensionnait jusqu'ici, donc brancher ne déplace rien.
      mission_plan.pile = vehicule_design.stages;
      // ═══ ET CE QU'ELLE SAIT FAIRE EN ORBITE ═══ [GDD 5.2 branche 1]
      // Les trois nœuds que le GDD nomme (« transfert de propergol orbital,
      // rendez-vous automatisé robuste ») et que rien ne consommait.
      mission_plan.assemblage.rdv_automatise     = techno_operationnelle("rdv_automatise");
      mission_plan.assemblage.robotique_orbitale = techno_operationnelle("robotique_orbitale");
      mission_plan.assemblage.transfert_ergols   = techno_operationnelle("transfert_ergols");
      // ═══ ET CE QUE L'AGENCE A EN STOCK ═══ [GDD 5.12.12] — même raison que les
      // trois précédents : le plan pur ne connaît pas l'état de l'agence. Pour
      // une architecture relativiste habitée, c'est ce stock qui décide de la
      // vitesse, donc de la durée, donc des vivres — la boucle que
      // `bilan_relativiste` referme, et qui peut diverger [GDD 19.1].
      mission_plan.antimatiere_g = jeu.ares.etat->antimatiere.grams;
      // ═══ CE QUE LES SOUS-SYSTÈMES AVANCÉS SUBISSENT ═══ [GDD 12.4, 7.8]
      // Trois grandeurs que le plan pur ne peut pas connaître, toutes prises à
      // leur source vivante : la qualité de confinement DÉCLARÉE par le palier
      // d'antimatière, la durée pendant laquelle un cœur nucléaire tournera, et
      // la densité du couloir que l'agence a POLLUÉ elle-même [GDD 10.5].
      mission_plan.antimatiere_fuite_par_jour =
          jeu.ares.etat->antimatiere.prod.loss_rate_per_day;
      mission_plan.env_mission.duree_vol_jours =
          mission_plan.crew_round_trip_days > 0.0
              ? mission_plan.crew_round_trip_days
              : duree_transit_jours(*m);
      // Le couloir où une campagne d'assemblage attend : la LEO basse, celle-là
      // même que les ruptures des missions passées polluent. `Corridor{}` par
      // défaut a un volume NUL — donc une densité nulle et un mécanisme muet.
      mission_plan.env_mission.densite_debris_m3 =
          jeu.ares.etat->debris.spatial_density(env::standard_corridors()[0]);
      mission_plan.evaluate(*m);
    }
  }

  // Une techno est disponible quand LE MONDE sait la faire [GDD 5.3, 5.4] :
  // TRL >= 7. C'est le même prédicat que celui des prérequis de mission
  // (`tech/Unlock.hpp`), pas une seconde règle qui pourrait en diverger.
  bool techno_operationnelle(const char* id) const {
    if (!jeu.ares.initialisee()) return false;
    const tech::TechNode* n = jeu.ares.etat->tree.find(id);
    return n && n->operational();
  }

  // ═══ CE QUE L'ANTIMATIÈRE EMBARQUÉE ACHÈTE COMME VITESSE ═══
  // [GDD 6.7.2, 19.3, décision 10] Une seule expression, un seul endroit : le feu
  // vert la fige sur la mission, l'embarquement et l'évaluation la relisent. Elle
  // vivait auparavant en double — dans `embarquer` seulement, donc une sonde
  // robotique n'avait jamais de β. La masse sèche à pousser est celle du PLAN
  // (charge utile + vivres + blindage), pas la charge du contrat, et le nombre de
  // poussées est celui de l'architecture [GDD 6.7.4].
  double beta_croisiere_de(const mission::Mission& m) const {
    if (m.contract.family != "relativiste") return 0.0;
    const auto& G = *jeu.ares.etat;
    const double sec_kg = m.contract.terms.payload_kg
                        + mission_plan.vital.total_kg()
                        + mission_plan.masse_blindage_kg_;
    return rel::beta_from_antimatter(sec_kg, G.antimatiere.grams,
                                     rel::burns_for_architecture(m.contract.crewed));
  }

  // Le tour du catalogue que la mission a choisi (nul si transfert direct).
  const mission::TourType* tour_courant(const mission::Mission& m) const {
    if (m.tour_id.empty()) return nullptr;
    return mission::find_tour(m.tour_id);
  }

  // LA TRACE DU VOL QU'ON FERAIT EN PARTANT MAINTENANT — la même construction que
  // `evaluer_navigation`, sortie pour que le corridor de survol s'appuie sur elle
  // au lieu d'en refaire une seconde qui pourrait en diverger.
  mission::FlightTrace trace_prospective(const mission::Mission& m) {
    mission::Mission prospect = m;
    prospect.state = mission::MissionState::Launched;
    prospect.state_entered_days = jeu.ares.etat->clock.now_days();
    const double epoch = jeu.epoch_courant();
    prospect.beta_croisiere = beta_croisiere_de(prospect);
    prospect.tof_days = duree_transit_jours(prospect);
    if (tour_bilan_valide(m))
      for (const auto& a : tour_bilan.arcs) {
        mission::Mission::TourArc s;
        s.r0[0] = a.r0.x; s.r0[1] = a.r0.y; s.r0[2] = a.r0.z;
        s.v0[0] = a.v0.x; s.v0[1] = a.v0.y; s.v0[2] = a.v0.z;
        s.t0_tdb = a.t0; s.dt_s = a.dt;
        prospect.tour_arcs.push_back(s);
      }
    if (!mission::flight_has_arc(prospect)) return {};
    return mission::build_flight_trace(prospect, prospect.state_entered_days, epoch,
                                       jeu.eph);
  }

  // Le vol prospectif : « si on lançait maintenant ». C'est exactement ce qu'une
  // évaluation de conception doit juger — le plan n'est pas encore parti.
  mission::NavDispersion evaluer_navigation(const mission::Mission& m) {
    const double epoch = jeu.epoch_courant();
    // LA MÊME TRACE QUE CELLE QUI SE DESSINE, tour compris. Elle était construite
    // ici une seconde fois, sans les morceaux du tour : la dispersion d'un vol
    // avec assistance se jugeait donc sur l'arc DIRECT — approximation déclarée
    // le matin même, et supprimée l'après-midi.
    const mission::FlightTrace tr = trace_prospective(m);
    if (!tr.ok) return {};
    mission::Mission prospect = m;
    prospect.state_entered_days = jeu.ares.etat->clock.now_days();
    // Vitesse du corps quitté à la date de départ : c'est par rapport à ELLE que
    // se mesure le v∞ que l'injection doit fournir.
    const double t_dep_tdb =
        epoch + (tr.depart().t_days - prospect.state_entered_days) * cst::DAY;
    const Vec3 v_terre =
        jeu.eph.state(ephem::Body::EarthBary, ephem::Body::Sun, fen::Epoch{t_dep_tdb}).v;
    return mission::nav_dispersion(tr, v_terre, mission_plan.program.dv_margin);
  }

  // ═══ LE CONTEXTE DE NAVIGATION D'UN VOL ═══
  // Trace, dispersion, réalisation et solution de poursuite : les quatre pièces
  // dont dépend tout ce qui suit. Toutes DÉTERMINISTES à partir de la mission et
  // de la graine d'agence — l'arc est ancré sur `state_entered_days`, pas sur la
  // date du jour — donc reconstruire ce contexte au feu vert ou à l'arrivée donne
  // exactement les mêmes valeurs. C'est ce qui permet de ne rien sauvegarder de
  // plus que les faits du vol.
  struct ContexteVol {
    bool ok{false};
    mission::FlightTrace   tr;
    mission::NavDispersion d;
    mission::NavRealisation r;
    mission::NavSolution   sol;
    Vec3   cible{};            // point de visée NOMINAL à l'arrivée
    double t_arr_days{0.0};
    double arc_jours{0.0};     // arc de poursuite RÉELLEMENT exploité [GDD 8.6]
  };

  // ═══ L'ARC DE POURSUITE DISPONIBLE À UNE DATE ═══ [GDD 8.6]
  // Ce que le joueur a acheté (au programme + en vol), BORNÉ PAR LE TEMPS ÉCOULÉ
  // depuis l'injection. **On ne mesure pas le futur** : rien ne bornait cet arc,
  // si bien qu'un `tracking_days` généreux donnait au feu vert une solution que
  // seules deux semaines d'écoute peuvent produire. Acheter n'accélère donc rien
  // — ça autorise les antennes à continuer d'écouter, et le reste est du temps
  // qui passe.
  double arc_poursuite_disponible(const mission::Mission& m, double t_injection_days,
                                  double a_la_date_days) const {
    const double achete = mission_plan.program.tracking_days + m.poursuite_jours;
    const double ecoule = a_la_date_days - t_injection_days;
    if (ecoule <= 0.0) return 0.0;
    return achete < ecoule ? achete : ecoule;
  }

  // `a_la_date_days` : la date à laquelle on veut savoir ce que le joueur SAIT.
  // Par défaut « maintenant » ; `resoudre_vol` la fixe à la date de chaque
  // rendez-vous, parce qu'on ne corrige pas avec la connaissance qu'on aura plus
  // tard. Sentinelle < 0 = maintenant (une date de jeu peut être négative, cf.
  // piège n°61 — d'où la sentinelle explicite plutôt qu'un zéro).
  ContexteVol contexte_vol(const mission::Mission& m, double a_la_date_days = -1.0e18) {
    ContexteVol c;
    if (!mission::flight_has_arc(m)) return c;
    const double epoch = jeu.epoch_courant();
    const double now_days = jeu.ares.etat->clock.now_days();
    c.tr = mission::build_flight_trace(m, now_days, epoch, jeu.eph);
    if (!c.tr.ok || c.tr.n_nodes < 2) return c;
    const double t_dep_tdb = epoch + (c.tr.depart().t_days - now_days) * cst::DAY;
    const Vec3 v_terre =
        jeu.eph.state(ephem::Body::EarthBary, ephem::Body::Sun, fen::Epoch{t_dep_tdb}).v;
    c.d = mission::nav_dispersion(c.tr, v_terre, mission_plan.program.dv_margin);
    if (!c.d.ok) return c;
    const std::uint64_t graine =
        mission::mission_seed(jeu.agence.graine_agence, m.contract.id);
    c.r = mission::nav_realisation(c.tr, c.d, graine);
    if (!c.r.ok) return c;
    // ═══ CE QUE LE JOUEUR SAIT, ET CE QUE ÇA LUI COÛTE ═══ [GDD 8.4, 8.6]
    // La poursuite ACHETÉE (au programme et EN VOL) détermine la qualité de sa
    // solution ; toute correction se calcule sur elle — sur ce qu'il CROIT,
    // jamais sur ce qui EST. Sans poursuite il vise à l'aveugle : il dépense sa
    // marge et rate quand même. Et l'arc est borné par le temps écoulé : au feu
    // vert il ne sait RIEN, quelle que soit la somme engagée.
    const double date = (a_la_date_days <= -1.0e17) ? now_days : a_la_date_days;
    c.arc_jours = arc_poursuite_disponible(m, c.tr.depart().t_days, date);
    c.sol = mission::nav_solution(c.tr, c.d, c.r, c.arc_jours,
                                  t_dep_tdb, jeu.eph, graine);
    if (!c.sol.ok) return c;
    c.t_arr_days = c.tr.arrivee().t_days;
    const auto K = astro::kepler_propagate(
        c.tr.r_dep, c.tr.v_dep,
        (c.t_arr_days - c.tr.depart().t_days) * cst::DAY, cst::MU_SUN);
    if (!K.converged) return c;
    c.cible = K.r;
    c.ok = true;
    return c;
  }

  // ═══ LE VAISSEAU QUI DÉCOLLE DEVIENT UN FAIT ═══ [GDD 12.2, 17.2]
  // La pile conçue, sa capsule, sa charge utile et — surtout — les ergols que le
  // dimensionnement de CETTE mission a exigés, étage par étage. C'est tout ce
  // dont `vehicle::build_hull` a besoin pour redonner la coupe, au chargement
  // comme au rendu. Rien n'est recalculé plus tard : la conception continue de
  // vivre au poste CONCEPTION, et elle ne doit plus rien à ce vol.
  void geler_vaisseau(mission::Mission& m) {
    m.vaisseau_etages.clear();
    m.vaisseau_capsule = -1;
    m.vaisseau_payload_kg = 0.0;
    const auto& pile = mission_plan.pile;
    const auto& ergols = mission_plan.assessment.propellant_par_etage;
    if (pile.empty() || ergols.size() != pile.size()) return;
    for (std::size_t k = 0; k < pile.size(); ++k) {
      mission::Mission::EtageVol e;
      e.engine = pile[k].engine;
      e.tank = pile[k].tank;
      e.propellant_kg = ergols[k];
      m.vaisseau_etages.push_back(e);
    }
    m.vaisseau_capsule = vehicule_design.capsule;
    m.vaisseau_payload_kg = m.contract.terms.payload_kg;
  }

  // Tire l'erreur d'exécution de l'injection et ouvre l'ÉTAT VRAI du vol. Ce
  // qu'on consigne ici, ce n'est plus une issue : c'est un point de départ.
  //
  // ═══ IL N'Y A PLUS DE FILET ═══ [GDD 7.4]
  // Ces deux chiffres portaient jusqu'ici la campagne de correction conduite
  // AUTOMATIQUEMENT, dès le feu vert : le vol arrivait donc corrigé sans que
  // personne n'ait rien commandé, et « toutes les manœuvres sont calculées PAR LE
  // JOUEUR » restait un vœu. Ne rien embarquer, ne rien piloter, ne coûtait rien.
  // Ils portent maintenant ce que le vol vaut RÉELLEMENT à cet instant : rien
  // dépensé, et le manque au but de la trajectoire telle qu'elle a été injectée.
  // Qui tiendra les rendez-vous se décide en vol, et se solde dans `resoudre_vol`.
  void tirer_navigation(mission::Mission& m) {
    m.nav_evaluee = false;
    m.nav_dv_required = 0.0;
    m.nav_miss_km = 0.0;
    const ContexteVol c = contexte_vol(m);
    if (!c.ok) return;
    m.nav_evaluee = true;

    // L'ÉTAT VRAI DU VOL commence ici, à l'injection. Le joueur ne le verra
    // jamais [GDD 7.5] ; il n'en connaîtra que ce que sa poursuite lui rend.
    m.vol_vrai_valide = true;
    m.vol_vrai_t_days = c.tr.depart().t_days;
    for (int k = 0; k < 3; ++k) {
      m.vol_vrai_r[k] = c.tr.r_dep[k];
      m.vol_vrai_v[k] = c.r.v_dep_vraie[k];
      m.nav_connu_dv[k] = c.sol.dv_estime[k];
    }
    m.nav_sigma_r = c.sol.sigma_r;
    m.nav_sigma_v = c.sol.sigma_v;
    m.tcm_dv_depense = 0.0;
    m.tcm_faits = 0;
    // LE MANQUE D'UN VOL QUE PERSONNE NE CORRIGE — le point de départ, et le
    // prix par défaut de l'inaction. Il se compte en millions de km : l'erreur
    // d'injection non rattrapée ne pardonne pas.
    {
      mission::EtatVol e0;
      e0.valide = true;
      e0.t_days = m.vol_vrai_t_days;
      e0.r = c.tr.r_dep;
      e0.v = c.r.v_dep_vraie;
      m.nav_miss_km = mission::manque_reel_km(e0, c.cible, c.t_arr_days);
    }

    // ═══ CE QUI MONTE À BORD MONTE MAINTENANT [GDD 15.5] ═══
    // Le logiciel de vol téléversé part avec le véhicule, et sa fiche de
    // qualification est confrontée AU VOL QU'ON ENTAME : l'environnement vient
    // du profil de la mission, l'entrée est l'incertitude 3σ que la poursuite
    // achetée procure. Un code qualifié pour la croisière et embarqué sur une
    // rentrée, ou qualifié jusqu'à 12 km et embarqué sur une solution à 40, est
    // NON COUVERT — et c'est `fly_mission` qui en tire la conséquence.
    m.code_embarque = source_vol_a_bord() && cert_vol.certified;
    m.code_non_couvert =
        m.code_embarque && code::out_of_validity_domain(
                               cert_vol, env_vol(m), 3.0 * m.nav_sigma_r, /*nominal*/ true);
    // LA COUVERTURE PART AVEC LE CODE. Même raison que les deux drapeaux
    // ci-dessus : on ne relit pas la fiche à l'arrivée, parce que le joueur peut
    // avoir rouvert son éditeur entre-temps. C'est contre CE nombre que se tire
    // la tenue des rendez-vous par le logiciel de bord.
    m.code_couverture = code::code_success_prob(cert_vol, env_vol(m),
                                                3.0 * m.nav_sigma_r, /*nominal*/ true);
  }

  // ═══ QUI A TENU LES RENDEZ-VOUS ? ═══ [GDD 7.4, 8.4, 9.3, 15.3, 15.5]
  //
  // Appelé une fois, à l'arrivée. Le vol se solde depuis l'état VRAI où il en
  // est : les corrections que le joueur a commandées de sa main y sont déjà
  // (`executer_tcm` les a appliquées à leur date). Restent les rendez-vous qu'il
  // n'a pas tenus, et c'est là que se joue tout le prix du logiciel de vol :
  //
  //   . un LOGICIEL EMBARQUÉ, dans son domaine, les tient — à leur date, tout
  //     seul, sans le sol. C'est très exactement sa raison d'être [GDD 9.6] :
  //     « le logiciel de vol embarqué prépare l'autonomie quand le sol est hors
  //     de portée ». Mais il ne les tient QUE SI son banc l'a réellement exercé :
  //     on tire contre la couverture figée au feu vert, parce que « le banc
  //     rassure sans garantir » et qu'« un état non imaginé passe toujours »
  //     [GDD 15.5]. C'est le premier endroit où `code_success_prob` mord ;
  //   . sinon, si le joueur est ABSENT — mission longue vécue —, l'ADJOINT
  //     conduit la campagne [GDD 9.3] : « ARES fonctionne normalement sous un
  //     adjoint, ni pénalité ni dégradation punitive ». L'agence est compétente,
  //     elle ne laisse pas tomber un vol pendant que l'architecte est en route ;
  //   . sinon PERSONNE. Le vol garde l'écart qu'il traîne.
  //
  // AUCUN MALUS N'EST APPLIQUÉ NULLE PART, et c'est le point. Ce qui coûte, c'est
  // le bras de levier qu'on a laissé passer — Φ_rv devient quasi singulière près
  // du but — et il est calculé par la même matrice de transition que tout le
  // reste. Une correction n'est pas un traitement de fin de vol : c'est un
  // rendez-vous daté, et le plafond de cadence [GDD 14.3] ramène déjà le monde au
  // temps réel à cet instant précis pour que le joueur PUISSE y être.
  void resoudre_vol(mission::Mission& m) {
    if (!m.nav_evaluee || !m.vol_vrai_valide) return;
    const ContexteVol c = contexte_vol(m);
    if (!c.ok) return;

    Vec3 r{m.vol_vrai_r[0], m.vol_vrai_r[1], m.vol_vrai_r[2]};
    Vec3 v{m.vol_vrai_v[0], m.vol_vrai_v[1], m.vol_vrai_v[2]};
    double t = m.vol_vrai_t_days;

    const std::uint64_t graine =
        mission::mission_seed(jeu.agence.graine_agence, m.contract.id);

    // LE LOGICIEL DE BORD TIENT-IL LE VOL ? Embarqué, dans son domaine, et le
    // tirage contre sa couverture passe. Un sous-flux DÉDIÉ : la tenue du code
    // ne doit pas déplacer l'aléa des manœuvres elles-mêmes.
    bool tenu = false;
    if (m.code_embarque && !m.code_non_couvert) {
      Rng rng_code(graine ^ 0x434F444500000001ull);   // « CODE »
      tenu = rng_code.uniform01() <= m.code_couverture;
    }
    // L'ADJOINT, et seulement en l'absence du joueur [GDD 9.3]. La chaîne de fin
    // de partie financière est gelée par le même drapeau, et pour la même raison.
    const bool adjoint = !tenu && jeu.ares.etat->finance.suspended;

    // CE QUE LE JOUEUR A FAIT DE SA MAIN COMPTE D'ABORD : c'est le cas nominal
    // de [GDD 7.4], et l'agent ne fait que compléter ce qu'il n'a pas tenu.
    m.vol_conduit_par = (m.tcm_faits > 0) ? 1 : 0;

    if (tenu || adjoint) {
      // LA CONNAISSANCE DE CHAQUE RENDEZ-VOUS, pas celle de la fin du vol
      // [GDD 8.6] : l'arc de poursuite grandit entre TCM-1 et TCM-2, et corriger
      // la première avec les mesures de la seconde serait tricher avec le temps.
      const ContexteVol c1 = contexte_vol(m, c.tr.depart().t_days + c.d.t_tcm_days);
      const ContexteVol c2 =
          contexte_vol(m, c.t_arr_days - mission::TCM2_AVANT_ARRIVEE_J);
      if (!c1.ok || !c2.ok) return;
      const mission::NavCampagne camp =
          mission::nav_campagne_depuis(c.tr, c.d, c1.sol, c2.sol, r, v, t, graine);
      if (camp.ok) {
        // Ce que l'agent dépense grève la MÊME marge que ce que le joueur
        // dépense : il n'y a qu'un réservoir à bord.
        m.tcm_dv_depense += camp.dv_total;
        m.vol_vrai_t_days = t;
        for (int k = 0; k < 3; ++k) { m.vol_vrai_r[k] = r[k]; m.vol_vrai_v[k] = v[k]; }
        // On ne nomme l'agent que s'il a RÉELLEMENT agi : un joueur qui a tout
        // tenu lui-même ne laisse rien à prendre, et le débrief doit alors le
        // créditer, lui.
        if (camp.dv_total > 0.0) m.vol_conduit_par = tenu ? 2 : 3;
      }
    }

    // LE VERDICT EST UNE MESURE, PAS UN TIRAGE : le manque au but de la
    // trajectoire réellement volée, et ce qu'elle a réellement coûté.
    mission::EtatVol e;
    e.valide = true;
    e.t_days = m.vol_vrai_t_days;
    e.r = {m.vol_vrai_r[0], m.vol_vrai_r[1], m.vol_vrai_r[2]};
    e.v = {m.vol_vrai_v[0], m.vol_vrai_v[1], m.vol_vrai_v[2]};
    m.nav_miss_km = mission::manque_reel_km(e, c.cible, c.t_arr_days);
    m.nav_dv_required = m.tcm_dv_depense;
  }

  // ═══ CE QUE LE JOUEUR VOIT DE SON VOL, MAINTENANT ═══ [GDD 8.3]
  // État estimé, manque au but projeté, incertitude. Rien de plus : la vérité
  // reste hors de portée. Rend une vue invalide si la mission n'est pas en vol
  // ou si le point de visée est déjà passé.
  mission::VueNavigation vue_vol(const mission::Mission& m) {
    if (!m.vol_vrai_valide || !trace_vol.ok || trace_vol.n_nodes < 2) return {};
    const auto Kc = astro::kepler_propagate(
        trace_vol.r_dep, trace_vol.v_dep,
        (trace_vol.arrivee().t_days - trace_vol.depart().t_days) * cst::DAY, cst::MU_SUN);
    if (!Kc.converged) return {};
    mission::VueNavigation vn = mission::vue_navigation(
        trace_vol.r_dep, trace_vol.v_dep,
        Vec3{m.nav_connu_dv[0], m.nav_connu_dv[1], m.nav_connu_dv[2]}, Kc.r,
        trace_vol.depart().t_days, jeu.ares.etat->clock.now_days(),
        trace_vol.arrivee().t_days, m.nav_sigma_r, m.nav_sigma_v);
    if (vn.ok) vn.delai_com_s = delai_com_s(vn);
    return vn;
  }

  // ═══ LE DÉLAI DE COMMUNICATION [GDD 8.3, 9.6] ═══
  // `mission::comms_delay_s` existait depuis le premier jour et **personne ne
  // l'appelait** — un modèle que rien ne consomme, même famille que
  // `Mission::phase` (piège n°20b) ou `ModeAide` avant le mode d'aide. Il est
  // maintenant la source unique du délai, ici comme à l'écran.
  //
  // CALCULÉ SUR L'ESTIMÉ, PAS SUR LA VÉRITÉ, et c'est doctrinal : le joueur ne
  // voit jamais sa position vraie [GDD 7.5], donc pas davantage un délai qui en
  // découlerait. APPROXIMATION DÉCLARÉE [GDD 6.8] : l'écart entre les deux vaut
  // le manque au but rapporté à la distance Terre-vaisseau — de l'ordre de 1e-5,
  // soit quelques millisecondes sur un délai qui se compte en minutes.
  //
  // Le poste du joueur est à bord de Novellus, en orbite basse : la distance
  // Terre-station (418 km, soit 1,4 ms) est six ordres de grandeur sous la
  // distance interplanétaire. On prend donc la Terre, et on le déclare.
  double delai_com_s(const mission::VueNavigation& vn) {
    if (!vn.ok) return 0.0;
    const Vec3 r_terre =
        jeu.eph.state(ephem::Body::EarthBary, ephem::Body::Sun,
                      fen::Epoch{jeu.epoch_courant()}).r;
    return mission::comms_delay_s(norm(vn.r_estime - r_terre));
  }

  // ═══ QUI PEUT CONDUIRE LA PHASE EN COURS ═══ [GDD 9.6, 15.3]
  // La question que le délai lumière rend enfin décidable, et la raison d'être
  // du logiciel embarqué. On compare deux grandeurs physiques : l'aller-retour
  // de la lumière et la durée propre de la manœuvre. Rien à régler.
  struct BoucleSol {
    bool   valide{false};
    bool   fermee{false};          // le sol peut voir, décider et commander
    double aller_retour_s{0.0};
    double duree_phase_s{0.0};
    mission::FlightPhase phase{mission::FlightPhase::Ground};
  };

  BoucleSol boucle_sol(const mission::Mission& m) {
    BoucleSol b;
    if (!m.vol_vrai_valide) return b;
    const mission::VueNavigation vn = vue_vol(m);
    if (!vn.ok) return b;
    b.phase = mission::flight_phase_of(m, jeu.ares.etat->clock.now_days());
    b.duree_phase_s = mission::phase_duration_s(b.phase);
    b.aller_retour_s = 2.0 * vn.delai_com_s;
    // `ground_loop_closes` est LA source du prédicat : on ne le récrit pas ici.
    const Vec3 r_terre =
        jeu.eph.state(ephem::Body::EarthBary, ephem::Body::Sun,
                      fen::Epoch{jeu.epoch_courant()}).r;
    b.fermee = mission::ground_loop_closes(norm(vn.r_estime - r_terre), b.duree_phase_s);
    b.valide = true;
    return b;
  }

  // ═══ EXÉCUTER UNE CORRECTION [GDD 7.4, 9.6] ═══
  // Le joueur commande trois composantes en repère RSW ; le modèle les applique
  // LITTÉRALEMENT à l'état vrai, erreur de Gates comprise. Aucun rattrapage.
  // Rend ce qui a été réellement dépensé (0 si l'exécution n'a pas eu lieu).
  //
  // ═══ CE QU'IL COMMANDE PART MAINTENANT ET ARRIVE PLUS TARD ═══
  // « Communication au délai lumière selon la distance ; le logiciel de vol
  // embarqué prépare l'autonomie quand le sol est hors de portée » [GDD 9.6].
  // La manœuvre s'exécute donc à `now + d/c`, sur l'état que le vaisseau aura
  // ALORS — pas sur celui que le joueur regardait en la calculant. Il vise avec
  // la base RSW de son estimé au moment de la commande : il ne peut pas faire
  // mieux, et c'est exactement le problème que le vol autonome supprime.
  double executer_tcm(mission::Mission& m, const Vec3& dv_rsw) {
    const mission::VueNavigation vn = vue_vol(m);
    if (!vn.ok) return 0.0;
    mission::EtatVol e;
    e.valide = m.vol_vrai_valide;
    e.t_days = m.vol_vrai_t_days;
    e.r = {m.vol_vrai_r[0], m.vol_vrai_r[1], m.vol_vrai_r[2]};
    e.v = {m.vol_vrai_v[0], m.vol_vrai_v[1], m.vol_vrai_v[2]};
    const double now = jeu.ares.etat->clock.now_days();
    // L'ARRIVÉE DE LA COMMANDE, pas son émission. Le logiciel de bord, lui, est
    // déjà sur place : `nav_campagne_depuis` agit à la date du rendez-vous.
    const double arrivee = now + vn.delai_com_s / cst::DAY;
    if (!mission::avancer_etat_vol(e, arrivee)) return 0.0;
    // Graine dédiée à CETTE manœuvre : deux corrections d'un même vol ne
    // partagent pas leur aléa d'exécution.
    const std::uint64_t g = mission::mission_seed(jeu.agence.graine_agence, m.contract.id) ^
                            (0x54434D31ull + static_cast<std::uint64_t>(m.tcm_faits));
    const mission::ResultatManoeuvre r = mission::executer_correction(e, vn, dv_rsw, g);
    if (!r.ok) return 0.0;
    m.vol_vrai_t_days = e.t_days;
    for (int k = 0; k < 3; ++k) { m.vol_vrai_r[k] = e.r[k]; m.vol_vrai_v[k] = e.v[k]; }
    m.tcm_dv_depense += r.dv_depense;
    m.tcm_faits += 1;
    // La correction change le vol : le manque au but réel n'est plus celui de
    // la campagne automatique, c'est celui que SA manœuvre a produit.
    const auto Kc = astro::kepler_propagate(
        trace_vol.r_dep, trace_vol.v_dep,
        (trace_vol.arrivee().t_days - trace_vol.depart().t_days) * cst::DAY, cst::MU_SUN);
    if (Kc.converged) {
      mission::EtatVol e2 = e;
      m.nav_miss_km = mission::manque_reel_km(e2, Kc.r, trace_vol.arrivee().t_days);
      m.nav_dv_required = m.tcm_dv_depense;
    }
    return r.dv_depense;
  }

  // MOTIF DU DERNIER REFUS D'AVANCE, pour l'écran. Un bouton qui refuse sans
  // dire pourquoi est une panne aux yeux du joueur (piège n°42) — et depuis que
  // le vol dure, le refus le plus fréquent est « pas encore arrivé », qui n'a
  // de sens qu'avec son nombre de jours.
  std::string dernier_refus_mission;

  // Avance la mission d'UNE phase (le chemin nominal). Renvoie le gate : si le
  // refus est motivé, `reason` le dit. Gère les effets de bord de chaque
  // transition (commit financier, exécution du vol, conséquences du débrief).
  mission::GateResult avancer_mission() {
    const mission::GateResult r = avancer_mission_impl();
    dernier_refus_mission = r.allowed ? std::string() : r.reason;
    return r;
  }

  mission::GateResult avancer_mission_impl() {
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
      case St::Debrief:       target = m->flight_success ? St::Completed : St::Failed; break;
      default:                return {false, "mission terminee"};
    }

    const mission::GateResult g = mission::mission_gate(*m, mission_plan, target);
    if (!g.allowed) return g;

    // GATE GÉOMÉTRIQUE : on ne signe le passage en qualification (= viser une
    // fenêtre) que si le ciel est là. Positions réelles des corps [GDD 7.3].
    if (target == St::Qualification) {
      // ═══ UN TOUR A SA PROPRE FENÊTRE, ET C'EST LA SIENNE QUI FAIT FOI ═══
      // [GDD 5.11, 7.3] La fenêtre du transfert DIRECT ne dit rien d'un tour :
      // l'optimiseur a trouvé une date de départ précise, souvent des mois plus
      // loin, et c'est elle l'opportunité. Partir un autre jour, c'est voler une
      // trajectoire que personne n'a calculée — exactement le défaut du 2026-08-01,
      // et on ne le repaiera pas.
      if (tour_bilan_valide(*m)) {
        const double attente_j =
            (tour_bilan.epoque_depart_tdb - jeu.epoch_courant()) / cst::DAY;
        if (attente_j > TOLERANCE_DEPART_TOUR_J) {
          char buf[128];
          std::snprintf(buf, sizeof buf,
                        "depart du tour %s dans %.0f jours (son opportunite)",
                        m->tour_id.c_str(), attente_j);
          return {false, buf};
        }
        if (attente_j < -TOLERANCE_DEPART_TOUR_J)
          return {false, "l opportunite du tour est passee : recalculer la trajectoire"};
      } else {
        const mission::GateResult wg = mission::launch_window_gate(
            *m, fen::Epoch{jeu.epoch_courant()}, jeu.eph);
        if (!wg.allowed) return wg;
      }
    }

    // GATE D'ARRIVÉE [GDD 9] : le vol DURE. On ne débriefe pas une sonde qui est
    // encore en croisière — le refus chiffre les jours restants, et c'est le
    // temps de jeu [GDD 14.2] qui les consomme.
    if (target == St::Debrief) {
      const mission::GateResult ag = mission::arrival_gate(*m, G.clock.now_days());
      if (!ag.allowed) return ag;
    }

    // COMMIT : le programme se paie à l'engagement du feu vert. Irréversible.
    // Prélèvement sur les FINANCES v1.2 : trésorerie puis réserve [GDD 13.4].
    if (target == St::Launched) {
      if (!G.finance.engage(mission_plan.assessment.cost_total))
        return {false, "fonds insuffisants pour engager le programme"};
      // ═══ CE QUE CE PROGRAMME A COÛTÉ EST UN FAIT DU VOL ═══ [GDD 3.3]
      // Le critère « respect budgétaire » se juge à l'arrivée, contre l'enveloppe
      // du contrat. Le coût doit donc être celui RÉELLEMENT engagé ici, et non
      // celui d'une conception que le joueur aura retouchée entre-temps — même
      // doctrine que la durée de transit et le vaisseau.
      m->cout_engage_musd = mission_plan.assessment.cost_total;
      // ═══ LE β SE FIGE AVANT LA DURÉE, PARCE QU'IL LA DÉCIDE ═══
      // [GDD 6.7, décision 10] Pour une architecture relativiste, la durée de
      // transit N'EST PAS une géométrie de ciel : c'est la distance de la cible
      // divisée par la vitesse que l'antimatière embarquée achète. L'ordre compte
      // donc — `transfer_tof_days` lit `beta_croisiere`.
      m->beta_croisiere = beta_croisiere_de(*m);
      // LA DURÉE DE TRANSIT SE FIGE ICI, et nulle part ailleurs : c'est la
      // géométrie du ciel AU DÉCOLLAGE qui date l'arrivée. Recalculée en route,
      // elle ferait glisser l'arrivée d'un vol déjà parti [GDD 7.3].
      // Un TOUR impose sa propre durée — c'est même tout son prix [GDD 5.11] :
      // il achète du Δv avec des années. `duree_transit_jours` est le seul
      // endroit où l'un ou l'autre est choisi, pour que le plan évalué et le vol
      // réellement daté ne puissent pas diverger (piège n°94).
      m->tof_days = duree_transit_jours(*m);
      // ═══ ET SA TRAJECTOIRE SE FIGE AVEC LUI ═══ [GDD 8.3]
      // Les morceaux que l'optimiseur a parcourus deviennent un FAIT du vol,
      // sauvegardé : un tour se recalculerait différemment à chaque date, donc
      // recalculer au chargement ferait voler un vol déjà parti sur une autre
      // trajectoire que la sienne.
      m->tour_arcs.clear();
      if (tour_bilan_valide(*m))
        for (const auto& a : tour_bilan.arcs) {
          mission::Mission::TourArc s;
          s.r0[0] = a.r0.x; s.r0[1] = a.r0.y; s.r0[2] = a.r0.z;
          s.v0[0] = a.v0.x; s.v0[1] = a.v0.y; s.v0[2] = a.v0.z;
          s.t0_tdb = a.t0; s.dt_s = a.dt;
          m->tour_arcs.push_back(s);
        }
      // ═══ ET LE VAISSEAU QUI PART EST FIGÉ AVEC EUX ═══ [GDD 12.2, 17.2]
      // Ce qui décolle est la pile conçue au poste CONCEPTION, avec les ergols que
      // Tsiolkovsky a exigés pour CETTE mission. Le joueur continue de retoucher
      // sa conception ensuite : sans ce gel, un vaisseau déjà parti changerait de
      // forme à l'écran. Même doctrine que la durée de transit et le tour.
      geler_vaisseau(*m);
      // ═══ L'INJECTION EST EXÉCUTÉE POUR DE BON ═══ [GDD 8.2]
      // L'écart d'exécution se TIRE ici, une fois, sur un sous-flux de la graine
      // de mission — rejouable, et figé comme l'arc. Le joueur ne le verra pas
      // [GDD 7.5] ; il en subira les conséquences si sa marge est trop courte.
      tirer_navigation(*m);
    }

    m->advance(target, G.clock.now_days());

    // ═══ LE FEU VERT RAMÈNE LE TEMPS À UN RYTHME LENT [GDD 14.3] ═══
    // Le vol commence par une ascension de ~9 minutes : à la cadence « mois/s »
    // elle serait franchie en deux centièmes de seconde réelle, sans que rien
    // n'ait pu être observé ni corrigé. Ce n'est donc pas au joueur de ralentir
    // avant de lancer — c'est la manœuvre qui freine le monde, ici, dans la
    // frame même du feu vert.
    jeu.appliquer_plafond();

    // LE VOL S'EXÉCUTE : issue déterministe (graine agence + mission). Le
    // résultat est consigné SUR LA MISSION — c'est un fait du modèle, pas de
    // l'écran qui l'a déclenché : il survit ainsi à une sauvegarde comme au
    // changement de mission pilotée.
    if (target == St::Debrief) {
      // ═══ LE VOL SE SOLDE AVANT D'ÊTRE JUGÉ ═══
      // Les rendez-vous de correction que le joueur n'a pas tenus reviennent au
      // logiciel de bord ou à l'adjoint, ou à personne. C'est ici, et pas au feu
      // vert, que ça se sait — un vol dure des mois, et ce qu'on en fait pendant
      // ces mois est précisément ce qu'on juge ensuite.
      resoudre_vol(*m);
      mission_outcome = mission::fly_mission(
          *m, mission_plan,
          mission::mission_seed(jeu.agence.graine_agence, m->contract.id));
      m->flight_flown = true;
      m->flight_success = mission_outcome.success;
      m->flight_has_anomaly = mission_outcome.has_anomaly;
      m->flight_anomaly = mission_outcome.anomaly;
      // ═══ LE DEMI-PALIER DE [GDD 10.3] SE MÉRITE ═══
      // « Rétrogradation possible d'un demi-palier selon les circonstances
      // atténuantes. » `brilliant_recovery` existait depuis toujours dans
      // `SeverityModifiers`, était sauvegardé, et n'était POSÉ PAR PERSONNE : le
      // seul modificateur adoucissant du barème ne pouvait jamais s'appliquer.
      // Il se pose ici, sur un FAIT : toutes les pannes survenues en vol ont été
      // menées à réparation. Diagnostiquer et réparer sous contrainte de temps,
      // avec les capacités qu'on a fait qualifier, c'est exactement ce que [3.3]
      // appelle « sauvegarde d'objectifs ou d'équipage ».
      if (m->flight_has_anomaly && m->crise_avaries > 0 &&
          m->crise_reparees >= m->crise_avaries) {
        m->flight_anomaly.modifiers.brilliant_recovery = true;
        m->flight_anomaly.severity = mission::apply_modifiers(
            m->flight_anomaly.severity, m->flight_anomaly.modifiers);
      }
      mission_outcome_pret = true;
      // ═══ LE CARNET RETIENT LE VOL ═══ [GDD 15.4]
      // `NotebookEntry::mission_ref` attendait ça depuis le premier jour. Le
      // carnet était sérialisé et transmis au successeur — VIDE. C'est ici qu'il
      // se remplit, et il ne contient que des faits que le débrief possède déjà.
      G.notebook.write(career::debrief_mission(
          *m, mission_plan.program.dv_margin, G.clock.now_days()));
    }

    // LE VOL VÉCU SE TERMINE AVEC SA MISSION [GDD 9.2] : l'Architecte rentre,
    // l'agence se dégèle et le carnet reçoit sa reconstitution d'absence. Avant
    // la triple lecture ci-dessous, et c'est voulu — le retour a lieu au débrief,
    // donc les conséquences du vol tombent sur un joueur redevenu présent.
    if (target == St::Debrief && jeu.ares.etat->lived.active &&
        jeu.ares.etat->lived.mission_id == m->contract.id)
      debarquer();

    // DÉBRIEF : triple lecture appliquée à TOUS les systèmes [GDD 10.4].
    if (target == St::Completed || target == St::Failed) {
      // ═══ LE SCORE DE PROMOTION SE JUGE ICI, SUR TROIS CRITÈRES ═══ [GDD 3.3]
      // C'est le seul endroit où les trois faits coexistent : l'issue, le coût
      // engagé au feu vert contre l'enveloppe du contrat, et ce que le vol a
      // traversé. Le barème vit dans `career::score_mission` — le HUD et les
      // oracles lisent la même fonction, et le détail par critère reste LISIBLE
      // (le joueur doit pouvoir savoir lequel des trois l'a fait progresser).
      {
        career::MissionBilan b;
        b.succes = m->flight_success;
        b.budget_contrat_musd = m->contract.terms.budget_musd;
        b.cout_engage_musd = m->cout_engage_musd;
        b.gravite = m->any_anomaly ? static_cast<int>(m->worst_severity) : 0;
        b.avaries_subies = m->crise_avaries;
        b.avaries_reparees = m->crise_reparees;
        const career::MissionScore sc = career::score_mission(b);
        G.career.add_score(sc.total());
        if (G.career.score < 0.0) G.career.score = 0.0;
        dernier_score_mission = sc;
        char buf[192];
        std::snprintf(buf, sizeof(buf),
                      "[GDD 3.3] SCORE %+.0f — reussite %+.2f, budget %+.2f, crise %+.2f",
                      sc.total(), sc.reussite, sc.budget, sc.crise);
        jeu.agence.log(buf);
      }
      if (m->flight_success) {
        jeu.agence.reussites += 1;
        // GAIN DE CONFIANCE [GDD 13.4] : +2..+5 nominal, davantage pour un
        // objectif difficile. Barème simple : +3, +6 si mission phare.
        const std::string& fam = m->contract.family;
        const double gain = (fam == "mars_habite" || fam == "relativiste" || m->contract.crewed) ? 6.0 : 3.0;
        G.career.confidence_ares = std::min(100.0, G.career.confidence_ares + gain);
      } else {
        jeu.agence.echecs += 1;
        if (m->flight_has_anomaly)
          G.apply_anomaly(*m, m->flight_anomaly);   // dont perte de confiance
      }
      mission_outcome_pret = false;
    }
    return {true, ""};
  }

  // ═══════════════════════════════════════════════════════════════════════════
  // EMBARQUER — LA MISSION VÉCUE [GDD 9, décision 18]
  // ═══════════════════════════════════════════════════════════════════════════
  // « Vol habité vécu INCLUS » (décision 18) : le joueur peut monter à bord et
  // devenir responsable scientifique de la mission [GDD 9.1]. Tout le modèle
  // existait — `Crew.hpp` calculait les consommables, `AgencyFinance::suspended`
  // gardait déjà l'adjoint, `career::journal_absence` attendait d'être écrite —
  // mais AUCUN chemin ne menait à cet état : `CrewMissionSlot::try_embark`
  // n'était appelé nulle part, et `suspended` n'était posé que par les tests. Un
  // chapitre entier du GDD sans porte d'entrée.
  //
  // LES CONDITIONS SONT CELLES DU GDD, ET AUCUNE N'EST UN RÉGLAGE :
  //   . la mission est HABITÉE — sinon il n'y a pas d'équipage à rejoindre ;
  //   . elle est PARTIE — on n'embarque pas sur un vol qui n'a pas décollé ;
  //   . une seule à la fois [GDD 9.2] ;
  //   . la confiance autorise l'habité [GDD 13.4] — le même seuil que celui qui
  //     filtre déjà l'ACCEPTATION d'un contrat habité, pas un second barème ;
  //   . et si la mission est LONGUE, il faut le RANG TERMINAL et la MATURITÉ :
  //     « rang + maturité correspondants », dit 9.2. La maturité, c'est le nœud
  //     `sejour_long` de la branche 4 — le support-vie long séjour est
  //     littéralement ce qui rend la chose possible [GDD 5.10, 19.1].
  struct VerdictEmbarquement { bool possible{false}; std::string raison; };
  std::string dernier_refus_embarquement;

  VerdictEmbarquement peut_embarquer() {
    if (!jeu.ares.initialisee())    return {false, "agence non initialisee"};
    mission::Mission* m = mission_courante();
    if (!m)                         return {false, "aucune mission pilotee"};
    auto& G = *jeu.ares.etat;
    if (G.lived.active)
      return {false, "une seule mission vecue a la fois [GDD 9.2]"};
    if (!m->contract.crewed)
      return {false, "mission robotique : aucun equipage a rejoindre"};
    // ═══ ON MONTE À BORD AVANT LE FEU VERT, JAMAIS APRÈS ═══
    // Trouvé EN CAPTURE : la première version acceptait `state == Launched`, si
    // bien que l'Architecte pouvait rejoindre un véhicule déjà en route vers
    // Mars. Aucun vaisseau ne se rattrape en vol : embarquer est une décision de
    // PLANIFICATION [GDD 9.2, 4.1] — « le joueur choisit d'embarquer OU de
    // conduire depuis le sol », et ce choix se prend avant le décollage. Il
    // engage ensuite pour toute la mission.
    if (m->state >= mission::MissionState::Launched)
      return {false, "le vol est deja parti : on n embarque pas en route"};
    if (m->state < mission::MissionState::Design)
      return {false, "mission trop tot : concevoir d abord [GDD 4.1]"};
    if (!economy::crewed_allowed(G.career.confidence_ares))
      return {false, "confiance insuffisante : missions habitees suspendues [GDD 13.4]"};
    const double rt = mission::crew_round_trip_days(
        *m, fen::Epoch{jeu.epoch_courant()}, jeu.eph);
    const bool longue = mission::mission_longue(m->contract.family, rt);

    // ═══ UN PERSONNAGE CONSOMMÉ NE REVOLE PAS — SAUF POUR LE DERNIER VOL ═══
    // [GDD 6.6, 9.2] La limite de dose de carrière est l'instrument qui protège
    // un astronaute RÉUTILISABLE : elle interdit de rembarquer quelqu'un qu'on
    // veut faire revoler. Or [GDD 9.2] dit que la mission longue est prise
    // « lorsqu'il n'a plus de carrière à construire » — c'est le vol TERMINAL, et
    // sur celui-là une limite de carrière protège une carrière qui n'existe plus.
    // Elle reste donc opposable à TOUTE mission ordinaire, et cesse de l'être sur
    // le vol terminal, qui est déjà gardé par le rang et la maturité ci-dessous.
    // CE N'EST PAS UNE PORTE QU'ON OUVRE : c'est un risque qu'on ACCEPTE, et le
    // verdict le chiffre pour que l'acceptation soit informée [GDD 12.5].
    if (G.dose_architecte.career_exceeded() && !longue)
      return {false, "limite de dose de carriere atteinte : inapte au vol [GDD 6.6]"};

    if (longue) {
      if (!career::terminal_rank(G.career.rank))
        return {false, "mission longue : reservee a la fin de carriere [GDD 9.2]"};
      if (!techno_operationnelle("sejour_long"))
        return {false, "maturite requise : support-vie long sejour [GDD 5.10]"};
    }
    return {true, ""};
  }

  bool embarquer() {
    const VerdictEmbarquement v = peut_embarquer();
    dernier_refus_embarquement = v.possible ? std::string() : v.raison;
    if (!v.possible) return false;

    auto& G = *jeu.ares.etat;
    mission::Mission* m = mission_courante();

    // LES SOUTES SE REMPLISSENT DU BUDGET QU'ON A PROVISIONNÉ, recalculé depuis
    // les MÊMES entrées que la conception (famille, géométrie, arbre) plutôt que
    // recopié du plan : un plan peut avoir été réévalué entre-temps, la
    // géométrie et l'arbre, eux, sont des faits du monde à cet instant.
    const double rt = mission::crew_round_trip_days(
        *m, fen::Epoch{jeu.epoch_courant()}, jeu.eph);
    const mission::RecyclingLoops loops = mission::loops_from_tech(
        techno_operationnelle("recyclage_partiel"),
        techno_operationnelle("recyclage_ferme"));
    const double jours = mission::crew_occupation_days(m->contract.family, rt);
    // L'EFFECTIF EMBARQUÉ EST CELUI QUE L'OBJECTIF DEMANDE [GDD 3.1] : ARES dit
    // combien de personnes doivent être là-bas, l'architecte dit comment les y
    // mettre. Lu sur le contrat, plus sur une table indexée par famille.
    const int n = m->contract.terms.crew_required;

    G.lived.try_embark(m->contract.id);
    G.lived.depart_days = G.clock.now_days();
    G.lived.n_crew = n;
    G.lived.loops  = loops;
    G.lived.vitals = mission::arm_vitals(
        mission::crew_consumables(n, m->contract.family, rt, loops), n, jours);
    G.lived.confidence_frozen     = G.career.confidence_ares;
    G.lived.missions_at_departure = static_cast<int>(G.missions.size());
    // LE BLINDAGE EMBARQUÉ EST CELUI QU'ON A PAYÉ [GDD 6.6] : celui du plan, dont
    // la masse a réellement pesé au décollage. Pas un réglage de dernière minute.
    // LA COQUE COMPTE : ce que l'équipage voit, c'est la structure PLUS ce qui a
    // été payé. Partir de zéro décrirait un astronaute sans véhicule.
    G.lived.blindage = mission::blindage_effectif(mission_plan.blindage);
    // ET LA FIABILITÉ DU VÉHICULE, elle aussi figée ici [GDD 12.3] : elle module
    // les taux de panne interne. Ce que le joueur a acheté en essais à feu se
    // paie — ou se récolte — en vol.
    G.lived.fiabilite_systeme = mission_plan.assessment.p_success > 0.0
                                    ? mission_plan.assessment.p_success
                                    : 0.98;
    // ET LA PRÉPARATION MÉDICALE REÇUE [GDD 11.6] : le module médical de Novellus
    // « réduit les urgences médicales en vol », `station::effects` le chiffrait
    // déjà — le tirage, lui, lisait 1,0 en dur. Figé ici avec le reste de ce qui
    // a été acquis AVANT le décollage.
    G.lived.facteur_risque_medical = station::effects(G.station).medical_risk_factor;
    // ET LE RYTHME DES DEUX HORLOGES [GDD 6.7, 14.4] : les demi-grands axes réels
    // du moment, lus sur les MÊMES éphémérides que la fenêtre et que le Δv.
    G.lived.horloge = mission::geometrie_horloge(
        *m, fen::Epoch{jeu.epoch_courant()}, jeu.eph);
    // ═══ ET LE β QUE L'ANTIMATIÈRE EMBARQUÉE ACHÈTE VRAIMENT ═══ [GDD 19.3]
    // Le seul régime qui puisse franchir le seuil relativiste [GDD 6.7.2] — et il
    // n'y a pas d'autre porte : « seule l'antimatière ouvre le régime
    // relativiste ». On embarque ce que le CONFINEMENT permet de transporter, pas
    // le stock rêvé, et la masse sèche à pousser est celle du plan (charge utile +
    // vivres + blindage), pas la charge du contrat.
    // ═══ ET LE NOMBRE DE POUSSÉES EST CELUI DE L'ARCHITECTURE ═══ [GDD 6.7.4]
    // Ce β se lisait en ALLER SIMPLE — une seule poussée — pour une mission qui
    // embarque un équipage, donc qui freine à l'arrivée ET revient. Le GDD appelle
    // ça « le verrou de l'aller-retour » et le chiffre au ratio à la PUISSANCE
    // QUATRE ; le lire à la puissance un surestimait la vitesse de bord, et
    // d'autant plus que l'architecture était contraignante. Mesure : sur une sonde
    // de 5 t et le stock d'équilibre, 0,301 en aller simple contre 0,131 en
    // aller-retour. Le verrou que le modèle savait calculer ne s'appliquait pas
    // là où il compte — sur l'horloge de l'équipage.
    // ON EMBARQUE AVANT LE FEU VERT (piège n°71), donc `m->beta_croisiere` n'est
    // pas encore figé : on prend la même expression, par le même appel. Le feu
    // vert la refigera, à l'identique tant que le plan n'a pas bougé.
    G.lived.horloge.beta_croisiere =
        m->beta_croisiere > 0.0 ? m->beta_croisiere : beta_croisiere_de(*m);
    // Nouvelle mission : ni avarie héritée, ni fenêtre d'événements déjà tirée.
    G.avaries.clear();
    G.lived.jour_evenements_tire = -1.0;
    // Nouvelle mission : le compteur de MISSION repart, celui de CARRIÈRE non.
    G.dose_architecte.mission_sv = 0.0;
    G.dose_architecte.mission_acute_gy = 0.0;

    // ═══ ARES CONTINUE SANS LUI, ET NE PEUT PLUS ÊTRE PERDUE ═══ [GDD 9.3]
    // « La chaîne de fin de partie financière est SUSPENDUE. » Nécessaire, dit le
    // GDD, « car une mission relativiste peut couvrir plusieurs décennies
    // terrestres ». `AgencyFinance` consultait déjà ce drapeau en tête de sa
    // chaîne ; personne ne le levait jamais.
    G.finance.suspended = true;
    jeu.agence.log("Embarquement : " + m->contract.title
                   + " — ARES passe sous l'adjoint [GDD 9.3]");
    return true;
  }

  // LE RETOUR. Dégèle l'agence et écrit la page que [GDD 9.3, 15.4] promettent :
  // « journal de reconstitution d'une absence ». Sans elle, une absence de
  // plusieurs années serait un trou noir pour un joueur à qui l'on a promis une
  // agence qui « fonctionne normalement » — donc qui a des comptes à rendre.
  bool debarquer() {
    if (!jeu.ares.initialisee()) return false;
    auto& G = *jeu.ares.etat;
    if (!G.lived.active) return false;

    const double retour = G.clock.now_days();
    const int menees = static_cast<int>(G.missions.size()) - G.lived.missions_at_departure;
    G.notebook.write(career::journal_absence(
        G.lived.depart_days, retour, menees < 0 ? 0 : menees, G.finance.treasury_me));
    G.finance.suspended = false;

    // ═══ CE QUE LA DOSE CHRONIQUE A COÛTÉ ═══ [GDD 6.6, 10.3 niveau 5]
    // Elle ne tuait PERSONNE : seule la dose aiguë avait un barème (DL50), et le
    // cumul chronique se contentait de verrouiller les vols suivants — c'est-à-dire
    // rien du tout sur un vol terminal [GDD 9.2], le seul où de telles doses
    // arrivent. Un aller-retour interstellaire rapporte ~10 Sv chroniques ; le
    // modèle les enregistrait et les oubliait.
    //
    // UN EFFET CHRONIQUE EST STOCHASTIQUE, donc il se TIRE — mais sur la graine de
    // mission, comme l'issue du vol et l'erreur d'injection : un rechargement
    // rejoue le même sort [GDD 18, déterminisme]. Le risque est le REID de la
    // mission, ICRP avec DDREF. Ce n'est pas une seconde punition superposée à la
    // dose aiguë : `reid_mission` en retire expressément la part aiguë, déjà jugée.
    const double reid = G.dose_architecte.reid_mission();
    bool cancer = false;
    if (reid > 0.0 && !G.character.operational_death) {
      Rng rng(mission::mission_seed(jeu.agence.graine_agence, G.lived.mission_id)
              ^ 0x5245494400000001ull);                       // « REID »
      cancer = rng.uniform01() < reid;
    }

    G.lived.disembark();
    G.avaries.clear();          // les avaries appartenaient au vol qui s'achève
    if (cancer) {
      // MORT AU RETOUR, PAS EN VOL : un cancer radio-induit se déclare des années
      // plus tard. C'est donc une mort NATURELLE anticipée [GDD 3.4], qui OUVRE
      // une passation — et non une mort opérationnelle, qui n'en ouvre aucune.
      // La distinction est celle du GDD, et elle change tout pour la partie.
      G.character.alive = false;
      jeu.agence.log("Retour de mission — cancer radio-induit declare : "
                     "l'Architecte ne reprendra pas son poste [GDD 6.6, 3.4]");
    } else {
      jeu.agence.log("Retour de mission — l'Architecte reprend son poste");
    }
    return true;
  }

  // ═══ CE QUE L'ÉQUIPAGE PEUT Y OPPOSER ═══ [GDD 9.1, 5.10]
  // « Diagnostics / réparations. » La capacité vient de l'ARBRE, pas d'un dé : on
  // répare ce que l'architecture permet de réparer, et ça prend le temps que ça
  // prend. C'est le premier effet EN VOL de la branche 4 après le recyclage.
  mission::CapaciteBord capacite_bord() const {
    mission::CapaciteBord c;
    c.maintenance_locale    = techno_operationnelle("maintenance_locale");
    c.diagnostics_autonomes = techno_operationnelle("diagnostics_autonomes");
    c.medecine_embarquee    = techno_operationnelle("medecine_embarquee");
    c.redondance_base       = techno_operationnelle("redondance_base");
    return c;
  }

  // Engage la réparation de l'avarie `idx`. L'avarie CONTINUE de coûter pendant
  // les travaux — c'est ce qui fait qu'on répare tôt plutôt que tard.
  std::string dernier_refus_reparation;
  bool reparer_avarie(std::size_t idx) {
    dernier_refus_reparation.clear();
    if (!jeu.ares.initialisee()) return false;
    auto& G = *jeu.ares.etat;
    if (idx >= G.avaries.size()) return false;
    mission::Avarie& av = G.avaries[idx];
    const double now = G.clock.now_days();
    if (av.reparee) { dernier_refus_reparation = "avarie deja reparee"; return false; }
    if (av.en_reparation(now)) {
      dernier_refus_reparation = "reparation deja engagee"; return false;
    }
    const mission::CapaciteBord cap = capacite_bord();
    if (!mission::reparable(av.kind, cap)) {
      // LE REFUS NOMME LA TECHNO MANQUANTE : sans ça, le joueur ne peut pas
      // savoir quoi rechercher (piège n°42).
      dernier_refus_reparation =
          av.kind == mission::EventKind::MedicalEmergency
              ? "aucune capacite medicale a bord : rechercher medecine_embarquee [GDD 5.10]"
              : "aucune capacite de reparation a bord : rechercher maintenance_locale [GDD 5.10]";
      return false;
    }
    av.fin_reparation_days = now + mission::duree_reparation_jours(av, cap);
    return true;
  }

  // ═══ LES RÉSERVES NE SUFFISENT PAS ═══ [GDD 9.4, 10.3]
  // « Une mission mal calculée avant lancement se traduit en dérives coûteuses,
  // VOIRE EN ÉCHEC si les réserves ne suffisent pas. » Le joueur est à bord : la
  // gravité n'est donc pas décrétée niveau 5, elle y MONTE — on déclare une perte
  // grave (Critique) assortie du modificateur d'exposition humaine, et c'est le
  // barème de [GDD 10.3] (« niveau final augmenté d'un palier si une présence
  // humaine est exposée à un risque létal ») qui en fait une catastrophe. Écrire
  // Catastrophe directement court-circuiterait la règle au lieu de l'appliquer.
  void verifier_reserves_vitales() {
    if (!jeu.ares.initialisee()) return;
    auto& G = *jeu.ares.etat;
    if (!G.lived.active) return;
    if (G.character.operational_death) return;          // déjà soldé

    // DEUX FAÇONS DE NE PAS RENTRER, et une seule est une faute de calcul.
    const bool vivres = G.lived.consumables_exhausted();
    // DOSE AIGUË LÉTALE [GDD 6.6] : `ACUTE_LETHAL_GY` = 4,5 Gy, la DL50 sans
    // soins. Elle vient d'une éruption, pas d'un oubli de provisionnement — d'où
    // l'absence de `player_error_causal` sur cette branche : l'environnement est
    // un ACTEUR [GDD 7.7], et le blindage est un pari, pas une case à cocher.
    const bool irradie = G.dose_architecte.acute_lethal();
    if (!vivres && !irradie) return;

    mission::Mission* m = nullptr;
    for (auto& mm : G.missions)
      if (mm.contract.id == G.lived.mission_id) { m = &mm; break; }
    if (!m) { G.lived.disembark(); return; }

    mission::AnomalyEvent ev;
    ev.mission_id = m->contract.id;
    ev.date_days  = G.clock.now_days();
    ev.what       = vivres ? "reserves vitales epuisees a bord"
                           : "dose aigue letale : blindage insuffisant";
    ev.severity   = mission::Severity::Critical;
    ev.modifiers.human_lethal_exposure = true;          // le palier monte ici
    ev.modifiers.primary_objective_lost = true;
    ev.modifiers.player_error_causal = vivres;          // provisionner était son métier
    G.apply_anomaly(*m, ev);

    // ═══ ET LE PERSONNAGE ÉTAIT À BORD ═══ [GDD 3.4, 10.3 niveau 5]
    // `consequences_for` refuse expressément de trancher : « game_over est décidé
    // par l'appelant : SEUL le décès du PERSONNAGE (pas d'un équipage PNJ)
    // termine la partie ». C'est ici, et nulle part ailleurs, qu'on sait qui
    // volait — d'où le fait que `lived.active` soit la condition d'entrée. Les
    // deux faits restent distincts : le modificateur d'exposition humaine porte
    // la GRAVITÉ (un équipage est mort), `lived.active` porte la FIN DE PARTIE
    // (c'était le joueur).
    G.character.alive = false;
    G.character.operational_death = true;
    // Rendu VISIBLE : une mort opérationnelle que rien n'affiche serait un
    // drapeau de plus que personne ne lit. La modale de fin de partie existe
    // déjà ; elle porte désormais deux motifs, et le dit.
    jeu.game_over = true;
    jeu.raison_faillite =
        std::string("MORT OPERATIONNELLE — ")
        + (vivres ? "reserves vitales epuisees" : "dose aigue letale")
        + " a bord de " + m->contract.title
        + ". [GDD 3.4] Aucune passation ne releve d'un deces en mission.";

    m->flight_flown = true;
    m->flight_success = false;
    // La chaîne financière se rouvre : il n'y a plus d'absent à protéger.
    G.finance.suspended = false;
    G.lived.disembark();
  }

  // ═══════════════════════════════════════════════════════════════════════
  // LA PASSATION [GDD 3.4, 3.5, décisions 6 et 7]
  // ═══════════════════════════════════════════════════════════════════════
  // « Portée multi-générationnelle : atteindre la fin de la branche 6 demande
  // souvent plusieurs vies. » Le vieillissement existait (en temps PROPRE, écart
  // relativiste compris), la fin de vie était calculable, `career::Succession`
  // était écrite et sous oracle — et RIEN ne les reliait : un Architecte de
  // 120 ans gardait son poste, un Architecte mort d'un cancer radio-induit
  // aussi. La portée multi-générationnelle n'existait tout simplement pas.
  bool passation_en_attente() const {
    if (!jeu.ares.initialisee()) return false;
    const auto& G = *jeu.ares.etat;
    return G.passation_ouverte && !G.character.operational_death;
  }

  // Ce que le successeur trouve en prenant le poste — pour l'écran, avant qu'il
  // n'accepte. Aucun calcul ici : le modèle a déjà tout tranché.
  std::string resume_passation() const {
    if (!jeu.ares.initialisee()) return {};
    const auto& G = *jeu.ares.etat;
    return std::string("RANG CONSERVE : ") + career::rank_name(G.career.rank)
         + "  .  CONFIANCE REMISE A 70  .  CARNET TRANSMIS ("
         + std::to_string(G.notebook.entries.size()) + " entrees)"
         + "  .  ETAT PROGRAMMATIQUE INTACT";
  }

  // L'ACTE. Ne touche QUE ce qui est personnel ; l'état programmatique (arbre,
  // finances, missions, station, catalogue, carnet) appartient à ARES et n'est
  // pas même lu — c'est la ligne « État programmatique : Oui, INTÉGRALEMENT »
  // du tableau de [GDD 3.5], et la meilleure façon de la tenir est de ne rien
  // écrire.
  bool passer_la_main() {
    if (!passation_en_attente()) return false;
    auto& G = *jeu.ares.etat;

    // ═══ CE QUI RESTE AU POSTE ═══ [décision 6] Le rang est un droit
    // institutionnel durable : il appartient au POSTE, pas à la personne.
    const career::Rank rang = G.career.rank;
    // ═══ CE QUI MEURT AVEC LA PERSONNE ═══ [décision 7] La crédibilité
    // personnelle repart à 70, le score à zéro — et la DOSE avec eux : c'est un
    // corps neuf, ce qui rouvre les vols terminaux que son prédécesseur ne
    // pouvait plus faire [GDD 6.6, 9.2].
    G.career = career::Succession::inherit_career(rang);
    G.dose_architecte = env::DoseAccumulator{};
    // L'ÉCART D'HORLOGE EST PERSONNEL AUSSI : il mesure ce que CE corps a vécu
    // de moins que la Terre [GDD 6.7.5]. Le successeur repart à zéro d'écart.
    G.dual_clock = rel::DualClock{};
    ++G.generation;
    G.character = career::Succession::inherit_character(
        "L'Architecte", G.clock.sim_time_s());

    // ═══ ET SI LE DÉFUNT ÉTAIT EN VOL ═══ [GDD 9.2, 9.3]
    // Une mission vécue survit à son responsable scientifique : l'équipage est
    // toujours à bord et la mission continue. Ce qui cesse, c'est l'ABSENCE —
    // le successeur est à son poste, donc la protection financière de [9.3] n'a
    // plus lieu d'être. Le vol, lui, n'est pas touché : ce serait le perdre pour
    // une raison qui ne le concerne pas.
    if (G.lived.active) {
      G.lived.disembark();
      G.finance.suspended = false;
      jeu.agence.log("[GDD 9.3] Le successeur prend le poste au sol : l'agence "
                     "n'est plus en absence, la chaine financiere reprend.");
    }

    modal = Modal::Aucun;
    jeu.agence.log("[GDD 3.5] Passation : " + std::to_string(G.generation) +
                   "e Architecte en fonction, rang " +
                   std::string(career::rank_name(rang)) +
                   " conserve, confiance remise a 70.");
    // Le carnet est le lien entre deux vies : on l'y écrit, comme le ferait le
    // successeur en ouvrant le cahier de son prédécesseur [GDD 15.4]. Le motif
    // est lu AVANT d'être effacé — c'est ce qui reste du prédécesseur.
    career::NotebookEntry e;
    e.title = "Passation — prise de fonction du " +
              std::to_string(G.generation) + "e Architecte";
    e.body = G.passation_motif;
    e.date_days = G.clock.now_days();
    G.notebook.write(std::move(e));
    G.passation_ouverte = false;
    G.passation_motif.clear();
    sauvegarder_partie();
    return true;
  }

  // Quitter la partie : on sauve, puis retour au menu.
  void retour_menu() {
    sauvegarder_partie();
    poste_ouvert = -1;
    scene = SceneJeu::Titre;
  }

  // ═══ VOL DE CAMÉRA [M] ═══ (voir VolCamera plus haut)
  static constexpr double DIST_SYSTEME_KM = 9.0e8;   // ~6 UA : le système interne cadré
  // Pose de la vue système : les valeurs de repos du pont (RenderBridge::MapCam).
  static constexpr double YAW_SYSTEME = 0.60;
  static constexpr double PITCH_SYSTEME = 1.05;

  // ═══ POSE D'AMARRAGE DE LA CAMÉRA (le handoff) ═══
  // La caméra de la carte est une ORBITE : œil = point visé + (dist, yaw, pitch).
  // Pour que la reprise en 1re personne soit invisible, cette orbite doit finir
  // pile sur l'œil du pawn. On convertit donc la position de l'œil (repère
  // station, mètres) en (dist, yaw, pitch) autour du centre de la station.
  //
  // Repère station -> monde de rendu : MIROIR EN Y (glTF droitier -> UE gaucher),
  // comme partout dans ce projet.
  //
  // ═══ ET L'ATTITUDE, SANS QUOI LA CAMÉRA SORT DU MAUVAIS CÔTÉ ═══ (2026-07-27)
  // La station TOURNE avec son orbite (cupola au nadir, un tour par orbite dans
  // l'inertiel). L'œil du pawn est donné dans le repère du MODÈLE ; sa direction
  // vue du monde est donc l'image de ce vecteur PAR L'ATTITUDE. Sans cette
  // rotation, le vol [M] sortait toujours par le même flanc inertiel de la
  // station : à un moment de l'orbite il traversait la coque, et surtout il ne
  // retombait plus sur l'œil du pawn — la coupure du handoff serait revenue,
  // d'une rotation entière. L'attitude vient du pont, où le modèle vient de la
  // publier : la caméra, l'intérieur et le modèle extérieur en voient UNE SEULE.
  struct PoseBord { double dist_km, yaw, pitch; };

  PoseBord pose_bord() const {
    const auto& O = g_render_bridge.station_out;
    double m[3] = {NOVELLUS_OEIL_M[0], NOVELLUS_OEIL_M[1], NOVELLUS_OEIL_M[2]};
    // UE publie l'œil VIVANT du pawn : on ressort donc là où l'on est entré, pas
    // au point d'apparition. Avant qu'il n'ait bâti la scène, la pose de la
    // référence fait foi (même chiffre, même source : app/postes.hpp).
    if (O.ready.load()) {
      m[0] = O.eye_m[0].load(); m[1] = O.eye_m[1].load(); m[2] = O.eye_m[2].load();
    }
    Vec3 local{m[0] / 1000.0, -m[1] / 1000.0, m[2] / 1000.0};
    if (!(norm(local) > 1.0e-5))   // œil au centre exact : direction indéfinie -> référence
      local = Vec3{ NOVELLUS_OEIL_M[0] / 1000.0,
                   -NOVELLUS_OEIL_M[1] / 1000.0,
                    NOVELLUS_OEIL_M[2] / 1000.0};
    const Vec3 o = appliquer_attitude(attitude_publiee(), local);
    const double r = norm(o);
    const double s = std::max(-1.0, std::min(1.0, o.z / r));
    return {r, std::atan2(o.y, o.x), std::asin(s)};
  }

  // L'attitude telle qu'elle est publiée sur le pont (repère de rendu). Relue
  // plutôt que recalculée : c'est l'assurance que la caméra et la géométrie
  // partagent le MÊME triplet, à la frame près.
  static AttitudeRendu attitude_publiee() {
    const auto& S = g_render_bridge.station;
    return {Vec3{S.att_avant[0],   S.att_avant[1],   S.att_avant[2]},
            Vec3{S.att_tribord[0], S.att_tribord[1], S.att_tribord[2]},
            Vec3{S.att_zenith[0],  S.att_zenith[1],  S.att_zenith[2]}};
  }

  double dist_bord_km() const { return pose_bord().dist_km; }

  // ENVELOPPE DE LA STATION : demi-envergure du modèle intérieur (55 m -> 27,5 m).
  // C'est le seuil de COEXISTENCE. Plancher relatif à la pose de l'œil : si le
  // joueur s'est éloigné dans le couloir, l'enveloppe s'ouvre avec lui, sinon la
  // fenêtre de bascule serait nulle.
  double rayon_enveloppe_km() const {
    return std::max(STATION_ENVERGURE_M * 0.5 / 1000.0, pose_bord().dist_km * 1.35);
  }

  // MÉLANGE DU REGARD : 0 hors de l'enveloppe (la caméra regarde la station), 1
  // à l'arrivée sur l'œil du pawn (la caméra regarde CE QUE regarde le pawn).
  // Piloté par la DISTANCE et non par le progrès du vol : la bascule se joue donc
  // exactement sur la portion qui traverse la coque, et elle est SYMÉTRIQUE
  // (l'entrée et la sortie suivent la même loi).
  double melange_regard(double dist_km) const {
    const double a = pose_bord().dist_km;
    const double e = rayon_enveloppe_km();
    if (dist_km <= a) return 1.0;
    if (dist_km >= e) return 0.0;
    const double t = (e - dist_km) / (e - a);
    return t * t * (3.0 - 2.0 * t);
  }

  // Publie l'état de caméra du vol en cours. Un seul endroit écrit ces champs :
  // la géométrie du handoff reste vérifiable d'un seul coup d'œil.
  void publier_camera_vol() {
    auto& B = g_render_bridge;
    // ═══ LE BOUT « BORD » DU VOL SUIT L'ATTITUDE VIVANTE ═══ (2026-07-27)
    // La pose d'amarrage était figée au DÉPART du vol. Elle ne pouvait pas l'être
    // une fois la station tournante : le vol dure 0,9 s de temps réel, ce qui à
    // pleine cadence fait des CENTAINES d'orbites de temps de jeu — la caméra
    // aurait visé le flanc où la station se trouvait au coup de touche, et serait
    // arrivée n'importe où sauf sur l'œil du pawn. On resynchronise donc l'extrémité
    // « bord » à chaque frame ; l'autre extrémité (le plan système) est inerte.
    // Le déplacement est continu et pondéré par (1 − lissage(progrès)) : aucun
    // à-coup, et l'arrivée est exacte par construction.
    const PoseBord pb = pose_bord();
    if (vol_cam.sens == SensVol::VersBord) {
      vol_cam.dist_arrivee_km = pb.dist_km;
      vol_cam.yaw_arrivee = pb.yaw;
      vol_cam.pitch_arrivee = pb.pitch;
    } else {
      vol_cam.dist_depart_km = pb.dist_km;
      vol_cam.yaw_depart = pb.yaw;
      vol_cam.pitch_depart = pb.pitch;
    }
    const double d = vol_cam.dist_courante_km();
    B.cam.dist_km = d;
    B.cam.yaw = vol_cam.yaw_courant();
    B.cam.pitch = vol_cam.pitch_courant();
    B.cam.look_to_bord = melange_regard(d);
    B.cam.vol_camera = vol_cam.actif;
    B.interieur_coexiste = (d <= rayon_enveloppe_km());
  }

  // [M] : lance le vol continu entre bord et système, ancré sur NOVELLUS.
  void demarrer_vol_cadrage() {
    if (vol_cam.actif || scene != SceneJeu::Monde) return;   // un vol à la fois
    const PoseBord pb = pose_bord();
    vol_cam.actif = true;
    vol_cam.progres = 0.0;
    if (cadrage == Cadrage::Bord) {
      // On quitte le bord : la vue s'ouvre DEPUIS L'ŒIL du joueur et recule. Le
      // plan système rend dès maintenant, mais l'intérieur coexiste tant qu'on
      // n'a pas franchi la coque — d'où l'absence de saut au départ.
      cadrage = Cadrage::Systeme;
      vol_cam.sens = SensVol::VersSysteme;
      vol_cam.dist_depart_km  = pb.dist_km;
      vol_cam.dist_arrivee_km = DIST_SYSTEME_KM;
      vol_cam.yaw_depart   = pb.yaw;    vol_cam.yaw_arrivee   = YAW_SYSTEME;
      vol_cam.pitch_depart = pb.pitch;  vol_cam.pitch_arrivee = PITCH_SYSTEME;
    } else {
      // On rentre à bord : la caméra plonge vers Novellus et s'AMARRE sur l'œil
      // du pawn ; la main passe à la 1re personne à l'arrivée, sans coupure.
      vol_cam.sens = SensVol::VersBord;
      vol_cam.dist_depart_km  = std::max(pb.dist_km, g_render_bridge.cam.dist_km.load());
      vol_cam.dist_arrivee_km = pb.dist_km;
      vol_cam.yaw_depart   = g_render_bridge.cam.yaw.load();    vol_cam.yaw_arrivee   = pb.yaw;
      vol_cam.pitch_depart = g_render_bridge.cam.pitch.load();  vol_cam.pitch_arrivee = pb.pitch;
    }
    g_render_bridge.focus_body = FOCUS_STATION;
    publier_camera_vol();
  }

  // Échap depuis le système : retour IMMÉDIAT à bord (coupe tout vol en cours).
  // Reste une coupure SÈCHE, et c'est son objet : c'est la sortie de secours.
  void retour_bord_immediat() {
    vol_cam.actif = false;
    cadrage = Cadrage::Bord;
    g_render_bridge.cam.look_to_bord = 0.0;
    g_render_bridge.cam.vol_camera = false;
    g_render_bridge.interieur_coexiste = false;
  }

  // -------------------------------------------------------------------------
  // LA FRAME : mise à jour d'état + publication du pont. Appelée une fois par
  // frame par USPGameSubsystem, AVANT que le monde UE ne lise le pont.
  void tick(double dt_reel) {
    // ═══ LE TEMPS QUI COULE [GDD 14.2] ═══ — AVANT tout le reste, pour que la
    // couche ARES et la publication du pont voient déjà le nouveau calendrier.
    // Le temps ne coule que DANS une partie et hors modale : une modale porte une
    // décision (faillite, réglages), le monde l'attend. Un POSTE OUVERT, lui, ne
    // suspend PAS le temps — c'est précisément au poste AGENCE qu'on règle la
    // cadence, et il faut la voir agir. La cadence par défaut est la PAUSE : rien
    // ne bouge tant que le joueur ne l'a pas demandé.
    if (scene == SceneJeu::Monde && modal == Modal::Aucun)
      jeu.faire_couler_le_temps(dt_reel);

    // couche ARES : création/reset/rattrapage mensuel (lecture seule sur l'agence)
    jeu.ares.assurer(jeu.agence, jeu.epoch_courant());

    // LES CONSOMMABLES SE SONT CONSOMMÉS DANS LE SOUS-PAS (GameState::tick) ; on
    // regarde ici s'il en reste. Après `assurer`, donc jamais sur un état absent.
    verifier_reserves_vitales();

    // NOVELLUS EST PUBLIÉE TÔT, ET C'EST UNE DÉPENDANCE, PAS UN RANGEMENT : le vol
    // de caméra ci-dessous lit l'ATTITUDE de la station sur le pont
    // (`pose_bord`). Publiée en fin de frame avec le reste de la carte, elle
    // aurait toujours une frame de retard sur la position visée — invisible au
    // temps réel, une orbite entière à pleine cadence.
    publier_novellus(jeu.epoch_courant());

    // ═══ LA PHASE DE VOL EST DÉRIVÉE [GDD 14.3] ═══
    // `Mission::phase` pilote les taux d'anomalie (Events.hpp) mais RIEN ne la
    // renseignait : elle ne pouvait être posée qu'à la main, c'est-à-dire
    // exactement le drapeau abstrait que la doctrine interdit. Elle est
    // maintenant recopiée depuis `flight_phase_of` — fonction de l'état FSM, du
    // temps passé dedans et de la famille — donc toujours vivante et rejouable.
    if (jeu.ares.initialisee()) {
      auto& G = *jeu.ares.etat;
      const double now_days = G.clock.now_days();
      for (auto& m : G.missions) m.phase = mission::flight_phase_of(m, now_days);
    }

    // garde-fous de routage : sans agence créée, on ne peut pas être dans le Monde.
    if (scene == SceneJeu::Monde && !jeu.agence.creee)
      scene = SceneJeu::Titre;
    if (scene != SceneJeu::Monde) vol_cam.actif = false;   // pas de vol hors du Monde

    // ═══ VOL DE CAMÉRA [M] ═══ : avance la transition et PILOTE la pose de
    // caméra cible (le lissage côté rendu la suit). À l'arrivée d'un vol de
    // RETOUR, la main passe à la 1re personne — la caméra étant DÉJÀ amarrée sur
    // l'œil du pawn, la reprise ne se voit pas (incr. 3c-3).
    if (vol_cam.actif) {
      vol_cam.avancer(dt_reel);
      g_render_bridge.focus_body = FOCUS_STATION;
      publier_camera_vol();
      if (vol_cam.fini()) {
        if (vol_cam.sens == SensVol::VersBord) {
          cadrage = Cadrage::Bord;
        } else {
          // Au plan système, Novellus est sous-pixellique (109 m vus de 6 UA) :
          // l'ancre utile redevient la TERRE. Le point visé saute de 418 km à
          // 1 UA de distance de vue — invisible, et DÉCLARÉ ici [GDD 6.8].
          g_render_bridge.focus_body = static_cast<int>(ephem::Body::EarthBary);
        }
        vol_cam.actif = false;
      }
    }
    // Hors vol, aucune coexistence ni mélange de regard : au plan système on
    // regarde le point visé, au bord la station rend dans son repère canonique.
    if (!vol_cam.actif) {
      g_render_bridge.cam.look_to_bord = 0.0;
      g_render_bridge.cam.vol_camera = false;
      g_render_bridge.interieur_coexiste = false;
    }

    // LA FAILLITE EST UN GAME OVER, imposé par le MODÈLE [économie stricte] :
    // c'est le seul état que l'UI ne peut pas refuser. Il se pose par-dessus la
    // scène en cours ; seules « nouvelle partie » et « charger » en sortent.
    // Au menu il n'y a plus de partie à condamner : la modale y est levée, même
    // si le modèle porte encore le drapeau (le joueur a déjà quitté la partie).
    if (scene == SceneJeu::Titre) {
      if (modal == Modal::GameOver || modal == Modal::Passation) modal = Modal::Aucun;
    } else if (jeu.game_over) {
      modal = Modal::GameOver;
    } else if (modal == Modal::GameOver) {
      modal = Modal::Aucun;
    } else if (passation_en_attente()) {
      // ═══ LA PASSATION SE CONSTATE, ELLE NE SE SUBIT PAS ═══ [GDD 3.4, 3.5]
      // Elle vient APRÈS le Game Over dans cette chaîne, et ce n'est pas un
      // détail d'écriture : une mort opérationnelle est irrévocable, et « la
      // passation ne l'annule jamais ». Si les deux se présentaient, c'est la fin
      // de partie qui doit rester à l'écran.
      modal = Modal::Passation;
    } else if (modal == Modal::Passation) {
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
  // ═══ NOVELLUS DANS LE MONDE [GDD v1.2 11.1, 17.3] ═══
  // La station a une position RÉELLE dans le monde unique : orbite LEO à 418 km,
  // inclinée de 51,64° sur l'ÉQUATEUR TERRESTRE, nœud en régression J2, période
  // 92,9 min. Le modèle et ses approximations vivent dans `app/novellus_orbite.hpp`
  // — la session ne fait que le publier.
  //
  // Elle passait AVANT par le helper de la FLOTTE (un cercle en plan écliptique) :
  // le rayon était bon, donc la période aussi, mais le PLAN était faux de 51,6° —
  // et un plan faux, c'est une Terre qui défile n'importe où sous la cupola.
  //
  // Publiée à part, et TÔT dans la frame : le vol de caméra [M] lit l'attitude
  // qu'elle pose (cf. `pose_bord`).
  void publier_novellus(double epoch) {
    auto& B = g_render_bridge;
    const NovellusEtat s = novellus_etat(epoch);
    B.station.rel_m[0] = s.r.x; B.station.rel_m[1] = s.r.y; B.station.rel_m[2] = s.r.z;
    // VITESSE : ANALYTIQUE (voir `novellus_etat`), pas une différence de positions.
    // Elle sert à l'ATTITUDE (l'axe de vol), et la dériver côté rendu serait faux
    // dès que le temps s'accélère : à mois/s, une frame avance de ~12 h, soit près
    // de huit orbites LEO — la corde entre deux échantillons ne dit plus rien de la
    // tangente, et la station se mettrait à tomber en vrille.
    B.station.vel_ms[0] = s.v.x; B.station.vel_ms[1] = s.v.y; B.station.vel_ms[2] = s.v.z;
    // ATTITUDE : calculée ICI parce qu'elle a trois consommateurs qui doivent voir
    // exactement la même (cf. `StationWorld::att_*`), dont `pose_bord`.
    const AttitudeRendu a = novellus_attitude_rendu(s.r, s.v);
    for (int k = 0; k < 3; ++k) {
      B.station.att_avant[k]   = a.avant[k];
      B.station.att_tribord[k] = a.tribord[k];
      B.station.att_zenith[k]  = a.zenith[k];
    }
    B.station.altitude_km = NOVELLUS_ALTITUDE_M / 1000.0;
    B.station.envergure_m = 109.0;   // envergure RÉELLE de l'ISS (~109 m, arrays comprises)
    B.station.valid = true;
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
    // La barre de temps AFFICHE la cadence, elle ne la commande pas [GDD 14].
    B.cadence = static_cast<int>(jeu.cadence);
    // ... et le PLAFOND que la mission impose [GDD 14.3], pour que le bandeau du
    // temps grise ce qui est interdit et NOMME la phase qui l'interdit. Un cran
    // refusé sans motif affiché serait un mécanisme incompréhensible.
    {
      const mission::TempoLimit lim = jeu.plafond_temps();
      B.cadence_max = static_cast<int>(lim.max_rate);
      B.tempo_phase = static_cast<int>(lim.phase);
      B.tempo_contraint = lim.constrained;
    }

    // LE VOL EN COURS [GDD 4.1, 9] : sa phase et sa date d'arrivée. Publié
    // depuis le MODÈLE — la chronologie est dérivée de la mission, le rendu ne
    // recalcule rien (doctrine du pont).
    {
      B.vol_actif = false;
      B.vol_phase = static_cast<int>(mission::FlightPhase::Ground);
      B.vol_arrivee_datee = false;
      B.vol_reste_jours = 0.0;
      if (jeu.ares.initialisee()) {
        auto& G = *jeu.ares.etat;
        const double now_days = G.clock.now_days();
        for (const auto& m : G.missions) {
          if (m.state != mission::MissionState::Launched) continue;
          const mission::ArrivalStatus a = mission::flight_arrival(m, now_days);
          B.vol_actif = true;
          B.vol_phase = static_cast<int>(mission::flight_phase_of(m, now_days));
          B.vol_arrivee_datee = a.dated;
          B.vol_reste_jours = a.reste_jours;
          break;   // une seule mission en vol à la fois dans la boucle actuelle
        }
      }
    }

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

    // NOVELLUS est publiée PLUS TÔT dans la frame (`publier_novellus`, appelée en
    // tête de `tick`) : le vol de caméra en dépend. Rien à faire ici.

    // ═══ LA TRACE DU VOL DANS LE MONDE [GDD 8.3, 17.3] ═══
    // Le tracé du pont dormait depuis la scission de `jeu.cpp` : le rendu savait
    // dessiner trajectoire, corridor et nœuds, plus personne ne les publiait. La
    // chronologie ayant donné au vol des DATES, il a maintenant des POSITIONS —
    // et c'est le modèle qui les calcule (Lambert + Kepler sur l'éphéméride
    // réelle), jamais le rendu.
    publier_trace_vol(epoch);
    // ═══ ET LA COUPE DE LA CONCEPTION EN COURS [GDD 12.2] ═══
    // Seulement quand le poste CONCEPTION est ouvert : c'est le seul écran qui la
    // dessine, et `evaluate_design` n'a rien à faire sous le plan système.
    if (poste_conception_ouvert()) publier_coupe_design();
    else g_render_bridge.hull_design.valid = false;
    rafraichir_poursuite();
    B.geo.valid = false;   // vol GEO 2D retiré (mécanique héritée)
  }

  // ═══ LA CONNAISSANCE GRANDIT AVEC L'ARC ═══ [GDD 8.6]
  // `nav_connu_dv` et `nav_sigma_*` étaient FIGÉS au feu vert : la solution de
  // navigation du joueur ne bougeait plus d'un iota pendant 329 jours de vol, et
  // « le joueur choisit son rythme de mesure » n'avait aucun sens — il n'y avait
  // rien à choisir. Ils sont maintenant recalculés à mesure que les antennes
  // écoutent.
  //
  // RECALCUL PAR PASSE, PAS PAR FRAME : le filtre ne gagne une observation que
  // toutes les `TRACKING_SAMPLE_S` (8 h de temps de jeu) — inutile de le relancer
  // plus souvent, et hors de question de le faire à chaque frame (un arc long,
  // c'est des milliers de propagations de Kepler). Le seuil est donc la cadence
  // de mesure elle-même : un chiffre, une source.
  void rafraichir_poursuite() {
    // Même garde que `publier_trace_vol` : au Titre, aucune partie n'existe et
    // `ares.etat` est nul. L'oublier coûte un segfault sur la toute première
    // frame, avant même qu'il y ait une mission à poursuivre.
    if (!jeu.ares.initialisee()) return;
    const double now = jeu.ares.etat->clock.now_days();
    for (mission::Mission& m : jeu.ares.etat->missions) {
      if (!m.nav_evaluee || !m.vol_vrai_valide) continue;
      if (!mission::flight_has_arc(m)) continue;
      // LA DATE D'INJECTION DE **CETTE** MISSION, lue dans SA chronologie.
      // La prendre dans `trace_vol` reviendrait à donner à toutes les missions
      // en vol l'injection de la première d'entre elles — faux dès qu'il y en a
      // deux, et invisible tant qu'il n'y en a qu'une.
      double t_inj = 0.0;
      if (!mission::flight_injection_days(m, t_inj)) continue;
      const double arc = arc_poursuite_disponible(m, t_inj, now);
      if (arc <= m.arc_poursuite_j + mission::TRACKING_SAMPLE_S / cst::DAY) continue;
      const ContexteVol c = contexte_vol(m, now);
      if (!c.ok) continue;
      for (int k = 0; k < 3; ++k) m.nav_connu_dv[k] = c.sol.dv_estime[k];
      m.nav_sigma_r = c.sol.sigma_r;
      m.nav_sigma_v = c.sol.sigma_v;
      m.arc_poursuite_j = c.arc_jours;
    }
  }

  // ═══ ACHETER DE L'ÉCOUTE, EN VOL ═══ [GDD 8.6, 13.3]
  // « Trop rare laisse dériver, trop fréquent coûte des ressources et du temps. »
  // Le joueur autorise le DSN à écouter `jours` de plus. Ça se PAIE (tarif
  // d'antenne, `cout_poursuite_me`) et ça ne fait gagner AUCUN temps : l'arc
  // reste borné par ce qui s'est réellement écoulé. Acheter tôt et attendre, ou
  // ne pas acheter et rester aveugle — c'est le choix, et il est daté.
  // Rend false si la trésorerie ne suit pas : on ne creuse pas la réserve pour
  // une passe d'antenne.
  bool acheter_poursuite(mission::Mission& m, double jours) {
    if (jours <= 0.0 || !m.nav_evaluee) return false;
    const double cout = mission::cout_poursuite_me(jours);
    auto& F = jeu.ares.etat->finance;
    if (F.treasury_me < cout) return false;
    F.treasury_me -= cout;
    m.poursuite_jours += jours;
    return true;
  }

  // Arc courant et sa signature : reconstruire Lambert + 512 propagations à
  // chaque frame serait un gaspillage pur, et le pont prévoyait déjà
  // `last_arc_sig` pour l'éviter. L'arc est FIGÉ au feu vert : tant que la date,
  // la durée de transit et la destination ne bougent pas, il n'a aucune raison
  // d'être refait — seule la POSITION avance, pour une propagation.
  mission::FlightTrace trace_vol;
  mission::NavDispersion trace_disp;   // dispersion du vol en cours (corridor)
  double trace_sig{0.0};
  bool   trace_valide{false};   // « il y a un arc » ne se lit pas dans la signature

  void publier_trace_vol(double epoch) {
    auto& B = g_render_bridge;
    auto invalider = [&] {
      B.vehicle.valid = false; B.hull_vol.valid = false; trace_valide = false;
    };
    if (!jeu.ares.initialisee()) { invalider(); return; }
    auto& G = *jeu.ares.etat;
    const double now_days = G.clock.now_days();

    const mission::Mission* vol = nullptr;
    for (const auto& m : G.missions)
      if (mission::flight_has_arc(m)) { vol = &m; break; }
    if (!vol) { invalider(); return; }

    const double sig = mission::flight_trace_signature(*vol);
    if (!trace_valide || sig != trace_sig) {
      trace_vol = mission::build_flight_trace(*vol, now_days, epoch, jeu.eph);
      trace_sig = sig;
      trace_valide = trace_vol.ok;
      // LA DISPERSION SE CALCULE AVEC L'ARC, pas à chaque frame : elle ne dépend
      // que de l'injection, qui est figée au feu vert comme l'arc lui-même.
      if (trace_vol.ok) {
        const double t_dep_tdb =
            epoch + (trace_vol.depart().t_days - now_days) * cst::DAY;
        const Vec3 v_terre = jeu.eph.state(ephem::Body::EarthBary, ephem::Body::Sun,
                                           fen::Epoch{t_dep_tdb}).v;
        trace_disp = mission::nav_dispersion(trace_vol, v_terre,
                                             mission_plan.program.dv_margin);
        B.vehicle.gen.fetch_add(1);
      }
    } else {
      mission::trace_avancer(trace_vol, now_days);
    }
    if (!trace_vol.ok) { B.vehicle.valid = false; B.hull_vol.valid = false; return; }
    // LE CORRIDOR 3σ [GDD 8.3] : ce que le joueur ne SAIT PAS de sa position. Il
    // croît depuis l'injection, et rien ne le rétrécit encore — la poursuite est
    // la brique suivante, et c'est DÉCLARÉ plutôt que simulé.
    trace_vol.corridor_3s_m =
        mission::corridor_3sigma_m(trace_vol, trace_disp, now_days);

    B.vehicle.n = trace_vol.n < RenderBridge::VehicleSnap::MAX_PTS
                      ? trace_vol.n : RenderBridge::VehicleSnap::MAX_PTS;
    for (int k = 0; k < B.vehicle.n; ++k) {
      B.vehicle.traj_m[k][0] = trace_vol.traj[k].x;
      B.vehicle.traj_m[k][1] = trace_vol.traj[k].y;
      B.vehicle.traj_m[k][2] = trace_vol.traj[k].z;
    }
    B.vehicle.pos_m[0] = trace_vol.pos.x;
    B.vehicle.pos_m[1] = trace_vol.pos.y;
    B.vehicle.pos_m[2] = trace_vol.pos.z;
    B.vehicle.vel_ms[0] = trace_vol.vel.x;
    B.vehicle.vel_ms[1] = trace_vol.vel.y;
    B.vehicle.vel_ms[2] = trace_vol.vel.z;
    B.vehicle.corridor_3s_m = trace_vol.corridor_3s_m;
    // LES NŒUDS : deux pour un transfert direct (injection, arrivée), jusqu'à six
    // pour un tour d'assistance — chaque manœuvre profonde et chaque survol est un
    // instant où quelque chose se passe, donc un point qui se voit [GDD 8.3].
    B.vehicle.n_nodes = trace_vol.n_nodes < mission::FlightTrace::MAX_NODES
                            ? trace_vol.n_nodes : mission::FlightTrace::MAX_NODES;
    for (int k = 0; k < B.vehicle.n_nodes; ++k) {
      B.vehicle.nodes_m[k][0] = trace_vol.nodes[k].pos.x;
      B.vehicle.nodes_m[k][1] = trace_vol.nodes[k].pos.y;
      B.vehicle.nodes_m[k][2] = trace_vol.nodes[k].pos.z;
      B.vehicle.node_done[k] = trace_vol.nodes[k].done;
    }
    B.vehicle.valid = true;
    publier_coupe(B.hull_vol, coupe_du_vol(*vol));
  }

  // ═══ LA COUPE, DU MODÈLE AU PONT ═══ [GDD 12.2, 17.2]
  // Le rendu ne reçoit que des mètres. `gen` ne bouge que si la coupe CHANGE :
  // reconstruire une géométrie à chaque frame pour un vaisseau qui ne change
  // jamais serait le gaspillage que `last_arc_sig` évite déjà pour l'arc.
  static void publier_coupe(RenderBridge::HullSnap& H,
                            const vehicle::VehicleHull& h) {
    if (!h.valid) { H.valid = false; H.n = 0; return; }
    const int n = h.segments.size() < RenderBridge::HullSnap::MAX_SEG
                      ? static_cast<int>(h.segments.size())
                      : RenderBridge::HullSnap::MAX_SEG;
    // Signature : la coupe a-t-elle bougé ? Longueur et diamètre suffisent — deux
    // piles distinctes qui auraient EXACTEMENT les mêmes cotes se dessinent
    // pareil, ce qui est précisément le cas où ne rien refaire est correct.
    const bool change = !H.valid.load() || H.n != n ||
                        std::fabs(H.length_m - h.length_m) > 1e-9 ||
                        std::fabs(H.diameter_m - h.max_diameter_m) > 1e-9;
    H.n = n;
    H.length_m = h.length_m;
    H.diameter_m = h.max_diameter_m;
    for (int k = 0; k < n; ++k) {
      const auto& s = h.segments[static_cast<std::size_t>(k)];
      H.seg[k].role = static_cast<int>(s.role);
      H.seg[k].stage = s.stage;
      H.seg[k].z0_m = s.z0_m; H.seg[k].z1_m = s.z1_m;
      H.seg[k].r0_m = s.r0_m; H.seg[k].r1_m = s.r1_m;
    }
    H.valid = true;
    if (change) H.gen.fetch_add(1);
  }

  // La coupe du vaisseau EN VOL : reconstruite depuis ce que le feu vert a figé
  // sur la mission, jamais depuis la conception courante.
  static vehicle::VehicleHull coupe_du_vol(const mission::Mission& m) {
    vehicle::VehicleHull h;
    if (m.vaisseau_etages.empty()) return h;
    std::vector<vehicle::StageChoice> pile;
    std::vector<double> ergols;
    pile.reserve(m.vaisseau_etages.size());
    ergols.reserve(m.vaisseau_etages.size());
    for (const auto& e : m.vaisseau_etages) {
      vehicle::StageChoice st;
      st.engine = e.engine;
      st.tank = e.tank;
      pile.push_back(st);
      ergols.push_back(e.propellant_kg);
    }
    const auto& caps = vehicle::capsule_catalog();
    const vehicle::CapsulePart* cap =
        (m.vaisseau_capsule >= 0 && m.vaisseau_capsule < static_cast<int>(caps.size()))
            ? &caps[static_cast<std::size_t>(m.vaisseau_capsule)] : nullptr;
    return vehicle::build_hull(pile, ergols, cap, m.vaisseau_payload_kg);
  }

  bool poste_conception_ouvert() const {
    if (poste_ouvert < 0) return false;
    int n = 0;
    const PosteDef* d = postes_def(n);
    return poste_ouvert < n && std::string(d[poste_ouvert].id) == "conception";
  }

  // La coupe de la conception EN COURS — celle que le poste CONCEPTION dessine
  // [GDD 12.2]. Elle bouge à chaque clic du joueur, et c'est sa raison d'être.
  void publier_coupe_design() {
    const DesignSummary s = evaluate_design(vehicule_design);
    std::vector<double> ergols;
    ergols.reserve(s.stages.size());
    for (const auto& st : s.stages) ergols.push_back(st.propellant_kg);
    const auto& caps = vehicle::capsule_catalog();
    const vehicle::CapsulePart* cap =
        (vehicule_design.capsule >= 0 &&
         vehicule_design.capsule < static_cast<int>(caps.size()))
            ? &caps[static_cast<std::size_t>(vehicule_design.capsule)] : nullptr;
    publier_coupe(g_render_bridge.hull_design,
                  vehicle::build_hull(vehicule_design.stages, ergols, cap,
                                      vehicule_design.payload_kg));
  }
};

} // namespace fen::app
