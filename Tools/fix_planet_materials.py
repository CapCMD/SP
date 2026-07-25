# Tools/fix_planet_materials.py — remet les materiaux des corps en OPAQUE.
#
# A lancer editeur FERME :
#   UnrealEditor-Cmd.exe SP.uproject -run=pythonscript -script="Tools/fix_planet_materials.py"
#
# POURQUOI : l'import Interchange des GLB rapporte des materiaux en fusion
# TRANSLUCIDE. A l'echelle vraie et en gros plan, on voyait donc l'INTERIEUR de
# la sphere a travers sa face avant : Jupiter ressemblait a une bille de verre
# avec des copies decalees de ses bandes, la Terre montrait sa face nuit sous
# un bandeau de jour. Chaque corps n'a pourtant qu'UNE maille et UN materiau
# (verifie dans Content/SolarSystem/<Corps>/<Corps>/Materials).
#
# Ce script diagnostique d'abord (il ecrit ce qu'il trouve), puis corrige :
# fusion OPAQUE, une seule face. Il est reexecutable sans dommage.
import unreal

RACINE = "/Game/SolarSystem"


def corriger_materiau(actif, chemin):
    """Materiau de base : les proprietes sont directes."""
    fusion = actif.get_editor_property("blend_mode")
    deux_faces = actif.get_editor_property("two_sided")
    unreal.log("[corps] MAT {} : fusion={} deux_faces={}".format(chemin, fusion, deux_faces))
    if fusion == unreal.BlendMode.BLEND_OPAQUE and not deux_faces:
        return False
    actif.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    actif.set_editor_property("two_sided", False)
    unreal.MaterialEditingLibrary.recompile_material(actif)
    return True


def corriger_instance(actif, chemin):
    """Instance de materiau : la fusion passe par les surcharges de base.

    N'ECRIT QUE SI C'EST NECESSAIRE : reecrire une instance deja conforme
    salit le depot de 30 .uasset pour rien (constate le 2026-07-24).
    """
    surcharges = actif.get_editor_property("base_property_overrides")
    fusion = surcharges.get_editor_property("blend_mode")
    deux_faces = surcharges.get_editor_property("two_sided")
    unreal.log("[corps] INST {} : fusion={} deux_faces={}".format(chemin, fusion, deux_faces))
    if fusion == unreal.BlendMode.BLEND_OPAQUE and not deux_faces:
        return False
    surcharges.set_editor_property("override_blend_mode", True)
    surcharges.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    surcharges.set_editor_property("override_two_sided", True)
    surcharges.set_editor_property("two_sided", False)
    actif.set_editor_property("base_property_overrides", surcharges)
    return True


def main():
    registre = unreal.AssetRegistryHelpers.get_asset_registry()
    # En commandlet, le registre n'a pas forcement balaye le dossier.
    registre.scan_paths_synchronous([RACINE], True)
    actifs = registre.get_assets_by_path(RACINE, recursive=True)
    n_vus, n_corriges = 0, 0

    for donnees in actifs:
        actif = donnees.get_asset()
        # L'import Interchange produit des INSTANCES, pas des materiaux de base :
        # c'est ce qui faisait echouer la premiere version de ce script.
        if not isinstance(actif, unreal.MaterialInterface):
            continue
        n_vus += 1
        chemin = str(donnees.package_name)
        if isinstance(actif, unreal.Material):
            change = corriger_materiau(actif, chemin)
        elif isinstance(actif, unreal.MaterialInstanceConstant):
            change = corriger_instance(actif, chemin)
        else:
            continue
        if change:
            unreal.EditorAssetLibrary.save_asset(chemin)
            n_corriges += 1
            unreal.log("[corps] {} -> OPAQUE, une face".format(chemin))

    unreal.log("[corps] {} materiaux inspectes, {} corriges".format(n_vus, n_corriges))


main()
