ARCHIVE — fichiers deplaces hors du projet actif le 17/07/2026 pour un
environnement epure. RIEN n'est supprime : tout est recuperable en le
remettant a sa place d'origine (indiquee ci-dessous).

Aucun de ces fichiers n'est reference par une cible de CMakeLists.txt :
les deplacer NE CASSE PAS le build (verifie).

frontends_v03/   -> les frontends d'avant SPACE PROGRAM v0.7 (fenetre_jeu).
  game.hpp, game_main.cpp   (origine: ui/)  = l'UI v0.3 "coquille deux champs",
                                              remplacee par ui/jeu_main.cpp +
                                              ui/jeu_ecrans.hpp.
  lua_runner.cpp, main_glfw.cpp, main_headless.cpp (origine: ui/) = anciens
                                              frontaux Lua/GLFW (deps externes).
  fenetre_lua.cpp           (origine: app/)  = la console Lua (besoin de Lua ;
                                              cf. docs/BUILD_LUA_UI.md).
  -> A restaurer seulement si tu ralumes la console Lua ou l'UI v0.3.
  -> NB : ui/panels.hpp et ui/hud.hpp sont RESTES en place : ils sont inclus
     par ui/jeu_ecrans.hpp (donc compiles dans fenetre_jeu).

artefacts_generes/ -> recrees automatiquement a l'execution/au build :
  autotest.sauvegarde.txt  = ecrit par fenetre_jeu --selftest [15].
  plan_agence.fpl          = ecrit par le jeu a chaque partie.
  imgui.ini                = layout ImGui, reecrit a chaque lancement.
  a.exe (si present)       = sortie d'un g++ ad-hoc dans build/.
  -> Ils REAPPARAITRONT a la racine des que tu relances le jeu. C'est normal.

Makefile         -> suppose un shell Unix (mkdir -p, tail, grep), inutilisable
                    sous PowerShell et entierement double par CMakeLists.txt.
                    A restaurer seulement pour un build Linux/macOS.

NON deplace (volontairement) :
  - docs/JEU_V04..06.md : historique, references par README.md.
  - assets/ : ton contenu (GLB, textures).
  - ../\_originaux_avant_unification/ : deja hors du projet (dossier parent).
