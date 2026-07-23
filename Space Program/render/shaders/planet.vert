#version 450
// spr/shaders/planet.vert — sommet du materiau planetaire (pipeline PlanetPbr).
// La position d'entree est LOCALE (sphere unite : inPos = direction objet unite).
// Le model (push) est deja camera-relative : vWorld est proche de 0 pres de la
// camera (precision metrique). On transmet aussi la position OBJET (vObj) : c'est
// le repere stable du corps ou la synthese procedurale (continents, reliefs) est
// echantillonnee -> les continents ne "glissent" pas quand la planete se deplace.
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;

layout(push_constant) uniform Push {
    mat4 model;
    vec4 color;
    int  style;
} pc;

layout(location = 0) out vec3 vWorld;   // camera-relative
layout(location = 1) out vec3 vNormal;  // normale monde (rotation camera-relative)
layout(location = 2) out vec3 vObj;     // position objet (sphere unite)

void main() {
    vec4 wp = pc.model * vec4(inPos, 1.0);
    vWorld  = wp.xyz;
    vNormal = mat3(pc.model) * inNormal;   // echelle uniforme : direction preservee
    vObj    = inPos;
    gl_Position = uf.proj * uf.view * wp;
}
