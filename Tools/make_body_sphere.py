# Tools/make_body_sphere.py — LA SPHERE DES CORPS, faite par UE et non importee.
#
#   UnrealEditor-Cmd.exe SP.uproject -run=pythonscript -script="<abs>/make_body_sphere.py"
#   (sortie dans Saved/Logs/SP.log, PAS dans la console)
#
# POURQUOI ABANDONNER LES SPHERES DES GLB (decision utilisateur, 2026-07-27).
# Les GLB de corps cumulaient les defauts, tous payes en captures :
#   . FACES RETOURNEES : on voyait l'interieur de l'hemisphere arriere. Le defaut
#     etait present dans la capture de reference que le document decrivait
#     pourtant comme « lisse, ronde » ;
#   . un materiau d'import generique (Material_001), translucide et deux faces ;
#   . une COQUILLE DE NUAGES low-poly separee pour la Terre, dont les facettes se
#     voyaient au gros plan ;
#   . aucune maitrise du jour/nuit, des anneaux ni de l'atmosphere.
# Une sphere faite ici est juste PAR CONSTRUCTION : orientation, densite et UV
# sont choisis, pas subis. Et les textures du projet (assets/textures) sont des
# equirectangulaires 8K, exactement ce qu'une sphere lat-long attend.
#
# DENSITE : 128 x 256 -> 65 536 triangles. La silhouette d'un corps plein ecran
# ne doit pas se voir polygonale ; c'est 4x la densite des GLB tessellés (15 360).
# RAYON 100 : on garde la convention des anciens meshes, pour que le calcul
# d'echelle du rendu (rayon reel / rayon du mesh) reste inchange.
import unreal

DEST = "/Game/SP"
NOM = "SM_SP_Body"
RAYON = 100.0
STEPS_PHI = 128        # divisions en latitude
STEPS_THETA = 256      # divisions en longitude

PRIM = unreal.GeometryScript_Primitives
AU = unreal.GeometryScript_AssetUtils
NRM = unreal.GeometryScript_Normals
# La CREATION d'un asset vit dans _NewAssetUtils, pas dans _AssetUtils (qui ne
# sait que lire/ecrire dans un asset EXISTANT). Verifie par introspection.
NEW = unreal.GeometryScript_NewAssetUtils


def new_dm():
    try:
        return unreal.DynamicMesh()
    except Exception:
        return unreal.new_object(unreal.DynamicMesh)


def construire():
    dm = new_dm()
    opts = unreal.GeometryScriptPrimitiveOptions()
    dm = PRIM.append_sphere_lat_long(
        dm, opts, unreal.Transform(), RAYON, STEPS_PHI, STEPS_THETA)
    tris = dm.get_triangle_count()
    unreal.log("[sphere] maillage genere : {} triangles".format(tris))

    # Normales LISSES par sommet : une sphere n'a pas d'arete. On les ecrit avec
    # recompute=False cote asset, sinon le build les re-facette (piege connu du
    # projet, cf. smooth_normals_geo.py).
    dm = NRM.recompute_normals(dm, unreal.GeometryScriptCalculateNormalsOptions())
    return dm


def ecrire(dm):
    chemin = "{}/{}".format(DEST, NOM)
    if unreal.EditorAssetLibrary.does_asset_exist(chemin):
        unreal.EditorAssetLibrary.delete_asset(chemin)     # re-executable

    opts = unreal.GeometryScriptCreateNewStaticMeshAssetOptions()
    opts.enable_recompute_normals = False
    opts.enable_recompute_tangents = True
    sm, outcome = NEW.create_new_static_mesh_asset_from_mesh(dm, chemin, opts)
    if sm is None:
        unreal.log_error("[sphere] creation de l'asset ECHOUEE")
        return None
    unreal.EditorAssetLibrary.save_asset(chemin)
    b = sm.get_bounds()
    unreal.log("[sphere] asset cree : {} | rayon_bounds={:.3f}".format(
        chemin, b.sphere_radius))
    return sm


ecrire(construire())
unreal.log("[sphere] TERMINE")
