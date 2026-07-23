// spr/rhi/Rhi.hpp
//
// RHI = Render Hardware Interface. LE joint d'abstraction. Tout le RenderCore, la
// Scene, la Camera et le HUD parlent EXCLUSIVEMENT a `IRenderDevice`. Aucun de ces
// fichiers n'inclut <vulkan/vulkan.h>. Consequence directe : ajouter un backend
// Vulkan/DX12/Metal plus tard = ecrire un nouveau .cpp qui implemente cette
// interface, sans toucher une ligne de scene/camera/hud. C'est l'exigence
// "Vulkan de preference, mais l'architecture ne doit pas en dependre".
//
// Le backend concret livre ici est Vulkan (voir src/rhi/vulkan/). La fabrique
// create_vulkan_device() est la SEULE fonction qui trahit le choix d'API, et elle
// est appelee en un seul endroit (RenderCore).
#pragma once
#include <cstdint>
#include <memory>
#include "spr/core/Math.hpp"

namespace spr {

// --- ressources opaques ------------------------------------------------------
using MeshHandle = std::uint32_t;
inline constexpr MeshHandle INVALID_MESH = 0;

// Topologie primitive d'un maillage. Point d'extension : POINT_LIST sert le
// starfield (chaque etoile = un point), LINE_STRIP les orbites, TRIANGLE_LIST
// les corps. `MeshDesc::lines` reste supporte (retro-compat) et equivaut a
// LineStrip.
enum class Topology : std::uint32_t {
  TriangleList = 0,
  LineStrip    = 1,
  PointList    = 2,
};

// Un sommet de rendu. Les lignes n'utilisent que `pos`. Le starfield reutilise
// `normal` pour empaqueter (magnitude, temperature_couleur, _) sans changer le
// format : aucun cout, aucun nouveau binding. `uv` (coordonnee de texture) sert aux
// maillages textures a UV reelles (modeles GLB, ex. l'ISS) ; il vaut (0,0) et est
// simplement ignore par tous les autres pipelines (attribut additif).
struct Vertex {
  Vec3 pos;
  Vec3 normal;
  Vec2 uv{};
};

// Description d'un maillage. Statique (max_vertices == vertex_count, indices) ou
// DYNAMIQUE (vertices == nullptr, max_vertices = capacite ; on l'alimente ensuite
// par update_vertices() a chaque frame, ex. la polyligne d'orbite camera-relative).
struct MeshDesc {
  const Vertex*        vertices{nullptr};
  std::uint32_t        vertex_count{0};
  const std::uint32_t* indices{nullptr};
  std::uint32_t        index_count{0};
  std::uint32_t        max_vertices{0};   // capacite ; 0 => = vertex_count (statique)
  bool                 lines{false};      // retro-compat : true => LineStrip
  Topology             topology{Topology::TriangleList};
};

// --- textures (combined image samplers) --------------------------------------
// Ressource opaque : le RenderCore ne connait ni VkImage ni sampler. Sert au
// systeme de materiau (albedo / normal / rugosite / emissif nocturne). A cette
// etape les materiaux planetaires sont PROCEDURAUX (detail synthetise dans le
// shader) : les slots de texture existent, sont lies a des textures par defaut
// neutres, et deviennent actifs des qu'on fournit de vraies cartes (Blue Marble,
// carte de nuit VIIRS, normal map) via create_texture + le flag correspondant.
using TextureHandle = std::uint32_t;
inline constexpr TextureHandle INVALID_TEXTURE = 0;

struct TextureDesc {
  const std::uint8_t* rgba{nullptr};   // width*height*4 octets, R8G8B8A8, non entrelace
  std::uint32_t       width{1};
  std::uint32_t       height{1};
  bool                srgb{true};      // albedo/nuit = sRGB ; normal/rugosite = lineaire
};

// --- systeme de materiau -----------------------------------------------------
// Distinction NETTE materiaux planetaires / vehicules (exigee), meme si seuls
// les planetaires sont reellement implementes a cette etape. Le backend mappe
// chaque `kind` a un pipeline ; `Vehicle` retombe pour l'instant sur le pipeline
// planetaire non-atmospherique (stub documente).
using MaterialHandle = std::uint32_t;
inline constexpr MaterialHandle INVALID_MATERIAL = 0;

enum class MaterialKind : std::uint32_t {
  Planet  = 0,   // corps celeste : sol, ocean, glace, lumieres de nuit, rim atmospherique
  Vehicle = 1,   // STUB a cette etape : structure prete, pipeline PBR vaisseau a venir
};

// Archetype de surface pilotant la synthese procedurale tant qu'aucune carte
// reelle n'est fournie. C'est le pendant "rendu" du tag de presentation
// BodyView::surface (pose par le pont). Aucune semantique physique.
enum class SurfaceArchetype : std::uint32_t {
  Star = 0, EarthLike = 1, Rock = 2, GasGiant = 3, Ice = 4,
};

// Bits de fonctionnalite : gouvernent a la fois le sampling des cartes (si
// fournies) et les branches d'eclairage. MAT_PROCEDURAL synthetise le detail
// sans aucune carte (cas de cette etape). Les bits ATMOSPHERE/CLOUDS sont
// reserves (rendu differe) : la structure les porte deja.
enum MaterialFeature : std::uint32_t {
  MAT_ALBEDO_MAP   = 1u << 0,
  MAT_NORMAL_MAP   = 1u << 1,
  MAT_ROUGH_MAP    = 1u << 2,
  MAT_NIGHT_MAP    = 1u << 3,
  MAT_PROCEDURAL   = 1u << 4,   // detail synthetise dans le shader (pas de carte)
  MAT_NIGHT_LIGHTS = 1u << 5,   // emissif cote nuit (lumieres de villes)
  MAT_OCEAN_SPEC   = 1u << 6,   // specularite marquee sur les oceans
  MAT_ATMOSPHERE   = 1u << 7,   // reserve : diffusion / rim atmospherique (stub)
  MAT_CLOUDS       = 1u << 8,   // reserve : couche nuageuse (stub)
  MAT_AO_MAP       = 1u << 9,   // occlusion ambiante (carte grise) -> assombrit l'eclairage
};

// Parametres de materiau exposes cote RenderCore (API-agnostiques). Le backend
// les empaquette en std140 dans un UBO par-materiau (set = 1). Regroupe albedo,
// rugosite/metallique (base metallic-roughness), colorimetrie procedurale et
// intensites. Extensible sans casser l'ABI : ajouter un champ = ajouter au bloc
// UBO + au shader, les materiaux existants gardent leurs valeurs par defaut.
struct MaterialParams {
  Vec3  base_color{1.0f, 1.0f, 1.0f};  // teinte albedo (x carte albedo si fournie)
  float roughness{0.9f};               // rugosite de base (metallic-roughness)
  float metallic{0.0f};
  float emissive{0.0f};                // emissif global (Soleil/balises)
  float night_intensity{0.0f};         // intensite des lumieres de nuit
  float rim_strength{0.0f};            // liseré atmospherique (jour), 0 = aucun

  // Colorimetrie de la synthese procedurale (planetes sans carte).
  Vec3  color_low{0.10f, 0.20f, 0.45f};  // fond/oceans/plaines
  Vec3  color_mid{0.20f, 0.45f, 0.20f};  // terres/reliefs bas
  Vec3  color_high{0.85f, 0.85f, 0.90f}; // sommets/glace
  float ocean_level{0.5f};             // seuil terre/mer (0..1) pour EarthLike
  float detail_scale{3.0f};            // frequence de base du bruit

  SurfaceArchetype archetype{SurfaceArchetype::Rock};
  std::uint32_t    features{MAT_PROCEDURAL};
  std::uint32_t    seed{0};
};

// Description complete d'un materiau : parametres + jusqu'a 4 cartes optionnelles.
// Les handles INVALID_TEXTURE sont lies a des textures par defaut neutres par le
// backend (blanc / normale plate / rugosite mediane / noir), si bien que le
// pipeline de descripteurs est TOUJOURS complet et valide, meme sans carte.
struct MaterialDesc {
  MaterialKind   kind{MaterialKind::Planet};
  MaterialParams params{};
  TextureHandle  albedo{INVALID_TEXTURE};
  TextureHandle  normal{INVALID_TEXTURE};
  TextureHandle  rough{INVALID_TEXTURE};   // R=rugosite, G=masque ocean/spec
  TextureHandle  night{INVALID_TEXTURE};   // emissif nocturne (lumieres)
};

// Style de rendu -> le backend choisit le pipeline. Point d'extension PBR :
// ajouter des styles (Atmosphere, Ring, Particle) mappe chacun a un pipeline
// sans changer l'interface.
enum class DrawStyle : std::uint32_t {
  PlanetLit = 0,   // Lambert directionnel legacy (corps sans materiau riche)
  Emissive  = 1,   // le Soleil / balises : pleine luminosite
  Line      = 2,   // orbites, axes, grilles
  Marker    = 3,   // marqueur de vaisseau (non eclaire, couleur pleine)
  PlanetPbr = 4,   // materiau planetaire (set=1) : pipeline planet.*
  Star      = 5,   // starfield : points, fond spatial (pipeline star.*)
  Shell     = 6,   // coquille translucide (nuages/atmosphere) : pipeline shell.*
  Ring      = 7,   // anneau plan (Saturne) : pipeline ring.*
  MeshTextured = 8,// maillage GLB texture a UV reelles (set=1) : pipeline mesh.*
  COUNT
};

// Un ordre de dessin. `model` est DEJA camera-relative (float) : la translation
// monde->camera a ete faite en double par la Scene (world_to_render). `material`
// n'est consulte que pour le style PlanetPbr.
struct DrawItem {
  MeshHandle     mesh{INVALID_MESH};
  Mat4           model{Mat4::identity()};
  Vec4           color{1, 1, 1, 1};
  DrawStyle      style{DrawStyle::PlanetLit};
  MaterialHandle material{INVALID_MATERIAL};
  // Largeur de trait (lignes seulement : orbites/trajectoires). 1.0 = trait fin
  // par defaut ; le backend applique un etat dynamique VK_DYNAMIC_STATE_LINE_WIDTH
  // clampe a la plage supportee (survol NASA Eyes : trajectoire epaissie).
  float          line_width{1.0f};
};

// Parametres globaux de la frame (UBO cote backend). L'eclairage principal est
// une source DIRECTIONNELLE (le Soleil) : a l'echelle planetaire, la direction
// vers un Soleil a ~1 UA est quasi constante sur un corps -> terminateur net et
// physiquement coherent. `sun_render` est la position camera-relative (le shader
// en derive la direction) ; couleur et intensite viennent de la presentation du
// corps-etoile. `ambient` reste minuscule (jamais d'ambiant plat qui ecrase le
// contraste jour/nuit) : la lisibilite du cote nuit vient des lumieres de nuit
// et du starfield. `exposure` est reserve au futur tone mapping.
struct FrameParams {
  Mat4  view{Mat4::identity()};   // oeil a l'origine (camera-relative)
  Mat4  proj{Mat4::identity()};
  Vec3  sun_render{0, 0, 0};      // position du Soleil en espace camera-relative
  bool  has_sun{false};
  Vec3  sun_color{1.0f, 0.96f, 0.9f};
  float sun_intensity{3.0f};      // ~PI : compense la normalisation /PI de la diffuse
  Vec3  ambient{0.015f, 0.02f, 0.03f};  // fill spatial minimal (lisibilite)
  float exposure{1.0f};                 // reserve tone mapping (extension)

  // --- ombres directionnelles (shadow mapping) -------------------------------
  // Matrice LIGHT-SPACE (ortho * lookAt du point de vue de la lumiere), exprimee
  // dans le MEME repere camera-relative que les `model` des DrawItem. La scene la
  // calcule pour les vues ISS (interieur/exterieur, une seule source directionnelle) ;
  // laissee a l'identite ailleurs (carte planetaire : has_shadow=0 -> passe sautee).
  Mat4  light_view_proj{Mat4::identity()};
  // x = has_shadow (1/0) : active l'ENREGISTREMENT de la passe depth-only (backend)
  //   ET l'echantillonnage de la shadow map dans les shaders de surface.
  // y = normal_offset : decalage du point d'echantillonnage le long de la normale
  //   (unites monde) -> combat peter-panning + acne sur les pentes.
  // z = depth_bias : biais soustrait a la profondeur comparee (anti-acne residuel).
  // w = pcf_texel : taille d'un texel de la shadow map (1/resolution) pour le PCF.
  Vec4  shadow_params{0.0f, 0.0f, 0.0f, 0.0f};
};

struct DrawList {
  FrameParams      frame{};
  const DrawItem*  items{nullptr};
  std::uint32_t    count{0};
};

// L'interface de peripherique de rendu. Implementee par VulkanDevice.
class IRenderDevice {
 public:
  virtual ~IRenderDevice() = default;

  // --- ressources ------------------------------------------------------------
  virtual MeshHandle create_mesh(const MeshDesc&) = 0;
  virtual void       update_vertices(MeshHandle, const Vertex* v, std::uint32_t count) = 0;
  virtual void       destroy_mesh(MeshHandle) = 0;

  // Textures et materiaux. create_texture uploade des pixels R8G8B8A8. Un
  // materiau possede son UBO (set=1, binding=0) et 4 slots de sampler ; les
  // handles absents sont lies a des textures par defaut neutres (le set reste
  // complet). update_material_params reecrit l'UBO (host-visible).
  virtual TextureHandle  create_texture(const TextureDesc&) = 0;
  virtual void           destroy_texture(TextureHandle) = 0;
  virtual MaterialHandle create_material(const MaterialDesc&) = 0;
  virtual void           update_material_params(MaterialHandle, const MaterialParams&) = 0;
  virtual void           destroy_material(MaterialHandle) = 0;

  // --- cycle de frame --------------------------------------------------------
  // Acquiert l'image de swapchain, ouvre la passe. false => frame a sauter
  // (fenetre minimisee, swapchain a recreer) : ne rien dessiner ce tour.
  virtual bool begin_frame() = 0;
  virtual void submit(const DrawList&) = 0;   // enregistre la scene
  virtual void draw_hud() = 0;                // enregistre ImGui dans la meme passe
  virtual void end_frame() = 0;               // clot la passe, soumet, presente

  virtual void resize(std::uint32_t w, std::uint32_t h) = 0;
  virtual void wait_idle() = 0;

  // Demande l'ecriture de la PROCHAINE frame presentee dans un fichier BMP 24 bits
  // (readback swapchain). Oracle visuel pour tests/captures ; no-op si le format
  // de swapchain ne supporte pas le transfert. Aucune incidence sur la scene.
  virtual void request_capture(const char* bmp_path) = 0;

  // --- HUD (ImGui) : le backend possede l'init du backend ImGui --------------
  // Ainsi le Hud n'inclut jamais imgui_impl_vulkan.h : couplage confine ici.
  virtual void imgui_init(void* glfw_window) = 0;
  virtual void imgui_new_frame() = 0;         // ImplVulkan+ImplGlfw+ImGui::NewFrame
  virtual void imgui_shutdown() = 0;

  virtual const char* device_name() const = 0;
};

// Configuration de creation du backend. Le handle natif vient de la fenetre GLFW ;
// la creation de la surface Win32 reste DANS le backend (le RHI ne fuit pas).
struct DeviceConfig {
  void*         hwnd{nullptr};       // HWND (Windows)
  void*         hinstance{nullptr};  // HINSTANCE
  std::uint32_t width{1280};
  std::uint32_t height{720};
  bool          enable_validation{false};
};

// LA seule fonction specifique a l'API. Appelee une fois, dans RenderCore.
std::unique_ptr<IRenderDevice> create_vulkan_device(const DeviceConfig&);

} // namespace spr
