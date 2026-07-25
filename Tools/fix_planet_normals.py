# Tools/fix_planet_normals.py — lisse l'ombrage des corps SANS rien inventer.
#
# Les GLB des planetes ont de VRAIES normales lisses et leurs textures, mais les
# spheres sont peu denses (~2000 sommets). Rendues avec des normales PLATES, elles
# montrent un ombrage FACETTE. On force le recalcul de normales LISSES (moyenne
# des faces adjacentes = radial pour une sphere) sur chaque StaticMesh, puis on
# sauve. Robuste : subsystem moderne sinon EditorStaticMeshLibrary (deprecated).
#
#   UnrealEditor-Cmd.exe SP.uproject -run=pythonscript -script="Tools/fix_planet_normals.py"
import unreal

ROOT = "/Game/SolarSystem"

sm_sub = None
try:
    sm_sub = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
except Exception as e:
    unreal.log_warning(f"[normals] subsystem indisponible: {e}")
unreal.log(f"[normals] StaticMeshEditorSubsystem = {sm_sub}")


def get_settings(mesh):
    if sm_sub is not None:
        return sm_sub.get_lod_build_settings(mesh, 0)
    return unreal.EditorStaticMeshLibrary.get_lod_build_settings(mesh, 0)


def set_settings(mesh, bs):
    if sm_sub is not None:
        sm_sub.set_lod_build_settings(mesh, 0, bs)
    else:
        unreal.EditorStaticMeshLibrary.set_lod_build_settings(mesh, 0, bs)


assets = unreal.EditorAssetLibrary.list_assets(ROOT, recursive=True, include_folder=False)
n_fixed = 0
for a in assets:
    obj = unreal.EditorAssetLibrary.load_asset(a)
    if not isinstance(obj, unreal.StaticMesh) or obj.get_num_lods() < 1:
        continue
    try:
        bs = get_settings(obj)
        was = bs.recompute_normals
        # APRES tessellation PN, les meshes sont DENSES (~15k tris) : recalculer
        # les normales donne alors un ombrage parfaitement lisse (le probleme des
        # coutures UV etait propre a la basse densite). rien invente.
        bs.recompute_normals = True
        bs.recompute_tangents = True
        bs.use_mikk_t_space = True
        set_settings(obj, bs)
        unreal.EditorAssetLibrary.save_asset(a)
        n_fixed += 1
        unreal.log(f"[normals] {a}  recompute_normals: {was} -> True")
    except Exception as e:
        unreal.log_error(f"[normals] ECHEC {a}: {e}")

unreal.log(f"[normals] TERMINE : {n_fixed} StaticMesh lisses et sauves sous {ROOT}")
