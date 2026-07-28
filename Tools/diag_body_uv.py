# Tools/diag_body_uv.py — OU EST LA LONGITUDE 0 SUR LA SPHERE DES CORPS ?
#
#   UnrealEditor-Cmd.exe SP.uproject -run=pythonscript -script="<ABSOLU>/diag_body_uv.py"
#   (sortie dans Saved/Logs/SP.log, PAS dans la console)
#
# POURQUOI. `SPSolarSystem::MeshRotationFrom` doit savoir quel axe LOCAL du mesh
# porte le meridien origine de la carte. La reponse a d'abord ete LUE dans le
# generateur du moteur (FSphereGenerator : U = 1 - theta/2pi), ce qui donne
# « longitude 0 -> local -X ». Lire une source, c'est bien ; MESURER l'asset
# livre, c'est mieux — un reglage d'import, un miroir d'UV ou un changement de
# version invalideraient la deduction sans rien dire.
#
# Ce script prend les sommets de /Game/SP/SM_SP_Body et leurs UV, puis imprime,
# pour six directions locales remarquables, l'UV effectivement echantillonnee. Le
# verdict attendu, si le C++ a raison :
#     -X -> U ~ 0,50 (longitude 0)      +X -> U ~ 0,00 ou 1,00 (longitude 180)
#     +Y -> U ~ 0,75 (longitude +90 E)  -Y -> U ~ 0,25 (longitude -90)
#     +Z -> V ~ 0,00 (pole nord, haut de l'image)   -Z -> V ~ 1,00 (pole sud)
# CONVENTION DE LECTURE : U = 0,5 + lon/360 et V = (90 - lat)/180, celle des
# equirectangulaires du projet (nord en haut, longitude 0 au centre).
#
# MESURE DU 2026-07-27, sur l'asset livre : les six directions sortent EXACTEMENT
# a ces valeurs (lon 0,00 / 180,00 / +90,00 / -90,00 ; lat +-90,00). La deduction
# faite depuis le generateur du moteur est donc confirmee PAR L'ASSET, ce qui
# ferme le seul maillon de l'orientation que les oracles C++ ne peuvent pas voir
# (ils prouvent la PHASE ; ce script prouve la CONVENTION DU MAILLAGE).
import unreal

CHEMIN = "/Game/SP/SM_SP_Body"

sm = unreal.EditorAssetLibrary.load_asset(CHEMIN)
if sm is None:
    unreal.log_error("[uv] asset introuvable : {} (lancer make_body_sphere.py)".format(CHEMIN))
    raise SystemExit

dm = unreal.DynamicMesh()
opts = unreal.GeometryScriptCopyMeshFromAssetOptions()
lod = unreal.GeometryScriptMeshReadLOD()
dm, _ = unreal.GeometryScript_AssetUtils.copy_mesh_from_static_mesh(sm, dm, opts, lod)
n_tri = dm.get_triangle_count()
unreal.log("[uv] {} : {} triangles".format(CHEMIN, n_tri))

Q = unreal.GeometryScript_MeshQueries


def valeur(r):
    """Les requetes GeometryScript rendent des formes variees : la valeur seule,
    (valeur, valide), ou (maillage_cible, valeur, valide) — les noeuds qui
    prennent un TargetMesh le RENVOIENT en premier. On prend donc la premiere
    sortie qui n'est ni le maillage ni un booleen, plutot que de parier sur un
    indice (le pari a coute deux allers-retours)."""
    if not isinstance(r, tuple):
        return r
    for v in r:
        if not isinstance(v, (unreal.DynamicMesh, bool)):
            return v
    return r[0]


# On balaie les TRIANGLES, pas les sommets : l'UV vit par COIN (le meridien 180
# est duplique sur la couture), donc c'est la seule lecture qui ne suppose rien.
# L'UV d'un coin se lit en interpolant sur la barycentrique de ce coin.
CIBLES = [
    ("-X (attendu lon 0    -> U 0,50)", (-1.0, 0.0, 0.0)),
    ("+X (attendu lon 180  -> U 0/1)",  (1.0, 0.0, 0.0)),
    ("+Y (attendu lon +90  -> U 0,75)", (0.0, 1.0, 0.0)),
    ("-Y (attendu lon -90  -> U 0,25)", (0.0, -1.0, 0.0)),
    ("+Z (attendu pole N   -> V 0,00)", (0.0, 0.0, 1.0)),
    ("-Z (attendu pole S   -> V 1,00)", (0.0, 0.0, -1.0)),
]
BARY = (unreal.Vector(1, 0, 0), unreal.Vector(0, 1, 0), unreal.Vector(0, 0, 1))
meilleurs = [(-2.0, None, None, None) for _ in CIBLES]   # (cos, pos, tri, coin)

pos = {}
for t in range(n_tri):
    ids = valeur(Q.get_triangle_indices(dm, t))
    for coin, vid in enumerate((int(ids.x), int(ids.y), int(ids.z))):
        p = pos.get(vid)
        if p is None:
            v = valeur(Q.get_vertex_position(dm, vid))
            n = (v.x * v.x + v.y * v.y + v.z * v.z) ** 0.5
            if n <= 0.0:
                continue
            p = (v.x, v.y, v.z, v.x / n, v.y / n, v.z / n)
            pos[vid] = p
        for k, (_, d) in enumerate(CIBLES):
            s = p[3] * d[0] + p[4] * d[1] + p[5] * d[2]
            if s > meilleurs[k][0]:
                meilleurs[k] = (s, p[:3], t, coin)

for (nom, _), (s, p, tri, coin) in zip(CIBLES, meilleurs):
    if p is None:
        unreal.log_error("[uv] {} : aucun sommet trouve".format(nom))
        continue
    uv = valeur(Q.get_interpolated_triangle_uv(dm, 0, tri, BARY[coin]))
    lon = (uv.x - 0.5) * 360.0
    lat = 90.0 - uv.y * 180.0
    unreal.log("[uv] {:34s} : sommet ({:+7.2f},{:+7.2f},{:+7.2f}) cos={:.4f} "
               "-> UV=({:.4f},{:.4f}) = lon {:+8.2f} lat {:+7.2f}".format(
                   nom, p[0], p[1], p[2], s, uv.x, uv.y, lon, lat))
unreal.log("[uv] TERMINE")
