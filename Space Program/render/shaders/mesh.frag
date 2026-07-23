#version 450
// spr/shaders/mesh.frag — MAILLAGE TEXTURE (pipeline MeshTextured : modeles GLB).
// Albedo = carte baseColor x facteur, echantillonnee aux UV REELLES du modele.
// + CARTE DE NORMALES (relief tangent-espace, repere cotangent derive a la volee ->
//   aucun attribut tangente requis) et OCCLUSION AMBIANTE (assombrit les creux).
// Eclairage directionnel (Soleil) diffus + ambiant minimal, rendu DEUX FACES
// (cull NONE : on retourne la normale des faces arriere).
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
#include "shadow.glsl"   // shadow_visibility() : auto-ombrage PCF (ISS exterieure)

layout(location = 0) in vec3 vWorld;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUv;

layout(push_constant) uniform Push {
    mat4 model;
    vec4 color;
    int  style;
} pc;

layout(set = 1, binding = 0) uniform Material {
    vec4  baseColor; vec4 pbr; vec4 extra;
    vec4  colorLow; vec4 colorMid; vec4 colorHigh;
    ivec4 flags;       // x archetype, y features, z seed, w
} um;

layout(set = 1, binding = 1) uniform sampler2D albedoMap;
layout(set = 1, binding = 2) uniform sampler2D normalMap;
layout(set = 1, binding = 3) uniform sampler2D roughMap;   // reutilise : carte d'occlusion (AO)
layout(set = 1, binding = 4) uniform sampler2D nightMap;

layout(location = 0) out vec4 outColor;

const int F_NORMAL_MAP = 2;    // 1<<1
const int F_ALBEDO_MAP = 1;    // 1<<0
const int F_AO_MAP     = 512;  // 1<<9
bool has(int bit) { return (um.flags.y & bit) != 0; }

// Repere cotangent (T,B,N) reconstruit depuis les derivees ecran de la position et
// des UV (Schuler) -> normal mapping SANS attribut tangente pre-calcule.
mat3 cotangent_frame(vec3 N, vec3 p, vec2 uv) {
    vec3 dp1 = dFdx(p),  dp2 = dFdy(p);
    vec2 duv1 = dFdx(uv), duv2 = dFdy(uv);
    vec3 dp2perp = cross(dp2, N);
    vec3 dp1perp = cross(N, dp1);
    vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;
    float invmax = inversesqrt(max(dot(T, T), dot(B, B)));
    return mat3(T * invmax, B * invmax, N);
}

void main() {
    vec3 albedo = um.baseColor.rgb;
    if (has(F_ALBEDO_MAP)) albedo *= texture(albedoMap, vUv).rgb;

    // normale geometrique orientee vers la face vue (deux faces : cull NONE).
    vec3 N = normalize(vNormal);
    if (!gl_FrontFacing) N = -N;
    vec3 Ng = N;   // normale GEOMETRIQUE conservee : sert de base stable au normal
                   // offset des ombres (la normale perturbee bruiterait le decalage).
    // relief : perturbation par la carte de normales (tangent-espace glTF, +Y haut).
    if (has(F_NORMAL_MAP)) {
        vec3 mapN = texture(normalMap, vUv).xyz * 2.0 - 1.0;
        N = normalize(cotangent_frame(N, vWorld, vUv) * mapN);
    }

    vec3 L = (uf.sun.w > 0.5) ? normalize(uf.sun.xyz - vWorld) : vec3(0.0, 0.0, 1.0);
    float ndl = max(dot(N, L), 0.0);

    float ao = has(F_AO_MAP) ? texture(roughMap, vUv).r : 1.0;   // creux assombris
    vec3 sun = uf.sunColor.rgb * uf.sunColor.w;
    // Ombre portee (PCF) : occulte le direct solaire (auto-ombrage des panneaux/
    // modules) ; l'ambiant + l'AO bakee restent, gardant les creux lisibles.
    float vis = shadow_visibility(vWorld, Ng, L);
    vec3 color = albedo * (uf.ambient.rgb + sun * (ndl / SPR_PI) * vis) * ao;
    color += albedo * um.pbr.z;                 // emissif optionnel (balises)

    outColor = vec4(color, 1.0);
}
