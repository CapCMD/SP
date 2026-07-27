# Tools/make_ring_mesh.py — LES ANNEAUX DE SATURNE, faits par UE.
#
#   UnrealEditor-Cmd.exe SP.uproject -run=pythonscript -script="<abs>/make_ring_mesh.py"
#   (sortie dans Saved/Logs/SP.log, PAS dans la console)
#
# Produit :
#   /Game/SP/SM_SP_Ring   anneau plat, UV RADIALES, pret pour 8k_saturn_ring_alpha
#
# POURQUOI LE FAIRE PLUTOT QUE L'IMPORTER. Les anneaux venaient du noeud « Circle »
# du GLB de Saturne et NE RENDAIENT PLUS (constate en capture, defaut anterieur au
# chantier des lunes). Comme pour les corps, on reprend la main : une couronne
# generee ici a des UV CHOISIES, et c'est tout le probleme d'une texture d'anneau.
#
# LA TEXTURE COMMANDE LES UV. `8k_saturn_ring_alpha.png` est un RUBAN de
# 8192 x 500 en RGBA : son profil radial court le long de U (bord interne a
# gauche, bord externe a droite) et son ALPHA porte les divisions (Cassini). On
# pose donc U = rayon normalise, V = 0.5. Consequence utile : U ne depend PAS de
# l'angle, donc l'anneau n'a AUCUNE couture angulaire a traiter.
#
# GEOMETRIE REELLE (rayons rapportes au rayon equatorial de Saturne, 60 268 km,
# qui vaut 100 dans le repere du mesh du corps — l'anneau est son enfant) :
#   . bord interne de l'anneau C  :  74 500 km  -> 123.6
#   . bord externe de l'anneau A  : 136 780 km  -> 226.9
# Aucune exageration : c'est la meme doctrine d'echelle vraie que le reste.
import math
import unreal

DEST = "/Game/SP"
NOM = "SM_SP_Ring"

R_SATURNE_KM = 60268.0
R_INT_KM = 74500.0
R_EXT_KM = 136780.0
RAYON_MESH_CORPS = 100.0        # convention des meshes de corps

SEGMENTS = 512                  # pas angulaire : la couronne doit rester ronde de pres
ANNEAUX = 16                    # pas radial

EDITS = unreal.GeometryScript_MeshEdits
NEW = unreal.GeometryScript_NewAssetUtils


def new_dm():
    try:
        return unreal.DynamicMesh()
    except Exception:
        return unreal.new_object(unreal.DynamicMesh)


def construire():
    r_int = R_INT_KM / R_SATURNE_KM * RAYON_MESH_CORPS
    r_ext = R_EXT_KM / R_SATURNE_KM * RAYON_MESH_CORPS

    sommets, uvs, normales, triangles = [], [], [], []
    for i in range(ANNEAUX + 1):
        t = i / float(ANNEAUX)
        r = r_int + (r_ext - r_int) * t
        for j in range(SEGMENTS):
            a = 2.0 * math.pi * j / float(SEGMENTS)
            sommets.append(unreal.Vector(r * math.cos(a), r * math.sin(a), 0.0))
            # U = rayon normalise (le profil du ruban), V au milieu de la bande.
            uvs.append(unreal.Vector2D(t, 0.5))
            normales.append(unreal.Vector(0.0, 0.0, 1.0))

    for i in range(ANNEAUX):
        for j in range(SEGMENTS):
            j2 = (j + 1) % SEGMENTS          # boucle fermee : pas de couture
            a = i * SEGMENTS + j
            b = i * SEGMENTS + j2
            c = (i + 1) * SEGMENTS + j2
            d = (i + 1) * SEGMENTS + j
            triangles.append(unreal.IntVector(a, b, c))
            triangles.append(unreal.IntVector(a, c, d))

    buffers = unreal.GeometryScriptSimpleMeshBuffers()
    buffers.set_editor_property("vertices", sommets)
    buffers.set_editor_property("triangles", triangles)
    buffers.set_editor_property("uv0", uvs)
    buffers.set_editor_property("normals", normales)

    dm = new_dm()
    dm, info = EDITS.append_buffers_to_mesh(dm, buffers)
    unreal.log("[anneaux] maillage : {} sommets, {} triangles | r_int={:.1f} r_ext={:.1f}".format(
        len(sommets), len(triangles), r_int, r_ext))
    return dm


def ecrire(dm):
    chemin = "{}/{}".format(DEST, NOM)
    if unreal.EditorAssetLibrary.does_asset_exist(chemin):
        unreal.EditorAssetLibrary.delete_asset(chemin)
    opts = unreal.GeometryScriptCreateNewStaticMeshAssetOptions()
    # NORMALES RECALCULEES, et c'est VOLONTAIRE. Sur les corps on les preserve
    # (une sphere lissee a la main), mais ici les normales posees dans les buffers
    # ne survivaient pas a l'ecriture : l'anneau ressortait NOIR sous eclairage
    # (invisible sur fond noir) alors qu'il s'affichait parfaitement en materiau
    # emissif — c'est ce qui a permis de trancher entre geometrie et ombrage.
    # Un disque plat n'a aucune ambiguite de normale : les recalculer est exact.
    opts.enable_recompute_normals = True
    opts.enable_recompute_tangents = True
    sm, outcome = NEW.create_new_static_mesh_asset_from_mesh(dm, chemin, opts)
    if sm is None:
        unreal.log_error("[anneaux] creation de l'asset ECHOUEE")
        return
    unreal.EditorAssetLibrary.save_asset(chemin)
    unreal.log("[anneaux] asset cree : {}".format(chemin))


def garantir_alpha():
    """L'ALPHA de la texture d'anneau doit survivre a l'import.

    Le materiau des anneaux est MASQUE : il lit le canal alpha pour percer les
    divisions. Si la compression le jette (DXT1), tout devient opaque et l'anneau
    ressort en disque plein. On force donc une compression AVEC alpha.
    """
    chemin = "/Game/SP/Bodies/T_SaturnRing"
    tex = unreal.EditorAssetLibrary.load_asset(chemin)
    if tex is None:
        unreal.log_error("[anneaux] texture absente : {} (lancer make_body_materials.py)".format(chemin))
        return
    tex.set_editor_property("compression_no_alpha", False)
    tex.set_editor_property("compression_settings",
                            unreal.TextureCompressionSettings.TC_DEFAULT)
    tex.set_editor_property("srgb", True)
    unreal.EditorAssetLibrary.save_asset(chemin)
    unreal.log("[anneaux] texture : alpha preserve (compression avec alpha)")


garantir_alpha()
ecrire(construire())
unreal.log("[anneaux] TERMINE")
