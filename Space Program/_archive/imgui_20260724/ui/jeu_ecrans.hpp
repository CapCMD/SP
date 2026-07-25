// ui/jeu_ecrans.hpp - LES ECRANS v0.5. ZERO PHYSIQUE : tout est lu dans app::Jeu.
// Style : salle de vol d'ingenieur (jauges, telemetrie, vue 3D filaire) + memos
// ouvrables qui montrent les calculs. Accessibilite : mode Cadet + assistant.
#pragma once
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include "imgui.h"
#include "implot.h"
#include "app/jeu.hpp"
#include "ui/ares_ecrans.hpp"
#include "ui/carte3d_ecran.hpp"
#include "ui/hud.hpp"
#include "ui/panels.hpp"
#include "ui/station_ecran.hpp"

namespace fen::ui {

// `Station` = L'ACCUEIL (à bord de l'ISS, module Novellus) ; `Carte3D` = la
// carte du système solaire, atteinte depuis la station par [M].
enum class Ecran { Titre, Creation, Reglages, Systeme, Bureau, Contrats, Gestion,
                   Programme, Vol, Carte, VolInterp, Etude, Ares, Carte3D,
                   Station, GameOver };

// RESPONSIVE : toutes les dimensions fixes passent par em() = multiples de la
// taille de police courante. Changer l'echelle de l'UI redimensionne TOUT.
inline float em(float k) { return k * ImGui::GetFontSize(); }

struct Interface {
  app::Jeu jeu;
  Ecran ecran{Ecran::Titre};
  Ecran ecran_retour{Ecran::Titre};   // d'ou on a ouvert les reglages
  bool quitter{false};
  int mode_choix{0};
  char nom_buf[64] = "";
  char inj_buf[32] = "", comb_buf[32] = "";
  char wiz_buf[6][24] = {};
  float man_dt{600}, man_r{0}, man_s{0}, man_w{0};
  int mc_n{40};
  float vente_gbit{10}, vente_kg{2};
  Board corridor;
  Vue3D vue;
  std::string memo_titre, memo_texte;
  bool memo_ouvert{false};
  double sel_dep{0}, sel_tof{0};
  int planete_sel{-1};                 // carte systeme : corps selectionne
  int poste_ouvert{-1};                // ISS : poste de travail ouvert (-1 = aucun)
  std::string chemin_sauvegarde{"agence.sauvegarde.txt"};

  // --- sauvegardes multiples (ecran titre) : une partie = un fichier .sav ----
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
    if (std::filesystem::exists(chemin_sauvegarde, ec))
      ajouter(chemin_sauvegarde);          // partie historique (fichier unique)
    save_sel = 0;
    saves_scannees = true;
  }
  bool charger_partie(const std::string& chemin) {
    if (!jeu.charger(chemin)) return false;
    chemin_sauvegarde = chemin;            // la partie chargee devient l'active
    jeu.ares.assurer(jeu.agence, jeu.epoch_courant());
    jeu.ares.charger(chemin + ".ares");
    return true;
  }

  // --- reglages d'affichage : main les lit chaque frame et applique ---
  int res_choix{2};                    // index dans RES[]
  bool plein_ecran{false};
  bool appliquer_affichage{false};     // main remet a false apres application
  static constexpr int NB_RES = 5;
  int res_w() const { static const int W[NB_RES]={1280,1360,1600,1920,2560}; return W[res_choix]; }
  int res_h() const { static const int H[NB_RES]={720,880,900,1080,1440}; return H[res_choix]; }
  // --- echelle de l'interface (lisibilite 4K) : main reconstruit la police ---
  int scale_choix{1};
  static constexpr int NB_SCALE = 6;
  float ui_scale() const { static const float S[NB_SCALE]={0.85f,1.0f,1.25f,1.5f,1.75f,2.0f}; return S[scale_choix]; }
  bool appliquer_scale{false};

  bool forcer_vab{false};              // capture : selectionne l'onglet VAB une fois
  // --- tutoriel : un personnage guide + machine a ecrire ---
  bool guide_ouvert{true};
  double dt_courant{0.016};
  int tuto_beat_affiche{-1};
  float tuto_reveal{0};

  // -------------------------------------------------------------------------
  void ouvrir_memo(const std::string& t, const std::string& c) { memo_titre = t; memo_texte = c; memo_ouvert = true; }
  void bouton_memo(const char* label, const std::string& t, const std::string& c) {
    if (ImGui::Button(label)) ouvrir_memo(t, c);
  }
  void dessiner_memo() {
    if (!memo_ouvert) return;
    ImGui::OpenPopup("MEMO");
    const ImVec2 disp = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSize(ImVec2(std::min(em(54), disp.x*0.92f), std::min(em(41), disp.y*0.88f)),
                             ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImVec2(disp.x*0.5f, disp.y*0.5f), ImGuiCond_Appearing, ImVec2(0.5f,0.5f));
    if (ImGui::BeginPopupModal("MEMO", &memo_ouvert)) {
      ImGui::SetWindowFontScale(1.2f); ImGui::TextUnformatted(memo_titre.c_str());
      ImGui::SetWindowFontScale(1.0f); ImGui::Separator();
      ImGui::BeginChild("mt", ImVec2(-1, -40), false,
                        ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_AlwaysVerticalScrollbar);
      ImGui::TextUnformatted(memo_texte.c_str());   // memos pre-formates (alignes) : pas de wrap
      ImGui::EndChild();
      if (ImGui::Button("Fermer", ImVec2(-1, 30))) { memo_ouvert = false; ImGui::CloseCurrentPopup(); }
      ImGui::EndPopup();
    }
  }
  static void centre(const char* t, float s = 1.0f) {
    float w = ImGui::GetWindowSize().x, tw = ImGui::CalcTextSize(t).x * s;
    ImGui::SetCursorPosX((w - tw) * 0.5f);
    if (s != 1.0f) { ImGui::SetWindowFontScale(s); ImGui::TextUnformatted(t); ImGui::SetWindowFontScale(1.0f); }
    else ImGui::TextUnformatted(t);
  }

  // -------------------------------------------------------------------------
  void dessiner(float w, float h, double dt_reel) {
    dt_courant = dt_reel;
    // couche ARES : creation/reset/rattrapage mensuel (lecture seule sur l'agence)
    jeu.ares.assurer(jeu.agence, jeu.epoch_courant());
    // ECRANS v0.6 DEBRANCHES [plan ARES jalon A] : la carte du systeme solaire
    // EST le jeu. Les ecrans du prototype 2D (bureau/contrats/vol/...) restent
    // dans le code mais ne sont plus atteignables ; toute navigation residuelle
    // retombe sur la carte. (Resolu AVANT le pont : le monde UE doit savoir des
    // cette frame s'il dessine la carte.)
    switch (ecran) {
      case Ecran::Titre: case Ecran::Creation: case Ecran::Reglages:
      case Ecran::GameOver: case Ecran::Carte3D: case Ecran::Station: break;
      default: ecran = Ecran::Station; break;   // l'accueil, c'est l'ISS
    }
    if (jeu.game_over && ecran != Ecran::GameOver && ecran != Ecran::Titre) ecran = Ecran::GameOver;
    if ((ecran == Ecran::Carte3D || ecran == Ecran::Station) && !jeu.agence.creee)
      ecran = Ecran::Titre;
    // pont rendu 3D : chaque subsystem UE s'active pour SA scene.
    app::g_render_bridge.carte3d_active = (ecran == Ecran::Carte3D);
    app::g_render_bridge.scene = static_cast<int>(
        ecran == Ecran::Carte3D ? app::SceneJeu::Carte
      : ecran == Ecran::Station ? app::SceneJeu::Station
                                : app::SceneJeu::Titre);
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
    ImGuiWindowFlags flags_jeu =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar;
    // SCENES 3D (station ISS, carte) : fond TRANSPARENT — sinon la fenetre
    // plein ecran d'ImGui peint par-dessus le monde UE et tout est noir.
    if (ecran == Ecran::Carte3D || ecran == Ecran::Station)
      flags_jeu |= ImGuiWindowFlags_NoBackground;
    ImGui::Begin("##jeu", nullptr, flags_jeu);
    switch (ecran) {
      case Ecran::GameOver:  e_game_over(); break;
      case Ecran::Titre:     e_titre();     break;
      case Ecran::Creation:  e_creation();  break;
      case Ecran::Reglages:  e_reglages();  break;
      case Ecran::Station:
        switch (ecran_station(jeu, poste_ouvert)) {
          case ActionStation::Carte: ecran = Ecran::Carte3D; break;
          case ActionStation::Sauver:
            jeu.sauvegarder(chemin_sauvegarde);
            jeu.ares.sauvegarder(chemin_sauvegarde + ".ares");
            saves_scannees = false;
            break;
          case ActionStation::Menu:
            jeu.sauvegarder(chemin_sauvegarde);
            jeu.ares.sauvegarder(chemin_sauvegarde + ".ares");
            saves_scannees = false;
            ecran = Ecran::Titre;
            break;
          case ActionStation::Rien: break;
        }
        break;
      case Ecran::Carte3D:
        switch (ecran_carte3d(jeu, dt_reel)) {
          case ActionCarte::Reglages:
            ecran_retour = Ecran::Carte3D; ecran = Ecran::Reglages; break;
          case ActionCarte::Sauver:
            jeu.sauvegarder(chemin_sauvegarde);
            jeu.ares.sauvegarder(chemin_sauvegarde + ".ares");
            saves_scannees = false;        // le titre re-scannera
            break;
          case ActionCarte::Menu:          // quitter la partie : sauver puis titre
            jeu.sauvegarder(chemin_sauvegarde);
            jeu.ares.sauvegarder(chemin_sauvegarde + ".ares");
            saves_scannees = false;
            ecran = Ecran::Titre;
            break;
          case ActionCarte::Station: ecran = Ecran::Station; break;
          case ActionCarte::Rien: break;
        }
        break;
      default: break;   // ecrans v0.6 : jamais atteints (rerouted ci-dessus)
    }
    if (!jeu.erreur.empty()) {
      ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 26);
      ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.25f, 1), "! %s", jeu.erreur.c_str());
    }
    dessiner_memo();
    ImGui::End();
    if (!jeu.mc_en_cours && jeu.mc_resultat >= 0) jeu.encaisser_mc();
  }

  // -------------------------------------------------------------------------
  void barre_haut() {
    const auto& A = jeu.agence;
    ImGui::Text("%s", A.nom.c_str());
    ImGui::SameLine(0, 20);
    ImGui::TextColored(A.tresorerie < 0 ? ImVec4(0.95f, 0.35f, 0.3f, 1) : ImVec4(0.55f, 0.85f, 0.55f, 1),
                       "%.1f M$", A.tresorerie);
    ImGui::SameLine(0, 18); ImGui::Text("T+%.1f mois", A.mois);
    ImGui::SameLine(0, 18); ImGui::Text("confiance %.0f%%", 100 * A.confiance);
    ImGui::SameLine(0, 18); ImGui::TextDisabled("%d OK / %d KO", A.reussites, A.echecs);
    ImGui::SameLine(0, 18);
    if (jeu.donnees_gbit > 0) { ImGui::TextColored(ImVec4(0.6f,0.7f,0.95f,1), "%.0f Gbit", jeu.donnees_gbit); ImGui::SameLine(0,18); }
    if (jeu.echantillons_kg > 0) { ImGui::TextColored(ImVec4(0.8f,0.7f,0.5f,1), "%.1f kg", jeu.echantillons_kg); ImGui::SameLine(0,18); }
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 340);
    if (jeu.tuto.actif && ImGui::SmallButton(guide_ouvert?"Guide: ON":"Guide: OFF")) guide_ouvert = !guide_ouvert;
    ImGui::SameLine();
    if (ImGui::SmallButton("Reglages")) { ecran_retour = ecran; ecran = Ecran::Reglages; }
    ImGui::SameLine();
    if (ImGui::SmallButton("Passer 1 mois")) jeu.passer_mois();
    ImGui::SameLine();
    if (ImGui::SmallButton("Sauver")) {
      jeu.sauvegarder(chemin_sauvegarde);
      jeu.ares.sauvegarder(chemin_sauvegarde + ".ares");   // couche GDD (binaire)
    }
    ImGui::Separator();
    auto onglet = [&](const char* nom, Ecran e) {
      const bool a = (ecran == e);
      if (a) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.26f, 0.42f, 0.66f, 1));
      if (ImGui::Button(nom, ImVec2(0, 28))) { ecran = e; jeu.erreur.clear(); }
      if (a) ImGui::PopStyleColor();
      ImGui::SameLine();
    };
    onglet("SYSTEME", Ecran::Systeme);
    onglet("BUREAU", Ecran::Bureau);
    onglet("CONTRATS", Ecran::Contrats);
    onglet("GESTION", Ecran::Gestion);
    onglet("ARES", Ecran::Ares);
    onglet("CARTE 3D", Ecran::Carte3D);
    const auto* ac = jeu.actif();
    if (ac && ac->type == app::TypeContrat::VolGeo) { onglet("PROGRAMME", Ecran::Programme); onglet("SALLE DE VOL", Ecran::Vol); }
    if (ac && ac->type != app::TypeContrat::VolGeo && ac->type != app::TypeContrat::EtudeMars) {
      onglet("CARTE", Ecran::Carte); onglet("SALLE DE VOL", Ecran::VolInterp); }
    onglet("ETUDE", Ecran::Etude);
    ImGui::NewLine(); ImGui::Separator();
  }

  // -------------------------------------------------------------------------
  void e_titre() {
    if (!saves_scannees) scanner_sauvegardes();
    ImGui::Dummy(ImVec2(0, 60)); centre("ARES", 3.8f);
    ImGui::Dummy(ImVec2(0, 6));
    centre("simulateur d'architecture de mission spatiale");
    centre("le joueur concoit, le monde propage, la physique tranche");
    ImGui::Dummy(ImVec2(0, 36));
    const float bw = 420, bh = 44;
    const float x = (ImGui::GetWindowSize().x - bw) * 0.5f;
    ImGui::SetCursorPosX(x);
    if (ImGui::Button("FONDER UNE AGENCE SPATIALE", ImVec2(bw, bh))) ecran = Ecran::Creation;
    // --- REPRENDRE : une partie = une sauvegarde, liste scannee sur disque ----
    ImGui::Dummy(ImVec2(0, 12));
    ImGui::SetCursorPosX(x);
    ImGui::TextUnformatted("REPRENDRE :");
    if (saves_listees.empty()) {
      ImGui::SetCursorPosX(x);
      ImGui::TextDisabled("  (aucune sauvegarde)");
    } else {
      ImGui::SetCursorPosX(x);
      ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.04f, 0.05f, 0.08f, 1));
      ImGui::BeginChild("##saves", ImVec2(bw, std::min(4.5f, (float)saves_listees.size() + 0.5f) * em(1.6f)),
                        ImGuiChildFlags_Border);
      for (int i = 0; i < (int)saves_listees.size(); ++i)
        if (ImGui::Selectable(saves_listees[(size_t)i].label.c_str(), save_sel == i))
          save_sel = i;
      ImGui::EndChild();
      ImGui::PopStyleColor();
      ImGui::SetCursorPosX(x);
      if (ImGui::Button("CHARGER LA PARTIE SELECTIONNEE", ImVec2(bw, 34))) {
        if (save_sel >= 0 && save_sel < (int)saves_listees.size() &&
            charger_partie(saves_listees[(size_t)save_sel].chemin))
          ecran = Ecran::Station;              // on reprend À BORD, pas sur la carte
        else jeu.erreur = "Sauvegarde illisible.";
      }
    }
    ImGui::Dummy(ImVec2(0, 12)); ImGui::SetCursorPosX(x);
    if (ImGui::Button("REGLAGES (resolution, plein ecran)", ImVec2(bw, bh))) { ecran_retour = Ecran::Titre; ecran = Ecran::Reglages; }
    ImGui::Dummy(ImVec2(0, 10)); ImGui::SetCursorPosX(x);
    if (ImGui::Button("Quitter", ImVec2(bw, bh))) quitter = true;
    ImGui::Dummy(ImVec2(0, 40));
    centre("v0.7 - la carte du systeme solaire EST le jeu [GDD 8.3] ;");
    centre("etat du monde synchronise sur l'instant reel a la fondation [GDD 14.1]");
    centre("185 oracles au vert - aucun chiffre invente a l'ecran");
  }

  // -------------------------------------------------------------------------
  void e_reglages() {
    ImGui::Dummy(ImVec2(0, 40)); centre("REGLAGES", 2.2f); ImGui::Dummy(ImVec2(0, 20));
    const float x = ImGui::GetWindowSize().x * 0.5f - 240;
    ImGui::SetCursorPosX(x); ImGui::TextUnformatted("TAILLE DE L'INTERFACE (pour les grands ecrans / 4K) :");
    const char* sc[NB_SCALE] = {"85 %","100 %","125 %","150 % (recommande en 4K)","175 %","200 %"};
    for (int i = 0; i < NB_SCALE; ++i) { ImGui::SetCursorPosX(x+12); ImGui::RadioButton(sc[i], &scale_choix, i); }
    ImGui::Dummy(ImVec2(0, 12)); ImGui::SetCursorPosX(x);
    ImGui::TextUnformatted("RESOLUTION DE LA FENETRE :");
    const char* res[NB_RES] = {"1280 x 720","1360 x 880","1600 x 900","1920 x 1080","2560 x 1440"};
    for (int i = 0; i < NB_RES; ++i) { ImGui::SetCursorPosX(x+12); ImGui::RadioButton(res[i], &res_choix, i); }
    ImGui::Dummy(ImVec2(0, 8)); ImGui::SetCursorPosX(x+12);
    ImGui::Checkbox("plein ecran (recommande en 4K, avec taille 150-200 %)", &plein_ecran);
    ImGui::Dummy(ImVec2(0, 16)); ImGui::SetCursorPosX(x);
    if (ImGui::Button("APPLIQUER", ImVec2(220, 40))) { appliquer_affichage = true; appliquer_scale = true; }
    ImGui::SameLine();
    if (ImGui::Button("Retour", ImVec2(220, 40))) ecran = ecran_retour;
    ImGui::Dummy(ImVec2(0, 16)); ImGui::SetCursorPosX(x);
    ImGui::PushTextWrapPos(x + 480);
    ImGui::TextDisabled("%s", "La TAILLE agrandit tout le texte et les boutons (l'echelle de l'UI). "
                        "La RESOLUTION change la taille de la fenetre. Sur un 4K : plein ecran + taille 150-200 %.");
    ImGui::PopTextWrapPos();
  }

  // -------------------------------------------------------------------------
  // GAME OVER : la faillite est claire, motivee, et definitive pour cette agence.
  // -------------------------------------------------------------------------
  void e_game_over() {
    const auto& A = jeu.agence;
    ImGui::Dummy(ImVec2(0, em(4)));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.30f, 0.25f, 1));
    centre("FAILLITE", 3.2f);
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, em(0.6f)));
    centre("l'agence est dissoute - fin de partie");
    ImGui::Dummy(ImVec2(0, em(1.5f)));
    const float x = ImGui::GetWindowSize().x * 0.5f - em(20);
    ImGui::SetCursorPosX(x);
    ImGui::PushTextWrapPos(x + em(40));
    ImGui::TextColored(ImVec4(0.9f,0.85f,0.7f,1), "POURQUOI :");
    ImGui::SetCursorPosX(x);
    ImGui::TextWrapped("%s", jeu.raison_faillite.c_str());
    ImGui::PopTextWrapPos();
    ImGui::Dummy(ImVec2(0, em(1)));
    ImGui::SetCursorPosX(x);
    ImGui::Text("Bilan de %s : %d mission(s) reussie(s), %d perdue(s), %.0f mois d'existence.",
                A.nom.c_str(), A.reussites, A.echecs, A.mois);
    ImGui::SetCursorPosX(x);
    ImGui::TextDisabled("Conseil : garde toujours de quoi payer les charges du mois (salaires,");
    ImGui::SetCursorPosX(x);
    ImGui::TextDisabled("entretien, flotte). Le marche (GESTION) transforme la science en tresorerie.");
    ImGui::Dummy(ImVec2(0, em(1.5f)));
    ImGui::SetCursorPosX(x);
    if (ImGui::Button("NOUVELLE PARTIE", ImVec2(em(19), em(2.6f)))) {
      jeu.reinitialiser(); ecran = Ecran::Titre;
    }
    ImGui::SameLine();
    if (ImGui::Button("CHARGER LA SAUVEGARDE", ImVec2(em(19), em(2.6f)))) {
      jeu.reinitialiser();
      if (charger_partie(chemin_sauvegarde)) ecran = Ecran::Carte3D; else ecran = Ecran::Titre;
    }
    ImGui::SetCursorPosX(x);
    if (ImGui::Button("Quitter", ImVec2(em(19), em(2)))) quitter = true;
  }

  // -------------------------------------------------------------------------
  // LE TUTORIEL : Iris, directrice de vol, guide la premiere mission GEO.
  // -------------------------------------------------------------------------
  static const char* beat_tuto(int e) {
    static const char* B[] = {
      "Bienvenue a la direction ! Je suis Iris, ta directrice de vol. Je vais te "
      "guider pour ta toute premiere mission : placer un satellite en orbite "
      "geostationnaire. Clique SUIVANT quand tu es pret.",
      "Etape 1 - LE CONTRAT. Va dans l'onglet CONTRATS (en haut) et signe "
      "\"M00 - GEO-SAT 1\". Le budget t'est verse a la signature ; ce que tu ne "
      "depenses pas, tu le gardes.",
      "Etape 2 - LES CALCULS. Va dans PROGRAMME, onglet 1. Le cahier des charges ne "
      "donne AUCUN Delta-v : a toi de les deriver. Debutant ? Utilise l'ASSISTANT "
      "(partie B) : clique 'reveler' sur chaque etape (c'est gratuit en mode Normal).",
      "Etape 3 - LE VEHICULE. Onglet 2 : choisis le moteur RL10 (meilleur Isp) et un "
      "niveau de POURSUITE (ta NAVIGATION : sans elle tu ignores ou est ta sonde). Ce "
      "PREMIER vol est une REPETITION, execution au nominal - comme un vrai centre de "
      "controle repete en simulateur avant un tir. Si tes calculs et tes manoeuvres "
      "sont justes, tu reussis. La tolerance la plus serree est l'inclinaison (0,25 deg).",
      "Etape 4 - LE FEU VERT. Onglet 3 : le BILAN. Tout en vert = c'est bon. Onglet 4 : "
      "COMMIT. Attention, c'est IRREVERSIBLE : une graine du hasard est tiree et gelee.",
      "Etape 5 - LE VOL. Va en SALLE DE VOL et clique LANCER. Regarde le decollage. "
      "A l'apogee, clique OBSERVER, puis fais CALCULER la manoeuvre par la division "
      "analyse, puis EXECUTER. C'est ta premiere insertion.",
      "Etape 6 - LA BOUCLE. Recommence OBSERVER -> CALCULER -> EXECUTER pour les "
      "manoeuvres AMF2 puis TRIM. Entre chaque, clique AVANCER. Tu peux accelerer le "
      "temps avec le curseur WARP.",
      "Etape 7 - LE VERDICT. Le post-mortem t'explique POURQUOI tu reussis ou rates, "
      "critere par critere (altitude, forme, inclinaison). Ce vol etait une REPETITION ; "
      "des M00b, l'erreur d'execution reelle entre en jeu - c'est la que tu apprendras a "
      "BUDGETER la marge et a acheter la bonne poursuite. Bonne PHYSIQUE !",
    };
    const int n = (int)(sizeof(B)/sizeof(B[0]));
    return B[e < 0 ? 0 : e >= n ? n-1 : e];
  }
  void dessiner_guide() {
    const int nbeats = 8;
    const char* txt = beat_tuto(jeu.tuto.etape);
    const int len = (int)std::strlen(txt);
    // machine a ecrire : on revele le texte caractere par caractere
    if (jeu.tuto.etape != tuto_beat_affiche) { tuto_beat_affiche = jeu.tuto.etape; tuto_reveal = 0; }
    tuto_reveal += (float)dt_courant * 55.0f;
    if (dt_courant <= 0.0) tuto_reveal = (float)len;   // capture : texte complet
    if (tuto_reveal > (float)len) tuto_reveal = (float)len;
    const bool complet = tuto_reveal >= (float)len;
    std::string montre(txt, (size_t)std::min((float)len, tuto_reveal));

    // fenetre de dialogue EN HAUT (top-level : ses boutons sont cliquables au-dessus
    // des cartes ImPlot, contrairement a un simple dessin dans la fenetre de fond).
    const ImVec2 disp = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(disp.x * 0.5f, 84), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(std::min(940.0f, disp.x - 40), 0), ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.10f,0.12f,0.18f,0.97f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.35f,0.55f,0.85f,1));
    ImGui::Begin("##guide", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 wp = ImGui::GetWindowPos();
    // avatar : casque stylise, dessine a gauche
    ImVec2 c(wp.x + 42, wp.y + 44);
    dl->AddCircleFilled(c, 30, col(0.2f,0.3f,0.5f,1));
    dl->AddCircleFilled(c, 25, col(0.85f,0.88f,0.95f,1));
    dl->AddCircleFilled(ImVec2(c.x, c.y+3), 16, col(0.15f,0.2f,0.3f,1));
    dl->AddCircle(c, 30, col(0.5f,0.7f,1.0f,1), 32, 2);
    ImGui::Indent(80);
    ImGui::TextColored(ImVec4(0.7f,0.82f,1.0f,1), "IRIS, directrice de vol   (etape %d / %d)", jeu.tuto.etape+1, nbeats);
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted(montre.c_str());
    if (!complet) { ImGui::SameLine(0,0); ImGui::TextColored(ImVec4(0.6f,0.7f,0.9f,1), "_"); }
    ImGui::PopTextWrapPos();
    ImGui::Dummy(ImVec2(0,6));
    if (ImGui::Button("< Precedent") && jeu.tuto.etape > 0) { jeu.tuto.etape--; }
    ImGui::SameLine();
    if (!complet) { if (ImGui::Button("Tout afficher")) tuto_reveal = (float)len; }
    else if (jeu.tuto.etape < nbeats-1) { if (ImGui::Button("Suivant >")) jeu.tuto.etape++; }
    else { if (ImGui::Button("Terminer le tutoriel")) jeu.tuto.actif = false; }
    ImGui::SameLine();
    if (ImGui::Button("Fermer le guide")) guide_ouvert = false;
    ImGui::Unindent(80);
    ImGui::End();
    ImGui::PopStyleColor(2);
  }

  // -------------------------------------------------------------------------
  // LA CARTE DU SYSTEME SOLAIRE : planetes (ephemeride), actifs de l'agence,
  // revenus. Cliquable : clic sur la Terre -> ta base (gestion).
  // -------------------------------------------------------------------------
  // CARTE DU SYSTEME : 6 planetes + LUNES (Lune, Titan - ephemeride reelle).
  // Zoom a la molette ; les lunes deviennent visibles/cliquables en zoomant.
  // Clic = FICHE temps reel (distances, vitesse, rayon, mu, periode, actifs).
  void e_systeme() {
    struct CorpsUi { const char* nom; ephem::Body b; double sma_ua; int parent; };  // parent -1 = Soleil
    static const CorpsUi C[8] = {
      {"Mercure", ephem::Body::Mercury,  0.387, -1},
      {"Venus",   ephem::Body::Venus,    0.723, -1},
      {"Terre",   ephem::Body::EarthBary,1.0,   -1},
      {"Mars",    ephem::Body::Mars,     1.524, -1},
      {"Jupiter", ephem::Body::Jupiter,  5.203, -1},
      {"Saturne", ephem::Body::Saturn,   9.537, -1},
      {"Lune",    ephem::Body::Moon,     1.0,    2},
      {"Titan",   ephem::Body::Titan,    9.537,  5},
    };
    const int NC = 8;
    const double t = jeu.epoch_courant();
    double px[NC], py[NC]; Vec3 rh[NC], vh[NC];
    for (int i = 0; i < NC; ++i) {
      if (C[i].b == ephem::Body::Titan) {
        // Titan n'est pas tabule par Standish : orbite keplerienne CIRCULAIRE
        // autour de Saturne (a = 1 221 870 km, T = 15,945 j) - modele declare.
        const double a_t = 1.22187e9, T_t = 15.945 * 86400.0;
        const double ang = 2 * 3.14159265358979 * std::fmod(t / T_t, 1.0);
        const double v_t = 2 * 3.14159265358979 * a_t / T_t;
        rh[i] = rh[5] + Vec3{a_t * std::cos(ang), a_t * std::sin(ang), 0};
        vh[i] = vh[5] + Vec3{-v_t * std::sin(ang), v_t * std::cos(ang), 0};
      } else {
        auto pv = jeu.eph.state(C[i].b, ephem::Body::Sun, Epoch{t});   // Lune : geree par l'ephemeride
        rh[i] = pv.r; vh[i] = pv.v;
      }
      px[i] = rh[i].x / cst::AU; py[i] = rh[i].y / cst::AU;
    }
    ImGui::BeginChild("map", ImVec2(ImGui::GetWindowWidth()*0.66f, -8), true);
    ImGui::TextUnformatted("SYSTEME SOLAIRE (positions reelles a la date de l'agence)");
    ImGui::TextDisabled("molette = ZOOM (les lunes apparaissent) ; clic = fiche ; double-clic Terre = base");
    double span = 21;
    if (ImPlot::BeginPlot("##sys", ImVec2(-1,-1), ImPlotFlags_Equal|ImPlotFlags_NoLegend)) {
      ImPlot::SetupAxes("x [UA]","y [UA]");
      ImPlot::SetupAxesLimits(-10.5,10.5,-10.5,10.5, ImPlotCond_Once);
      const ImPlotRect lim = ImPlot::GetPlotLimits();
      span = lim.X.Max - lim.X.Min;
      const bool zoom_lunes = span < 1.2;     // assez proche pour resoudre les lunes
      const int N=140; static std::vector<double> ox(N+1), oy(N+1);
      for (int i = 0; i < 6; ++i) {
        for (int k=0;k<=N;++k){ double a=2*3.14159265*k/N; ox[k]=C[i].sma_ua*cos(a); oy[k]=C[i].sma_ua*sin(a); }
        ImPlot::SetNextLineStyle(ImVec4(0.4f,0.45f,0.55f,0.5f),1.0f);
        ImPlot::PlotLine(C[i].nom, ox.data(), oy.data(), N+1);
      }
      double sx[1]={0}, sy[1]={0};
      ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 10, ImVec4(0.98f,0.85f,0.3f,1));
      ImPlot::PlotScatter("Soleil", sx, sy, 1);
      for (int i = 0; i < NC; ++i) {
        const bool lune = C[i].parent >= 0;
        if (lune && !zoom_lunes) continue;    // invisibles a l'echelle du systeme
        double mx[1]={px[i]}, my[1]={py[i]};
        const bool base = (i==2);
        ImPlot::SetNextMarkerStyle(lune?ImPlotMarker_Diamond:ImPlotMarker_Circle,
          lune?5.f:(base?9.f:7.f),
          lune?ImVec4(0.8f,0.8f,0.6f,1):(base?ImVec4(0.4f,0.7f,1.0f,1):ImVec4(0.75f,0.72f,0.66f,1)));
        ImPlot::PlotScatter(C[i].nom, mx, my, 1);
        ImPlot::Annotation(px[i], py[i], ImVec4(0.8f,0.85f,0.9f,1), ImVec2(6,-6), false, "%s", C[i].nom);
        if (lune) {  // l'orbite de la lune autour de son parent
          const int P = C[i].parent;
          const double r_orb = std::hypot(px[i]-px[P], py[i]-py[P]);
          for (int k=0;k<=N;++k){ double a=2*3.14159265*k/N; ox[k]=px[P]+r_orb*cos(a); oy[k]=py[P]+r_orb*sin(a); }
          ImPlot::SetNextLineStyle(ImVec4(0.6f,0.6f,0.5f,0.5f),1.0f);
          ImPlot::PlotLine("##orb_lune", ox.data(), oy.data(), N+1);
        }
      }
      if (jeu.relais_geo>0) ImPlot::Annotation(px[2], py[2], ImVec4(0.4f,0.9f,0.5f,1), ImVec2(6,10), true, "%d relais", jeu.relais_geo);
      if (jeu.orbiteurs_mars>0) ImPlot::Annotation(px[3], py[3], ImVec4(0.4f,0.9f,0.5f,1), ImVec2(6,10), true, "%d orbiteur(s)", jeu.orbiteurs_mars);
      if (jeu.sondes_lointaines>0) ImPlot::Annotation(px[5], py[5], ImVec4(0.4f,0.9f,0.5f,1), ImVec2(6,10), true, "%d sonde(s)", jeu.sondes_lointaines);
      if (ImPlot::IsPlotHovered() && ImGui::IsMouseClicked(0)) {
        ImPlotPoint m = ImPlot::GetPlotMousePos();
        int best=-1; double bd=1e18;
        for (int i=0;i<NC;++i){
          if (C[i].parent>=0 && !zoom_lunes) continue;
          double d=std::hypot(px[i]-m.x, py[i]-m.y); if (d<bd){bd=d; best=i;} }
        if (best>=0 && bd < span*0.06) planete_sel = best;
      }
      if (ImPlot::IsPlotHovered() && ImGui::IsMouseDoubleClicked(0) && planete_sel==2) ecran = Ecran::Gestion;
      if (planete_sel>=0 && (C[planete_sel].parent<0 || zoom_lunes)) {
        double hx[1]={px[planete_sel]}, hy[1]={py[planete_sel]};
        ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 14, ImVec4(0,0,0,0), 2, ImVec4(1,1,0.5f,1));
        ImPlot::PlotScatter("##sel", hx, hy, 1);
      }
      ImPlot::EndPlot();
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("info", ImVec2(0, -8), true);
    ImGui::SetWindowFontScale(1.15f); ImGui::TextUnformatted("TON AGENCE"); ImGui::SetWindowFontScale(1.0f);
    ImGui::Separator();
    ImGui::Text("tresorerie     %.1f M$", jeu.agence.tresorerie);
    ImGui::Text("revenu science %.1f Gbit / mois", jeu.revenu_mensuel_gbit());
    ImGui::Text("stock : %.0f Gbit, %.1f kg", jeu.donnees_gbit, jeu.echantillons_kg);
    ImGui::Separator();
    ImGui::TextUnformatted("FLOTTE");
    ImGui::BulletText("Base : Terre");
    ImGui::BulletText("%d relais science (GEO)", jeu.relais_geo);
    ImGui::BulletText("%d orbiteur(s) de Mars", jeu.orbiteurs_mars);
    ImGui::BulletText("%d sonde(s) lointaine(s)", jeu.sondes_lointaines);
    ImGui::Separator();
    if (planete_sel >= 0 && planete_sel < NC) {
      const int i = planete_sel;
      // ------- LA FICHE : donnees TEMPS REEL calculees de l'ephemeride -------
      ImGui::SetWindowFontScale(1.2f); ImGui::Text("FICHE : %s", C[i].nom); ImGui::SetWindowFontScale(1.0f);
      ImGui::TextDisabled(C[i].parent<0 ? "planete" : "lune de %s", C[i].parent<0?"":C[C[i].parent].nom);
      ImGui::Separator();
      const double d_soleil = norm(rh[i]) / cst::AU;
      const double d_terre  = norm(rh[i] - rh[2]) / cst::AU;
      ImGui::Text("dist. Soleil   %10.3f UA", d_soleil);
      ImGui::Text("dist. Terre    %10.3f UA  (%.0f millions km)", d_terre, d_terre*149.6);
      ImGui::Text("vitesse helio  %10.2f km/s", norm(vh[i])/1000);
      ImGui::Text("rayon          %10.0f km", ephem::body_radius(C[i].b)/1000);
      ImGui::Text("mu             %10.3e m3/s2", ephem::body_mu(C[i].b));
      if (C[i].parent < 0)
        ImGui::Text("periode orbit. %10.1f ans", std::sqrt(C[i].sma_ua*C[i].sma_ua*C[i].sma_ua));
      else {
        const int P = C[i].parent;
        const double r_rel = norm(rh[i]-rh[P]);
        const double T_l = 2*3.14159265*std::sqrt(r_rel*r_rel*r_rel/ephem::body_mu(C[P].b))/86400.0;
        ImGui::Text("dist. parente  %10.0f mille km", r_rel/1e6);
        ImGui::Text("periode orbit. %10.1f jours", T_l);
      }
      const double aller = d_terre*149.6e9/3e8/60.0;
      ImGui::Text("lumiere A/R    %10.1f min", 2*aller);
      ImGui::Separator();
      if (i==2) {
        ImGui::TextWrapped("TA BASE. Conception, integration, lancements.");
        if (ImGui::Button("Ouvrir la GESTION (base)", ImVec2(-1, em(2)))) ecran = Ecran::Gestion;
        if (ImGui::Button("Voir les CONTRATS", ImVec2(-1, em(1.8f)))) ecran = Ecran::Contrats;
      } else if (i==6) ImGui::TextWrapped("La Lune : la banlieue de la base. (Missions lunaires : plus tard.)");
      else if (i==3) ImGui::TextWrapped("Mars : cible des orbiteurs. L'arrivee se joue dans le corridor du plan-B.");
      else if (i==5) ImGui::TextWrapped("Saturne : zoom pour voir TITAN, la cible exobiologique.");
      else if (i==7) ImGui::TextWrapped("TITAN : le graal. Survol sous 2000 km via assistances gravitationnelles.");
      else ImGui::TextWrapped("Corps utile aux assistances gravitationnelles (briser le mur du C3).");
    } else ImGui::TextDisabled("Clique un corps pour sa fiche.\nZoome (molette) pour voir les lunes.");
    ImGui::EndChild();
  }

  void e_creation() {
    ImGui::Dummy(ImVec2(0, 40)); centre("FONDER L'AGENCE", 2.2f); ImGui::Dummy(ImVec2(0, 16));
    const float x = ImGui::GetWindowSize().x * 0.5f - 260;
    ImGui::SetCursorPosX(x); ImGui::SetNextItemWidth(520);
    ImGui::InputTextWithHint("##nom", "nom de l'agence", nom_buf, sizeof(nom_buf));
    ImGui::Dummy(ImVec2(0, 14)); ImGui::SetCursorPosX(x);
    ImGui::TextUnformatted("DIFFICULTE (elle change le PRIX/la DISPONIBILITE de l'aide, JAMAIS la physique) :");
    const char* modes[2] = {
      "NORMAL - 45 M$, acces a l'assistant : il te guide dans les calculs.",
      "PRO - 32 M$, aucune aide : tu realises tous les calculs toi-meme."};
    for (int i = 0; i < 2; ++i) { ImGui::SetCursorPosX(x); ImGui::RadioButton(modes[i], &mode_choix, i); }
    ImGui::Dummy(ImVec2(0, 10)); ImGui::SetCursorPosX(x);
    ImGui::TextDisabled("A la fondation, l'etat du systeme solaire est fige sur");
    ImGui::SetCursorPosX(x);
    ImGui::TextDisabled("la date et l'heure REELLES [GDD 14.1] - chaque partie est unique.");
    ImGui::Dummy(ImVec2(0, 10)); ImGui::SetCursorPosX(x);
    if (ImGui::Button("FONDER", ImVec2(520, 42))) {
      jeu.creer_agence(nom_buf, (app::ModeAide)mode_choix);
      jeu.tuto.actif = false;   // tutoriel v0.6 debranche avec les ecrans 2D
      // une partie = une sauvegarde : <dossier>/<slug>.sav devient l'active
      chemin_sauvegarde = (dossier_saves() / (slug_agence(jeu.agence.nom) + ".sav")).string();
      saves_scannees = false;
      ecran = Ecran::Station;                  // ON EST ACCUEILLI À BORD DE L'ISS
      poste_ouvert = -1;
    }
  }

  // -------------------------------------------------------------------------
  void e_bureau() {
    ImGui::BeginChild("g", ImVec2(ImGui::GetWindowWidth() * 0.46f, -8), true);
    ImGui::SetWindowFontScale(1.25f); ImGui::TextUnformatted("LE BUREAU"); ImGui::SetWindowFontScale(1.0f);
    ImGui::Separator();
    const auto& A = jeu.agence;
    ImGui::Text("tresorerie   %10.1f M$", A.tresorerie);
    ImGui::Text("calendrier   T+%8.1f mois", A.mois);
    ImGui::Text("confiance    %10.0f %%", 100 * A.confiance);
    ImGui::Text("flotte       %d relais GEO, %d orbiteurs Mars", jeu.relais_geo, jeu.orbiteurs_mars);
    ImGui::Text("stock        %.0f Gbit de donnees, %.1f kg d'echantillons", jeu.donnees_gbit, jeu.echantillons_kg);
    ImGui::Separator();
    if (const auto* c = jeu.actif()) {
      ImGui::TextWrapped("Programme : %s - %s", c->id.c_str(), c->titre.c_str());
      if (c->termine) ImGui::TextColored(c->reussi?ImVec4(0.35f,0.85f,0.45f,1):ImVec4(0.95f,0.35f,0.3f,1),
                                         c->reussi?"LIVRE.":"PERDU (voir post-mortem).");
    } else ImGui::TextWrapped("Aucun programme en cours. Va signer un contrat.");
    ImGui::Separator();
    ImGui::TextWrapped("Boucle : CONTRAT -> conception (derive tes Delta-v) -> achats -> COMMIT "
      "(irreversible) -> vol en boucle fermee (temps reel) -> POST-MORTEM. Le TEMPS et l'ARGENT "
      "sont des ressources ; la GESTION (installations, recherche) change leurs prix, jamais la physique.");
    ImGui::Separator();
    ImGui::TextDisabled("Difficulte : %s",
      A.mode==app::ModeAide::Pro?"PRO":"NORMAL");
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("j", ImVec2(0, -8), true);
    ImGui::SetWindowFontScale(1.25f); ImGui::TextUnformatted("JOURNAL"); ImGui::SetWindowFontScale(1.0f);
    ImGui::Separator();
    for (auto it = jeu.agence.journal.rbegin(); it != jeu.agence.journal.rend(); ++it) {
      ImGui::TextDisabled("T+%5.1f", it->mois); ImGui::SameLine();
      ImGui::TextWrapped("%s", it->texte.c_str());
    }
    ImGui::EndChild();
  }

  // -------------------------------------------------------------------------
  void e_contrats() {
    ImGui::TextWrapped("Objectifs = grandeurs physiques + tolerances, jamais des etoiles. "
                       "GEO / Mars (corridor) / comete (Rosetta) / etude.");
    ImGui::Separator();
    ImGui::BeginChild("cl", ImVec2(-1, -8));
    for (int i = 0; i < (int)jeu.contrats.size(); ++i) {
      auto& c = jeu.contrats[i];
      if (c.termine && !c.accepte) continue;
      ImGui::PushID(i);
      ImGui::BeginChild("c", ImVec2(-1, 172), true);
      const char* badge = c.type==app::TypeContrat::VolMars?"[MARS]":c.type==app::TypeContrat::VolComete?"[COMETE]":
                          c.type==app::TypeContrat::EtudeMars?"[ETUDE]":"[GEO]";
      ImGui::SetWindowFontScale(1.25f);
      ImGui::Text("%s %s - %s", badge, c.id.c_str(), c.titre.c_str());
      ImGui::SetWindowFontScale(1.0f);
      ImGui::TextDisabled("client : %s", c.client.c_str());
      ImGui::PushTextWrapPos(0); ImGui::TextUnformatted(c.resume.c_str()); ImGui::PopTextWrapPos();
      if (c.type != app::TypeContrat::EtudeMars)
        ImGui::Text("charge %g kg | budget %.0f M$ | delai %.0f mois | P exigee %.0f%% | prime %.0f | penalite %.0f",
                    c.spec.payload_kg, c.spec.budget_musd, c.spec.deadline_months,
                    100*c.spec.min_success_prob, c.prime_succes, c.penalite_echec);
      else ImGui::Text("paiement a la livraison : %.0f M$", c.spec.budget_musd);
      if (c.termine) ImGui::TextColored(c.reussi?ImVec4(0.35f,0.85f,0.45f,1):ImVec4(0.95f,0.35f,0.3f,1),
                                        c.reussi?"TERMINE - reussi":"TERMINE - perdu");
      else if (c.accepte) ImGui::TextColored(ImVec4(0.95f,0.75f,0.3f,1), "EN COURS");
      else if (ImGui::Button("SIGNER", ImVec2(200, 30))) jeu.accepter_contrat(i);
      ImGui::EndChild(); ImGui::PopID();
    }
    ImGui::EndChild();
  }

  // -------------------------------------------------------------------------
  void e_gestion() {
    ImGui::TextWrapped("La gestion change des PRIX et des DELAIS - jamais la physique. Chaque effet est ecrit.");
    ImGui::Separator();
    ImGui::BeginChild("inst", ImVec2(ImGui::GetWindowWidth() * 0.5f, -8), true);
    ImGui::SetWindowFontScale(1.2f); ImGui::TextUnformatted("INSTALLATIONS"); ImGui::SetWindowFontScale(1.0f);
    ImGui::Separator();
    for (int i = 0; i < (int)jeu.installations.size(); ++i) {
      auto& b = jeu.installations[i]; ImGui::PushID(i);
      ImGui::Text("%s", b.nom.c_str());
      ImGui::SameLine(); ImGui::TextColored(ImVec4(0.5f,0.7f,0.95f,1), "[%s]", b.effet.c_str());
      ImGui::PushTextWrapPos(0); ImGui::TextDisabled("%s", b.pourquoi.c_str()); ImGui::PopTextWrapPos();
      ImGui::TextDisabled("cout %.0f M$ | entretien %.2f M$/mois", b.cout, b.entretien);
      if (b.construite) ImGui::TextColored(ImVec4(0.35f,0.85f,0.45f,1), "  operationnelle");
      else if (ImGui::Button("CONSTRUIRE", ImVec2(160, 26))) jeu.construire(i);
      ImGui::Separator(); ImGui::PopID();
    }
    ImGui::Dummy(ImVec2(0, 6));
    // ------------------------------ LE MARCHE ------------------------------
    jeu.rafraichir_marche();
    ImGui::SetWindowFontScale(1.15f); ImGui::TextUnformatted("LE MARCHE"); ImGui::SetWindowFontScale(1.0f);
    ImGui::TextDisabled("prix FLUCTUANTS (par mois) ; demande mensuelle bornee par acheteur");
    ImGui::Text("stock : %.1f Gbit de donnees | %.2f kg d'echantillons", jeu.donnees_gbit, jeu.echantillons_kg);
    if (ImGui::BeginTable("marche", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
      ImGui::TableSetupColumn("acheteur"); ImGui::TableSetupColumn("M$/Gbit");
      ImGui::TableSetupColumn("demande"); ImGui::TableSetupColumn("M$/kg");
      ImGui::TableSetupColumn("demande##k"); ImGui::TableSetupColumn("vendre");
      ImGui::TableHeadersRow();
      for (int k = 0; k < 3; ++k) {
        ImGui::PushID(300 + k);
        ImGui::TableNextRow(); ImGui::TableNextColumn();
        ImGui::TextUnformatted(jeu.acheteurs[k].nom.c_str());
        ImGui::TextDisabled("%s", jeu.acheteurs[k].profil.c_str());
        ImGui::TableNextColumn(); ImGui::Text("%.2f", jeu.prix_donnees(k));
        ImGui::TableNextColumn(); ImGui::Text("%.0f Gbit", jeu.demande_gbit[k]);
        ImGui::TableNextColumn(); ImGui::Text("%.2f", jeu.prix_echantillons(k));
        ImGui::TableNextColumn(); ImGui::Text("%.1f kg", jeu.demande_kg[k]);
        ImGui::TableNextColumn();
        if (ImGui::SmallButton("10 Gbit")) jeu.vendre_a(k, false, 10);
        ImGui::SameLine(); if (ImGui::SmallButton("tout##g")) jeu.vendre_a(k, false, 1e9);
        if (ImGui::SmallButton("1 kg")) jeu.vendre_a(k, true, 1);
        ImGui::SameLine(); if (ImGui::SmallButton("tout##k")) jeu.vendre_a(k, true, 1e9);
        ImGui::PopID();
      }
      ImGui::EndTable();
    }
    if (ImGui::TreeNode("historique des ventes")) {
      for (auto it = jeu.historique_ventes.rbegin(); it != jeu.historique_ventes.rend(); ++it)
        ImGui::TextDisabled("T+%5.1f  %-22s %6.1f %-18s -> +%.2f M$",
                            it->mois, it->acheteur.c_str(), it->qte, it->quoi.c_str(), it->total);
      if (jeu.historique_ventes.empty()) ImGui::TextDisabled("(aucune vente pour l'instant)");
      ImGui::TreePop();
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("rech", ImVec2(0, -8), true);
    ImGui::SetWindowFontScale(1.2f); ImGui::TextUnformatted("CENTRE DE RECHERCHE"); ImGui::SetWindowFontScale(1.0f);
    ImGui::TextDisabled("une recherche a la fois ; avance d'1 mois par tour");
    ImGui::Separator();
    for (int i = 0; i < (int)jeu.recherches.size(); ++i) {
      auto& r = jeu.recherches[i]; ImGui::PushID(100+i);
      ImGui::Text("%s", r.nom.c_str());
      ImGui::PushTextWrapPos(0);
      ImGui::TextDisabled("%s", r.desc.c_str());
      ImGui::TextColored(ImVec4(0.55f,0.75f,0.6f,1), "%s", r.pourquoi.c_str());
      ImGui::PopTextWrapPos();
      if (!r.prereq.empty()) ImGui::TextDisabled("prerequis : recherche \"%s\"", r.prereq.c_str());
      if (r.faite) ImGui::TextColored(ImVec4(0.35f,0.85f,0.45f,1), "  ACQUISE");
      else if (r.active) { ImGui::ProgressBar((float)(r.avancement / r.mois_requis), ImVec2(-1, 0)); }
      else { ImGui::Text("cout %.0f M$ | %.0f mois", r.cout, r.mois_requis);
        if (ImGui::Button("LANCER", ImVec2(140, 26))) jeu.lancer_recherche(i); }
      ImGui::Separator(); ImGui::PopID();
    }
    ImGui::EndChild();
  }

  // -------------------------------------------------------------------------
  void e_programme() {
    const auto* ac = jeu.actif();
    if (!ac) { ImGui::Dummy(ImVec2(0,30)); centre("Signe un contrat GEO d'abord.", 1.2f); return; }
    auto& k = jeu.conception; const auto& c = *ac;
    if (jeu.vol.commis) { ImGui::TextColored(ImVec4(0.95f,0.75f,0.3f,1), "COMMIT fait : conception GELEE."); ImGui::Separator(); }
    if (ImGui::BeginTabBar("prog")) {
      // --- 1. dérivations + assistant ---
      if (ImGui::BeginTabItem("1. DERIVATIONS")) {
        ImGui::BeginChild("g", ImVec2(ImGui::GetWindowWidth()*0.5f, -8), true);
        ImGui::TextWrapped("Le cahier des charges ne donne AUCUN Delta-v. Deux facons :");
        bouton_memo("? memo : pourquoi ces calculs", "Les derivations de M00", jeu.memo_derivations());
        ImGui::Separator();
        ImGui::TextUnformatted("A) SAISIE DIRECTE (rapide) :");
        ImGui::SetNextItemWidth(180); ImGui::InputText("dv injection (m/s)", inj_buf, sizeof(inj_buf));
        ImGui::SetNextItemWidth(180); ImGui::InputText("insertion combinee", comb_buf, sizeof(comb_buf));
        if (ImGui::Button("VERIFIER", ImVec2(-1, 30)) && !jeu.vol.commis) {
          k.dv_inj_joueur = std::atof(inj_buf); k.dv_comb_joueur = std::atof(comb_buf);
          jeu.verifier_derivations();
        }
        ImGui::Separator();
        ImGui::TextUnformatted("B) ASSISTANT PAS-A-PAS (accessible) :");
        for (int e = 0; e < (int)k.wizard.size(); ++e) {
          auto& w = k.wizard[e]; ImGui::PushID(200+e);
          ImGui::TextWrapped("%s", w.question.c_str());
          ImGui::TextDisabled("formule : %s", w.formule.c_str());
          ImGui::SetNextItemWidth(140);
          ImGui::InputText("m/s", wiz_buf[e], sizeof(wiz_buf[e]));
          ImGui::SameLine();
          if (ImGui::SmallButton("verifier") && !jeu.vol.commis) { w.reponse = std::atof(wiz_buf[e]); jeu.wizard_verifier(e); }
          ImGui::SameLine();
          if (ImGui::SmallButton("indice")) jeu.erreur = w.indice;
          ImGui::SameLine();
          if (ImGui::SmallButton("reveler") && !jeu.vol.commis) { jeu.wizard_reveler(e); std::snprintf(wiz_buf[e], sizeof(wiz_buf[e]), "%.1f", w.valeur); }
          if (w.validee) ImGui::TextColored(ImVec4(0.35f,0.85f,0.45f,1), "  OK");
          ImGui::Separator(); ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("d", ImVec2(0, -8), true);
        ImGui::TextUnformatted("ETAT DE LA CONCEPTION");
        ImGui::Separator();
        if (k.derive_ok) ImGui::TextColored(ImVec4(0.35f,0.85f,0.45f,1), "Derivations VALIDEES.\nLes onglets suivants sont ouverts.");
        else if (k.derive_verifiee) {
          ImGui::TextColored(ImVec4(0.95f,0.6f,0.3f,1), "Pas encore.");
          if (k.indice_separee) ImGui::TextWrapped("Ta valeur d'insertion = la manoeuvre SEPAREE. Combine-la (loi des cosinus) : tu economises ~1154 m/s.");
        } else ImGui::TextDisabled("Utilise A) ou B). En mode Normal, 'reveler' est gratuit.");
        ImGui::Separator();
        ImGui::TextWrapped("L'ECONOMIE de la combinaison (%.0f m/s) decide si la mission est PAYABLE. "
                           "Ce n'est pas un choix de menu : c'est de la geometrie.", k.d.economie);
        ImGui::EndChild();
        ImGui::EndTabItem();
      }
      // --- 2. véhicule & achats ---
      if (ImGui::BeginTabItem("2. ATELIER (VAB)", nullptr, forcer_vab ? ImGuiTabItemFlags_SetSelected : 0)) {
        forcer_vab = false;
        const bool gele = jeu.vol.commis;
        ImGui::BeginChild("g2", ImVec2(ImGui::GetWindowWidth()*0.42f, -8), true);
        ImGui::SetWindowFontScale(1.1f); ImGui::TextUnformatted("ASSEMBLAGE DU VAISSEAU"); ImGui::SetWindowFontScale(1.0f);
        ImGui::SameLine(); bouton_memo("?##fus", "Le point fixe du vehicule", jeu.memo_vehicule());
        ImGui::TextDisabled("chaque module a une masse et un prix ; le Delta-v sort de Tsiolkovski");
        ImGui::Separator();
        ImGui::TextUnformatted("MOTEUR (l'ame de l'etage)");
        for (int m = 0; m < (int)mission::engines().size(); ++m) {
          const auto& E = mission::engines()[m];
          char lab[128]; std::snprintf(lab, sizeof(lab), "%s  Isp %.0f s | %.0f kN | %.0f kg",
                                       E.eng.id.c_str(), E.eng.isp_vac, E.eng.thrust_vac/1000, E.eng.mass);
          if (ImGui::RadioButton(lab, &k.moteur, m) && !gele) jeu.recalculer_conception();
        }
        ImGui::Separator();
        ImGui::TextUnformatted("AVIONIQUE");
        static const char* avio[3] = {"basique (45 kg, 2 M$)","redondante (70 kg, 5 M$)","miniaturisee (22 kg, 9 M$)"};
        for (int a2 = 0; a2 < 3; ++a2)
          if (ImGui::RadioButton(avio[a2], &k.vab.avionique, a2) && !gele) jeu.recalculer_conception();
        ImGui::TextUnformatted("STRUCTURE / FUSELAGE");
        static const char* stru[3] = {"legere (95 kg, 1,5 M$)","standard (150 kg, 2,5 M$)","renforcee (230 kg, 4,5 M$)"};
        for (int s2 = 0; s2 < 3; ++s2)
          if (ImGui::RadioButton(stru[s2], &k.vab.structure, s2) && !gele) jeu.recalculer_conception();
        ImGui::Separator();
        ImGui::TextUnformatted("RESERVOIR (ergols charges)");
        float erg = (float)k.vab.ergols;
        bool va = k.vab_auto;
        if (ImGui::Checkbox("dimensionner AUTOMATIQUEMENT (point fixe)", &va) && !gele) { k.vab_auto = va; jeu.recalculer_conception(); }
        if (!k.vab_auto) {
          if (ImGui::SliderFloat("##erg", &erg, 300, 8000, "%.0f kg") && !gele) { k.vab.ergols = erg; jeu.recalculer_conception(); }
          ImGui::TextDisabled("a la main : trop peu = panne seche EN VOL ; trop = masse et argent gaspilles");
        } else ImGui::TextDisabled("le point fixe regle les ergols au besoin exact (%.0f kg)", k.vab.ergols);
        ImGui::Separator();
        ImGui::TextUnformatted("OPTIONS");
        bool ins = k.instrument;
        if (ImGui::Checkbox("instrument SCIENCE (+150 kg, +8 M$) -> relais apres mission", &ins) && !gele) { k.instrument = ins; jeu.recalculer_conception(); }
        bool ant = k.vab.antenne;
        if (ImGui::Checkbox("antenne grand gain (+40 kg, +4 M$) -> +50 % de debit", &ant) && !gele) { k.vab.antenne = ant; jeu.recalculer_conception(); }
        ImGui::Separator();
        ImGui::TextUnformatted("ACHATS PROGRAMME");
        const char* ln[4] = {"lanceur : le moins cher","L-A leger","L-B moyen","L-C lourd"};
        int li = k.lanceur + 1; ImGui::SetNextItemWidth(em(15));
        if (ImGui::Combo("##l", &li, ln, 4) && !gele) { k.lanceur = li - 1; jeu.recalculer_conception(); }
        float he = (float)k.heures_essai;
        if (ImGui::SliderFloat("heures d'essai", &he, 0, 1500, "%.0f h") && !gele) { k.heures_essai = he; jeu.recalculer_conception(); }
        float mg = (float)k.marge_dv;
        if (ImGui::SliderFloat("marge correction", &mg, 0, 200, "%.0f m/s") && !gele) { k.marge_dv = mg; jeu.recalculer_conception(); }
        bool rev = k.revue;
        if (ImGui::Checkbox("revue independante (3 M$)", &rev) && !gele) { k.revue = rev; jeu.recalculer_conception(); }
        ImGui::Separator();
        ImGui::TextUnformatted("POURSUITE (navigation)");
        for (int l = 0; l <= 6; ++l) {
          const auto& N = app::niveau_poursuite(l);
          char lab[160];
          if (jeu.p_catalogue_visible())
            std::snprintf(lab, sizeof(lab), "%d - %s (%.2f M$, P~%.0f%%)", l, N.nom, N.cout_musd*jeu.m_poursuite(), 100*N.p_physique_catalogue);
          else std::snprintf(lab, sizeof(lab), "%d - %s (%.2f M$)", l, N.nom, N.cout_musd*jeu.m_poursuite());
          if (ImGui::RadioButton(lab, &k.niveau, l) && !gele) { k.p_mesuree = false; jeu.recalculer_conception(); }
        }
        ImGui::SetNextItemWidth(em(7)); ImGui::SliderInt("##mc", &mc_n, 10, 100, "%d vols");
        ImGui::SameLine();
        if (jeu.mc_en_cours) ImGui::ProgressBar(jeu.mc_total?(float)jeu.mc_fait/jeu.mc_total:0, ImVec2(-1,0));
        else if (ImGui::Button("MESURER P(physique)") && !gele) jeu.mesurer_p_physique(mc_n);
        ImGui::EndChild();
        ImGui::SameLine();
        // ---------------- la baie d'assemblage : schema + verdicts ----------------
        ImGui::BeginChild("d2", ImVec2(0, -8), true);
        ImGui::TextUnformatted("BAIE D'ASSEMBLAGE");
        ImGui::Separator();
        dessiner_vab_schema();
        const auto& b = k.bilan;
        const double dv = jeu.vab_dv();
        ImGui::Separator();
        auto ligne_check = [&](const char* n, bool ok2, const char* detail) {
          ImGui::TextColored(ok2?ImVec4(0.4f,0.9f,0.5f,1):ImVec4(0.95f,0.4f,0.35f,1), "%s %s", ok2?"[OK]":"[ X]", n);
          ImGui::SameLine(em(16)); ImGui::TextDisabled("%s", detail);
        };
        char d1[64], d2b[64], d3[64];
        std::snprintf(d1, sizeof(d1), "%.0f m/s fournis / %.0f requis", dv, b.dv_design);
        ligne_check("Delta-v", dv >= b.dv_design - 0.5, d1);
        std::snprintf(d2b, sizeof(d2b), "m0 = %.0f kg", jeu.vab_m0());
        ligne_check("lanceur", b.fits_mass, d2b);
        const double arc = jeu.vab_duree_injection();
        std::snprintf(d3, sizeof(d3), "arc d'injection ~%.0f s", arc);
        ligne_check("poussee", arc < 900, d3);
        if (arc >= 900) ImGui::TextDisabled("  arc trop long : les pertes de poussee finie explosent (>0,5 %%)");
        if (b.launcher_index >= 0) {
          const auto& L = mission::launchers()[b.launcher_index];
          ImGui::Text("lanceur retenu : %s", L.id.c_str());
          jauge("remplissage", jeu.vab_m0(), L.payload_leo, "kg", ImVec4(0.4f,0.6f,0.9f,1));
        }
        ImGui::EndChild();
        ImGui::EndTabItem();
      }
      // --- 3. bilan + matrice ---
      if (ImGui::BeginTabItem("3. BILAN")) {
        const auto& b = k.bilan;
        ImGui::BeginChild("g3", ImVec2(ImGui::GetWindowWidth()*0.5f, -8), true);
        ImGui::TextUnformatted("ARGENT");
        ImGui::Text("  lanceur %.1f | moteur %.1f | etage %.1f", b.cost_launcher, b.cost_engine, b.cost_stage);
        ImGui::Text("  poursuite %.1f | essais %.1f | revue %.1f | ops %.1f", b.cost_tracking, b.cost_tests, b.cost_review, b.cost_ops);
        ImGui::Text("  TOTAL %.1f M$  (budget %.0f)", b.cost_total, c.spec.budget_musd);
        ImGui::Separator();
        ImGui::Text("CALENDRIER %.1f mois (delai %.0f)", b.schedule_months, c.spec.deadline_months);
        ImGui::Separator();
        ImGui::TextUnformatted("RISQUE (decompose)");
        ImGui::Text("  lanceur %.1f%% | moteur x4 %.1f%% | bevues %.1f%%", 100*b.p_launcher, 100*b.p_engine, 100*(1-b.p_blunder));
        ImGui::Text("  PHYSIQUE %.1f%% %s", 100*k.p_physique, k.p_mesuree?"(MESUREE)":"(catalogue)");
        ImGui::SetWindowFontScale(1.15f);
        ImGui::Text("  P(succes) %.1f%% (exige %.0f%%)", 100*b.p_success, 100*c.spec.min_success_prob);
        ImGui::SetWindowFontScale(1.0f);
        ImGui::Separator();
        auto feu = [](const char* n, bool ok){ ImGui::TextColored(ok?ImVec4(0.35f,0.85f,0.45f,1):ImVec4(0.95f,0.35f,0.3f,1),"%s %s", ok?"[OK]":"[NON]", n); };
        feu("masse", b.fits_mass); feu("budget", b.fits_budget); feu("calendrier", b.fits_schedule); feu("risque", b.fits_risk);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("d3", ImVec2(0, -8), true);
        ImGui::TextUnformatted("MATRICE DE PROGRAMME (moteur x poursuite)");
        if (!k.matrice_achetee) {
          ImGui::TextWrapped("La division analyse evalue toutes les combinaisons (0,4 M$).");
          if (ImGui::Button("ACHETER LA MATRICE", ImVec2(-1, 30)) && !jeu.vol.commis) jeu.acheter_matrice();
        } else if (ImGui::BeginTable("m", 4, ImGuiTableFlags_Borders|ImGuiTableFlags_RowBg)) {
          ImGui::TableSetupColumn("nav");
          for (int m=0;m<3;++m) ImGui::TableSetupColumn(mission::engines()[m].eng.id.c_str());
          ImGui::TableHeadersRow();
          for (int l=0;l<=6;++l){ ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::Text("%d", l);
            for (int m=0;m<3;++m){ ImGui::TableNextColumn();
              for (auto& cm : jeu.matrice) if (cm.moteur==m && cm.niveau==l) {
                if (cm.a.ok) ImGui::TextColored(ImVec4(0.35f,0.85f,0.45f,1), "%.0fM/%.0f%%", cm.a.cost_total, 100*cm.a.p_success);
                else ImGui::TextColored(ImVec4(0.8f,0.45f,0.35f,1), "%s", cm.a.why.c_str());
              } } }
          ImGui::EndTable();
        }
        ImGui::EndChild();
        ImGui::EndTabItem();
      }
      // --- 4. commit ---
      if (ImGui::BeginTabItem("4. COMMIT")) {
        ImGui::TextWrapped("Le COMMIT gele la GRAINE (tiree, cachee), le VEHICULE, l'ARGENT, la DATE. "
                           "Pas la valeur des manoeuvres futures - comme dans la vraie vie.");
        ImGui::Separator();
        ImGui::Text("cout a l'engagement : %.1f M$   |   tresorerie apres : %.1f M$",
                    k.bilan.cost_total, jeu.agence.tresorerie - k.bilan.cost_total);
        ImGui::Text("P(succes) : %.1f%%", 100*k.bilan.p_success);
        if (!k.derive_ok) ImGui::TextColored(ImVec4(0.95f,0.6f,0.3f,1), "Derive d'abord tes Delta-v (onglet 1).");
        ImGui::Dummy(ImVec2(0, 10));
        if (!jeu.vol.commis && !c.termine)
          if (ImGui::Button("COMMIT - IRREVERSIBLE", ImVec2(-1, 46))) { if (jeu.commit()) ecran = Ecran::Vol; }
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
    }
  }

  // -------------------------------------------------------------------------
  // SALLE DE VOL GEO : vivante, temps reel, 3D
  // -------------------------------------------------------------------------
  const char* consigne() const {
    switch (jeu.vol.etape) {
      case app::EtapeVol::PreInjection: return "Pret au lancement. LANCER injecte en GTO et lance la montee.";
      case app::EtapeVol::PretAMF:  return "Apogee : OBSERVE tes passes, calcule l'AMF (insertion combinee).";
      case app::EtapeVol::DeriveAMF2: return "AVANCER : derive vers l'apside opposee.";
      case app::EtapeVol::PretAMF2: return "AMF2 : observe, calcule, execute (petite manoeuvre).";
      case app::EtapeVol::DeriveTRIM: return "AVANCER : derive (+ revolutions payees).";
      case app::EtapeVol::PretTRIM: return "TRIM final : circulariser. Observe, calcule, execute.";
      case app::EtapeVol::DeriveVerdict: return "AVANCER : la verite va etre revelee.";
      default: return "";
    }
  }
  void e_vol() {
    auto& v = jeu.vol;
    if (!v.commis) { ImGui::Dummy(ImVec2(0,30)); centre("Concois puis COMMIT (onglet PROGRAMME).", 1.2f); return; }
    if (v.etape == app::EtapeVol::Verdict) { e_postmortem(); return; }
    const bool pret = v.etape==app::EtapeVol::PretAMF||v.etape==app::EtapeVol::PretAMF2||v.etape==app::EtapeVol::PretTRIM;
    const bool derive = v.etape==app::EtapeVol::PreInjection||v.etape==app::EtapeVol::DeriveAMF2||
                        v.etape==app::EtapeVol::DeriveTRIM||v.etape==app::EtapeVol::DeriveVerdict;
    const float topH = ImGui::GetContentRegionAvail().y * 0.64f;

    // colonne gauche : cockpit
    ImGui::BeginChild("cock", ImVec2(360, topH), true);
    ImGui::SetWindowFontScale(1.15f); ImGui::TextUnformatted("SALLE DE VOL"); ImGui::SetWindowFontScale(1.0f);
    ImGui::TextDisabled("t0 + %.0f s  (%.2f h)", v.S->t()-v.t0, (v.S->t()-v.t0)/3600);
    ImGui::Separator();
    ImGui::PushTextWrapPos(0); ImGui::TextUnformatted(consigne()); ImGui::PopTextWrapPos();
    ImGui::Separator();
    // jauges
    jauge("Delta-v restant", v.S->dv_remaining(0), std::max(50.0, k_dv_budget()), "m/s", ImVec4(0.4f,0.7f,0.9f,1), 20);
    jauge("correction payee", v.dv_corr, 100, "m/s", ImVec4(0.9f,0.7f,0.4f,1));
    if (v.obs_valide) jauge("incertitude (log)", std::log10(std::max(1.0, v.obs.sigma_pos)), 5, "", ImVec4(0.8f,0.5f,0.9f,1));
    ImGui::Separator();
    // stations DSN
    ImGui::TextUnformatted("STATIONS DSN");
    voyant("Goldstone", v.stations & 1); ImGui::SameLine(130);
    voyant("Madrid", v.stations & 2);
    voyant("Canberra", v.stations & 4);
    ImGui::Separator();
    // controle du temps
    ImGui::TextUnformatted("TEMPS");
    if (v.ascension_t >= 0.0) {
      ImGui::TextColored(ImVec4(1.0f,0.7f,0.3f,1), "  >> SEQUENCE DE LANCEMENT");
      if (ImGui::Button(v.tr_actif?"|| PAUSE":"|> REPRENDRE", ImVec2(150,28))) v.tr_actif = !v.tr_actif;
      ImGui::SameLine();
      if (ImGui::Button(">>| SAUTER", ImVec2(150,28))) jeu.vol_sauter();
    } else if (v.tr_en_route) {
      ImGui::TextColored(ImVec4(0.4f,0.9f,0.5f,1), "  >> croisiere en cours");
      float warp = (float)v.tr_warp;
      ImGui::SetNextItemWidth(-1);
      if (ImGui::SliderFloat("##w", &warp, 60, 4000, "warp x%.0f")) v.tr_warp = warp;
      if (ImGui::Button(v.tr_actif?"|| PAUSE":"|> REPRENDRE", ImVec2(150,28))) v.tr_actif = !v.tr_actif;
      ImGui::SameLine();
      if (ImGui::Button(">>| SAUTER", ImVec2(150,28))) jeu.vol_sauter();
    } else if (derive) {
      if (ImGui::Button(v.etape==app::EtapeVol::PreInjection?">>> LANCER <<<":">>> AVANCER", ImVec2(-1, 44))) jeu.vol_engager();
    }
    ImGui::Separator();
    // actions de manoeuvre
    if (pret) {
      if (ImGui::Button("OBSERVER (determination d'orbite)", ImVec2(-1, 30))) jeu.vol_observer();
      if (v.obs_valide) ImGui::TextDisabled("%s | sigma %.1f m", v.obs.source.c_str(), v.obs.sigma_pos);
      char lab[64]; std::snprintf(lab, sizeof(lab), "DIVISION ANALYSE : calcule (%.1f M$)", jeu.prix_analyse());
      if (ImGui::Button(lab, ImVec2(-1, 30))) jeu.vol_analyser();
      if (v.prop.valide) {
        ImGui::TextWrapped("%s", v.prop.note.c_str());
        ImGui::SameLine(); if (!v.prop.memo.empty()) bouton_memo("?##man", "Le calcul de la manoeuvre", v.prop.memo);
        if (ImGui::Button("EXECUTER", ImVec2(-1, 36))) jeu.vol_bruler_proposition();
      } else if (!v.prop.note.empty()) ImGui::TextColored(ImVec4(0.95f,0.6f,0.3f,1), "%s", v.prop.note.c_str());
      if (ImGui::TreeNode("manoeuvre a la main (RSW)")) {
        ImGui::SetNextItemWidth(120); ImGui::InputFloat("dans s", &man_dt);
        ImGui::SetNextItemWidth(80); ImGui::InputFloat("R", &man_r); ImGui::SameLine();
        ImGui::SetNextItemWidth(80); ImGui::InputFloat("S", &man_s); ImGui::SameLine();
        ImGui::SetNextItemWidth(80); ImGui::InputFloat("W", &man_w);
        if (ImGui::Button("BRULER (manuel)", ImVec2(-1, 28))) jeu.vol_bruler_manuel(man_dt, man_r, man_s, man_w);
        ImGui::TreePop();
      }
    }
    ImGui::EndChild();

    // centre : la vue 3D (ou la sequence de lancement pendant l'ascension)
    ImGui::SameLine();
    ImGui::BeginChild("v3d", ImVec2(ImGui::GetContentRegionAvail().x*0.66f, topH), true);
    if (v.ascension_t >= 0.0) { ImGui::TextUnformatted("DECOLLAGE"); dessiner_ascension(v.ascension_t); }
    else { ImGui::TextUnformatted("VUE 3D (ton ESTIME, jamais la verite en vol)"); dessiner_vue3d_geo(); }
    ImGui::EndChild();

    // droite : télémétrie
    ImGui::SameLine();
    ImGui::BeginChild("tele", ImVec2(0, topH), true);
    ImGui::TextUnformatted("TELEMETRIE");
    ImGui::Separator();
    for (auto& l : v.flux) ImGui::TextWrapped("%s", l.c_str());
    ImGui::EndChild();

    // bas : cadrans + chronologie
    ImGui::BeginChild("bas", ImVec2(0, 0), true);
    if (v.obs_valide) {
      const auto el = astro::rv_to_elements(v.obs.state.r, v.obs.state.v, cst::MU_EARTH);
      cadran("a / GEO", (float)std::min(1.2, el.a/42164170.0), fmt1(el.a/1000, "km"), ImVec4(0.4f,0.7f,0.9f,1)); ImGui::SameLine();
      cadran("ecc", (float)std::min(1.0, el.e/0.75), fmt3(el.e), ImVec4(0.9f,0.7f,0.4f,1)); ImGui::SameLine();
      cadran("inc/28.5", (float)std::min(1.0, el.i/cst::DEG/28.5), fmt1(el.i/cst::DEG, "d"), ImVec4(0.8f,0.5f,0.9f,1)); ImGui::SameLine();
    }
    ImGui::BeginChild("chr", ImVec2(0,0));
    ImGui::TextUnformatted("CHRONOLOGIE");
    if (ImPlot::BeginPlot("##c", ImVec2(-1,-1))) {
      ImPlot::SetupAxes("h", nullptr, 0, ImPlotAxisFlags_NoTickLabels|ImPlotAxisFlags_NoGridLines);
      ImPlot::SetupAxesLimits(-1, std::max(30.0,(v.S->t()-v.t0)/3600+3), 0, 4, ImPlotCond_Always);
      double nx[2]={(v.S->t()-v.t0)/3600,(v.S->t()-v.t0)/3600}, ny[2]={0.3,3.7};
      ImPlot::SetNextLineStyle(ImVec4(1,1,1,0.6f),1.0f); ImPlot::PlotLine("maintenant", nx, ny, 2);
      for (auto& e : v.chrono) { double xs[2]={e.t_h,e.t_h}, ys[2]={0.5,3.5};
        ImVec4 col2 = e.type==1?ImVec4(0.95f,0.55f,0.2f,1):e.type==2?ImVec4(0.35f,0.65f,0.95f,1):e.type==3?ImVec4(0.75f,0.55f,0.95f,1):ImVec4(0.8f,0.8f,0.8f,1);
        ImPlot::SetNextLineStyle(col2, e.type==1?3.f:1.5f); ImPlot::PlotLine(e.nom.c_str(), xs, ys, 2); }
      ImPlot::EndPlot();
    }
    ImGui::EndChild();
    ImGui::EndChild();
  }

  double k_dv_budget() const { return jeu.conception.bilan.dv_design; }
  static const char* fmt1(double v, const char* u) { static char b[32]; std::snprintf(b,sizeof(b),"%.0f%s",v,u); return b; }
  static const char* fmt3(double v) { static char b[32]; std::snprintf(b,sizeof(b),"%.3f",v); return b; }

  void dessiner_vue3d_geo() {
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 sz = ImGui::GetContentRegionAvail();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, ImVec2(p0.x+sz.x, p0.y+sz.y), col(0.04f,0.05f,0.07f,1));
    ImGui::InvisibleButton("v3dhit", sz);
    if (ImGui::IsItemActive()) { ImVec2 d = ImGui::GetIO().MouseDelta; vue.yaw += d.x*0.01f; vue.pitch += d.y*0.01f; }
    if (ImGui::IsItemHovered() && ImGui::GetIO().MouseWheel != 0) vue.zoom *= (1 + ImGui::GetIO().MouseWheel*0.1f);
    vue.pitch = std::fmax(0.05f, std::fmin(1.5f, vue.pitch));
    vue.cadrer(ImVec2(p0.x+sz.x*0.5f, p0.y+sz.y*0.5f), std::min(sz.x,sz.y)*0.42f, 46000.0);
    vue.sphere_fil(dl, 6378.137, col(0.25f,0.45f,0.75f,0.8f));
    vue.cercle_equatorial(dl, 42164.17, col(0.35f,0.75f,0.45f,0.7f), 1.5f);
    vue.cercle_equatorial(dl, 6578.137, col(0.5f,0.5f,0.55f,0.5f), 1.0f);
    if (!jeu.vol.traj_x.empty())
      vue.polyligne(dl, jeu.vol.traj_x, jeu.vol.traj_y, jeu.vol.traj_z, col(0.95f,0.75f,0.3f,0.9f), 2.0f);
    Vec3 m = jeu.vol_position_estimee();
    if (norm(m) > 0) vue.marqueur(dl, m, col(1,1,1,1), "sonde");
    dl->AddText(ImVec2(p0.x+8, p0.y+8), col(0.6f,0.64f,0.7f,1), "glisser = tourner | molette = zoom");
  }

  // sequence de lancement : une fusee monte du sol, compte a rebours, flammes.
  // LA BAIE D'ASSEMBLAGE : schema en coupe du vaisseau, modules a l'echelle
  // (hauteur ~ masse), etiquettes chiffrees. Dessin vectoriel sobre.
  void dessiner_vab_schema() {
    const auto& k = jeu.conception;
    const auto* c = jeu.actif(); if (!c) return;
    const auto& E = mission::engines()[k.moteur];
    struct Module { const char* nom; double masse; ImVec4 col; };
    const double m_payload = c->spec.payload_kg + (k.instrument?150.0:0.0) + (k.vab.antenne?40.0:0.0);
    static const double AV[3] = {45,70,22}, ST[3] = {95,150,230};
    const double m_tank_sec = k.vab.ergols * E.tank_dry_fraction;
    Module mods[5] = {
      {"CHARGE UTILE", m_payload,               ImVec4(0.55f,0.7f,0.95f,1)},
      {"AVIONIQUE",    AV[k.vab.avionique],     ImVec4(0.7f,0.6f,0.9f,1)},
      {"RESERVOIR",    k.vab.ergols + m_tank_sec, ImVec4(0.85f,0.75f,0.45f,1)},
      {"STRUCTURE",    ST[k.vab.structure],     ImVec4(0.6f,0.65f,0.7f,1)},
      {"MOTEUR",       E.eng.mass,              ImVec4(0.9f,0.5f,0.4f,1)},
    };
    double total = 0; for (auto& m : mods) total += m.masse;
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    const float W = ImGui::GetContentRegionAvail().x;
    const float H = em(13), bw = em(7);
    const float bx = p0.x + em(1.5f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float y = p0.y;
    // coiffe
    dl->AddTriangleFilled(ImVec2(bx, y+em(1)), ImVec2(bx+bw, y+em(1)), ImVec2(bx+bw/2, y), col(0.75f,0.78f,0.82f,1));
    y += em(1);
    for (auto& m : mods) {
      const float h = (float)(H * std::sqrt(m.masse / std::max(1.0, total)));  // sqrt : lisible
      dl->AddRectFilled(ImVec2(bx, y), ImVec2(bx+bw, y+h), ImGui::GetColorU32(ImVec4(m.col.x,m.col.y,m.col.z,0.35f)));
      dl->AddRect(ImVec2(bx, y), ImVec2(bx+bw, y+h), ImGui::GetColorU32(m.col), 0, 0, 1.6f);
      char lab[64]; std::snprintf(lab, sizeof(lab), "%s  %.0f kg", m.nom, m.masse);
      dl->AddText(ImVec2(bx+bw+em(0.6f), y + h*0.5f - em(0.55f)), ImGui::GetColorU32(m.col), lab);
      y += h;
    }
    // tuyere
    dl->AddTriangleFilled(ImVec2(bx+bw*0.30f, y), ImVec2(bx+bw*0.70f, y), ImVec2(bx+bw*0.5f, y+em(0.9f)), col(0.5f,0.52f,0.56f,1));
    (void)W;
    ImGui::Dummy(ImVec2(0, (y+em(1.2f)) - p0.y));
    ImGui::Text("MASSE TOTALE  %.0f kg   |   DELTA-V  %.0f m/s", jeu.vab_m0(), jeu.vab_dv());
  }

  // SEQUENCE DE LANCEMENT, version salle de controle : sobre et lisible.
  // Un grand compte a rebours, une checklist qui s'egrene, puis les lectures
  // ALTITUDE / VITESSE et une barre de progression d'ascension. Zero cartoon.
  void dessiner_ascension(float t) {
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 sz = ImGui::GetContentRegionAvail();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, ImVec2(p0.x+sz.x, p0.y+sz.y), col(0.03f,0.04f,0.06f,1));
    // quadrillage discret de salle de controle
    for (int i = 1; i < 8; ++i) {
      dl->AddLine(ImVec2(p0.x, p0.y+sz.y*i/8.f), ImVec2(p0.x+sz.x, p0.y+sz.y*i/8.f), col(1,1,1,0.03f));
      dl->AddLine(ImVec2(p0.x+sz.x*i/8.f, p0.y), ImVec2(p0.x+sz.x*i/8.f, p0.y+sz.y), col(1,1,1,0.03f));
    }
    const bool avant_allumage = t < 0.5f;
    // --- le grand chrono, centre ---
    char chrono[32];
    if (avant_allumage) std::snprintf(chrono, sizeof(chrono), "T - %02d", std::max(1, 10 - (int)(t*20)));
    else                std::snprintf(chrono, sizeof(chrono), "T + %02d", (int)((t-0.5f)*20));
    ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + sz.y*0.08f));
    ImGui::SetWindowFontScale(3.4f);
    const float cw = ImGui::CalcTextSize(chrono).x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (sz.x - cw) * 0.5f);
    ImGui::TextColored(avant_allumage ? ImVec4(0.95f,0.8f,0.35f,1) : ImVec4(0.4f,0.9f,0.55f,1), "%s", chrono);
    ImGui::SetWindowFontScale(1.0f);
    // --- la checklist s'egrene avec le decompte ---
    static const char* etapes[6] = {
      "Alimentation interne          . . . GO",
      "Pressurisation des reservoirs . . . GO",
      "Guidage interne               . . . GO",
      "Bras cryotechniques retires   . . . GO",
      "Sequenceur d'allumage arme    . . . GO",
      "ALLUMAGE                      . . . GO",
    };
    ImGui::Dummy(ImVec2(0, em(1)));
    const float lx = p0.x + sz.x*0.5f - em(11);
    for (int i = 0; i < 6; ++i) {
      const float seuil = 0.06f + i * 0.078f;
      if (t < seuil) break;
      ImGui::SetCursorScreenPos(ImVec2(lx, ImGui::GetCursorScreenPos().y));
      const bool derniere = (i == 5);
      ImGui::TextColored(derniere ? ImVec4(1.0f,0.65f,0.25f,1) : ImVec4(0.55f,0.85f,0.6f,1), "%s", etapes[i]);
    }
    // --- apres l'allumage : lectures et progression (profil INDICATIF affiche comme tel) ---
    if (!avant_allumage) {
      const float u = (t - 0.5f) * 2.0f;             // 0..1 pendant l'ascension
      ImGui::Dummy(ImVec2(0, em(1)));
      ImGui::SetCursorScreenPos(ImVec2(lx, ImGui::GetCursorScreenPos().y));
      ImGui::Text("ALTITUDE   %6.1f km", 200.0 * u * u);
      ImGui::SetCursorScreenPos(ImVec2(lx, ImGui::GetCursorScreenPos().y));
      ImGui::Text("VITESSE    %6.0f m/s", 7784.0 * u);
      ImGui::SetCursorScreenPos(ImVec2(lx, ImGui::GetCursorScreenPos().y));
      ImGui::TextDisabled("(profil indicatif d'ascension ; l'injection reelle suit)");
      // barre de progression fine en bas
      const float by = p0.y + sz.y - em(2.2f);
      dl->AddRectFilled(ImVec2(p0.x+em(2), by), ImVec2(p0.x+sz.x-em(2), by+em(0.5f)), col(1,1,1,0.08f), em(0.2f));
      dl->AddRectFilled(ImVec2(p0.x+em(2), by), ImVec2(p0.x+em(2)+(sz.x-em(4))*u, by+em(0.5f)),
                        col(0.4f,0.75f,0.95f,0.9f), em(0.2f));
      dl->AddText(ImVec2(p0.x+em(2), by-em(1.4f)), col(0.7f,0.75f,0.8f,1), "ASCENSION VERS L'ORBITE DE PARKING");
    }
  }

  // rangee de resultat par critere : label, valeur, jauge d'ecart, OK/ECHEC.
  void critere(const char* nom, double atteint, double cible, double tol, const char* unite, bool ok, bool relatif=false) {
    ImGui::Text("%-14s", nom);
    ImGui::SameLine(130);
    ImGui::TextColored(ok?ImVec4(0.4f,0.9f,0.5f,1):ImVec4(0.95f,0.4f,0.35f,1), ok?"[ OK ]":"[ECHEC]");
    ImGui::SameLine(210);
    if (relatif) ImGui::Text("atteint %.4f  (max %.4f)", atteint, tol);
    else ImGui::Text("atteint %.1f %s  (cible %.0f +/- %.0f)", atteint, unite, cible, tol);
    // jauge d'ecart : 0 = pile, 1 = a la limite, >1 = hors tolerance
    const double ecart = relatif ? atteint/tol : std::fabs(atteint-cible)/tol;
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ok?ImVec4(0.35f,0.8f,0.45f,1):ImVec4(0.9f,0.35f,0.3f,1));
    char ov[24]; std::snprintf(ov,sizeof(ov), "%.0f%% de la tolerance", 100*ecart);
    ImGui::ProgressBar((float)std::min(1.2, ecart)/1.2f, ImVec2(-1, 14), ov);
    ImGui::PopStyleColor();
  }

  void e_postmortem() {
    auto& v = jeu.vol;
    const auto* ac = jeu.actif();
    ImGui::BeginChild("g", ImVec2(ImGui::GetWindowWidth()*0.46f, 0), true);
    ImGui::SetWindowFontScale(1.7f);
    ImGui::TextColored(v.ok?ImVec4(0.35f,0.85f,0.45f,1):ImVec4(0.95f,0.35f,0.3f,1), v.ok?"+++ MISSION REUSSIE +++":"--- MISSION PERDUE ---");
    ImGui::SetWindowFontScale(1.0f); ImGui::Separator();
    // POURQUOI, en clair et gros
    ImGui::TextColored(ImVec4(0.9f,0.9f,0.7f,1), "POURQUOI :");
    ImGui::PushTextWrapPos(0); ImGui::TextWrapped("%s", v.pourquoi.c_str()); ImGui::PopTextWrapPos();
    ImGui::Separator();
    // les 3 criteres, avec jauges
    if (!v.perdu_avant_cible && ac) {
      ImGui::TextUnformatted("LES TROIS CRITERES DU CONTRAT :");
      critere("altitude (a)", v.el_final.a/1000, ac->cible_sma/1000, ac->tol_sma/1000, "km", v.a_ok);
      critere("forme (e)", v.el_final.e, 0, ac->tol_ecc, "", v.e_ok, true);
      critere("inclinaison (i)", v.el_final.i/cst::DEG, 0, ac->tol_inc_deg, "deg", v.i_ok, true);
      ImGui::TextDisabled("La jauge montre a quel point du budget de tolerance tu es. Verte = dedans.");
    }
    ImGui::Separator(); ImGui::TextUnformatted("ANALYSE (Monte-Carlo) :");
    if (jeu.mc_en_cours) ImGui::ProgressBar(jeu.mc_total?(float)jeu.mc_fait/jeu.mc_total:0, ImVec2(-1,0), "decomposition...");
    ImGui::PushTextWrapPos(0); ImGui::TextUnformatted(v.postmortem.c_str()); ImGui::PopTextWrapPos();
    ImGui::Separator();
    if (ImGui::Button("RETOUR AU BUREAU", ImVec2(-1, 38))) ecran = Ecran::Bureau;
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("d", ImVec2(0, 0), true);
    ImGui::TextUnformatted("L'ORBITE VRAIE contre la cible");
    if (ImPlot::BeginPlot("##pm", ImVec2(-1,-1), ImPlotFlags_Equal)) {
      ImPlot::SetupAxes("x [km]","y [km]");
      ImPlot::SetupAxesLimits(-50000,50000,-50000,50000, ImPlotCond_Once);
      const int N=180; static std::vector<double> cx(N+1),cy(N+1);
      for (int i=0;i<=N;++i){ double a=2*3.14159265*i/N; cx[i]=42164.17*cos(a); cy[i]=42164.17*sin(a);}
      ImPlot::SetNextLineStyle(ImVec4(0.35f,0.85f,0.45f,0.9f),1.5f); ImPlot::PlotLine("cible GEO", cx.data(),cy.data(),N+1);
      if (!v.verite_x.empty()){ ImPlot::SetNextLineStyle(ImVec4(0.95f,0.35f,0.3f,1),2.f); ImPlot::PlotLine("orbite VRAIE", v.verite_x.data(),v.verite_y.data(),(int)v.verite_x.size()); }
      ImPlot::EndPlot();
    }
    ImGui::EndChild();
  }

  // -------------------------------------------------------------------------
  // CARTE INTERPLANETAIRE (conception Mars / comete)
  // -------------------------------------------------------------------------
  void e_carte() {
    const auto* ac = jeu.actif();
    if (!ac) { ImGui::Dummy(ImVec2(0,30)); centre("Signe un contrat interplanetaire.", 1.2f); return; }
    auto& ci = jeu.cinterp;
    const bool comete = ac->type==app::TypeContrat::VolComete;
    const bool titan = ac->type==app::TypeContrat::VolTitan;
    const bool survol = comete || titan;
    const char* cible = titan?"Saturne/Titan":comete?"la comete":"Mars";
    ImGui::BeginChild("g", ImVec2(ImGui::GetWindowWidth()*0.55f, -8), true);
    ImGui::Text("Cible : %s. Chaque case = une solution de Lambert EXACTE (dv total km/s).", cible);
    ImGui::TextWrapped("Clique une case pour figer (date de depart, duree de transit).");
    ImGui::SameLine(); bouton_memo("? memo", "Concevoir un transfert", jeu.memo_interp());
    if (titan) { ImGui::SameLine(); bouton_memo("? Titan", "Titan : le graal", jeu.memo_titan()); }
    if (!ci.carte_calculee && !ci.calcul) {
      if (ImGui::Button("CALCULER LA CARTE (temps de calcul paye)", ImVec2(-1, 32))) jeu.interp_calculer_carte();
    } else if (ci.calcul) ImGui::TextColored(ImVec4(0.95f,0.75f,0.3f,1), "calcul en cours...");
    if (ci.carte_calculee) {
      if (ImPlot::BeginPlot("##pk", ImVec2(-1,-1))) {
        ImPlot::SetupAxes("transit [j]","depart [j depuis 2026-06-01]");
        const double d1=(ci.dep1-ci.dep0)/cst::DAY, t0j=ci.tof0/cst::DAY, t1j=ci.tof1/cst::DAY;
        ImPlot::PushColormap(ImPlotColormap_Viridis);
        ImPlot::PlotHeatmap("dv km/s", ci.grille.data(), ci.n_dep, ci.n_tof, survol?8.0:5.0, survol?40.0:20.0,
                            nullptr, ImPlotPoint(t0j,0), ImPlotPoint(t1j,d1));
        ImPlot::PopColormap();
        if (ci.choisie) { double bx[1]={ci.tof/cst::DAY}, by[1]={(ci.t_dep-ci.dep0)/cst::DAY};
          ImPlot::SetNextMarkerStyle(ImPlotMarker_Cross,10,ImVec4(1,1,1,1),2.5f); ImPlot::PlotScatter("choix", bx,by,1); }
        if (ImPlot::IsPlotHovered() && ImGui::IsMouseClicked(0)) {
          ImPlotPoint pp = ImPlot::GetPlotMousePos();
          double tof = pp.x*cst::DAY, dep = ci.dep0 + pp.y*cst::DAY;
          if (tof>ci.tof0 && tof<ci.tof1 && dep>ci.dep0 && dep<ci.dep1) jeu.interp_choisir(dep, tof);
        }
        ImPlot::EndPlot();
      }
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("d", ImVec2(0, -8), true);
    ImGui::TextUnformatted("LE TRANSFERT CHOISI");
    ImGui::Separator();
    if (!ci.choisie) ImGui::TextDisabled("Clique une case de la carte.");
    else {
      ImGui::Text("C3 depart       %8.1f km2/s2", ci.c3);
      ImGui::Text("v_inf arrivee   %8.2f km/s", ci.vinf_arr/1000);
      ImGui::Text("dv TMI (parking)%8.0f m/s%s", ci.dv_tmi, ci.assistance?" (assistance)":"");
      if (survol) ImGui::TextDisabled("survol : pas de mise en orbite (on passe au plus pres)");
      else ImGui::Text("dv insertion    %8.0f m/s%s", ci.dv_insertion, jeu.recherche_faite("aero")?" (aerocapt.)":"");
      ImGui::Text("dv TOTAL        %8.0f m/s", ci.dv_total);
      if (!ci.bilan.fits_mass && ci.choisie)
        ImGui::TextColored(ImVec4(0.95f,0.5f,0.35f,1), "! %s", ci.bilan.why.c_str());
      ImGui::Separator();
      if (survol) {
        bool as=ci.assistance;
        if (ImGui::Checkbox("tour a ASSISTANCES gravitationnelles (V-E-E-J-S)", &as)) { ci.assistance=as; jeu.interp_choisir(ci.t_dep, ci.tof); }
        ImGui::SameLine(); bouton_memo("?##as","Assistances : le mur du C3", jeu.memo_titan());
        ImGui::TextDisabled("  reduit le dv de depart de 47 %% (mesure, t01_veega) mais +2-3 ans.");
      }
      ImGui::TextUnformatted("MOTEUR");
      for (int m=0;m<(int)mission::engines().size();++m)
        if (ImGui::RadioButton(mission::engines()[m].eng.id.c_str(), &ci.moteur, m)) jeu.interp_recalculer();
      ImGui::TextUnformatted("ETAGES (empilement identique)");
      int ne=ci.n_etages; ImGui::SetNextItemWidth(160);
      if (ImGui::SliderInt("##netages", &ne, 1, 3, "%d etage(s)")) { ci.n_etages=ne; jeu.interp_recalculer(); }
      ImGui::SameLine(); bouton_memo("?##etg","Le mur du mono-etage", jeu.memo_vehicule());
      ImGui::TextDisabled("  +d'etages = franchit le mur du mono-etage (masse /2 sur missions lourdes),");
      ImGui::TextDisabled("  mais +cout moteur et un risque de separation par largage. Optimum = a trouver.");
      float mg=(float)ci.marge_dv;
      if (ImGui::SliderFloat("marge", &mg, 0, 150, "%.0f m/s")) { ci.marge_dv=mg; jeu.interp_recalculer(); }
      ImGui::TextUnformatted("STRATEGIE DE CORRECTION (provisionnee) :");
      const char* tcm[4]={"aucune (87778 km)","TCM precoce (5195 km)","TCM tardive (539 km)","les DEUX (121 km)"};
      if (ImGui::Combo("##tcm", &ci.strategie_tcm, tcm, 4)) jeu.interp_recalculer();
      ImGui::SameLine(); bouton_memo("?##disp","Modele de dispersion", jeu.memo_modele_disp());
      if (survol) { bool col2=ci.collecteur; if (ImGui::Checkbox("collecteur d'echantillons (+80 kg, +6 M$)", &col2)) { ci.collecteur=col2; jeu.interp_recalculer(); } }
      ImGui::Separator();
      const auto& b = ci.bilan;
      ImGui::Text("masse decollage %8.0f kg  (%d etage%s)", b.m0_kg, ci.n_etages, ci.n_etages>1?"s":"");
      ImGui::Text("cout total      %8.1f M$ (budget %.0f)", b.cost_total, ac->spec.budget_musd);
      ImGui::Text("calendrier      %8.1f mois", b.schedule_months);
      ImGui::Text("P(succes)       %8.1f %%", 100*b.p_success);
      if (!b.fits_mass) ImGui::TextColored(ImVec4(0.95f,0.35f,0.3f,1), "aucun lanceur ne souleve.");
      ImGui::Separator();
      if (!jeu.vinterp.commis)
        if (ImGui::Button("COMMIT INTERPLANETAIRE", ImVec2(-1, 42))) { if (jeu.interp_commit()) ecran = Ecran::VolInterp; }
    }
    ImGui::EndChild();
  }

  // -------------------------------------------------------------------------
  // SALLE DE VOL INTERPLANETAIRE : croisiere temps reel, TCM, corridor
  // -------------------------------------------------------------------------
  void e_vol_interp() {
    auto& v = jeu.vinterp;
    if (!v.commis) { ImGui::Dummy(ImVec2(0,30)); centre("Choisis un transfert et COMMIT (onglet CARTE).", 1.2f); return; }
    ImGui::BeginChild("g", ImVec2(360, -8), true);
    ImGui::SetWindowFontScale(1.15f); ImGui::TextUnformatted("CROISIERE"); ImGui::SetWindowFontScale(1.0f);
    const double frac = (v.t - v.t_dep) / std::max(1.0, v.tof);
    ImGui::Text("depart T+%.0f j / arrivee T+%.0f j", (v.t-v.t_dep)/cst::DAY, v.tof/cst::DAY);
    ImGui::ProgressBar((float)std::min(1.0,frac), ImVec2(-1,0), "trajet");
    ImGui::Separator();
    if (!v.fini) {
      const char* ph = v.phase==0?"vers TCM-1 (depart +30 j)":v.phase==1?"vers TCM-2 (arrivee -30 j)":"approche finale";
      ImGui::TextWrapped("Phase : %s", ph);
      float warp=(float)v.tr_warp; ImGui::SetNextItemWidth(-1);
      if (ImGui::SliderFloat("##w", &warp, 43200, 864000, "warp x%.0f")) v.tr_warp=warp;
      if (ImGui::Button(v.tr_actif?"|| PAUSE":"|> REPRENDRE", ImVec2(170,28))) v.tr_actif=!v.tr_actif;
      ImGui::Separator();
      ImGui::Text("ellipse 3-sigma courante : %.0f km", v.ellipse_km);
      ImGui::SameLine(); bouton_memo("?##d","Modele de dispersion", jeu.memo_modele_disp());
      if (v.phase==0 || v.phase==1) {
        if (ImGui::Button("EXECUTER LA TCM (-12 m/s)", ImVec2(-1, 34))) jeu.interp_faire_tcm();
        if (ImGui::Button("passer cette fenetre", ImVec2(-1, 26))) jeu.interp_passer_tcm();
      }
      ImGui::Text("TCM cumulees : %.0f m/s", v.dv_tcm);
    }
    ImGui::Separator();
    ImGui::TextUnformatted("TELEMETRIE");
    for (auto& l : v.flux) ImGui::TextWrapped("%s", l.c_str());
    ImGui::EndChild();
    ImGui::SameLine();
    // vue heliocentrique
    ImGui::BeginChild("map", ImVec2(ImGui::GetWindowWidth()*0.62f, -8), true);
    if (v.fini) { e_verdict_interp(); }
    else {
      ImGui::TextUnformatted("VUE HELIOCENTRIQUE (plan ecliptique, UA)");
      if (ImPlot::BeginPlot("##h", ImVec2(-1,-1), ImPlotFlags_Equal)) {
        ImPlot::SetupAxes("x [UA]","y [UA]");
        const auto tt = jeu.actif() ? jeu.actif()->type : app::TypeContrat::VolMars;
        const double lim = tt==app::TypeContrat::VolTitan ? 10.5 : tt==app::TypeContrat::VolComete ? 5.5 : 2.0;
        ImPlot::SetupAxesLimits(-lim,lim,-lim,lim, ImPlotCond_Once);
        double sx[1]={0}, sy[1]={0};
        ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 8, ImVec4(0.95f,0.85f,0.35f,1));
        ImPlot::PlotScatter("Soleil", sx, sy, 1);
        if (!v.ter_x.empty()){ ImPlot::SetNextLineStyle(ImVec4(0.4f,0.6f,0.9f,0.8f),1.5f); ImPlot::PlotLine("Terre", v.ter_x.data(),v.ter_y.data(),(int)v.ter_x.size()); }
        if (!v.cib_x.empty()){ ImPlot::SetNextLineStyle(ImVec4(0.9f,0.5f,0.35f,0.8f),1.5f); ImPlot::PlotLine("cible", v.cib_x.data(),v.cib_y.data(),(int)v.cib_x.size()); }
        if (!v.arc_x.empty()){ ImPlot::SetNextLineStyle(ImVec4(0.95f,0.85f,0.4f,0.9f),2.f); ImPlot::PlotLine("croisiere", v.arc_x.data(),v.arc_y.data(),(int)v.arc_x.size()); }
        // marqueur du vaisseau : interpolation sur l'arc par le temps
        if (!v.arc_t.empty()) {
          int idx=0; for (size_t i=1;i<v.arc_t.size();++i) if (v.arc_t[i]<=v.t) idx=(int)i;
          double mx[1]={v.arc_x[idx]}, my[1]={v.arc_y[idx]};
          ImPlot::SetNextMarkerStyle(ImPlotMarker_Diamond, 9, ImVec4(1,1,1,1), 2.f);
          ImPlot::PlotScatter("sonde", mx, my, 1);
        }
        ImPlot::EndPlot();
      }
    }
    ImGui::EndChild();
  }

  void e_verdict_interp() {
    auto& v = jeu.vinterp;
    ImGui::SetWindowFontScale(1.6f);
    ImGui::TextColored(v.ok?ImVec4(0.35f,0.85f,0.45f,1):ImVec4(0.95f,0.35f,0.3f,1), v.ok?"+++ MISSION REUSSIE +++":"--- MISSION PERDUE ---");
    ImGui::SetWindowFontScale(1.0f); ImGui::Separator();
    ImGui::TextColored(ImVec4(0.9f,0.9f,0.7f,1), "POURQUOI :");
    ImGui::PushTextWrapPos(0); ImGui::TextWrapped("%s", v.pourquoi.c_str());
    ImGui::Separator(); ImGui::TextUnformatted("MESURES :");
    ImGui::TextUnformatted(v.verdict.c_str());
    ImGui::Separator(); ImGui::TextUnformatted(v.postmortem.c_str()); ImGui::PopTextWrapPos();
    ImGui::SameLine(); bouton_memo("? modele de dispersion", "Ce qui est exact, ce qui est declare", jeu.memo_modele_disp());
    ImGui::Separator();
    // le corridor plan-B (pour Mars) avec la valeur mesuree
    if (jeu.actif() && jeu.actif()->type==app::TypeContrat::VolMars) {
      corridor.tcm_choice = v.strategie_tcm;
      ImGui::TextUnformatted("LE CORRIDOR DU PLAN-B (ellipse effectivement realisee) :");
      dessiner_corridor_inline(v.ellipse_km);
    }
    if (ImGui::Button("RETOUR AU BUREAU", ImVec2(-1, 36))) ecran = Ecran::Bureau;
  }

  void dessiner_corridor_inline(double ellipse_km) {
    if (ImPlot::BeginPlot("##bp", ImVec2(-1, 300), ImPlotFlags_Equal)) {
      ImPlot::SetupAxes("B.T [km]","B.R [km]");
      const double cxx=corridor.bt_aim_km, cyy=corridor.br_aim_km;
      const double S=std::fmax(1.15*(corridor.b_max_km-corridor.b_min_km), 1.6*ellipse_km);
      ImPlot::SetupAxesLimits(cxx-S,cxx+S,cyy-S,cyy+S, ImPlotCond_Always);
      const int N=200; static std::vector<double> x(N+1),y(N+1);
      auto cerc=[&](double r){ for(int i=0;i<=N;++i){double a=2*3.14159265*i/N; x[i]=r*cos(a); y[i]=r*sin(a);} };
      cerc(corridor.b_min_km); ImPlot::SetNextLineStyle(ImVec4(0.95f,0.45f,0.25f,1),2.f); ImPlot::PlotLine("atmosphere", x.data(),y.data(),N+1);
      cerc(corridor.b_max_km); ImPlot::SetNextLineStyle(ImVec4(0.35f,0.65f,0.95f,1),2.f); ImPlot::PlotLine("budget insertion", x.data(),y.data(),N+1);
      const double th=std::atan2(corridor.br_aim_km,corridor.bt_aim_km), a3=ellipse_km, b3=ellipse_km*0.22;
      for(int i=0;i<=N;++i){ double u=2*3.14159265*i/N, ex=a3*cos(u), ey=b3*sin(u);
        x[i]=cxx+ex*cos(th)-ey*sin(th); y[i]=cyy+ex*sin(th)+ey*cos(th); }
      const bool tient = ellipse_km < 0.5*(corridor.b_max_km-corridor.b_min_km);
      ImPlot::SetNextLineStyle(tient?ImVec4(0.35f,0.85f,0.45f,1):ImVec4(0.95f,0.35f,0.3f,1),2.5f);
      ImPlot::PlotLine("dispersion 3-sigma", x.data(),y.data(),N+1);
      ImPlot::EndPlot();
    }
  }

  // -------------------------------------------------------------------------
  void e_etude() {
    bool ok=false; for (auto& c : jeu.contrats) if (c.type==app::TypeContrat::EtudeMars && c.accepte) ok=true;
    if (!ok) { ImGui::Dummy(ImVec2(0,30)); centre("Signe le contrat d'etude (CONTRATS).", 1.2f); return; }
    auto& E = jeu.etude;
    ImGui::TextWrapped("CONTRAT D'ANALYSE : tu ne lances rien. Tu produis DEUX documents "
                       "et tu es paye a la livraison (9 M$). Aucun risque - juste du calcul.");
    ImGui::Separator();
    ImGui::BeginChild("g", ImVec2(ImGui::GetWindowWidth()*0.52f, -8), true);
    ImGui::TextUnformatted("LIVRABLE 1 - PORKCHOP TERRE->MARS");
    ImGui::TextWrapped("Le calcul se paie (temps de calcul). Grille %dx%d = %.1f M$.", E.n_dep, E.n_tof, E.cout_calcul());
    if (!E.calculee && !E.calcul_en_cours) { if (ImGui::Button("ACHETER LE CALCUL", ImVec2(-1,32))) jeu.etude_calculer_porkchop(); }
    else if (E.calcul_en_cours) ImGui::TextColored(ImVec4(0.95f,0.75f,0.3f,1), "calcul...");
    if (E.calculee) {
      ImGui::Text("C3 min %.2f km2/s2 | depart %s | transit %.0f j", E.best_c3, E.best_dep_iso.c_str(), E.best_tof/cst::DAY);
      ImGui::TextDisabled("fenetre suivante ~780 j plus tard (periode synodique).");
      if (ImPlot::BeginPlot("##pk", ImVec2(-1,-1))) {
        ImPlot::SetupAxes("transit [j]","depart [j depuis 2026-06-01]");
        const double d1=(E.dep1-E.dep0)/cst::DAY, t0j=E.tof0/cst::DAY, t1j=E.tof1/cst::DAY;
        ImPlot::PushColormap(ImPlotColormap_Viridis);
        ImPlot::PlotHeatmap("C3", E.grille_c3.data(), E.n_dep, E.n_tof, 8.0, 60.0, nullptr, ImPlotPoint(t0j,0), ImPlotPoint(t1j,d1));
        ImPlot::PopColormap();
        double bx[1]={E.best_tof/cst::DAY}, by[1]={(E.best_dep-E.dep0)/cst::DAY};
        ImPlot::SetNextMarkerStyle(ImPlotMarker_Cross,9,ImVec4(1,1,1,1),2.5f); ImPlot::PlotScatter("min", bx,by,1);
        ImPlot::EndPlot();
      }
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("d", ImVec2(0, -8), true);
    ImGui::TextUnformatted("LIVRABLE 2 - CORRIDOR DU PLAN-B (mesure, m01_corridor)");
    for (int i=0;i<(int)corridor.tcm.size();++i){ if (ImGui::RadioButton(corridor.tcm[i].name, &E.tcm_choix, i)) {} if (i<(int)corridor.tcm.size()-1) ImGui::SameLine(); }
    const auto& T = corridor.tcm[E.tcm_choix];
    const bool tient = T.ellipse_km < 0.5*(corridor.b_max_km-corridor.b_min_km);
    ImGui::TextColored(tient?ImVec4(0.35f,0.85f,0.45f,1):ImVec4(0.95f,0.35f,0.3f,1),
                       "ellipse %8.0f km | P %3.0f %% | %s", T.ellipse_km, 100*T.p_success, tient?"TIENT":"DEBORDE");
    dessiner_corridor_inline(T.ellipse_km);
    E.corridor_vu = true;
    ImGui::Separator();
    if (!E.livree) { if (ImGui::Button("LIVRER L'ETUDE (9 M$)", ImVec2(-1, 36))) jeu.etude_livrer(); }
    else ImGui::TextColored(ImVec4(0.35f,0.85f,0.45f,1), "ETUDE LIVREE.");
    ImGui::EndChild();
  }
};

} // namespace fen::ui
