// spr/bridge/RenderSnapshot.hpp
//
// L'ETAT FIGE que le RenderCore consomme a chaque frame. C'est le seul contrat
// entre la physique et le rendu, et il est A SENS UNIQUE (physique -> rendu).
//
// DOCTRINE (non negociable, cf. docs/RENDER_ARCHITECTURE.md) :
//   1. Le RenderCore ne RECALCULE rien : positions, orbite echantillonnee,
//      elements, telemetrie sont TOUS produits par le DataBridge a partir de
//      astro_core (la verite). Ici, ce ne sont que des nombres deja calcules.
//   2. Aucune interpolation mensongere : si le pont ne fournit pas une donnee,
//      le champ est marque invalide, il n'est pas invente.
//   3. GRANDE ECHELLE : les positions monde sont en DOUBLE (Dvec3), repere
//      inertiel du corps central, en metres. La conversion en float (rendu) se
//      fait APRES soustraction de l'origine camera -> precision metrique locale
//      quelle que soit la distance absolue (1 UA = 1.5e11 m ne tient pas dans un
//      float au metre pres ; un ecart de 100 km a la camera, si).
//
// Ce header n'inclut AUCUN entete fen/ : le RenderCore reste totalement decouple
// de astro_core. Seul bridge/DataBridge.cpp connait les deux mondes.
#pragma once
#include <array>
#include <cstdint>
#include "spr/core/Math.hpp"

namespace spr {

// Position/vecteur monde en DOUBLE. Ne jamais rabaisser en float sans avoir
// d'abord soustrait une origine locale (camera ou corps focalise).
struct Dvec3 {
  double x{}, y{}, z{};
};
inline Vec3 to_float_rel(const Dvec3& p, const Dvec3& origin) {
  return Vec3{static_cast<float>(p.x - origin.x),
              static_cast<float>(p.y - origin.y),
              static_cast<float>(p.z - origin.z)};
}

// Classe de surface : tag de PRESENTATION (pas de physique) permettant au rendu
// de choisir un template de materiau planetaire SANS interpreter l'enum physique
// `id`. C'est le pendant du champ `color` deja present (pure presentation, pose
// par le pont). Extension minimale et justifiee du contrat (cf. RENDER_MATERIALS
// .md) : sans elle, le rendu devrait soit lire `id` (= interpreter la physique,
// interdit), soit comparer `name` (fragile). L'enum reste POD -> snapshot POD.
enum class SurfaceType : std::uint8_t {
  Star = 0,       // etoile : emissif
  Rocky = 1,      // corps rocheux gris (Lune, Mercure)
  EarthLike = 2,  // oceans + terres + glace + lumieres de nuit
  Desert = 3,     // rocheux tinte (Mars)
  GasGiant = 4,   // geante gazeuse bandee (Jupiter/Saturne)
  Icy = 5,        // corps glace clair (Titan approx.)
};

// Un corps celeste tel que le rendu doit l'afficher (deja positionne par le pont).
struct BodyView {
  int         id{-1};        // = static_cast<int>(fen::ephem::Body)
  char        name[32]{};
  Dvec3       position{};    // m, monde (repere inertiel du corps central)
  double      radius{};      // m
  Vec3        color{0.7f, 0.7f, 0.7f};
  bool        is_star{false};// le Soleil : traite comme emissif
  SurfaceType surface{SurfaceType::Rocky};  // tag de presentation (choix materiau)
};

// Trace d'orbite : polyligne ECHANTILLONNEE PAR LE PONT via astro_core
// (elements osculateurs a l'epoque du snapshot). Le rendu ne fait que la tracer.
inline constexpr int ORBIT_SAMPLES = 256;
struct OrbitView {
  std::array<Dvec3, ORBIT_SAMPLES> points{};
  int   count{0};
  Vec3  color{0.35f, 0.75f, 1.0f};
  bool  closed{true};        // ellipse fermee ; hyperbole : ouverte
};

// Le vaisseau. NB : l'astro_core ne porte PAS d'attitude (State = r,v,m). On
// expose donc la VITESSE pour un marqueur directionnel explicite, jamais une
// "attitude physique" inventee. Quand un programme d'attitude de manoeuvre
// existe (FlightPlan), le pont le remplira dans `attitude` (extension).
struct VehicleView {
  bool   valid{false};
  Dvec3  position{};
  Dvec3  velocity{};         // m/s, monde
  double mass{};             // kg
  Vec3   color{1.0f, 0.85f, 0.2f};
  bool   has_attitude{false};// true si une attitude physique est fournie
  Vec4   attitude{0, 0, 0, 1};// quaternion (x,y,z,w), monde -> corps (si has_attitude)
};

// Telemetrie DEJA CALCULEE par le pont (via astro_core). Le HUD n'en calcule
// aucune : il ne fait que la mettre en page. Champs invalides = NaN + `valid`.
struct Telemetry {
  bool   valid{false};
  double altitude{};         // m, au-dessus du rayon du corps central
  double speed{};            // m/s, |v|
  double radius{};           // m, |r|
  // elements osculateurs a l'epoque
  double sma{};              // demi-grand axe (m)
  double ecc{};              // excentricite
  double inc_deg{};          // inclinaison (deg)
  double period_s{};         // periode (s) ; NaN si non elliptique
  double apoapsis{};         // m (rayon)
  double periapsis{};        // m (rayon)
};

// LE bloc fige, trivialement copiable (double-buffering du pont). Une frame de
// rendu ne lit QUE ceci ; elle ne touche jamais astro_core.
struct RenderSnapshot {
  std::uint64_t frame_id{0};
  double        epoch_tdb{0.0};
  char          epoch_iso[32]{};

  int    central_body{0};
  char   central_name[32]{};
  double central_mu{0.0};
  double central_radius{0.0};

  static constexpr int MAX_BODIES = 16;
  std::array<BodyView, MAX_BODIES> bodies{};
  int         body_count{0};

  OrbitView   orbit{};
  bool        orbit_valid{false};

  VehicleView vehicle{};
  Telemetry   telemetry{};
};

} // namespace spr
