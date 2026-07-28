# Tools/diag_iss_cupola.py — OU EST LA CUPOLA DANS LE MODELE INTERIEUR, ET
# EST-ELLE TRANSPARENTE ?
#
#   UnrealEditor-Cmd.exe SP.uproject -run=pythonscript -script="<ABSOLU>/diag_iss_cupola.py"
#   (sortie dans Saved/Logs/SP.log, PAS dans la console)
#
# POURQUOI CE DIAG. Faire rendre le monde A BORD n'a d'interet que si l'on peut
# VOIR dehors. Le modele interieur porte un mesh `Cupola_Int_Glass` et sept
# `Cupola_WC_*` (window covers, les volets de la cupola) : trois questions, qu'on
# ne peut pas deviner —
#   . OU est la cupola dans le repere station (celui ou marche le pawn) ?
#   . le VERRE est-il translucide, ou opaque comme le reste du modele ?
#   . les VOLETS sont-ils modelises ouverts ou fermes (une cupola aveugle) ?
#
# Le repere est celui de SPStation::BuildScene, reproduit ici a l'identique :
# boite englobante de TOUS les meshes, centre ramene a l'origine, echelle telle
# que la plus grande dimension fasse STATION_ENVERGURE_M (55 m). Le repere
# station (metres, droitier) s'en deduit par le miroir en y du projet.
import unreal

DOSSIER = "/Game/ISS/Interior/ISS_Internal/StaticMeshes"
ENVERGURE_M = 55.0          # app/postes.hpp : STATION_ENVERGURE_M
MOTIFS = ["Cupola", "Node3", "Destiny", "Zvezda", "Node1", "Node2", "Columbus"]

ar = unreal.AssetRegistryHelpers.get_asset_registry()
ar.scan_paths_synchronous([DOSSIER], True)
assets = ar.get_assets_by_path(DOSSIER, recursive=True)

boites = {}
mini = [1e30] * 3
maxi = [-1e30] * 3
for a in assets:
    obj = a.get_asset()
    if not isinstance(obj, unreal.StaticMesh):
        continue
    b = obj.get_bounds()
    o, e = b.origin, b.box_extent
    boites[str(a.asset_name)] = (obj, (o.x, o.y, o.z), (e.x, e.y, e.z))
    for k, (c, d) in enumerate(zip((o.x, o.y, o.z), (e.x, e.y, e.z))):
        mini[k] = min(mini[k], c - d)
        maxi[k] = max(maxi[k], c + d)

if not boites:
    unreal.log_error("[cupola] aucun StaticMesh sous {}".format(DOSSIER))
    raise SystemExit

centre = [(mini[k] + maxi[k]) * 0.5 for k in range(3)]
taille = [maxi[k] - mini[k] for k in range(3)]
span = max(taille)
echelle = ENVERGURE_M * 100.0 / span if span > 1.0 else 1.0
unreal.log("[cupola] {} meshes | span {:.0f} u -> x{:.6f} (55 m)".format(
    len(boites), span, echelle))
unreal.log("[cupola] taille modele = ({:.1f},{:.1f},{:.1f}) m apres echelle".format(
    taille[0] * echelle / 100.0, taille[1] * echelle / 100.0, taille[2] * echelle / 100.0))


def pose_station(nom):
    """(x,y,z) en METRES dans le repere STATION (droitier, miroir y), comme
    NOVELLUS_OEIL_M — c'est le repere que lisent postes.hpp et SPStation."""
    _, o, _ = boites[nom]
    ue = [(o[k] - centre[k]) * echelle for k in range(3)]     # monde UE, cm
    return (ue[0] / 100.0, -ue[1] / 100.0, ue[2] / 100.0)


# --- 1. OU SONT LES REPERES NOMMES -------------------------------------------
for motif in MOTIFS:
    trouves = sorted(n for n in boites if n.lower().startswith(motif.lower()))
    if not trouves:
        unreal.log("[cupola] {:12s} : absent".format(motif))
        continue
    for n in trouves[:8]:
        x, y, z = pose_station(n)
        _, _, e = boites[n]
        unreal.log("[cupola] {:34s} station = ({:+7.2f},{:+7.2f},{:+7.2f}) m | "
                   "demi-taille = ({:.2f},{:.2f},{:.2f}) m".format(
                       n, x, y, z,
                       e[0] * echelle / 100.0, e[1] * echelle / 100.0, e[2] * echelle / 100.0))

# --- 2. LE VERRE EST-IL TRANSLUCIDE ? ----------------------------------------
# Un mesh opaque ne laisse rien passer, quelle que soit la geometrie : c'est la
# question qui decide si « voir la Terre par la cupola » est faisable tel quel.
for nom in sorted(n for n in boites if "cupola" in n.lower()):
    mesh = boites[nom][0]
    mats = mesh.get_editor_property("static_materials")
    for sm in mats:
        mi = sm.material_interface
        if mi is None:
            unreal.log("[cupola] {:34s} slot {:20s} -> AUCUN materiau".format(
                nom, str(sm.material_slot_name)))
            continue
        base = mi.get_base_material() if hasattr(mi, "get_base_material") else mi
        try:
            blend = base.get_editor_property("blend_mode")
            shading = base.get_editor_property("shading_model")
            opacite = base.get_editor_property("two_sided")
        except Exception as ex:                      # materiau non editable ici
            blend, shading, opacite = "?", "?", str(ex)[:40]
        unreal.log("[cupola] {:34s} slot {:20s} -> {} | blend={} shading={} 2sided={}".format(
            nom, str(sm.material_slot_name), mi.get_name(), blend, shading, opacite))

unreal.log("[cupola] TERMINE")
