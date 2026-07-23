# Tools/import_glb_planets.py — importe les planetes GLB dans /Game/SolarSystem.
#
# A lancer DANS l'editeur UE : Tools > Execute Python Script... (ou console
# Python) — ou en ligne de commande :
#   UnrealEditor-Cmd.exe SP.uproject -run=pythonscript -script="Tools/import_glb_planets.py"
#
# Chaque corps est importe dans /Game/SolarSystem/<NomAnglais>/ ; le subsystem
# SPSolarSystem cherche d'abord /Game/SolarSystem/<Nom>.<Nom>, sinon le premier
# StaticMesh du dossier /Game/SolarSystem/<Nom>/ (via l'AssetRegistry).
# L'ISS (500+ Mo) est volontairement exclue : elle servira a StationView, a
# importer a part quand on portera l'interieur.
import os
import unreal

SRC = r"C:\Users\Cap\Documents\Unreal Projects\SP\Space Program\assets\3D models"
DEST_ROOT = "/Game/SolarSystem"

# dossiers assets (parfois en francais) -> nom canonique attendu par le C++
RENAME = {"Saturne": "Saturn", "Terre": "Earth", "Lune": "Moon", "Mercure": "Mercury"}
SKIP_DIRS = {"ISS"}

def canon(stem: str) -> str:
    return RENAME.get(stem, stem)

def import_glb(path: str, dest: str) -> None:
    task = unreal.AssetImportTask()
    task.filename = path
    task.destination_path = dest
    task.automated = True
    task.save = True
    task.replace_existing = True
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    unreal.log(f"[SolarSystem] importe : {path} -> {dest}")

def main() -> None:
    if not os.path.isdir(SRC):
        unreal.log_error(f"[SolarSystem] dossier assets introuvable : {SRC}")
        return
    n = 0
    for folder in sorted(os.listdir(SRC)):
        full = os.path.join(SRC, folder)
        if not os.path.isdir(full) or folder in SKIP_DIRS:
            continue
        for f in sorted(os.listdir(full)):
            if not f.lower().endswith(".glb"):
                continue
            body = canon(os.path.splitext(f)[0])
            import_glb(os.path.join(full, f), f"{DEST_ROOT}/{body}")
            n += 1
    unreal.log(f"[SolarSystem] {n} GLB importes sous {DEST_ROOT}. "
               "Relancer PIE : les spheres de repli sont remplacees.")

main()
