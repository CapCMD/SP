#version 450
// spr/shaders/star.vert — STARFIELD natif (pipeline Star, topologie POINT_LIST).
// Chaque etoile est une DIRECTION a l'infini. L'oeil est deja a l'origine (rendu
// camera-relative) : seule la ROTATION de la vue compte -> le champ est stable au
// zoom et a la translation, et tourne correctement avec l'arcball. La profondeur
// est forcee au plan lointain (z = w) ; le pipeline desactive le test/ecriture de
// profondeur et dessine EN PREMIER, donc les corps l'occultent naturellement.
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

layout(location = 0) in vec3 inDir;    // direction unitaire (sur la sphere celeste)
layout(location = 1) in vec3 inAttr;   // x = brillance, y = temperature couleur, z = taille px

layout(location = 0) out float vBright;
layout(location = 1) out float vTemp;

void main() {
    vec3 vd = mat3(uf.view) * inDir;             // rotation seule (pas de translation)
    vec4 clip = uf.proj * vec4(vd, 0.0);         // point a l'infini dans cette direction
    clip.z = 0.0;                                // reversed-Z : profondeur = plan lointain (0)
    gl_Position = clip;
    gl_PointSize = clamp(inAttr.z, 1.0, 8.0);
    vBright = inAttr.x;
    vTemp   = inAttr.y;
}
