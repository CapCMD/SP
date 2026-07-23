#version 450
// spr/shaders/shell.frag — coquille translucide ECLAIREE (nuages / atmosphere).
// Sphere legerement plus grande que le corps, texture equirectangulaire, eclairee
// par le Soleil (jour clair / nuit sombre) et alpha-blendee sur la surface. Deux
// modes via les bits de materiau :
//   MAT_CLOUDS      -> nuages : couleur blanche, alpha = densite (luminance carte)
//   MAT_ATMOSPHERE  -> atmosphere : couleur de la carte, alpha ~ constant (jour)
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

layout(location = 0) in vec3 vWorld;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec3 vObj;

layout(push_constant) uniform Push { mat4 model; vec4 color; int style; } pc;

layout(set = 1, binding = 0) uniform Material {
    vec4  baseColor; vec4 pbr; vec4 extra;
    vec4  colorLow; vec4 colorMid; vec4 colorHigh; ivec4 flags;
} um;
layout(set = 1, binding = 1) uniform sampler2D albedoMap;
layout(set = 1, binding = 2) uniform sampler2D normalMap;
layout(set = 1, binding = 3) uniform sampler2D roughMap;
layout(set = 1, binding = 4) uniform sampler2D nightMap;

layout(location = 0) out vec4 outColor;

const int F_ATMOSPHERE = 128;   // 1<<7
const int F_CLOUDS     = 256;   // 1<<8
bool has(int bit) { return (um.flags.y & bit) != 0; }

// Echantillonnage EQUIRECTANGULAIRE SANS couture (identique a planet.frag). Le u
// vient de atan(y,x) qui saute de +PI a -PI au meridien arriere : sa derivee y
// devient enorme -> le GPU choisit le mip le plus grossier -> une BANDE GRISE
// pixelisee relie les deux poles sur la coquille (nuages / atmosphere). On corrige
// les gradients au saut et on echantillonne via textureGrad -> couture invisible.
vec4 texEquirect(sampler2D tex, vec3 p) {
    vec2 uv = vec2(atan(p.y, p.x) * (0.5 / SPR_PI) + 0.5, acos(clamp(p.z, -1.0, 1.0)) / SPR_PI);
    vec2 ddx = dFdx(uv), ddy = dFdy(uv);
    if (ddx.x >  0.5) ddx.x -= 1.0; else if (ddx.x < -0.5) ddx.x += 1.0;
    if (ddy.x >  0.5) ddy.x -= 1.0; else if (ddy.x < -0.5) ddy.x += 1.0;
    return textureGrad(tex, uv, ddx, ddy);
}

void main() {
    vec3 pObj = normalize(vObj);
    vec3 tex = texEquirect(albedoMap, pObj).rgb;

    vec3 N = normalize(vNormal);
    vec3 L = (uf.sun.w > 0.5) ? normalize(uf.sun.xyz - vWorld) : vec3(0.0, 0.0, 1.0);
    float ndl = max(dot(N, L), 0.0);
    vec3 sun = uf.sunColor.rgb * uf.sunColor.w;
    vec3 lit = uf.ambient.rgb + sun * (ndl / SPR_PI);   // jour clair, nuit sombre

    float amax = um.pbr.z;               // intensite/alpha max (champ emissive reutilise)
    vec3 color;
    float a;
    if (has(F_CLOUDS)) {
        float dens = max(max(tex.r, tex.g), tex.b);       // densite depuis la carte N&B
        color = vec3(1.0) * lit;                          // nuages blancs eclaires
        a = clamp(dens * amax, 0.0, 1.0);
    } else {                                              // atmosphere
        color = tex * lit * 1.6;
        a = clamp(amax * (0.30 + 0.70 * ndl), 0.0, 1.0);  // plus dense cote jour
    }
    outColor = vec4(color * um.baseColor.rgb, a);
}
