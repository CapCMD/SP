# Tools/iss_collision.py — rend la géométrie de l'ISS SOLIDE pour le joueur.
#
# Un StaticMesh importé n'a pas de collision « simple » (boîtes/capsules) : par
# défaut un pawn le TRAVERSE. Le jeu de référence construit un BVH sur les
# 511 183 triangles du modèle et teste la collision contre la vraie géométrie.
# L'équivalent UE natif : CollisionTraceFlag = UseComplexAsSimple, qui fait
# servir les triangles eux-mêmes de volume de collision.
#
# À lancer une fois après l'import (éditeur FERMÉ) :
#   UnrealEditor-Cmd.exe SP.uproject -run=pythonscript -script="Tools/iss_collision.py"
import unreal

DOSSIERS = ["/Game/ISS/Interior/ISS_Internal/StaticMeshes"]

ar = unreal.AssetRegistryHelpers.get_asset_registry()
ar.scan_paths_synchronous(DOSSIERS, True)

modifies = []
for dossier in DOSSIERS:
    for a in ar.get_assets_by_path(dossier, recursive=True):
        mesh = a.get_asset()
        if not isinstance(mesh, unreal.StaticMesh):
            continue
        body = mesh.get_editor_property("body_setup")
        if body is None:
            continue
        if body.get_editor_property("collision_trace_flag") != \
                unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE:
            body.set_editor_property(
                "collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
            modifies.append(mesh)

for mesh in modifies:
    unreal.EditorAssetLibrary.save_loaded_asset(mesh, False)

unreal.log(f"[iss-col] {len(modifies)} meshes passes en UseComplexAsSimple (collision reelle).")
