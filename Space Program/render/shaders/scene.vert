#version 450
// spr/shaders/scene.vert — sommet LEGACY partage maillages simples + lignes +
// marqueurs (corps sans materiau riche, orbites, vecteurs). La position d'entree
// est LOCALE ; le model (push) est deja camera-relative, donc vWorld est en
// espace camera-relative (proche de 0 pres de la camera).
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;

layout(push_constant) uniform Push {
    mat4 model;
    vec4 color;
    int  style;
} pc;

layout(location = 0) out vec3 vWorld;
layout(location = 1) out vec3 vNormal;

void main() {
    vec4 wp = pc.model * vec4(inPos, 1.0);
    vWorld  = wp.xyz;
    vNormal = mat3(pc.model) * inNormal;   // echelle uniforme : direction preservee
    gl_Position = uf.proj * uf.view * wp;
}
