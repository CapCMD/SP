// ui/station_ecran.hpp — L'ACCUEIL : à bord de l'ISS, module Novellus.
//
// C'est l'écran d'arrivée du jeu (pas la carte) : première personne dans la
// station, on se déplace, on s'approche d'un poste, on l'ouvre. La carte du
// système solaire est un MODE qu'on atteint d'ici par [M].
// Référence : docs/reference_solar_system_map/ref_iss.png et ref_poste.png
//
// ZÉRO rendu ici : le monde UE dessine la station. En revanche l'ENTRÉE vit
// ici — l'overlay Slate capte tout le clavier et la souris, le pawn UE ne
// reçoit rien directement. On publie donc les commandes dans le pont.
#pragma once
#include <cmath>
#include <cstdio>
#include "imgui.h"
#include "app/bridge_flags.hpp"
#include "app/jeu.hpp"

namespace fen::ui {

// Ce que le joueur demande depuis la station.
enum class ActionStation { Rien = 0, Carte, Menu, Sauver };

// LES 8 POSTES [référence : ISS_POSTS]. Chaque poste est une étape du cycle de
// mission, logée dans un module réel de l'ISS, avec sa couleur d'accent.
struct PosteDef {
  const char* id;
  const char* label;
  const char* sub;
  ImU32 accent;
};
inline const PosteDef* postes_def(int& n) {
  static const PosteDef P[] = {
    {"agence",        "AGENCE",        "ZVEZDA . DIRECTION",         IM_COL32(255, 189,  87, 255)},
    {"analyse",       "ANALYSE",       "ZARYA . ARCHIVES",           IM_COL32(189, 158, 255, 255)},
    {"operations",    "OPERATIONS",    "TRANQUILITY . SYSTEMES",     IM_COL32(117, 230, 143, 255)},
    {"controle",      "CONTROLE",      "DESTINY . CONTROLE DE VOL",  IM_COL32(102, 209, 255, 255)},
    {"conception",    "CONCEPTION",    "COLUMBUS . CONCEPTION",      IM_COL32( 82, 219, 204, 255)},
    {"planification", "PLANIFICATION", "KIBO . TRAJECTOIRE",         IM_COL32(112, 179, 255, 255)},
    {"observation",   "COUPOLE",       "TRANQUILITY . OBSERVATION",  IM_COL32(140, 204, 255, 255)},
    {"novellus",      "NOVELLUS",      "QG . ORDINATEUR PRINCIPAL",  IM_COL32(235, 143, 235, 255)},
  };
  n = 8;
  return P;
}

// Le modèle 3D réel étant chargé, les postes sont regroupés dans NOVELLUS et
// alignés le long du couloir (même disposition que le jeu de référence).
inline void publier_postes() {
  auto& B = app::g_render_bridge;
  int n = 0;
  postes_def(n);
  const double base_x = 19.68, base_y = -3.67, base_z = -1.10;   // Novellus
  for (int i = 0; i < n && i < app::RenderBridge::PostSnap::MAX; ++i) {
    auto& it = B.posts.items[i];
    it.x = static_cast<float>(base_x + (i - (n - 1) * 0.5) * 1.7);
    it.y = static_cast<float>(base_y);
    it.z = static_cast<float>(base_z);
    it.radius_m = 1.5f;
  }
  B.posts.n = n;
}

// Panneau holographique d'un poste (style de la référence : liseré d'accent,
// titre + sous-titre, pastille EN LIGNE, lignes clé -> valeur, note de bas).
inline void panneau_poste(const app::Jeu& jeu, int poste, bool& ouvert) {
  int n = 0;
  const PosteDef* P = postes_def(n);
  if (poste < 0 || poste >= n) { ouvert = false; return; }
  const PosteDef& D = P[poste];

  const ImVec2 disp = ImGui::GetIO().DisplaySize;
  const ImVec2 taille(std::min(600.0f, disp.x * 0.55f), std::min(290.0f, disp.y * 0.45f));
  ImGui::SetNextWindowPos(ImVec2(disp.x * 0.5f, disp.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(taille, ImGuiCond_Always);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.06f, 0.08f, 0.13f, 0.94f));
  ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(D.accent));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0f);
  ImGui::Begin("##poste", nullptr,
               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
               ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);

  ImGui::SetWindowFontScale(1.5f);
  ImGui::TextUnformatted(D.label);
  ImGui::SetWindowFontScale(1.0f);
  ImGui::TextDisabled("%s", D.sub);
  ImGui::SameLine(taille.x - 120.0f);
  ImGui::TextColored(ImVec4(0.55f, 0.85f, 0.95f, 1), "* EN LIGNE");
  ImGui::Separator();

  auto kv = [&](const char* k, const char* v) {
    ImGui::TextUnformatted(k);
    ImGui::SameLine(taille.x - 30.0f - ImGui::CalcTextSize(v).x);
    ImGui::TextUnformatted(v);
  };
  char b[64];
  const auto& A = jeu.agence;
  const int vols = A.reussites + A.echecs;
  // Contenu VIVANT tiré du modèle de jeu (lecture seule), poste par poste.
  if (std::strcmp(D.id, "agence") == 0) {
    kv("PROGRAMME", A.nom.c_str());
    std::snprintf(b, sizeof b, "%.1f M$", A.tresorerie);   kv("TRESORERIE", b);
    std::snprintf(b, sizeof b, "T+%.0f mois", A.mois);     kv("CALENDRIER", b);
    kv("MODE D'AIDE", A.mode == app::ModeAide::Pro ? "PRO" : "NORMAL");
    ImGui::Dummy(ImVec2(0, 6));
    ImGui::ProgressBar((float)A.confiance, ImVec2(-1, 6), "");
    ImGui::TextDisabled("Confiance ARES");
  } else if (std::strcmp(D.id, "analyse") == 0) {
    std::snprintf(b, sizeof b, "%d", vols);          kv("VOLS BOUCLES", b);
    std::snprintf(b, sizeof b, "%d", A.reussites);   kv("REUSSITES", b);
    std::snprintf(b, sizeof b, "%d", A.echecs);      kv("ECHECS", b);
    std::snprintf(b, sizeof b, "%.1f Gbit", jeu.donnees_gbit);      kv("DONNEES", b);
    std::snprintf(b, sizeof b, "%.1f kg", jeu.echantillons_kg);     kv("ECHANTILLONS", b);
    ImGui::Dummy(ImVec2(0, 6));
    ImGui::ProgressBar(vols > 0 ? (float)A.reussites / vols : 0.0f, ImVec2(-1, 6), "");
    ImGui::TextDisabled("Taux de reussite");
  } else if (std::strcmp(D.id, "operations") == 0) {
    std::snprintf(b, sizeof b, "%d", jeu.relais_geo);         kv("RELAIS GEO", b);
    std::snprintf(b, sizeof b, "%d", jeu.orbiteurs_mars);     kv("ORBITEURS MARS", b);
    std::snprintf(b, sizeof b, "%d", jeu.sondes_lointaines);  kv("SONDES LOINT.", b);
    std::snprintf(b, sizeof b, "%.1f Gbit/mo", jeu.revenu_mensuel_gbit());
    kv("REVENU FLOTTE", b);
    kv("ORBITE", "LEO 418 km");
  } else if (std::strcmp(D.id, "planification") == 0) {
    if (const app::Contrat* c = jeu.actif()) {
      kv("CONTRAT", c->titre.c_str());
      kv("CLIENT", c->client.c_str());
      std::snprintf(b, sizeof b, "%.0f M$", c->prime_succes); kv("PRIME", b);
    } else {
      std::snprintf(b, sizeof b, "%d offres", (int)jeu.contrats.size());
      kv("APPELS D'OFFRE", b);
      if (!jeu.contrats.empty()) kv("PROCHAIN", jeu.contrats.front().titre.c_str());
    }
  } else if (std::strcmp(D.id, "novellus") == 0) {
    kv("MODULE", "NOVELLUS");
    kv("QG", A.nom.c_str());
    kv("ORDINATEUR", "EN LIGNE");
    kv("CARTE", "SYSTEME SOLAIRE [M]");
  } else {
    ImGui::TextDisabled("Poste en cours de portage.");
  }

  ImGui::SetCursorPosY(taille.y - 28.0f);
  ImGui::Separator();
  ImGui::TextDisabled("[ ECHAP ] FERMER");
  ImGui::End();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor(2);

  if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ouvert = false;
}

// L'écran STATION. `poste_ouvert` = index du poste ouvert (-1 = aucun).
inline ActionStation ecran_station(app::Jeu& jeu, int& poste_ouvert) {
  auto& B = app::g_render_bridge;
  ActionStation action = ActionStation::Rien;
  publier_postes();

  const bool panneau = poste_ouvert >= 0;
  auto& In = B.station_in;

  // --- entrée : le pawn UE ne reçoit rien, on lui transmet tout -------------
  if (!panneau) {
    const bool z = ImGui::IsKeyDown(ImGuiKey_W) || ImGui::IsKeyDown(ImGuiKey_UpArrow);
    const bool s = ImGui::IsKeyDown(ImGuiKey_S) || ImGui::IsKeyDown(ImGuiKey_DownArrow);
    const bool q = ImGui::IsKeyDown(ImGuiKey_A) || ImGui::IsKeyDown(ImGuiKey_LeftArrow);
    const bool d = ImGui::IsKeyDown(ImGuiKey_D) || ImGui::IsKeyDown(ImGuiKey_RightArrow);
    In.fwd = (z ? 1.0f : 0.0f) - (s ? 1.0f : 0.0f);
    In.right = (d ? 1.0f : 0.0f) - (q ? 1.0f : 0.0f);
    In.up = (ImGui::IsKeyDown(ImGuiKey_Space) ? 1.0f : 0.0f) -
            (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) ? 1.0f : 0.0f);
    In.boost = ImGui::IsKeyDown(ImGuiKey_LeftShift);
    // REGARD : glisser à la souris (le curseur reste visible et libre — le
    // verrouillage plein écran viendra avec la passe d'ambiance).
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
      const ImVec2 dd = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f);
      ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
      In.look_dx = In.look_dx.load() + dd.x;
      In.look_dy = In.look_dy.load() + dd.y;
    }
  } else {
    In.fwd = In.right = In.up = 0.0f;
    In.boost = false;
  }

  // --- HUD d'ambulation -----------------------------------------------------
  const ImVec2 disp = ImGui::GetIO().DisplaySize;
  ImDrawList* dl = ImGui::GetBackgroundDrawList();
  const ImU32 gris = IM_COL32(190, 205, 225, 190);
  const ImU32 pale = IM_COL32(150, 170, 195, 150);

  char barre[220];
  std::snprintf(barre, sizeof barre,
                "%s   |   ZQSD/WASD se deplacer   |   GLISSER : regarder   |   "
                "E : poste   |   M : carte   |   F5 : sauvegarder",
                jeu.agence.nom.c_str());
  dl->AddText(ImVec2(18.0f, 14.0f), pale, barre);
  dl->AddText(ImVec2(18.0f, disp.y - 30.0f), gris, "CARTE  [ M ]");

  // invite contextuelle : UE mesure la proximité, on affiche
  const int proche = B.station_out.near_post.load();
  int nposte = 0;
  const PosteDef* P = postes_def(nposte);
  if (!panneau && proche >= 0 && proche < nposte) {
    char inv[96];
    std::snprintf(inv, sizeof inv, "[ E ]  OUVRIR  --  %s", P[proche].label);
    const ImVec2 sz = ImGui::CalcTextSize(inv);
    const ImVec2 c(disp.x * 0.5f - sz.x * 0.5f, disp.y * 0.78f);
    dl->AddRectFilled(ImVec2(c.x - 16, c.y - 8), ImVec2(c.x + sz.x + 16, c.y + sz.y + 8),
                      IM_COL32(10, 14, 20, 200), 3.0f);
    dl->AddRect(ImVec2(c.x - 16, c.y - 8), ImVec2(c.x + sz.x + 16, c.y + sz.y + 8),
                P[proche].accent, 3.0f, 0, 1.5f);
    dl->AddText(c, IM_COL32(235, 245, 255, 255), inv);
    if (ImGui::IsKeyPressed(ImGuiKey_E, false)) poste_ouvert = proche;
  }

  if (!B.station_out.ready.load())
    dl->AddText(ImVec2(disp.x * 0.5f - 150.0f, disp.y * 0.5f), gris,
                "Chargement de la station...");

  // --- panneau de poste ----------------------------------------------------
  if (panneau) {
    bool ouvert = true;
    panneau_poste(jeu, poste_ouvert, ouvert);
    if (!ouvert) poste_ouvert = -1;
  } else {
    if (ImGui::IsKeyPressed(ImGuiKey_M, false)) action = ActionStation::Carte;
    if (ImGui::IsKeyPressed(ImGuiKey_F5, false)) action = ActionStation::Sauver;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) action = ActionStation::Menu;
  }
  return action;
}

} // namespace fen::ui
