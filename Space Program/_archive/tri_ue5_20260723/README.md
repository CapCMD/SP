# FENÊTRE

Jeu d'architecte de mission spatiale. **Le joueur conçoit, le monde propage, la physique tranche.**

```bash
make                # noyau + CLI + tests + scripts   (C++20, aucune dépendance)
make test           # 102 oracles physiques
make m00            # la mission tutoriel, de bout en bout
```

## Démarrage

```bash
./build/fenetre design missions/m00_geo.fpl            # plan impulsionnel naïf  -> ÉCHOUE
./build/m00_iterate missions/m00_geo.fpl               # la boucle de convergence du joueur
./build/fenetre design missions/m00_geo_solution.fpl   # plan convergé          -> RÉUSSIT
./build/fenetre run    missions/m00_geo_solution.fpl --seed 4071   # COMMIT     -> ÉCHOUE (Gates)
./build/fenetre mc     missions/m00_geo_solution.fpl --n 400       # P(succès) = 5,2 %
./build/m00_ops        missions/m00_geo_solution.fpl 500           # boucle fermée : 100 %
./build/m00_nav        missions/m00_geo_solution.fpl 80            # l'économie de la NAVIGATION
./build/m00_program                                                # LE PROGRAMME : argent, délai, risque
./build/m00_postmortem 50                                          # LE POST-MORTEM par ablation
./build/m01_porkchop                                               # porkchop Terre-Mars + oracles
./build/m01_economy                                                # le SYSTÈME SIGNATURE
./build/m01_corridor   80                                          # le CORRIDOR du plan-B
./build/t01_tour                                                   # TITAN : le transport
./build/t01_veega  900                                             # TITAN : casser le mur du C3
./build/t01_dsm                                                    # TITAN : MGA-1DSM (le Delta-v se déplace)
./build/t01_refine 4                                               # TITAN : le raffineur local (docs/OPTIMISEUR.md)
./build/t01_facture                                                # TITAN : la facture du ciel
./build/m00_design --check 2454.6 1836.5                           # la boucle de CONCEPTION (Phase 1 du jeu)
./build/m00_play 4071 5                                            # un vol complet (graine, niveau de poursuite)
```

## Le jeu Windows prêt à jouer

**`dist/fenetre_windows/fenetre_jeu.exe` — SPACE PROGRAM v0.6** : fonde ton agence
(3 modes d'aide + tutoriel guidé par Iris, directrice de vol), explore la **carte
du système solaire cliquable**, signe des contrats GEO / Mars / comète / **Titan**,
dérive tes Δv (assistant pas-à-pas + mémos ouvrables), gère installations et arbre
de recherche, COMMIT, **décolle** (compte à rebours animé) et **vole en temps réel
dans une salle de vol vivante** (vue 3D filaire, jauges, télémétrie, warp), puis
reçois un post-mortem qui te dit **clairement pourquoi** tu réussis ou rates
(critère par critère). Menu réglages (résolution, plein écran), aucune console.
Autonome (double-clic), sauvegarde intégrée. Voir `docs/JEU_V06.md` (captures
`docs/images/v06_*.png`) ; historique dans `docs/JEU_V05.md` et `docs/JEU_V04.md`.

Le même dossier contient aussi `fenetre.exe` (la coquille v0.3 : deux champs +
délégation à `m00_design.exe`/`m00_play.exe`) et les outils CLI dans `outils/`
— tout est expliqué dans `dist/fenetre_windows/LISEZMOI.txt`.

La console Lua (`app/fenetre_lua.cpp`) et les frontaux `ui/main_*.cpp`
dépendent de bibliothèques externes — voir `docs/BUILD_LUA_UI.md`. Le jeu
v0.4, lui, se construit par CMake dès que `extern/` est présent :
`ninja -C build fenetre_jeu`.

## Documents

| | |
|---|---|
| `docs/GDD_v1.md` | game design consolidé |
| `docs/ARCHITECTURE.md` | modules, classes, fichiers, résultats des oracles |
| `docs/ROADMAP.md` | plan de développement par étapes |
| `docs/NAVIGATION.md` | l'économie de la navigation (Phase 5) |
| `docs/POSTMORTEM.md` | pourquoi l'ablation est le mauvais outil pour un système bouclé |
| `docs/MISSION_M00.md` | mission tutoriel (LEO → GEO) |
| `docs/MISSION_M01.md` | Terre → Mars — **le système signature + le corridor du plan-B** |
| `docs/MISSION_TITAN.md` | mission exobiologique (contenu V1/V2) |
| `docs/JEU_V06.md` | **v0.6** : carte du système solaire, Titan, tutoriel Iris, réglages, post-mortem clair |
| `docs/JEU_V05.md` | v0.5 : salle de vol temps réel, Mars/comète, gestion, accessibilité |
| `docs/JEU_V04.md` | le jeu d'agence v0.4 : écrans, systèmes du GDD, build |
| `docs/OPTIMISEUR.md` | le raffineur local à gradient (`LocalRefine.hpp`, `t01_refine`) |
| `docs/BUILD_LUA_UI.md` | construire la console Lua et l'UI (dépendances externes) |

## Les cinq axiomes

1. Le joueur **conçoit**, il ne pilote pas.
2. La rigueur, c'est l'**erreur quantifiée** — pas la fidélité maximale.
3. **Un seul** propagateur de vérité : N-corps + poussée finie + erreurs d'exécution.
4. Le jeu **ne corrige jamais** le joueur. Il propage.
5. Toute ressource est une **grandeur physique**.
