// spr/scene/RenderScene.cpp
#include "spr/scene/RenderScene.hpp"
#include <algorithm>
#include <cmath>
#include <random>

namespace spr {

void make_uv_sphere(std::vector<Vertex>& verts, std::vector<std::uint32_t>& idx,
                    int stacks, int slices) {
  verts.clear();
  idx.clear();
  for (int i = 0; i <= stacks; ++i) {
    const float v = static_cast<float>(i) / stacks;      // 0..1
    const float phi = v * PI_F;                          // 0..pi
    const float sp = std::sin(phi), cp = std::cos(phi);
    for (int j = 0; j <= slices; ++j) {
      const float u = static_cast<float>(j) / slices;    // 0..1
      const float th = u * 2.0f * PI_F;                  // 0..2pi
      const Vec3 n{sp * std::cos(th), sp * std::sin(th), cp};
      verts.push_back(Vertex{n, n});  // sphere unite : position == normale
    }
  }
  const int stride = slices + 1;
  for (int i = 0; i < stacks; ++i)
    for (int j = 0; j < slices; ++j) {
      const std::uint32_t a = i * stride + j;
      const std::uint32_t b = a + stride;
      idx.insert(idx.end(), {a, b, a + 1, a + 1, b, b + 1});
    }
}

void make_starfield(std::vector<Vertex>& verts, int count, unsigned seed) {
  verts.clear();
  verts.reserve(static_cast<size_t>(count));
  std::mt19937 rng(seed);
  std::uniform_real_distribution<float> U(0.0f, 1.0f);
  for (int i = 0; i < count; ++i) {
    // direction uniforme sur la sphere celeste
    const float z = 2.0f * U(rng) - 1.0f;
    const float phi = 2.0f * PI_F * U(rng);
    const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
    const Vec3 dir{r * std::cos(phi), r * std::sin(phi), z};

    // distribution de magnitude : beaucoup de faibles, peu de brillantes
    const float m = U(rng);
    const float bright = 0.20f + 1.60f * std::pow(m, 3.2f);   // peut depasser 1 (HDR)
    const float size = 1.0f + 3.0f * std::pow(m, 7.0f);       // rares grosses etoiles
    const float temp = U(rng);                                // temperature de couleur
    verts.push_back(Vertex{dir, Vec3{bright, temp, size}});
  }
}

void make_ring(std::vector<Vertex>& verts, std::vector<std::uint32_t>& idx,
               float inner, float outer, int segments) {
  verts.clear();
  idx.clear();
  const Vec3 n{0.0f, 0.0f, 1.0f};
  for (int j = 0; j <= segments; ++j) {
    const float th = static_cast<float>(j) / segments * 2.0f * PI_F;
    const float c = std::cos(th), s = std::sin(th);
    verts.push_back(Vertex{Vec3{inner * c, inner * s, 0.0f}, n});   // bord interne
    verts.push_back(Vertex{Vec3{outer * c, outer * s, 0.0f}, n});   // bord externe
  }
  for (int j = 0; j < segments; ++j) {
    const std::uint32_t a = static_cast<std::uint32_t>(j) * 2, b = a + 1, cc = a + 2, d = a + 3;
    idx.insert(idx.end(), {a, b, cc, cc, b, d});
  }
}

// Cylindre le long de +Z : paroi laterale (normales radiales) + capuchons
// optionnels (normales +/-Z). Aucune texture : maillage placeholder pur.
void make_cylinder(std::vector<Vertex>& verts, std::vector<std::uint32_t>& idx,
                   float radius, float half_len, int segments, bool capped, bool inward,
                   float open0, float open1, float open_half) {
  verts.clear();
  idx.clear();
  if (segments < 3) segments = 3;
  const float ns = inward ? -1.0f : 1.0f;   // sens des normales (interieur/exterieur)
  // Segment omis si son angle median est a moins de open_half d'une ouverture.
  auto in_opening = [&](float th) {
    auto near = [&](float c) {
      if (c < 0.0f) return false;
      float d = std::fabs(th - c);
      d = std::min(d, 2.0f * PI_F - d);   // distance angulaire (wrap)
      return d < open_half;
    };
    return near(open0) || near(open1);
  };
  // paroi laterale : deux anneaux (z = -half_len, +half_len), normale radiale.
  for (int j = 0; j <= segments; ++j) {
    const float th = static_cast<float>(j) / segments * 2.0f * PI_F;
    const float c = std::cos(th), s = std::sin(th);
    const Vec3 n{ns * c, ns * s, 0.0f};
    verts.push_back(Vertex{Vec3{radius * c, radius * s, -half_len}, n});
    verts.push_back(Vertex{Vec3{radius * c, radius * s, +half_len}, n});
  }
  for (int j = 0; j < segments; ++j) {
    const float th_mid = (static_cast<float>(j) + 0.5f) / segments * 2.0f * PI_F;
    if (in_opening(th_mid)) continue;   // porte : segment de paroi omis
    const std::uint32_t a = static_cast<std::uint32_t>(j) * 2, b = a + 1, cc = a + 2, d = a + 3;
    idx.insert(idx.end(), {a, cc, b, b, cc, d});   // cull NONE : le sens n'importe pas
  }
  if (capped) {
    // capuchon +Z.
    const std::uint32_t base_top = static_cast<std::uint32_t>(verts.size());
    verts.push_back(Vertex{Vec3{0, 0, +half_len}, Vec3{0, 0, ns}});
    const std::uint32_t ring_top = static_cast<std::uint32_t>(verts.size());
    for (int j = 0; j <= segments; ++j) {
      const float th = static_cast<float>(j) / segments * 2.0f * PI_F;
      verts.push_back(Vertex{Vec3{radius * std::cos(th), radius * std::sin(th), +half_len}, Vec3{0, 0, ns}});
    }
    for (int j = 0; j < segments; ++j)
      idx.insert(idx.end(), {base_top, ring_top + static_cast<std::uint32_t>(j),
                             ring_top + static_cast<std::uint32_t>(j) + 1});
    // capuchon -Z.
    const std::uint32_t base_bot = static_cast<std::uint32_t>(verts.size());
    verts.push_back(Vertex{Vec3{0, 0, -half_len}, Vec3{0, 0, -ns}});
    const std::uint32_t ring_bot = static_cast<std::uint32_t>(verts.size());
    for (int j = 0; j <= segments; ++j) {
      const float th = static_cast<float>(j) / segments * 2.0f * PI_F;
      verts.push_back(Vertex{Vec3{radius * std::cos(th), radius * std::sin(th), -half_len}, Vec3{0, 0, -ns}});
    }
    for (int j = 0; j < segments; ++j)
      idx.insert(idx.end(), {base_bot, ring_bot + static_cast<std::uint32_t>(j) + 1,
                             ring_bot + static_cast<std::uint32_t>(j)});
  }
}

// Boite centree : 6 faces separees (4 sommets chacune) pour des normales franches.
void make_box(std::vector<Vertex>& verts, std::vector<std::uint32_t>& idx,
              float hx, float hy, float hz) {
  verts.clear();
  idx.clear();
  struct Face { Vec3 n, u, v; };
  const Face faces[6] = {
    {{ 1, 0, 0}, {0, 1, 0}, {0, 0, 1}},   // +X
    {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},   // -X
    {{ 0, 1, 0}, {0, 0, 1}, {1, 0, 0}},   // +Y
    {{ 0,-1, 0}, {1, 0, 0}, {0, 0, 1}},   // -Y
    {{ 0, 0, 1}, {1, 0, 0}, {0, 1, 0}},   // +Z
    {{ 0, 0,-1}, {0, 1, 0}, {1, 0, 0}},   // -Z
  };
  const Vec3 half{hx, hy, hz};
  for (const Face& f : faces) {
    const std::uint32_t base = static_cast<std::uint32_t>(verts.size());
    const Vec3 center{f.n.x * half.x, f.n.y * half.y, f.n.z * half.z};
    const Vec3 U{f.u.x * half.x, f.u.y * half.y, f.u.z * half.z};
    const Vec3 V{f.v.x * half.x, f.v.y * half.y, f.v.z * half.z};
    verts.push_back(Vertex{center - U - V, f.n});
    verts.push_back(Vertex{center + U - V, f.n});
    verts.push_back(Vertex{center + U + V, f.n});
    verts.push_back(Vertex{center - U + V, f.n});
    idx.insert(idx.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
  }
}

// Fabrique les MaterialParams d'un archetype. Colorimetrie et intensites =
// PRESENTATION pure (aucune constante physique). Les corps rocheux/gazeux sont
// re-teintes par la couleur du snapshot (pc.color) cote shader ; EarthLike garde
// ses couleurs procedurales. Expose (cf. RenderScene.hpp) pour composition externe.
MaterialDesc default_planet_material(SurfaceArchetype a) {
  MaterialDesc d{};
  d.kind = MaterialKind::Planet;
  MaterialParams& p = d.params;
  p.archetype = a;
  p.features = MAT_PROCEDURAL;
  switch (a) {
    case SurfaceArchetype::EarthLike:
      p.features |= MAT_NIGHT_LIGHTS | MAT_OCEAN_SPEC;
      p.base_color = {1.0f, 1.0f, 1.0f};
      p.roughness = 0.6f;
      p.night_intensity = 1.3f;
      p.rim_strength = 0.6f;
      p.color_low  = {0.05f, 0.17f, 0.43f};  // ocean (bleu franc)
      p.color_mid  = {0.19f, 0.40f, 0.16f};  // terres/vegetation
      p.color_high = {0.60f, 0.54f, 0.42f};  // reliefs/montagnes
      p.ocean_level = 0.43f;
      p.detail_scale = 3.0f;
      p.seed = 7u;
      break;
    case SurfaceArchetype::GasGiant:
      p.roughness = 0.7f;
      p.rim_strength = 0.25f;
      p.color_low  = {0.62f, 0.52f, 0.40f};
      p.color_mid  = {0.88f, 0.80f, 0.62f};
      p.color_high = {0.96f, 0.93f, 0.86f};
      p.detail_scale = 2.0f;
      p.seed = 5u;
      break;
    case SurfaceArchetype::Ice:
      p.roughness = 0.5f;
      p.color_low  = {0.40f, 0.36f, 0.28f};
      p.color_mid  = {0.62f, 0.54f, 0.36f};
      p.color_high = {0.86f, 0.80f, 0.64f};
      p.detail_scale = 3.0f;
      p.seed = 42u;
      break;
    case SurfaceArchetype::Rock:
    default:
      p.roughness = 0.95f;
      p.color_low  = {0.20f, 0.20f, 0.22f};
      p.color_mid  = {0.42f, 0.41f, 0.40f};
      p.color_high = {0.66f, 0.64f, 0.62f};
      p.detail_scale = 4.2f;
      p.seed = 777u;
      break;
  }
  return d;
}

void RenderScene::init(IRenderDevice& dev) {
  dev_ = &dev;

  std::vector<Vertex> sv;
  std::vector<std::uint32_t> si;
  make_uv_sphere(sv, si);
  MeshDesc sd{};
  sd.vertices = sv.data();
  sd.vertex_count = static_cast<std::uint32_t>(sv.size());
  sd.indices = si.data();
  sd.index_count = static_cast<std::uint32_t>(si.size());
  sd.max_vertices = sd.vertex_count;
  sphere_ = dev.create_mesh(sd);

  // Orbite : ligne dynamique, capacite = ORBIT_SAMPLES, reecrite chaque frame.
  MeshDesc od{};
  od.max_vertices = ORBIT_SAMPLES;
  od.lines = true;
  orbit_ = dev.create_mesh(od);

  // Vecteur vitesse : 2 sommets dynamiques.
  MeshDesc vd{};
  vd.max_vertices = 2;
  vd.lines = true;
  velvec_ = dev.create_mesh(vd);

  // Starfield : points statiques (directions sur la sphere celeste).
  std::vector<Vertex> stars;
  make_starfield(stars);
  MeshDesc st{};
  st.vertices = stars.data();
  st.vertex_count = static_cast<std::uint32_t>(stars.size());
  st.max_vertices = st.vertex_count;
  st.topology = Topology::PointList;
  stars_ = dev.create_mesh(st);

  // Anneau (annulus) : maillage statique, oriente/echelle par le model.
  std::vector<Vertex> rv; std::vector<std::uint32_t> ri;
  make_ring(rv, ri);
  MeshDesc rd{};
  rd.vertices = rv.data();
  rd.vertex_count = static_cast<std::uint32_t>(rv.size());
  rd.indices = ri.data();
  rd.index_count = static_cast<std::uint32_t>(ri.size());
  rd.max_vertices = rd.vertex_count;
  ring_ = dev.create_mesh(rd);

  // Pool de trajectoires (map) : lignes dynamiques reecrites chaque frame. Une
  // par corps -> chacune sa propre memoire (pas d'ecrasement entre traces).
  trails_.reserve(TRAIL_POOL);
  for (int i = 0; i < TRAIL_POOL; ++i) {
    MeshDesc td{};
    td.max_vertices = TRAIL_CAP;
    td.lines = true;
    trails_.push_back(dev.create_mesh(td));
  }

  // Materiaux planetaires (templates crees une fois).
  mat_earth_ = dev.create_material(default_planet_material(SurfaceArchetype::EarthLike));
  mat_rock_  = dev.create_material(default_planet_material(SurfaceArchetype::Rock));
  mat_gas_   = dev.create_material(default_planet_material(SurfaceArchetype::GasGiant));
  mat_ice_   = dev.create_material(default_planet_material(SurfaceArchetype::Ice));
}

MaterialHandle RenderScene::material_for(SurfaceType s) const {
  switch (s) {
    case SurfaceType::EarthLike: return mat_earth_;
    case SurfaceType::GasGiant:  return mat_gas_;
    case SurfaceType::Icy:       return mat_ice_;
    case SurfaceType::Rocky:
    case SurfaceType::Desert:    return mat_rock_;   // teinte via la couleur du corps
    default:                     return INVALID_MATERIAL;
  }
}

void RenderScene::shutdown(IRenderDevice& dev) {
  if (sphere_ != INVALID_MESH) dev.destroy_mesh(sphere_);
  if (orbit_  != INVALID_MESH) dev.destroy_mesh(orbit_);
  if (velvec_ != INVALID_MESH) dev.destroy_mesh(velvec_);
  if (stars_  != INVALID_MESH) dev.destroy_mesh(stars_);
  if (ring_   != INVALID_MESH) dev.destroy_mesh(ring_);
  for (MeshHandle t : trails_)
    if (t != INVALID_MESH) dev.destroy_mesh(t);
  trails_.clear();
  if (mat_earth_ != INVALID_MATERIAL) dev.destroy_material(mat_earth_);
  if (mat_rock_  != INVALID_MATERIAL) dev.destroy_material(mat_rock_);
  if (mat_gas_   != INVALID_MATERIAL) dev.destroy_material(mat_gas_);
  if (mat_ice_   != INVALID_MATERIAL) dev.destroy_material(mat_ice_);
  sphere_ = orbit_ = velvec_ = stars_ = INVALID_MESH;
  mat_earth_ = mat_rock_ = mat_gas_ = mat_ice_ = INVALID_MATERIAL;
}

const DrawList& RenderScene::build(const RenderSnapshot& s, const Camera& cam, float aspect,
                                   const MapView* map, const StationView* station) {
  items_.clear();

  // --- INTERIEUR ISS : scene de presentation pure (aucune physique) ----------
  // Priorite sur `map` : les deux scenes sont exclusives (carte OU interieur).
  if (station) {
    FrameParams fp{};
    fp.view = cam.view();
    fp.proj = cam.proj(aspect);
    // Lumiere de cabine : source directionnelle synthetique, placee loin le long
    // de -light_dir depuis l'oeil (l'oeil est a l'origine en camera-relative -> on
    // exprime la position "soleil" directement en repere de rendu).
    const Vec3 Ld = normalize(station->light_dir);
    fp.sun_render = Vec3{-Ld.x, -Ld.y, -Ld.z} * 1.0e6f;
    fp.sun_color = station->light_color;
    fp.has_sun = true;
    fp.sun_intensity = 2.5f;          // directionnelle marquee + ambiant bas -> fort contraste sol/plafond
    fp.ambient = station->ambient;   // ambiant releve : lisibilite sans etoile

    // --- ombres portees : matrice LIGHT-SPACE de la source directionnelle -------
    // Projection ORTHOGRAPHIQUE centree sur l'OEIL (origine du repere camera-relative).
    // La shadow map SUIT le joueur -> texels fins la ou il regarde (consoles/racks
    // proches), au lieu d'etaler la resolution sur toute l'ISS (~54 m). La lumiere
    // voyage le long de Ld ; sa "camera" recule a -Ld*dist et regarde vers l'oeil.
    // Compromis : les modules a plus de ~22 m de l'oeil n'ont pas d'ombre portee (a
    // cette distance elle serait imperceptible) -> R accorde nettete vs couverture.
    {
      const Vec3 Ld = normalize(station->light_dir);
      const Vec3 center{0.0f, 0.0f, 0.0f};    // l'oeil est a l'origine (camera-relative)
      const float R = 22.0f;                   // demi-cote couvert (m) autour de l'oeil
      const float dist = 60.0f;                // recul de la camera-lumiere (m)
      // up non parallele a Ld (evite un look_at degenere si la lumiere est verticale).
      const bool y_ok = (Ld.y < 0.95f && Ld.y > -0.95f);
      const Vec3 up = y_ok ? Vec3{0.0f, 1.0f, 0.0f} : Vec3{1.0f, 0.0f, 0.0f};
      const Mat4 lview = look_at(center - Ld * dist, center, up);
      const Mat4 lproj = ortho(-R, R, -R, R, 2.0f, 120.0f);
      fp.light_view_proj = lproj * lview;
      // x=on ; y=normalOffset (m) ; z=depthBias (NDC) ; w=texel=1/SHADOW_RES (miroir
      // de la constante backend, 2048). Ces trois derniers sont les CURSEURS anti-
      // artefacts (acne / peter-panning / durete du bord PCF).
      fp.shadow_params = Vec4{1.0f, 0.04f, 0.0008f, 1.0f / 2048.0f};
    }

    list_.frame = fp;

    // fond etoile (visible par les hublots / la cupola) : ponctuel, additif.
    if (stars_ != INVALID_MESH) {
      DrawItem sky{};
      sky.mesh = stars_;
      sky.model = Mat4::identity();
      sky.color = Vec4{1, 1, 1, 1};
      sky.style = DrawStyle::Star;
      items_.push_back(sky);
    }

    // pieces placeholder (modules, consoles, ecrans...) : deja en repere local
    // metrique proche de l'origine -> translation camera-relative de l'oeil.
    for (int i = 0; i < station->part_count; ++i) {
      const StationPart& sp = station->parts[i];
      if (sp.mesh == INVALID_MESH) continue;
      DrawItem it{};
      it.mesh = sp.mesh;
      // La piece est en repere station (= repere monde ici) : on translate de -oeil.
      it.model = translation(cam.world_to_render(Dvec3{0, 0, 0})) * sp.model;
      it.color = sp.color;
      it.style = sp.style;
      it.material = sp.material;
      items_.push_back(it);
    }

    list_.items = items_.data();
    list_.count = static_cast<std::uint32_t>(items_.size());
    return list_;
  }

  // --- parametres globaux de la frame ---------------------------------------
  FrameParams fp{};
  fp.view = cam.view();
  fp.proj = cam.proj(aspect);
  fp.has_sun = false;
  for (int i = 0; i < s.body_count; ++i) {
    if (s.bodies[i].is_star) {
      fp.sun_render = cam.world_to_render(s.bodies[i].position);
      fp.sun_color = s.bodies[i].color;   // couleur d'eclairage = celle de l'etoile
      fp.has_sun = true;
    }
  }
  fp.sun_intensity = 3.0f;   // ~PI : eclairement coherent avec la diffuse /PI (PBR)
  // ambient/exposure : valeurs par defaut de FrameParams (fill minimal, expo 1).
  list_.frame = fp;

  // --- fond : Voie lactee (sphere celeste texturee a l'infini) ---------------
  // Dessinee AVANT tout : sphere geante centree sur l'oeil (rendu camera-relative
  // -> translation nulle), non eclairee (materiau archetype Star). Cull NONE du
  // pipeline planetaire -> les faces internes s'affichent. A l'infini -> depth ~0.
  if (map && map->skybox_material != INVALID_MATERIAL && sphere_ != INVALID_MESH) {
    DrawItem sky{};
    sky.mesh = sphere_;
    sky.model = map->skybox_rot * scaling(1.0e14f);   // oriente (inclinaison galactique)
    sky.color = Vec4{1, 1, 1, 1};
    sky.style = DrawStyle::PlanetPbr;
    sky.material = map->skybox_material;
    items_.push_back(sky);
  }

  // --- starfield procedural : ponctuel, additif (par-dessus la Voie lactee) ---
  if (stars_ != INVALID_MESH) {
    DrawItem sky{};
    sky.mesh = stars_;
    sky.model = Mat4::identity();   // les etoiles ne dependent que de la rotation de vue
    sky.color = Vec4{1, 1, 1, 1};
    sky.style = DrawStyle::Star;
    items_.push_back(sky);
  }

  // --- corps celestes --------------------------------------------------------
  for (int i = 0; i < s.body_count; ++i) {
    const BodyView& b = s.bodies[i];
    DrawItem it{};
    it.mesh = sphere_;
    // Rayon de rendu : rayon reel (override de presentation si fourni, sinon
    // snapshot), avec plancher optionnel. Par defaut aucune mise a l'echelle.
    double Rreal = b.radius;
    if (map && i < static_cast<int>(map->body_radius.size()) && map->body_radius[i] > 0.0)
      Rreal = map->body_radius[i];
    float rscale = static_cast<float>(Rreal);
    if (map && map->body_min_size > 0.0f)
      rscale = std::max(rscale, static_cast<float>(cam.distance) * map->body_min_size);
    // Orientation + rotation propre (repere monde) fournie par l'app.
    const Mat4 rot = (map && i < static_cast<int>(map->body_rot.size()))
                         ? map->body_rot[i] : Mat4::identity();
    it.model = translation(cam.world_to_render(b.position)) * rot * scaling(rscale);
    it.color = Vec4{b.color, 1.0f};
    // Materiau : override par corps (texture reelle) sinon choix par SurfaceType.
    MaterialHandle mat = INVALID_MATERIAL;
    if (map && i < static_cast<int>(map->body_material.size()))
      mat = map->body_material[i];
    if (mat != INVALID_MATERIAL) {
      it.style = DrawStyle::PlanetPbr;                 // materiau texture (set=1)
      it.material = mat;
    } else if (b.is_star) {
      it.style = DrawStyle::Emissive;                  // Soleil sans texture : emissif legacy
    } else if ((mat = material_for(b.surface)) != INVALID_MATERIAL) {
      it.style = DrawStyle::PlanetPbr;
      it.material = mat;
    } else {
      it.style = DrawStyle::PlanetLit;                 // repli Lambert legacy
    }
    items_.push_back(it);
  }

  // --- corps supplementaires (lunes) : sphere texturee + rotation propre -----
  if (map && map->extra_bodies) {
    for (int e = 0; e < map->extra_count; ++e) {
      const MapBody& mb = map->extra_bodies[e];
      DrawItem it{};
      it.mesh = sphere_;
      it.model = translation(cam.world_to_render(mb.position)) * mb.rot *
                 scaling(static_cast<float>(mb.radius));
      it.color = Vec4{mb.color, 1.0f};
      if (mb.material != INVALID_MATERIAL) { it.style = DrawStyle::PlanetPbr; it.material = mb.material; }
      else it.style = DrawStyle::PlanetLit;
      items_.push_back(it);
    }
  }

  // --- modele 3D exterieur de l'ISS (a sa position orbitale reelle) ----------
  // Un plancher de taille ecran (comme les corps) la garde visible pres de la Terre
  // (a l'echelle reelle elle serait sous-pixel), sans deroger a la doctrine map.
  // PRIORITE au rendu TEXTURE (sous-maillages par materiau, cartes baseColor GLB) ;
  // repli sur le maillage gris fusionne (flat-shade Lambert) si non disponible.
  if (map && map->show_iss && (map->iss_part_count > 0 || map->iss_mesh != INVALID_MESH)) {
    float sc = static_cast<float>(map->iss_scale);
    if (map->iss_min_size > 0.0f && map->iss_model_radius > 0.0) {
      const float floor_sc = static_cast<float>(cam.distance * static_cast<double>(map->iss_min_size) /
                                                map->iss_model_radius);
      sc = std::max(sc, floor_sc);
    }
    const Mat4 iss_model = translation(cam.world_to_render(map->iss_position)) * map->iss_rot * scaling(sc);
    if (map->iss_part_count > 0 && map->iss_parts) {
      for (int p = 0; p < map->iss_part_count; ++p) {
        const IssPart& ip = map->iss_parts[p];
        if (ip.mesh == INVALID_MESH) continue;
        DrawItem it{};
        it.mesh     = ip.mesh;
        it.model    = iss_model;
        it.color    = ip.color;
        it.style    = ip.style;
        it.material = ip.material;
        items_.push_back(it);
      }
    } else {
      DrawItem it{};
      it.mesh  = map->iss_mesh;
      it.model = iss_model;
      it.color = Vec4{0.74f, 0.77f, 0.82f, 1.0f};   // structure gris clair (repli)
      it.style = DrawStyle::PlanetLit;
      items_.push_back(it);
    }
  }

  // --- coquilles translucides (nuages Terre, atmosphere Venus) ---------------
  // Sphere legerement plus grande, sa propre rotation, alpha-blendee sur le corps.
  if (map && map->shells) {
    for (int si = 0; si < map->shell_count; ++si) {
      const MapShell& sh = map->shells[si];
      if (sh.material == INVALID_MATERIAL) continue;
      if (sh.body_index < 0 || sh.body_index >= s.body_count) continue;
      const BodyView& b = s.bodies[sh.body_index];
      double Rreal = b.radius;
      if (sh.body_index < static_cast<int>(map->body_radius.size()) &&
          map->body_radius[sh.body_index] > 0.0)
        Rreal = map->body_radius[sh.body_index];
      DrawItem it{};
      it.mesh = sphere_;
      it.model = translation(cam.world_to_render(b.position)) * sh.rot *
                 scaling(static_cast<float>(Rreal) * sh.radius_factor);
      it.color = Vec4{1, 1, 1, 1};
      it.style = DrawStyle::Shell;
      it.material = sh.material;
      items_.push_back(it);
    }
  }

  // --- anneaux (Saturne) : annulus dans le plan equatorial du corps ----------
  if (map && map->rings && ring_ != INVALID_MESH) {
    for (int ri = 0; ri < map->ring_count; ++ri) {
      const MapRing& rg = map->rings[ri];
      if (rg.material == INVALID_MATERIAL) continue;
      if (rg.body_index < 0 || rg.body_index >= s.body_count) continue;
      const BodyView& b = s.bodies[rg.body_index];
      double Rreal = b.radius;
      if (rg.body_index < static_cast<int>(map->body_radius.size()) &&
          map->body_radius[rg.body_index] > 0.0)
        Rreal = map->body_radius[rg.body_index];
      DrawItem it{};
      it.mesh = ring_;
      it.model = translation(cam.world_to_render(b.position)) * rg.rot *
                 scaling(static_cast<float>(Rreal));
      it.color = Vec4{1, 1, 1, 1};
      it.style = DrawStyle::Ring;
      it.material = rg.material;
      items_.push_back(it);
    }
  }

  // --- trajectoires de la carte (orbites planetaires) ------------------------
  // Chaque trace = sa propre ligne dynamique (pool). ANTI-SCINTILLEMENT : les
  // sommets sont exprimes relativement a l'ANCRE de la trace (points[0]) -> petits
  // nombres, precision float fine ; la grande translation ancre->oeil est portee
  // par le MODEL (identique pour tous les sommets) donc la ligne ne "vibre" plus
  // (les sommets ne quantifient plus independamment a l'echelle ~1e12 m).
  // L'alpha par sommet (fondu facon NASA Eyes) est empaquete dans normal.x.
  if (map && map->show_trails && map->trails) {
    const int nt = std::min(map->trail_count, static_cast<int>(trails_.size()));
    for (int t = 0; t < nt; ++t) {
      const MapTrail& tr = map->trails[t];
      if (!tr.points || tr.count < 2) continue;
      const Dvec3 anchor = tr.points[0];
      const int cnt = std::min(tr.count, TRAIL_CAP);
      // Survol facon NASA Eyes : la trajectoire EPAISSIT seulement (line_width). On
      // GARDE l'alpha par sommet tel quel -> le fondu arriere (comete) ET le fondu a
      // l'approche (la trace disparait quand on est proche) restent actifs au survol.
      line_scratch_.clear();
      line_scratch_.reserve(cnt + 1);
      for (int k = 0; k < cnt; ++k) {
        Vertex v{};
        v.pos = to_float_rel(tr.points[k], anchor);
        v.normal.x = tr.alpha ? tr.alpha[k] : 1.0f;
        line_scratch_.push_back(v);
      }
      if (tr.closed && cnt < TRAIL_CAP) {   // referme la boucle de l'ellipse
        Vertex v{};
        v.pos = to_float_rel(tr.points[0], anchor);
        v.normal.x = tr.alpha ? tr.alpha[0] : 1.0f;
        line_scratch_.push_back(v);
      }
      dev_->update_vertices(trails_[t], line_scratch_.data(),
                            static_cast<std::uint32_t>(line_scratch_.size()));
      DrawItem it{};
      it.mesh = trails_[t];
      it.model = translation(cam.world_to_render(anchor));   // grande translation ici
      it.color = Vec4{tr.color, 1.0f};
      it.style = DrawStyle::Line;
      it.line_width = tr.emphasized ? 2.6f : 1.0f;   // survol = plus epais (fondu conserve)
      items_.push_back(it);
    }
  }

  // --- orbite (ligne dynamique, camera-relative par sommet) ------------------
  if (s.orbit_valid && s.orbit.count > 1) {
    line_scratch_.clear();
    line_scratch_.reserve(s.orbit.count);
    for (int i = 0; i < s.orbit.count; ++i) {
      Vertex v{};
      v.pos = cam.world_to_render(s.orbit.points[i]);
      v.normal.x = 1.0f;   // opaque (alpha par sommet du pipeline ligne)
      line_scratch_.push_back(v);
    }
    dev_->update_vertices(orbit_, line_scratch_.data(),
                          static_cast<std::uint32_t>(line_scratch_.size()));
    DrawItem it{};
    it.mesh = orbit_;
    it.model = Mat4::identity();  // sommets deja camera-relative
    it.color = Vec4{s.orbit.color, 1.0f};
    it.style = DrawStyle::Line;
    items_.push_back(it);
  }

  // --- vaisseau : marqueur + vecteur vitesse ---------------------------------
  if (s.vehicle.valid) {
    // marqueur : rayon toujours visible (fonction de la distance camera).
    const float mr = std::max(static_cast<float>(s.central_radius) * 0.02f,
                              static_cast<float>(cam.distance) * 0.01f);
    DrawItem mk{};
    mk.mesh = sphere_;
    mk.model = translation(cam.world_to_render(s.vehicle.position)) * scaling(mr);
    mk.color = Vec4{s.vehicle.color, 1.0f};
    mk.style = DrawStyle::Marker;
    items_.push_back(mk);

    // vecteur vitesse : segment depuis le vaisseau, longueur d'echelle visible.
    const Dvec3& p = s.vehicle.position;
    const Dvec3& vv = s.vehicle.velocity;
    const double vn = std::sqrt(vv.x * vv.x + vv.y * vv.y + vv.z * vv.z);
    if (vn > 1e-6) {
      const double L = std::max(s.central_radius * 0.5, cam.distance * 0.15);
      Dvec3 tip{p.x + vv.x / vn * L, p.y + vv.y / vn * L, p.z + vv.z / vn * L};
      Vertex seg[2]{};
      seg[0].pos = cam.world_to_render(p);   seg[0].normal.x = 1.0f;
      seg[1].pos = cam.world_to_render(tip); seg[1].normal.x = 1.0f;
      dev_->update_vertices(velvec_, seg, 2);
      DrawItem vl{};
      vl.mesh = velvec_;
      vl.model = Mat4::identity();
      vl.color = Vec4{1.0f, 0.9f, 0.3f, 1.0f};
      vl.style = DrawStyle::Line;
      items_.push_back(vl);
    }
  }

  list_.items = items_.data();
  list_.count = static_cast<std::uint32_t>(items_.size());
  return list_;
}

} // namespace spr
