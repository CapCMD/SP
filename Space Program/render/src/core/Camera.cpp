// spr/core/Camera.cpp
#include "spr/core/Camera.hpp"
#include <algorithm>
#include <cmath>

namespace spr {

const char* camera_mode_name(CameraMode m) {
  switch (m) {
    case CameraMode::Inertial:     return "INERTIELLE";
    case CameraMode::BodyCentered: return "CENTREE CORPS";
    case CameraMode::Vehicle:      return "VEHICULE";
    case CameraMode::Surface:      return "SURFACE";
    case CameraMode::Map:          return "CARTE";
    case CameraMode::FirstPerson:  return "PREMIERE PERSONNE";
    default:                       return "?";
  }
}

// Direction unitaire focus -> oeil, en double, a partir de (yaw, pitch).
// +Z = nord ecliptique. yaw tourne autour de +Z, pitch = elevation.
Dvec3 Camera::offset_dir() const {
  const double cp = std::cos(pitch), sp = std::sin(pitch);
  const double cy = std::cos(yaw),   sy = std::sin(yaw);
  return Dvec3{cp * cy, cp * sy, sp};
}

// Direction unitaire de REGARD (mode FirstPerson) : identique a offset_dir() mais
// interpretee comme "ou pointe la camera" (et non focus->oeil).
Dvec3 Camera::look_dir() const { return offset_dir(); }

// Direction unitaire (yaw,pitch) avec un decalage optionnel (ballant d'inactivite).
static Dvec3 dir_from(double yaw, double pitch) {
  const double cp = std::cos(pitch), sp = std::sin(pitch);
  const double cy = std::cos(yaw),   sy = std::sin(yaw);
  return Dvec3{cp * cy, cp * sy, sp};
}

Dvec3 Camera::eye_world() const {
  // FirstPerson : `focus` EST l'oeil + une derive bornee (ressenti d'impesanteur).
  if (mode == CameraMode::FirstPerson)
    return Dvec3{focus.x + fp_bob.x, focus.y + fp_bob.y, focus.z + fp_bob.z};
  const Dvec3 d = offset_dir();
  return Dvec3{focus.x + distance * d.x,
              focus.y + distance * d.y,
              focus.z + distance * d.z};
}

// Construit la matrice de vue (oeil a l'origine) a partir d'une direction de REGARD
// `fwd` (unitaire) et de l'azimut `az`. La DROITE de la camera = tangente azimutale
// (-sin az, cos az, 0) : toujours definie et perpendiculaire au regard, meme quand
// le regard est vertical (pole). Elimine la singularite du "up" monde (plus de
// bascule +Z/+X ni d'effet MIROIR quand on aligne la camera sur les poles).
static Mat4 view_basis(const Dvec3& fwd, double az) {
  Vec3 f = normalize(Vec3{static_cast<float>(fwd.x), static_cast<float>(fwd.y), static_cast<float>(fwd.z)});
  Vec3 r{static_cast<float>(-std::sin(az)), static_cast<float>(std::cos(az)), 0.0f};
  r = normalize(r - dot(r, f) * f);        // r orthonormalisee contre f
  const Vec3 u = cross(r, f);              // up = right x forward (comme look_at)
  Mat4 m = Mat4::identity();
  m.m[0] = r.x;  m.m[4] = r.y;  m.m[8]  = r.z;
  m.m[1] = u.x;  m.m[5] = u.y;  m.m[9]  = u.z;
  m.m[2] = -f.x; m.m[6] = -f.y; m.m[10] = -f.z;
  return m;
}

Mat4 Camera::view() const {
  if (mode == CameraMode::FirstPerson) {
    // Oeil a l'origine, regard = (yaw,pitch) + ballant borne (impesanteur).
    // view_basis attend l'azimut du vecteur focus->oeil (OPPOSE au regard) pour
    // fabriquer la droite/haut : la carte lui passe fwd=-offset_dir et az=yaw. En
    // FPS le regard EST +azimut, donc on passe az+PI (azimut oppose) -> sinon le
    // haut de vue serait -Z et toute la scene apparaitrait a l'envers (miroir).
    const double az = yaw + fp_sway_yaw;
    return view_basis(dir_from(az, pitch + fp_sway_pitch),
                      az + 3.14159265358979323846);
  }
  // Oeil a l'origine (rendu camera-relative) : le regard vaut (focus - oeil) =
  // -offset_dir (l'oeil est a focus + distance*offset_dir).
  const Dvec3 d = offset_dir();
  return view_basis(Dvec3{-d.x, -d.y, -d.z}, yaw);
}

float Camera::znear() const {
  // FirstPerson (interieur ISS, echelle metrique) : near tres serre.
  if (mode == CameraMode::FirstPerson) return 0.05f;
  // Adapte a l'echelle : proche du focus on veut un near serre.
  return std::max(static_cast<float>(distance) * 5.0e-4f, 0.5f);
}
float Camera::zfar() const {
  // Assez loin pour englober au moins l'echelle du systeme visible.
  return std::max(static_cast<float>(distance) * 4.0e3f, 5.0e11f);
}

Mat4 Camera::proj(float aspect) const {
  // Reversed-Z a far infini : precision de profondeur quasi uniforme du proche
  // vehicule a la planete lointaine (cf. Math.hpp / RENDER_PIPELINE_HDR.md).
  return perspective_reversed_infinite(radians(fov_deg), aspect, znear());
}

void Camera::orbit(float dyaw, float dpitch) {
  yaw += dyaw;
  pitch += dpitch;
  const double lim = 89.0 * (3.14159265358979323846 / 180.0);
  pitch = std::clamp(pitch, -lim, lim);
}

void Camera::dolly(float factor) {
  distance *= factor;
  distance = std::clamp(distance, 1.0, 5.0e13);  // 1 m a au-dela de l'orbite de Pluton
}

void Camera::pan(float dx, float dy) {
  // Deplace le POINT REGARDE (focus) dans le plan de l'ecran. L'amplitude suit la
  // distance -> le pan "colle" au curseur quel que soit le zoom. Base de vue en
  // double (coherente avec view()/eye_world()).
  const Dvec3 d = offset_dir();                 // focus -> oeil
  const Dvec3 fwd{-d.x, -d.y, -d.z};            // oeil -> focus
  Dvec3 up{0.0, 0.0, 1.0};
  if (std::abs(pitch) > 1.55) up = Dvec3{1.0, 0.0, 0.0};
  // right = normalize(fwd x up) ; camUp = normalize(right x fwd)
  Dvec3 right{fwd.y * up.z - fwd.z * up.y, fwd.z * up.x - fwd.x * up.z,
              fwd.x * up.y - fwd.y * up.x};
  double rn = std::sqrt(right.x * right.x + right.y * right.y + right.z * right.z);
  if (rn < 1e-12) return;
  right = Dvec3{right.x / rn, right.y / rn, right.z / rn};
  Dvec3 cup{right.y * fwd.z - right.z * fwd.y, right.z * fwd.x - right.x * fwd.z,
            right.x * fwd.y - right.y * fwd.x};
  const double k = distance * 0.0016;           // sensibilite ~ constante a l'ecran
  focus.x += (-right.x * dx + cup.x * dy) * k;
  focus.y += (-right.y * dx + cup.y * dy) * k;
  focus.z += (-right.z * dx + cup.z * dy) * k;
}

void Camera::move(double fwd, double right, double up) {
  // Deplace l'oeil (`focus` en FirstPerson) dans la base de vue. Avant = regard,
  // droite = perpendiculaire au regard dans le plan horizontal, haut = +Z monde.
  const Dvec3 f = offset_dir();                 // avant (regard)
  const Dvec3 wup{0.0, 0.0, 1.0};
  // right = normalize(f x up) ; degenere si regard ~ vertical -> repli sur +X.
  Dvec3 r{f.y * wup.z - f.z * wup.y, f.z * wup.x - f.x * wup.z, f.x * wup.y - f.y * wup.x};
  double rn = std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z);
  if (rn < 1e-9) { r = Dvec3{1, 0, 0}; rn = 1.0; }
  r = Dvec3{r.x / rn, r.y / rn, r.z / rn};
  focus.x += f.x * fwd + r.x * right + wup.x * up;
  focus.y += f.y * fwd + r.y * right + wup.y * up;
  focus.z += f.z * fwd + r.z * right + wup.z * up;
}

void Camera::fp_move(double fwd, double right, double up, double dt) {
  if (dt <= 0.0) return;
  // Base de vue (avant = regard courant, droite dans le plan horizontal, haut monde).
  const Dvec3 f = offset_dir();
  const Dvec3 wup{0.0, 0.0, 1.0};
  Dvec3 r{f.y * wup.z - f.z * wup.y, f.z * wup.x - f.x * wup.z, f.x * wup.y - f.y * wup.x};
  double rn = std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z);
  if (rn < 1e-9) { r = Dvec3{1, 0, 0}; rn = 1.0; }
  r = Dvec3{r.x / rn, r.y / rn, r.z / rn};
  // Poussee desiree (normalisee si diagonale) -> acceleration.
  Dvec3 thrust{f.x * fwd + r.x * right + wup.x * up,
               f.y * fwd + r.y * right + wup.y * up,
               f.z * fwd + r.z * right + wup.z * up};
  const double tn = std::sqrt(thrust.x * thrust.x + thrust.y * thrust.y + thrust.z * thrust.z);
  const double accel = 16.0;   // m/s^2 (reponse franche)
  if (tn > 1e-9) {
    fp_vel.x += thrust.x / tn * accel * dt;
    fp_vel.y += thrust.y / tn * accel * dt;
    fp_vel.z += thrust.z / tn * accel * dt;
  }
  // Amortissement exponentiel (inertie d'impesanteur, mais toujours controlable) :
  // sans poussee la vitesse decroit avec une constante de temps ~0.45 s.
  const double tau = (tn > 1e-9) ? 0.9 : 0.45;
  const double k = std::exp(-dt / tau);
  fp_vel.x *= k; fp_vel.y *= k; fp_vel.z *= k;
  // Plafond de vitesse doux (evite l'emballement).
  const double vmax = 5.0, vn = std::sqrt(fp_vel.x * fp_vel.x + fp_vel.y * fp_vel.y + fp_vel.z * fp_vel.z);
  if (vn > vmax) { const double s = vmax / vn; fp_vel.x *= s; fp_vel.y *= s; fp_vel.z *= s; }
  focus.x += fp_vel.x * dt; focus.y += fp_vel.y * dt; focus.z += fp_vel.z * dt;
}

void Camera::fp_update_idle(double dt) {
  fp_time += dt;
  const double s = std::sin(fp_time * 0.55), c = std::cos(fp_time * 0.73);
  const double s2 = std::sin(fp_time * 0.41 + 1.3), c2 = std::cos(fp_time * 0.9 + 0.6);
  // ballant du regard tres leger (~0.3-0.4 deg) : le corps "respire" sans nausee.
  fp_sway_yaw   = 0.0065 * s;
  fp_sway_pitch = 0.0045 * c2;
  // derive bornee de l'oeil (quelques cm) -> parallaxe douce (jamais de derive nette).
  fp_bob = Dvec3{0.030 * c, 0.024 * s2, 0.035 * s};
}

void Camera::set_distance(double d) { distance = std::clamp(d, 1.0, 5.0e13); }

void Camera::frame_radius(double body_radius) {
  // Cadre une sphere de rayon donne : distance = r / sin(fov/2) * marge.
  const double half = radians(fov_deg) * 0.5;
  const double s = std::sin(half);
  distance = (s > 1e-6 ? body_radius / s : body_radius) * 2.4;
  distance = std::clamp(distance, 1.0, 1.0e13);
}

void Camera::follow(const RenderSnapshot& s, int focused_body_index) {
  // follow() ne fait QUE deplacer le focus (suivi de cible). Il ne touche pas
  // yaw/pitch/distance : l'utilisateur garde la main sur l'arcball et le zoom.
  auto central = Dvec3{0, 0, 0};  // le corps central est a l'origine du repere monde
  switch (mode) {
    case CameraMode::Inertial:
      focus = central;
      break;
    case CameraMode::Map:
      // Carte LIBRE : le focus est pilote par le pan utilisateur ; ne pas le
      // reinitialiser (sinon la camera reste collee au Soleil).
      break;
    case CameraMode::FirstPerson:
      // Premiere personne (interieur ISS) : `focus` = oeil, pilote par move() ;
      // ne rien reinitialiser (comme Map).
      break;
    case CameraMode::BodyCentered:
      if (focused_body_index >= 0 && focused_body_index < s.body_count)
        focus = s.bodies[focused_body_index].position;
      else
        focus = central;
      break;
    case CameraMode::Vehicle:
      focus = s.vehicle.valid ? s.vehicle.position : central;
      break;
    case CameraMode::Surface: {
      // Point de surface sous le vaisseau (ou point de reference si absent).
      if (s.vehicle.valid) {
        const Dvec3& p = s.vehicle.position;
        const double n = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
        const double R = s.central_radius;
        if (n > 1.0)
          focus = Dvec3{p.x / n * R, p.y / n * R, p.z / n * R};
        else
          focus = Dvec3{R, 0, 0};
      } else {
        focus = Dvec3{s.central_radius, 0, 0};
      }
      break;
    }
    default:
      focus = central;
      break;
  }
}

} // namespace spr
