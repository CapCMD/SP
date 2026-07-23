// spr/shaders/shadow.glsl — echantillonnage de la shadow map directionnelle avec
// PCF (Percentage Closer Filtering) : ombres DOUCES (bord adouci sur un noyau 3x3)
// + NORMAL OFFSET (anti peter-panning / shadow acne). Inclus par les shaders de
// SURFACE qui RECOIVENT l'ombre (scene.frag = interieur ISS, mesh.frag = exterieur
// GLB). Requiert le bloc Frame de common.glsl (uf.lightViewProj, uf.shadow).
//
// La shadow map (set 0, binding 1) est rendue par shadow.vert avec ortho() :
// profondeur [0,1], SANS flip Y. On reprojette ici le fragment avec la MEME matrice
// puis uv = ndc*0.5+0.5 (auto-coherent). Le sampler est NEAREST + CLAMP_TO_BORDER
// blanc (profondeur 1 = "loin") -> tout ce qui tombe hors de la carte est ECLAIRE.
// NEAREST (et non LINEAR) : le format D32_SFLOAT ne garantit pas le filtrage
// lineaire sur tous les GPU ; le lissage vient du PCF, pas du sampler.

layout(set = 0, binding = 1) uniform sampler2D uShadowMap;

// VISIBILITE de la lumiere en [0,1] : 1 = pleinement eclaire, 0 = totalement a
// l'ombre (les valeurs intermediaires = bord PCF adouci). worldPos et N sont en
// repere camera-relative (le meme que lightViewProj). L = direction vers la lumiere.
float shadow_visibility(vec3 worldPos, vec3 N, vec3 L) {
    if (uf.shadow.x < 0.5) return 1.0;             // ombres desactivees (ex. vue carte)

    // --- Normal offset : on decale le point d'echantillonnage le long de la
    // normale, d'autant plus que la surface est rasante a la lumiere (1 - N.L).
    // Cela combat le shadow acne SANS enfoncer l'ombre sous l'objet (peter-panning)
    // comme le ferait un gros biais de profondeur constant.
    float ndl   = clamp(dot(N, L), 0.0, 1.0);
    float slope = 1.0 - ndl;
    vec3  sp    = worldPos + N * (uf.shadow.y * (0.4 + slope));

    vec4 lp  = uf.lightViewProj * vec4(sp, 1.0);
    vec3 ndc = lp.xyz / lp.w;                      // ortho -> w=1, mais on reste robuste
    // Hors du frustum de profondeur de la lumiere -> pas d'info d'ombre = eclaire.
    if (ndc.z <= 0.0 || ndc.z >= 1.0) return 1.0;
    vec2 uv = ndc.xy * 0.5 + 0.5;

    float current = ndc.z - uf.shadow.z;           // biais de comparaison (anti-acne)
    float texel   = uf.shadow.w;

    // --- PCF 3x3 : moyenne de 9 comparaisons decalees d'un texel -> transition
    // d'ombre progressive (penombre bon marche) au lieu d'un bord dur aliase.
    float lit = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x) {
            float closest = texture(uShadowMap, uv + vec2(x, y) * texel).r;
            lit += (current <= closest) ? 1.0 : 0.0;
        }
    return lit * (1.0 / 9.0);
}
