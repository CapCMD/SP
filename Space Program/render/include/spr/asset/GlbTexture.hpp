// spr/asset/GlbTexture.hpp
//
// Extraction d'une texture embarquee d'un fichier .glb (glTF binaire), decodee en
// RGBA8. Autonome : parse le conteneur GLB + le JSON glTF a la main, decode le
// JPEG/PNG via WIC (Windows Imaging Component, natif Windows -> zero dependance).
//
// N'inclut NI fen/ NI vulkan.h : c'est du chargement d'ASSET pur, il peut vivre
// partout dans le module de rendu sans casser les invariants d'architecture.
//
// FAIL-SAFE : aucune exception ne remonte ; en cas d'echec (fichier absent, GLB
// malforme, image introuvable, decodage impossible) l'image retournee est vide
// (ok() == false). L'appelant retombe alors sur le rendu procedural.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace spr::asset {

// Image decodee en RGBA8 (origine haut-gauche, lignes contigues, pas de padding).
struct ImageRgba {
  std::vector<std::uint8_t> pixels;   // width*height*4 octets, R8G8B8A8
  int width{0};
  int height{0};
  bool ok() const { return width > 0 && height > 0 &&
                           pixels.size() == static_cast<std::size_t>(width) * height * 4; }
};

// `max_dim` (0 = pleine resolution) borne la plus grande dimension : les cartes
// 8K sont downscalees par WIC a la volee -> VRAM et temps de decodage maitrises
// (un equirect ~4K reste net meme en gros plan).

// Charge l'image embarquee dont le NOM glTF contient `name_substr` (ex. "8k_mars",
// "nightmap"). La selection par nom est robuste au graphe de materiau et laisse
// l'appelant choisir explicitement daymap / nightmap / etc. Retourne une image
// vide si rien ne correspond ou si le decodage echoue.
ImageRgba load_glb_image_by_name(const std::string& glb_path, const std::string& name_substr,
                                 int max_dim = 0);

// Charge un fichier image autonome (JPEG/PNG/TIFF/...) decode en RGBA8 via WIC.
// Meme contrat fail-safe : image vide si absent/illisible.
ImageRgba load_image_file(const std::string& path, int max_dim = 0);

// --- geometrie GLB (maillage) ------------------------------------------------
// Maillage FUSIONNE d'un .glb : positions + normales (repere du modele, apres
// application des transformations de node/hierarchie) et indices. Autonome : NE
// depend NI de fen/ NI de Vulkan NI de spr::Math (les transforms sont calculees
// avec une petite algebre 4x4 locale) -> l'appelant convertit en son format de
// sommet. Suffisant pour un modele statique non-anime (l'ISS exterieure).
struct MeshData {
  std::vector<float>         positions;  // 3*N : x,y,z par sommet
  std::vector<float>         normals;    // 3*N : nx,ny,nz par sommet (unitaires)
  std::vector<std::uint32_t> indices;    // triangles (TRIANGLE_LIST)
  float min[3]{0, 0, 0};                 // coin min de la boite englobante
  float max[3]{0, 0, 0};                 // coin max
  bool ok() const {
    return !positions.empty() && positions.size() == normals.size() && !indices.empty();
  }
};

// Charge et FUSIONNE toute la geometrie d'un .glb (glTF 2.0 non compresse) en un
// seul maillage. Parcourt la scene par defaut (hierarchie de nodes, transforms
// T*R*S accumulees), lit POSITION/NORMAL (float VEC3) et les indices (u8/u16/u32).
// Normales manquantes -> normales de facette synthetisees. Contrat FAIL-SAFE :
// aucune exception ne remonte ; en cas d'echec (fichier absent/malforme,
// compression Draco/meshopt, primitive non-triangulaire) MeshData::ok() == false.
MeshData load_glb_mesh(const std::string& glb_path);

// --- modele GLB TEXTURE (sous-maillages par materiau) ------------------------
// Contrairement a load_glb_mesh (fusion en un maillage gris), decoupe le modele en
// SOUS-MAILLAGES groupes par materiau, chacun portant ses UV reelles et sa carte de
// couleur de base (baseColorTexture) decodee. Sert au rendu FIDELE d'un modele
// texture (l'ISS exterieure : foil dore, modules blancs, panneaux sombres, etc.).
struct GlbSubMesh {
  std::vector<float>         positions;   // 3*N : x,y,z (repere modele, transforms de node appliquees)
  std::vector<float>         normals;     // 3*N : nx,ny,nz (unitaires)
  std::vector<float>         uvs;         // 2*N : u,v (UV reelles du modele ; (0,0) si absentes)
  std::vector<std::uint32_t> indices;     // TRIANGLE_LIST
  int   image_index{-1};                  // baseColorTexture -> index dans GlbModel::images ; -1 = couleur seule
  int   normal_index{-1};                 // normalTexture    -> index dans GlbModel::images ; -1 = aucune
  int   ao_index{-1};                     // occlusionTexture -> index dans GlbModel::images ; -1 = aucune
  float base_color[4]{1, 1, 1, 1};        // baseColorFactor (multiplie la carte)
  // EMISSIF (neons / ecrans auto-eclaires) : emissiveFactor x KHR_materials_emissive_strength.
  float emissive[3]{0, 0, 0};             // couleur emise (0 = materiau non emissif)
  float emissive_strength{1.0f};          // intensite (extension KHR ; 1 par defaut)
};
struct GlbModel {
  std::vector<GlbSubMesh> submeshes;
  std::vector<ImageRgba>  images;         // cartes decodees, indexees par GlbSubMesh::*_index
  // Espace colorimetrique par image (parallele a `images`) : 1 = LINEAIRE (donnee :
  // carte de normales / occlusion), 0 = sRGB (couleur). L'appelant cree la texture
  // GPU avec le bon `srgb` -> une carte de normales n'est jamais gamma-decodee a tort.
  std::vector<char>       image_linear;
  float min[3]{0, 0, 0};                  // boite englobante globale (unites du modele)
  float max[3]{0, 0, 0};
  bool ok() const { return !submeshes.empty(); }
};

// Charge un .glb en SOUS-MAILLAGES textures (voir GlbModel). `max_tex_dim` (0 =
// PLEINE RESOLUTION, defaut) borne la plus grande dimension des cartes decodees.
// Meme contrat FAIL-SAFE : GlbModel::ok() == false en cas d'echec.
GlbModel load_glb_model(const std::string& glb_path, int max_tex_dim = 0);

} // namespace spr::asset
