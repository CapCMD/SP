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


def importer_texture():
    chemin = "{}/{}".format(DEST, TEX_NAME)
    if unreal.EditorAssetLibrary.does_asset_exist(chemin):
        unreal.log("[SPSky] texture deja presente : {}".format(chemin))
        return unreal.EditorAssetLibrary.load_asset(chemin)
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
    # Groupe Skybox : pas de perte de resolution au streaming, filtrage adapte
    # a une carte du ciel. sRGB : la source est une photo.
    tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_SKYBOX)
    tex.set_editor_property("srgb", True)
    tex.set_editor_property("never_stream", True)
    unreal.EditorAssetLibrary.save_asset(chemin)
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
