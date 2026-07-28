# Tools/diag_iss_modules.py — QUELS MODULES LE MODELE INTERIEUR CONTIENT-IL,
# ET OU SONT-ILS ?
#
#   UnrealEditor-Cmd.exe SP.uproject -run=pythonscript -script="<ABSOLU>/diag_iss_modules.py"
#   (sortie dans Saved/Logs/SP.log, PAS dans la console)
#
# POURQUOI CE DIAG. Les 8 postes de travail [GDD 11] NOMMENT chacun leur module
# (« COUPOLE — TRANQUILITY . OBSERVATION », « AGENCE — ZVEZDA . DIRECTION »...)
# mais `app/postes.hpp` les aligne tous le long du couloir, a 1,7 m d'intervalle,
# a partir du point d'apparition : la station est un decor, pas un lieu. Pour les
# poser DANS leur module, il faut savoir ou chaque module se trouve — et le
# modele est un import glTF de 310 meshes dont les noms ne suivent pas les noms
# du GDD (un premier essai a rendu « Destiny : absent » et « Zvezda : absent »,
# alors que les modules y sont : ils portent leurs sigles de programme).
#
# Le script imprime donc TOUT l'inventaire, groupe par prefixe de nom, avec la
# boite englobante de chaque groupe dans le repere STATION (metres) — le meme que
# NOVELLUS_OEIL_M. Rien n'est devine : on lit ce que l'asset contient.
import unreal

DOSSIER = "/Game/ISS/Interior/ISS_Internal/StaticMeshes"
ENVERGURE_M = 55.0          # app/postes.hpp : STATION_ENVERGURE_M

# Sigles connus des modules reels -> nom du GDD. Sert UNIQUEMENT a annoter la
# sortie : l'inventaire complet est imprime de toute facon.
SIGLES = {
    "zvezda": "ZVEZDA (module de service)", "sm": "ZVEZDA ?",
    "zarya": "ZARYA (FGB)", "fgb": "ZARYA (FGB)",
    "destiny": "DESTINY (US Lab)", "uslab": "DESTINY (US Lab)", "lab": "DESTINY ?",
    "node1": "UNITY (Node 1)", "unity": "UNITY (Node 1)",
    "node2": "HARMONY (Node 2)", "harmony": "HARMONY (Node 2)",
    "node3": "TRANQUILITY (Node 3)", "tranquility": "TRANQUILITY (Node 3)",
    "cupola": "CUPOLA (observation)",
    "columbus": "COLUMBUS (ESA)", "col": "COLUMBUS (ESA)",
    "kibo": "KIBO (JAXA)", "jem": "KIBO (JEM/JAXA)", "jpm": "KIBO (JEM-PM)",
    "airlock": "QUEST (sas)", "quest": "QUEST (sas)",
    "beam": "BEAM (module gonflable)", "bishop": "BISHOP (sas)",
    "leonardo": "LEONARDO (PMM)", "pmm": "LEONARDO (PMM)",
    "mlm": "NAUKA (MLM)", "nauka": "NAUKA (MLM)", "rassvet": "RASSVET (MRM1)",
    "poisk": "POISK (MRM2)", "pirs": "PIRS",
}

ar = unreal.AssetRegistryHelpers.get_asset_registry()
ar.scan_paths_synchronous([DOSSIER], True)
assets = ar.get_assets_by_path(DOSSIER, recursive=True)

meshes = {}
mini = [1e30] * 3
maxi = [-1e30] * 3
for a in assets:
    obj = a.get_asset()
    if not isinstance(obj, unreal.StaticMesh):
        continue
    b = obj.get_bounds()
    o, e = b.origin, b.box_extent
    meshes[str(a.asset_name)] = ((o.x, o.y, o.z), (e.x, e.y, e.z))
    for k, (c, d) in enumerate(zip((o.x, o.y, o.z), (e.x, e.y, e.z))):
        mini[k] = min(mini[k], c - d)
        maxi[k] = max(maxi[k], c + d)

if not meshes:
    unreal.log_error("[modules] aucun StaticMesh sous {}".format(DOSSIER))
    raise SystemExit

centre = [(mini[k] + maxi[k]) * 0.5 for k in range(3)]
echelle = ENVERGURE_M * 100.0 / max(maxi[k] - mini[k] for k in range(3))


def station_bb(noms):
    """Boite englobante du groupe, en METRES repere STATION (miroir y)."""
    lo = [1e30] * 3
    hi = [-1e30] * 3
    for n in noms:
        o, e = meshes[n]
        for k in range(3):
            lo[k] = min(lo[k], (o[k] - e[k] - centre[k]) * echelle / 100.0)
            hi[k] = max(hi[k], (o[k] + e[k] - centre[k]) * echelle / 100.0)
    # miroir en y : le repere station est droitier (cf. StationToWorld)
    c = [(lo[0] + hi[0]) * 0.5, -(lo[1] + hi[1]) * 0.5, (lo[2] + hi[2]) * 0.5]
    t = [hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]]
    return c, t


# --- groupement par PREFIXE (avant le premier '_') ---------------------------
groupes = {}
for n in meshes:
    pref = n.split("_")[0]
    groupes.setdefault(pref, []).append(n)

unreal.log("[modules] {} meshes, {} prefixes, echelle x{:.6f}".format(
    len(meshes), len(groupes), echelle))
unreal.log("[modules] --- inventaire, trie par X du repere station (l'axe du couloir) ---")

def station_pt(nom):
    o, _ = meshes[nom]
    ue = [(o[k] - centre[k]) * echelle for k in range(3)]
    return (ue[0] / 100.0, -ue[1] / 100.0, ue[2] / 100.0)


def mediane(vals):
    v = sorted(vals)
    n = len(v)
    return v[n // 2] if n % 2 else 0.5 * (v[n // 2 - 1] + v[n // 2])


lignes = []
for pref, noms in groupes.items():
    c, t = station_bb(noms)
    # CENTRE ROBUSTE : la mediane des centres de mesh, pas le centre de la boite.
    # La boite est faussee par un seul intrus — `Cupola_Int_Glass` est a 16 m des
    # six autres pieces de la cupola et etirait sa « taille » a 19,6 m en y. Une
    # mediane ne bouge pas pour un intrus sur onze.
    pts = [station_pt(n) for n in noms]
    med = [mediane([p[k] for p in pts]) for k in range(3)]
    lignes.append((c[0], pref, len(noms), c, t, med))
lignes.sort()
for x, pref, n, c, t, med in lignes:
    note = SIGLES.get(pref.lower(), "")
    unreal.log("[modules] {:22s} n={:3d} bbox=({:+7.2f},{:+7.2f},{:+7.2f}) "
               "taille=({:5.1f},{:5.1f},{:5.1f}) | MEDIANE=({:+7.2f},{:+7.2f},{:+7.2f})  {}".format(
                   pref, n, c[0], c[1], c[2], t[0], t[1], t[2],
                   med[0], med[1], med[2], note))

# --- detail des modules qui portent un poste [GDD 11] ------------------------
unreal.log("[modules] --- detail des porteurs de poste ---")
for pref in ("US", "JPM", "Columbus", "Node3", "Cupola"):
    noms = sorted(groupes.get(pref, []))
    for nm in noms[:14]:
        p = station_pt(nm)
        _, e = meshes[nm]
        unreal.log("[modules]   {:44s} ({:+7.2f},{:+7.2f},{:+7.2f}) demi=({:.2f},{:.2f},{:.2f})".format(
            nm, p[0], p[1], p[2],
            e[0] * echelle / 100.0, e[1] * echelle / 100.0, e[2] * echelle / 100.0))

unreal.log("[modules] TERMINE")
