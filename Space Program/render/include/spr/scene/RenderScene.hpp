// spr/scene/RenderScene.hpp
//
// Transforme un RenderSnapshot (fige) + une Camera en une DrawList (donnees pures)
// que le RHI consomme. C'est TOUT ce que fait la scene : pas de physique, pas
// d'E/S, pas d'appel Vulkan direct. Elle possede les maillages statiques (sphere
// unite, marqueur) et un buffer de LIGNE DYNAMIQUE pour l'orbite, reecrit chaque
// frame en coordonnees camera-relative (double->float par sommet -> precision
// metrique quelle que soit l'echelle).
#pragma once
#include <vector>
#include "spr/rhi/Rhi.hpp"
#include "spr/bridge/RenderSnapshot.hpp"
#include "spr/core/Camera.hpp"
#include "spr/MapView.hpp"
#include "spr/StationView.hpp"

namespace spr {

// Capacite d'un buffer de trajectoire (map) et taille du pool. Les orbites
// planetaires sont des ellipses lisses -> quelques centaines de sommets suffisent.
inline constexpr int TRAIL_CAP  = 512;
inline constexpr int TRAIL_POOL = 16;

class RenderScene {
 public:
  void init(IRenderDevice& dev);
  void shutdown(IRenderDevice& dev);

  // Construit la liste de dessin de la frame. Reecrit le buffer d'orbite en
  // camera-relative. La DrawList reference des donnees possedees par la scene :
  // valable jusqu'au prochain build(). `aspect` = largeur/hauteur du viewport.
  // `map` (optionnel) active la vue carte : trajectoires planetaires, plancher de
  // taille des corps et remplacements de materiau (textures GLB). nullptr = rendu
  // classique inchange. `station` (optionnel) active la vue INTERIEUR ISS : la
  // scene n'emet alors que le fond + les pieces placeholder (StationPart), sous un
  // eclairage de cabine synthetique. `station` a la priorite sur `map` (scenes
  // exclusives : carte OU interieur).
  const DrawList& build(const RenderSnapshot& s, const Camera& cam, float aspect,
                        const MapView* map = nullptr, const StationView* station = nullptr);

  // Choisit le materiau planetaire selon le tag de presentation du corps. Aucune
  // physique : lit un SurfaceType deja pose par le pont.
  MaterialHandle material_for(SurfaceType s) const;

 private:
  IRenderDevice*          dev_{nullptr};
  MeshHandle              sphere_{INVALID_MESH};   // sphere unite, eclairee
  MeshHandle              orbit_{INVALID_MESH};    // ligne dynamique (orbite)
  MeshHandle              velvec_{INVALID_MESH};   // ligne dynamique (vecteur vitesse)
  MeshHandle              stars_{INVALID_MESH};    // starfield (points), fond spatial
  MeshHandle              ring_{INVALID_MESH};     // annulus (anneau de Saturne)
  std::vector<MeshHandle> trails_;                 // pool de lignes dynamiques (orbites map)

  // Templates de materiau planetaire (crees une fois). Le Soleil reste emissif
  // (pipeline legacy) ; les corps solides prennent le pipeline PlanetPbr.
  MaterialHandle          mat_earth_{INVALID_MATERIAL};
  MaterialHandle          mat_rock_{INVALID_MATERIAL};
  MaterialHandle          mat_gas_{INVALID_MATERIAL};
  MaterialHandle          mat_ice_{INVALID_MATERIAL};

  std::vector<DrawItem>   items_;
  std::vector<Vertex>     line_scratch_;           // tampon de recalcul des lignes
  DrawList                list_{};
};

// --- generateurs de geometrie (CPU, une fois) --------------------------------
// Sphere UV unite (rayon 1) : le model matrix applique l'echelle = rayon reel.
void make_uv_sphere(std::vector<Vertex>& verts, std::vector<std::uint32_t>& idx,
                    int stacks = 48, int slices = 96);

// Starfield : nuage de points sur la sphere celeste (directions unitaires).
// `normal` empaquete (brillance, temperature_couleur, taille_px). Deterministe
// (graine fixe) et INDEPENDANT des objets de mission.
void make_starfield(std::vector<Vertex>& verts, int count = 4000, unsigned seed = 0x5eed1701u);

// Anneau plan (annulus) dans le plan XY objet, rayons [inner, outer] (x rayon du
// corps a l'echelle), normale +Z. Doit correspondre a INNER_F/OUTER_F de ring.frag.
void make_ring(std::vector<Vertex>& verts, std::vector<std::uint32_t>& idx,
               float inner = 1.11f, float outer = 2.27f, int segments = 256);

// --- primitives placeholder (interieur ISS) ----------------------------------
// Cylindre unite le long de l'axe +Z, rayon `radius`, demi-longueur `half_len`
// (etendue [-half_len, +half_len] en Z), avec capuchons optionnels. Normales
// exterieures par defaut ; `inward = true` retourne les normales VERS l'axe -> un
// MODULE de station vu de l'INTERIEUR est correctement eclaire par la lumiere de
// cabine (le sol face a la lumiere s'illumine, la paroi opposee reste sombre).
// `open0`/`open1` (angles en RADIANS, <0 = desactive) percent une OUVERTURE (porte)
// dans la paroi : les segments dont l'angle est a moins de `open_half` de open0/open1
// sont omis -> un module parent peut s'ouvrir vers un module perpendiculaire sans
// qu'une paroi ne bouche la jonction. Angle 0 = +X local (avant rotation d'axe).
void make_cylinder(std::vector<Vertex>& verts, std::vector<std::uint32_t>& idx,
                   float radius = 1.0f, float half_len = 1.0f, int segments = 24,
                   bool capped = true, bool inward = false,
                   float open0 = -1.0f, float open1 = -1.0f, float open_half = 0.7f);

// Boite (pave) centree a l'origine, demi-dimensions (hx, hy, hz), 6 faces a
// normales franches. Sert de console / ecran / rack / table / siege placeholder.
void make_box(std::vector<Vertex>& verts, std::vector<std::uint32_t>& idx,
              float hx = 0.5f, float hy = 0.5f, float hz = 0.5f);

// Parametres de materiau planetaire par archetype (colorimetrie/intensites de
// PRESENTATION, aucune constante physique). Expose pour que le point d'entree
// puisse composer un materiau texture (albedo/nuit GLB) a partir de la meme base
// que la scene, sans dupliquer les reglages ni toucher la scene.
MaterialDesc default_planet_material(SurfaceArchetype a);

} // namespace spr
