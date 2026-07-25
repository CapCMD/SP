# Tools/import_iss.py — importe les modèles ISS dans /Game/ISS.
#
# L'ISS est le QG du joueur : l'INTÉRIEUR (ISS_Internal.glb, ~273 Mo) est la
# scène d'accueil du jeu (on y est accueilli, première personne), l'EXTÉRIEUR
# (ISS_stationary.glb, ~242 Mo) est le modèle vu depuis la carte.
# Volontairement séparé de import_glb_planets.py : ces deux fichiers sont lourds
# et n'ont pas à être réimportés à chaque passe sur les planètes.
#
# En ligne de commande (éditeur FERMÉ) :
#   UnrealEditor-Cmd.exe SP.uproject -run=pythonscript -script="Tools/import_iss.py"
import os
import unreal

SRC = r"C:\Users\Cap\Documents\Unreal Projects\SP\Space Program\assets\3D models\ISS"
DEST_ROOT = "/Game/ISS"

# fichier -> sous-dossier de destination
MODELS = {
    "ISS_Internal.glb": "Interior",
    "ISS_stationary.glb": "Exterior",
}


def import_glb(path: str, dest: str) -> None:
    task = unreal.AssetImportTask()
    task.filename = path
    task.destination_path = dest
    task.automated = True
    task.save = True
    task.replace_existing = True
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    unreal.log(f"[ISS] importe : {path} -> {dest}")


def main() -> None:
    if not os.path.isdir(SRC):
        unreal.log_error(f"[ISS] dossier introuvable : {SRC}")
        return
    for fichier, sous_dossier in MODELS.items():
        chemin = os.path.join(SRC, fichier)
        if not os.path.isfile(chemin):
            unreal.log_error(f"[ISS] absent : {chemin}")
            continue
        mo = os.path.getsize(chemin) / (1024 * 1024)
        unreal.log(f"[ISS] import de {fichier} ({mo:.0f} Mo) — patience...")
        import_glb(chemin, f"{DEST_ROOT}/{sous_dossier}")
    unreal.log("[ISS] termine.")


main()
