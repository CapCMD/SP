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
layout(set = 0, binding = 0) uniform Frame {
    mat4 view;
    mat4 proj;
    vec4 sun;
    vec4 sunColor;
    vec4 ambient;
} uf;

const float SPR_PI = 3.14159265358979;
