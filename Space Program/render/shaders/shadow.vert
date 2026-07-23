#version 450
// spr/shaders/shadow.vert — passe DEPTH-ONLY du shadow mapping directionnel.
//
// Rend la profondeur des occluders depuis le point de vue de la lumiere dans une
// shadow map (texture de profondeur). Il n'y a PAS de fragment shader associe :
// seule gl_Position (donc la profondeur ecrite) compte -> passe tres bon marche.
// On reutilise EXACTEMENT le format de sommet et les push constants de la passe
// scene (les attributs normal/uv sont ignores ici) : aucun buffer dedie, on
// re-parcourt simplement les memes maillages.
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

// Seule la POSITION est consommee (profondeur). Le pipeline d'ombre ne declare
// donc qu'un attribut de sommet (location 0), meme si le buffer est un Vertex
// complet (stride inchange) : normal/uv ne sont simplement pas lus ici.
layout(location = 0) in vec3 inPos;

layout(push_constant) uniform Push {
    mat4 model;
    vec4 color;
    int  style;
} pc;

void main() {
    // `model` est deja camera-relative (translation monde->oeil faite par la Scene) ;
    // lightViewProj est exprimee dans CE MEME repere -> reprojection coherente avec
    // l'echantillonnage fait par shadow.glsl dans les shaders de surface.
    gl_Position = uf.lightViewProj * pc.model * vec4(inPos, 1.0);
}
