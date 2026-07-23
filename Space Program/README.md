# Space Program — matériel de migration UE5 (tri du 2026-07-23)

> L'ancien moteur custom (GLFW/OpenGL/Vulkan) est **abandonné**. Le jeu vit
> désormais dans le projet UE 5.8 (`Source/SP`). Ce dossier ne contient plus que
> ce que la migration UE5 **nécessite encore**. Tout le reste est dans
> `_archive/tri_ue5_20260723/` (déplacé, rien n'est supprimé).

## La cible de migration : `solar_system_map`

Le jeu de référence à porter est **`render/app/solar_system_map.cpp`**
(carte 3D du système solaire : orbites, contrôle du temps, planètes GLB,
intérieur ISS, panneaux branchés sur le modèle `fen::app::Jeu`).
`fenetre_jeu` (2D) est déjà porté (pont Slate `Source/SP/UEBridge`) ;
`space_program_3d` (démo RenderCore) est supplanté → archivés.

## Ce qui reste ici, et pourquoi

| Dossier | Rôle pour UE5 |
| :--- | :--- |
| `render/` | **Référence du portage 3D** : `solar_system_map.cpp` (gameplay carte), `include/spr` + `src/` (RenderCore, DataBridge, RenderSnapshot, MapView, StationView, MenuView, RenderScene, GlbTexture, Camera), `shaders/` (GLSL planet/ring/star/shell → à traduire en materials UE) |
| `assets/` | **À importer dans le Content UE** : GLB planètes/lunes/ISS (dont `ISS_Internal.glb` 273 Mo), textures 8k Terre, Soleil |
| `missions/` | Fichiers de mission de référence (`m00_geo.fpl`, solutions, lua) |
| `docs/` | Docs de conception, dont `RENDER_ARCHITECTURE.md` (architecture du render à transposer) |
| `tests/` | **Les 102 oracles physiques** (`test_astro_core.cpp`). Ils testent le cœur VIVANT (`Source/SP/SpaceProgram`), pas une copie locale |

## Lancer les oracles contre le cœur vivant

Le CMake historique est archivé (il buildait l'ancien moteur). Les tests se
compilent directement contre `Source/SP/SpaceProgram` :

```
cl /std:c++20 /EHsc /W4 /fp:precise ^
   /I "..\Source\SP\SpaceProgram\astro_core\include" ^
   /I "..\Source\SP\SpaceProgram\mission\include" /I tests ^
   tests\test_astro_core.cpp "..\Source\SP\SpaceProgram\astro_core\src\*.cpp" ^
   /Fe:test_astro_core.exe
```

(Les oracles de la couche ARES sont dans
`Source/SP/SpaceProgram/tests/test_ares_modules.cpp`, macro `SP_STANDALONE_TESTS`.)

## Contenu de l'archive

`_archive/tri_ue5_20260723/` : `app/`, `ui/`, `astro_core/`, `mission/`
(migrés verbatim dans `Source/SP/SpaceProgram` — les copies vivantes sont
LÀ-BAS), `extern/` (GLFW/ImGui/ImPlot — UE utilise `Source/SP/ThirdParty`),
`scripts/` (outils de conception m00/t01), `build/`, `build_vk/`, `dist/`
(artefacts), `_backup_render/`, `space_program_3d.cpp`, `CMakeLists.txt`,
ancien `README.md`. Supprimable quand la migration 3D sera terminée.
