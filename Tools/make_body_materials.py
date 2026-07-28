# Tools/make_body_materials.py — LES CORPS PEINTS PAR NOUS, depuis assets/textures.
#
#   UnrealEditor-Cmd.exe SP.uproject -run=pythonscript -script="<abs>/make_body_materials.py"
#   (sortie dans Saved/Logs/SP.log, PAS dans la console)
#
# Produit, sous /Game/SP/Bodies :
#   T_<Corps>[_night|_clouds]   les textures equirectangulaires du projet
#   M_SP_Body                   materiau maitre ECLAIRE des corps
#   M_SP_Star                   materiau NON ECLAIRE du Soleil
#   M_SP_Ring                   materiau MASQUE et DEUX FACES des anneaux
#   MI_SP_<Corps>               une instance par corps
#
# CE QUE CE MATERIAU FAIT ET QUE LES GLB NE FAISAIENT PAS :
#   . JOUR / NUIT reel : la face non eclairee emet la carte de nuit (lumieres des
#     villes). Le masque vient du produit scalaire entre la normale du pixel et la
#     DIRECTION DU SOLEIL, passee en parametre par le C++ (qui la connait deja :
#     c'est celle de sa lumiere directionnelle). Aucune valeur en dur ;
#   . NUAGES EN CARTE, pas en coquille. La Terre avait une seconde sphere GLB
#     low-poly translucide, dont les facettes se voyaient au gros plan : elle
#     disparait, les nuages sont melanges dans l'albedo. Meme mecanique pour
#     l'ATMOSPHERE de Venus (sa carte de surface reste dessous) ;
#   . ANNEAUX de Saturne avec leur ALPHA reel (8k_saturn_ring_alpha.png).
#
# Re-executable : les assets existants sont remplaces.
import os
import unreal

RACINE = os.path.join(
    unreal.SystemLibrary.get_project_directory(), "Space Program", "assets", "textures")
DEST = "/Game/SP/Bodies"

LIB = unreal.MaterialEditingLibrary
OUTILS = unreal.AssetToolsHelpers.get_asset_tools()

# --- LA TABLE : un corps -> ses cartes. Les noms de corps sont ceux de
# `fen::ephem::Body` cote C++ (SPSolarSystem.cpp les cite tels quels).
# (albedo, nuit, nuages) ; None = absent.
CORPS = {
    "Sun":       ("Sun/8k_sun.jpg", None, None),
    "Mercury":   ("Mercure/8k_mercury.jpg", None, None),
    # Venus : la carte de SURFACE dessous, l'ATMOSPHERE par-dessus en « nuages ».
    "Venus":     ("Venus/8k_venus_surface.jpg", None, "Venus/4k_venus_atmosphere.jpg"),
    "Earth":     ("Earth/8k_earth_daymap.jpg", "Earth/8k_earth_nightmap.jpg",
                  "Earth/8k_earth_clouds.jpg"),
    "Moon":      ("Moon/8k_moon.jpg", None, None),
    "Mars":      ("Mars/8k_mars.jpg", None, None),
    "Phobos":    ("Mars/Mars - Phobos.tif", None, None),
    "Deimos":    ("Mars/Mars - Deimos.tif", None, None),
    "Jupiter":   ("Jupiter/8k_jupiter.jpg", None, None),
    "Io":        ("Jupiter/Jupiter - Io (A).tif", None, None),
    "Europa":    ("Jupiter/Jupiter - Europa.tif", None, None),
    "Ganymede":  ("Jupiter/Jupiter - Ganymede.tif", None, None),
    "Callisto":  ("Jupiter/Jupiter - Callisto.tif", None, None),
    "Saturn":    ("Saturne/8k_saturn.jpg", None, None),
    "Mimas":     ("Saturne/Saturn - Mimas.tif", None, None),
    "Enceladus": ("Saturne/Saturn - Enceladus.tif", None, None),
    "Tethys":    ("Saturne/Saturn - Tethys.tif", None, None),
    "Dione":     ("Saturne/Saturn - Dione.tif", None, None),
    "Rhea":      ("Saturne/Saturn - Rhea.tif", None, None),
    "Titan":     ("Saturne/Saturn - Titan.tif", None, None),
    "Iapetus":   ("Saturne/Saturn - Iapetus.tif", None, None),
    "Uranus":    ("Uranus/2k_uranus.jpg", None, None),
    "Miranda":   ("Uranus/Uranus - Miranda.tif", None, None),
    "Umbriel":   ("Uranus/Uranus - Umbriel.tif", None, None),
    "Titania":   ("Uranus/Uranus - Titania.tif", None, None),
    "Oberon":    ("Uranus/Uranus - Oberon.tif", None, None),
    "Neptune":   ("Neptune/2k_neptune.jpg", None, None),
    "Triton":    ("Neptune/Neptune - Triton.tif", None, None),
    "Pluto":     ("Pluton/Pluto.tif", None, None),
    "Charon":    ("Pluton/Pluto - Charon.tif", None, None),
}
RING_SRC = "Saturne/8k_saturn_ring_alpha.png"


# ---------------------------------------------------------------------------
def importer(rel, nom):
    """Importe une texture du dossier du projet. Renvoie l'asset (ou None)."""
    if rel is None:
        return None
    src = os.path.join(RACINE, rel.replace("/", os.sep))
    chemin = "{}/{}".format(DEST, nom)
    if unreal.EditorAssetLibrary.does_asset_exist(chemin):
        return unreal.EditorAssetLibrary.load_asset(chemin)
    if not os.path.isfile(src):
        unreal.log_error("[corps] source introuvable : {}".format(src))
        return None
    t = unreal.AssetImportTask()
    t.filename = src
    t.destination_path = DEST
    t.destination_name = nom
    t.automated = True
    t.save = True
    t.replace_existing = True
    OUTILS.import_asset_tasks([t])
    tex = unreal.EditorAssetLibrary.load_asset(chemin)
    if tex is None:
        unreal.log_error("[corps] import echoue : {}".format(src))
        return None
    # sRGB : ce sont des photos/cartes d'albedo. Le groupe WORLD garde le
    # streaming normal (une seule planete est vue de pres a la fois).
    tex.set_editor_property("srgb", True)
    unreal.EditorAssetLibrary.save_asset(chemin)
    return tex


def neuf(mat, cls, x, y):
    return LIB.create_material_expression(mat, cls, x, y)


# UNE LIAISON QUI RATE NE DIT RIEN, ET C'EST LE PIEGE (2026-07-27).
# `connect_material_expressions` renvoie False quand le nom de broche ne
# correspond a rien — un nom d'entree change entre versions d'UE, par exemple —
# et le materiau se compile quand meme, avec la branche non cablee. Le defaut ne
# se voit alors qu'a l'ecran, des semaines plus tard. On exige donc le retour.
def relier(de, sortie, vers, entree):
    if not LIB.connect_material_expressions(de, sortie, vers, entree):
        unreal.log_error(
            "[corps] LIAISON REFUSEE : {} [{}] -> {} [{}] (nom de broche inconnu ?)".format(
                type(de).__name__, sortie or "<defaut>", type(vers).__name__, entree))


def creer_materiau_corps(defaut):
    """Materiau maitre ECLAIRE : albedo + nuages + jour/nuit."""
    chemin = "{}/M_SP_Body".format(DEST)
    if unreal.EditorAssetLibrary.does_asset_exist(chemin):
        unreal.EditorAssetLibrary.delete_asset(chemin)
    mat = OUTILS.create_asset("M_SP_Body", DEST, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("two_sided", False)

    # --- albedo -------------------------------------------------------------
    alb = neuf(mat, unreal.MaterialExpressionTextureSampleParameter2D, -900, -200)
    alb.set_editor_property("parameter_name", "Albedo")
    if defaut:
        alb.set_editor_property("texture", defaut)

    # --- nuages / atmosphere : melanges DANS l'albedo (pas de coquille) ------
    nua = neuf(mat, unreal.MaterialExpressionTextureSampleParameter2D, -900, 100)
    nua.set_editor_property("parameter_name", "Clouds")
    if defaut:
        nua.set_editor_property("texture", defaut)

    # ═══ L'ATMOSPHERE TOURNE, ET PAS A LA VITESSE DU SOL ═══ (2026-07-27)
    # Le voile etait une carte FIGEE sur la surface : une planete dont
    # l'atmosphere est solidaire de son sol au metre pres. C'est faux pour les
    # deux corps qui en ont une ici, et spectaculairement faux pour Venus, dont
    # les nuages font le tour en ~4,4 jours quand le sol en met 243.
    # MECANIQUE : un simple decalage de la coordonnee U (la longitude) du
    # sampler des nuages. Le C++ ecrit `CloudSpin` a chaque frame depuis
    # l'EPOQUE DE JEU et le vent zonal REEL du corps (fen::ephem::
    # cloud_zonal_wind_ms) : la derive suit donc la cadence du temps, ce qui est
    # le comportement juste — a l'echelle du temps reel, un nuage ne fait pas le
    # tour de la Terre en une seconde.
    # PAS DE frac() ICI, ET C'EST VOULU : le decalage est une CONSTANTE sur tout
    # le maillage, donc U reste aussi continu qu'avant (delta -> 1+delta) et les
    # derivees d'UV ne sautent nulle part. Un frac() dans le materiau creerait
    # une couture de mip la ou il replie, en travers du disque.
    uv = neuf(mat, unreal.MaterialExpressionTextureCoordinate, -1400, 100)
    uv.set_editor_property("coordinate_index", 0)
    spin = neuf(mat, unreal.MaterialExpressionScalarParameter, -1400, 260)
    spin.set_editor_property("parameter_name", "CloudSpin")
    spin.set_editor_property("default_value", 0.0)
    zero_v = neuf(mat, unreal.MaterialExpressionConstant, -1400, 380)
    zero_v.set_editor_property("r", 0.0)
    decalage = neuf(mat, unreal.MaterialExpressionAppendVector, -1150, 300)
    relier(spin, "", decalage, "A")      # U : la longitude
    relier(zero_v, "", decalage, "B")    # V : la latitude ne bouge pas
    uv_nua = neuf(mat, unreal.MaterialExpressionAdd, -1050, 130)
    relier(uv, "", uv_nua, "A")
    relier(decalage, "", uv_nua, "B")
    # « UVs » ET PAS « Coordinates » : l'entree du sampler s'appelle Coordinates
    # dans le C++, mais `connect_material_expressions` compare au nom RACCOURCI de
    # la broche du graphe (`UMaterialGraphNode::GetShortenPinName` : Coordinates ->
    # UVs, TextureObject -> Tex). Avec « Coordinates », la liaison est REFUSEE en
    # silence et le voile ne bouge pas — pris sur le fait par `relier`.
    relier(uv_nua, "", nua, "UVs")

    qte_nua = neuf(mat, unreal.MaterialExpressionScalarParameter, -900, 380)
    qte_nua.set_editor_property("parameter_name", "CloudAmount")
    qte_nua.set_editor_property("default_value", 0.0)
    masque_nua = neuf(mat, unreal.MaterialExpressionMultiply, -600, 200)
    relier(nua, "R", masque_nua, "A")
    relier(qte_nua, "", masque_nua, "B")
    blanc = neuf(mat, unreal.MaterialExpressionConstant3Vector, -600, 380)
    blanc.set_editor_property("constant", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
    melange = neuf(mat, unreal.MaterialExpressionLinearInterpolate, -300, 0)
    relier(alb, "RGB", melange, "A")
    relier(blanc, "", melange, "B")
    relier(masque_nua, "", melange, "Alpha")
    LIB.connect_material_property(melange, "", unreal.MaterialProperty.MP_BASE_COLOR)

    # --- jour / nuit ---------------------------------------------------------
    # La nuit est la ou la normale du pixel s'ecarte du Soleil. `SunDir` est la
    # direction dans laquelle la lumiere VOYAGE (du Soleil vers l'exterieur) :
    # dot(N, SunDir) > 0 <=> la face tourne le dos au Soleil.
    nuit = neuf(mat, unreal.MaterialExpressionTextureSampleParameter2D, -900, 700)
    nuit.set_editor_property("parameter_name", "Night")
    if defaut:
        nuit.set_editor_property("texture", defaut)
    qte_nuit = neuf(mat, unreal.MaterialExpressionScalarParameter, -900, 1000)
    qte_nuit.set_editor_property("parameter_name", "NightAmount")
    qte_nuit.set_editor_property("default_value", 0.0)
    soleil = neuf(mat, unreal.MaterialExpressionVectorParameter, -900, 1150)
    soleil.set_editor_property("parameter_name", "SunDir")
    soleil.set_editor_property("default_value", unreal.LinearColor(1.0, 0.0, 0.0, 0.0))
    normale = neuf(mat, unreal.MaterialExpressionPixelNormalWS, -900, 1300)
    dot = neuf(mat, unreal.MaterialExpressionDotProduct, -600, 1200)
    relier(normale, "", dot, "A")
    relier(soleil, "", dot, "B")
    zero = neuf(mat, unreal.MaterialExpressionConstant, -600, 1400)
    zero.set_editor_property("r", 0.0)
    masque_nuit = neuf(mat, unreal.MaterialExpressionMax, -400, 1250)
    relier(dot, "", masque_nuit, "A")
    relier(zero, "", masque_nuit, "B")
    m1 = neuf(mat, unreal.MaterialExpressionMultiply, -200, 900)
    relier(nuit, "RGB", m1, "A")
    relier(masque_nuit, "", m1, "B")
    m2 = neuf(mat, unreal.MaterialExpressionMultiply, 0, 900)
    relier(m1, "", m2, "A")
    relier(qte_nuit, "", m2, "B")
    LIB.connect_material_property(m2, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    # --- rugosite : une planete n'est pas un miroir --------------------------
    rug = neuf(mat, unreal.MaterialExpressionScalarParameter, -300, 600)
    rug.set_editor_property("parameter_name", "Roughness")
    rug.set_editor_property("default_value", 0.9)
    LIB.connect_material_property(rug, "", unreal.MaterialProperty.MP_ROUGHNESS)

    LIB.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(chemin)
    unreal.log("[corps] materiau maitre : {}".format(chemin))
    return mat


def creer_materiau_etoile(tex):
    """Le Soleil : NON ECLAIRE. Une etoile ne depend d'aucune lumiere."""
    chemin = "{}/M_SP_Star".format(DEST)
    if unreal.EditorAssetLibrary.does_asset_exist(chemin):
        unreal.EditorAssetLibrary.delete_asset(chemin)
    mat = OUTILS.create_asset("M_SP_Star", DEST, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    ech = neuf(mat, unreal.MaterialExpressionTextureSampleParameter2D, -600, 0)
    ech.set_editor_property("parameter_name", "Albedo")
    if tex:
        ech.set_editor_property("texture", tex)
    force = neuf(mat, unreal.MaterialExpressionScalarParameter, -600, 250)
    force.set_editor_property("parameter_name", "Intensity")
    force.set_editor_property("default_value", 8.0)
    prod = neuf(mat, unreal.MaterialExpressionMultiply, -300, 0)
    relier(ech, "RGB", prod, "A")
    relier(force, "", prod, "B")
    LIB.connect_material_property(prod, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    LIB.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(chemin)
    unreal.log("[corps] materiau etoile : {}".format(chemin))
    return mat


def creer_materiau_anneau(tex):
    """Les anneaux : MASQUE (alpha reel) et DEUX FACES (on les voit des deux cotes).

    ECLAIRE, comme les corps — les anneaux sont de la glace qui reflechit le
    Soleil, pas une source. Ils sont vus sous une incidence RASANTE (ils sont dans
    le plan equatorial, incline de 26,7 deg sur l'ecliptique) : leur eclairement
    plafonne donc vers sin(26,7 deg) ~ 0,45 de celui d'une face qui fait face au
    Soleil. C'est PHYSIQUE, et ca suffit — a condition que les normales du mesh
    soient justes (cf. make_ring_mesh.py, ou l'oubli les rendait NOIRS).
    """
    chemin = "{}/M_SP_Ring".format(DEST)
    if unreal.EditorAssetLibrary.does_asset_exist(chemin):
        unreal.EditorAssetLibrary.delete_asset(chemin)
    mat = OUTILS.create_asset("M_SP_Ring", DEST, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    mat.set_editor_property("two_sided", True)
    ech = neuf(mat, unreal.MaterialExpressionTextureSampleParameter2D, -600, 0)
    ech.set_editor_property("parameter_name", "Albedo")
    if tex:
        ech.set_editor_property("texture", tex)
    LIB.connect_material_property(ech, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
    LIB.connect_material_property(ech, "A", unreal.MaterialProperty.MP_OPACITY_MASK)
    rug = neuf(mat, unreal.MaterialExpressionScalarParameter, -300, 300)
    rug.set_editor_property("parameter_name", "Roughness")
    rug.set_editor_property("default_value", 0.85)
    LIB.connect_material_property(rug, "", unreal.MaterialProperty.MP_ROUGHNESS)

    # ═══ DIFFUSION DES ANNEAUX — APPROXIMATION DECLAREE [GDD 6.8] ═══
    # Un disque LAMBERTIEN plat s'eteint des que le Soleil rase son plan. Or c'est
    # exactement la situation : le Soleil a traverse le plan des anneaux en 2025,
    # il n'est donc qu'a quelques degres au-dessus en 2026, et les anneaux
    # ressortaient NOIRS (verifie en capture — le meme materiau en EMISSIF les
    # montrait parfaitement, ce qui a innocente geometrie, UV et alpha).
    # Les vrais anneaux restent lumineux dans cette configuration parce qu'ils ne
    # sont pas une surface : ce sont des milliards de blocs de glace qui DIFFUSENT
    # la lumiere entre eux, y compris vers l'avant. On approche cette diffusion
    # multiple par une composante emissive tiree de la MEME texture — donc qui
    # respecte la structure des anneaux (divisions comprises), sans inventer de
    # motif. `RingScatter` la dose ; la mettre a 0 rend le disque lambertien pur.
    diff = neuf(mat, unreal.MaterialExpressionScalarParameter, -600, 400)
    diff.set_editor_property("parameter_name", "RingScatter")
    diff.set_editor_property("default_value", 0.45)
    emis = neuf(mat, unreal.MaterialExpressionMultiply, -300, 100)
    relier(ech, "RGB", emis, "A")
    relier(diff, "", emis, "B")
    LIB.connect_material_property(emis, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    LIB.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(chemin)
    unreal.log("[corps] materiau anneaux : {}".format(chemin))
    return mat


def creer_instance(nom, parent, albedo, nuit, nuages):
    chemin = "{}/MI_SP_{}".format(DEST, nom)
    if unreal.EditorAssetLibrary.does_asset_exist(chemin):
        unreal.EditorAssetLibrary.delete_asset(chemin)
    mi = OUTILS.create_asset("MI_SP_{}".format(nom), DEST,
                             unreal.MaterialInstanceConstant,
                             unreal.MaterialInstanceConstantFactoryNew())
    LIB.set_material_instance_parent(mi, parent)
    if albedo:
        LIB.set_material_instance_texture_parameter_value(mi, "Albedo", albedo)
    if nuit:
        LIB.set_material_instance_texture_parameter_value(mi, "Night", nuit)
        LIB.set_material_instance_scalar_parameter_value(mi, "NightAmount", 1.0)
    if nuages:
        LIB.set_material_instance_texture_parameter_value(mi, "Clouds", nuages)
        # Venus est COUVERTE : son atmosphere masque presque tout. La Terre, non.
        LIB.set_material_instance_scalar_parameter_value(
            mi, "CloudAmount", 0.95 if nom == "Venus" else 0.55)
    unreal.EditorAssetLibrary.save_asset(chemin)
    return mi


# ---------------------------------------------------------------------------
def main():
    unreal.EditorAssetLibrary.make_directory(DEST)

    # 1) les textures
    tex = {}
    for nom, (alb, nuit, nua) in CORPS.items():
        tex[nom] = (
            importer(alb, "T_{}".format(nom)),
            importer(nuit, "T_{}_night".format(nom)),
            importer(nua, "T_{}_clouds".format(nom)),
        )
    tex_anneau = importer(RING_SRC, "T_SaturnRing")
    unreal.log("[corps] {} corps texturés".format(len(tex)))

    # 2) les materiaux maitres
    defaut = tex.get("Earth", (None,))[0]
    maitre = creer_materiau_corps(defaut)
    etoile = creer_materiau_etoile(tex.get("Sun", (None,))[0])
    creer_materiau_anneau(tex_anneau)

    # 3) une instance par corps (le Soleil sur le materiau NON ECLAIRE)
    n = 0
    for nom, (alb, nuit, nua) in tex.items():
        parent = etoile if nom == "Sun" else maitre
        creer_instance(nom, parent, alb, nuit if nom != "Sun" else None,
                       nua if nom != "Sun" else None)
        n += 1
    unreal.log("[corps] TERMINE : {} instances creees sous {}".format(n, DEST))


main()
