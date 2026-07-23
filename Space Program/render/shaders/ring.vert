#version 450
// spr/shaders/ring.vert — anneau plan (Saturne). Meme structure que planet.vert :
// on transmet la position OBJET (vObj) pour deriver la distance radiale -> U de la
// carte d'anneau (bandeau radial). Le model place/oriente/echelle l'anneau.
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;

layout(push_constant) uniform Push { mat4 model; vec4 color; int style; } pc;

layout(location = 0) out vec3 vWorld;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec3 vObj;

void main() {
    vec4 wp = pc.model * vec4(inPos, 1.0);
    vWorld  = wp.xyz;
    vNormal = mat3(pc.model) * inNormal;
    vObj    = inPos;
    gl_Position = uf.proj * uf.view * wp;
}
