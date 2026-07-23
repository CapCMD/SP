#version 450
// spr/shaders/mesh.vert — sommet d'un MAILLAGE TEXTURE a UV reelles (pipeline
// MeshTextured : modeles GLB comme l'ISS exterieure). Contrairement au pipeline
// planetaire (UV equirectangulaire derivee de la position), on transmet les UV
// REELLES du modele (attribut de sommet location=2). `model` (push) est deja
// camera-relative -> vWorld proche de 0 pres de la camera (precision metrique).
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;

layout(push_constant) uniform Push {
    mat4 model;
    vec4 color;
    int  style;
} pc;

layout(location = 0) out vec3 vWorld;   // camera-relative
layout(location = 1) out vec3 vNormal;  // normale monde (rotation camera-relative)
layout(location = 2) out vec2 vUv;      // UV du modele

void main() {
    vec4 wp = pc.model * vec4(inPos, 1.0);
    vWorld  = wp.xyz;
    vNormal = mat3(pc.model) * inNormal;   // echelle uniforme : direction preservee
    vUv     = inUv;
    gl_Position = uf.proj * uf.view * wp;
}
