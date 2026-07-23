#version 450
// spr/shaders/composite.frag — PASSE DE COMPOSITION (HDR -> LDR swapchain).
// Lit la cible HDR lineaire (R16F), ajoute le bloom, applique l'exposition, le
// tone mapping ACES (filmic) puis l'encodage gamma. C'est ici que le rendu
// devient "physique et lisible" plutot que lineaire brut. La swapchain est UNORM
// -> on encode le gamma a la main (l'UI ImGui, dessinee APRES, reste non affectee).
layout(location = 0) in vec2 vUv;

layout(set = 0, binding = 0) uniform sampler2D uHdr;    // scene HDR pleine resolution
layout(set = 0, binding = 1) uniform sampler2D uBloom;  // bloom (demi-res, upscale lineaire)

layout(push_constant) uniform Push {
    float exposure;
    float bloom_strength;
    float _pad0;
    float _pad1;
} pc;

layout(location = 0) out vec4 outColor;

// ACES filmic (approximation de Krzysztof Narkowicz) : rolloff doux des hautes
// lumieres, aspect cinema sobre.
vec3 aces(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    vec3 hdr = texture(uHdr, vUv).rgb;
    hdr += texture(uBloom, vUv).rgb * pc.bloom_strength;   // bloom additif
    hdr *= pc.exposure;                                    // exposition
    vec3 mapped = aces(hdr);                               // tone mapping
    mapped = pow(mapped, vec3(1.0 / 2.2));                 // encodage gamma (UNORM)
    outColor = vec4(mapped, 1.0);
}
