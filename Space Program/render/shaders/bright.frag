#version 450
// spr/shaders/bright.frag — extraction des hautes lumieres (seuil soft-knee) pour
// le bloom. Sous-echantillonne la cible HDR pleine resolution (le sampler lineaire
// half-res fait un premier downsample). Seules les valeurs > seuil debordent :
// le bloom reste MAITRISE (le Soleil, les limbes tres brillants), pas un halo global.
layout(location = 0) in vec2 vUv;
layout(set = 0, binding = 0) uniform sampler2D uSrc;
layout(push_constant) uniform Push { vec2 dir; float threshold; float pad; } pc;
layout(location = 0) out vec4 outColor;

void main() {
    vec3 c = texture(uSrc, vUv).rgb;
    float br = max(c.r, max(c.g, c.b));
    float k = max(br - pc.threshold, 0.0) / max(br, 1e-4);  // soft-knee
    outColor = vec4(c * k, 1.0);
}
