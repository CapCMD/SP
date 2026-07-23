#version 450
// spr/shaders/star.frag — rendu d'une etoile ponctuelle. Point rond a bord doux ;
// couleur par temperature. Sortie en blend ADDITIF (voir pipeline) : sur le fond
// spatial quasi-noir, les etoiles se lisent comme des lueurs. Les valeurs peuvent
// depasser 1.0 (etoiles brillantes) -> compatible avec un futur bloom / tone
// mapping HDR (l'exposure vit deja dans le bloc Frame).
layout(location = 0) in float vBright;
layout(location = 1) in float vTemp;

layout(location = 0) out vec4 outColor;

void main() {
    vec2 d = gl_PointCoord - vec2(0.5);
    float r2 = dot(d, d);
    if (r2 > 0.25) discard;                       // disque
    float core = smoothstep(0.25, 0.0, r2);       // coeur brillant, bord attenue

    // temperature : 0 = froide (orange), 1 = chaude (bleu-blanc)
    vec3 cool = vec3(1.0, 0.80, 0.55);
    vec3 warm = vec3(0.75, 0.83, 1.0);
    vec3 tint = mix(cool, warm, clamp(vTemp, 0.0, 1.0));

    vec3 c = tint * vBright * core;
    outColor = vec4(c, core);
}
