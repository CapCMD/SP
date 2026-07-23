// spr/MapView.hpp
//
// Canal ADDITIF de la "vue carte" (l'ecran principal de gameplay : la map du
// systeme solaire). Il ne touche NI au RenderSnapshot (le contrat physique->rendu,
// produit par le SEUL DataBridge) NI a la physique : c'est une couche de
// PRESENTATION composee par le point d'entree (composition root) a partir de
// nombres DEJA calcules par astro_core.
//
// Doctrine preservee (cf. docs/RENDER_ARCHITECTURE.md) :
//   - Les TRAJECTOIRES ici tracees sont des polylignes monde (double) que le point
//     d'entree echantillonne via l'ephemeride EXISTANTE (rv_to_elements /
//     elements_to_rv), exactement comme DataBridge::fill_orbit le fait pour le
//     vaisseau. Le RenderCore ne recalcule aucune physique : il trace des points.
//   - Le CONTROLE DU TEMPS est un simple etat edite par le HUD (mise en page) et
//     applique par l'app (choix du facteur de temps). Rien de physique ici.
//
// Passer un `const MapView*` a RenderCore::render active la vue carte ; nullptr
// laisse le rendu classique (l'app de demonstration GTO ne change pas).
#pragma once
#include <array>
#include <cstdint>
#include "spr/core/Math.hpp"
#include "spr/bridge/RenderSnapshot.hpp"   // Dvec3, RenderSnapshot::MAX_BODIES
#include "spr/rhi/Rhi.hpp"                  // MaterialHandle, INVALID_MATERIAL

namespace spr {

// Une trajectoire a tracer sur la carte : polyligne MONDE (double, m). `points`
// pointe vers des donnees possedees par l'appelant, valides le temps du render().
struct MapTrail {
  const Dvec3* points{nullptr};
  int          count{0};
  Vec3         color{0.40f, 0.60f, 1.0f};
  bool         closed{true};   // ellipse fermee : la scene referme la boucle
  // Alpha par sommet (fondu facon NASA Eyes : plein au corps, s'eteint le long de
  // la trace). nullptr = trace pleinement opaque. Meme taille que `points`.
  const float* alpha{nullptr};
  // Survol (facon NASA Eyes) : trajectoire epaissie et pleinement opaque quand le
  // corps est survole. Pose par l'app d'apres MapView::hover_body.
  bool         emphasized{false};
};

// Coquille translucide autour d'un corps (nuages Terre, atmosphere Venus) : une
// sphere legerement plus grande, texturee, ECLAIREE et alpha-blendee, avec sa
// PROPRE rotation (independante de la surface -> "de maniere relative"). Le
// materiau porte le drapeau MAT_CLOUDS (alpha = densite) ou MAT_ATMOSPHERE
// (alpha ~ constant), cf. shell.frag.
struct MapShell {
  int            body_index{-1};      // indice dans snapshot.bodies
  MaterialHandle material{INVALID_MATERIAL};
  float          radius_factor{1.01f};// rayon coquille = rayon corps * facteur
  Mat4           rot{};               // orientation + rotation propre (rempli/frame)
};

// Anneau plan (Saturne) : annulus dans le plan equatorial du corps, texture
// radiale (bandeau). L'etendue radiale est BAKEE dans le maillage/ring.frag
// (INNER_F..OUTER_F x rayon du corps). `rot` = alignement sur le pole du corps.
struct MapRing {
  int            body_index{-1};
  MaterialHandle material{INVALID_MATERIAL};
  Mat4           rot{};
};

// Sous-maillage TEXTURE de l'ISS exterieure (un par materiau GLB) : maillage a UV
// reelles + son materiau (carte baseColor). Possede par l'app. Tous les parts
// partagent la meme transformation d'ensemble (position/orientation/echelle ISS) ;
// seul le maillage + materiau changent -> rendu FIDELE (foil, modules, panneaux).
struct IssPart {
  MeshHandle     mesh{INVALID_MESH};
  MaterialHandle material{INVALID_MATERIAL};
  DrawStyle      style{DrawStyle::MeshTextured};
  Vec4           color{1, 1, 1, 1};
};

// Corps SUPPLEMENTAIRE (lune) : compose par l'app (position monde = parent +
// orbite declaree relative). Le pont ne tabule pas ces corps ; c'est de la
// PRESENTATION (elements orbitaux moyens publies). Rendu et etiquete comme un
// corps, mais avec un nom secondaire (gris).
struct MapBody {
  Dvec3          position{};
  double         radius{0.0};
  MaterialHandle material{INVALID_MATERIAL};
  Mat4           rot{};
  Vec3           color{0.7f, 0.7f, 0.7f};
  char           name[24]{};
};

// Regime temporel de la carte (edite par le HUD, applique par l'app).
//   RealTime = mode par defaut demande : 1 s simulee = 1 s reelle.
//   Les paliers d'avance rapide sont exprimes en "temps simule par seconde reelle".
enum class TimeMode : std::uint8_t {
  Pause = 0,
  RealTime,     // 1 s reelle -> 1 s simulee
  DayPerSec,    // 1 s reelle -> 1 jour
  WeekPerSec,   // 1 s reelle -> 1 semaine
  MonthPerSec,  // 1 s reelle -> 1 mois (~30.44 j)
  COUNT
};

inline const char* time_mode_name(TimeMode m) {
  switch (m) {
    case TimeMode::Pause:       return "PAUSE";
    case TimeMode::RealTime:    return "TEMPS REEL";
    case TimeMode::DayPerSec:   return "JOUR / s";
    case TimeMode::WeekPerSec:  return "SEMAINE / s";
    case TimeMode::MonthPerSec: return "MOIS / s";
    default:                    return "?";
  }
}

// Facteur de temps (s simulees par seconde reelle). 0 en pause.
inline double time_mode_warp(TimeMode m) {
  switch (m) {
    case TimeMode::Pause:       return 0.0;
    case TimeMode::RealTime:    return 1.0;
    case TimeMode::DayPerSec:   return 86400.0;
    case TimeMode::WeekPerSec:  return 604800.0;
    case TimeMode::MonthPerSec: return 2629746.0;   // 30.436875 jours moyens
    default:                    return 1.0;
  }
}

// Etat de controle du temps, partage entre l'app et le HUD.
struct TimeControl {
  TimeMode mode{TimeMode::RealTime};   // regime courant
  int      reverse{0};                 // 0 = avance, 1 = recule (paliers)
  // Saut ponctuel (boutons "+1 jour / +1 semaine / +1 mois"), en secondes
  // simulees. L'app l'applique puis le remet a zero (jamais accumule ici).
  double   step_request{0.0};
  // Demande "LIVE" (bouton) : l'app repositionne le temps sur l'instant reel puis
  // remet ce drapeau a false.
  bool     go_live{false};
  // Vrai si l'affichage suit l'instant reel (temps reel ET temps ~ maintenant) :
  // pose par l'app, lu par le HUD pour l'indicateur LIVE.
  bool     is_live{false};
};

// LA "vue carte" additive passee a RenderCore::render(). Sans possession.
struct MapView {
  MapView() {
    for (auto& m : body_rot) m = Mat4::identity();
    skybox_rot = Mat4::identity();
    iss_rot = Mat4::identity();
  }

  // --- trajectoires (orbites planetaires) -----------------------------------
  const MapTrail* trails{nullptr};
  int             trail_count{0};
  bool            show_trails{true};

  // --- lisibilite ------------------------------------------------------------
  // Taille ecran minimale des corps : rayon de RENDU = max(rayon_reel,
  // distance*body_min_size). 0 = rayon reel STRICT (l'utilisateur veut aucune mise
  // a l'echelle : a l'echelle du systeme les planetes sont des points, reperees
  // par leurs etiquettes projetees).
  float body_min_size{0.0f};
  bool  show_labels{true};        // etiquettes de corps projetees (HUD)

  // Rayon reel de PRESENTATION par corps (m), indexe comme snapshot.bodies. Le
  // noyau ne definit pas de rayon pour tous les corps (Mercure/Venus/Jupiter/Lune
  // -> 0) : l'app fournit ici les rayons reels sans toucher la physique. 0 =
  // utiliser le rayon du snapshot.
  std::array<double, RenderSnapshot::MAX_BODIES> body_radius{};

  // Orientation+rotation propre par corps (repere monde), indexe comme
  // snapshot.bodies. Compose par l'app (alignement du pole * rotation siderale).
  // Defaut identite (cf. constructeur).
  std::array<Mat4, RenderSnapshot::MAX_BODIES> body_rot{};

  // Remplacement de materiau par corps (textures reelles) : indexe comme
  // snapshot.bodies. INVALID_MATERIAL = choix par SurfaceType.
  std::array<MaterialHandle, RenderSnapshot::MAX_BODIES> body_material{};

  // Coquilles translucides (nuages/atmosphere), possedees par l'appelant.
  const MapShell* shells{nullptr};
  int             shell_count{0};

  // Anneaux (Saturne...), possedes par l'appelant.
  const MapRing*  rings{nullptr};
  int             ring_count{0};

  // Corps supplementaires (lunes), possedes par l'appelant.
  const MapBody*  extra_bodies{nullptr};
  int             extra_count{0};

  // Materiau de fond (Voie lactee) : sphere celeste texturee dessinee a l'infini.
  // INVALID_MATERIAL = pas de skybox (le starfield procedural reste le fond).
  MaterialHandle skybox_material{INVALID_MATERIAL};
  Mat4           skybox_rot{};    // orientation du fond (inclinaison galactique)

  // --- controle du temps (edite par le HUD, lu par l'app) --------------------
  TimeControl time{};
  bool        show_time_panel{true};

  // Requete de focus emise par le HUD (clic sur un corps) et consommee par l'app :
  // -2 = aucune, -1 = vue libre (Soleil), >=0 = indice du corps a suivre.
  int         focus_request{-2};

  // Survol (facon NASA Eyes) : indice du corps sous le curseur, calcule par l'app
  // (meme projection que le pick). Le HUD epaissit son rond et force son nom ;
  // l'app pose MapTrail::emphasized sur sa trajectoire. -1 = aucun.
  int         hover_body{-1};

  // --- marqueur ISS (QG cliquable, facon NASA Eyes) --------------------------
  // Position monde de l'ISS (l'app la calcule : position Terre + petit offset LEO).
  // Le HUD dessine un marqueur/label "ISS" cliquable ; le clic pose
  // enter_iss_request, que l'app consomme pour basculer dans l'interieur.
  bool        show_iss{false};
  Dvec3       iss_position{};
  bool        iss_occluded{false};   // vrai si la Terre masque l'ISS (marqueur cache)
  // Interaction facon NASA Eyes : 1er clic sur le marqueur -> GROS PLAN (le HUD pose
  // focus_iss_request ; l'app cadre la camera sur l'ISS comme sur une planete).
  // `iss_focused` (pose par l'app) = on est en gros plan -> le HUD affiche la fiche
  // station + le bouton ENTRER, qui pose enter_iss_request (bascule dans l'interieur).
  bool        focus_iss_request{false};
  bool        iss_focused{false};
  bool        enter_iss_request{false};

  // --- modele 3D EXTERIEUR de l'ISS (facultatif, fail-safe) ------------------
  // Maillage fusionne charge depuis le .glb (par l'app). INVALID_MESH = seul le
  // marqueur 2D est dessine (repli). Dessine a `iss_position` quand `show_iss`.
  MeshHandle  iss_mesh{INVALID_MESH};
  // Rendu TEXTURE (prioritaire sur iss_mesh) : sous-maillages par materiau, avec
  // leurs cartes baseColor. Si iss_part_count > 0, la scene dessine ces parts (avec
  // la transformation iss_rot/iss_scale/iss_position) au lieu du maillage gris.
  const IssPart* iss_parts{nullptr};
  int            iss_part_count{0};
  Mat4        iss_rot{};             // orientation du modele (rempli au ctor)
  double      iss_scale{1.0};        // unites du modele -> metres (taille reelle)
  double      iss_model_radius{1.0}; // rayon caracteristique du modele (unites modele)
  // Taille ecran minimale (comme body_min_size) : sans plancher l'ISS (~109 m) est
  // sous-pixel a l'echelle de la carte. Le plancher la garde visible pres de la
  // Terre ; elle retrouve sa taille reelle quand la camera s'en approche. 0 = reel.
  float       iss_min_size{0.0f};
};

} // namespace spr
