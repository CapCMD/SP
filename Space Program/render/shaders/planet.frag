#version 450
// spr/shaders/planet.frag — MATERIAU PLANETAIRE natif (pipeline PlanetPbr).
//
// Ce qui est IMPLEMENTE a cette etape :
//   - eclairage directionnel (Soleil) + BRDF Cook-Torrance (GGX) metallic-rough
//     -> terminateur net et physiquement coherent (le N.L de la sphere EST la
//        separation jour/nuit ; aucune shadow map necessaire sur un corps convexe)
//   - albedo procedural par archetype (oceans/terres/glace, rocheux, geante, glace)
//   - normal mapping ANALYTIQUE : perturbation de la normale par le gradient d'un
//     champ de hauteur (bump), sans carte externe
//   - rugosite/specular coherents (oceans lisses et brillants, terres rugueuses)
//   - emissif nocturne (lumieres de villes) cote nuit -> lisibilite du cote sombre
//   - lisere atmospherique (rim) optionnel cote jour
//   - ambiant minimal (JAMAIS d'ambiant plat : le contraste jour/nuit est preserve)
//
// Ce qui est STUB / EXTENSION (structure prete, cf. docs/RENDER_MATERIALS.md) :
//   - cartes reelles (albedo/normal/rough/night) : les samplers existent et sont
//     lies ; il suffit de fournir les textures + lever le flag MAT_*_MAP.
//   - couche nuageuse (MAT_CLOUDS), diffusion atmospherique complete (MAT_ATMOSPHERE)
//   - eclipses inter-corps (ombre de la Lune sur la Terre) : shadow map -> passe
//     dediee ; la structure d'UBO peut accueillir une matrice light-space.
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

layout(location = 0) in vec3 vWorld;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec3 vObj;

layout(push_constant) uniform Push {
    mat4 model;
    vec4 color;   // teinte du corps issue du snapshot (utilisee pour les rocheux)
    int  style;
} pc;

// UBO par-materiau (set=1, binding=0). Correspond a MaterialUbo de VulkanDevice.
layout(set = 1, binding = 0) uniform Material {
    vec4  baseColor;   // rgb teinte albedo, w alpha
    vec4  pbr;         // x roughness, y metallic, z emissive, w nightIntensity
    vec4  extra;       // x rimStrength, y oceanLevel, z detailScale, w (reserve)
    vec4  colorLow;    // rgb fond/oceans/plaines
    vec4  colorMid;    // rgb terres/reliefs
    vec4  colorHigh;   // rgb sommets/glace
    ivec4 flags;       // x archetype, y features, z seed, w (reserve)
} um;

// Cartes optionnelles. Non echantillonnees a cette etape (flags MAT_*_MAP=0) mais
// LIEES (textures par defaut neutres) : le set de descripteurs est complet/valide,
// et fournir de vraies cartes + lever le flag suffit a les activer.
layout(set = 1, binding = 1) uniform sampler2D albedoMap;
layout(set = 1, binding = 2) uniform sampler2D normalMap;
layout(set = 1, binding = 3) uniform sampler2D roughMap;
layout(set = 1, binding = 4) uniform sampler2D nightMap;

layout(location = 0) out vec4 outColor;

// --- archetypes (miroir de SurfaceArchetype) ---------------------------------
const int ARCH_STAR = 0, ARCH_EARTH = 1, ARCH_ROCK = 2, ARCH_GAS = 3, ARCH_ICE = 4;
// --- bits de fonctionnalite (miroir de MaterialFeature) ----------------------
const int F_ALBEDO_MAP = 1, F_NORMAL_MAP = 2, F_ROUGH_MAP = 4, F_NIGHT_MAP = 8;
const int F_PROCEDURAL = 16, F_NIGHT_LIGHTS = 32, F_OCEAN_SPEC = 64;
bool has(int bit) { return (um.flags.y & bit) != 0; }

// --- bruit value 3D + fbm (aucune texture) -----------------------------------
float hash13(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}
float vnoise(vec3 x) {
    vec3 i = floor(x);
    vec3 f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    float n000 = hash13(i + vec3(0, 0, 0));
    float n100 = hash13(i + vec3(1, 0, 0));
    float n010 = hash13(i + vec3(0, 1, 0));
    float n110 = hash13(i + vec3(1, 1, 0));
    float n001 = hash13(i + vec3(0, 0, 1));
    float n101 = hash13(i + vec3(1, 0, 1));
    float n011 = hash13(i + vec3(0, 1, 1));
    float n111 = hash13(i + vec3(1, 1, 1));
    float nx00 = mix(n000, n100, f.x);
    float nx10 = mix(n010, n110, f.x);
    float nx01 = mix(n001, n101, f.x);
    float nx11 = mix(n011, n111, f.x);
    float nxy0 = mix(nx00, nx10, f.y);
    float nxy1 = mix(nx01, nx11, f.y);
    return mix(nxy0, nxy1, f.z);   // ~[0,1]
}
float fbm(vec3 p) {
    float a = 0.5, s = 0.0;
    for (int i = 0; i < 5; ++i) { s += a * vnoise(p); p *= 2.02; a *= 0.5; }
    return s;   // ~[0,1]
}

// Champ de hauteur du corps, dans le repere OBJET (stable). Sert a la couleur ET
// au bump (via son gradient numerique).
// Offset de graine BORNE : float(seed)*grand nombre depasserait la precision
// float (le bruit deviendrait constant sur la sphere). On derive un decalage
// dans [0,17] qui preserve la variation locale.
vec3 seed_offset() {
    float s = float(um.flags.z);
    return vec3(fract(s * 0.0131) * 17.0,
                fract(s * 0.0291) * 17.0,
                fract(s * 0.0517) * 17.0);
}
float heightField(vec3 p) {
    vec3 sp = p * um.extra.z + seed_offset();
    float continents = fbm(sp * 0.85);
    float detail     = fbm(sp * 3.2);
    return clamp(continents * 0.66 + detail * 0.34, 0.0, 1.0);
}

// --- BRDF Cook-Torrance (GGX / Smith / Fresnel-Schlick) ----------------------
float D_GGX(float ndh, float rough) {
    float a = rough * rough;
    float a2 = a * a;
    float d = ndh * ndh * (a2 - 1.0) + 1.0;
    return a2 / max(SPR_PI * d * d, 1e-7);
}
float G_Smith(float ndv, float ndl, float rough) {
    float k = (rough + 1.0); k = k * k / 8.0;
    float gv = ndv / (ndv * (1.0 - k) + k);
    float gl = ndl / (ndl * (1.0 - k) + k);
    return gv * gl;
}
vec3 F_Schlick(float vdh, vec3 f0) {
    return f0 + (1.0 - f0) * pow(clamp(1.0 - vdh, 0.0, 1.0), 5.0);
}

// Echantillonnage EQUIRECTANGULAIRE d'une direction objet, SANS couture. Le u vient
// de atan(y,x) qui saute de +PI a -PI au meridien arriere : la derivee implicite y
// devient enorme -> le GPU choisit le mip le plus grossier -> un "trait gris" relie
// les deux poles. On corrige les gradients (du corrige au saut) et on sample via
// textureGrad -> couture invisible (le wrap REPEAT du sampler gere le bord u=0/1).
vec4 texEquirect(sampler2D tex, vec3 p) {
    vec2 uv = vec2(atan(p.y, p.x) * (0.5 / SPR_PI) + 0.5, acos(clamp(p.z, -1.0, 1.0)) / SPR_PI);
    vec2 ddx = dFdx(uv), ddy = dFdy(uv);
    if (ddx.x >  0.5) ddx.x -= 1.0; else if (ddx.x < -0.5) ddx.x += 1.0;
    if (ddy.x >  0.5) ddy.x -= 1.0; else if (ddy.x < -0.5) ddy.x += 1.0;
    return textureGrad(tex, uv, ddx, ddy);
}

void main() {
    int arch = um.flags.x;
    vec3 pObj = normalize(vObj);

    // --- etoile (Soleil) : NON eclairee, pleine luminosite depuis la carte ----
    if (arch == ARCH_STAR) {
        vec3 c = um.baseColor.rgb;
        if (has(F_ALBEDO_MAP)) {
            c = texEquirect(albedoMap, pObj).rgb;
        }
        outColor = vec4(c * um.pbr.z, 1.0);   // emissif = albedo * intensite (HDR -> bloom)
        return;
    }

    // --- surface procedurale : albedo, rugosite, masque ocean ----------------
    float h = heightField(pObj);
    float lat = abs(pObj.z);                 // 0 equateur, 1 pole (axe objet = z)
    vec3  albedo;
    float rough = clamp(um.pbr.x, 0.04, 1.0);
    float oceanMask = 0.0;                    // 1 = surface liquide (specular)
    float landMask = 1.0;

    if (arch == ARCH_EARTH) {
        float sea = um.extra.y;              // oceanLevel
        landMask  = smoothstep(sea - 0.03, sea + 0.04, h);
        oceanMask = 1.0 - landMask;
        // ocean : profond (large) -> cotier (pres du trait de cote), plus clair
        vec3 deep    = um.colorLow.rgb * 0.7;
        vec3 coastal = um.colorLow.rgb * 1.35;
        vec3 ocean   = mix(deep, coastal, smoothstep(sea - 0.22, sea, h));
        // terres : vegetation vs zones arides (2e bruit) puis reliefs en altitude
        float arid = fbm(pObj * um.extra.z * 1.7 + vec3(31.7));
        vec3 veg    = um.colorMid.rgb;
        vec3 desert = vec3(0.60, 0.48, 0.30);
        vec3 land   = mix(veg, desert, smoothstep(0.46, 0.66, arid));
        land = mix(land, um.colorHigh.rgb, smoothstep(sea + 0.16, sea + 0.34, h));
        albedo = mix(ocean, land, landMask);
        // calottes polaires + banquise : hautes latitudes
        float ice = smoothstep(0.70, 0.86, lat);
        ice = max(ice, smoothstep(0.60, 0.90, lat) * landMask);
        ice = clamp(ice, 0.0, 1.0);
        albedo = mix(albedo, vec3(0.92, 0.94, 0.98), ice);
        landMask = max(landMask, ice);
        rough = mix(0.10, 0.90, landMask);   // ocean lisse, terre rugueuse
    } else if (arch == ARCH_GAS) {
        // bandes zonales : sinus de la latitude module par le bruit
        float band = sin(pObj.z * 14.0 + fbm(pObj * 3.0) * 4.0) * 0.5 + 0.5;
        albedo = mix(um.colorLow.rgb, um.colorMid.rgb, band);
        albedo = mix(albedo, um.colorHigh.rgb, smoothstep(0.85, 1.0, lat)); // poles
        rough = 0.65;
    } else if (arch == ARCH_ICE) {
        albedo = mix(um.colorMid.rgb, um.colorHigh.rgb, h);
        rough = 0.5;
    } else { // ARCH_ROCK / defaut
        vec3 dark  = um.colorLow.rgb;
        vec3 light = um.colorHigh.rgb;
        // "crateres" : bruit ridge
        float crater = abs(fbm(pObj * (um.extra.z * 1.5)) - 0.5) * 2.0;
        albedo = mix(dark, light, h) * (0.75 + 0.35 * crater);
        rough = clamp(um.pbr.x, 0.6, 1.0);
    }

    // teinte : rocheux/desert modules par la couleur du snapshot (pc.color),
    // EarthLike garde ses couleurs procedurales (sinon continents bleutes a tort).
    vec3 tint = (arch == ARCH_EARTH) ? vec3(1.0) : pc.color.rgb;
    albedo *= um.baseColor.rgb * tint;

    // carte albedo reelle si fournie (ex. Mars 8k) : REMPLACE la synthese
    // procedurale (sinon la teinte rocheuse du corps assombrirait la vraie carte).
    // Seule la teinte de base du materiau la module (baseColor = blanc -> carte pure).
    if (has(F_ALBEDO_MAP)) {
        // UV equirectangulaire depuis la direction objet (pole = axe z objet), sans couture
        albedo = texEquirect(albedoMap, pObj).rgb * um.baseColor.rgb;
    }
    if (has(F_ROUGH_MAP)) {
        vec2 rm = texEquirect(roughMap, pObj).rg;   // R=rough, G=ocean/spec
        rough = rm.r; oceanMask = rm.g;
    }

    // --- normal mapping analytique (bump par gradient du champ de hauteur) ----
    vec3 No = pObj;                              // normale objet = position (sphere)
    float bumpAmp = (arch == ARCH_GAS) ? 0.0 : (0.35 * (1.0 - oceanMask));
    // pas de relief procedural quand une vraie carte pilote la surface
    if (bumpAmp > 0.0 && !has(F_NORMAL_MAP) && !has(F_ALBEDO_MAP)) {
        float e = 0.75 / max(um.extra.z, 1.0);
        float h0 = heightField(pObj);
        vec3 g = vec3(heightField(pObj + vec3(e, 0, 0)) - h0,
                      heightField(pObj + vec3(0, e, 0)) - h0,
                      heightField(pObj + vec3(0, 0, e)) - h0) / e;
        vec3 tg = g - dot(g, No) * No;           // composante tangente
        No = normalize(No - bumpAmp * tg);
    }
    vec3 N = normalize(mat3(pc.model) * No);     // -> monde (camera-relative)

    // --- eclairage directionnel + Cook-Torrance ------------------------------
    vec3 V = normalize(-vWorld);                 // oeil a l'origine
    vec3 L = (uf.sun.w > 0.5) ? normalize(uf.sun.xyz - vWorld) : vec3(0.0, 0.0, 1.0);
    vec3 H = normalize(L + V);
    float ndl = dot(N, L);
    float ndlC = max(ndl, 0.0);
    float ndv = max(dot(N, V), 1e-3);
    float ndh = max(dot(N, H), 0.0);
    float vdh = max(dot(V, H), 0.0);

    float metallic = clamp(um.pbr.y, 0.0, 1.0);
    vec3  F0 = mix(vec3(0.04), albedo, metallic);
    // renforce la specularite des oceans
    if (has(F_OCEAN_SPEC)) F0 = mix(F0, vec3(0.02), oceanMask);

    float D = D_GGX(ndh, rough);
    float G = G_Smith(ndv, ndlC, rough);
    vec3  Fr = F_Schlick(vdh, F0);
    vec3  spec = (D * G) * Fr / (4.0 * ndv * ndlC + 1e-4);
    vec3  kd = (vec3(1.0) - Fr) * (1.0 - metallic);

    vec3 sun = uf.sunColor.rgb * uf.sunColor.w;
    vec3 direct = (kd * albedo / SPR_PI + spec) * sun * ndlC;

    // --- ambiant MINIMAL (fill spatial, ne detruit pas le contraste) ---------
    vec3 ambient = albedo * uf.ambient.rgb;

    // --- emissif nocturne : lisibilite du cote nuit --------------------------
    vec3 night = vec3(0.0);
    if (has(F_NIGHT_LIGHTS) && um.pbr.w > 0.0) {
        float dark = smoothstep(0.10, -0.20, ndl);   // 1 cote nuit, doux au terminateur
        // amas de lumieres : sur les terres, sparse et haute frequence
        float cities = smoothstep(0.55, 0.85, fbm(pObj * um.extra.z * 6.0));
        cities *= landMask;
        vec3 lampColor = vec3(1.0, 0.82, 0.45);
        night = lampColor * cities * dark * um.pbr.w;
        if (has(F_NIGHT_MAP)) {
            night = texEquirect(nightMap, pObj).rgb * dark * um.pbr.w;
        }
    }

    // --- lisere atmospherique (jour) : approx. bon marche de la diffusion -----
    vec3 rim = vec3(0.0);
    if (um.extra.x > 0.0) {
        float f = pow(1.0 - ndv, 3.0);               // Fresnel geometrique
        float lit = clamp(ndl + 0.25, 0.0, 1.0);     // seulement cote eclaire
        rim = vec3(0.35, 0.55, 1.0) * f * lit * um.extra.x;
    }

    vec3 color = ambient + direct + night + rim + albedo * um.pbr.z;
    outColor = vec4(color, um.baseColor.a);
}
