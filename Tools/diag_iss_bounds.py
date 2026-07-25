# Tools/diag_iss_bounds.py — DIAGNOSTIC : les transformations de nœuds du GLB
# sont-elles cuites dans les sommets ?
#
# Enjeu : le GLB de l'ISS s'importe en centaines de StaticMesh SANS hiérarchie
# d'acteurs. Si chaque mesh est centré sur son propre pivot, les poser tous à
# l'origine empilerait la station en un tas. S'ils sont déjà dans un repère
# commun (transformations cuites), un seul acteur suffit — comme dans le jeu de
# référence, qui applique UNE transformation à tous les sous-maillages.
#
# Lecture : si les centres de boîte sont ÉLOIGNÉS les uns des autres (dizaines
# de mètres), les transformations sont cuites -> repère commun. S'ils sont tous
# proches de (0,0,0), elles ne le sont pas -> réimport « combine meshes ».
import unreal

DOSSIER = "/Game/ISS/Interior/ISS_Internal/StaticMeshes"

ar = unreal.AssetRegistryHelpers.get_asset_registry()
ar.scan_paths_synchronous([DOSSIER], True)     # commandlet : le registre est froid
assets = ar.get_assets_by_path(DOSSIER, recursive=True)
unreal.log(f"[diag] {len(assets)} assets sous {DOSSIER}")

n = 0
cx = cy = cz = []
centres = []
for a in assets:
    obj = a.get_asset()
    if not isinstance(obj, unreal.StaticMesh):
        continue
    b = obj.get_bounds()
    o, e = b.origin, b.box_extent
    centres.append((o.x, o.y, o.z))
    if n < 12:
        unreal.log(f"[diag] {a.asset_name}: centre=({o.x:.0f},{o.y:.0f},{o.z:.0f}) "
              f"demi-taille=({e.x:.0f},{e.y:.0f},{e.z:.0f})")
    n += 1

if centres:
    xs = [c[0] for c in centres]; ys = [c[1] for c in centres]; zs = [c[2] for c in centres]
    unreal.log(f"[diag] {n} StaticMesh")
    unreal.log(f"[diag] etendue des CENTRES : x[{min(xs):.0f},{max(xs):.0f}] "
          f"y[{min(ys):.0f},{max(ys):.0f}] z[{min(zs):.0f},{max(zs):.0f}]")
    span = max(max(xs) - min(xs), max(ys) - min(ys), max(zs) - min(zs))
    unreal.log(f"[diag] dispersion des centres : {span:.0f} unites")
    if span > 1000.0:
        unreal.log("[diag] VERDICT : transformations CUITES -> repere commun, un seul acteur suffit.")
    else:
        unreal.log("[diag] VERDICT : centres empiles -> reimport avec combine meshes necessaire.")
