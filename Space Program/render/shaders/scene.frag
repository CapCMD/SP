#version 450
// spr/shaders/scene.frag — styles LEGACY pilotes par push.style :
//   0 PlanetLit (corps SANS materiau riche : eclairage directionnel + specular doux)
//   1 Emissive  (Soleil / balises : pleine luminosite, teinte par intensite)
//   2 Line      (orbites, vecteurs)
//   3 Marker    (vaisseau : couleur pleine)
// Les corps planetaires "riches" (Terre...) passent par planet.frag (PlanetPbr).
// Plus d'ambiant PLAT : l'eclairage est directionnel (Soleil du snapshot), le
// fill ambiant est minuscule -> le contraste jour/nuit est preserve.
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
#include "shadow.glsl"   // shadow_visibility() : ombres portees PCF (interieur ISS)

layout(location = 0) in vec3 vWorld;
layout(location = 1) in vec3 vNormal;

layout(push_constant) uniform Push {
    mat4 model;
    vec4 color;
    int  style;
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    if (pc.style == 0) {                          // PlanetLit (Lune, corps simples)
        vec3 n = normalize(vNormal);
        vec3 V = normalize(-vWorld);
        vec3 L = (uf.sun.w > 0.5) ? normalize(uf.sun.xyz - vWorld) : vec3(0.0, 0.0, 1.0);
        vec3 H = normalize(L + V);
        float ndl = max(dot(n, L), 0.0);
        vec3 sun  = uf.sunColor.rgb * uf.sunColor.w;
        vec3 diff = pc.color.rgb * (1.0 / SPR_PI) * sun * ndl;  // Lambert normalise (coherent PBR)
        vec3 amb  = pc.color.rgb * uf.ambient.rgb;
        float spec = pow(max(dot(n, H), 0.0), 24.0) * 0.05 * sun.g * step(0.001, ndl);
        // Ombre portee (PCF) : seul le DIRECT (diffus + speculaire) est occulte ;
        // l'ambiant subsiste -> les zones d'ombre restent lisibles (jamais noires).
        float vis = shadow_visibility(vWorld, n, L);
        outColor = vec4(amb + (diff + spec * uf.sunColor.rgb) * vis, pc.color.a);
    } else if (pc.style == 1) {                    // Emissive (Soleil / balises)
        outColor = pc.color;                       // pleine luminosite (tone map futur)
    } else if (pc.style == 2) {                    // Line : alpha par sommet (fondu)
        float a = pc.color.a * clamp(vNormal.x, 0.0, 1.0);
        outColor = vec4(pc.color.rgb, a);
    } else {                                       // Marker
        outColor = pc.color;
    }
}
