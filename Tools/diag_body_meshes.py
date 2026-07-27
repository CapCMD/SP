# Tools/diag_body_meshes.py — QU'Y A-T-IL VRAIMENT DANS UN CORPS ?
#
# Ecrit pour trancher, le 2026-07-27, un artefact visible en capture depuis au
# moins le 2026-07-25 : au gros plan, un corps montre un DOME translucide et des
# pans manquants, comme si sa geometrie etait dechiree. Quatre pistes ont ete
# eliminees par l'experience (materiaux translucides, normales facettees, Nanite,
# precision GPU a l'echelle reelle : l'artefact est INVARIANT D'ECHELLE).
# Reste a regarder ce que le mesh CONTIENT, au lieu de le deviner.
#
#   UnrealEditor-Cmd.exe SP.uproject -run=pythonscript -script="<abs>/diag_body_meshes.py"
#
# La sortie va dans Saved/Logs/SP.log (PAS dans la console).
import unreal

RACINE = "/Game/SolarSystem"

unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous([RACINE], True)
actifs = unreal.EditorAssetLibrary.list_assets(RACINE, recursive=True, include_folder=False)

for chemin in actifs:
    obj = unreal.EditorAssetLibrary.load_asset(chemin)
    if not isinstance(obj, unreal.StaticMesh):
        continue
    bounds = obj.get_bounds()
    n_mat = obj.get_num_sections(0) if hasattr(obj, "get_num_sections") else -1
    mats = obj.get_editor_property("static_materials")
    noms = []
    for m in mats:
        mi = m.material_interface
        noms.append(mi.get_name() if mi else "<vide>")
    unreal.log(
        "[diag] {} | sections={} | slots={} [{}] | bounds_r={:.3f} | extent=({:.3f},{:.3f},{:.3f})".format(
            chemin.split("/")[-1], n_mat, len(mats), ", ".join(noms),
            bounds.sphere_radius,
            bounds.box_extent.x, bounds.box_extent.y, bounds.box_extent.z))

unreal.log("[diag] TERMINE")
