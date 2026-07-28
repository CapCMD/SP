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
enum class Modal { Aucun = 0, GameOver, Reglages };

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

  void evaluer_plan() {
    if (mission::Mission* m = mission_courante()) {
      // Δv de trajectoire tiré de la géométrie RÉELLE de la fenêtre (Mars) ;
      // forfait par famille sinon. Rend le budget sensible à la fenêtre [7.3].
      mission_plan.dv_traj_override = mission::trajectory_dv_for_mission(
          *m, fen::Epoch{jeu.epoch_courant()}, jeu.eph);
      // ═══ P(NAVIGATION) N'EST PLUS 0,985 [GDD 7.5, 8.4] ═══
      // On évalue le vol QU'ON FERAIT EN PARTANT MAINTENANT : erreur d'injection
      // (Gates), amplifiée par Oberth, propagée par la matrice de transition de
      // l'arc jusqu'à l'arrivée, puis ramenée au Δv de correction que ce manque
      // au but exigerait. La probabilité de succès de navigation est celle que
      // la MARGE PROVISIONNÉE le couvre — donc une conséquence des choix de
      // conception, plus une constante.
      nav_disp = evaluer_navigation(*m);
      if (nav_disp.ok) mission_plan.p_physics = nav_disp.p_marge;
      mission_plan.evaluate(*m);
    }
  }

  // Le vol prospectif : « si on lançait maintenant ». C'est exactement ce qu'une
  // évaluation de conception doit juger — le plan n'est pas encore parti.
  mission::NavDispersion evaluer_navigation(const mission::Mission& m) {
    mission::Mission prospect = m;
    prospect.state = mission::MissionState::Launched;
    prospect.state_entered_days = jeu.ares.etat->clock.now_days();
    const double epoch = jeu.epoch_courant();
    prospect.tof_days =
        mission::transfer_tof_days(prospect, fen::Epoch{epoch}, jeu.eph);
    if (!mission::flight_has_arc(prospect)) return {};
    const mission::FlightTrace tr = mission::build_flight_trace(
        prospect, prospect.state_entered_days, epoch, jeu.eph);
    if (!tr.ok) return {};
    // Vitesse du corps quitté à la date de départ : c'est par rapport à ELLE que
    // se mesure le v∞ que l'injection doit fournir.
    const double t_dep_tdb =
        epoch + (tr.nodes[0].t_days - prospect.state_entered_days) * cst::DAY;
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
    const double t_dep_tdb = epoch + (c.tr.nodes[0].t_days - now_days) * cst::DAY;
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
    c.arc_jours = arc_poursuite_disponible(m, c.tr.nodes[0].t_days, date);
    c.sol = mission::nav_solution(c.tr, c.d, c.r, c.arc_jours,
                                  t_dep_tdb, jeu.eph, graine);
    if (!c.sol.ok) return c;
    c.t_arr_days = c.tr.nodes[1].t_days;
    const auto K = astro::kepler_propagate(
        c.tr.r_dep, c.tr.v_dep,
        (c.t_arr_days - c.tr.nodes[0].t_days) * cst::DAY, cst::MU_SUN);
    if (!K.converged) return c;
    c.cible = K.r;
    c.ok = true;
    return c;
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
    m.vol_vrai_t_days = c.tr.nodes[0].t_days;
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
      const ContexteVol c1 = contexte_vol(m, c.tr.nodes[0].t_days + c.d.t_tcm_days);
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
        (trace_vol.nodes[1].t_days - trace_vol.nodes[0].t_days) * cst::DAY, cst::MU_SUN);
    if (!Kc.converged) return {};
    mission::VueNavigation vn = mission::vue_navigation(
        trace_vol.r_dep, trace_vol.v_dep,
        Vec3{m.nav_connu_dv[0], m.nav_connu_dv[1], m.nav_connu_dv[2]}, Kc.r,
        trace_vol.nodes[0].t_days, jeu.ares.etat->clock.now_days(),
        trace_vol.nodes[1].t_days, m.nav_sigma_r, m.nav_sigma_v);
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
        (trace_vol.nodes[1].t_days - trace_vol.nodes[0].t_days) * cst::DAY, cst::MU_SUN);
    if (Kc.converged) {
      mission::EtatVol e2 = e;
      m.nav_miss_km = mission::manque_reel_km(e2, Kc.r, trace_vol.nodes[1].t_days);
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
      const mission::GateResult wg = mission::launch_window_gate(
          *m, fen::Epoch{jeu.epoch_courant()}, jeu.eph);
      if (!wg.allowed) return wg;
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
      // LA DURÉE DE TRANSIT SE FIGE ICI, et nulle part ailleurs : c'est la
      // géométrie du ciel AU DÉCOLLAGE qui date l'arrivée. Recalculée en route,
      // elle ferait glisser l'arrivée d'un vol déjà parti [GDD 7.3].
      m->tof_days = mission::transfer_tof_days(
          *m, fen::Epoch{jeu.epoch_courant()}, jeu.eph);
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
      mission_outcome_pret = true;
    }

    // DÉBRIEF : triple lecture appliquée à TOUS les systèmes [GDD 10.4].
    if (target == St::Completed || target == St::Failed) {
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
    auto invalider = [&] { B.vehicle.valid = false; trace_valide = false; };
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
            epoch + (trace_vol.nodes[0].t_days - now_days) * cst::DAY;
        const Vec3 v_terre = jeu.eph.state(ephem::Body::EarthBary, ephem::Body::Sun,
                                           fen::Epoch{t_dep_tdb}).v;
        trace_disp = mission::nav_dispersion(trace_vol, v_terre,
                                             mission_plan.program.dv_margin);
        B.vehicle.gen.fetch_add(1);
      }
    } else {
      mission::trace_avancer(trace_vol, now_days);
    }
    if (!trace_vol.ok) { B.vehicle.valid = false; return; }
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
    B.vehicle.corridor_3s_m = trace_vol.corridor_3s_m;
    B.vehicle.n_nodes = trace_vol.n_nodes;
    for (int k = 0; k < trace_vol.n_nodes && k < 2; ++k) {
      B.vehicle.nodes_m[k][0] = trace_vol.nodes[k].pos.x;
      B.vehicle.nodes_m[k][1] = trace_vol.nodes[k].pos.y;
      B.vehicle.nodes_m[k][2] = trace_vol.nodes[k].pos.z;
      B.vehicle.node_done[k] = trace_vol.nodes[k].done;
    }
    B.vehicle.valid = true;
  }
};

} // namespace fen::app
