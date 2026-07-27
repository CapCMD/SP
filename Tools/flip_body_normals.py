# Tools/orient_body_normals.py (nom historique : flip_body_normals.py)
# REMET LES FACES DES CORPS VERS L'EXTERIEUR — en le MESURANT, pas en le supposant.
#
# DIAGNOSTIC (2026-07-27). Au gros plan, un corps montrait un « dome » translucide
# et des pans manquants : on voyait l'INTERIEUR de son hemisphere arriere. Quatre
# pistes eliminees par l'experience (materiaux, normales lissees, Nanite,
# precision GPU — l'artefact est INVARIANT D'ECHELLE), et le mesh lui-meme est
# sain (1 section, 1 slot, sphere de rayon 100 : cf. diag_body_meshes.py).
# L'INDICE QUI TRANCHE : sur la Terre, la face NUIT (texture EMISSIVE, qui ne
# depend pas des normales) rend en disque parfaitement rond, tandis que la seule
# face ECLAIREE (qui, elle, en depend) est cassee. Les faces regardaient dedans.
#
# PIEGE PAYE, ET C'EST LA RAISON DE CE SCRIPT : un premier jet retournait TOUS les
# corps sans condition, apres verification sur le seul Mars. Mars et la Terre sont
# devenus corrects... et SATURNE, dont les faces etaient deja bonnes, s'est
# retrouvee cassee et ses anneaux ont disparu. Le sens des faces n'est PAS uniforme
# d'un GLB a l'autre. On ne retourne donc plus a l'aveugle : on MESURE l'orientation
# de chaque corps et on ne corrige que ceux qui regardent dedans. Corollaire :
# le script devient IDEMPOTENT (le relancer ne casse rien).
#
#   UnrealEditor-Cmd.exe SP.uproject -run=pythonscript -script="<abs>/flip_body_normals.py"
#
# Sortie dans Saved/Logs/SP.log (PAS dans la console).
import unreal

RACINE = "/Game/SolarSystem"
ECHANTILLONS = 64          # triangles sondes par corps : un vote suffit largement

AU = unreal.GeometryScript_AssetUtils
NRM = unreal.GeometryScript_Normals
QRY = unreal.GeometryScript_MeshQueries


def new_dm():
    try:
        return unreal.DynamicMesh()
    except Exception:
        return unreal.new_object(unreal.DynamicMesh)


def regarde_dehors(dm):
    """Vote : la normale de face (deduite du WINDING) pointe-t-elle vers le dehors ?

    Ces corps sont des spheres fermees centrees sur l'origine du mesh. Pour une
    telle surface, une face correctement orientee verifie n . c > 0, ou c est le
    centroide du triangle. On sonde des triangles repartis et on prend la
    majorite : robuste a quelques faces degenerees, et sans dependance a une
    convention d'import.
    """
    # `get_triangle_count` vient du DynamicMesh lui-meme (comme dans
    # subdivide_planets.py) : GeometryScript_MeshQueries n'expose pas de
    # `get_num_triangle_ids` en 5.8.
    total = dm.get_triangle_count()
    if total <= 0:
        return True, 0, 0
    pas = max(1, total // ECHANTILLONS)
    dehors = dedans = 0
    for tid in range(0, total, pas):
        ok, v1, v2, v3 = QRY.get_triangle_positions(dm, tid)
        if not ok:
            continue
        n = (v2 - v1).cross(v3 - v1)
        c = (v1 + v2 + v3) / 3.0
        d = n.dot(c)
        if d > 0.0:
            dehors += 1
        elif d < 0.0:
            dedans += 1
    return dehors >= dedans, dehors, dedans


unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous([RACINE], True)
actifs = unreal.EditorAssetLibrary.list_assets(RACINE, recursive=True, include_folder=False)

n_vus = n_corriges = 0
for chemin in actifs:
    if "Circle" in chemin:            # anneaux plats : pas de « dehors » defini
        continue
    sm = unreal.EditorAssetLibrary.load_asset(chemin)
    if not isinstance(sm, unreal.StaticMesh):
        continue

    dm = new_dm()
    read_opts = unreal.GeometryScriptCopyMeshFromAssetOptions()
    read_lod = unreal.GeometryScriptMeshReadLOD()
    dm, _ = AU.copy_mesh_from_static_mesh(sm, dm, read_opts, read_lod)

    n_vus += 1
    bon, dehors, dedans = regarde_dehors(dm)
    court = chemin.split("/")[-1]
    if bon:
        unreal.log("[orient] {} : deja vers l'exterieur ({}/{}) -> intact".format(
            court, dehors, dehors + dedans))
        continue

    dm = NRM.flip_normals(dm)
    dm = NRM.recompute_normals(dm, unreal.GeometryScriptCalculateNormalsOptions())

    write_opts = unreal.GeometryScriptCopyMeshToAssetOptions()
    write_opts.enable_recompute_normals = False   # sinon le build les re-calcule
    write_opts.enable_recompute_tangents = True
    write_lod = unreal.GeometryScriptMeshWriteLOD()
    write_lod.lod_index = 0
    AU.copy_mesh_to_static_mesh(dm, sm, write_opts, write_lod)
    unreal.EditorAssetLibrary.save_asset(chemin)
    n_corriges += 1
    unreal.log("[orient] {} : regardait DEDANS ({}/{}) -> RETOURNE".format(
        court, dedans, dehors + dedans))

unreal.log("[orient] TERMINE : {} corps inspectes, {} retournes".format(n_vus, n_corriges))
