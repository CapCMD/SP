// spr/core/Math.hpp
//
// Petite algebre lineaire flottante AUTONOME (aucune dependance : ni GLM, ni web).
// C'est la math du RENDU (float, GPU). Elle ne remplace JAMAIS fen::Vec3 (double,
// SI) : la physique reste en double, on ne convertit en float qu'APRES avoir
// soustrait l'origine camera (voir bridge/RenderSnapshot.hpp). Un float ne peut
// pas representer 1 UA au metre pres ; c'est tout l'enjeu du rendu grande echelle.
//
// Conventions VULKAN assumees :
//   - espace vue main droite, camera regardant vers -Z ;
//   - clip Vulkan : X droite, Y BAS, profondeur [0,1] (perspective() applique le
//     flip Y dans la matrice -> pas de viewport negatif a gerer ailleurs).
//   - matrices COLONNE-MAJEURE, stockage m[col*4 + row] (compatible layout GLSL).
#pragma once
#include <cmath>
#include <cstdint>

namespace spr {

inline constexpr float PI_F = 3.14159265358979323846f;
inline float radians(float deg) { return deg * (PI_F / 180.0f); }

struct Vec2 { float x{}, y{}; };

struct Vec3 {
  float x{}, y{}, z{};
  constexpr Vec3() = default;
  constexpr Vec3(float a, float b, float c) : x(a), y(b), z(c) {}
  Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
  Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
  Vec3 operator-() const { return {-x, -y, -z}; }
  Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
  Vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
  Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
};
inline Vec3 operator*(float s, const Vec3& v) { return v * s; }

struct Vec4 {
  float x{}, y{}, z{}, w{};
  constexpr Vec4() = default;
  constexpr Vec4(float a, float b, float c, float d) : x(a), y(b), z(c), w(d) {}
  constexpr Vec4(const Vec3& v, float d) : x(v.x), y(v.y), z(v.z), w(d) {}
};

inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3& a, const Vec3& b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(const Vec3& v) { return std::sqrt(dot(v, v)); }
inline Vec3 normalize(const Vec3& v) {
  const float n = length(v);
  return n > 0.0f ? v / n : Vec3{};
}

// Matrice 4x4 colonne-majeure. m[col*4 + row].
struct Mat4 {
  float m[16]{};

  static Mat4 identity() {
    Mat4 r;
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
  }

  // Produit COLONNE-MAJEUR : (this * o), applique o puis this.
  Mat4 operator*(const Mat4& o) const {
    Mat4 r;
    for (int c = 0; c < 4; ++c)
      for (int row = 0; row < 4; ++row) {
        float s = 0.0f;
        for (int k = 0; k < 4; ++k) s += m[k * 4 + row] * o.m[c * 4 + k];
        r.m[c * 4 + row] = s;
      }
    return r;
  }

  Vec4 operator*(const Vec4& v) const {
    return {
        m[0] * v.x + m[4] * v.y + m[8] * v.z + m[12] * v.w,
        m[1] * v.x + m[5] * v.y + m[9] * v.z + m[13] * v.w,
        m[2] * v.x + m[6] * v.y + m[10] * v.z + m[14] * v.w,
        m[3] * v.x + m[7] * v.y + m[11] * v.z + m[15] * v.w};
  }
};

inline Mat4 translation(const Vec3& t) {
  Mat4 r = Mat4::identity();
  r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z;
  return r;
}

inline Mat4 scaling(float s) {
  Mat4 r = Mat4::identity();
  r.m[0] = r.m[5] = r.m[10] = s;
  return r;
}

// Rotation d'angle `angle` (rad) autour de l'axe `axis_in` (Rodrigues), stockage
// COLONNE-MAJEUR m[col*4+row]. Axe nul ou angle nul -> identite. Sert a orienter
// et faire tourner les corps (alignement du pole + rotation propre).
inline Mat4 rotation_axis(const Vec3& axis_in, float angle) {
  const float len = length(axis_in);
  if (len <= 0.0f || angle == 0.0f) return Mat4::identity();
  const Vec3 a = axis_in / len;
  const float c = std::cos(angle), s = std::sin(angle), t = 1.0f - c;
  const float x = a.x, y = a.y, z = a.z;
  Mat4 r = Mat4::identity();
  r.m[0] = t * x * x + c;      r.m[1] = t * x * y + s * z;  r.m[2]  = t * x * z - s * y;
  r.m[4] = t * x * y - s * z;  r.m[5] = t * y * y + c;      r.m[6]  = t * y * z + s * x;
  r.m[8] = t * x * z + s * y;  r.m[9] = t * y * z - s * x;  r.m[10] = t * z * z + c;
  return r;
}

// Vue main droite (equivalent glm::lookAtRH). Camera en `eye` regardant `center`.
inline Mat4 look_at(const Vec3& eye, const Vec3& center, const Vec3& up) {
  const Vec3 f = normalize(center - eye);
  const Vec3 s = normalize(cross(f, up));
  const Vec3 u = cross(s, f);
  Mat4 r = Mat4::identity();
  r.m[0] = s.x;  r.m[4] = s.y;  r.m[8]  = s.z;
  r.m[1] = u.x;  r.m[5] = u.y;  r.m[9]  = u.z;
  r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
  r.m[12] = -dot(s, eye);
  r.m[13] = -dot(u, eye);
  r.m[14] = dot(f, eye);
  return r;
}

// Perspective main droite, profondeur [0,1] (convention Vulkan), Y deja flippe.
// far peut etre tres grand : le rendu grande echelle decoupe la scene en
// "coquilles" (voir docs/RENDER_ARCHITECTURE.md, extension depth logarithmique).
inline Mat4 perspective(float fovy_rad, float aspect, float znear, float zfar) {
  const float f = 1.0f / std::tan(fovy_rad * 0.5f);
  Mat4 r;  // tout a 0
  r.m[0]  = f / aspect;
  r.m[5]  = -f;  // flip Y pour le clip Vulkan
  r.m[10] = zfar / (znear - zfar);
  r.m[11] = -1.0f;
  r.m[14] = (zfar * znear) / (znear - zfar);
  return r;
}

// Perspective REVERSED-Z a plan far INFINI (convention Vulkan, Y flippe).
// NDC z = znear / distance : near -> 1, distance -> +inf -> 0. Combine a un depth
// buffer FLOTTANT (D32_SFLOAT) et a un test de profondeur GREATER, cela donne une
// precision quasi uniforme du metre a l'infini : c'est LA solution grande echelle
// (elimine le z-fighting vehicule-proche / planete-lointaine sans plan far). Voir
// docs/RENDER_PIPELINE_HDR.md pour la justification complete.
// IMPORTANT (cote backend) : clear depth = 0.0 et VK_COMPARE_OP_GREATER(_OR_EQUAL).
inline Mat4 perspective_reversed_infinite(float fovy_rad, float aspect, float znear) {
  const float f = 1.0f / std::tan(fovy_rad * 0.5f);
  Mat4 r;  // tout a 0
  r.m[0]  = f / aspect;
  r.m[5]  = -f;       // flip Y pour le clip Vulkan
  r.m[10] = 0.0f;     // far infini
  r.m[11] = -1.0f;
  r.m[14] = znear;    // clip.z = znear ; clip.w = distance -> NDC = znear/distance
  return r;
}

// Orthographique main droite, profondeur [0,1] (convention Vulkan). Sert a la
// projection LIGHT-SPACE des ombres directionnelles (shadow mapping) : la lumiere
// n'a pas de point de fuite -> une boite qui englobe la scene projette la
// profondeur uniformement. IMPORTANT : contrairement a perspective(), on NE flippe
// PAS Y ici. La shadow map est rendue ET echantillonnee avec CETTE meme matrice
// (uv = ndc.xy*0.5+0.5) : la convention Y n'a aucune importance tant qu'on ne la
// retourne qu'une seule fois -> auto-coherente. En vue: z va de -znear (NDC 0) a
// -zfar (NDC 1), la camera regardant vers -Z (comme look_at).
inline Mat4 ortho(float l, float r, float b, float t, float znear, float zfar) {
  Mat4 m;  // tout a 0
  m.m[0]  = 2.0f / (r - l);
  m.m[5]  = 2.0f / (t - b);
  m.m[10] = -1.0f / (zfar - znear);
  m.m[12] = -(r + l) / (r - l);
  m.m[13] = -(t + b) / (t - b);
  m.m[14] = -znear / (zfar - znear);
  m.m[15] = 1.0f;
  return m;
}

} // namespace spr
