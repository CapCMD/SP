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

// Le modèle 3D réel étant chargé, les postes sont regroupés dans NOVELLUS et
// alignés le long du couloir (même disposition que le jeu de référence). UE
// n'a qu'à tester la proximité et republier `near_post`.
inline void publier_postes() {
  auto& B = g_render_bridge;
  int n = 0;
  postes_def(n);
  const double base_x = 19.68, base_y = -3.67, base_z = -1.10;   // Novellus
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
