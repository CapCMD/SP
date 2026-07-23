#version 450
// spr/shaders/fullscreen.vert — triangle plein ecran sans buffer de sommets
// (gl_VertexIndex). Sert aux passes de post-process (bright, blur, composite).
layout(location = 0) out vec2 vUv;
void main() {
    vUv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);  // (0,0)(2,0)(0,2)
    gl_Position = vec4(vUv * 2.0 - 1.0, 0.0, 1.0);
}
