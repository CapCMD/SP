#version 450
// spr/shaders/ring.frag — anneau de Saturne. La carte est un BANDEAU RADIAL
// (largeur = distance du bord interne au bord externe ; l'alpha encode les
// divisions, ex. Cassini). On derive U = fraction radiale depuis la position
// objet, on echantillonne couleur+alpha, on eclaire en double face.
// INNER_F / OUTER_F doivent correspondre a make_ring() (RenderScene.cpp).
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

layout(location = 0) in vec3 vWorld;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec3 vObj;

layout(push_constant) uniform Push { mat4 model; vec4 color; int style; } pc;

layout(set = 1, binding = 0) uniform Material {
    vec4 baseColor; vec4 pbr; vec4 extra;
    vec4 colorLow; vec4 colorMid; vec4 colorHigh; ivec4 flags;
} um;
layout(set = 1, binding = 1) uniform sampler2D albedoMap;
layout(set = 1, binding = 2) uniform sampler2D normalMap;
layout(set = 1, binding = 3) uniform sampler2D roughMap;
layout(set = 1, binding = 4) uniform sampler2D nightMap;

layout(location = 0) out vec4 outColor;

const float INNER_F = 1.11;   // bord interne (x rayon de Saturne)
const float OUTER_F = 2.27;   // bord externe

void main() {
    float r = length(vObj.xy);
    float u = (r - INNER_F) / (OUTER_F - INNER_F);
    if (u < 0.0 || u > 1.0) discard;
    vec4 t = texture(albedoMap, vec2(u, 0.5));

    vec3 N = normalize(vNormal);
    vec3 L = (uf.sun.w > 0.5) ? normalize(uf.sun.xyz - vWorld) : vec3(0.0, 0.0, 1.0);
    float ndl = abs(dot(N, L));                      // double face : les 2 cotes captent le Soleil
    vec3 sun = uf.sunColor.rgb * uf.sunColor.w;
    vec3 lit = uf.ambient.rgb * 2.0 + sun * (ndl / SPR_PI);

    outColor = vec4(t.rgb * lit * um.baseColor.rgb, t.a);
}
