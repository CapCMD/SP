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

assets = unreal.EditorAssetLibrary.list_assets(ROOT, recursive=True, include_folder=False)
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
