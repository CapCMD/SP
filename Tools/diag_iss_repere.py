# Tools/diag_iss_repere.py — OU SONT LE NADIR ET L'AVANT DANS LE MODELE ISS ?
#
#   UnrealEditor-Cmd.exe SP.uproject -run=pythonscript -script="<ABSOLU>/diag_iss_repere.py"
#   (sortie dans Saved/Logs/SP.log, PAS dans la console)
#
# POURQUOI CE DIAG. Pour poser l'attitude de Novellus (cupola vers la Terre), il
# faut savoir quel axe du MODELE est le nadir. Le deviner, c'est se donner une
# chance sur six d'avoir raison et aucune de s'en apercevoir. Le modele porte la
# reponse : il a un mesh nomme `Cupola`, et la cupola de l'ISS est sur le port
# NADIR de Node 3. La direction centre_du_modele -> centre_de_la_cupola est donc
# le nadir, a la position de Node 3 pres — on la PROJETTE donc sur les axes de la
# boite pour trancher lequel c'est, et dans quel sens.
#
# Ce que le script imprime, et qui est tout ce dont le C++ a besoin :
#   . la boite englobante du modele complet (envergure, et donc quel axe porte la
#     poutre de 109 m et lequel porte les modules) ;
#   . le centre de `Cupola` et de quelques reperes nommes (Node3, les radiateurs,
#     les panneaux) dans le repere du modele ;
#   . la composante de chacun sur les trois axes -> le nadir signe.
import unreal

DOSSIER = "/Game/ISS/Exterior/ISS_stationary/StaticMeshes"
# Les meshes qui portent une information de DIRECTION connue dans le vrai ISS.
REPERES = ["Cupola", "Node3", "Node1", "Node2", "Zvezda", "Zarya",
           "Radiator", "SolarArray", "Airlock", "Columbus", "Kibo", "Destiny"]

ar = unreal.AssetRegistryHelpers.get_asset_registry()
ar.scan_paths_synchronous([DOSSIER], True)
assets = ar.get_assets_by_path(DOSSIER, recursive=True)

boites = {}          # nom -> (centre, demi-taille)
mini = [1e30] * 3
maxi = [-1e30] * 3
for a in assets:
    obj = a.get_asset()
    if not isinstance(obj, unreal.StaticMesh):
        continue
    b = obj.get_bounds()
    o, e = b.origin, b.box_extent
    boites[str(a.asset_name)] = ((o.x, o.y, o.z), (e.x, e.y, e.z))
    for k, (c, d) in enumerate(zip((o.x, o.y, o.z), (e.x, e.y, e.z))):
        mini[k] = min(mini[k], c - d)
        maxi[k] = max(maxi[k], c + d)

if not boites:
    unreal.log_error("[repere] aucun StaticMesh sous {}".format(DOSSIER))
    raise SystemExit

centre = [(mini[k] + maxi[k]) * 0.5 for k in range(3)]
taille = [maxi[k] - mini[k] for k in range(3)]
unreal.log("[repere] {} meshes".format(len(boites)))
unreal.log("[repere] boite : x[{:.0f},{:.0f}] y[{:.0f},{:.0f}] z[{:.0f},{:.0f}]".format(
    mini[0], maxi[0], mini[1], maxi[1], mini[2], maxi[2]))
unreal.log("[repere] centre = ({:.0f},{:.0f},{:.0f}) | taille = ({:.1f},{:.1f},{:.1f}) m".format(
    centre[0], centre[1], centre[2],
    taille[0] / 100.0, taille[1] / 100.0, taille[2] / 100.0))
ordre = sorted(range(3), key=lambda k: -taille[k])
noms = "xyz"
unreal.log("[repere] axes par etendue DECROISSANTE : {} ({:.1f} m) > {} ({:.1f} m) > {} ({:.1f} m)".format(
    noms[ordre[0]], taille[ordre[0]] / 100.0,
    noms[ordre[1]], taille[ordre[1]] / 100.0,
    noms[ordre[2]], taille[ordre[2]] / 100.0))
unreal.log("[repere] LECTURE : le plus long = la POUTRE (~109 m, axe babord-tribord),")
unreal.log("[repere]           le second   = la PILE DE MODULES (~74 m, axe de vol),")
unreal.log("[repere]           le plus court = l'axe NADIR-ZENITH.")

# --- les reperes nommes, en metres relativement au centre du modele -----------
for motif in REPERES:
    trouves = [(n, v) for n, v in boites.items() if n.lower().startswith(motif.lower())]
    if not trouves:
        unreal.log("[repere] {:12s} : absent".format(motif))
        continue
    # centre du groupe, pondere par aucun poids (on veut une DIRECTION, pas une masse)
    n_exact = None
    for n, v in trouves:
        if n.lower() == motif.lower():
            n_exact = (n, v)
            break
    n, (o, e) = n_exact if n_exact else trouves[0]
    rel = [(o[k] - centre[k]) / 100.0 for k in range(3)]
    unreal.log("[repere] {:12s} -> {:34s} rel_centre = ({:+7.2f},{:+7.2f},{:+7.2f}) m".format(
        motif, n, rel[0], rel[1], rel[2]))

unreal.log("[repere] TERMINE")
