// spr/shaders/common.glsl — declarations PARTAGEES par les pipelines.
// glslc resout `#include "common.glsl"` relativement au fichier includeur. Chaque
// etage (vert/frag) est compile separement : il inclut ce bloc une seule fois,
// donc pas de redefinition. Un unique point de verite pour le bloc Frame ->
// offsets std140 identiques partout, aucun decalage d'UBO possible. Correspond
// EXACTEMENT a la struct FrameUbo de VulkanDevice.cpp.

// Bloc global de frame (set=0, binding=0).
//   sun.xyz  = position du Soleil (camera-relative) ; sun.w = has_sun (1/0)
//   sunColor = rgb couleur du Soleil ; w = intensite
//   ambient  = rgb fill ambiant minimal ; w = exposure (reserve tone mapping)
// L'eclairage principal est DIRECTIONNEL : la direction vers le Soleil se derive
// de normalize(sun.xyz - positionFragment) (Soleil a ~1 UA -> quasi constant sur
// un corps -> terminateur net et physiquement coherent).
// lightViewProj = matrice LIGHT-SPACE des ombres directionnelles (miroir de
//   FrameParams::light_view_proj). Repere camera-relative (comme les model).
// shadow.x = hasShadow (1/0), .y = normalOffset (monde), .z = depthBias,
//   .w = pcfTexel (1/resolution). Utilises par shadow.glsl (voir scene/mesh.frag).
layout(set = 0, binding = 0) uniform Frame {
    mat4 view;
    mat4 proj;
    vec4 sun;
    vec4 sunColor;
    vec4 ambient;
    mat4 lightViewProj;
    vec4 shadow;
} uf;

const float SPR_PI = 3.14159265358979;
