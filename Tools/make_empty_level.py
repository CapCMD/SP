# Tools/make_empty_level.py — cree le NIVEAU VIDE dedie du projet.
#
# A lancer editeur FERME :
#   UnrealEditor-Cmd.exe SP.uproject -run=pythonscript -script="Tools/make_empty_level.py"
#
# Produit : /Game/Maps/SP_Empty  (niveau VIDE).
#
# Pourquoi : le projet n'a AUCUN .umap a lui. Tout le jeu est bati PAR CODE
# (les WorldSubsystems SP spawnent la scene au BeginPlay). En -game, UE charge
# GameDefaultMap (/Engine/Maps/Entry) et tout va bien ; mais en PIE, UE lance le
# niveau OUVERT dans l'editeur — faute de niveau projet, c'est un template
# (landscape + defaut), et les subsystems se batissent PAR-DESSUS ce decor.
# On donne donc au projet un niveau vide, mis en niveau de demarrage (voir
# Config/DefaultEngine.ini + DefaultEditorPerProjectUserSettings.ini).
import unreal

LEVEL_PATH = "/Game/Maps/SP_Empty"

les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
ok = les.new_level(LEVEL_PATH)          # niveau VIDE (aucun acteur par defaut)
unreal.log("[make_empty_level] new_level({}) -> {}".format(LEVEL_PATH, ok))

# Sauver le paquet du niveau sur disque.
saved = unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
unreal.log("[make_empty_level] save_dirty_packages -> {}".format(saved))

# Verification : l'asset existe-t-il ?
exists = unreal.EditorAssetLibrary.does_asset_exist(LEVEL_PATH)
unreal.log("[make_empty_level] asset existe = {}".format(exists))
if not exists:
    unreal.log_error("[make_empty_level] ECHEC : le niveau n'a pas ete cree")
