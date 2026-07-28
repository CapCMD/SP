#!/usr/bin/env python3
# Tools/stage_sdk.py — EMBARQUER LE SDK DU JOUEUR DANS LA DISTRIBUTION [GDD 18]
#
# « Embarquer compilateur, en-têtes et outils associés. » [GDD 18]
#
# Le mode PRO compile du VRAI C++ contre l'API ARES. En développement, la chaîne
# compile contre les en-têtes du DÉPÔT (`Source/SP/SpaceProgram/...`) — un chemin
# qui **n'existe pas dans un build packagé**. Sans ce staging, l'atelier logiciel
# marche sur la machine de développement et nulle part ailleurs.
#
# ═══ LA SOURCE DE VÉRITÉ NE BOUGE PAS ═══
# `Content/SP/Sdk/` est un ARTEFACT DE BUILD, pas un second exemplaire du moteur :
# il est régénéré par cet outil et ignoré par git. Dupliquer `fen/astro/Kepler.hpp`
# dans le dépôt en ferait deux vérités, dont une périmerait en silence.
#
# ═══ CE QU'ON EMBARQUE, ET POURQUOI SI PEU ═══
# `ares/vol.hpp` ne tire que `fen/astro/Stm.hpp`, `fen/core/*` ; `ares/sol.hpp` y
# ajoute Lambert, l'éphéméride et le catalogue de pièces. Tout cela vit sous
# `astro_core/include` — 396 Ko, 74 en-têtes. On copie l'arbre entier plutôt
# qu'une liste triée à la main : une liste se périme au premier `#include` ajouté,
# et 396 Ko ne valent pas ce risque.
# S'y ajoute `Kepler.cpp` : depuis que `ares::vol::Solveur` résout la correction
# sur la vraie matrice de transition (piège n°72), le programme du joueur doit
# LIER le propagateur du moteur — le même, pas une copie.
#
# ═══ NON-UFS, ET C'EST LE POINT ═══
# Ces fichiers sont lus par `cl.exe`, pas par Unreal. Ils doivent donc exister
# comme de VRAIS fichiers sur le disque du joueur, jamais empaquetés dans un .pak
# — d'où `DirectoriesToAlwaysStageAsNonUFS` dans `Config/DefaultGame.ini`.
#
# Usage : python Tools/stage_sdk.py [racine_du_projet]
#         (le python d'UE fait l'affaire ; aucune dépendance externe)

import os
import shutil
import sys

SDK_REL = os.path.join("Content", "SP", "Sdk")


def stage(racine):
    src_inc = os.path.join(racine, "Source", "SP", "SpaceProgram",
                           "astro_core", "include")
    src_kepler = os.path.join(racine, "Source", "SP", "SpaceProgram",
                              "astro_core", "src", "Kepler.cpp")
    if not os.path.isdir(src_inc):
        print("ECHEC : arbre d'en-tetes introuvable : %s" % src_inc)
        return 1
    if not os.path.isfile(src_kepler):
        print("ECHEC : Kepler.cpp introuvable : %s" % src_kepler)
        return 1

    dst = os.path.join(racine, SDK_REL)
    dst_inc = os.path.join(dst, "include")
    dst_src = os.path.join(dst, "src")

    # ON REGENERE, ON NE FUSIONNE PAS : un en-tete supprime du moteur doit
    # DISPARAITRE du SDK, sinon le joueur compile contre un fantome.
    if os.path.isdir(dst):
        shutil.rmtree(dst)
    os.makedirs(dst_src)
    shutil.copytree(src_inc, dst_inc)
    shutil.copy2(src_kepler, os.path.join(dst_src, "Kepler.cpp"))

    n, octets = 0, 0
    for base, _, fichiers in os.walk(dst):
        for f in fichiers:
            n += 1
            octets += os.path.getsize(os.path.join(base, f))

    with open(os.path.join(dst, "LISEZMOI.txt"), "w", encoding="utf-8") as f:
        f.write(
            "SDK ARES - en-tetes de l'API joueur (mode PRO) [GDD 15.2, 15.3, 18]\n"
            "\n"
            "Genere par Tools/stage_sdk.py. NE PAS EDITER : la source de verite est\n"
            "Source/SP/SpaceProgram/astro_core/. Toute modification ici est perdue\n"
            "au prochain staging.\n"
            "\n"
            "include/  les en-tetes contre lesquels le code du joueur compile\n"
            "src/      les unites de traduction qu'il doit lier (Kepler.cpp)\n"
            "\n"
            "LE COMPILATEUR n'est pas ici : voir Toolchain/LISEZMOI.txt.\n")

    print("SDK stage dans %s" % dst)
    print("  %d fichiers, %.1f Ko" % (n, octets / 1024.0))
    return 0


def main():
    racine = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(
        os.path.dirname(os.path.abspath(__file__)))
    return stage(racine)


if __name__ == "__main__":
    sys.exit(main())
