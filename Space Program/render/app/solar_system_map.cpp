// render/app/solar_system_map.cpp
//
// LA FENETRE PRINCIPALE DE GAMEPLAY : la map du systeme solaire (type NASA Eyes).
// Doctrine : PhysicsCore (astro_core) -> DataBridge (fige) -> RenderCore.
//   * Positions par l'ephemeride Standish, a l'instant REEL d'ouverture.
//   * Physique / DataBridge / RenderSnapshot INCHANGES (ce fichier = composition
//     root ; il compose la presentation via le canal additif MapView).
//   * Surfaces des corps = textures des MODELES `assets/3D models/*.glb` (extraites
//     du GLB, pleine resolution). Exceptions (couches) depuis `assets/textures` :
//     surface Terre (daymap), coquille nuages Terre, coquille atmosphere Venus,
//     et le fond Voie lactee. Tout est charge SANS compression (8K natif).
//   * Nuages (Terre) et atmosphere (Venus) = coquilles a rotation PROPRE.
//
// Camera facon NASA Eyes : cliquer un corps (liste CORPS) -> la camera s'y rend
// et le suit ; molette = zoom ; glisser gauche = pivoter ; glisser droit = pan ;
// bouton "Vue libre (Soleil)" pour revenir au systeme.
//
// Args : --frames N  --validation  --capture f.bmp  --assets <dir>
//        --focus <index>  --dist D --pitch P --yaw Y
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "spr/RenderCore.hpp"
#include "spr/bridge/DataBridge.hpp"
#include "spr/MapView.hpp"
#include "spr/StationView.hpp"
#include "spr/MenuView.hpp"
#include "spr/scene/RenderScene.hpp"
#include "spr/asset/GlbTexture.hpp"
#include "calc_eval.hpp"
#include "iss_collision.hpp"
#include "imgui.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>   // codes touches (E / Echap) pour l'interieur ISS

#include "fen/astro/Elements.hpp"
#include "fen/astro/Transfers.hpp"   // patched-conic REEL : hohmann / injection / insertion
#include "fen/core/Constants.hpp"
#include "fen/core/Epoch.hpp"
#include "fen/core/Vec3.hpp"
#include "fen/ephem/Ephemeris.hpp"

#include "app/jeu.hpp"          // MODELE DE JEU 2D (lecture seule) : panneaux 3D de l'ISS
#include "mission_catalog.hpp"  // catalogue de missions historiques + arbre de competences

#ifndef SPR_ASSET_DIR
#define SPR_ASSET_DIR "assets"
#endif

using fen::ephem::Body;

namespace {

constexpr double PId    = 3.14159265358979323846;
constexpr double TWO_PI = 6.28318530717958647693;
constexpr double DEG    = PId / 180.0;
constexpr double DAY    = 86400.0;

// ISS : orbite LEO REELLE (elements moyens declares, comme les lunes ; le noyau ne
// tabule pas l'ISS). Position en TEMPS REEL : theta avance avec le temps simule,
// au bon rythme orbital (~92.8 min). a = rayon Terre + altitude ; i = 51.64 deg.
constexpr double ISS_ALT   = 418.0e3;        // altitude moyenne (m)
constexpr double ISS_INC   = 51.64 * DEG;    // inclinaison sur l'equateur terrestre
constexpr int    ISS_ORBIT_N = 160;          // sommets de la trace orbitale

// Table de PRESENTATION. Orientation = modele de rotation IAU 2015 (WGCCRE) :
// pole (a0, del0) en ICRF J2000 + angle du meridien d'origine W = W0 + Wdot*d
// (d = jours depuis J2000). Donne l'axe (inclinaison) ET la phase (jour/nuit)
// EXACTS. Wdot<0 = rotation retrograde. (Termes seculaires/periodiques mineurs
// negliges : < ~0.2 deg sur la periode du jeu.)
struct BodyDef {
  Body        body;
  double      radius;      // m
  const char* glb;         // GLB relatif (assets/) d'ou extraire la texture
  const char* glb_img;     // nom d'image de surface dans le GLB
  int         archetype;   // spr::SurfaceArchetype
  double      a0, del0;     // pole IAU ICRF J2000 (deg)
  double      W0, Wdot;     // meridien d'origine : W0 (deg) + Wdot (deg/jour)
  bool        is_sun;
};

const BodyDef DEFS[] = {
  {Body::Sun,       6.9570e8, nullptr,                          nullptr,          0, 286.13,    63.87,    84.176,  14.1844000,   true},
  {Body::Mercury,   2.4397e6, "3D models/Mercury/Mercury.glb",  "mercury",        2, 281.0103,  61.4155, 329.5988,  6.1385108,  false},
  {Body::Venus,     6.0518e6, "3D models/Venus/Venus.glb",      "venus_surface",  2, 272.76,    67.16,   160.20,   -1.4813688,  false},
  {Body::EarthBary, 6.3710e6, nullptr,                          nullptr,          1,   0.00,    90.00,   190.147, 360.9856235,  false},
  {Body::Moon,      1.7374e6, "3D models/Earth/Moon.glb",       "moon",           2, 269.9949,  66.5392,  38.3213, 13.17635815, false},
  {Body::Mars,      3.3895e6, "3D models/Mars/Mars.glb",        "mars",           2, 317.68143, 52.88650,176.630, 350.89198226, false},
  {Body::Jupiter,   6.9911e7, "3D models/Jupiter/Jupiter.glb",  "jupiter",        3, 268.056595,64.495303,284.95, 870.5360000,  false},
  {Body::Saturn,    5.8232e7, "3D models/Saturne/Saturne.glb",  "saturn",         3,  40.589,   83.537,   38.90,  810.7939024,  false},
  {Body::Uranus,    2.5559e7, "3D models/Uranus/Uranus.glb",    "uranus",         3, 257.311,  -15.175,  203.81, -501.1600928,  false},
  {Body::Neptune,   2.4764e7, "3D models/Neptune/Neptune.glb",  "neptune",        3, 299.36,    43.46,   249.978, 541.1397757,  false},
  {Body::Pluto,     1.1883e6, "3D models/Pluto/Pluto.glb",      "pluto",          2, 132.993,   -6.163,  302.695,  56.3625225,  false},
};

const BodyDef* def_for(int id) {
  for (const BodyDef& d : DEFS) if (static_cast<int>(d.body) == id) return &d;
  return nullptr;
}

// Lunes : elements orbitaux MOYENS (a en km, e, inclinaison a l'equateur du
// parent en deg, periode siderale en jours, rayon en km). Orbite KEPLERIENNE
// (ellipse) dans le plan equatorial du parent, avec noeud/argument du periastre et
// phase a l'epoque J2000 (raan_deg, argp_deg, M0_deg), + precession seculaire J2
// (calculee depuis le J2 du parent). Rotation SYNCHRONE (verrou de maree).
//
// POSITION ABSOLUE : a/e/inc/periode = fiables. Les ANGLES A L'EPOQUE
// (raan/argp/M0) sont a 0 par defaut -> phase non ancree sur le ciel reel. C'est la
// limite du hors-ligne (le noyau ne tabule aucune de ces lunes). Pour des positions
// EXACTES, renseigner ici les elements moyens JPL (ssd.jpl.nasa.gov, "Planetary
// Satellite Mean Orbital Parameters", epoque J2000, plan de Laplace ~ equateur du
// parent). Ajouter les 3 valeurs a la fin de la ligne de la lune concernee.
struct MoonDef {
  Body        parent;
  const char* name;
  const char* glb;      // relatif a assets/
  const char* img;      // sous-chaine du nom d'image dans le GLB (insensible casse)
  double      a_km, e, inc_deg, period_days, radius_km;
  double      raan_deg{0.0}, argp_deg{0.0}, M0_deg{0.0};   // elements a J2000 (0 = non ancre)
};
const MoonDef MOONS[] = {
  {Body::Mars,    "Phobos",    "3D models/Mars/Phobos.glb",       "Phobos",     9376,    0.0151,  1.08,   0.318910, 11.3},
  {Body::Mars,    "Deimos",    "3D models/Mars/Deimos.glb",       "Deimos",     23463,   0.00033, 1.79,   1.262441,  6.2},
  {Body::Jupiter, "Io",        "3D models/Jupiter/Io-A.glb",      "Io (A)",     421700,  0.0041,  0.050,  1.769138, 1821.6},
  {Body::Jupiter, "Europa",    "3D models/Jupiter/Europa.glb",    "Europa",     671034,  0.0090,  0.470,  3.551181, 1560.8},
  {Body::Jupiter, "Ganymede",  "3D models/Jupiter/Ganymede.glb",  "Ganymede",   1070412, 0.0013,  0.200,  7.154553, 2634.1},
  {Body::Jupiter, "Callisto",  "3D models/Jupiter/Callisto.glb",  "Callisto",   1882709, 0.0074,  0.192, 16.689018, 2410.3},
  {Body::Saturn,  "Mimas",     "3D models/Saturne/Mimas.glb",     "Mimas",      185539,  0.0196,  1.574,  0.942422,  198.2},
  {Body::Saturn,  "Enceladus", "3D models/Saturne/Enceladus.glb", "Enceladus",  237948,  0.0047,  0.009,  1.370218,  252.1},
  {Body::Saturn,  "Tethys",    "3D models/Saturne/Tethys.glb",    "Tethys",     294619,  0.0001,  1.120,  1.887802,  531.1},
  {Body::Saturn,  "Dione",     "3D models/Saturne/Dione.glb",     "Dione",      377396,  0.0022,  0.028,  2.736915,  561.4},
  {Body::Saturn,  "Rhea",      "3D models/Saturne/Rhea.glb",      "Rhea",       527108,  0.0012,  0.345,  4.518212,  763.8},
  {Body::Saturn,  "Titan",     "3D models/Saturne/Titan.glb",     "Titan",      1221870, 0.0288,  0.349, 15.945000, 2574.7},
  {Body::Saturn,  "Iapetus",   "3D models/Saturne/Lapetus.glb",   "apetus",     3560820, 0.0286, 15.470, 79.321500,  734.5},
  {Body::Uranus,  "Miranda",   "3D models/Uranus/Miranda.glb",    "Miranda",    129390,  0.0013,  4.232,  1.413479,  235.8},
  {Body::Uranus,  "Umbriel",   "3D models/Uranus/Umbriel.glb",    "Umbriel",    266000,  0.0039,  0.128,  4.144000,  584.7},
  {Body::Uranus,  "Titania",   "3D models/Uranus/Titania.glb",    "Titania",    436300,  0.0011,  0.340,  8.706000,  788.9},
  {Body::Uranus,  "Oberon",    "3D models/Uranus/Oberon.glb",     "Oberon",     583500,  0.0014,  0.058, 13.463000,  761.4},
  {Body::Neptune, "Triton",    "3D models/Neptune/Triton.glb",    "Triton",     354759,  0.00002,156.885, 5.876854, 1353.4},
  {Body::Pluto,   "Charon",    "3D models/Pluto/Charon.glb",      "Charon",     19591,   0.0002,  0.080,  6.387200,  606.0},
};

spr::Vec3 pole_axis(double ra_deg, double dec_deg) {
  const double a = ra_deg * DEG, d = dec_deg * DEG;
  const double ex = std::cos(d) * std::cos(a), ey = std::cos(d) * std::sin(a), ez = std::sin(d);
  const double eps = 23.4392911 * DEG;
  return spr::Vec3{static_cast<float>(ex),
                   static_cast<float>(ey * std::cos(eps) + ez * std::sin(eps)),
                   static_cast<float>(-ey * std::sin(eps) + ez * std::cos(eps))};
}
// Orientation IAU complete (corps-fixe -> ECLIPTIQUE J2000) :
//   M = Rx(-eps) . Rz(90+a0) . Rx(90-del0) . Rz(W)
// Le mesh est en repere corps-fixe (pole=+Z, meridien 0 = +X, texture equirect
// centree sur le meridien 0). Donne le pole (inclinaison) ET la phase (jour/nuit).
spr::Mat4 iau_orientation(double a0_deg, double del0_deg, double W_deg) {
  const double eps = 23.4392911 * DEG;
  const double a0 = a0_deg * DEG, del0 = del0_deg * DEG, W = W_deg * DEG;
  return spr::rotation_axis(spr::Vec3{1, 0, 0}, static_cast<float>(-eps)) *
         spr::rotation_axis(spr::Vec3{0, 0, 1}, static_cast<float>(PId * 0.5 + a0)) *
         spr::rotation_axis(spr::Vec3{1, 0, 0}, static_cast<float>(PId * 0.5 - del0)) *
         spr::rotation_axis(spr::Vec3{0, 0, 1}, static_cast<float>(W));
}

// Position d'une lune RELATIVE a son parent (m) : orbite circulaire de rayon `a`
// dans le plan equatorial du parent (normale = `pole`), inclinee de `inc` autour
// du noeud, angle `theta`.
spr::Dvec3 moon_offset(const spr::Vec3& pole, double a, double inc, double theta) {
  spr::Vec3 u = spr::cross(spr::Vec3{0, 0, 1}, pole);
  if (spr::length(u) < 1e-4f) u = spr::Vec3{1, 0, 0};
  u = spr::normalize(u);
  const spr::Vec3 v = spr::normalize(spr::cross(pole, u));
  const double ci = std::cos(inc), si = std::sin(inc);
  const double a2x = ci * v.x + si * pole.x, a2y = ci * v.y + si * pole.y, a2z = ci * v.z + si * pole.z;
  const double ct = std::cos(theta), st = std::sin(theta);
  return spr::Dvec3{a * (ct * u.x + st * a2x), a * (ct * u.y + st * a2y), a * (ct * u.z + st * a2z)};
}

// J2 (aplatissement) du corps parent : gouverne la precession seculaire du noeud et
// du periastre des lunes (physique, pas de la donnee d'ephemeride). Source : IAU.
double parent_j2(Body b) {
  switch (b) {
    case Body::Mars:    return 1.9566e-3;
    case Body::Jupiter: return 1.4736e-2;
    case Body::Saturn:  return 1.6298e-2;
    case Body::Uranus:  return 3.3430e-3;
    case Body::Neptune: return 3.4110e-3;
    case Body::EarthBary: return 1.0826e-3;
    default:            return 0.0;   // Pluton ~ 0
  }
}

// Position d'une lune RELATIVE a son parent (m) : orbite KEPLERIENNE complete dans
// le plan equatorial du parent (normale = `pole`). Ellipse (a, e), inclinaison
// `inc`, noeud ascendant `raan` (mesure depuis l'intersection equateur-parent /
// ecliptique), argument du periastre `argp`, anomalie moyenne `M`. C'est le modele
// le plus precis possible hors-ligne ; l'exactitude ABSOLUE depend des elements
// (raan/argp/M) fournis a l'epoque. Repere = ecliptique J2000 (comme `pole`).
spr::Dvec3 kepler_relative(const spr::Vec3& pole, double a, double e, double inc,
                           double raan, double argp, double M) {
  // Kepler : M -> E (Newton) -> nu, r.
  double E = M;
  for (int it = 0; it < 8; ++it)
    E -= (E - e * std::sin(E) - M) / (1.0 - e * std::cos(E));
  const double nu = 2.0 * std::atan2(std::sqrt(1.0 + e) * std::sin(0.5 * E),
                                     std::sqrt(1.0 - e) * std::cos(0.5 * E));
  const double r = a * (1.0 - e * std::cos(E));
  // Base orthonormee du plan equatorial du parent : (u, w), normale = pole.
  spr::Vec3 u = spr::cross(spr::Vec3{0, 0, 1}, pole);
  if (spr::length(u) < 1e-4f) u = spr::Vec3{1, 0, 0};
  u = spr::normalize(u);
  const spr::Vec3 w = spr::normalize(spr::cross(pole, u));
  // Noeud ascendant N (dans le plan equatorial), normale d'orbite k (= pole incline
  // de `inc` autour de N), perpendiculaire m dans le plan d'orbite.
  const float cr = static_cast<float>(std::cos(raan)), sr = static_cast<float>(std::sin(raan));
  const spr::Vec3 N{u.x * cr + w.x * sr, u.y * cr + w.y * sr, u.z * cr + w.z * sr};
  const float ci = static_cast<float>(std::cos(inc)), si = static_cast<float>(std::sin(inc));
  const spr::Vec3 NxP = spr::cross(N, pole);
  const spr::Vec3 k{pole.x * ci + NxP.x * si, pole.y * ci + NxP.y * si, pole.z * ci + NxP.z * si};
  const spr::Vec3 m = spr::cross(k, N);
  // Direction du periastre P (N tourne de `argp` dans le plan d'orbite), Q = k x P.
  const float ca = static_cast<float>(std::cos(argp)), sa = static_cast<float>(std::sin(argp));
  const spr::Vec3 P{N.x * ca + m.x * sa, N.y * ca + m.y * sa, N.z * ca + m.z * sa};
  const spr::Vec3 Q = spr::cross(k, P);
  const double xp = r * std::cos(nu), yp = r * std::sin(nu);
  return spr::Dvec3{xp * P.x + yp * Q.x, xp * P.y + yp * Q.y, xp * P.z + yp * Q.z};
}

// Textures PLEINE RESOLUTION (max_dim = 0 -> aucune compression).
spr::TextureHandle tex_from_glb(spr::IRenderDevice* dev, const std::string& glb,
                                const std::string& img, const char* label) {
  spr::asset::ImageRgba im = spr::asset::load_glb_image_by_name(glb, img, 0);
  if (!im.ok()) { std::printf("[map] surface GLB absente : %s\n", label); return spr::INVALID_TEXTURE; }
  spr::TextureDesc td{}; td.rgba = im.pixels.data();
  td.width = static_cast<std::uint32_t>(im.width); td.height = static_cast<std::uint32_t>(im.height);
  td.srgb = true;
  std::printf("[map] GLB %-16s %dx%d\n", label, im.width, im.height);
  return dev->create_texture(td);
}
spr::TextureHandle tex_from_file(spr::IRenderDevice* dev, const std::string& path, const char* label) {
  spr::asset::ImageRgba im = spr::asset::load_image_file(path, 0);
  if (!im.ok()) { std::printf("[map] fichier absent : %s\n", label); return spr::INVALID_TEXTURE; }
  spr::TextureDesc td{}; td.rgba = im.pixels.data();
  td.width = static_cast<std::uint32_t>(im.width); td.height = static_cast<std::uint32_t>(im.height);
  td.srgb = true;
  std::printf("[map] fichier %-14s %dx%d\n", label, im.width, im.height);
  return dev->create_texture(td);
}

// Projette une position monde a l'ecran (px). false si derriere la camera.
bool project(const spr::Camera& cam, const spr::Dvec3& world, float aspect,
             float W, float H, float& sx, float& sy) {
  const spr::Vec3 rel = cam.world_to_render(world);
  const spr::Vec4 clip = (cam.proj(aspect) * cam.view()) * spr::Vec4{rel, 1.0f};
  if (clip.w <= 1e-6f) return false;
  sx = ((clip.x / clip.w) * 0.5f + 0.5f) * W;
  sy = ((clip.y / clip.w) * 0.5f + 0.5f) * H;
  return true;
}

fen::Epoch epoch_now() {
  std::time_t tt = std::time(nullptr);
  std::tm* g = std::gmtime(&tt);
  char iso[40];
  std::snprintf(iso, sizeof iso, "%04d-%02d-%02dT%02d:%02d:%02d",
                g->tm_year + 1900, g->tm_mon + 1, g->tm_mday, g->tm_hour, g->tm_min, g->tm_sec);
  return fen::epoch_from_iso(iso);
}

// ============================ INTERIEUR ISS =================================
// Composition PLACEHOLDER du QG. Modules = cylindres (make_cylinder), mobilier =
// boites (make_box). Repere LOCAL metrique : X = axe du couloir, Z = haut. Nommage
// clair par piece -> remplacer un placeholder = echanger UN maillage, sans rien
// casser. Aucune physique : pure presentation (canal StationView).
spr::MeshHandle upload_mesh(spr::IRenderDevice* dev, const std::vector<spr::Vertex>& v,
                            const std::vector<std::uint32_t>& i) {
  spr::MeshDesc md{};
  md.vertices = v.data(); md.vertex_count = static_cast<std::uint32_t>(v.size());
  md.indices = i.data();  md.index_count = static_cast<std::uint32_t>(i.size());
  md.max_vertices = md.vertex_count;
  return dev->create_mesh(md);
}

// ============================================================================
// AGENCEMENT REEL de l'ISS (source de verite : geometrie ET deplacement).
// Modelise "en gros" a taille reelle : axe principal fore-aft
//   ZVEZDA - ZARYA - UNITY(Node 1) - DESTINY - HARMONY(Node 2)
// + branches reelles :
//   . TRANQUILITY (Node 3) au NADIR depuis Unity, terminee par la COUPOLE (hublots)
//   . COLUMBUS (tribord) et KIBO (babord) depuis Harmony
// Chaque module = un cylindre (vu de l'interieur, normales rentrantes). Le MEME
// tableau borne le deplacement de l'oeil (reseau ramifie traversable).
enum IssAxis { IAX_X = 0, IAX_Y = 1, IAX_Z = 2 };
struct IssModule {
  const char* name;
  IssAxis     axis;
  float       cx, cy, cz;   // centre (m, repere station)
  float       hl;           // demi-longueur le long de l'axe
  float       r;            // rayon interne
};
constexpr float ISS_MR = 2.15f;   // rayon interne standard (~4.3 m de diametre reel)
constexpr IssModule ISS_MODULES[] = {
  //  nom            axe     cx      cy      cz     hl     r
  {"ZVEZDA",       IAX_X, -23.0f,  0.0f,  0.0f,  7.0f, ISS_MR},
  {"ZARYA",        IAX_X, -10.0f,  0.0f,  0.0f,  6.5f, ISS_MR},
  {"UNITY",        IAX_X,  -1.5f,  0.0f,  0.0f,  3.0f, ISS_MR},
  {"DESTINY",      IAX_X,   6.0f,  0.0f,  0.0f,  5.0f, ISS_MR},
  {"HARMONY",      IAX_X,  13.5f,  0.0f,  0.0f,  3.5f, ISS_MR},
  {"TRANQUILITY",  IAX_Z,  -1.5f,  0.0f, -4.85f, 3.65f, ISS_MR},  // nadir depuis Unity (peu de chevauchement)
  {"COUPOLE",      IAX_Z,  -1.5f,  0.0f, -9.0f,  1.3f, 1.65f},    // hublots d'observation
  {"COLUMBUS",     IAX_Y,  13.5f,  5.5f,  0.0f,  4.0f, ISS_MR},   // tribord depuis Harmony
  {"KIBO",         IAX_Y,  13.5f, -6.5f,  0.0f,  5.0f, ISS_MR},   // babord depuis Harmony
  // NOVELLUS : module FICTIF (QG du joueur, cf. Intro.txt), ACCOSTE a l'avant sur le
  // port PMA-2 de Harmony (+X). Ordinateur principal, carte, quartiers, messagerie.
  {"NOVELLUS",     IAX_X,  20.0f,  0.0f,  0.0f,  4.0f, ISS_MR},   // 16..24, chevauche Harmony (16..17)
};
constexpr int ISS_MODULE_N = sizeof(ISS_MODULES) / sizeof(ISS_MODULES[0]);

// Rotation alignant le +Z d'un cylindre (make_cylinder) sur l'axe d'un module.
spr::Mat4 iss_axis_rot(IssAxis a) {
  switch (a) {
    case IAX_X: return spr::rotation_axis(spr::Vec3{0, 1, 0},  spr::PI_F * 0.5f);   // Z -> X
    case IAX_Y: return spr::rotation_axis(spr::Vec3{1, 0, 0}, -spr::PI_F * 0.5f);   // Z -> Y
    default:    return spr::Mat4::identity();                                        // Z
  }
}

// Definition d'un poste : etape du cycle de mission, placee dans un module REEL de
// l'ISS (sous-titre), avec sa couleur d'accent (DA holographique) et sa position.
// `work` = poste de travail (mobilier) ; false = zone d'observation (coupole).
struct PostDef {
  const char* id;
  const char* label;
  const char* sub;
  spr::Vec3   accent;
  float       x, y, z;      // centre du poste (repere station)
  IssAxis     mod_axis;     // axe du module hote (oriente le mobilier)
  bool        work;
};
constexpr PostDef ISS_POSTS[] = {
  {"agence",        "AGENCE",        "ZVEZDA . DIRECTION",        {1.00f, 0.74f, 0.34f}, -23.0f,  0.0f,  0.0f, IAX_X, true},
  {"analyse",       "ANALYSE",       "ZARYA . ARCHIVES",         {0.74f, 0.62f, 1.00f}, -10.0f,  0.0f,  0.0f, IAX_X, true},
  {"operations",    "OPERATIONS",    "TRANQUILITY . SYSTEMES",   {0.46f, 0.90f, 0.56f},  -1.5f,  0.0f, -3.6f, IAX_Z, true},
  {"controle",      "CONTROLE",      "DESTINY . CONTROLE DE VOL",{0.40f, 0.82f, 1.00f},   6.0f,  0.0f,  0.0f, IAX_X, true},
  {"conception",    "CONCEPTION",    "COLUMBUS . CONCEPTION",    {0.32f, 0.86f, 0.80f},  13.5f,  4.2f,  0.0f, IAX_Y, true},
  {"planification", "PLANIFICATION", "KIBO . TRAJECTOIRE",       {0.44f, 0.70f, 1.00f},  13.5f, -5.8f,  0.0f, IAX_Y, true},
  {"observation",   "COUPOLE",       "TRANQUILITY . OBSERVATION",{0.55f, 0.80f, 1.00f},  -1.5f,  0.0f, -8.6f, IAX_Z, false},
  {"novellus",      "NOVELLUS",      "QG . ORDINATEUR PRINCIPAL",{0.92f, 0.56f, 0.92f},  20.0f,  0.0f,  0.0f, IAX_X, true},
};
constexpr int ISS_POST_N = sizeof(ISS_POSTS) / sizeof(ISS_POSTS[0]);

void build_iss(spr::IRenderDevice* dev, std::vector<spr::MeshHandle>& meshes,
               std::vector<spr::StationPart>& parts, std::vector<spr::StationZone>& zones) {
  using spr::Vec3; using spr::Vec4; using spr::Mat4;
  std::vector<spr::Vertex> v; std::vector<std::uint32_t> idx;

  const Vec4 hull {0.66f, 0.69f, 0.75f, 1.0f};
  const Vec4 hull2{0.52f, 0.56f, 0.62f, 1.0f};
  const Vec4 metal{0.34f, 0.36f, 0.41f, 1.0f};
  const Vec4 glow {0.60f, 0.72f, 0.86f, 1.0f};   // bandeaux lumineux : blanc froid doux

  auto push = [&](spr::MeshHandle mesh, const Mat4& model, Vec4 color,
                  spr::DrawStyle style, const char* name) {
    spr::StationPart p{}; p.mesh = mesh; p.model = model; p.color = color; p.style = style;
    std::snprintf(p.name, sizeof p.name, "%s", name);
    parts.push_back(p);
  };
  auto upl = [&](void) { return upload_mesh(dev, v, idx); };

  // --- coques : un cylindre (normales rentrantes, sans capuchon) par module ---
  for (int i = 0; i < ISS_MODULE_N; ++i) {
    const IssModule& M = ISS_MODULES[i];
    // PORTES (ouvertures de paroi) vers les modules perpendiculaires -> la jonction
    // n'est plus bouchee par une paroi qu'on traverse. Angle local 0 = +X module ;
    // pour un module X : angle->monde = (0, sin, -cos). UNITY -> Tranquility (-Z,
    // angle 0) ; HARMONY -> Columbus/Kibo (±Y, angles PI/2 et 3PI/2).
    float o0 = -1.0f, o1 = -1.0f;
    const bool is_unity   = std::strcmp(M.name, "UNITY") == 0;
    const bool is_harmony = std::strcmp(M.name, "HARMONY") == 0;
    if (is_unity)        o0 = 0.0f;
    else if (is_harmony) { o0 = spr::PI_F * 0.5f; o1 = spr::PI_F * 1.5f; }
    spr::make_cylinder(v, idx, M.r, M.hl, 40, false, true, o0, o1, 0.85f);
    const spr::MeshHandle m = upl();
    meshes.push_back(m);
    push(m, spr::translation(Vec3{M.cx, M.cy, M.cz}) * iss_axis_rot(M.axis), hull,
         spr::DrawStyle::PlanetLit, M.name);
    // caillebotis de sol (-Z) pour modules horizontaux (SAUF Unity : porte au sol)
    if (M.axis != IAX_Z && !is_unity) {
      if (M.axis == IAX_X) spr::make_box(v, idx, M.hl - 0.10f, 0.55f, 0.04f);
      else                 spr::make_box(v, idx, 0.55f, M.hl - 0.10f, 0.04f);
      const spr::MeshHandle mf = upl(); meshes.push_back(mf);
      push(mf, spr::translation(Vec3{M.cx, M.cy, M.cz - (M.r - 0.25f)}), metal,
           spr::DrawStyle::PlanetLit, "SOL");
    }
  }

  // --- anneaux de sas (hatch) aux jonctions : segmente la coque + repere de
  //     profondeur (comme les vrais anneaux entre modules ISS). --------------
  spr::make_cylinder(v, idx, ISS_MR - 0.12f, 0.15f, 32, false, true);
  const spr::MeshHandle m_hatch = upl(); meshes.push_back(m_hatch);
  struct Hatch { float x, y, z; IssAxis ax; };
  const Hatch hatches[] = {
    {-16.25f, 0.0f, 0.0f, IAX_X}, {-4.0f, 0.0f, 0.0f, IAX_X},   // Zvezda|Zarya, Zarya|Unity
    { 1.25f,  0.0f, 0.0f, IAX_X}, {10.5f, 0.0f, 0.0f, IAX_X},   // Unity|Destiny, Destiny|Harmony
    {-1.5f,   0.0f,-2.0f, IAX_Z},                               // Unity -> Tranquility
    {13.5f,   2.0f, 0.0f, IAX_Y}, {13.5f, -2.0f, 0.0f, IAX_Y},  // Harmony -> Columbus / Kibo
    {16.6f,   0.0f, 0.0f, IAX_X},                               // Harmony -> Novellus (PMA-2)
  };
  for (const Hatch& h : hatches)
    push(m_hatch, spr::translation(Vec3{h.x, h.y, h.z}) * iss_axis_rot(h.ax), hull2,
         spr::DrawStyle::PlanetLit, "SAS");

  // --- capuchons de bout (extremites reelles : Zvezda arriere, Harmony avant,
  //     Columbus, Kibo). Disque fin (cylindre tres court, capuchonne). ----------
  spr::make_cylinder(v, idx, ISS_MR, 0.06f, 40, /*capped*/ true, /*inward*/ true);
  const spr::MeshHandle m_endwall = upl(); meshes.push_back(m_endwall);
  struct EndCap { float x, y, z; IssAxis ax; };
  const EndCap caps[] = {
    {-30.0f, 0.0f, 0.0f, IAX_X},   // Zvezda (arriere)
    { 24.0f, 0.0f, 0.0f, IAX_X},   // Novellus (avant : bout du QG accoste)
    { 13.5f, 9.5f, 0.0f, IAX_Y},   // Columbus (bout)
    { 13.5f,-11.5f,0.0f, IAX_Y},   // Kibo (bout)
  };
  for (const EndCap& c : caps)
    push(m_endwall, spr::translation(Vec3{c.x, c.y, c.z}) * iss_axis_rot(c.ax), hull2,
         spr::DrawStyle::PlanetLit, "CLOISON");

  // --- COUPOLE : hublots d'observation (nadir). Bas OUVERT -> le champ d'etoiles
  //     est visible "au travers" ; 6 meneaux radiaux + anneau donnent la coupole
  //     hexagonale caracteristique, face a la Terre. ------------------------------
  spr::make_box(v, idx, 0.055f, 0.16f, 0.85f); const spr::MeshHandle m_mullion = upl(); meshes.push_back(m_mullion);
  spr::make_cylinder(v, idx, 1.62f, 0.10f, 32, false, true); const spr::MeshHandle m_ring = upl(); meshes.push_back(m_ring);
  {
    const Vec3 cup{-1.5f, 0.0f, -9.9f};   // pourtour bas de la coupole
    push(m_ring, spr::translation(cup) * spr::Mat4::identity(), hull2, spr::DrawStyle::PlanetLit, "COUPOLE_ANNEAU");
    for (int k = 0; k < 6; ++k) {
      const float a = spr::PI_F * 2.0f * static_cast<float>(k) / 6.0f;
      const Vec3 p{cup.x + 1.35f * std::cos(a), cup.y + 1.35f * std::sin(a), cup.z + 0.55f};
      char nm[24]; std::snprintf(nm, sizeof nm, "COUPOLE_MENEAU_%d", k);
      push(m_mullion, spr::translation(p) * spr::rotation_axis(Vec3{0, 0, 1}, a), hull2,
           spr::DrawStyle::PlanetLit, nm);
    }
  }

  // --- mobilier de poste (mesh reutilisables) -------------------------------
  spr::make_box(v, idx, 0.80f, 0.80f, 0.30f);  const spr::MeshHandle m_console = upl(); meshes.push_back(m_console);
  spr::make_box(v, idx, 0.70f, 0.55f, 0.045f); const spr::MeshHandle m_screen  = upl(); meshes.push_back(m_screen);
  spr::make_box(v, idx, 0.40f, 0.40f, 1.30f);  const spr::MeshHandle m_rack    = upl(); meshes.push_back(m_rack);
  spr::make_box(v, idx, 1.30f, 0.05f, 0.80f);  const spr::MeshHandle m_banY    = upl(); meshes.push_back(m_banY);  // bandeau paroi +/-Y
  spr::make_box(v, idx, 0.05f, 1.30f, 0.80f);  const spr::MeshHandle m_banX    = upl(); meshes.push_back(m_banX);  // bandeau paroi +/-X

  // Un "poste de travail" : console + ecran incline + grand bandeau mural, oriente
  // selon l'axe du module hote (sol = -Z pour X/Y, paroi -X pour Z).
  auto furnish = [&](const PostDef& d) {
    const Vec4 screen{d.accent.x, d.accent.y, d.accent.z, 1.0f};
    const Vec3 base{d.x, d.y, d.z};
    Vec3 floorN, sideN; spr::MeshHandle banner = m_banY;
    if (d.mod_axis == IAX_Z) { floorN = Vec3{-1, 0, 0}; sideN = Vec3{0, 1, 0}; banner = m_banX; }
    else                     { floorN = Vec3{ 0, 0,-1}; sideN = Vec3{0, 1, 0}; banner = m_banY; }
    const float r = ISS_MR;
    char nm[24];
    // console au sol + ecran incline au-dessus
    const Vec3 cpos = base + floorN * (r - 0.45f);
    std::snprintf(nm, sizeof nm, "%.7s_CONSOLE", d.id);
    push(m_console, spr::translation(cpos), metal, spr::DrawStyle::PlanetLit, nm);
    std::snprintf(nm, sizeof nm, "%.7s_ECRAN", d.id);
    push(m_screen, spr::translation(base + floorN * (r - 1.05f)), screen, spr::DrawStyle::Emissive, nm);
    // grand bandeau-ecran sur la paroi laterale
    std::snprintf(nm, sizeof nm, "%.7s_MUR", d.id);
    push(banner, spr::translation(base + sideN * (r - 0.06f) + Vec3{0, 0, 0.35f}),
         Vec4{d.accent.x * 1.08f, d.accent.y * 1.04f, d.accent.z, 1.0f}, spr::DrawStyle::Emissive, nm);
    // rack d'equipement paroi opposee
    std::snprintf(nm, sizeof nm, "%.7s_RACK", d.id);
    push(m_rack, spr::translation(base - sideN * (r - 0.35f)), hull2, spr::DrawStyle::PlanetLit, nm);
  };

  // --- postes interactifs (spheres analytiques) + mobilier -------------------
  for (int i = 0; i < ISS_POST_N; ++i) {
    const PostDef& d = ISS_POSTS[i];
    if (d.work) furnish(d);
    spr::StationZone Z{};
    Z.center = spr::Dvec3{d.x, d.y, d.z};
    Z.radius = d.work ? 3.0 : 2.4;
    std::strncpy(Z.id, d.id, sizeof(Z.id) - 1);
    std::strncpy(Z.label, d.label, sizeof(Z.label) - 1);
    std::strncpy(Z.sub, d.sub, sizeof(Z.sub) - 1);
    Z.accent = d.accent;
    zones.push_back(Z);
  }

  // --- bandeaux lumineux au "plafond" (+Z) des modules horizontaux -----------
  spr::make_box(v, idx, 1.6f, 0.14f, 0.035f); const spr::MeshHandle m_light = upl(); meshes.push_back(m_light);
  for (int i = 0; i < ISS_MODULE_N; ++i) {
    const IssModule& M = ISS_MODULES[i];
    if (M.axis == IAX_Z) continue;
    const int n = std::max(1, static_cast<int>(M.hl / 3.0f));
    for (int k = 0; k < n; ++k) {
      const float t = (n == 1) ? 0.0f : (-1.0f + 2.0f * static_cast<float>(k) / (n - 1)) * (M.hl - 1.0f);
      Vec3 p = (M.axis == IAX_X) ? Vec3{M.cx + t, M.cy, M.cz + M.r - 0.2f}
                                 : Vec3{M.cx, M.cy + t, M.cz + M.r - 0.2f};
      char nm[24]; std::snprintf(nm, sizeof nm, "PLAFONNIER_%d_%d", i, k);
      const Mat4 rot = (M.axis == IAX_Y) ? spr::rotation_axis(Vec3{0, 0, 1}, spr::PI_F * 0.5f) : Mat4::identity();
      push(m_light, spr::translation(p) * rot, glow, spr::DrawStyle::Emissive, nm);
    }
  }
}

// Contraint l'oeil (premiere personne) DANS le RESEAU de modules de l'ISS (union des
// cylindres de `ISS_MODULES`). CLE : la marge ne s'applique qu'au RAYON (paroi), PAS
// a l'axe -> aux jonctions (modules qui se chevauchent) la traversee reste franche et
// les postes d'extremite ne sont PAS bloques par des murs invisibles. L'axe n'est
// borne qu'aux vrais bouts (dead-ends), ce qui bloque juste contre les cloisons.
void clamp_station_eye(spr::Dvec3& e) {
  const double margin = 0.45;   // ecart radial mini a la paroi
  auto axial_of = [](const IssModule& M, const spr::Dvec3& p) {
    return (M.axis == IAX_X) ? (p.x - M.cx) : (M.axis == IAX_Y) ? (p.y - M.cy) : (p.z - M.cz);
  };
  auto radial_of = [](const IssModule& M, const spr::Dvec3& p) {
    const double dx = p.x - M.cx, dy = p.y - M.cy, dz = p.z - M.cz;
    return (M.axis == IAX_X) ? std::sqrt(dy * dy + dz * dz)
         : (M.axis == IAX_Y) ? std::sqrt(dx * dx + dz * dz)
                             : std::sqrt(dx * dx + dy * dy);
  };
  // 1) VALIDE si l'oeil est dans un module (rayon avec marge, axe SANS marge).
  for (int i = 0; i < ISS_MODULE_N; ++i) {
    const IssModule& M = ISS_MODULES[i];
    if (radial_of(M, e) <= M.r - margin && std::fabs(axial_of(M, e)) <= M.hl) return;
  }
  // 2) Hors de tous : ramene dans le module demandant la plus petite correction.
  int best = -1; double best_corr = 1e30;
  for (int i = 0; i < ISS_MODULE_N; ++i) {
    const IssModule& M = ISS_MODULES[i];
    const double dr = std::max(0.0, radial_of(M, e) - (M.r - margin));
    const double da = std::max(0.0, std::fabs(axial_of(M, e)) - M.hl);
    const double corr = dr * dr + da * da;
    if (corr < best_corr) { best_corr = corr; best = i; }
  }
  if (best < 0) return;
  const IssModule& M = ISS_MODULES[best];
  const double ax = std::clamp(axial_of(M, e), -static_cast<double>(M.hl), static_cast<double>(M.hl));
  double perp1, perp2;   // composantes dans le plan perpendiculaire a l'axe
  if (M.axis == IAX_X)      { perp1 = e.y - M.cy; perp2 = e.z - M.cz; }
  else if (M.axis == IAX_Y) { perp1 = e.x - M.cx; perp2 = e.z - M.cz; }
  else                      { perp1 = e.x - M.cx; perp2 = e.y - M.cy; }
  const double rad = std::sqrt(perp1 * perp1 + perp2 * perp2);
  const double rmax = M.r - margin;
  if (rad > rmax && rad > 1e-9) { const double s = rmax / rad; perp1 *= s; perp2 *= s; }
  if (M.axis == IAX_X)      { e.x = M.cx + ax;    e.y = M.cy + perp1; e.z = M.cz + perp2; }
  else if (M.axis == IAX_Y) { e.y = M.cy + ax;    e.x = M.cx + perp1; e.z = M.cz + perp2; }
  else                      { e.z = M.cz + ax;    e.x = M.cx + perp1; e.y = M.cy + perp2; }
}

// ============================ MIGRATION 2D -> 3D ============================
// Remplit le CONTENU VIVANT des panneaux holographiques de l'ISS a partir du VRAI
// modele de jeu (fen::app::Jeu) : chaque poste de l'ISS reflete un systeme reel de
// l'agence. LECTURE SEULE -> la physique/maths (astro_core, calculs de Jeu) est
// intacte. Le render-lib reste agnostique du jeu : c'est l'app (ici) qui traduit le
// modele en `spr::ZonePanel` (presentation pure, chaines bornees, aucune alloc GPU).
void fill_station_panels(const fen::app::Jeu& jeu,
                         const std::vector<spr::StationZone>& zones,
                         double iss_period_min,
                         std::vector<spr::ZonePanel>& out) {
  out.assign(zones.size(), spr::ZonePanel{});
  auto zp = [&](const char* id) -> spr::ZonePanel* {
    for (std::size_t i = 0; i < zones.size(); ++i)
      if (std::strcmp(zones[i].id, id) == 0) return &out[i];
    return nullptr;
  };
  auto KV = [](spr::ZonePanel* p, const char* k, const char* v) {
    if (!p || p->kv_count >= 6) return;
    spr::PanelKV& r = p->kv[p->kv_count++];
    std::snprintf(r.key, sizeof r.key, "%s", k);
    std::snprintf(r.val, sizeof r.val, "%s", v);
  };
  auto BAR = [](spr::ZonePanel* p, const char* k, double f) {
    if (!p || p->bar_count >= 3) return;
    spr::PanelBar& b = p->bars[p->bar_count++];
    std::snprintf(b.key, sizeof b.key, "%s", k);
    b.frac = static_cast<float>(std::clamp(f, 0.0, 1.0));
  };
  auto NOTE = [](spr::ZonePanel* p, const char* n) {
    if (!p) return;
    std::snprintf(p->note, sizeof p->note, "%s", n);
    p->filled = true;
  };
  char b[28];
  const fen::app::Agence& ag = jeu.agence;
  const char* mode = (ag.mode == fen::app::ModeAide::Pro) ? "PRO" : "NORMAL";
  const int vols = ag.reussites + ag.echecs;

  // AGENCE (Zvezda) : direction du programme (tresorerie, calendrier, confiance).
  if (spr::ZonePanel* p = zp("agence")) {
    KV(p, "PROGRAMME", ag.nom.c_str());
    std::snprintf(b, sizeof b, "%.1f M$", ag.tresorerie);  KV(p, "TRESORERIE", b);
    std::snprintf(b, sizeof b, "T+%.0f mois", ag.mois);    KV(p, "CALENDRIER", b);
    KV(p, "MODE D'AIDE", mode);
    std::snprintf(b, sizeof b, "%d offres", (int)jeu.contrats.size());  KV(p, "CONTRATS", b);
    BAR(p, "Confiance", ag.confiance);
    std::snprintf(p->status, sizeof p->status, "%s", jeu.game_over ? "FAILLITE" : "EN LIGNE");
    NOTE(p, "Direction : tresorerie, calendrier et arbitrages du programme.");
  }
  // ANALYSE (Zarya) : bilan des missions + archives (donnees/echantillons).
  if (spr::ZonePanel* p = zp("analyse")) {
    std::snprintf(b, sizeof b, "%d", vols);         KV(p, "VOLS BOUCLES", b);
    std::snprintf(b, sizeof b, "%d", ag.reussites); KV(p, "REUSSITES", b);
    std::snprintf(b, sizeof b, "%d", ag.echecs);    KV(p, "ECHECS", b);
    std::snprintf(b, sizeof b, "%.1f Gbit", jeu.donnees_gbit);   KV(p, "DONNEES", b);
    std::snprintf(b, sizeof b, "%.1f kg", jeu.echantillons_kg);  KV(p, "ECHANTILLONS", b);
    BAR(p, "Taux de reussite", vols > 0 ? static_cast<double>(ag.reussites) / vols : 0.0);
    NOTE(p, "Archives : debrief des vols, donnees et echantillons collectes.");
  }
  // OPERATIONS (Tranquility) : flotte deployee + revenus de la science.
  if (spr::ZonePanel* p = zp("operations")) {
    std::snprintf(b, sizeof b, "%d", jeu.relais_geo);        KV(p, "RELAIS GEO", b);
    std::snprintf(b, sizeof b, "%d", jeu.orbiteurs_mars);    KV(p, "ORBITEURS MARS", b);
    std::snprintf(b, sizeof b, "%d", jeu.sondes_lointaines); KV(p, "SONDES LOINT.", b);
    std::snprintf(b, sizeof b, "%.1f Gbit/mo", jeu.revenu_mensuel_gbit()); KV(p, "REVENU FLOTTE", b);
    KV(p, "ORBITE", "LEO 418 km");
    NOTE(p, "Operations : flotte en service et revenus de la science.");
  }
  // PLANIFICATION (Kibo) : contrat actif ou appels d'offre disponibles.
  if (spr::ZonePanel* p = zp("planification")) {
    if (const fen::app::Contrat* a = jeu.actif()) {
      KV(p, "CONTRAT", a->titre.c_str());
      KV(p, "CLIENT", a->client.c_str());
      std::snprintf(b, sizeof b, "%.0f M$", a->prime_succes); KV(p, "PRIME", b);
      std::snprintf(p->status, sizeof p->status, "ACTIF");
    } else if (!jeu.contrats.empty()) {
      const fen::app::Contrat& c0 = jeu.contrats.front();
      std::snprintf(b, sizeof b, "%d offres", (int)jeu.contrats.size()); KV(p, "APPELS D'OFFRE", b);
      KV(p, "PROCHAIN", c0.titre.c_str());
      KV(p, "CLIENT", c0.client.c_str());
      std::snprintf(b, sizeof b, "%.0f M$", c0.prime_succes); KV(p, "PRIME", b);
      std::snprintf(p->status, sizeof p->status, "EN ATTENTE");
    } else {
      KV(p, "CONTRATS", "aucun");
    }
    NOTE(p, "Planification : fenetres de lancement et choix de mission.");
  }
  // CONCEPTION (Columbus) : bureau d'etudes (VAB, Delta-v Tsiolkovski exact).
  if (spr::ZonePanel* p = zp("conception")) {
    static const char* MOT[4] = {"MOTEUR A", "MOTEUR B", "MOTEUR C", "MOTEUR D"};
    KV(p, "MOTEUR", MOT[std::clamp(jeu.conception.moteur, 0, 3)]);
    std::snprintf(b, sizeof b, "niveau %d", jeu.conception.niveau);    KV(p, "FIABILITE", b);
    std::snprintf(b, sizeof b, "%.0f kg", jeu.conception.vab.ergols);  KV(p, "ERGOLS", b);
    std::snprintf(b, sizeof b, "%.0f m/s", jeu.vab_dv());              KV(p, "DELTA-V", b);
    BAR(p, "Marge dv", jeu.conception.marge_dv / 200.0);
    NOTE(p, "Bureau d'etudes : assemblage et dimensionnement du lanceur.");
  }
  // CONTROLE (Destiny) : salle de vol (etat de la mission en cours).
  if (spr::ZonePanel* p = zp("controle")) {
    if (jeu.vol.commis && !jeu.vol.fini) {
      KV(p, "VOL GEO", "EN COURS");
      std::snprintf(p->status, sizeof p->status, "EN VOL");
    } else if (jeu.vinterp.commis && !jeu.vinterp.fini) {
      KV(p, "VOL INTERP.", "EN COURS");
      std::snprintf(p->status, sizeof p->status, "EN VOL");
    } else {
      KV(p, "MISSION", "AUCUNE");
      KV(p, "ETAT", "EN ATTENTE");
    }
    NOTE(p, "Controle de vol : conduite de la mission en temps reel.");
  }
  // COUPOLE (Tranquility) : orbite reelle de l'ISS (donnee de la carte).
  if (spr::ZonePanel* p = zp("observation")) {
    KV(p, "VISEE", "TERRE (NADIR)");
    KV(p, "ALTITUDE", "418 km");
    std::snprintf(b, sizeof b, "%.1f min", iss_period_min); KV(p, "PERIODE", b);
    KV(p, "INCLINAISON", "51.64 deg");
    NOTE(p, "Coupole : observation de la Terre et de l'orbite.");
  }
  // NOVELLUS (module fictif accoste) : ORDINATEUR PRINCIPAL du joueur = QG (Intro.txt).
  if (spr::ZonePanel* p = zp("novellus")) {
    KV(p, "MODULE", "NOVELLUS");
    KV(p, "QG", ag.nom.c_str());
    KV(p, "ORDINATEUR", "EN LIGNE");
    KV(p, "CARTE", "SYSTEME SOLAIRE");
    KV(p, "MESSAGERIE", "0 nouveau");
    KV(p, "QUARTIERS", "operationnels");
    std::snprintf(p->status, sizeof p->status, "QG");
    NOTE(p, "Novellus : ordinateur principal, carte du systeme solaire, quartiers, messagerie.");
  }
}

// Index d'une zone par identifiant (-1 si absente).
int zone_index(const std::vector<spr::StationZone>& zones, const char* id) {
  for (std::size_t i = 0; i < zones.size(); ++i)
    if (std::strcmp(zones[i].id, id) == 0) return static_cast<int>(i);
  return -1;
}

// Index d'un noeud tech par id (-1 si absent / nullptr).
int tech_index(const char* id) {
  if (!id) return -1;
  for (int i = 0; i < spgame::TECH_COUNT; ++i)
    if (std::strcmp(spgame::TECH_NODES[i].id, id) == 0) return i;
  return -1;
}

// ============================ PLAN DE VOL REEL =============================
// AUCUN RNG : le resultat d'une mission sort d'un VRAI CALCUL deterministe. Budget
// Δv patched-conic (Hohmann) depuis les VRAIES positions des corps a l'epoque de
// lancement (ephemeride), compare au Δv REEL du vehicule (Tsiolkovski, selon la
// propulsion/le lanceur recherches). Faisable <=> Δv_dispo >= Δv_requis.
struct MissionPlan {
  bool   feasible{false};
  double dv_required{0.0}, dv_available{0.0};
  double c3{0.0};        // km^2/s^2 (energie de lancement ; 0 pour LEO/Lune)
  double tof_days{0.0};  // duree de transfert (Hohmann)
  double dist_au{0.0};   // distance helio de la cible (0 pour LEO/Lune)
  bool   window_open{true};    // FENETRE DE LANCEMENT ouverte (angle de phase correct)
  double days_to_window{0.0};  // jours avant la prochaine fenetre (0 si ouverte)
  // --- COMPOSANTES (unites KM), source des POSTES DE CALCUL : le joueur DERIVE ces
  //     memes valeurs a la main (Pro) ou par assemblage (Normal). Ce sont les vrais
  //     chiffres d'astro_core (converties m->km, m/s->km/s, mu m3/s2->km3/s2). -----
  int    kind{0};          // TargetKind (recopie)
  double mu_earth{0.0};    // km^3/s^2
  double r_park{0.0};      // km (orbite de parking LEO)
  double v_circ{0.0};      // km/s  [LANCEMENT] vitesse circulaire parking
  double vinf_dep{0.0};    // km/s  v-infini de depart (helio)
  double dv_inj{0.0};      // km/s  [INJECTION] echappement helio OU injection trans-lunaire
  double a_transfer{0.0};  // km    [CROISIERE] demi-grand axe du transfert de Hohmann
  double mu_transfer{0.0}; // km^3/s^2 (Soleil pour helio / Terre pour lunaire)
  double r_target{0.0};    // km    rayon helio de la cible OU distance Terre-Lune
  double vinf_arr{0.0};    // km/s  v-infini d'arrivee
  double mu_target{0.0};   // km^3/s^2 (corps d'insertion ; 0 = petit corps = rendez-vous)
  double r_ins{0.0};       // km    rayon de periapse a l'insertion
  double ra_ins{0.0};      // km    apoapse de l'orbite de capture
  double dv_cap{0.0};      // km/s  [INSERTION] capture / rendez-vous
  double dv_desc{0.0};     // km/s  [ATTERRISSAGE] descente (0 si pas de pose)
  double dv_ret{0.0};      // km/s  [RETOUR] injection retour + rentree (0 si pas de retour)
};
MissionPlan compute_mission_plan(int mi, const fen::Epoch& t,
                                 const fen::ephem::StandishEphemeris& eph, double dv_avail) {
  namespace ast = fen::astro;
  using fen::cst::MU_SUN; using fen::cst::MU_EARTH; using fen::cst::MU_MOON;
  const spgame::MissionProfile& P = spgame::MISSION_PROFILE[mi];
  MissionPlan pl; pl.dv_available = dv_avail;
  const double r_park = fen::cst::R_EARTH + 300.0e3;
  const double ascent = ast::v_circular(r_park, MU_EARTH) + 1600.0;   // -> LEO (pertes reelles)
  auto rmag = [](const fen::ephem::PosVel& pv) {
    return std::sqrt(pv.r.x * pv.r.x + pv.r.y * pv.r.y + pv.r.z * pv.r.z);
  };
  // composantes communes (unites KM) pour les postes de calcul
  pl.kind    = P.kind;
  pl.mu_earth = MU_EARTH / 1.0e9;
  pl.r_park   = r_park / 1000.0;
  pl.v_circ   = ast::v_circular(r_park, MU_EARTH) / 1000.0;
  double req = ascent;
  if (P.kind == spgame::TK_LEO) {
    // req = ascent seul
  } else if (P.kind == spgame::TK_MOON_ORBIT || P.kind == spgame::TK_MOON_LAND ||
             P.kind == spgame::TK_MOON_RETURN) {
    const double d_moon = 384400.0e3;
    ast::Hohmann h = ast::hohmann(r_park, d_moon, MU_EARTH);   // injection trans-lunaire
    const double vinf_moon = std::fabs(h.dv2);
    const double rlo = fen::cst::R_MOON + 100.0e3;
    req += std::fabs(h.dv1) + ast::dv_insertion(rlo, rlo, vinf_moon, MU_MOON);   // TLI + capture
    pl.tof_days = h.tof / fen::cst::DAY;
    const double descent = ast::v_circular(rlo, MU_MOON) + 200.0;
    if (P.kind != spgame::TK_MOON_ORBIT) req += descent;                          // pose
    if (P.kind == spgame::TK_MOON_RETURN) req += descent + std::fabs(h.dv1);       // remontee + retour
    // composantes (KM) : injection = TLI ; transfert autour de la Terre ; capture Lune
    pl.dv_inj      = std::fabs(h.dv1) / 1000.0;
    pl.a_transfer  = 0.5 * (r_park + d_moon) / 1000.0;
    pl.mu_transfer = MU_EARTH / 1.0e9;
    pl.r_target    = d_moon / 1000.0;
    pl.vinf_arr    = vinf_moon / 1000.0;
    pl.mu_target   = MU_MOON / 1.0e9;
    pl.r_ins       = rlo / 1000.0;
    pl.ra_ins      = rlo / 1000.0;                 // capture circulaire (ra = rp)
    pl.dv_cap      = ast::dv_insertion(rlo, rlo, vinf_moon, MU_MOON) / 1000.0;
    if (P.kind != spgame::TK_MOON_ORBIT) pl.dv_desc = descent / 1000.0;
    if (P.kind == spgame::TK_MOON_RETURN) pl.dv_ret = (descent + std::fabs(h.dv1)) / 1000.0;
  } else {   // heliocentrique (planetes / petit corps / Soleil)
    const fen::ephem::PosVel eE = eph.state(fen::ephem::Body::EarthBary, fen::ephem::Body::Sun, t);
    const double r1 = rmag(eE);
    double r2;
    const bool tabulated = (P.body != spgame::B_NONE && P.body != fen::ephem::Body::Sun);
    fen::ephem::PosVel eT{};
    if (tabulated) { eT = eph.state(P.body, fen::ephem::Body::Sun, t); r2 = rmag(eT); }  // position REELLE
    else           { r2 = P.helio_au * fen::cst::AU; }                                    // cible non tabulee / perihelie
    pl.dist_au = r2 / fen::cst::AU;
    ast::Hohmann h = ast::hohmann(r1, r2, MU_SUN);
    // FENETRE DE LANCEMENT (cibles tabulees) : angle de phase courant Terre->cible vs
    // angle requis pour un Hohmann ; sinon, jours avant la prochaine fenetre (synodique).
    if (tabulated) {
      auto wrap = [](double a) {
        while (a >  fen::cst::PI) a -= fen::cst::TWO_PI;
        while (a < -fen::cst::PI) a += fen::cst::TWO_PI;
        return a;
      };
      const double thE = std::atan2(eE.r.y, eE.r.x), thT = std::atan2(eT.r.y, eT.r.x);
      const double phi_now = wrap(thT - thE);
      const double phi_req = wrap(ast::hohmann_phase_angle(r1, r2, MU_SUN));
      const double err = wrap(phi_now - phi_req);
      pl.window_open = std::fabs(err) < (5.0 * fen::cst::DEG);
      const double n1 = std::sqrt(MU_SUN / (r1 * r1 * r1)), n2 = std::sqrt(MU_SUN / (r2 * r2 * r2));
      const double rate = n2 - n1;                     // derivee de l'angle de phase
      if (std::fabs(rate) > 1e-14) {
        double tnext = -err / rate;
        const double syn = fen::cst::TWO_PI / std::fabs(rate);
        while (tnext < 0.0) tnext += syn;
        pl.days_to_window = pl.window_open ? 0.0 : tnext / fen::cst::DAY;
      }
    }
    const double vinf_dep = std::fabs(h.dv1), vinf_arr = std::fabs(h.dv2);
    pl.c3 = vinf_dep * vinf_dep / 1.0e6;                        // km^2/s^2
    pl.tof_days = h.tof / fen::cst::DAY;
    req += ast::dv_injection(r_park, vinf_dep, MU_EARTH);       // echappement Terre
    // composantes (KM) : injection helio + transfert autour du Soleil
    pl.vinf_dep    = vinf_dep / 1000.0;
    pl.dv_inj      = ast::dv_injection(r_park, vinf_dep, MU_EARTH) / 1000.0;
    pl.a_transfer  = 0.5 * (r1 + r2) / 1000.0;
    pl.mu_transfer = MU_SUN / 1.0e9;
    pl.r_target    = r2 / 1000.0;
    pl.vinf_arr    = vinf_arr / 1000.0;
    if (P.kind == spgame::TK_ORBIT || P.kind == spgame::TK_LAND || P.kind == spgame::TK_RETURN) {
      double dv_cap;
      if (P.body != spgame::B_NONE && P.body != fen::ephem::Body::Sun) {
        const double mu = fen::ephem::body_mu(P.body), R = fen::ephem::body_radius(P.body);
        const double rp = R + 200.0e3;
        dv_cap = ast::dv_insertion(rp, rp * 8.0, vinf_arr, mu);                 // capture elliptique
        pl.mu_target = mu / 1.0e9;                     // composantes (KM) de l'insertion
        pl.r_ins     = rp / 1000.0;
        pl.ra_ins    = rp * 8.0 / 1000.0;
        pl.dv_cap    = dv_cap / 1000.0;                // capture SEULE (avant descente)
        if (P.kind == spgame::TK_LAND) {
          const double desc = ast::v_circular(rp, mu) * 0.6;   // descente (aerofreinage)
          dv_cap += desc;
          pl.dv_desc = desc / 1000.0;
        }
      } else {
        dv_cap = vinf_arr;   // petit corps : annuler la vitesse relative (rendez-vous)
        pl.mu_target = 0.0;  // 0 -> le calcul d'insertion est un simple "annuler vinf"
        pl.dv_cap    = vinf_arr / 1000.0;
      }
      req += dv_cap;
      if (P.kind == spgame::TK_RETURN) {
        req += ast::dv_injection(r_park, vinf_arr, MU_EARTH) + vinf_arr;
        pl.dv_ret = (ast::dv_injection(r_park, vinf_arr, MU_EARTH) + vinf_arr) / 1000.0;
      }
    }
  }
  pl.dv_required = req;
  pl.feasible = (dv_avail >= req);
  return pl;
}

// ============================ POSTES DE CALCUL ============================
// Chaque ETAPE de mission porte un CALCUL que le joueur fait LUI-MEME (Pro : il tape
// la formule dans `calc::eval` ; Normal : assemblage guide). La bonne reponse vient
// d'astro_core (composantes de MissionPlan, en KM). `formula` EST la physique : evaluee
// avec `givens`, elle DOIT redonner `answer` (verifie hors-ligne par --calcsteps).
struct CalcGiven { char name[10]; double value; char unit[10]; };
struct CalcStep {
  char   phase[16];    // phase concernee ("LANCEMENT", "INJECTION", ...)
  char   find[44];     // quantite a trouver
  char   sym[8];       // symbole du resultat ("v", "dv", "t")
  char   unit[10];     // unite du resultat ("km/s", "j")
  char   formula[120]; // formule de reference (mini-langage calc::)
  char   hint[96];     // indice pedagogique (loi utilisee)
  CalcGiven givens[6];
  int    given_count{0};
  double answer{0.0};  // bonne reponse (astro_core), meme unite que `unit`
  double tol{1e-3};    // tolerance relative
};
std::vector<CalcStep> build_calc_steps(const MissionPlan& pl) {
  std::vector<CalcStep> v;
  auto step = [&](const char* phase, const char* find, const char* sym, const char* unit,
                  const char* formula, const char* hint, double answer,
                  std::initializer_list<CalcGiven> givens) {
    CalcStep s{};
    std::snprintf(s.phase, sizeof s.phase, "%s", phase);
    std::snprintf(s.find, sizeof s.find, "%s", find);
    std::snprintf(s.sym, sizeof s.sym, "%s", sym);
    std::snprintf(s.unit, sizeof s.unit, "%s", unit);
    std::snprintf(s.formula, sizeof s.formula, "%s", formula);
    std::snprintf(s.hint, sizeof s.hint, "%s", hint);
    int n = 0; for (const CalcGiven& g : givens) if (n < 6) s.givens[n++] = g;
    s.given_count = n; s.answer = answer; s.tol = 1e-3;
    v.push_back(s);
  };
  const bool lunar = (pl.kind == spgame::TK_MOON_ORBIT || pl.kind == spgame::TK_MOON_LAND ||
                      pl.kind == spgame::TK_MOON_RETURN);
  const bool helio = !(pl.kind == spgame::TK_LEO || lunar);

  // 1) LANCEMENT : vitesse circulaire sur l'orbite de parking (toutes missions)
  step("LANCEMENT", "Vitesse circulaire sur l'orbite de parking", "v", "km/s",
       "sqrt(mu/r)", "Orbite circulaire : v = racine(mu / r).", pl.v_circ,
       {{"mu", pl.mu_earth, "km3/s2"}, {"r", pl.r_park, "km"}});

  // 2) INJECTION
  if (lunar)
    step("INJECTION", "Delta-v d'injection trans-lunaire (TLI)", "dv", "km/s",
         "sqrt(mu/r)*(sqrt(2*rL/(r+rL)) - 1)",
         "Premiere impulsion de Hohmann Terre vers Lune.", pl.dv_inj,
         {{"mu", pl.mu_earth, "km3/s2"}, {"r", pl.r_park, "km"}, {"rL", pl.r_target, "km"}});
  else if (helio)
    step("INJECTION", "Delta-v d'injection (echappement Terre)", "dv", "km/s",
         "sqrt(vinf^2 + 2*mu/r) - sqrt(mu/r)",
         "Vitesse hyperbolique au perigee moins la circulaire.", pl.dv_inj,
         {{"mu", pl.mu_earth, "km3/s2"}, {"r", pl.r_park, "km"}, {"vinf", pl.vinf_dep, "km/s"}});

  // 3) CROISIERE : duree du transfert de Hohmann (helio / lunaire)
  if ((lunar || helio) && pl.a_transfer > 0.0)
    step("CROISIERE", "Duree du transfert de Hohmann", "t", "j",
         "pi*sqrt(a^3/mu)/86400",
         "Demi-periode de l'ellipse de transfert, en jours.", pl.tof_days,
         {{"a", pl.a_transfer, "km"}, {"mu", pl.mu_transfer, "km3/s2"}});

  // 4) INSERTION : capture (gravite de la cible) ou rendez-vous (petit corps)
  if (pl.dv_cap > 0.0) {
    if (pl.mu_target > 0.0)
      step("INSERTION", "Delta-v de capture", "dv", "km/s",
           "sqrt(vinf^2 + 2*mu/rp) - sqrt(mu*(2/rp - 2/(rp+ra)))",
           "Hyperbolique au periapse moins vitesse de l'orbite de capture.", pl.dv_cap,
           {{"mu", pl.mu_target, "km3/s2"}, {"rp", pl.r_ins, "km"}, {"ra", pl.ra_ins, "km"},
            {"vinf", pl.vinf_arr, "km/s"}});
    else
      step("INSERTION", "Delta-v de rendez-vous (petit corps)", "dv", "km/s",
           "vinf", "Annuler la vitesse relative (pas de gravite de capture).", pl.dv_cap,
           {{"vinf", pl.vinf_arr, "km/s"}});
  }

  // 5) ATTERRISSAGE / ALUNISSAGE
  if (pl.dv_desc > 0.0) {
    if (lunar)
      step("ATTERRISSAGE", "Delta-v de descente (Lune)", "dv", "km/s",
           "sqrt(mu/r) + 0.2", "Vitesse orbitale basse + marge de pose (0,2 km/s).", pl.dv_desc,
           {{"mu", pl.mu_target, "km3/s2"}, {"r", pl.r_ins, "km"}});
    else
      step("ATTERRISSAGE", "Delta-v de descente", "dv", "km/s",
           "0.6*sqrt(mu/r)", "Fraction de la vitesse orbitale (aerofreinage).", pl.dv_desc,
           {{"mu", pl.mu_target, "km3/s2"}, {"r", pl.r_ins, "km"}});
  }

  // 6) RETOUR (missions de retour d'echantillon)
  if (pl.dv_ret > 0.0) {
    if (lunar)
      step("RETOUR", "Delta-v de remontee + retour", "dv", "km/s",
           "(sqrt(muM/rM) + 0.2) + sqrt(muE/rp)*(sqrt(2*rL/(rp+rL)) - 1)",
           "Remontee depuis la Lune + injection retour vers la Terre.", pl.dv_ret,
           {{"muM", pl.mu_target, "km3/s2"}, {"rM", pl.r_ins, "km"}, {"muE", pl.mu_earth, "km3/s2"},
            {"rp", pl.r_park, "km"}, {"rL", pl.r_target, "km"}});
    else
      step("RETOUR", "Delta-v d'injection retour + rentree", "dv", "km/s",
           "(sqrt(vinf^2 + 2*mu/r) - sqrt(mu/r)) + vinf",
           "Injection retour depuis la cible + annulation a l'arrivee.", pl.dv_ret,
           {{"mu", pl.mu_earth, "km3/s2"}, {"r", pl.r_park, "km"}, {"vinf", pl.vinf_arr, "km/s"}});
  }
  return v;
}

// --- ETAPES d'une mission LANCEE : sequence REALISTE tiree du plan deterministe --
// Une mission ne se resout plus d'un clic : elle traverse des ETAPES (preparation,
// lancement, injection, croisiere, insertion, pose, operations, retour, verdict)
// dont les DUREES (jours) viennent du plan de vol reel (ToF Hohmann, operations
// selon la difficulte). Le joueur avance etape par etape depuis le poste CONTROLE.
struct MissionPhase { char label[24]; char detail[44]; double days; };
struct MissionRun {
  bool active{false};
  int  mission{-1};
  int  phase{0};
  std::vector<MissionPhase> phases;
  std::vector<CalcStep> calc_steps;   // calculs (repere par LABEL de phase)
  bool solved{false};                 // le calcul de la phase courante est-il resolu ?
};
std::vector<MissionPhase> build_mission_phases(int mi, const MissionPlan& pl) {
  const spgame::MissionDef& m = spgame::MISSIONS[mi];
  const int kind = spgame::MISSION_PROFILE[mi].kind;
  const int diff = std::clamp(m.difficulty, 1, 5);
  std::vector<MissionPhase> ph;
  auto add = [&](const char* label, const char* detail, double days) {
    MissionPhase p{}; std::snprintf(p.label, sizeof p.label, "%s", label);
    std::snprintf(p.detail, sizeof p.detail, "%s", detail); p.days = days; ph.push_back(p);
  };
  const double cruise = std::max(2.0, pl.tof_days);
  add("PREPARATION", "Assemblage & integration", 14.0 + diff * 7.0);
  add("LANCEMENT",   "Decollage & ascension vers LEO", 1.0);
  if (kind == spgame::TK_LEO) {
    add("MISE EN ORBITE", "Insertion en orbite basse", 1.0);
    add("OPERATIONS",     "Charge utile en service",   30.0 + diff * 10.0);
  } else if (kind == spgame::TK_MOON_ORBIT || kind == spgame::TK_MOON_LAND ||
             kind == spgame::TK_MOON_RETURN) {
    add("INJECTION",  "Injection trans-lunaire (TLI)", 1.0);
    add("CROISIERE",  "Transit Terre - Lune", std::max(3.0, cruise));
    add("INSERTION",  "Mise en orbite lunaire", 1.0);
    if (kind != spgame::TK_MOON_ORBIT) add("ALUNISSAGE", "Descente & pose sur la Lune", 1.0);
    add("OPERATIONS", "Operations autour / sur la Lune", 4.0 + diff * 4.0);
    if (kind == spgame::TK_MOON_RETURN) {
      add("REMONTEE", "Remontee & injection retour", 1.0);
      add("RETOUR",   "Transit Lune - Terre, rentree", std::max(3.0, cruise));
    }
  } else {   // heliocentrique (planete / petit corps / Soleil)
    add("INJECTION", "Echappement Terre (C3)", 1.0);
    add("CROISIERE", "Transit heliocentrique", cruise);
    if (kind == spgame::TK_FLYBY) {
      add("SURVOL", "Survol & mesures rapprochees", 2.0);
    } else {
      add("INSERTION", "Capture / rendez-vous", 3.0);
      if (kind == spgame::TK_LAND) add("ATTERRISSAGE", "Descente & pose", 2.0);
      add("OPERATIONS", "Campagne scientifique", 30.0 + diff * 12.0);
      if (kind == spgame::TK_RETURN) add("RETOUR", "Transit retour & rentree", cruise);
    }
  }
  add("VERDICT", "Mission accomplie", 0.0);
  return ph;
}

// Construit la VUE LISTE du catalogue : verrouillage par PALIER **et** par
// TECHNOLOGIES REQUISES (l'arbre de competences gate le catalogue) + fiche + puces
// + etat accompli (coche) et lançabilite (boucle de jeu).
void build_mission_views(int level, int sel_mission, const std::vector<char>& done,
                         const std::vector<char>& mdone,
                         std::vector<spr::PanelListItem>& items, spr::PanelList& list) {
  auto is_done = [&](int mi) { return mi < static_cast<int>(mdone.size()) && mdone[static_cast<std::size_t>(mi)]; };
  auto tech_done_id = [&](const char* id) {   // acquise ? (id inconnu -> considere acquis)
    const int t = tech_index(id);
    return (t < 0) || (t < static_cast<int>(done.size()) && done[static_cast<std::size_t>(t)]);
  };
  auto available = [&](int mi) {
    if (spgame::MISSIONS[mi].tier > level) return false;
    const spgame::MissionReq& r = spgame::MISSION_REQS[mi];
    for (int k = 0; k < 4 && r.tech[k]; ++k) if (!tech_done_id(r.tech[k])) return false;
    return true;
  };
  items.resize(spgame::MISSION_COUNT);
  for (int i = 0; i < spgame::MISSION_COUNT; ++i) {
    const spgame::MissionDef& m = spgame::MISSIONS[i];
    spr::PanelListItem& it = items[static_cast<std::size_t>(i)];
    std::snprintf(it.title, sizeof it.title, "%s", m.name);
    char stars[6] = "*****"; stars[std::min(std::max(m.difficulty, 0), 5)] = '\0';
    std::snprintf(it.tag, sizeof it.tag, "%d %s", m.year, stars);
    it.done   = is_done(i);
    it.locked = !it.done && !available(i);
  }
  list.items = items.data();
  list.count = static_cast<int>(items.size());
  list.selected = std::clamp(sel_mission, 0, spgame::MISSION_COUNT - 1);

  const int si = list.selected;
  list.sel_done   = is_done(si);
  list.can_launch = available(si) && !list.sel_done;
  const spgame::MissionDef& m = spgame::MISSIONS[si];
  std::snprintf(list.detail_title, sizeof list.detail_title, "%s", m.name);
  list.detail_count = 0;
  auto add = [&](const char* k, const char* v) {
    if (list.detail_count >= 7) return;
    spr::PanelKV& r = list.detail[list.detail_count++];
    std::snprintf(r.key, sizeof r.key, "%s", k);
    std::snprintf(r.val, sizeof r.val, "%s", v);
  };
  char stars[6] = "*****"; stars[std::min(std::max(m.difficulty, 0), 5)] = '\0';
  char pal[28];
  std::snprintf(pal, sizeof pal, "%s%s", spgame::tier_name(m.tier), (m.tier > level) ? "  (verrou.)" : "");
  add("LANCEUR", m.vehicle);
  add("VAISSEAU", m.craft);
  add("CHARGE UTILE", m.payload);
  add("CIBLE", m.target);
  add("DIFFICULTE", stars);
  add("PALIER REQUIS", pal);
  // puces des technologies requises (vert = acquise, rouge = a rechercher).
  list.req_count = 0;
  const spgame::MissionReq& rq = spgame::MISSION_REQS[si];
  for (int k = 0; k < 4 && rq.tech[k] && list.req_count < 6; ++k) {
    const int t = tech_index(rq.tech[k]);
    if (t < 0) continue;
    spr::PanelReq& pr = list.reqs[static_cast<std::size_t>(list.req_count++)];
    std::snprintf(pr.name, sizeof pr.name, "%s", spgame::TECH_NODES[t].name);
    pr.met = (t < static_cast<int>(done.size()) && done[static_cast<std::size_t>(t)]);
  }
  std::snprintf(list.detail_note, sizeof list.detail_note, "%s  %d  -  %s", m.agency, m.year, m.feat);
}
// Prerequis DANS la branche : le noeud de meme categorie a `depth`-1 (-1 = racine).
int tech_prereq(int i) {
  const spgame::TechNode& n = spgame::TECH_NODES[i];
  if (n.depth <= 0) return -1;
  for (int j = 0; j < spgame::TECH_COUNT; ++j)
    if (spgame::TECH_NODES[j].cat == n.cat && spgame::TECH_NODES[j].depth == n.depth - 1) return j;
  return -1;
}

// Palette d'accent par categorie (10 branches).
const spr::Vec3 CAT_COL[spgame::TECH_CAT_COUNT] = {
  {1.00f, 0.55f, 0.35f},  // PROPULSION
  {1.00f, 0.72f, 0.32f},  // LANCEURS
  {0.85f, 0.82f, 0.74f},  // STRUCTURES
  {0.45f, 0.80f, 1.00f},  // AVIONIQUE
  {1.00f, 0.85f, 0.42f},  // ENERGIE
  {0.80f, 0.60f, 1.00f},  // VOL HABITE
  {0.40f, 0.92f, 0.86f},  // SCIENCE
  {0.55f, 0.75f, 1.00f},  // COMMS
  {0.58f, 0.88f, 0.66f},  // ROBOTIQUE
  {0.74f, 0.79f, 0.92f},  // INFRASTRUCTURE
};

// Construit la GRANDE TOILE de competences : positions (px canvas), etats
// (verrouille / disponible / acquis), abordabilite, categories, points, legende.
void build_tech_views(int level, const std::vector<char>& done, int points_avail,
                      std::vector<spr::PanelTreeNode>& nodes,
                      std::vector<spr::PanelTreeLane>& lanes, spr::PanelTree& tree) {
  constexpr float LEFT = 122.0f, COLW = 178.0f, ROWH = 50.0f, TOP = 26.0f;
  int maxdepth = 0;
  for (int i = 0; i < spgame::TECH_COUNT; ++i) maxdepth = std::max(maxdepth, spgame::TECH_NODES[i].depth);
  tree.canvas_w = LEFT + (maxdepth + 1) * COLW + 24.0f;
  tree.canvas_h = TOP + spgame::TECH_CAT_COUNT * ROWH + 16.0f;

  nodes.resize(spgame::TECH_COUNT);
  int ndone = 0;
  for (int i = 0; i < spgame::TECH_COUNT; ++i) {
    const spgame::TechNode& n = spgame::TECH_NODES[i];
    spr::PanelTreeNode& o = nodes[static_cast<std::size_t>(i)];
    std::snprintf(o.label, sizeof o.label, "%s", n.name);
    o.x = LEFT + n.depth * COLW + COLW * 0.5f;
    o.y = TOP + n.cat * ROWH + ROWH * 0.5f;
    o.prereq = tech_prereq(i);
    o.xreq   = tech_index(n.xreq);
    o.cost   = n.cost;
    o.accent = CAT_COL[n.cat % spgame::TECH_CAT_COUNT];
    const bool pre_ok = (o.prereq < 0) || done[static_cast<std::size_t>(o.prereq)];
    const bool x_ok   = (o.xreq   < 0) || done[static_cast<std::size_t>(o.xreq)];
    if (done[static_cast<std::size_t>(i)]) { o.state = 2; ++ndone; }
    else if (pre_ok && x_ok && n.tier <= level) o.state = 1;
    else o.state = 0;
    o.afford = (points_avail >= n.cost);
  }
  lanes.resize(spgame::TECH_CAT_COUNT);
  for (int c = 0; c < spgame::TECH_CAT_COUNT; ++c) {
    std::snprintf(lanes[static_cast<std::size_t>(c)].name, sizeof lanes[0].name, "%s", spgame::TECH_CATS[c]);
    lanes[static_cast<std::size_t>(c)].y = TOP + c * ROWH + ROWH * 0.5f;
    lanes[static_cast<std::size_t>(c)].accent = CAT_COL[c];
  }
  tree.nodes = nodes.data(); tree.count = static_cast<int>(nodes.size());
  tree.lanes = lanes.data(); tree.lane_count = static_cast<int>(lanes.size());
  tree.points = points_avail;
  std::snprintf(tree.legend, sizeof tree.legend,
                "Niveau %s   |   %d PsR   |   %d / %d competences   |   glisser pour explorer, clic pour rechercher",
                spgame::tier_name(level), points_avail, ndone, spgame::TECH_COUNT);
}

}  // namespace

// --calctest : auto-test DETERMINISTE de l'evaluateur (calc_eval.hpp). Verifie
// l'arithmetique, la precedence, l'associativite DROITE du '^', les fonctions,
// des formules astro (circulaire, vis-viva) et des cas d'ERREUR (symbole inconnu,
// domaine, division par zero, parenthese). Renvoie 0 si tout passe.
int run_calc_selftest() {
  calc::Env env;
  env.vars["mu"] = 398600.4418;   // km^3/s^2 (Terre)
  env.vars["r"]  = 6778.0;        // km (LEO ~400 km)
  env.vars["a"]  = 24371.0;       // km (demi-grand axe GTO)
  int pass = 0, fail = 0;
  auto ok = [&](const char* expr, double expect, double tol = 1e-6) {
    const calc::Result r = calc::eval(expr, env);
    const bool good = r.ok && std::fabs(r.value - expect) <= tol * std::max(1.0, std::fabs(expect));
    std::printf("  %-32s = %-13.6g %s\n", expr, r.ok ? r.value : 0.0,
                good ? "OK" : (r.ok ? "FAIL(valeur)" : ("FAIL(" + r.error + ")").c_str()));
    good ? ++pass : ++fail;
  };
  auto bad = [&](const char* expr) {   // doit ECHOUER proprement (ok=false)
    const calc::Result r = calc::eval(expr, env);
    std::printf("  %-32s -> %s\n", expr, r.ok ? "FAIL(devait echouer)" : ("erreur OK : " + r.error).c_str());
    r.ok ? ++fail : ++pass;
  };
  std::printf("[calctest] evaluateur d'expressions :\n");
  ok("1+2*3", 7.0);
  ok("(1+2)*3", 9.0);
  ok("2^10", 1024.0);
  ok("2^3^2", 512.0);          // associatif a droite : 2^(3^2)
  ok("-3^2", -9.0);            // unaire sous la puissance
  ok("10 % 3", 1.0);
  ok("sqrt(16)", 4.0);
  ok("hypot(3,4)", 5.0);
  ok("max(min(5,9), 2)", 5.0);
  ok("deg(pi)", 180.0, 1e-9);
  ok("cos(0)+sin(0)", 1.0);
  ok("sqrt(mu/r)", std::sqrt(398600.4418 / 6778.0));               // vitesse circulaire LEO
  ok("sqrt(mu*(2/r - 1/a))", std::sqrt(398600.4418 * (2.0 / 6778.0 - 1.0 / 24371.0)));  // vis-viva
  bad("sqrt(mu/r");            // parenthese non fermee
  bad("sqrt(-1)");             // domaine
  bad("1/0");                  // division par zero
  bad("foo(2)");               // fonction inconnue
  bad("mu + z");               // variable inconnue
  bad("2 3");                  // token en trop
  std::printf("[calctest] %d OK, %d FAIL\n", pass, fail);
  return fail == 0 ? 0 : 1;
}

// --calcsteps N : DUMP + VERIFICATION hors-ligne des etapes-calcul de la mission N.
// Pour chaque etape : evalue sa `formula` avec ses `givens` (calc::eval) et compare
// a `answer` (astro_core). Prouve que les formules affichees redonnent les vrais
// chiffres. Aucune fenetre.
int run_calcsteps_dump(int mi) {
  if (mi < 0 || mi >= spgame::MISSION_COUNT) {
    std::printf("[calcsteps] mission %d hors bornes (0..%d)\n", mi, spgame::MISSION_COUNT - 1);
    return 1;
  }
  fen::ephem::StandishEphemeris eph;
  const MissionPlan pl = compute_mission_plan(mi, epoch_now(), eph, 30000.0);   // dv_avail large
  const std::vector<CalcStep> steps = build_calc_steps(pl);
  std::printf("[calcsteps] mission %d : %s  (%d etapes-calcul)\n",
              mi, spgame::MISSIONS[mi].name, static_cast<int>(steps.size()));
  int fail = 0;
  for (const CalcStep& s : steps) {
    calc::Env env;
    for (int i = 0; i < s.given_count; ++i) env.vars[s.givens[i].name] = s.givens[i].value;
    const calc::Result r = calc::eval(s.formula, env);
    const bool ok = r.ok && std::fabs(r.value - s.answer) <= std::max(1e-6, s.tol * std::fabs(s.answer));
    std::printf("  [%-12s] %s = %s\n", s.phase, s.sym, s.formula);
    for (int i = 0; i < s.given_count; ++i)
      std::printf("        %-6s = %.6g %s\n", s.givens[i].name, s.givens[i].value, s.givens[i].unit);
    std::printf("        attendu %.6g %s | eval %.6g | %s\n", s.answer, s.unit,
                r.ok ? r.value : 0.0, ok ? "OK" : (r.ok ? "MISMATCH" : ("ERR:" + r.error).c_str()));
    if (!ok) ++fail;
  }
  std::printf("[calcsteps] %d etape(s), %d FAIL\n", static_cast<int>(steps.size()), fail);
  return fail == 0 ? 0 : 1;
}

// Charge un modele GLB TEXTURE et l'ajoute comme StationParts (repere station).
// `scale_override` <= 0 -> echelle auto (plus grande arete -> `target_span` metres).
// Le CENTRE de la boite du modele est place a `pos` ; `yaw_deg` tourne autour de la
// verticale (apres Y-up glTF -> Z-up monde). Fail-safe (renvoie 0 parts si echec).
// VRAM maitrisee : cartes bornees a `max_tex` px. Renvoie le nombre de parts ajoutees.
int add_glb_station_parts(spr::IRenderDevice* dev, const std::string& glb,
                          float scale_override, float target_span,
                          const spr::Vec3& pos, float yaw_deg, int max_tex,
                          std::vector<spr::MeshHandle>& meshes,
                          std::vector<spr::MaterialHandle>& mats,
                          std::vector<spr::TextureHandle>& texs,
                          std::vector<spr::StationPart>& parts,
                          std::vector<isscol::V3>* coll_out = nullptr) {
  spr::asset::GlbModel gm = spr::asset::load_glb_model(glb, max_tex);
  if (!gm.ok()) { std::printf("[iss-int] modele indisponible : %s\n", glb.c_str()); return 0; }
  const float ex = gm.max[0] - gm.min[0], ey = gm.max[1] - gm.min[1], ez = gm.max[2] - gm.min[2];
  const float span = std::max({ex, ey, ez});
  const float scale = (scale_override > 0.0f) ? scale_override
                    : (span > 1e-6f ? target_span / span : 1.0f);
  const spr::Vec3 c{(gm.min[0] + gm.max[0]) * 0.5f, (gm.min[1] + gm.max[1]) * 0.5f,
                    (gm.min[2] + gm.max[2]) * 0.5f};
  // xform = T(pos) * R(yaw autour de Z) * R(Y-up -> Z-up) * S(scale) * T(-centre)
  const spr::Mat4 rot = spr::rotation_axis(spr::Vec3{0, 0, 1}, yaw_deg * spr::PI_F / 180.0f)
                      * spr::rotation_axis(spr::Vec3{1, 0, 0}, spr::PI_F * 0.5f);
  const spr::Mat4 xform = spr::translation(pos) * rot * spr::scaling(scale)
                        * spr::translation(spr::Vec3{-c.x, -c.y, -c.z});
  // cartes decodees -> textures GPU (index parallele a gm.images).
  std::vector<spr::TextureHandle> tex_of_img(gm.images.size(), spr::INVALID_TEXTURE);
  for (std::size_t i = 0; i < gm.images.size(); ++i) {
    const spr::asset::ImageRgba& im = gm.images[i];
    if (!im.ok()) continue;
    spr::TextureDesc td{}; td.rgba = im.pixels.data();
    td.width = static_cast<std::uint32_t>(im.width); td.height = static_cast<std::uint32_t>(im.height);
    td.srgb = (i < gm.image_linear.size()) ? (gm.image_linear[i] == 0) : true;
    tex_of_img[i] = dev->create_texture(td);
    if (tex_of_img[i] != spr::INVALID_TEXTURE) texs.push_back(tex_of_img[i]);
  }
  auto tex_at = [&](int idx) -> spr::TextureHandle {
    return (idx >= 0 && idx < static_cast<int>(tex_of_img.size())) ? tex_of_img[idx] : spr::INVALID_TEXTURE;
  };
  // DEDUPLICATION des materiaux : 1 seul materiau par combinaison (cartes + couleur)
  // -> ~49 materiaux au lieu de 312 (evite d'epuiser le pool de descripteurs Vulkan).
  struct MatCache { int img, nrm, ao; std::uint32_t bc; spr::MaterialHandle mh; };
  std::vector<MatCache> mcache;
  auto bc_key = [](const float* c) {
    auto q = [](float v) { return static_cast<std::uint32_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
    return q(c[0]) | (q(c[1]) << 8) | (q(c[2]) << 16) | (q(c[3]) << 24);
  };
  auto get_material = [&](const spr::asset::GlbSubMesh& S) -> spr::MaterialHandle {
    const std::uint32_t bck = bc_key(S.base_color);
    for (const MatCache& e : mcache)
      if (e.img == S.image_index && e.nrm == S.normal_index && e.ao == S.ao_index && e.bc == bck) return e.mh;
    spr::MaterialDesc mdm{};
    mdm.params.base_color = {S.base_color[0], S.base_color[1], S.base_color[2]};
    std::uint32_t feats = 0;
    if (const spr::TextureHandle t = tex_at(S.image_index); t != spr::INVALID_TEXTURE) { mdm.albedo = t; feats |= spr::MAT_ALBEDO_MAP; }
    if (const spr::TextureHandle t = tex_at(S.normal_index); t != spr::INVALID_TEXTURE) { mdm.normal = t; feats |= spr::MAT_NORMAL_MAP; }
    if (const spr::TextureHandle t = tex_at(S.ao_index); t != spr::INVALID_TEXTURE) { mdm.rough = t; feats |= spr::MAT_AO_MAP; }
    mdm.params.features = feats;
    const spr::MaterialHandle mh = dev->create_material(mdm); mats.push_back(mh);
    mcache.push_back({S.image_index, S.normal_index, S.ao_index, bck, mh});
    return mh;
  };
  int added = 0, n_emis = 0; std::size_t nverts = 0;
  for (const spr::asset::GlbSubMesh& S : gm.submeshes) {
    const std::size_t nv = S.positions.size() / 3;
    if (nv == 0 || S.indices.empty()) continue;
    const bool has_n  = S.normals.size() >= nv * 3;   // certains sous-maillages n'ont pas de normales/UV
    const bool has_uv = S.uvs.size()     >= nv * 2;
    std::vector<spr::Vertex> vv(nv);
    for (std::size_t i = 0; i < nv; ++i) {
      vv[i].pos    = spr::Vec3{S.positions[i * 3], S.positions[i * 3 + 1], S.positions[i * 3 + 2]};
      vv[i].normal = has_n  ? spr::Vec3{S.normals[i * 3], S.normals[i * 3 + 1], S.normals[i * 3 + 2]}
                            : spr::Vec3{0.0f, 0.0f, 1.0f};
      vv[i].uv     = has_uv ? spr::Vec2{S.uvs[i * 2], S.uvs[i * 2 + 1]} : spr::Vec2{0.0f, 0.0f};
    }
    const spr::MeshHandle m = upload_mesh(dev, vv, S.indices);
    meshes.push_back(m);
    // NEONS / ECRANS : sous-maillages EMISSIFS rendus auto-eclaires (style Emissive,
    // couleur = emissiveFactor x intensite) -> ils BRILLENT. Sinon rendu texture.
    const float es = S.emissive_strength;
    const float ei = (S.emissive[0] + S.emissive[1] + S.emissive[2]) / 3.0f * es;
    spr::StationPart p{}; p.mesh = m; p.model = xform;
    if (ei > 0.02f) {
      p.style = spr::DrawStyle::Emissive;
      p.color = spr::Vec4{S.emissive[0] * es, S.emissive[1] * es, S.emissive[2] * es, 1.0f};
      p.material = spr::INVALID_MATERIAL;   // Emissive = couleur plate auto-eclairee
      ++n_emis;
    } else {
      p.style = spr::DrawStyle::MeshTextured;
      p.material = get_material(S);
    }
    parts.push_back(p);
    // triangles de COLLISION (repere station = positions transformees par xform)
    if (coll_out) {
      auto tp = [&](std::uint32_t vi) {
        const spr::Vec4 w = xform * spr::Vec4{spr::Vec3{S.positions[vi * 3], S.positions[vi * 3 + 1],
                                                        S.positions[vi * 3 + 2]}, 1.0f};
        return isscol::V3{w.x, w.y, w.z};
      };
      for (std::size_t k = 0; k + 2 < S.indices.size(); k += 3) {
        coll_out->push_back(tp(S.indices[k]));
        coll_out->push_back(tp(S.indices[k + 1]));
        coll_out->push_back(tp(S.indices[k + 2]));
      }
    }
    ++added; nverts += nv;
  }
  std::printf("[iss-int] materiaux uniques crees : %d ; sous-maillages emissifs (neons) : %d\n",
              static_cast<int>(mcache.size()), n_emis);
  std::printf("[iss-int] modele interieur : %d sous-maillages, %zu sommets, %zu cartes ; "
              "span %.1f u -> x%.4g (echelle %.4g m/u), centre->(%.1f,%.1f,%.1f), yaw %.0f deg\n",
              added, nverts, gm.images.size(), span, span * scale, scale, pos.x, pos.y, pos.z, yaw_deg);
  return added;
}

int main(int argc, char** argv) {
  int         max_frames = 0;
  bool        validation = false;
  const char* capture_path = nullptr;
  std::string assets = SPR_ASSET_DIR;
  double cam_dist = 4.6e12, cam_pitch = 1.20, cam_yaw = 0.60;
  int    focus_index = -1;
  bool   dist_set = false;
  bool   start_in_iss = false;   // --iss : demarrer directement dans l'interieur ISS
  int    forced_hover = -1;      // --hover N : force le survol du corps N (debug)
  int    start_panel  = -1;      // --panel N : ouvre le poste N a l'entree ISS (debug/capture)
  bool   iss_cam      = false;   // --isscam : braque la camera sur l'ISS (--dist = distance) (debug)
  bool   start_iss_focus = false;// --issfocus : demarre en GROS PLAN ISS (fiche + bouton ENTRER) (debug)
  double isseye[3]    = {0, 0, 0};   // --isseye X Y Z : point de vue interieur (debug/capture)
  bool   isseye_set   = false;
  bool   yaw_set = false, pitch_set = false;
  double tau0_days = 0.0;   // saut de temps initial (jours) : test/oracle d'alignement
  int    start_menu_screen = -1; // --menuscreen N : demarre le menu sur l'ecran N (0 titre/1 difficulte/2 sauvegardes) (debug/capture)
  int    run_mission_dbg   = -1; // --runmission N : lance la mission N au demarrage (debug/capture du deroulement)
  int    run_advance_dbg   = 0;  // --runadvance K : avance de K etapes la mission lancee (debug/capture)
  bool   calc_test         = false; // --calctest : auto-test de l'evaluateur d'expressions puis sortie
  int    calc_steps_dbg    = -1;    // --calcsteps N : dump + verif des etapes-calcul de la mission N puis sortie
  bool   pro_dbg           = false; // --pro : demarre la partie demo en mode PRO (debug/capture)
  bool   calc_open_dbg     = false; // --calcopen : ouvre la console de calcul de la phase courante (debug/capture)
  bool   calc_solve_dbg    = false; // --calcsolve : pre-remplit la bonne formule + verifie (debug/capture)
  bool   load_iss_model    = true;  // MODELE 3D interieur (ISS_Internal.glb) par DEFAUT ; --placeholder pour l'ancien
  float  im_scale          = 0.0f;  // --imscale F : echelle du modele interieur (0 = auto ~55 m)
  double im_pos[3]         = {0, 0, 0}; // --impos X Y Z : position du CENTRE du modele (repere station)
  float  im_yaw            = 0.0f;   // --imyaw D : rotation du modele autour de la verticale (deg)
  bool   no_clamp          = false;  // --noclamp : desactive la contrainte de collision (inspection libre)
  bool   f5_test           = false;  // --f5test : declenche un F5 (quicksave) auto a l'entree (test du save de position)
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) max_frames = std::atoi(argv[++i]);
    else if (std::strcmp(argv[i], "--validation") == 0) validation = true;
    else if (std::strcmp(argv[i], "--capture") == 0 && i + 1 < argc) capture_path = argv[++i];
    else if (std::strcmp(argv[i], "--assets") == 0 && i + 1 < argc) assets = argv[++i];
    else if (std::strcmp(argv[i], "--dist") == 0 && i + 1 < argc) { cam_dist = std::atof(argv[++i]); dist_set = true; }
    else if (std::strcmp(argv[i], "--pitch") == 0 && i + 1 < argc) { cam_pitch = std::atof(argv[++i]); pitch_set = true; }
    else if (std::strcmp(argv[i], "--yaw") == 0 && i + 1 < argc) { cam_yaw = std::atof(argv[++i]); yaw_set = true; }
    else if (std::strcmp(argv[i], "--focus") == 0 && i + 1 < argc) focus_index = std::atoi(argv[++i]);
    else if (std::strcmp(argv[i], "--tau") == 0 && i + 1 < argc) tau0_days = std::atof(argv[++i]);
    else if (std::strcmp(argv[i], "--iss") == 0) start_in_iss = true;
    else if (std::strcmp(argv[i], "--hover") == 0 && i + 1 < argc) forced_hover = std::atoi(argv[++i]);
    else if (std::strcmp(argv[i], "--panel") == 0 && i + 1 < argc) start_panel = std::atoi(argv[++i]);
    else if (std::strcmp(argv[i], "--menuscreen") == 0 && i + 1 < argc) start_menu_screen = std::atoi(argv[++i]);
    else if (std::strcmp(argv[i], "--runmission") == 0 && i + 1 < argc) run_mission_dbg = std::atoi(argv[++i]);
    else if (std::strcmp(argv[i], "--runadvance") == 0 && i + 1 < argc) run_advance_dbg = std::atoi(argv[++i]);
    else if (std::strcmp(argv[i], "--calctest") == 0) calc_test = true;
    else if (std::strcmp(argv[i], "--calcsteps") == 0 && i + 1 < argc) calc_steps_dbg = std::atoi(argv[++i]);
    else if (std::strcmp(argv[i], "--pro") == 0) pro_dbg = true;
    else if (std::strcmp(argv[i], "--calcopen") == 0) calc_open_dbg = true;
    else if (std::strcmp(argv[i], "--calcsolve") == 0) { calc_open_dbg = true; calc_solve_dbg = true; }
    else if (std::strcmp(argv[i], "--issmodel") == 0) load_iss_model = true;
    else if (std::strcmp(argv[i], "--placeholder") == 0) load_iss_model = false;   // repli : ancien interieur cylindres
    else if (std::strcmp(argv[i], "--imscale") == 0 && i + 1 < argc) im_scale = static_cast<float>(std::atof(argv[++i]));
    else if (std::strcmp(argv[i], "--impos") == 0 && i + 3 < argc) {
      im_pos[0] = std::atof(argv[++i]); im_pos[1] = std::atof(argv[++i]); im_pos[2] = std::atof(argv[++i]);
    }
    else if (std::strcmp(argv[i], "--imyaw") == 0 && i + 1 < argc) im_yaw = static_cast<float>(std::atof(argv[++i]));
    else if (std::strcmp(argv[i], "--noclamp") == 0) no_clamp = true;
    else if (std::strcmp(argv[i], "--f5test") == 0) f5_test = true;
    else if (std::strcmp(argv[i], "--isscam") == 0) iss_cam = true;
    else if (std::strcmp(argv[i], "--issfocus") == 0) start_iss_focus = true;
    else if (std::strcmp(argv[i], "--isseye") == 0 && i + 3 < argc) {
      isseye[0] = std::atof(argv[++i]); isseye[1] = std::atof(argv[++i]); isseye[2] = std::atof(argv[++i]);
      isseye_set = true;
    }
  }
  if (calc_test) return run_calc_selftest();          // auto-test hors-ligne (aucune fenetre)
  if (calc_steps_dbg >= 0) return run_calcsteps_dump(calc_steps_dbg);   // dump/verif etapes-calcul
  const std::string A = assets + "/";

  // --- 1) verite : ephemeride helio-centre, instant REEL ----------------------
  fen::ephem::StandishEphemeris eph;
  const double MU_SUN = fen::ephem::body_mu(Body::Sun);
  spr::WorldConfig cfg;
  cfg.central = Body::Sun;
  cfg.bodies = {Body::Mercury, Body::Venus, Body::EarthBary, Body::Moon,
                Body::Mars, Body::Jupiter, Body::Saturn,
                Body::Uranus, Body::Neptune, Body::Pluto};
  spr::DataBridge bridge(eph, cfg);
  const fen::Epoch t0 = epoch_now();

  // --- 2) rendu ---------------------------------------------------------------
  spr::RenderCore core;
  spr::RenderConfig rc;
  rc.title = "SPACE PROGRAM - Systeme solaire";
  rc.validation = validation;
  try {
    if (!core.init(rc)) { std::fprintf(stderr, "Echec init RenderCore\n"); return 1; }
  } catch (const std::exception& e) {
    std::fprintf(stderr, "Exception init : %s\n", e.what()); return 1;
  }
  core.camera().mode = (focus_index >= 0) ? spr::CameraMode::BodyCentered : spr::CameraMode::Map;
  if (focus_index >= 0 && !dist_set) cam_dist = 2.0e7;
  core.camera().set_distance(cam_dist);
  core.camera().pitch = cam_pitch;
  core.camera().yaw = cam_yaw;
  core.camera().fov_deg = 45.0f;

  spr::IRenderDevice* dev = core.device();
  spr::MapView map;
  map.body_min_size = 0.0f;   // taille reelle stricte
  int current_focus = focus_index;   // -1 = vue libre

  // --- 3) corps : rayons, axes, materiaux (surfaces depuis les GLB) -----------
  std::printf("[map] chargement des textures (pleine resolution, sans compression)...\n");
  spr::RenderSnapshot snap0 = bridge.freeze(t0);
  std::vector<spr::MaterialHandle> owned_mats;
  int earth_index = -1, venus_index = -1, saturn_index = -1;

  for (int k = 0; k < snap0.body_count; ++k) {
    const spr::BodyView& bv = snap0.bodies[k];
    const BodyDef* d = def_for(bv.id);
    if (!d) continue;
    map.body_radius[k] = d->radius;
    if (bv.id == static_cast<int>(Body::EarthBary)) earth_index = k;
    if (bv.id == static_cast<int>(Body::Venus))     venus_index = k;
    if (bv.id == static_cast<int>(Body::Saturn))    saturn_index = k;
    if (!dev || d->is_sun) continue;   // Soleil : emissif (pas de texture)

    spr::MaterialDesc md;
    if (bv.id == static_cast<int>(Body::EarthBary)) {
      // Terre : surface = daymap (assets/textures), nuit = nightmap du GLB.
      spr::TextureHandle day   = tex_from_file(dev, A + "textures/Earth/8k_earth_daymap.jpg", "earth day");
      spr::TextureHandle night = tex_from_glb(dev, A + "3D models/Earth/Earth.glb", "nightmap", "earth night");
      md = spr::default_planet_material(spr::SurfaceArchetype::EarthLike);
      std::uint32_t f = spr::MAT_ALBEDO_MAP;
      md.params.base_color = {1, 1, 1};
      md.albedo = day;
      if (night != spr::INVALID_TEXTURE) { f |= spr::MAT_NIGHT_MAP | spr::MAT_NIGHT_LIGHTS; md.night = night; }
      md.params.features = f;
    } else {
      spr::TextureHandle surf = tex_from_glb(dev, A + d->glb, d->glb_img, d->glb_img);
      if (surf == spr::INVALID_TEXTURE) continue;
      md = spr::default_planet_material(static_cast<spr::SurfaceArchetype>(d->archetype));
      md.params.features = spr::MAT_ALBEDO_MAP;
      md.params.base_color = {1, 1, 1};
      md.albedo = surf;
    }
    spr::MaterialHandle mh = dev->create_material(md);
    map.body_material[k] = mh;
    owned_mats.push_back(mh);
  }

  // --- 4) coquilles : nuages Terre + atmosphere Venus (rotation propre) -------
  // Orientees comme le corps (meme pole IAU) mais avec leur PROPRE meridien
  // W0+Wdot*d -> rotation relative a la surface.
  struct ShellDef { int body; float rfac; double a0, del0, W0, Wdot; };
  std::vector<ShellDef>     shell_defs;
  std::vector<spr::MapShell> shells;
  const BodyDef* ed = def_for(static_cast<int>(Body::EarthBary));
  const BodyDef* vd = def_for(static_cast<int>(Body::Venus));
  if (dev && earth_index >= 0 && ed) {
    spr::TextureHandle clouds = tex_from_file(dev, A + "textures/Earth/8k_earth_clouds.jpg", "earth clouds");
    if (clouds != spr::INVALID_TEXTURE) {
      spr::MaterialDesc md; md.params.features = spr::MAT_CLOUDS; md.params.emissive = 0.95f;
      md.params.base_color = {1, 1, 1}; md.albedo = clouds;
      spr::MaterialHandle mh = dev->create_material(md); owned_mats.push_back(mh);
      // nuages : tournent avec la surface + derive lente relative (~+8 deg/jour)
      shell_defs.push_back({earth_index, 1.010f, ed->a0, ed->del0, ed->W0, ed->Wdot + 8.0});
      shells.push_back(spr::MapShell{earth_index, mh, 1.010f, spr::Mat4::identity()});
    }
  }
  if (dev && venus_index >= 0 && vd) {
    spr::TextureHandle atmo = tex_from_file(dev, A + "textures/Venus/4k_venus_atmosphere.jpg", "venus atmo");
    if (atmo != spr::INVALID_TEXTURE) {
      spr::MaterialDesc md; md.params.features = spr::MAT_ATMOSPHERE; md.params.emissive = 0.9f;
      md.params.base_color = {1, 1, 1}; md.albedo = atmo;
      spr::MaterialHandle mh = dev->create_material(md); owned_mats.push_back(mh);
      // super-rotation retrograde ~ 4 jours (independante de la surface tres lente)
      shell_defs.push_back({venus_index, 1.020f, vd->a0, vd->del0, vd->W0, -90.0});
      shells.push_back(spr::MapShell{venus_index, mh, 1.020f, spr::Mat4::identity()});
    }
  }
  map.shells = shells.data();
  map.shell_count = static_cast<int>(shells.size());

  // Anneaux de Saturne : maillage annulus + bandeau radial (assets/textures).
  std::vector<spr::MapRing> rings;
  const BodyDef* sd = def_for(static_cast<int>(Body::Saturn));
  if (dev && saturn_index >= 0 && sd) {
    spr::TextureHandle rt = tex_from_file(dev, A + "textures/Saturne/8k_saturn_ring_alpha.png", "saturn rings");
    if (rt != spr::INVALID_TEXTURE) {
      spr::MaterialDesc md; md.params.features = spr::MAT_ALBEDO_MAP;
      md.params.base_color = {1, 1, 1}; md.albedo = rt;
      spr::MaterialHandle mh = dev->create_material(md); owned_mats.push_back(mh);
      // plan equatorial de Saturne (orientation IAU, meridien indifferent -> W=0)
      rings.push_back(spr::MapRing{saturn_index, mh, iau_orientation(sd->a0, sd->del0, 0.0)});
    }
  }
  map.rings = rings.data();
  map.ring_count = static_cast<int>(rings.size());

  // --- lunes : textures GLB + orbite declaree autour du parent ----------------
  struct MoonRT { int parent_idx; spr::Vec3 pole; double a0, del0;
                  double a_m, e, inc, n, raan0, argp0, M0, raan_dot, argp_dot; };
  std::vector<MoonRT>        moon_rt;
  std::vector<spr::MapBody>  extra;
  for (const MoonDef& md : MOONS) {
    int pidx = -1;
    for (int k = 0; k < snap0.body_count; ++k)
      if (snap0.bodies[k].id == static_cast<int>(md.parent)) { pidx = k; break; }
    if (pidx < 0) continue;
    const BodyDef* pd = def_for(static_cast<int>(md.parent));
    if (!pd) continue;
    MoonRT r{};
    r.parent_idx = pidx;
    r.pole = pole_axis(pd->a0, pd->del0);   // pole du parent (plan orbital)
    r.a0 = pd->a0; r.del0 = pd->del0;
    r.a_m = md.a_km * 1000.0;
    r.e   = md.e;
    r.inc = md.inc_deg * DEG;
    r.n   = TWO_PI / (md.period_days * DAY);          // mouvement moyen (rad/s), periode siderale
    r.raan0 = md.raan_deg * DEG;                      // elements a l'epoque J2000
    r.argp0 = md.argp_deg * DEG;
    r.M0    = md.M0_deg   * DEG;
    // Precession seculaire due au J2 du parent (physique) : noeud regresse, periastre avance.
    const double J2 = parent_j2(md.parent);
    const double p  = r.a_m * (1.0 - r.e * r.e);
    const double Rp = pd->radius;
    const double fac = 1.5 * r.n * J2 * (Rp / p) * (Rp / p);
    r.raan_dot = -fac * std::cos(r.inc);
    r.argp_dot =  fac * (2.0 - 2.5 * std::sin(r.inc) * std::sin(r.inc));
    spr::MapBody mb{};
    mb.radius = md.radius_km * 1000.0;
    mb.color = {0.72f, 0.72f, 0.74f};
    std::strncpy(mb.name, md.name, sizeof(mb.name) - 1);
    if (dev) {
      spr::TextureHandle tx = tex_from_glb(dev, A + md.glb, md.img, md.name);
      if (tx != spr::INVALID_TEXTURE) {
        spr::MaterialDesc mm = spr::default_planet_material(spr::SurfaceArchetype::Rock);
        mm.params.features = spr::MAT_ALBEDO_MAP; mm.params.base_color = {1, 1, 1}; mm.albedo = tx;
        mb.material = dev->create_material(mm); owned_mats.push_back(mb.material);
      }
    }
    moon_rt.push_back(r);
    extra.push_back(mb);
  }
  map.extra_bodies = extra.data();
  map.extra_count = static_cast<int>(extra.size());

  // --- 5) orbites (planetes) : echantillonnees via astro_core + fondu ---------
  struct OrbitTrail { std::vector<spr::Dvec3> pts; std::vector<float> alpha; Body body; };
  std::vector<OrbitTrail>    trail_storage;
  std::vector<spr::MapTrail> trails;
  trail_storage.reserve(snap0.body_count);
  trails.reserve(snap0.body_count);
  constexpr int N = 360;
  for (int k = 0; k < snap0.body_count; ++k) {
    const spr::BodyView& bv = snap0.bodies[k];
    if (bv.is_star || bv.id == static_cast<int>(Body::Moon)) continue;
    fen::ephem::PosVel pv = eph.state(static_cast<Body>(bv.id), Body::Sun, t0);
    fen::astro::Elements el = fen::astro::rv_to_elements(pv.r, pv.v, MU_SUN);
    if (!std::isfinite(el.a) || !(el.e < 1.0)) continue;
    OrbitTrail tr; tr.body = static_cast<Body>(bv.id); tr.pts.resize(N); tr.alpha.assign(N, 1.0f);
    for (int i = 0; i < N; ++i) {
      fen::astro::Elements e = el; e.nu = TWO_PI * static_cast<double>(i) / N;
      fen::Vec3 rr, vv; fen::astro::elements_to_rv(e, MU_SUN, rr, vv);
      tr.pts[i] = spr::Dvec3{rr.x, rr.y, rr.z};
    }
    trail_storage.push_back(std::move(tr));
    spr::MapTrail mt; mt.points = trail_storage.back().pts.data();
    mt.alpha = trail_storage.back().alpha.data(); mt.count = N; mt.color = bv.color; mt.closed = true;
    trails.push_back(mt);
  }

  // --- ISS : elements d'orbite + trace (autour de la Terre, refaite chaque frame) --
  const spr::Vec3 iss_pole = ed ? pole_axis(ed->a0, ed->del0) : spr::Vec3{0, 0, 1};
  const double a_iss = (ed ? ed->radius : 6.371e6) + ISS_ALT;
  const double mu_earth = fen::ephem::body_mu(Body::EarthBary);
  const double iss_period = TWO_PI * std::sqrt(a_iss * a_iss * a_iss / mu_earth);
  std::vector<spr::Dvec3> iss_pts(ISS_ORBIT_N);
  std::vector<float>      iss_alpha(ISS_ORBIT_N, 0.0f);
  const int iss_trail_index = static_cast<int>(trails.size());
  {
    spr::MapTrail it; it.points = iss_pts.data(); it.alpha = iss_alpha.data();
    it.count = 0;   // masquee tant qu'on n'est pas pres de la Terre
    it.color = spr::Vec3{0.55f, 0.85f, 1.0f}; it.closed = true;
    trails.push_back(it);
  }
  map.trails = trails.data();
  map.trail_count = static_cast<int>(trails.size());

  // --- 6) fond : Voie lactee (8K, sans compression) ---------------------------
  if (dev) {
    spr::TextureHandle milky = tex_from_file(dev, A + "textures/8k_stars_milky_way.jpg", "milky way");
    if (milky != spr::INVALID_TEXTURE) {
      spr::MaterialDesc md; md.params.archetype = spr::SurfaceArchetype::Star;
      md.params.features = spr::MAT_ALBEDO_MAP; md.params.emissive = 0.85f;
      md.params.base_color = {1, 1, 1}; md.albedo = milky;
      spr::MaterialHandle mh = dev->create_material(md); owned_mats.push_back(mh);
      map.skybox_material = mh;
      map.skybox_rot = spr::rotation_axis(spr::Vec3{1, 0, 0}, 60.0f * 3.14159265f / 180.0f);
    }
  }
  std::printf("[map] pret. epoque REELLE %s ; %d orbites\n", snap0.epoch_iso, map.trail_count);

  // NOVELLUS = le QG : point d'apparition ET module ou TOUS les postes sont
  // regroupes (exigence user). Position RELEVEE par le user (F5 dans le 2e module
  // japonais, au fond du couloir principal) ; on regarde le couloir (vers -X).
  const spr::Dvec3 novellus_pos{19.68, -3.67, -1.10};
  const double     novellus_yaw = 3.19, novellus_pitch = -0.03;

  // --- interieur ISS (QG) : geometrie placeholder composee une fois -----------
  spr::StationView              station;
  std::vector<spr::StationPart> iss_parts;
  std::vector<spr::StationZone> iss_zones;
  std::vector<spr::MeshHandle>  iss_meshes;
  std::vector<spr::TextureHandle> iss_int_textures;   // cartes du modele interieur (nettoyees au shutdown)
  isscol::Mesh iss_coll;             // COLLISION MAILLEE (BVH) du modele interieur
  bool have_iss_model = false;       // le modele est-il charge (-> collision maillee, plus de placeholder) ?
  if (dev) {
    build_iss(dev, iss_meshes, iss_parts, iss_zones);
    // MODELE 3D INTERIEUR (defaut) : REMPLACE le placeholder cylindres. La COLLISION
    // devient MAILLEE (contre la vraie geometrie). Alignable via --imscale/--impos/--imyaw.
    if (load_iss_model) {
      const std::size_t nph = iss_parts.size();   // pieces placeholder deja creees
      std::vector<isscol::V3> coll_tris;
      const int added = add_glb_station_parts(
          dev, A + "3D models/ISS/ISS_Internal.glb", im_scale, 55.0f,
          spr::Vec3{static_cast<float>(im_pos[0]), static_cast<float>(im_pos[1]),
                    static_cast<float>(im_pos[2])},
          im_yaw, 0, iss_meshes, owned_mats, iss_int_textures, iss_parts, &coll_tris);   // 0 = PLEINE RESOLUTION (aucune reduction)
      if (added > 0) {
        iss_parts.erase(iss_parts.begin(), iss_parts.begin() + nph);   // retire le placeholder visuel
        iss_coll.build(coll_tris);
        have_iss_model = true;
        std::printf("[iss-int] collision maillee : %zu triangles\n", iss_coll.triangle_count());
      }
    }
    // REGROUPER tous les postes dans NOVELLUS (exigence user) : on dispose les zones
    // en ligne le long de l'axe X du module autour de novellus_pos, espacees pour que
    // la detection de proximite distingue chaque poste. (Avec le vrai modele : plus de
    // mobilier placeholder ; les zones sont des marqueurs interactifs.)
    if (have_iss_model && !iss_zones.empty()) {
      const int n = static_cast<int>(iss_zones.size());
      for (int i = 0; i < n; ++i) {
        const double off = (static_cast<double>(i) - (n - 1) / 2.0) * 1.7;
        iss_zones[static_cast<std::size_t>(i)].center =
            spr::Dvec3{novellus_pos.x + off, novellus_pos.y, novellus_pos.z};
        iss_zones[static_cast<std::size_t>(i)].radius = 1.5;
      }
    }
    station.parts = iss_parts.data();  station.part_count = static_cast<int>(iss_parts.size());
    station.zones = iss_zones.data();  station.zone_count = static_cast<int>(iss_zones.size());
    // Ambiance de cabine : ambiant plus RELEVE pour le vrai modele (surfaces detaillees
    // a lire) ; les neons EMISSIFS ressortent en plus. Placeholder = ambiant tres bas.
    station.ambient     = have_iss_model ? spr::Vec3{0.17f, 0.18f, 0.21f}
                                         : spr::Vec3{0.055f, 0.065f, 0.085f};
    station.light_color = spr::Vec3{0.90f, 0.93f, 1.00f};
    std::printf("[map] ISS composee : %d pieces, %d postes\n",
                station.part_count, station.zone_count);
  }
  // POINT D'APPARITION dans l'ISS : DANS un module du vrai modele (confirme par
  // inspection) si charge, sinon le noeud central du placeholder. (Repositionnement
  // fin du spawn + des postes = passe suivante.)
  const spr::Dvec3 iss_spawn = have_iss_model ? novellus_pos          // spawn DANS Novellus
                                              : spr::Dvec3{-1.5, 0.0, 0.4};
  // POSITION 3D chargee depuis une sauvegarde (F5 la persiste) : a l'entree dans
  // l'ISS on reprend LA OU on etait, pas au spawn de base. Consommee a la 1re entree.
  spr::Dvec3 loaded_eye{};
  double     loaded_yaw = 0.0, loaded_pitch = -0.05;
  bool       has_loaded_pos = false;

  // --- MIGRATION 2D -> 3D : le VRAI modele de jeu alimente les panneaux ISS ----
  // On instancie l'agence (modele `fen::app::Jeu`, LECTURE SEULE cote 3D) et on
  // traduit son etat en contenu vivant des postes. La physique/maths reste intacte.
  // L'agence (modele `fen::app::Jeu`, LECTURE SEULE cote 3D) n'est PLUS creee au
  // boot : c'est le MENU (ecran titre) qui la fonde (Nouvelle partie) ou la charge
  // (Reprendre), via les helpers `demarrer_partie` / `charger_partie` definis plus
  // bas. Le 3D traduit ensuite son etat en contenu vivant des postes.
  fen::app::Jeu jeu;

  std::vector<spr::ZonePanel>     station_panels;
  std::vector<spr::PanelListItem> mission_items;   // possede la liste (stable apres 1er build)
  std::vector<spr::PanelTreeNode> tree_nodes;      // possede les noeuds de l'arbre
  std::vector<spr::PanelTreeLane> tree_lanes;      // libelles de categorie
  spr::PanelList mission_list{};
  spr::PanelTree tech_tree{};
  std::vector<char> tech_done(spgame::TECH_COUNT, 0);        // competences RECHERCHEES (etat 3D)
  std::vector<char> mission_done(spgame::MISSION_COUNT, 0);  // missions ACCOMPLIES (boucle de jeu)
  int sel_mission = 17;         // Apollo 11 par defaut (indice dans MISSIONS)
  int tech_spent  = 0;          // points de recherche depenses par recherche INTERACTIVE
  int mission_psr_bonus = 0;    // PsR gagnes en accomplissant des missions (boucle)
  MissionRun run;                          // mission LANCEE en cours (etapes) ; inactive au depart
  std::vector<spr::PanelStep> flight_steps; // possede la checklist affichee (poste CONTROLE)
  const int agence_idx   = zone_index(iss_zones, "agence");        // arbre de competences
  const int planif_idx   = zone_index(iss_zones, "planification"); // catalogue de missions
  const int controle_idx = zone_index(iss_zones, "controle");      // deroulement du vol (etapes)
  auto agency_lvl = [&]() {
    return spgame::agency_level(jeu.agence.reussites, jeu.relais_geo, jeu.orbiteurs_mars,
                                jeu.sondes_lointaines, jeu.agence.tresorerie);
  };
  // Epoque de LANCEMENT = calendrier de l'agence (avance avec `mois`). Fait bouger
  // les corps -> les fenetres de lancement s'ouvrent/se ferment reellement.
  auto launch_epoch = [&]() { return t0 + (jeu.epoch_courant() - t0.tdb); };
  // Points de recherche DISPONIBLES = acquis (progression de l'agence) - depenses
  // interactives. La pre-acquisition (paliers inferieurs) est GRATUITE (historique).
  auto tech_points = [&]() {
    const int earned = 60 + jeu.agence.reussites * 5 + jeu.relais_geo * 3 +
                       jeu.orbiteurs_mars * 5 + jeu.sondes_lointaines * 6 +
                       static_cast<int>(jeu.agence.tresorerie / 20.0) + mission_psr_bonus;
    return std::max(0, earned - tech_spent);
  };
  // Δv REEL du vehicule (Tsiolkovski) selon la PROPULSION et le LANCEUR recherches :
  // meilleur Isp d'etage + rapport de masse (structures) + nombre d'etages (lanceur)
  // + eventuel etage ionique. Aucun RNG : c'est la capacite reelle a fournir du Δv.
  auto vehicle_dv = [&]() {
    auto d = [&](const char* id) {
      const int t = tech_index(id);
      return t >= 0 && t < static_cast<int>(tech_done.size()) && tech_done[static_cast<std::size_t>(t)];
    };
    double isp = 255.0;                       // propergol solide
    if (d("p_kero"))   isp = 311.0;
    if (d("p_hyper"))  isp = 320.0;
    if (d("p_cryo"))   isp = 450.0;
    if (d("p_staged")) isp = 462.0;
    const double mr = d("s_comp") ? 5.2 : 4.2;   // rapport de masse par etage (structures)
    int stages = 1;
    if (d("l_small") || d("l_med"))  stages = 2;
    if (d("l_heavy") || d("l_super")) stages = 3;
    double dv = stages * isp * fen::cst::G0 * std::log(mr);
    if (d("p_ion")) dv += 7000.0;                // etage ionique (espace lointain)
    return dv;
  };
  // MISSION EN PLUSIEURS ETAPES (le PLAN DE VOL REEL decide, aucun RNG) :
  //  . launch_mission : le Δv reel couvre-t-il le Δv requis + fenetre ouverte ?
  //    si oui, DEMARRE le vol (etapes) ; sinon rien. NE termine PLUS d'un coup.
  //  . advance_run    : etape suivante depuis CONTROLE (le calendrier avance de la
  //    duree reelle de l'etape) ; l'entree en VERDICT applique le SUCCES.
  //  . complete_run   : gains du succes (etat de jeu, jamais la physique) ->
  //    reussites/flotte font monter le niveau -> deverrouille la suite.
  auto complete_run = [&](int mi) {
    if (mi < 0 || mi >= spgame::MISSION_COUNT || mission_done[static_cast<std::size_t>(mi)]) return;
    const spgame::MissionDef& m = spgame::MISSIONS[mi];
    mission_done[static_cast<std::size_t>(mi)] = 1;
    jeu.agence.reussites += 1;
    jeu.agence.confiance  = std::min(1.0, jeu.agence.confiance + 0.015);
    jeu.agence.tresorerie += 6.0 + m.difficulty * 3.0;
    jeu.donnees_gbit      += 4.0 * m.difficulty;   // le calendrier a deja avance (etapes)
    mission_psr_bonus     += m.difficulty * 2 + m.tier * 2 + 3;
    const char* t = m.target;   // flotte selon la cible (pese sur le niveau)
    if (std::strstr(t, "basse"))
      jeu.relais_geo += 1;
    else if (std::strstr(t, "Lune") || std::strstr(t, "Mars") || std::strstr(t, "Venus") ||
             std::strstr(t, "Mercure") || std::strstr(t, "Soleil"))
      jeu.orbiteurs_mars += 1;
    else
      jeu.sondes_lointaines += 1;   // systeme externe / comete / asteroide / L2
  };
  auto launch_mission = [&](int mi) {   // DEMARRE le vol (ne l'accomplit pas)
    if (run.active) return;             // une mission a la fois
    if (mi < 0 || mi >= spgame::MISSION_COUNT || mission_done[static_cast<std::size_t>(mi)]) return;
    const MissionPlan pl = compute_mission_plan(mi, launch_epoch(), eph, vehicle_dv());
    if (!pl.feasible || !pl.window_open) return;   // Δv insuffisant OU fenetre fermee
    run.active = true; run.mission = mi; run.phase = 0; run.solved = false;
    run.phases = build_mission_phases(mi, pl);
    run.calc_steps = build_calc_steps(pl);         // les calculs a faire (par phase)
  };
  // Le CALCUL de la phase COURANTE (ou nullptr si la phase n'en a pas : prep/croisiere...).
  auto current_calc = [&]() -> const CalcStep* {
    if (!run.active || run.phase < 0 || run.phase >= static_cast<int>(run.phases.size())) return nullptr;
    const char* label = run.phases[static_cast<std::size_t>(run.phase)].label;
    for (const CalcStep& s : run.calc_steps)
      if (std::strcmp(s.phase, label) == 0) return &s;
    return nullptr;
  };
  // lu DYNAMIQUEMENT (l'agence est creee plus tard par le menu / demarrer_partie).
  auto mode_pro = [&]() { return jeu.agence.mode == fen::app::ModeAide::Pro; };
  auto advance_run = [&]() {   // etape suivante (bouton CONTROLE) ; verdict = succes
    if (!run.active) return;
    const int last = static_cast<int>(run.phases.size()) - 1;
    if (run.phase < last) {
      // VERROU (mode PRO) : si la phase a un calcul non resolu, on ne passe pas.
      if (mode_pro() && current_calc() && !run.solved) return;
      jeu.agence.mois += run.phases[static_cast<std::size_t>(run.phase)].days / 30.44;  // temps reel de l'etape
      run.phase++;
      run.solved = false;   // nouvelle phase -> nouveau calcul a resoudre
      if (run.phase == last) complete_run(run.mission);   // entree en VERDICT : succes applique
    } else {
      run.active = false;   // CLORE : on quitte le suivi du vol
    }
  };
  // Ouvre la console de calcul (mode PRO) sur le calcul de la phase courante.
  auto open_calc = [&]() {
    const CalcStep* s = current_calc();
    if (!s) return;
    spr::CalcConsole& c = station.calc;
    c = spr::CalcConsole{};
    std::snprintf(c.title, sizeof c.title, "%s", s->phase);
    std::snprintf(c.find,  sizeof c.find,  "%s", s->find);
    std::snprintf(c.sym,   sizeof c.sym,   "%s", s->sym);
    std::snprintf(c.unit,  sizeof c.unit,  "%s", s->unit);
    std::snprintf(c.hint,  sizeof c.hint,  "%s", s->hint);
    c.given_count = s->given_count;
    for (int i = 0; i < s->given_count; ++i)
      std::snprintf(c.givens[i], sizeof c.givens[i], "%s = %.6g %s",
                    s->givens[i].name, s->givens[i].value, s->givens[i].unit);
    c.solved = run.solved;
    c.active = true;
  };
  // VERIFIE la formule tapee (calc::eval) contre la vraie reponse d'astro_core.
  auto verify_calc = [&]() {
    const CalcStep* s = current_calc();
    spr::CalcConsole& c = station.calc;
    if (!s) { c.active = false; return; }
    calc::Env env;
    for (int i = 0; i < s->given_count; ++i) env.vars[s->givens[i].name] = s->givens[i].value;
    const calc::Result r = calc::eval(c.input, env);
    if (!r.ok) {
      c.feedback_kind = 2;
      std::snprintf(c.feedback, sizeof c.feedback, "Erreur : %s", r.error.c_str());
      return;
    }
    const double err = std::fabs(r.value - s->answer);
    if (err <= std::max(1e-6, s->tol * std::fabs(s->answer))) {
      c.feedback_kind = 1; c.solved = true; run.solved = true;
      std::snprintf(c.feedback, sizeof c.feedback, "Juste ! %s = %.4g %s", s->sym, r.value, s->unit);
    } else {
      c.feedback_kind = 2;
      const double pct = 100.0 * err / std::max(1e-9, std::fabs(s->answer));
      std::snprintf(c.feedback, sizeof c.feedback, "Faux : tu obtiens %.4g %s (ecart %.1f%%). Reessaie.",
                    r.value, s->unit, pct);
    }
  };
  // Traduit l'etat de jeu -> contenu des panneaux (KV + vues riches). Appele chaque
  // frame dans l'interieur (l'interaction modifie sel_mission / tech_done).
  auto refresh_views = [&]() {
    const int lvl = agency_lvl();
    fill_station_panels(jeu, iss_zones, iss_period / 60.0, station_panels);
    build_mission_views(lvl, sel_mission, tech_done, mission_done, mission_items, mission_list);
    {   // PLAN DE VOL REEL du selectionne (patched-conic, positions reelles ; aucun RNG)
      const MissionPlan pl = compute_mission_plan(sel_mission, launch_epoch(), eph, vehicle_dv());
      mission_list.plan_feasible = pl.feasible;
      mission_list.window_open   = pl.window_open;
      std::snprintf(mission_list.plan_a, sizeof mission_list.plan_a,
                    "Delta-v  %.1f / %.1f km/s", pl.dv_required / 1000.0, pl.dv_available / 1000.0);
      if (pl.dist_au > 0.0)
        std::snprintf(mission_list.plan_b, sizeof mission_list.plan_b,
                      "C3 %.0f  |  vol %.0f j  |  %.2f UA", pl.c3, pl.tof_days, pl.dist_au);
      else if (pl.tof_days > 0.0)
        std::snprintf(mission_list.plan_b, sizeof mission_list.plan_b,
                      "transfert lunaire  |  vol %.1f j", pl.tof_days);
      else
        std::snprintf(mission_list.plan_b, sizeof mission_list.plan_b, "mise en orbite basse");
      if (pl.window_open)
        std::snprintf(mission_list.window_txt, sizeof mission_list.window_txt, "Fenetre de lancement OUVERTE");
      else
        std::snprintf(mission_list.window_txt, sizeof mission_list.window_txt,
                      "Prochaine fenetre : %.0f j", pl.days_to_window);
    }
    build_tech_views(lvl, tech_done, tech_points(), tree_nodes, tree_lanes, tech_tree);
    if (planif_idx >= 0 && planif_idx < static_cast<int>(station_panels.size())) {
      spr::ZonePanel& p = station_panels[static_cast<std::size_t>(planif_idx)];
      p.filled = true; p.list = &mission_list;
      std::snprintf(p.status, sizeof p.status, "%s", spgame::tier_name(lvl));
    }
    if (agence_idx >= 0 && agence_idx < static_cast<int>(station_panels.size())) {
      spr::ZonePanel& p = station_panels[static_cast<std::size_t>(agence_idx)];   // ARBRE dans AGENCE
      p.filled = true; p.tree = &tech_tree;
    }
    // CONTROLE (Destiny) : DEROULEMENT de la mission lancee (checklist + bouton).
    if (run.active && controle_idx >= 0 && controle_idx < static_cast<int>(station_panels.size())) {
      spr::ZonePanel& p = station_panels[static_cast<std::size_t>(controle_idx)];
      p = spr::ZonePanel{};
      p.filled = true;
      const int last = static_cast<int>(run.phases.size()) - 1;
      const MissionPhase& cur = run.phases[static_cast<std::size_t>(run.phase)];
      const spgame::MissionDef& m = spgame::MISSIONS[run.mission];
      auto KVc = [&](const char* k, const char* val) {
        if (p.kv_count >= 6) return;
        std::snprintf(p.kv[p.kv_count].key, sizeof p.kv[p.kv_count].key, "%s", k);
        std::snprintf(p.kv[p.kv_count].val, sizeof p.kv[p.kv_count].val, "%s", val);
        ++p.kv_count;
      };
      char bb[28];
      KVc("MISSION", m.name);
      std::snprintf(bb, sizeof bb, "%d / %d", run.phase + 1, last + 1); KVc("ETAPE", bb);
      if (cur.days > 0.5) { std::snprintf(bb, sizeof bb, "%.0f j", cur.days); KVc("DUREE ETAPE", bb); }
      std::snprintf(bb, sizeof bb, "T+%.0f mois", jeu.agence.mois);          KVc("CALENDRIER", bb);
      const CalcStep* cc = current_calc();   // calcul de la phase courante (mode PRO)
      if (mode_pro() && cc) KVc("CALCUL", run.solved ? "resolu" : "a resoudre");
      flight_steps.resize(run.phases.size());   // checklist : etat par etape
      for (std::size_t i = 0; i < run.phases.size(); ++i) {
        std::snprintf(flight_steps[i].label, sizeof flight_steps[i].label, "%s", run.phases[i].label);
        flight_steps[i].state = (static_cast<int>(i) < run.phase) ? 2
                              : (static_cast<int>(i) == run.phase) ? 1 : 0;
      }
      p.steps = flight_steps.data();
      p.step_count = static_cast<int>(flight_steps.size());
      const char* btn = (run.phase >= last) ? "CLORE"
                      : (mode_pro() && cc && !run.solved) ? "RESOUDRE LE CALCUL" : "ETAPE SUIVANTE";
      std::snprintf(p.button, sizeof p.button, "%s", btn);
      std::snprintf(p.status, sizeof p.status, "%s", run.phase < last ? "EN VOL" : "TERMINE");
      std::snprintf(p.note, sizeof p.note, "%s", cur.detail);
    }
    station.panels = station_panels.data();
    station.panel_count = static_cast<int>(station_panels.size());
  };
  // --- MENU : fonder (Nouvelle partie) ou charger (Reprendre) une partie -------
  // `seed_derived` reconstruit l'ETAT 3D-LOCAL (missions accomplies + techs
  // pre-acquises) a partir du niveau courant de l'agence. Il n'est PAS persiste :
  // on le reconstruit a l'identique au demarrage ET au chargement.
  auto seed_derived = [&](bool set_reussites) {
    std::fill(tech_done.begin(), tech_done.end(), static_cast<char>(0));
    std::fill(mission_done.begin(), mission_done.end(), static_cast<char>(0));
    tech_spent = 0; mission_psr_bonus = 0;
    int n = 0;   // missions des premieres eres deja accomplies (agence etablie)
    for (int i = 0; i < spgame::MISSION_COUNT; ++i)
      if (spgame::MISSIONS[i].tier <= spgame::T_LEO) { mission_done[static_cast<std::size_t>(i)] = 1; ++n; }
    if (set_reussites) jeu.agence.reussites = n;   // nouvelle partie : cale les reussites
    const int lvl = agency_lvl();                  // pre-acquisition des paliers inferieurs
    for (int pass = 0; pass < spgame::TECH_CAT_COUNT; ++pass)
      for (int i = 0; i < spgame::TECH_COUNT; ++i) {
        const spgame::TechNode& tn = spgame::TECH_NODES[i];
        const int pr = tech_prereq(i), xr = tech_index(tn.xreq);
        const bool pre = (pr < 0) || tech_done[static_cast<std::size_t>(pr)];
        const bool xok = (xr < 0) || tech_done[static_cast<std::size_t>(xr)];
        if (!tech_done[static_cast<std::size_t>(i)] && pre && xok && tn.tier <= lvl - 1)
          tech_done[static_cast<std::size_t>(i)] = 1;
      }
  };
  // NOUVELLE PARTIE : fonde l'agence (nom + difficulte du menu). La dotation vient
  // de la difficulte (creer_agence) ; petit socle etabli pour que les postes soient
  // vivants ; calendrier a T+0.
  auto demarrer_partie = [&](const std::string& nom, fen::app::ModeAide mode) {
    jeu.creer_agence(nom, mode);
    jeu.agence.confiance = 0.78;
    jeu.relais_geo = 2; jeu.orbiteurs_mars = 1;
    jeu.donnees_gbit = 20.0; jeu.echantillons_kg = 1.0;
    seed_derived(true);
    has_loaded_pos = false;   // nouvelle partie -> apparait au spawn de base (Novellus)
    if (!iss_zones.empty()) refresh_views();
    std::printf("[jeu] nouvelle partie : \"%s\" (%s), niveau %d (%s)\n",
                jeu.agence.nom.c_str(), mode == fen::app::ModeAide::Pro ? "PRO" : "NORMAL",
                agency_lvl(), spgame::tier_name(agency_lvl()));
  };
  // REPRENDRE : charge une sauvegarde puis reconstruit l'etat 3D-local (missions /
  // techs non persistees) SANS ecraser les reussites chargees.
  auto charger_partie = [&](const std::string& chemin) -> bool {
    if (!jeu.charger(chemin)) return false;
    seed_derived(false);
    // POSITION 3D sauvegardee (lignes cam_* ajoutees par le quicksave ; ignorees par
    // Jeu::charger) : on reprendra LA OU on etait au lieu du spawn de base.
    has_loaded_pos = false;
    {
      std::ifstream f(chemin); std::string ligne;
      while (std::getline(f, ligne)) {
        if (ligne.rfind("cam_eye=", 0) == 0) {
          if (std::sscanf(ligne.c_str() + 8, "%lf %lf %lf",
                          &loaded_eye.x, &loaded_eye.y, &loaded_eye.z) == 3) has_loaded_pos = true;
        } else if (ligne.rfind("cam_yaw=", 0) == 0)   loaded_yaw   = std::atof(ligne.c_str() + 8);
        else if   (ligne.rfind("cam_pitch=", 0) == 0) loaded_pitch = std::atof(ligne.c_str() + 10);
      }
    }
    if (!iss_zones.empty()) refresh_views();
    std::printf("[jeu] partie chargee : \"%s\", niveau %d%s\n", jeu.agence.nom.c_str(), agency_lvl(),
                has_loaded_pos ? " (+position 3D)" : "");
    return true;
  };

  // --- modele 3D EXTERIEUR de l'ISS (place sur la carte, fail-safe) -----------
  // PRIORITE : rendu TEXTURE (sous-maillages par materiau GLB + cartes baseColor a
  // UV reelles) -> foil dore, modules blancs, panneaux solaires sombres, etc. Repli
  // sur le maillage gris fusionne (flat-shade) ; repli ultime = marqueur 2D seul.
  // `iss_parts_vec`/`iss_textures` vivent jusqu'a la fin (references par map.*).
  std::vector<spr::IssPart>       iss_parts_vec;
  std::vector<spr::TextureHandle> iss_textures;
  const std::string ISS_GLB = A + "3D models/ISS/ISS_stationary.glb";
  auto set_iss_transform = [&](float span) {
    map.iss_scale        = (span > 1e-6f) ? (109.0 / span) : 1.0;   // plus grand cote -> ~109 m (taille reelle)
    map.iss_model_radius = 0.5 * span;                              // rayon caracteristique (unites modele)
    map.iss_min_size     = 0.0f;                                    // TAILLE REELLE stricte (aucun plancher)
    map.iss_rot = spr::rotation_axis(spr::Vec3{1, 0, 0}, spr::PI_F * 0.5f);  // Y-up (glTF) -> Z-up (monde)
  };
  if (dev) {
    spr::asset::GlbModel gm = spr::asset::load_glb_model(ISS_GLB, 0);   // 0 = pleine resolution
    if (gm.ok()) {
      // cartes decodees -> textures GPU (index parallele a gm.images). L'espace
      // colorimetrique vient de gm.image_linear : couleur = sRGB, normales/AO = lineaire.
      std::vector<spr::TextureHandle> tex_of_img(gm.images.size(), spr::INVALID_TEXTURE);
      for (std::size_t i = 0; i < gm.images.size(); ++i) {
        const spr::asset::ImageRgba& im = gm.images[i];
        spr::TextureDesc td{}; td.rgba = im.pixels.data();
        td.width = static_cast<std::uint32_t>(im.width); td.height = static_cast<std::uint32_t>(im.height);
        td.srgb = (i < gm.image_linear.size()) ? (gm.image_linear[i] == 0) : true;
        tex_of_img[i] = dev->create_texture(td);
        if (tex_of_img[i] != spr::INVALID_TEXTURE) iss_textures.push_back(tex_of_img[i]);
      }
      auto tex_at = [&](int idx) -> spr::TextureHandle {
        return (idx >= 0 && idx < static_cast<int>(tex_of_img.size())) ? tex_of_img[idx]
                                                                       : spr::INVALID_TEXTURE;
      };
      std::size_t nverts_tot = 0, ntri = 0; int n_norm = 0, n_ao = 0;
      iss_parts_vec.reserve(gm.submeshes.size());
      for (const spr::asset::GlbSubMesh& S : gm.submeshes) {
        const std::size_t nv = S.positions.size() / 3;
        std::vector<spr::Vertex> vv(nv);
        for (std::size_t i = 0; i < nv; ++i) {
          vv[i].pos    = spr::Vec3{S.positions[i * 3], S.positions[i * 3 + 1], S.positions[i * 3 + 2]};
          vv[i].normal = spr::Vec3{S.normals[i * 3],   S.normals[i * 3 + 1],   S.normals[i * 3 + 2]};
          vv[i].uv     = spr::Vec2{S.uvs[i * 2],       S.uvs[i * 2 + 1]};
        }
        const spr::MeshHandle m = upload_mesh(dev, vv, S.indices);
        iss_meshes.push_back(m);   // detruit au shutdown avec les maillages interieur
        spr::MaterialDesc mdm{};
        mdm.params.base_color = {S.base_color[0], S.base_color[1], S.base_color[2]};
        std::uint32_t feats = 0;
        if (const spr::TextureHandle t = tex_at(S.image_index); t != spr::INVALID_TEXTURE) {
          mdm.albedo = t; feats |= spr::MAT_ALBEDO_MAP;
        }
        if (const spr::TextureHandle t = tex_at(S.normal_index); t != spr::INVALID_TEXTURE) {
          mdm.normal = t; feats |= spr::MAT_NORMAL_MAP; ++n_norm;   // relief
        }
        if (const spr::TextureHandle t = tex_at(S.ao_index); t != spr::INVALID_TEXTURE) {
          mdm.rough = t;  feats |= spr::MAT_AO_MAP; ++n_ao;         // occlusion (slot rough)
        }
        mdm.params.features = feats;
        const spr::MaterialHandle mh = dev->create_material(mdm);
        owned_mats.push_back(mh);
        spr::IssPart p{}; p.mesh = m; p.material = mh; p.style = spr::DrawStyle::MeshTextured;
        iss_parts_vec.push_back(p);
        nverts_tot += nv; ntri += S.indices.size() / 3;
      }
      map.iss_parts = iss_parts_vec.data();
      map.iss_part_count = static_cast<int>(iss_parts_vec.size());
      set_iss_transform(std::max({gm.max[0] - gm.min[0], gm.max[1] - gm.min[1], gm.max[2] - gm.min[2]}));
      std::printf("[map] ISS exterieure TEXTUREE : %d sous-maillages, %zu sommets, %zu tris, %zu cartes "
                  "(%d normales, %d occlusion) -> x%.4g m/u\n",
                  map.iss_part_count, nverts_tot, ntri, gm.images.size(), n_norm, n_ao, map.iss_scale);
    } else if (spr::asset::MeshData md = spr::asset::load_glb_mesh(ISS_GLB); md.ok()) {
      std::vector<spr::Vertex> vv(md.positions.size() / 3);
      for (std::size_t i = 0; i < vv.size(); ++i)
        vv[i] = spr::Vertex{spr::Vec3{md.positions[i * 3], md.positions[i * 3 + 1], md.positions[i * 3 + 2]},
                            spr::Vec3{md.normals[i * 3],   md.normals[i * 3 + 1],   md.normals[i * 3 + 2]}};
      map.iss_mesh = upload_mesh(dev, vv, md.indices);
      iss_meshes.push_back(map.iss_mesh);
      set_iss_transform(std::max({md.max[0] - md.min[0], md.max[1] - md.min[1], md.max[2] - md.min[2]}));
      std::printf("[map] ISS exterieure (repli gris) : %zu sommets, %zu indices\n", vv.size(), md.indices.size());
    } else {
      std::printf("[map] ISS exterieure : modele 3D indisponible -> marqueur seul\n");
    }
  }
  // Bascule de scene (ECRAN TITRE <-> carte <-> interieur) + sauvegarde du cadrage.
  enum class AppScene { Title, SolarSystem, IssInterior };
  AppScene scene = AppScene::Title;   // le jeu demarre a l'ECRAN TITRE (sauf drapeaux debug)
  spr::CameraMode saved_mode = spr::CameraMode::Map;
  double saved_dist = 4.6e12, saved_yaw = cam_yaw, saved_pitch = cam_pitch;
  spr::Dvec3 saved_focus{0, 0, 0};
  int saved_focus_idx = -1;
  bool iss_focus = false;   // gros plan ISS (facon NASA Eyes) avant d'entrer
  bool prev_e = false, prev_esc = false;
  GLFWwindow* glfw_win = core.window().glfw();

  // --- ECRAN TITRE + MENUS : canal additif, machine a etats pilotee par l'app ---
  spr::MenuView menu;
  std::vector<spr::MenuSaveItem> menu_saves;          // possede la liste affichee
  const std::filesystem::path save_dir = "saves";     // dossier des sauvegardes (relatif au cwd)
  bool prev_f5 = false;
  bool prev_m  = false;   // touche M (carte) : front montant
  // slug de fichier a partir du nom d'agence : minuscules, [a-z0-9] -> '_'.
  auto save_slug = [](const std::string& nom) {
    std::string s;
    for (char c : nom) {
      const unsigned char u = static_cast<unsigned char>(c);
      if (std::isalnum(u)) s += static_cast<char>(std::tolower(u));
      else if (!s.empty() && s.back() != '_') s += '_';
    }
    while (!s.empty() && s.back() == '_') s.pop_back();
    return s.empty() ? std::string("agence") : s;
  };
  // scanne le dossier des sauvegardes -> menu_saves (label lisible + chemin). Lit
  // l'en-tete de chaque .sav (cf. Jeu::sauvegarder : nom= / mois= / reussites=).
  auto scan_saves = [&]() {
    menu_saves.clear();
    std::error_code ec;
    if (std::filesystem::exists(save_dir, ec)) {
      for (const auto& e : std::filesystem::directory_iterator(save_dir, ec)) {
        if (e.path().extension() != ".sav") continue;   // filtre par extension (pas de stat)
        std::string nom = e.path().stem().string(); double mois = 0.0; int reuss = 0;
        std::ifstream f(e.path()); std::string ligne;
        while (std::getline(f, ligne)) {
          if (ligne.rfind("nom=", 0) == 0)            nom   = ligne.substr(4);
          else if (ligne.rfind("mois=", 0) == 0)      mois  = std::atof(ligne.c_str() + 5);
          else if (ligne.rfind("reussites=", 0) == 0) reuss = std::atoi(ligne.c_str() + 10);
          else if (ligne.rfind("J ", 0) == 0)         break;   // debut du journal : stop
        }
        spr::MenuSaveItem it{};
        std::snprintf(it.label, sizeof it.label, "%s   -   T+%.0f mois   -   %d reussites",
                      nom.c_str(), mois, reuss);
        std::snprintf(it.path, sizeof it.path, "%s", e.path().string().c_str());
        menu_saves.push_back(it);
      }
    }
    std::sort(menu_saves.begin(), menu_saves.end(),
              [](const spr::MenuSaveItem& a, const spr::MenuSaveItem& b) {
                return std::strcmp(a.label, b.label) < 0;
              });
    menu.saves = menu_saves.data();
    menu.save_count = static_cast<int>(menu_saves.size());
    menu.save_selected = menu_saves.empty() ? -1 : 0;
  };
  // sauvegarde rapide (F5 dans l'ISS) -> saves/<slug>.sav
  auto quicksave = [&]() {
    std::error_code ec; std::filesystem::create_directories(save_dir, ec);
    const std::string chemin = (save_dir / (save_slug(jeu.agence.nom) + ".sav")).string();
    if (jeu.sauvegarder(chemin)) {
      // POSITION 3D (hors modele pur) : ajoutee au MEME fichier ; Jeu::charger ignore
      // ces cles inconnues, l'app les relit a la reprise -> on reprend ou on etait.
      const spr::Dvec3 e = core.camera().focus;
      std::ofstream f(chemin, std::ios::app);
      if (f) {
        f << "cam_eye=" << e.x << " " << e.y << " " << e.z << "\n";
        f << "cam_yaw=" << core.camera().yaw << "\n";
        f << "cam_pitch=" << core.camera().pitch << "\n";
      }
      // checkpoint EN SESSION : ressortir/rentrer dans l'ISS ramene ici aussi.
      loaded_eye = e; loaded_yaw = core.camera().yaw; loaded_pitch = core.camera().pitch;
      has_loaded_pos = true;
      station.save_flash = 2.2f;
      std::printf("[jeu] sauvegarde -> %s  (pos %.2f %.2f %.2f)\n", chemin.c_str(), e.x, e.y, e.z);
    }
  };

  // Drapeaux debug/capture (--iss, --issfocus, --isscam, --focus) : on saute le
  // menu et on fonde une partie de demonstration pour peupler les postes.
  const bool skip_menu = start_in_iss || start_iss_focus || iss_cam || focus_index >= 0;
  if (skip_menu) {
    scene = AppScene::SolarSystem;
    if (dev) demarrer_partie("STATION ALPHA",
                             pro_dbg ? fen::app::ModeAide::Pro : fen::app::ModeAide::Normal);
    // --runmission / --runadvance : deroulement de mission pour capture headless.
    if (dev && run_mission_dbg >= 0) {
      launch_mission(run_mission_dbg);
      for (int k = 0; k < run_advance_dbg; ++k) advance_run();
      if (calc_open_dbg) {                       // --calcopen / --calcsolve : capture de la console
        open_calc();
        if (calc_solve_dbg) {
          if (const CalcStep* s = current_calc())
            std::snprintf(station.calc.input, sizeof station.calc.input, "%s", s->formula);
          verify_calc();
        }
      }
    }
  }
  // --menuscreen N : demarre le menu sur un ecran precis (debug/capture headless).
  if (!skip_menu && start_menu_screen >= 0) {
    menu.screen = (start_menu_screen == 1) ? spr::MenuScreen::Difficulty
                : (start_menu_screen == 2) ? spr::MenuScreen::Saves
                                           : spr::MenuScreen::Title;
    if (menu.screen == spr::MenuScreen::Saves) scan_saves();
  }

  // --issfocus : demarrer directement en GROS PLAN ISS (fiche station + bouton
  // ENTRER) pour tester/capturer le HUD de gros plan (le cadrage se fait dans la
  // boucle via le bloc iss_focus).
  if (start_iss_focus && dev) {
    iss_focus = true;
    current_focus = -1;
    core.camera().mode = spr::CameraMode::Map;
    core.camera().set_distance(260.0);
  }

  // --iss : demarrer directement dans l'interieur (debug / capture / demo).
  if (start_in_iss && dev) {
    scene = AppScene::IssInterior;
    core.camera().mode = spr::CameraMode::FirstPerson;
    core.camera().focus = isseye_set ? spr::Dvec3{isseye[0], isseye[1], isseye[2]}
                                     : iss_spawn;   // point d'apparition (modele reel ou placeholder)
    core.camera().yaw = yaw_set ? cam_yaw : 0.0;                     // regard +X par defaut
    core.camera().pitch = pitch_set ? cam_pitch : -0.05;
    if (start_panel >= 0 && start_panel < station.zone_count) {
      station.active_panel = start_panel;                // --panel : ouvre un poste (debug/capture)
      core.camera().focus = iss_zones[start_panel].center;
    }
    if (max_frames == 0) core.window().set_cursor_disabled(station.active_panel < 0);   // FPS (sauf panneau/capture)
  }

  // --- 7) boucle : temps -> physique -> rotations/coquilles/fondu -> rendu -----
  using clock = std::chrono::steady_clock;
  auto prev = clock::now();
  double tau = tau0_days * 86400.0;   // saut de temps initial (test d'alignement)
  int frame = 0;
  bool   prev_down = false;
  double press_x = 0.0, press_y = 0.0;
  bool   press_ui = false;
  try {
    while (!core.should_close()) {
      auto now = clock::now();
      float dt = std::chrono::duration<float>(now - prev).count();
      prev = now;
      if (dt > 0.1f) dt = 0.1f;

      core.begin_frame(dt);

      // Front montant : E (poste), Echap (fermer), M (basculer ISS <-> carte).
      const bool e_now   = glfw_win && glfwGetKey(glfw_win, GLFW_KEY_E) == GLFW_PRESS;
      const bool esc_now = glfw_win && glfwGetKey(glfw_win, GLFW_KEY_ESCAPE) == GLFW_PRESS;
      const bool m_now   = glfw_win && glfwGetKey(glfw_win, GLFW_KEY_M) == GLFW_PRESS;
      const bool e_pressed   = e_now && !prev_e;
      const bool esc_pressed = esc_now && !prev_esc;
      const bool m_pressed   = m_now && !prev_m;
      prev_e = e_now; prev_esc = esc_now; prev_m = m_now;

      if (scene == AppScene::Title) {
        // ===================== ECRAN TITRE + MENUS ===========================
        // Machine a etats pilotee par les requetes que le HUD pose dans `menu` ;
        // l'app fonde/charge la partie et bascule vers l'interieur de l'ISS.
        auto entrer_iss = [&]() {   // "Lancer partie ... dans l'ISS"
          scene = AppScene::IssInterior;
          core.camera().mode = spr::CameraMode::FirstPerson;
          if (has_loaded_pos) {   // on revient LA OU on etait (F5 / sortie / reprise)
            core.camera().focus = loaded_eye;
            core.camera().yaw = loaded_yaw; core.camera().pitch = loaded_pitch;
          } else {                // nouvelle partie : spawn de base (Novellus), face au couloir
            core.camera().focus = iss_spawn;
            core.camera().yaw = novellus_yaw; core.camera().pitch = novellus_pitch;
          }
          core.camera().fp_vel = spr::Dvec3{0, 0, 0};
          station.active_panel = -1; station.near_zone = -1; station.exit_request = false;
          core.window().set_cursor_disabled(true);            // vraie vue FPS
        };
        if (menu.go_new_game) menu.screen = spr::MenuScreen::Difficulty;
        if (menu.go_saves)  { scan_saves(); menu.screen = spr::MenuScreen::Saves; }
        if (menu.go_back)     menu.screen = spr::MenuScreen::Title;
        if (menu.start_game) {
          const fen::app::ModeAide mode = (menu.difficulty == 1)
              ? fen::app::ModeAide::Pro : fen::app::ModeAide::Normal;
          demarrer_partie(menu.agency_name, mode);
          entrer_iss();
        }
        if (menu.load_index >= 0 && menu.load_index < static_cast<int>(menu_saves.size())) {
          if (charger_partie(menu_saves[static_cast<std::size_t>(menu.load_index)].path))
            entrer_iss();
        }
        if (menu.quit) glfwSetWindowShouldClose(glfw_win, GLFW_TRUE);
        menu.clear_requests();
        if (scene != AppScene::Title) continue;   // transition : la scene cible rend a la frame suivante

        core.window().set_cursor_disabled(false);          // curseur visible sur le menu
        if (capture_path && max_frames > 0 && frame == max_frames - 1)
          core.request_capture(capture_path);
        core.render(snap0, -1, &map, nullptr, &menu);       // systeme solaire en fond + menu
        if (max_frames > 0 && ++frame >= max_frames) break;
        continue;
      }

      if (scene == AppScene::IssInterior) {
        // ===================== INTERIEUR ISS (premiere personne) ==============
        // Contrainte de l'oeil dans le couloir apres l'input : COLLISION MAILLEE
        // (modele reel) si dispo, sinon bornes analytiques (placeholder). --noclamp = libre.
        spr::Dvec3 eye = core.camera().focus;
        if (!no_clamp) {
          if (have_iss_model) iss_coll.resolve(eye.x, eye.y, eye.z, 0.35);
          else                clamp_station_eye(eye);
        }
        core.camera().focus = eye;
        station.show_eye = no_clamp;   // affiche la position (aide au placement)
        station.eye_pos  = eye;

        // Poste le plus proche (proximite "Entrer").
        station.near_zone = -1;
        double best = 1e30;
        for (int z = 0; z < station.zone_count; ++z) {
          const spr::StationZone& Z = iss_zones[z];
          const double dx = eye.x - Z.center.x, dy = eye.y - Z.center.y, dz = eye.z - Z.center.z;
          const double d2 = dx * dx + dy * dy + dz * dz;
          if (d2 < Z.radius * Z.radius && d2 < best) { best = d2; station.near_zone = z; }
        }
        // E : ouvrir le poste. ECHAP : fermer seulement la console/panneau (PAS de sortie).
        if (e_pressed && station.near_zone >= 0 && station.active_panel < 0)
          station.active_panel = station.near_zone;
        if (esc_pressed) {
          if (station.calc.active) station.calc.active = false;    // fermer la console de calcul
          else if (station.active_panel >= 0) station.active_panel = -1;   // fermer le panneau
        }
        // M : aller a la CARTE (sortir de l'ISS), seulement si rien n'est ouvert.
        if (m_pressed && !station.calc.active && station.active_panel < 0)
          station.exit_request = true;
        // F5 : sauvegarde rapide -> saves/<slug>.sav (+ bandeau "SAUVEGARDEE").
        const bool f5_now = glfw_win && glfwGetKey(glfw_win, GLFW_KEY_F5) == GLFW_PRESS;
        if (f5_now && !prev_f5) quicksave();
        prev_f5 = f5_now;
        if (f5_test) { quicksave(); f5_test = false; }   // --f5test : F5 auto (test du save de position)
        // Curseur : capture (mouse-look libre) en jeu, relache si un panneau OU la
        // console de calcul est ouvert (pour rendre les widgets cliquables).
        core.window().set_cursor_disabled(station.active_panel < 0 && !station.calc.active);
        // Sortie de l'ISS (M ou bouton HUD) : restaure le cadrage carte.
        if (station.exit_request) {
          station.exit_request = false;
          // memorise la position interieure -> rentrer dans l'ISS ramene ICI.
          loaded_eye = core.camera().focus;
          loaded_yaw = core.camera().yaw; loaded_pitch = core.camera().pitch;
          has_loaded_pos = true;
          core.window().set_cursor_disabled(false);   // rend le curseur a la carte
          scene = AppScene::SolarSystem;
          core.camera().mode = saved_mode;
          core.camera().focus = saved_focus;
          core.camera().set_distance(saved_dist);
          core.camera().yaw = saved_yaw;
          core.camera().pitch = saved_pitch;
          current_focus = saved_focus_idx;
          iss_focus = true;   // ressort en GROS PLAN sur l'ISS (re-cadre la station)
        }

        // INTERACTION des vues riches (catalogue de missions / arbre de competences).
        if (station.ui_list_click >= 0) {
          sel_mission = station.ui_list_click;   // selectionne pour la fiche (verrouille ou non)
          station.ui_list_click = -1;
        }
        if (station.ui_tree_click >= 0) {
          const int n = station.ui_tree_click; station.ui_tree_click = -1;
          if (n >= 0 && n < spgame::TECH_COUNT && !tech_done[static_cast<std::size_t>(n)]) {
            const spgame::TechNode& tn = spgame::TECH_NODES[n];
            const int pr = tech_prereq(n), xr = tech_index(tn.xreq);
            const bool pre = (pr < 0) || tech_done[static_cast<std::size_t>(pr)];
            const bool xok = (xr < 0) || tech_done[static_cast<std::size_t>(xr)];
            if (pre && xok && tn.tier <= agency_lvl() && tech_points() >= tn.cost) {
              tech_done[static_cast<std::size_t>(n)] = 1;   // recherche : depense des PsR
              tech_spent += tn.cost;
            }
          }
        }
        if (station.ui_mission_wait >= 0) {   // AVANCER le calendrier jusqu'a la fenetre
          const int mi = station.ui_mission_wait; station.ui_mission_wait = -1;
          if (mi >= 0 && mi < spgame::MISSION_COUNT) {
            const MissionPlan pl = compute_mission_plan(mi, launch_epoch(), eph, vehicle_dv());
            if (!pl.window_open && pl.days_to_window > 0.0)
              jeu.agence.mois += pl.days_to_window / 30.44 + 0.03;   // ouvre la fenetre
          }
        }
        if (station.ui_mission_launch >= 0) {   // DEMARRE le vol (etapes) -> suivi au CONTROLE
          launch_mission(station.ui_mission_launch);
          station.ui_mission_launch = -1;
        }
        if (station.ui_panel_button >= 0) {     // bouton du poste CONTROLE
          if (station.ui_panel_button == controle_idx) {
            // PRO : si la phase a un calcul non resolu -> ouvrir la console ; sinon avancer.
            if (mode_pro() && current_calc() && !run.solved) open_calc();
            else advance_run();
          }
          station.ui_panel_button = -1;
        }
        if (station.calc.active) {              // console de calcul (mode PRO)
          if (station.calc.verify) { verify_calc(); station.calc.verify = false; }
          if (station.calc.close)  { station.calc.active = false; station.calc.close = false; }
        }
        refresh_views();   // re-traduit l'etat de jeu -> panneaux (apres interaction)

        if (capture_path && max_frames > 0 && frame == max_frames - 1)
          core.request_capture(capture_path);
        core.render(snap0, -1, nullptr, &station);   // snap0 ignore par le rendu station
        if (max_frames > 0 && ++frame >= max_frames) break;
        continue;
      }

      // ========================= CARTE SYSTEME SOLAIRE =======================
      if (map.time.go_live) {          // bouton LIVE : recaler sur l'instant reel
        tau = epoch_now().tdb - t0.tdb;
        map.time.go_live = false;
      }
      const double warp = spr::time_mode_warp(map.time.mode) * (map.time.reverse ? -1.0 : 1.0);
      tau += static_cast<double>(dt) * warp;
      if (map.time.step_request != 0.0) { tau += map.time.step_request; map.time.step_request = 0.0; }
      // LIVE = temps reel ET affichage ~ instant present.
      map.time.is_live = (map.time.mode == spr::TimeMode::RealTime && !map.time.reverse &&
                          std::fabs(tau - (epoch_now().tdb - t0.tdb)) < 2.0);

      const fen::Epoch t = t0 + tau;
      spr::RenderSnapshot snap = bridge.freeze(t);

      // ANTI-PENETRATION : l'oeil ne peut pas entrer dans le corps suivi (en mode
      // CENTREE CORPS, focus = centre du corps -> distance = |oeil-centre|). En vue
      // libre, plancher = Soleil (a l'origine). Empeche le zoom "dans" une planete.
      if (!iss_cam && !iss_focus) {
        double R = snap.central_radius;   // vue libre : le Soleil
        if (current_focus >= 0 && current_focus < snap.body_count)
          R = (map.body_radius[current_focus] > 0.0) ? map.body_radius[current_focus]
                                                      : snap.bodies[current_focus].radius;
        const double min_d = R * 1.05;
        if (core.camera().distance < min_d) core.camera().set_distance(min_d);
      }

      const float W = static_cast<float>(core.window().width());
      const float H = static_cast<float>(core.window().height());
      const float aspect = (H > 0.0f) ? W / H : 1.0f;
      const spr::InputState& in = core.window().input();
      const bool over_ui = ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse;

      // Survol facon NASA Eyes : corps le plus proche du curseur (rond + trajectoire
      // epaissis par le HUD / la scene). -1 hors survol ou au-dessus du HUD.
      map.hover_body = -1;
      if (!over_ui) {
        float bh = 26.0f * 26.0f;
        for (int k = 0; k < snap.body_count; ++k) {
          if (snap.bodies[k].is_star) continue;
          float sx, sy;
          if (!project(core.camera(), snap.bodies[k].position, aspect, W, H, sx, sy)) continue;
          const float ex = sx - static_cast<float>(in.mouse_x), ey = sy - static_cast<float>(in.mouse_y);
          if (ex * ex + ey * ey < bh) { bh = ex * ex + ey * ey; map.hover_body = k; }
        }
      }
      if (forced_hover >= 0) map.hover_body = forced_hover;   // --hover (debug)

      // ISS : point en ORBITE REELLE autour de la Terre, position en TEMPS REEL
      // (theta avance avec le temps simule). Marqueur + trace visibles des qu'on
      // s'approche de la Terre. Le model 3D se branchera ici (a `map.iss_position`).
      map.show_iss = false;
      trails[iss_trail_index].count = 0;
      if (earth_index >= 0) {
        const spr::Dvec3& ep = snap.bodies[earth_index].position;
        const double theta = std::fmod(TWO_PI * (t.tdb / iss_period), TWO_PI);
        const spr::Dvec3 off = moon_offset(iss_pole, a_iss, ISS_INC, theta);
        map.iss_position = spr::Dvec3{ep.x + off.x, ep.y + off.y, ep.z + off.z};
        const spr::Dvec3 eeye = core.camera().eye_world();
        const double dxe = eeye.x - ep.x, dye = eeye.y - ep.y, dze = eeye.z - ep.z;
        if (std::sqrt(dxe * dxe + dye * dye + dze * dze) < 8.0e8) {   // Terre bien cadree
          map.show_iss = true;
          for (int i = 0; i < ISS_ORBIT_N; ++i) {   // trace (comete : pleine a la tete)
            const double th = TWO_PI * static_cast<double>(i) / ISS_ORBIT_N;
            const spr::Dvec3 o = moon_offset(iss_pole, a_iss, ISS_INC, th);
            iss_pts[i] = spr::Dvec3{ep.x + o.x, ep.y + o.y, ep.z + o.z};
            double back = std::fmod(theta - th, TWO_PI); if (back < 0) back += TWO_PI;
            iss_alpha[i] = static_cast<float>(std::clamp(1.0 - back / (TWO_PI * 0.92), 0.18, 1.0));
          }
          trails[iss_trail_index].count = ISS_ORBIT_N;
        }
        // OCCULTATION du marqueur : cachee si la Terre est entre l'oeil et l'ISS
        // (le modele 3D, lui, est deja occulte par le depth-test). Le marqueur 2D
        // n'a pas de depth-test -> on teste le rayon oeil->ISS contre la sphere Terre.
        map.iss_occluded = false;
        {
          double Re = (earth_index < static_cast<int>(map.body_radius.size()) &&
                       map.body_radius[earth_index] > 0.0)
                          ? map.body_radius[earth_index] : snap.bodies[earth_index].radius;
          const spr::Dvec3 E = core.camera().eye_world();
          const spr::Dvec3 u{map.iss_position.x - E.x, map.iss_position.y - E.y, map.iss_position.z - E.z};
          const double L = std::sqrt(u.x * u.x + u.y * u.y + u.z * u.z);
          if (L > 1.0) {
            const spr::Dvec3 uh{u.x / L, u.y / L, u.z / L};
            const spr::Dvec3 m{E.x - ep.x, E.y - ep.y, E.z - ep.z};
            const double b = m.x * uh.x + m.y * uh.y + m.z * uh.z;
            const double c = (m.x * m.x + m.y * m.y + m.z * m.z) - Re * Re;
            const double disc = b * b - c;
            if (disc >= 0.0) {
              const double thit = -b - std::sqrt(disc);   // 1re intersection sphere Terre
              if (thit > 1.0e3 && thit < L - 1.0e3) map.iss_occluded = true;
            }
          }
        }
      }
      // --isscam (debug/demo) : braque la camera sur l'ISS pour voir le modele 3D
      // exterieur de pres (Map -> follow() ne reinitialise pas le focus).
      if (iss_cam && earth_index >= 0) {
        core.camera().mode = spr::CameraMode::Map;
        core.camera().focus = map.iss_position;
        core.camera().set_distance(cam_dist);   // --dist = distance a l'ISS
        map.show_iss = true;
      }
      // GROS PLAN ISS (facon NASA Eyes) : 1er clic sur le marqueur -> on cadre la
      // camera sur l'ISS (comme une planete) ; l'entree se fait ensuite via ENTRER.
      bool iss_just_focused = false;
      if (map.focus_iss_request) {
        map.focus_iss_request = false;
        iss_focus = true; iss_just_focused = true;
        current_focus = -1;
        core.camera().mode = spr::CameraMode::Map;
        core.camera().set_distance(260.0);   // cadrage initial (~2.4x la taille ISS)
      }

      // M : ENTRER dans l'ISS depuis la carte (remplace le bouton). Retour direct au QG.
      if (m_pressed) map.enter_iss_request = true;
      // Entree dans l'ISS (touche M ou clic) : sauve le cadrage.
      if (map.enter_iss_request) {
        map.enter_iss_request = false;
        iss_focus = false; map.iss_focused = false;
        saved_mode = core.camera().mode; saved_dist = core.camera().distance;
        saved_yaw = core.camera().yaw;   saved_pitch = core.camera().pitch;
        saved_focus = core.camera().focus; saved_focus_idx = current_focus;
        scene = AppScene::IssInterior;
        core.camera().mode = spr::CameraMode::FirstPerson;
        if (has_loaded_pos) {   // revient LA OU on etait (F5 / sortie precedente)
          core.camera().focus = loaded_eye;
          core.camera().yaw = loaded_yaw; core.camera().pitch = loaded_pitch;
        } else {                // spawn de base : Novellus, face au couloir
          core.camera().focus = iss_spawn;
          core.camera().yaw = novellus_yaw; core.camera().pitch = novellus_pitch;
        }
        core.camera().fp_vel = spr::Dvec3{0, 0, 0};  // pas de vitesse residuelle
        station.active_panel = -1; station.near_zone = -1; station.exit_request = false;
        core.window().set_cursor_disabled(true);     // vraie vue FPS (souris libre)
        core.render(snap0, -1, nullptr, &station);   // 1 frame de transition (interieur)
        if (max_frames > 0 && ++frame >= max_frames) break;
        continue;
      }

      // Clic dans la vue 3D : selectionne le corps le plus proche du curseur
      // (un clic = appui/relache sans deplacement notable, hors HUD).
      {
        if (in.dragging && !prev_down) { press_x = in.mouse_x; press_y = in.mouse_y; press_ui = over_ui; }
        if (!in.dragging && prev_down && !press_ui) {
          const double mx = in.mouse_x - press_x, my = in.mouse_y - press_y;
          if (mx * mx + my * my < 36.0) {
            int best = -1; float bestd = 28.0f * 28.0f;
            for (int k = 0; k < snap.body_count; ++k) {
              float sx, sy;
              if (!project(core.camera(), snap.bodies[k].position, aspect, W, H, sx, sy)) continue;
              const float ex = sx - static_cast<float>(in.mouse_x), ey = sy - static_cast<float>(in.mouse_y);
              if (ex * ex + ey * ey < bestd) { bestd = ex * ex + ey * ey; best = k; }
            }
            if (best >= 0 && !iss_focus) map.focus_request = best;   // en gros plan ISS, le clic n'attrape pas un corps
          }
        }
        prev_down = in.dragging;
      }

      // Camera facon NASA Eyes : requete de focus (clic HUD ou vue 3D).
      if (map.focus_request != -2) {
        if (iss_just_focused) {
          map.focus_request = -2;   // meme clic que l'approche ISS : ne pas re-cibler un corps
        } else {
          if (iss_focus) { iss_focus = false; map.iss_focused = false; }   // clic ailleurs -> quitte le gros plan
          if (map.focus_request == -1) {                 // vue libre : recadre le systeme
            current_focus = -1;
            core.camera().mode = spr::CameraMode::Map;
            core.camera().focus = spr::Dvec3{0, 0, 0};
            core.camera().set_distance(4.6e12);
          } else if (map.focus_request < snap.body_count) {
            current_focus = map.focus_request;           // suivre ce corps
            core.camera().mode = spr::CameraMode::BodyCentered;
            const double R = (map.body_radius[current_focus] > 0.0)
                                 ? map.body_radius[current_focus] : 7.0e8;
            core.camera().set_distance(R * 6.0);          // zoom sur le corps
          }
          map.focus_request = -2;
        }
      }

      // GROS PLAN ISS actif : la camera cadre l'ISS (suit sa position, zoom borne).
      if (iss_focus) {
        core.camera().mode = spr::CameraMode::Map;
        core.camera().focus = map.iss_position;                       // suit l'ISS
        core.camera().set_distance(std::clamp(core.camera().distance, 130.0, 4000.0));
        map.iss_focused = true;
        map.show_iss = true;
        trails[iss_trail_index].count = 0;   // gros plan : la trace orbitale n'a pas
                                             // de sens a l'echelle de la station -> masquee
      } else {
        map.iss_focused = false;
      }

      const double dd = t.tdb / DAY;   // jours depuis J2000 (pour le meridien IAU)
      // orientation EXACTE des corps (modele IAU : pole + meridien W0+Wdot*d).
      for (int k = 0; k < snap.body_count; ++k) {
        const BodyDef* d = def_for(snap.bodies[k].id);
        if (!d) { map.body_rot[k] = spr::Mat4::identity(); continue; }
        map.body_rot[k] = iau_orientation(d->a0, d->del0, std::fmod(d->W0 + d->Wdot * dd, 360.0));
      }
      // coquilles (nuages/atmosphere) : meme pole, meridien propre -> rotation relative.
      for (int si = 0; si < static_cast<int>(shells.size()); ++si) {
        const ShellDef& sh = shell_defs[si];
        shells[si].rot = iau_orientation(sh.a0, sh.del0, std::fmod(sh.W0 + sh.Wdot * dd, 360.0));
      }
      // lunes : orbite KEPLERIENNE (ellipse + noeud/periastre a l'epoque + precession
      // J2) evaluee au temps courant, + rotation SYNCHRONE (verrou de maree : meridien
      // = longitude vraie moyenne -> meme face vers le parent).
      for (int mi = 0; mi < static_cast<int>(moon_rt.size()); ++mi) {
        const MoonRT& r = moon_rt[mi];
        const double M    = r.M0    + r.n * t.tdb;              // anomalie moyenne
        const double raan = r.raan0 + r.raan_dot * t.tdb;       // noeud (precession J2)
        const double argp = r.argp0 + r.argp_dot * t.tdb;       // periastre (precession J2)
        const spr::Dvec3 off = kepler_relative(r.pole, r.a_m, r.e, r.inc, raan, argp, M);
        const spr::Dvec3& pp = snap.bodies[r.parent_idx].position;
        extra[mi].position = spr::Dvec3{pp.x + off.x, pp.y + off.y, pp.z + off.z};
        const double L = M + argp + raan;                       // longitude moyenne (verrou de maree)
        extra[mi].rot = iau_orientation(r.a0, r.del0, L * (180.0 / PId));
      }
      // orbites : RE-ECHANTILLONNEES chaque frame depuis les elements osculateurs
      // COURANTS (l'ellipse derive avec les termes seculaires de Standish). Ainsi
      // la planete (position ephemeride) reste EXACTEMENT sur sa trace, meme en
      // accelere. + fondu facon NASA Eyes (plein au corps, s'eteint derriere) +
      // FONDU A L'APPROCHE (la trace disparait quand la camera est proche du corps).
      const spr::Dvec3 eye = core.camera().eye_world();
      for (int tix = 0; tix < static_cast<int>(trail_storage.size()); ++tix) {   // planetes (hors trace ISS)
        OrbitTrail& tr = trail_storage[tix];
        fen::ephem::PosVel pv = eph.state(tr.body, Body::Sun, t);
        fen::astro::Elements el = fen::astro::rv_to_elements(pv.r, pv.v, MU_SUN);
        const double nu_now = el.nu;
        // fondu a l'approche : plein loin, s'eteint quand l'oeil s'approche du corps.
        const BodyDef* bd = def_for(static_cast<int>(tr.body));
        const double R = (bd && bd->radius > 0.0) ? bd->radius : 6.4e6;
        const double dx = eye.x - pv.r.x, dy = eye.y - pv.r.y, dz = eye.z - pv.r.z;
        const double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        const double u = std::clamp((dist - 15.0 * R) / (220.0 * R - 15.0 * R), 0.0, 1.0);
        const double near_fade = u * u * (3.0 - 2.0 * u);   // smoothstep
        for (int i = 0; i < N; ++i) {
          fen::astro::Elements e = el; e.nu = TWO_PI * static_cast<double>(i) / N;
          fen::Vec3 rr, vv; fen::astro::elements_to_rv(e, MU_SUN, rr, vv);
          tr.pts[i] = spr::Dvec3{rr.x, rr.y, rr.z};
          double back = std::fmod(nu_now - TWO_PI * i / N, TWO_PI);
          if (back < 0) back += TWO_PI;
          const double fade = std::clamp(1.0 - back / (TWO_PI * 0.94), 0.0, 1.0);
          tr.alpha[i] = static_cast<float>(fade * near_fade);
        }
      }
      // Survol : la trajectoire du corps survole est epaissie + pleinement opaque.
      const int hover_id = (map.hover_body >= 0) ? snap.bodies[map.hover_body].id : -1;
      for (int tix = 0; tix < static_cast<int>(trail_storage.size()); ++tix)
        trails[tix].emphasized = (static_cast<int>(trail_storage[tix].body) == hover_id);

      if (capture_path && max_frames > 0 && frame == max_frames - 1)
        core.request_capture(capture_path);
      core.render(snap, current_focus, &map, nullptr);
      if (max_frames > 0 && ++frame >= max_frames) break;
    }
  } catch (const std::exception& e) {
    std::fprintf(stderr, "Exception boucle : %s\n", e.what());
    core.shutdown(); return 1;
  }

  if (dev) {
    for (spr::MaterialHandle m : owned_mats)  dev->destroy_material(m);
    for (spr::MeshHandle m : iss_meshes)      dev->destroy_mesh(m);    // maillages ISS
    for (spr::TextureHandle t : iss_textures) dev->destroy_texture(t); // cartes ISS texturees
    for (spr::TextureHandle t : iss_int_textures) dev->destroy_texture(t); // cartes du modele interieur
  }
  core.shutdown();
  std::printf("[map] termine proprement.\n");
  return 0;
}
