// spr/core/Camera.hpp
//
// Camera de simulation spatiale. Deux exigences non triviales :
//
//  1) GRANDE ECHELLE. Le point regarde (`focus`) et la position de l'oeil sont
//     en DOUBLE, en metres, dans le repere inertiel du corps central. La matrice
//     de vue rendue place l'oeil a l'ORIGINE (rendu "camera-relative") : les
//     objets sont translates de (position_monde - oeil_monde) en double PUIS
//     rabaisses en float. C'est ce qui permet de tenir une planete a 1 UA et un
//     boulon a 1 m dans la meme scene sans scintillement de precision.
//
//  2) MODES. Cinq points de vue d'un vrai space-sim. Le mode ne change QUE la
//     facon de choisir le focus/att : la physique n'est jamais touchee.
#pragma once
#include "spr/core/Math.hpp"
#include "spr/bridge/RenderSnapshot.hpp"

namespace spr {

enum class CameraMode {
  Inertial,     // repere inertiel, focus = corps central, arcball libre
  BodyCentered, // suit un corps (planete/lune) qui se deplace
  Vehicle,      // camera poursuite du vaisseau
  Surface,      // proche d'une surface, near-clip minuscule, operations au sol
  Map,          // vue de dessus du systeme (plan de l'ecliptique)
  FirstPerson,  // premiere personne (interieur ISS) : focus = OEIL, yaw/pitch = regard
  COUNT
};
const char* camera_mode_name(CameraMode m);

class Camera {
 public:
  // --- etat regarde (monde, double) -----------------------------------------
  Dvec3  focus{};              // point regarde, m, monde
  double distance{2.0e7};      // m, distance oeil<->focus
  double yaw{0.0};             // rad, azimut autour de +Z ecliptique
  double pitch{0.35};          // rad, elevation ; clampe a +/-89 deg
  float  fov_deg{50.0f};

  CameraMode mode{CameraMode::Inertial};

  // --- etat premiere personne (impesanteur) ----------------------------------
  Dvec3  fp_vel{};             // vitesse de l'oeil (m/s), amortie -> glisse inertiel
  double fp_time{0.0};         // horloge d'oscillation d'inactivite
  double fp_sway_yaw{0.0};     // ballant du regard (rad), borne
  double fp_sway_pitch{0.0};
  Dvec3  fp_bob{};             // derive bornee de l'oeil (m) -> parallaxe douce

  // --- sorties de rendu -------------------------------------------------------
  Dvec3 eye_world() const;                 // position oeil, monde (double)
  Mat4  view() const;                      // vue, oeil a l'origine (camera-relative)
  Mat4  proj(float aspect) const;          // near/zfar choisis selon `distance`
  float znear() const;                     // adapte a l'echelle courante
  float zfar()  const;
  Dvec3 look_dir() const;                  // direction de regard unitaire (mode FirstPerson)

  // Translation camera-relative d'un point monde -> float, pret pour le model.
  Vec3 world_to_render(const Dvec3& p) const { return to_float_rel(p, eye_world()); }

  // --- controles (souris/clavier) --------------------------------------------
  void orbit(float dyaw, float dpitch);    // arcball
  void dolly(float factor);                // zoom multiplicatif (>1 = recule)
  void pan(float dx, float dy);            // deplace le focus dans le plan de vue (px)
  void set_distance(double d);
  void frame_radius(double body_radius);   // cadre un corps de rayon donne

  // Premiere personne (mode FirstPerson) : deplace l'OEIL (`focus`) le long de la
  // base de vue. fwd = avant/arriere (sens du regard), right = gauche/droite,
  // up = bas/haut (axe monde +Z). Amplitudes en metres (l'app les scale par dt).
  void move(double fwd, double right, double up);

  // Deplacement PREMIERE PERSONNE avec INERTIE (impesanteur controlable) : la
  // poussee (fwd/right/up in {-1,0,1}) accelere une vitesse amortie ; l'oeil
  // continue de glisser apres relachement, sans etre incontrolable. A appeler
  // chaque frame (poussee nulle => decelere en douceur).
  void fp_move(double fwd, double right, double up, double dt);

  // Micro-oscillation d'inactivite (ressenti d'impesanteur) : met a jour un leger
  // ballant du regard et une derive de l'oeil, bornes (jamais de derive nette).
  void fp_update_idle(double dt);

  // --- pilotage par le mode ---------------------------------------------------
  // Recompose le focus/att a partir du snapshot selon le mode courant. Le RenderCore
  // l'appelle une fois par frame, APRES avoir fige le snapshot. Ne recalcule
  // aucune physique : lit des positions deja gelees.
  void follow(const RenderSnapshot& s, int focused_body_index);

 private:
  Dvec3 offset_dir() const;                // direction focus->oeil (unitaire, double)
};

} // namespace spr
