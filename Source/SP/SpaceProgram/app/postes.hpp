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
//
// ═══ ET DEPUIS LE 2026-07-27, ILS Y SONT VRAIMENT ═══
// Le sous-titre NOMME le module depuis le premier jour, mais les huit postes
// étaient alignés le long du couloir, à 1,7 m d'intervalle, tous à partir du
// point d'apparition : la station était un décor de panneaux flottants, pas un
// lieu. `x,y,z` (repère STATION, mètres — le même que NOVELLUS_OEIL_M) les pose
// DANS le module que le sous-titre annonce.
//
// LES POSITIONS SONT MESURÉES, pas choisies : `Tools/diag_iss_modules.py` lit les
// 310 meshes du modèle intérieur et rend, par module, la MÉDIANE des centres —
// robuste à l'intrus isolé (`Cupola_Int_Glass` est à 16 m des six autres pièces de
// la cupola et faussait à lui seul le centre de sa boîte englobante). Les sigles
// de l'asset ne sont pas ceux du GDD : Destiny y est `US_Lab_*`, Kibo `JPM_*`.
//
// ═══ DEUX MODULES N'EXISTENT PAS DANS L'ASSET ═══ (déclaré [GDD 6.8])
// `ISS_Internal` ne couvre que le SEGMENT AMÉRICAIN : de BEAM (arrière) à Kibo.
// ZVEZDA et ZARYA — le segment russe — n'y sont pas. AGENCE et ANALYSE restent
// donc dans NOVELLUS, comme les huit l'étaient hier : leur sous-titre garde le
// module visé, ce n'est pas une régression, c'est le statu quo pour deux postes
// sur huit pendant que les six autres rejoignent leur module.
//
// NOVELLUS est le module FICTIF du jeu [GDD 11] : dans ce modèle, c'est la copie
// de Kibo posée en avant de Node 2 (les meshes `JPM_*_001`, axe principal,
// x ≈ 11,8 à 22,8 m) — là où le jeu de référence plaçait déjà son point
// d'apparition.
struct PosteDef {
  const char* id;
  const char* label;
  const char* sub;
  CouleurAccent accent;
  double x, y, z;          // repère station, mètres
};

inline const PosteDef* postes_def(int& n) {
  static const PosteDef P[] = {
    // module ABSENT de l'asset (segment russe) -> reste dans NOVELLUS, échelonné
    // le long de son axe (le module va de x = 11,8 à 22,8 m)
    {"agence",        "AGENCE",        "ZVEZDA . DIRECTION",         {255, 189,  87},
     16.30, -3.67, -1.20},
    {"analyse",       "ANALYSE",       "ZARYA . ARCHIVES",           {189, 158, 255},
     12.90, -3.67, -1.20},
    // TRANQUILITY : le hub de Node 3 (six panneaux `Node3_Int_Hub_*`, tous y=+7,17)
    {"operations",    "OPERATIONS",    "TRANQUILITY . SYSTEMES",     {117, 230, 143},
    -18.20,  7.17, -1.43},
    // DESTINY : `US_Lab_Int_MainFrame`, l'axe du laboratoire américain
    {"controle",      "CONTROLE",      "DESTINY . CONTROLE DE VOL",  {102, 209, 255},
     -7.47, -3.64, -1.45},
    // COLUMBUS : à tribord de Node 2
    {"conception",    "CONCEPTION",    "COLUMBUS . CONCEPTION",      { 82, 219, 204},
      7.14,-12.57, -1.46},
    // KIBO : à bâbord de Node 2 (`JPM_*`, la copie transversale)
    {"planification", "PLANIFICATION", "KIBO . TRAJECTOIRE",         {112, 179, 255},
      7.13,  7.13, -1.40},
    // LA COUPOLE : au port NADIR de Node 3, sous le hub. C'est le seul poste d'où
    // l'on voit la Terre — le monde rend à bord depuis le 2026-07-27, et la cupola
    // regarde le nadir en permanence (attitude LVLH).
    {"observation",   "COUPOLE",       "TRANQUILITY . OBSERVATION",  {140, 204, 255},
    -18.19,  7.16, -5.10},
    // NOVELLUS : le module fictif du jeu, au point d'apparition du joueur
    {"vigie",         "VIGIE",         "NOVELLUS . POSTE ARCHITECTE", {235, 143, 235},
     19.68, -3.67, -1.10},
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

// PORTÉE D'UN POSTE : le rayon dans lequel l'invite « [E] OUVRIR » s'allume.
// 1,5 m, inchangé. La tentation était de l'ouvrir maintenant que les postes sont
// dans des modules distants de dizaines de mètres — mais TROIS d'entre eux
// partagent encore NOVELLUS (le segment russe manque à l'asset), et ce module ne
// fait que 11 m : à 2 m de portée leurs zones se recouvraient. L'oracle
// « les portées ne se recouvrent pas » l'a dit tout de suite.
inline constexpr double POSTE_PORTEE_M = 1.5;

// Les postes sont publiés à leur position RÉELLE, dans leur module (voir
// `postes_def`). UE n'a qu'à tester la proximité et republier `near_post`.
inline void publier_postes() {
  auto& B = g_render_bridge;
  int n = 0;
  const PosteDef* P = postes_def(n);
  for (int i = 0; i < n && i < RenderBridge::PostSnap::MAX; ++i) {
    auto& it = B.posts.items[i];
    it.x = static_cast<float>(P[i].x);
    it.y = static_cast<float>(P[i].y);
    it.z = static_cast<float>(P[i].z);
    it.radius_m = static_cast<float>(POSTE_PORTEE_M);
  }
  B.posts.n = n;
}

} // namespace fen::app
