# Tools/smooth_normals_geo.py — normales LISSES par sommet sur les corps DENSES.
#
# La tessellation PN a arrondi la GEOMETRIE mais gardait des normales par-face
# (ombrage facette sur les corps peu texturés). Ici on recalcule des normales
# LISSES par sommet via Geometry Script, et on ecrit avec recompute=False pour
# que le build ne les re-facette pas. NE re-tessellate PAS (agit sur le dense).
import unreal

ROOT = "/Game/SolarSystem"
AU = unreal.GeometryScript_AssetUtils
NRM = unreal.GeometryScript_Normals

def new_dm():
    try:
        return unreal.DynamicMesh()
    except Exception:
        return unreal.new_object(unreal.DynamicMesh)

assets = unreal.EditorAssetLibrary.list_assets(ROOT, recursive=True, include_folder=False)
n = 0
for a in assets:
    if "Circle" in a:
        continue
    sm = unreal.EditorAssetLibrary.load_asset(a)
    if not isinstance(sm, unreal.StaticMesh):
        continue
    try:
        dm = new_dm()
        dm, _ = AU.copy_mesh_from_static_mesh(
            sm, dm, unreal.GeometryScriptCopyMeshFromAssetOptions(),
            unreal.GeometryScriptMeshReadLOD())
        dm = NRM.set_per_vertex_normals(dm)          # normales lisses par sommet
        wopts = unreal.GeometryScriptCopyMeshToAssetOptions()
        wopts.enable_recompute_normals = False       # GARDER nos normales lisses
        wopts.enable_recompute_tangents = True
        wlod = unreal.GeometryScriptMeshWriteLOD()
        wlod.lod_index = 0
        AU.copy_mesh_to_static_mesh(dm, sm, wopts, wlod)
        unreal.EditorAssetLibrary.save_asset(a)
        n += 1
        unreal.log(f"[smooth] {a}")
    except Exception as e:
        unreal.log_error(f"[smooth] ECHEC {a}: {e}")

unreal.log(f"[smooth] TERMINE : {n} corps lissés (normales par sommet)")
