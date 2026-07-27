# Tools/subdivide_planets.py — arrondit les corps SANS rien inventer.
#
# Les spheres GLB sont peu denses (~2000 sommets) : silhouette polygonale et
# terminateur en escalier, meme avec des normales lisses. La TESSELLATION PN
# (Point-Normal) subdivise chaque triangle en surface de Bezier courbee, en
# n'utilisant QUE les sommets, normales et UV existants (aucune texture ni
# geometrie inventee) : la silhouette et l'ombrage deviennent ronds.
#
#   UnrealEditor-Cmd.exe SP.uproject -run=pythonscript -script="Tools/subdivide_planets.py"
import unreal

ROOT = "/Game/SolarSystem"
LEVEL = 3   # 4 segments/arete -> 16 sous-triangles ; ~2000 -> ~32000 tris

# GARDE D'IDEMPOTENCE (ajoute le 2026-07-27, en tessellant les LUNES).
# Ce script balaye TOUT /Game/SolarSystem : sans garde, le relancer pour traiter
# un corps nouvellement importe re-tessellait aussi tous ceux qui l'etaient deja
# (15 000 -> 245 000 triangles, x16 a chaque passage). Un corps deja dense n'a
# plus rien a gagner : on le saute. Seuil bien au-dessus d'une sphere GLB brute
# (~960-2000 tris) et bien en-dessous d'un corps deja tessellé (~15 000).
DEJA_DENSE = 8000

SM = unreal.GeometryScript_AssetUtils     # copy_mesh_from/to_static_mesh
SUB = unreal.GeometryScript_MeshSubdivide  # apply_pn_tessellation

def new_dm():
    try:
        return unreal.DynamicMesh()
    except Exception:
        return unreal.new_object(unreal.DynamicMesh)

def tessellate(asset_path):
    sm = unreal.EditorAssetLibrary.load_asset(asset_path)
    if not isinstance(sm, unreal.StaticMesh):
        return False
    dm = new_dm()
    read_opts = unreal.GeometryScriptCopyMeshFromAssetOptions()
    read_lod = unreal.GeometryScriptMeshReadLOD()
    dm, outcome = SM.copy_mesh_from_static_mesh(sm, dm, read_opts, read_lod)
    tris0 = dm.get_triangle_count()
    if tris0 >= DEJA_DENSE:
        unreal.log(f"[subdiv] {asset_path}  DEJA DENSE ({tris0} tris) -> saute")
        return False

    tess_opts = unreal.GeometryScriptPNTessellateOptions()
    dm = SUB.apply_pn_tessellation(dm, tess_opts, LEVEL)
    tris1 = dm.get_triangle_count()

    write_opts = unreal.GeometryScriptCopyMeshToAssetOptions()
    write_opts.enable_recompute_normals = False   # garder les normales PN lisses
    write_opts.enable_recompute_tangents = True
    write_lod = unreal.GeometryScriptMeshWriteLOD()
    write_lod.lod_index = 0
    SM.copy_mesh_to_static_mesh(dm, sm, write_opts, write_lod)
    unreal.EditorAssetLibrary.save_asset(asset_path)
    unreal.log(f"[subdiv] {asset_path}  tris {tris0} -> {tris1}")
    return True

# Filet de securite, aligne sur fix_planet_materials.py : en commandlet le
# registre peut ne pas avoir balaye le dossier, et list_assets rendrait alors une
# liste VIDE — le script « reussirait » sans rien faire.
# NB POUR LE DIAGNOSTIC : les unreal.log de ce script vont dans Saved/Logs/SP.log,
# PAS dans la sortie standard. Une console muette ne veut donc pas dire que rien
# ne s'est passe (perdu 20 minutes la-dessus le 2026-07-27).
unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous([ROOT], True)

assets = unreal.EditorAssetLibrary.list_assets(ROOT, recursive=True, include_folder=False)
unreal.log(f"[subdiv] {len(assets)} actifs sous {ROOT}")
n = 0
for a in assets:
    if "Circle" in a:          # anneaux plats : inutile de subdiviser
        continue
    obj = unreal.EditorAssetLibrary.load_asset(a)
    if not isinstance(obj, unreal.StaticMesh):
        continue
    try:
        if tessellate(a):
            n += 1
    except Exception as e:
        unreal.log_error(f"[subdiv] ECHEC {a}: {e}")

unreal.log(f"[subdiv] TERMINE : {n} corps tessellés (niveau {LEVEL}) sous {ROOT}")
