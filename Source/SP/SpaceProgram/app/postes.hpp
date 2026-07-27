// app/postes.hpp — LES 8 POSTES DE TRAVAIL DE L'ISS.
//
// Extrait de ui/station_ecran.hpp lors du passage au rendu 100 % UE5 : ce
// fichier ne contient plus QUE la donnée (définition des postes + publication
// de leur position dans le pont). Le DESSIN du panneau holographique est
// désormais natif (UEBridge/SPHud.cpp), plus aucune dépendance ImGui ici.
//
// C++ pur : inclus des deux côtés de la frontière, JAMAIS d'entête UnrealEngine.
#pragma once
#include "app/bridge_flags.hpp"

namespace fen::app {

// Couleur d'accent d'un poste, en octets — le rendu la convertit dans SON
// espace (Slate attend du linéaire, ImGui attendait du sRGB : la donnée reste
// neutre et c'est le rendu qui traduit).
struct CouleurAccent { unsigned char r, g, b; };

// LES 8 POSTES [référence : docs/reference_solar_system_map/ref_poste.png].
// Chaque poste est une étape du cycle de mission, logée dans un module réel de
// l'ISS, avec sa couleur d'accent.
struct PosteDef {
  const char* id;
  const char* label;
  const char* sub;
  CouleurAccent accent;
};

inline const PosteDef* postes_def(int& n) {
  static const PosteDef P[] = {
    {"agence",        "AGENCE",        "ZVEZDA . DIRECTION",         {255, 189,  87}},
    {"analyse",       "ANALYSE",       "ZARYA . ARCHIVES",           {189, 158, 255}},
    {"operations",    "OPERATIONS",    "TRANQUILITY . SYSTEMES",     {117, 230, 143}},
    {"controle",      "CONTROLE",      "DESTINY . CONTROLE DE VOL",  {102, 209, 255}},
    {"conception",    "CONCEPTION",    "COLUMBUS . CONCEPTION",      { 82, 219, 204}},
    {"planification", "PLANIFICATION", "KIBO . TRAJECTOIRE",         {112, 179, 255}},
    {"observation",   "COUPOLE",       "TRANQUILITY . OBSERVATION",  {140, 204, 255}},
    {"vigie",         "VIGIE",         "NOVELLUS . POSTE ARCHITECTE", {235, 143, 235}},
  };
  n = 8;
  return P;
}

// ═══ LE MODÈLE DE NOVELLUS, en repère station ═══
// Repère station : X = axe du couloir, Z = haut, MÈTRES, origine au CENTRE du
// modèle. Valeurs RELEVÉES dans le jeu de référence (`target_span`,
// `novellus_pos`) — rien d'inventé. Elles vivent ici, en C++ pur, parce que les
// DEUX côtés de la frontière en ont besoin : le rendu pour poser le modèle et le
// pawn (UEBridge/SPStation.cpp), la session pour calculer la pose de caméra du
// handoff [GDD v1.2 17.4] — un seul chiffre, une seule source.
inline constexpr double STATION_ENVERGURE_M = 55.0;   // plus grande dimension du modèle
// Le point d'apparition du joueur : le module NOVELLUS (QG), et son cap.
inline constexpr double NOVELLUS_OEIL_M[3] = {19.68, -3.67, -1.10};
inline constexpr double NOVELLUS_YAW_RAD = 3.19;
inline constexpr double NOVELLUS_PITCH_RAD = -0.03;

// Le modèle 3D réel étant chargé, les postes sont regroupés dans NOVELLUS et
// alignés le long du couloir (même disposition que le jeu de référence). UE
// n'a qu'à tester la proximité et republier `near_post`.
inline void publier_postes() {
  auto& B = g_render_bridge;
  int n = 0;
  postes_def(n);
  const double base_x = NOVELLUS_OEIL_M[0], base_y = NOVELLUS_OEIL_M[1],
               base_z = NOVELLUS_OEIL_M[2];
  for (int i = 0; i < n && i < RenderBridge::PostSnap::MAX; ++i) {
    auto& it = B.posts.items[i];
    it.x = static_cast<float>(base_x + (i - (n - 1) * 0.5) * 1.7);
    it.y = static_cast<float>(base_y);
    it.z = static_cast<float>(base_z);
    it.radius_m = 1.5f;
  }
  B.posts.n = n;
}

} // namespace fen::app
