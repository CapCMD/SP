# Tools/make_sky.py — cree les assets du FOND ETOILE (Voie lactee).
#
# A lancer editeur FERME :
#   UnrealEditor-Cmd.exe SP.uproject -run=pythonscript -script="Tools/make_sky.py"
#
# Produit :
#   /Game/SP/T_Starfield     texture 8K equirectangulaire (groupe Skybox)
#   /Game/SP/M_SP_Starfield  materiau NON ECLAIRE, DEUX FACES, parametres
#                            "Texture" (la carte du ciel) et "Intensity".
#
# Pourquoi des assets plutot qu'un decodage a la volee : le premier portage
# creait la texture avec CreateTransient (8192 x 4096). Le dome etait bien
# soumis au renderer (WasRecentlyRendered = 1) mais ressortait NOIR. Un asset
# importe est compresse, mippe et resident — et le materiau de projet peut etre
# deux faces, ce qu'un MaterialInstanceDynamic ne peut pas changer.
import os
import unreal

SRC = os.path.join(
    unreal.SystemLibrary.get_project_directory(),
    "Space Program", "assets", "textures", "8k_stars_milky_way.jpg")
DEST = "/Game/SP"
TEX_NAME = "T_Starfield"
MAT_NAME = "M_SP_Starfield"


def regler_texture(tex, chemin):
    """Les reglages de la carte du ciel — appliques a l'import ET aux re-passages."""
    # Groupe Skybox : pas de perte de resolution au streaming, filtrage adapte
    # a une carte du ciel. sRGB : la source est une photo.
    tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_SKYBOX)
    tex.set_editor_property("srgb", True)
    tex.set_editor_property("never_stream", True)
    # ═══ PAS DE BC1 SUR UN CHAMP D'ETOILES ═══ (2026-07-27)
    # TC_DEFAULT compresse en BC1 : DEUX couleurs par bloc de 4x4, en 5:6:5. Sur
    # une image continue (une planete) c'est invisible ; sur un champ d'etoiles —
    # des points isoles d'un pixel sur du noir — c'est le cas PATHOLOGIQUE du
    # format : l'etoile est moyennee avec le noir de son bloc, elle s'etale et
    # perd son eclat. C'est une des deux raisons du ciel « flou » (l'autre etant
    # l'anti-aliasing temporel, cf. Config/DefaultEngine.ini).
    # BC7 : meme famille de compression mais bien plus de precision par bloc.
    # L'ancien moteur Vulkan, lui, lisait le JPG en RGBA brut — d'ou sa nettete.
    tex.set_editor_property("compression_settings",
                            unreal.TextureCompressionSettings.TC_BC7)
    unreal.EditorAssetLibrary.save_asset(chemin)


def importer_texture():
    chemin = "{}/{}".format(DEST, TEX_NAME)
    if unreal.EditorAssetLibrary.does_asset_exist(chemin):
        # RE-APPLIQUER LES REGLAGES, ne pas juste renvoyer l'asset : la version
        # precedente sortait ici, si bien qu'un changement de reglage (la
        # compression, ci-dessous) n'atteignait JAMAIS une texture deja importee.
        # Un script « re-executable » qui saute son propre travail ne l'est pas.
        tex = unreal.EditorAssetLibrary.load_asset(chemin)
        if tex is not None:
            regler_texture(tex, chemin)
        unreal.log("[SPSky] texture deja presente, reglages reappliques : {}".format(chemin))
        return tex
    if not os.path.isfile(SRC):
        unreal.log_error("[SPSky] source introuvable : {}".format(SRC))
        return None

    task = unreal.AssetImportTask()
    task.filename = SRC
    task.destination_path = DEST
    task.destination_name = TEX_NAME
    task.automated = True
    task.save = True
    task.replace_existing = True
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

    tex = unreal.EditorAssetLibrary.load_asset(chemin)
    if tex is None:
        unreal.log_error("[SPSky] import de la texture echoue")
        return None
    regler_texture(tex, chemin)
    unreal.log("[SPSky] texture importee : {}".format(chemin))
    return tex


def creer_materiau(tex):
    chemin = "{}/{}".format(DEST, MAT_NAME)
    if unreal.EditorAssetLibrary.does_asset_exist(chemin):
        unreal.EditorAssetLibrary.delete_asset(chemin)   # re-executable

    mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        MAT_NAME, DEST, unreal.Material, unreal.MaterialFactoryNew())
    if mat is None:
        unreal.log_error("[SPSky] creation du materiau echouee")
        return
    # NON ECLAIRE : le ciel n'est pas une surface, c'est une source.
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    # DEUX FACES : on regarde l'INTERIEUR du dome.
    mat.set_editor_property("two_sided", True)
    # IS SKY : SANS CE DRAPEAU, Lumen voit le dome comme une source d'aire
    # geante et l'interieur de l'ISS ressort entierement crame (verifie par
    # capture : correct des qu'on coupe la GI). "Is Sky" exclut le materiau de
    # la scene Lumen tout en le laissant se dessiner normalement.
    mat.set_editor_property("is_sky", True)

    lib = unreal.MaterialEditingLibrary
    echant = lib.create_material_expression(
        mat, unreal.MaterialExpressionTextureSampleParameter2D, -600, 0)
    echant.set_editor_property("parameter_name", "Texture")
    if tex is not None:
        echant.set_editor_property("texture", tex)
    # BIAIS DE MIP NEGATIF (2026-07-27) : la voute est une sphere ENORME et la
    # carte est equirectangulaire ; les derivees d'UV y sont grandes, donc UE
    # choisissait un mip grossier et le ciel sortait FLOU alors que la texture est
    # bien en 8K, mip 0, jamais streamee (verifie). On force un cran plus net.
    # RESTE, et c'est GEOMETRIQUE, pas un reglage : 8192 px etales sur 360 deg font
    # ~23 px/degre, quand l'ecran en affiche ~43 a 45 deg de champ. Le ciel est
    # donc AGRANDI ~2x : au-dela de ce biais, il faudrait une source plus definie
    # (ou des etoiles en points plutot qu'en texture).
    echant.set_editor_property("mip_value_mode", unreal.TextureMipValueMode.TMVM_MIP_BIAS)
    echant.set_editor_property("const_mip_value", -1.0)

    force = lib.create_material_expression(
        mat, unreal.MaterialExpressionScalarParameter, -600, 250)
    force.set_editor_property("parameter_name", "Intensity")
    force.set_editor_property("default_value", 1.0)

    produit = lib.create_material_expression(
        mat, unreal.MaterialExpressionMultiply, -300, 0)
    lib.connect_material_expressions(echant, "RGB", produit, "A")
    lib.connect_material_expressions(force, "", produit, "B")
    lib.connect_material_property(produit, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    lib.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(chemin)
    unreal.log("[SPSky] materiau cree : {}".format(chemin))


def main():
    creer_materiau(importer_texture())


main()
