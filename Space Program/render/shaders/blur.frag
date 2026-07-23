#version 450
// spr/shaders/blur.frag — flou gaussien SEPARABLE (1D). Applique deux fois
// (horizontal puis vertical) via le pas `dir` en push constant. 9 taps.
layout(location = 0) in vec2 vUv;
layout(set = 0, binding = 0) uniform sampler2D uSrc;
layout(push_constant) uniform Push { vec2 dir; float threshold; float pad; } pc;
layout(location = 0) out vec4 outColor;

void main() {
    const float w[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
    vec3 c = texture(uSrc, vUv).rgb * w[0];
    for (int i = 1; i < 5; ++i) {
        c += texture(uSrc, vUv + pc.dir * float(i)).rgb * w[i];
        c += texture(uSrc, vUv - pc.dir * float(i)).rgb * w[i];
    }
    outColor = vec4(c, 1.0);
}
