# ARES / solar_system_map — ÉTAT COMPRESSÉ & PLAN DE TRI

> Document de reprise. Il remplace l'historique de conversation : tout ce qu'il
> faut pour continuer, et rien d'autre. Objectif final du tri : **ne garder que
> ce qui relève de la PHYSIQUE, du GDD et de solar_system_map.**

---

## 1. Le cap (à ne plus jamais reperdre)

**LE jeu = `solar_system_map`**, pas le prototype 2D ImGui. Sa structure réelle,
établie en capturant le binaire de référence (et non en lisant son source) :

1. **Menu** sobre — « SPACE PROGRAM » / « AGENCE SPATIALE - QG A BORD DE L'ISS »,
   fond étoilé, 3 boutons : NOUVELLE PARTIE / REPRENDRE / QUITTER.
2. **On est accueilli DANS l'ISS** (module Novellus), **première personne**,
   modèle GLB réel. C'est le QG. HUD : barre haute (ZQSD / souris / E / M / F5),
   « CARTE [M] », invite « [E] OUVRIR — <POSTE> ».
3. **Postes (E)** : panneau holographique translucide à liseré coloré, titre +
   sous-titre, « EN LIGNE », lignes clé→valeur, barre, « [ECHAP] FERMER ».
   8 postes : AGENCE, ANALYSE, OPERATIONS, CONTROLE, CONCEPTION, PLANIFICATION,
   COUPOLE, NOVELLUS.
4. **[M] → la carte**, façon NASA Eyes, **HUD MINIMAL** : « < SYSTEME SOLAIRE »
   en haut-gauche, « LIVE » en bas-gauche, **barre de temps en bas**
   (date | REAL RATE | heure + curseur). Aucun panneau latéral.
5. **Clic sur un corps → on y VOLE**, il remplit l'écran en 3D (Terre 8K
   jour/nuit + nuages). Jamais une vue du dessus.
6. **ISS sur la carte** → gros plan texturé + « [M] ENTRER DANS L'ISS ».

Captures de référence : `docs/reference_solar_system_map/ref_*.png`.
Captures du portage UE : `docs/reference_solar_system_map/ue_*.png`
(`ue_*_natif.png` = après le passage en rendu total UE5, 2026-07-24).

**RÈGLE DE MÉTHODE** — avant toute passe UI/3D : lancer le binaire de référence
en headless et REGARDER. Ne jamais déduire l'apparence depuis le code.

```
cd "Space Program/_archive/tri_ue5_20260723/build_vk"
.\solar_system_map.exe --assets "..\..\..\assets" --frames 150 --capture out.bmp [flags]
```
Flags : `--menuscreen 0` · `--iss` · `--iss --panel N` · `--issfocus` ·
`--focus N --dist <m>` · `--calctest` · `--calcsteps N`.

Côté UE (équivalent, ajouté au projet) :
```
UnrealEditor.exe SP.uproject -game -windowed -ResX=1280 -ResY=720 -nosplash ^
  "-spscene=iss|map|menu" "-spfocus=<Body>" "-spframes=900" ^
  "-spcapture=<chemin absolu .png>"
```
`-spfocus` = le `--focus N` de la référence (indice `fen::ephem::Body` :
3 = Terre, 5 = Mars, 6 = Jupiter). `-sppost=N` ouvre un poste d'emblée
(0 = AGENCE, 1 = ANALYSE, 2 = OPERATIONS, 4 = CONCEPTION, 5 = PLANIFICATION) —
l'équivalent du `--panel N` de la référence. `-spdist=<km>` impose la distance de
vue (sinon la distance de cadrage du corps) — pour cadrer un objet proche d'un
corps (Novellus en LEO). Prendre `-spframes=900` après tout
changement de matériau : à froid, les shaders ne sont pas prêts à 300 frames et
l'image capturée montre des matériaux de repli.

---

## 2. État du portage UE5 (vérifié par capture)

**RENDU TOTAL UE5 depuis le 2026-07-24 : ImGui et ImPlot sont SORTIS du module.**
Tout ce qui s'affiche est peint par Slate ou composé de widgets Slate ; toute
l'entrée passe par le pipeline UE (`ASPPlayerController`). Sources ImGui
conservées dans `Space Program/_archive/imgui_20260724/`.

| Élément | État |
| :--- | :--- |
| Intérieur ISS 1re personne, 310 meshes texturés, collision réelle, spawn Novellus | **rend correctement** |
| Carte : orbites réelles, Soleil, marqueurs + libellés, échelle vraie | **rend correctement** |
| Détection de proximité des postes + invite « [E] OUVRIR » | fonctionne |
| Bascule Titre → Station → [M] Carte → [M] Station | fonctionne |
| Époque réelle figée à la fondation [GDD 14.1] + persistance | fait, sous oracle |
| Flotte : éphéméride par engin [GDD 8.3] | fait, sous oracle |
| Postes ISS interactifs : le cœur C++ remonte au joueur | **fait** (`SSPPoste`) : AGENCE = arbre techno navigable (lancer recherche, passer 1 mois), PLANIFICATION = missions notifiées + mails + **ACCEPTER un contrat**, CONCEPTION = **atelier d'assemblage** (pile d'étages, Δv/masse recalculés Tsiolkovsky en temps réel), ANALYSE = fiabilité, OPERATIONS = flotte ; CONTROLE/COUPOLE/NOVELLUS en lecture seule |
| HUD carte minimal (barre de temps, « < SYSTEME SOLAIRE », « LIVE ») | **fait** (natif Slate) |
| Fond étoilé Voie lactée | **fait** (`ASPSkyActor` + `/Game/SP/M_SP_Starfield`) |
| Menu au format de la référence | **fait** (widgets Slate, fond = la carte en retrait) |
| Entrées natives (souris capturée à bord, orbite/zoom/picking sur la carte) | **fait** |
| Clic sur un corps = vol vers lui | **fait** : distance de vue lissée en LOG (`SmoothDistKm`, τ = 0,35 s) |
| Faillite (Game Over) et Réglages | **fait**, en modales natives (`SSPModal`), sous oracle |
| Terre jour/nuit + nuages, orientations IAU, 19 lunes, anneaux | **orientations IAU : FAIT et VÉRIFIÉ AU RENDU**. `ephem/BodyOrientation.hpp` (WGCCRE 2015) donne axe écliptique, obliquité, méridien origine W(t), latitude sub-solaire (oracle couronne : Terre +23.4° au solstice de juin). `SPSolarSystem::OrientationAt` le CONSOMME : incline le mesh de l'obliquité réelle + le tourne de W(t) (miroir y -> −W), repli sidéral si pas d'éléments IAU. **Preuve par capture** (build SPEditor OK 19 s ; `-spfocus` 3/7/9 → `ue_{terre,saturn,uranus}_iau_natif.png`) : **les anneaux de Saturne sont nettement INCLINÉS à 26.7°** (avant : plats dans l'écliptique), la Terre est inclinée + jour/nuit, HUD « orientation = IAU WGCCRE ». Bonus : Lune synchrone, Vénus/Uranus rétrogrades (signe de W). **QUALITÉ DU MESH — FAIT (planètes rondes)** : GLB peu denses (~960 tris → silhouette polygonale). Pipeline SANS rien inventer (on n'agit que sur géométrie/normales du GLB, textures intactes) : `Tools/subdivide_planets.py` = tessellation PN ×16 (960→15360 tris) via Geometry Script (plugin **GeometryScripting** activé dans `SP.uproject`), puis `Tools/smooth_normals_geo.py` = normales lisses par sommet (`set_per_vertex_normals`, écrit `recompute_normals=False` sinon le build les re-facette). **Terre : lisse, ronde, jour/nuit + continents** (`ue_terre_iau_natif.png`). RESTE (mineur, distinct de l'orientation ET des textures) : corps quasi SANS relief (surface Saturne, Uranus) gardent un léger banding au terminateur sous éclairage mono-source ; 19 lunes présentes dans les GLB, pas encore placées |
| Postes : contenu complet + arbre de compétences + catalogue de missions | **à faire** |
| Boucle de mission [GDD 4.1] : contrat → conception → fenêtre → qualif → lancement → débrief | **fait** (`mission/MissionLoop.hpp` + poste CONTROLE) : gates réels par phase, commit financier, issue déterministe, conséquences à triple lecture |

Oracles hors moteur : **135** (`tests/test_carte_flotte.cpp`) + **58**
(`tests/test_ares_modules.cpp`) + **46** (`tests/test_session.cpp`) + **57**
(`tests/test_gdd_manques.cpp`) + **108** (`tests/test_reentry_perturb.cpp`) +
**612** (`tests/test_contenu_gdd.cpp`) + **53** (`tests/test_mission_loop.cpp`) +
**44** (`tests/test_economie_v12.cpp`) + **22** (`tests/test_code_qualif.cpp`) +
**59** (`tests/test_api_sol.cpp`) +
**141** (`Space Program/tests/test_astro_core.cpp`) = **1 344**, tous au vert.
(`test_session.cpp` compte 55.)

`test_api_sol.cpp` a besoin des seuls TU du cœur (pas de `app/jeu.cpp`) :
```
cl /std:c++20 /EHsc /fp:precise /DSP_STANDALONE_TESTS ^
   /I Source\SP\SpaceProgram /I Source\SP\SpaceProgram\astro_core\include ^
   /I Source\SP\SpaceProgram\mission\include ^
   Source\SP\SpaceProgram\tests\test_api_sol.cpp ^
   Source\SP\SpaceProgram\astro_core\src\Ephemeris.cpp ^
   Source\SP\SpaceProgram\astro_core\src\Lambert.cpp ^
   Source\SP\SpaceProgram\astro_core\src\Epoch.cpp ^
   Source\SP\SpaceProgram\astro_core\src\Kepler.cpp ^
   Source\SP\SpaceProgram\astro_core\src\Elements.cpp
```

### PASSAGE AU GDD v1.2 (2026-07-24/25)

Le GDD a été révisé en **v1.2** (la copie du dépôt `SP/GDD_ARES.md` est
autoritaire ; celle de `Downloads` est restée en v1.1). Décisions de
l'utilisateur : refonte économie à l'échelle Md€, slice MODÈLE du terminal/code
(ch.15) sans la toolchain/éditeur, et **tout reste en 3D** (marche 1re personne
dans l'ISS conservée — pas d'interface terminale pure). Fait dans cette passe :

| GDD v1.2 | Module | État |
| :--- | :--- | :--- |
| 13 économie chiffrée (~100 Md€/an) | `economy/Economy.hpp` `AgencyFinance` | ✔ M€, deux jauges (trésorerie/réserve), recettes conditionnées à l'activité, invariant de pression d'inactivité, chaîne de fin de partie graduée |
| 13.4 confiance 0-100 = FILTRE | `economy/Economy.hpp` (`access_band`) + `Session::accepter_contrat` | ✔ habité suspendu <60, robotique <40, gelé <20 |
| 6.7.4 verrou de l'aller-retour | `rel/Relativity.hpp` | ✔ ratio unitaire^4 (β=0,5 → ×730) |
| 5.12.12 production d'antimatière | `rel/Relativity.hpp` (`AntimatterProduction`) | ✔ chaîne masse↔β, 4 params, β minuscule pour quelques grammes |
| **15.5 banc d'essai + certification** | **`code/CodeQualification.hpp`** | ✔ domaine de validité, rassure sans garantir, hors domaine = anomalie. Slice modèle (pas d'éditeur/toolchain) |
| **15.2 API sol (analyse, lecture seule)** | **`astro_core/include/ares/sol.hpp`** | ✔ façade sur astro_core : `ephemeride`/`lambert`/`Vehicule`/`journal`. La chaîne de l'exemple 15.2 s'exécute contre elle. **Rigueur : `dv_total` calculé sur les VRAIES vitesses des corps** (le Lambert du GDD ne passe que les positions ; on fait mieux). **`charger()` branché sur le VRAI catalogue** [12.1] : `etage_reel()` assemble un `fen::vehicle::Stage` (RL10C-1 + LOX/LH2…), `Vehicule` en lit le budget — oracle croisé façade↔cœur (Δv pleine charge == `Vehicle::total_dv`) |
| **15.3 API vol (logiciel embarqué)** | **`astro_core/include/ares/vol.hpp`** | ✔ `Contexte`/`Etat`/`Cible`/`Ecart`/`Manoeuvre`/`Solveur`/`Reserves` ; le Contexte ENREGISTRE les décisions (exécuté/différé/alerte/replanif/journal) pour que le simulateur les applique et l'oracle les inspecte. L'exemple `sequence_correction` du GDD tourne littéralement, ses 4 garde-fous sous oracle |
| 11.1 Novellus = station, Vigie = poste | postes / HUD | ✔ le poste architecte renommé **VIGIE** |

RESTE du ch.15 (gros chantier UI/technique, différé après le slice modèle) :
- ~~brancher `charger()` sur le vrai catalogue~~ — **fait** (`etage_reel` +
  `architectures()` sur `vehicle/PartsCatalog`, oracle croisé façade↔cœur) ;
- **brancher le solveur `ares::vol` sur la nav réelle** de la boucle : bloqué
  tant qu'il n'existe pas de source de SOLUTION DE NAVIGATION de premier ordre
  ni de chemin d'exécution du code joueur (⇒ vient avec la toolchain ci-dessous) ;
- l'**éditeur de graphe** (mode Normal, mêmes appels que le C++) ;
- la **toolchain C++ embarquée + bac à sable** (mode Pro).

### PASSAGE AU MONDE UNIQUE 1:1 (GDD v1.2, 2026-07-25)

Le seul delta v1.2 non traité par les passes précédentes est **la scène unique**
[décision 19 + ch.8.3/17.3/17.4/18], confirmé par l'utilisateur : le monde d'ARES
est **UNE seule scène persistante = le système solaire à l'échelle 1:1**. Novellus
ET les vaisseaux y sont placés à leur position réelle ; la **caméra libre** zoome
en continu du plan système au plan vaisseau ; la « carte » n'est qu'un **cadrage
lointain** du même monde, **[M] = signet de caméra**. Les **déplacements 3D
restent** (marche 1re personne dans Novellus et dans les missions vécues) — seul
le *pilotage* est médié par le terminal, pas l'observation ni l'ambulation.

Le code partait de l'inverse : **3 scènes exclusives** `SceneJeu{Titre,Station,
Carte}` togglées par [M]. Refactor en **plusieurs incréments**, chacun build +
oracles verts.

**Incrément 1 — FONDATION (FAIT, 2026-07-25).** Modèle de scène unifié dans le
cœur pur, migré côté pont UE, rendu à l'identique de l'ancien couple Station/
Carte (aucune régression visuelle voulue à ce stade) :
- `app/bridge_flags.hpp` : `enum SceneJeu{Titre,Monde}` + nouveau
  `enum Cadrage{Bord,Systeme}` (le plan de caméra DANS le Monde, pas une scène).
  `carte3d_active` devient le signal « cadrage == Systeme ».
- `app/session.hpp` : champ `cadrage`, `nouvelle_partie` → `Monde`+`Bord`,
  publication du pont dérivée de (scène, cadrage), postes publiés au seul `Bord`.
- Pont UE migré : `SPPlayerController` (routage entrée par cadrage ; **[M] bascule
  le cadrage Bord↔Systeme, plus la scène**), `SPHud` (`PaintStation`/`PaintCarte`
  selon cadrage ; visibilité poste ; reprises de partie), `SPStation` (mesh actif
  au cadrage Bord = `Monde && !carte3d_active`), `SPGameSubsystem` (`-spscene`).
  `SPSolarSystem` inchangé (déjà piloté par `carte3d_active`||`menu_backdrop`).
- Oracle `tests/test_session.cpp` réécrit sur le nouveau modèle : **56/56**.
- Build `SPEditor` : Succeeded. **NB : rendu à l'identique — la sensation d'« un
  seul monde » n'arrive qu'à l'incrément 2.**

**Incrément 2 — CONTINUITÉ (FAIT, 2026-07-25).** [M] n'est plus une bascule sèche
mais un VOL de caméra continu, ancré sur la Terre (là où orbite Novellus), qui
réutilise le lissage de distance déjà présent côté système (`SmoothDistKm`,
τ=0,35 s) :
- `app/session.hpp` : modèle `VolCamera` (progrès 0→1, smoothstep, interpolation
  LOG de la distance de vue) + `demarrer_vol_cadrage()` ([M]) et
  `retour_bord_immediat()` (Échap). `Session::tick(dt)` avance le vol et pilote
  `cam.dist_km`/`focus_body`. Bord→Système : la vue s'ouvre AU RAS de la Terre
  et RECULE ; Système→Bord : la caméra PLONGE vers la Terre puis passe la main à
  la 1re personne à l'arrivée (fin du vol).
- `SPPlayerController` : [M] lance le vol (plus la bascule sèche) ; zoom/orbite
  manuel suspendu pendant le vol (`if (!vol_cam.actif) TickCarte`) ; Échap = retour
  immédiat ; l'ambulation n'est pilotée que si l'on est encore à bord.
- Oracle `test_session` : **73/73** (dont 17 sur le vol) ; build `SPEditor`
  Succeeded.
- LIMITE ASSUMÉE : un seul plan rend à la fois — il reste UNE coupure au zoom le
  plus serré (intérieur ↔ Terre au ras), mais le MOUVEMENT de caméra est continu.
  La coexistence visuelle (voir l'intérieur grandir dans le zoom) est l'incr. 3.

**Incrément 3 — MONDE UNIQUE (moteur ch.18), EN COURS.** Constat en lisant le
rendu : la carte porte DÉJÀ le gros du ch.18 pour le système — coordonnées double
précision, rendu caméra-relatif (floating origin), near-clip adaptatif, et
compression de profondeur « scaled space » (`app/scaled_space.hpp`). Le vrai
manque est que **Novellus et l'ambulation sont un MONDE À PART** de ce pipeline.
- **Incr. 3a — position monde de Novellus (FAIT, 2026-07-25).** Le C++ pur publie
  la position RÉELLE de Novellus (orbite LEO 418 km, cercle écliptique déclaré,
  helper flotte `flotte_position_rel` réutilisé — aucune physique côté rendu) sur
  `RenderBridge::station`. Oracle `test_session` **80/80** ; build Succeeded.
  Rien ne la REND encore — c'est 3b.
- **Incr. 3b — Novellus rendu dans la carte : MARQUEUR FAIT (2026-07-25).**
  Composant `StationMarker` (sphère émissive bleue, `SPSolarSystem`) placé à la
  position LEO publiée par 3a, via le pipeline caméra-relatif + `MarkerScale` de
  la flotte (même chemin PROUVÉ, aucune physique côté rendu). Build Succeeded ;
  capture `-spscene=map` SANS régression (identique à `ue_terre_iau_natif.png`,
  y compris les panneaux translucides = coquille de nuages low-poly préexistante).
  Nouveau flag de capture **`-spdist=<km>`** (distance de vue imposée, utile pour
  cadrer un objet proche d'un corps). CAVEAT vérifié par SCAN DE PIXELS : à la
  date réelle du test, Novellus (418 km) est OCCULTÉ derrière la Terre — correct
  pour un objet LEO vu de l'extérieur ; en jeu, orbiter la caméra (glisser) le
  révèle. Le visuel positif décisif vient avec 3c (modèle extérieur + focus).
- **Incr. 3c-1 — Novellus focalisable + label « [M] ENTRER » : FAIT (2026-07-25).**
  Sentinelle de focus `FOCUS_STATION` (=1000, hors enum Body) comprise par le
  rendu (`FocusWorldKm`), le picking (`PublishScreen` : Novellus dans la liste
  écran, donc CLIQUABLE), le zoom (`SPPlayerController`) et le HUD (`SPHud` : label
  centré « NOVELLUS / ORBITE TERRESTRE BASSE - 418 km » + « [ M ] ENTRER » vert,
  format `ref_issfocus.png`). [M] entre déjà à bord (vol de caméra incr.2). Oracle
  82/82 ; build OK. VÉRIFIÉ PAR CAPTURE `-spfocus=1000` : Novellus cadré contre les
  étoiles (caméra collée → Terre hors champ, pas d'occultation), anneau de
  désignation + label + « [M] ENTRER ». PIÈGE PAYÉ : `body_name`/`body_radius` NE
  doivent PAS être appelés sur le sentinelle (hors enum) — court-circuités dans le
  HUD (label) et le clamp de zoom.
- **Incr. 3c-2 — modèle extérieur ISS : FAIT (2026-07-25), via le RENDU À ÉCHELLE
  RÉELLE.** Le vrai modèle `ISS_stationary` (669 meshes NANITE) rend au zoom proche
  à la place du marqueur (LOD par taille apparente), TEXTURÉ + ÉCLAIRÉ, conforme à
  `ref_issfocus.png`. Racine du blocage trouvée par INVESTIGATION (pas captures en
  boucle) : (1) les meshes sont Nanite ; (2) Nanite + monde caméra-relatif à
  MICRO-ÉCHELLE (109 m = 0,109 u à 1 u = 1 km) = rien ne rend. L'utilisateur a
  tranché la vraie cause : passer TOUTE la carte à l'ÉCHELLE RÉELLE (1 u = 1 cm,
  voir §4). À cette échelle l'ISS fait ~10 900 u, Nanite marche, matériaux réels,
  échelle ~1.0 — plus AUCUN hack (repli/émissif/micro). VÉRIFIÉ par capture
  `-spfocus=1000` (ISS complète, panneaux dorés, éclairée). Composant `ExtRoot`
  (enfant du MapActor) + `ExtLight` d'appoint ; chargé tôt (Tick) pour chauffer les
  shaders. Bodies aussi re-vérifiés à échelle réelle (Terre jour/nuit OK, marqueur
  Novellus en LEO visible). Build OK. RESTE : R-3 (nettoyer `scaled_space.hpp` du
  dépôt si vraiment inutile), et **3c-3** (handoff vers l'intérieur ambulable).
- **Incr. 3c-3 — handoff continu vers l'intérieur ambulable** : remplacer la
  coupure au bout du vol [M] par une transition où l'intérieur prend le relais
  quand la caméra entre dans l'enveloppe de la station — fin de la dernière coupure.

### Cadrage du CONTENU (décision du 2026-07-24)

On remplit **tout ce que le corps du GDD NOMME**, et **rien** de ce que son
chapitre 20 renvoie explicitement à une version ultérieure — coûts et durées de
recherche unitaires, matrice mission × technos, table TRL par rang. Écrire ces
tables reviendrait à faire du game design que le GDD a délibérément reporté.
Le multijoueur [ch. 16] est reporté après le solo ; l'architecture actuelle
(autorité unique sur `GameState`, tick déterministe à sous-pas fixes, hash
d'état) ne s'y ferme pas.

Fait dans cette passe : tableau **6.4** complet (8 filières, une par ligne, avec
son facteur limitant), **paliers 1/2/4** de 5.12.3 (solaire, RTG, fission — les
sources d'ÉNERGIE, qui manquaient entièrement), **les 10 modules** de 11.2 avec
les effets de 11.6 (il en manquait trois : noyau, nœud d'amarrage, module
énergétique — les trois obligatoires du palier 1).

Puis, dans la même passe : l'**arbre technologique** couvre désormais toutes les
sous-branches nommées en 5.7–5.13 (**66 nœuds**, dont 10 transverses répartis
sur quatre branches), le **catalogue de missions** porte les types de 10.1
(**11 entrées**, du satellite LEO à la mission relativiste), et le **catalogue
de pièces** de 12.1 existe (`vehicle/PartsCatalog.hpp` : 18 moteurs, 5
réservoirs, 5 capsules, chacun avec sa lignée réelle, sa source, son niveau de
confiance A–D et son statut de qualification).

**Le contenu du GDD est donc couvert.** Ce qui reste hors périmètre est ce que
le GDD lui-même diffère (ch. 20) ou reporte (multijoueur, ch. 16).

### Couverture du GDD v1.1 par le C++ (relu chapitre par chapitre, 2026-07-24)

La correspondance détaillée vit dans `Source/SP/SpaceProgram/ARES_MODULES.md`.
Résultat de la relecture : **la couverture est complète sauf deux systèmes, qui
ont été écrits dans la foulée**, plus un point partiel.

| GDD | Module C++ | État |
| :--- | :--- | :--- |
| 2 modes (Normal→Pro irréversible) | `game/GameState.hpp` | ✔ |
| 3 carrière, vieillissement, carnet, passation | `career/Career.hpp` | ✔ |
| 4.1–4.4 cycle, déblocage, ressources | `mission/MissionFsm.hpp`, `economy/` | ✔ |
| 5 arbre 6 branches + verrou le plus fort | `tech/TechTree.hpp`, `tech/Unlock.hpp` | ✔ |
| 5.12 / 6.4 paliers 0–8 de propulsion | `vehicle/Propulsion.hpp` | ✔ |
| 6.1–6.3 Tsiolkovsky, F=2ηP/ve, régimes | `vehicle/Vehicle.hpp`, `vehicle/Propulsion.hpp` | ✔ |
| 6.5 thermique · 6.6 radiations · 6.7 relativité | `env/Thermal.hpp`, `env/Radiation.hpp`, `rel/Relativity.hpp` | ✔ |
| 7.1–7.5 simulation, éphémérides, nav | `astro_core/` (`prop/`, `astro/`, `nav/`) | ✔ |
| **7.1 traînée + pression de radiation** | **`force/Drag.hpp`, `force/Srp.hpp`** | **étaient ABSENTES de la pile de forces — ajoutées** |
| **7.6 rentrée atmosphérique, EDL, aérofreinage** | **`flight/Reentry.hpp`** | **était ABSENT (`Descent.hpp` = sans atmosphère) — ajouté** |
| 7.7 météo spatiale + atmosphères | `env/SpaceWeather.hpp`, `env/Atmosphere.hpp` | ✔ |
| **7.8 / 10.5 débris orbitaux** | **`env/Debris.hpp`** | **était ABSENT — ajouté** |
| 8 suivi de trajectoire et corrections | `nav/Tracking.hpp`, `nav/Gates.hpp` | ✔ |
| 9 habité, ressources vitales, délai lumière | `mission/Crew.hpp`, `mission/Events.hpp` | ✔ |
| **10.2 contrats exclusivement par mail** | **`mission/Mail.hpp`** | **était ABSENT — ajouté** |
| 10.3–10.4 gravité 5 niveaux, triple lecture | `mission/Severity.hpp` | ✔ |
| 11 Novellus, 10 modules, 4 paliers | `station/Novellus.hpp` | ✔ |
| 12.3–12.5 base de fiabilité | `reliability/Reliability.hpp` | ✔ |
| 13.1-13.2 économie, paliers d'alerte | `economy/Economy.hpp` | ✔ |
| **13.3 sites de lancement (azimut, rotation)** | **`economy/Economy.hpp` `LaunchSite`** | **était incomplet — `reachable()` n'enforçait que i ≥ latitude ; ajout de la trigonométrie sphérique `cos(i)=sin(β)cos(φ)`, du couloir d'azimut et de l'assist de rotation** |
| **12.4 dégradation des filières avancées** | **`reliability/AdvancedFilieres.hpp`** | **était ABSENT — cœurs nucléaires, radiateurs (branchés sur `env/Debris`), confinement antimatière = catastrophe** |
| 14 temps, accélération, deux horloges | `game/GameClock.hpp`, `rel/Relativity.hpp` | ✔ |
| 15.3 carnet de notes | `career/Career.hpp` | ✔ |
| 12.1 catalogue de pièces réelles | `vehicle/PartsCatalog.hpp` | ✔ 18 moteurs, 5 réservoirs, 5 capsules — lignée, source, confiance A–D, statut de qualification |
| 10.1 types de mission | `app/ares.hpp` (`seed_catalogue`) | ✔ 11 entrées |
| 5.7–5.13 sous-branches de l'arbre | `app/ares.hpp` (`seed_arbre`) | ✔ 66 nœuds, transverses répartis |
| 16 multijoueur | — | hors périmètre v1, assumé |
Les deux premières suites se relancent hors UE (vcvars64 puis) :
```
cl /std:c++20 /EHsc /fp:precise /DSP_STANDALONE_TESTS ^
   /I Source\SP\SpaceProgram /I Source\SP\SpaceProgram\astro_core\include ^
   /I Source\SP\SpaceProgram\mission\include ^
   Source\SP\SpaceProgram\tests\test_carte_flotte.cpp ^
   Source\SP\SpaceProgram\app\jeu.cpp Source\SP\SpaceProgram\astro_core\src\*.cpp
```

### Carte de la frontière depuis le rendu total

| Fichier | Rôle |
| :--- | :--- |
| `app/session.hpp` | **C++ pur** : la partie (routage de scène, sauvegardes, publication du pont). Remplace `fen::ui::Interface`. |
| `app/postes.hpp` | **C++ pur** : les 8 postes et leur position. Extrait de `station_ecran.hpp`. |
| `UEBridge/SPGameSubsystem` | possède la `Session`, la tick, monte le HUD, câble le contrôleur |
| `UEBridge/SPHud` | HUD natif : couche carte/station peinte + menu, modales, et **postes interactifs** (`SSPPoste`) en widgets Slate |
| `UEBridge/SPPlayerController` | entrée native par scène + `ASPGameMode` (`GlobalDefaultGameMode`) |
| `UEBridge/SPSky` | voûte étoilée, visible dans toutes les scènes |
| `Tools/make_sky.py` | crée `/Game/SP/T_Starfield` et `/Game/SP/M_SP_Starfield` |

---

## 3. Pièges déjà payés (ne pas les repayer)

1. 🔴 **Rien ne rendait en 3D** : `SSpaceProgramWidget::OnPaint` peignait un
   `MakeBox` plein écran opaque (le `glClearColor` du jeu 2D). Il masquait tout
   le monde UE. → ne le peindre que si `scene == SceneJeu::Titre`.
2. `ImGuiWindowFlags_NoBackground` doit couvrir **toutes** les scènes 3D.
3. Composants créés à l'exécution : **`Movable`**, jamais `Static`.
4. **`SetupAttachment()` AVANT `RegisterComponent()`** — l'ordre inverse échoue
   silencieusement.
5. **Diagnostic qui tranche** : `DrawDebugSphere` devant la caméra. Si elle
   n'apparaît pas, le problème est global, pas propre aux meshes.
6. L'overlay Slate renvoie `FReply::Handled()` sur **tout** le clavier et la
   souris → le monde UE ne reçoit AUCUNE entrée. Caméra et déplacement sont
   commandés par le HUD via `RenderBridge`, et UE republie en retour
   (projections écran, poste à portée).
7. `TWO_PI`, `PI`, `check` sont des **macros UE** : les entêtes du jeu doivent
   être inclus AVANT tout entête UE ; utiliser `UE_DOUBLE_TWO_PI`.
8. `UWorld::PersistentLineBatcher` est privé en 5.6+ →
   `GetLineBatcher(UWorld::ELineBatcherType::WorldPersistent)`.
9. Capture UE : `FViewport::ReadPixels` manuel rend un tampon VIDE → utiliser
   `FScreenshotRequest::RequestScreenshot(path, bShowUI=true, false)` puis
   attendre ~30 frames avant `RequestExit`.
10. Build impossible si l'éditeur est ouvert (Live Coding). `Config/DefaultRemoteControl.ini`
    autorise `ExecuteConsoleCommand` → `LiveCoding.Compile` via
    `PUT http://127.0.0.1:30010/remote/object/call` sans fermer l'éditeur.
11. Le GLB de l'ISS s'importe en 310 StaticMesh **sans hiérarchie mais avec les
    transformations CUITES** dans les sommets → un seul acteur porte tout.
    Collision : `CTF_USE_COMPLEX_AS_SIMPLE` (`Tools/iss_collision.py`), sinon on
    traverse les murs.
12. Un GLB peut contenir plusieurs meshes (Saturne : `Sphere` = corps,
    `Circle` = anneaux) → choisir par nom, pas « le premier du dossier ».

Payés au passage en rendu total UE5 (2026-07-24) :

13. **Les teintes Slate sont LINÉAIRES**, l'écriture finale repasse en sRGB.
    Donner `0,04` pour un panneau presque noir le rend GRIS MOYEN. Saisir les
    couleurs telles qu'on les mesure sur la référence (sRGB 0-255) et les
    convertir : `FLinearColor::FromSRGBColor`.
14. **Une voûte ÉMISSIVE géante crame toute la scène sous Lumen.** Les drapeaux
    de composant (`bAffectDynamicIndirectLighting`, `bAffectDistanceFieldLighting`)
    ne suffisent PAS. Le réglage qui marche est **`is_sky` sur le MATÉRIAU**
    (`Tools/make_sky.py`). Diagnostic qui tranche : recapturer avec
    `-ExecCmds="r.DynamicGlobalIlluminationMethod 0"` — si l'image redevient
    correcte, c'est la GI.
15. **`UTexture2D::CreateTransient` en 8192 x 4096 rend NOIR** alors que le
    primitif est bien soumis au renderer. Passer par un asset importé
    (`WasRecentlyRendered() == 1` distingue « pas rendu » de « rendu noir »).
16. UHT ne supporte pas un **`TUniquePtr` sur type incomplet** dans un `.h`
    d'UCLASS : le `.gen.cpp` instancie le destructeur. Pointeur nu + `delete`
    dans `Deinitialize`.
17. `Rect` est un **type de SlateCore** (`Rendering/SlateRenderer.h`) : nommer
    ses utilitaires autrement.
18. Slate **ignore l'interlettrage dans `Measure()`** : un texte positionné par
    sa largeur mesurée déborde. Rajouter `Len * LetterSpacing * Size / 1000`.
19. `FGeometry::GetLocalSize()` n'est PAS la taille en pixels du viewport (ici
    2883 x 1622 pour un backbuffer 1280 x 720). Toute échelle de HUD se calcule
    sur la géométrie ALLOUÉE, jamais sur `GetViewportSize`.
20b. **Un drapeau que personne ne peut renseigner est un piège.**
    `Severity::massive_debris` existait sans modèle de débris derrière : il ne
    pouvait être coché qu'à la main, c'est-à-dire exactement le « malus
    abstrait » interdit par [GDD 10.5]. Règle : quand un modificateur décrit un
    FAIT physique, il doit être DÉDUIT d'un calcul, jamais saisi. Idem pour
    `MissionContract::mail_body` sans boîte mail.
21. **La loi de décroissance orbitale se vérifie sur un cas connu.** La première
    écriture donnait 77 000 ans de durée de vie à 300 km (facteur `a` oublié
    dans `t ≈ B·H/(a·ρ·v)`, et activité solaire comptée deux fois : la table
    exponentielle standard est déjà une atmosphère MOYENNE, alors que
    `atmo_density_factor` part du minimum solaire). Les oracles l'ont attrapé.
    Repère : l'ISS à 400 km sans rehaussement, ~1 an.
23. **Ne jamais publier ρ ET H côte à côte.** La table exponentielle d'atmosphère
    donnait densité et hauteur d'échelle indépendamment : l'exponentielle d'un
    palier n'atteignait donc pas la densité de base du palier suivant, et la
    densité REMONTAIT de +39 % à 50 km — traînée non monotone en altitude,
    physiquement faux. Correction : H est DÉRIVÉE de la table,
    `H_i = (h_{i+1} − h_i)/ln(ρ_i/ρ_{i+1})`. La continuité devient structurelle.
24. **Une constante prise « à une altitude de référence » est un chiffre magique.**
    Allen–Eggers évalue tout à une hauteur d'échelle unique : la prendre à 60 km
    faisait dépendre le pic de décélération d'un paramètre arbitraire. Corrigé
    par point fixe : H est évalué À L'ALTITUDE DU PIC, qui dépend de H.
25. **Un modèle hors de son domaine doit le DIRE, et se tromper dans le sens
    sévère.** Allen–Eggers suppose la pente d'entrée constante ; à 11 km/s et
    −6,5° la trajectoire s'aplatit de ~3° avant le pic et la forme close
    surestime la décélération d'un facteur 1,9. Le modèle expose donc
    `constant_gamma_ok` et `gamma_drift_rad`, et l'oracle exige que la forme
    close MAJORE l'intégration hors domaine [GDD 12.5].
26. **Un invariant mathématique ne se teste que là où il est vrai.** « Le pic de
    décélération est indépendant du coefficient balistique » est un théorème
    d'Allen–Eggers en atmosphère ISOTHERME. En atmosphère réelle, un véhicule
    plus dense pénètre plus bas, où H diffère : la dépendance en B reparaît
    (quelques %). Les oracles testent l'égalité exacte sur une atmosphère
    isotherme construite pour ça, et la quasi-égalité sur l'atmosphère réelle.
27. **`fen::ephem::body_radius` renvoyait 0** pour Mercure, Vénus, la Lune et
    Jupiter (constantes absentes de `Constants.hpp`). Conséquences silencieuses :
    ces corps étaient rendus à taille NULLE, et `distance_cadrage` plaçait l'œil
    à 3 000 km de leur CENTRE — donc à l'intérieur de Jupiter. Corrigé (IAU 2015)
    et mis sous oracle. Leçon : un `default: return 0.0` sur un enum de données
    physiques est un piège, pas un repli.

Build : `Build.bat SPEditor Win64 Development -project=…\SP.uproject -WaitMutex`

---

## 4. Décisions d'architecture en vigueur

- **Cœur C++ pur, sans UnrealEngine** (`astro_core`, modules `fen/…`, `app/`) ;
  UE = rendu, entrées, audio. Frontière = `UEBridge/`.
- **Pont sens unique** `app/bridge_flags.hpp` : le jeu écrit, le rendu lit ;
  en retour UE publie projections écran et proximité des postes. Aucun recalcul
  de physique côté rendu.
- **Carte à l'ÉCHELLE RÉELLE : 1 u = 1 cm (natif UE), AUCUNE compression**
  (décision utilisateur 2026-07-25 : « 1 m = 1 m point barre »). Rendu relatif à
  la caméra (l'œil = origine, positions km rebasées en double puis converties en
  cm réels via `UU_PER_KM = 1e5` dans `SPSolarSystem`). Un objet de 100 m fait
  10 000 u = taille NORMALE → Nanite/culling/précision fonctionnent, plus de
  micro-échelle. Les corps lointains restent des **marqueurs HUD** (pas de conflit
  de z-buffer, donc pas besoin de compression). **`scaled_space.hpp` n'est plus
  utilisé par le rendu** (fichier + oracles conservés, include retiré de
  `SPSolarSystem`). Le « km » du pont (`cam.dist_km`, `distance_cadrage`, vol_cam)
  reste une distance LOGIQUE ; seule la conversion km→u de rendu a changé.
- **Station à l'échelle UE** : 1 u = 1 cm, modèle mis à 55 m d'envergure
  (valeur de la référence). Repère station (m, X = couloir, Z = haut) → UE :
  ×100 avec miroir en y.
- **Toute approximation est DÉCLARÉE** dans le HUD [GDD 6.8].
- **NIVEAU VIDE DÉDIÉ `/Game/Maps/SP_Empty`** (créé 2026-07-25 par
  `Tools/make_empty_level.py`, commandlet `UnrealEditor-Cmd -run=pythonscript`).
  Le jeu se bâtit PAR CODE (WorldSubsystems au BeginPlay) : il lui faut juste un
  niveau vide comme conteneur. Réglé dans `Config/DefaultEngine.ini`
  (`GameDefaultMap` + `EditorStartupMap` = SP_Empty) et
  `Config/DefaultEditorPerProjectUserSettings.ini` (`LoadLevelAtStartup=ProjectDefault`)
  + `Saved/Config/.../EditorPerProjectUserSettings.ini` (`[EditorStartup] LastLevel`).
  RÉSOUT le bug PIE (l'éditeur ouvrait le template landscape `OpenWorld` et les
  subsystems bâtissaient par-dessus). Vérifié : `-game` sur SP_Empty rend la scène
  SP proprement.

---

## 5. LE TRI — quoi garder, quoi supprimer

### 5.1 GARDER — physique et GDD (le cœur, intouchable)

| Chemin | Rôle |
| :--- | :--- |
| `Source/SP/SpaceProgram/astro_core/` | Kepler, Lambert, propagateurs, éphéméride, forces, nav, vehicle — LA physique |
| `Source/SP/SpaceProgram/mission/include/fen/{career,economy,game,mission,reliability,save,station,tech}` | Modules GDD v1.1 (carrière, économie, fiabilité, arbre, Novellus, sauvegarde) |
| `Source/SP/SpaceProgram/app/ares.hpp` | Adaptateur couche ARES ↔ agence |
| `Source/SP/SpaceProgram/app/bridge_flags.hpp` | Le pont jeu ↔ rendu |
| `Source/SP/SpaceProgram/app/scaled_space.hpp` | Compression de profondeur (sous oracle) |
| `Source/SP/SpaceProgram/tests/` | Oracles (135 + 58) |
| `Space Program/tests/` | Les 102 oracles physiques |
| `Space Program/assets/` | GLB + textures 8K |
| `Space Program/render/` | **Référence** solar_system_map (source `spr` + app) — documentation vivante |
| `Space Program/docs/` | GDD dérivés, captures de référence, ce document |
| `Content/SolarSystem/`, `Content/ISS/` | Assets importés |
| `Tools/*.py` | Import GLB, collision, diagnostic |
| `Source/SP/UEBridge/` | Frontière UE (map, station, overlay, capture) |

### 5.2 À SUPPRIMER — le prototype 2D, une fois ses acquis extraits

Ces écrans sont **déjà débranchés** (toute navigation retombe sur la station) ;
ils ne sont plus atteignables en jeu, mais le code pèse encore.

| Chemin | Lignes | Condition de suppression |
| :--- | ---: | :--- |
| `ui/jeu_ecrans.hpp` — écrans `e_bureau`, `e_contrats`, `e_gestion`, `e_programme`, `e_vol`, `e_carte`, `e_vol_interp`, `e_etude`, `e_systeme`, tutoriel `dessiner_guide`/`beat_tuto`, `barre_haut` | ~1200 sur 1562 | garder uniquement : `Interface`, routage de scènes, `e_titre`, `e_creation`, `e_reglages`, `e_game_over`, gestion des sauvegardes |
| `ui/ares_ecrans.hpp` | 272 | après avoir porté son contenu dans les **postes** de l'ISS (AGENCE/arbre, NOVELLUS, etc.) |
| `ui/hud.hpp`, `ui/panels.hpp` (`Board`, `Vue3D`, jauges 2D) | 314 | dès que `jeu_ecrans` ne les référence plus |
| `ThirdParty/implot` | — | dès que plus aucun écran ne trace de courbe ImPlot |
| `Space Program/_archive/tri_ue5_20260723/{app,ui,astro_core,mission,extern,scripts,dist,build,_backup_render,CMakeLists.txt,space_program_3d.cpp}` | — | **SAUF `build_vk/`** qui porte le binaire de référence et ses shaders — indispensable aux captures |

### 5.3 À TRANCHER (ni évident à garder, ni à jeter)

- ~~`app/jeu.{hpp,cpp}` (2531 lignes)~~ — **SCINDÉ le 2026-07-26.** Le modèle
  mélangeait l'agence utile (contrats, économie, flotte, sauvegarde, époque)
  et toute la mécanique de vol 2D héritée (`Vol`, `VolInterp`, `Conception`,
  `EtudeMars`, wizard, marché, Monte-Carlo, installations/recherches maison).
  Cette dernière n'était **plus atteignable** (aucun écran ne l'armait depuis
  le rendu total UE5 : plus aucun appel à `commit()`/`interp_commit()`/
  `vol_engager()` dans le code vif ; seul `session.hpp::publier_carte` en lisait
  encore l'état, dans des branches toujours-fausses) et est remplacée par la
  couche ARES + `mission/MissionLoop.hpp`. Retirée. `jeu.cpp` : **2035 → 361 l.**,
  `jeu.hpp` : **496 → 141 l.**, `session.hpp` : **−115 l.** (branches de tracé de
  vol mortes). Save/load : format inchangé (les clés héritées `inst*`/`rech*`
  sont ignorées si présentes dans une vieille save). Vérifié : oracles
  `test_carte_flotte` 135/135 + `test_session` 82/82, **build `SPEditor`
  Succeeded**. NB : le rendu du vaisseau/GEO dans `SPSolarSystem` (piloté par
  `RenderBridge`) reste en place mais dort (snapshots `valid=false`) — il sera
  réveillé par la boucle de mission vécue [GDD 9] quand elle publiera une vraie
  trace.
- `mission/include/fen/mission/Program.hpp` : embryon de contrats, à réconcilier
  avec `MissionFsm.hpp` (GDD 4.1).

### 5.4 Ordre du tri (chaque étape se termine par build + oracles verts)

1. Porter le contenu de `ares_ecrans.hpp` dans les postes de l'ISS → supprimer le fichier.
2. Amputer `jeu_ecrans.hpp` de ses écrans morts → supprimer `hud.hpp`, `panels.hpp`, `implot`.
3. ~~Construire la boucle de mission cible (jalon D) → scinder `jeu.cpp`.~~
   — **fait** (2026-07-26) : boucle cible = `MissionLoop.hpp` ; `jeu.cpp` scindé
   (vol 2D retiré, cf. §5.3).
4. Purger `_archive` sauf `build_vk/`.

---

## 6. Suite du travail (ordre)

~~1. HUD carte au format référence~~ — **fait** (2026-07-24, natif Slate).
~~2. Fond étoilé + bloom du Soleil~~ — **fait**.
~~3. Menu au format `ref_menu.png`~~ — **fait**.

~~4. Clic = vol vers le corps~~ — **fait** : la distance de vue est lissée en
   LOGARITHME (`USPSolarSystemSubsystem::SmoothDistKm`, τ = 0,35 s), le point
   visé l'était déjà. Un cran de molette paraît immédiat, un changement de
   focus devient un vol. Drapeau de vérification : `-spfocus=<Body>`.
5. **Fidélité des corps** — C'EST LE PROCHAIN GROS MORCEAU. Constats établis par
   capture (`-spscene=map -spfocus=3` et `-spfocus=6`, cf. `ue_terre_natif.png`) :
   - **le facettage est visible** dès qu'un corps remplit l'écran : les sphères
     GLB sont peu tessellées et/ou importées à normales plates ;
   - **la face jour est délavée** : le Soleil est un `PointLight` d'intensité 12
     sans décroissance en carré ; à l'échelle vraie il sur-éclaire la face
     proche. Un `DirectionalLight` orienté depuis le Soleil serait plus juste ;
   - **le terminateur est dur**, sans transition jour/nuit ;
   - la carte de nuit FONCTIONNE (les lumières des villes sortent bien côté
     nuit) : le manque est le mélange jour/nuit et les nuages, pas les textures.
   - piste écartée : les matériaux importés sont DÉJÀ opaques et une face
     (`Tools/fix_planet_materials.py` le vérifie et le journalise).
   Reste ensuite : orientations IAU (axe + phase), atmosphère Vénus, anneaux
   Saturne, 19 lunes.
6. **ISS extérieure** sur la carte + « [M] ENTRER DANS L'ISS ».
~~7. Postes complets~~ — **fait** : les 8 postes câblés (`UEBridge/SPHud.cpp`,
   `SSPPoste`), actions terminales comprises (recherche, accepter contrat,
   assembler véhicule).
~~8. Boucle de mission~~ — **fait** (`mission/MissionLoop.hpp` + poste CONTROLE) :
   gates réels par phase, commit financier au feu vert, issue déterministe tirée
   contre la P(succès), conséquences à triple lecture.
   ~~fenêtres synodiques~~ — **fait** : le gate FENÊTRE est réel
   (`astro/LaunchWindow.hpp`, auto-calibré sur la carte `Porkchop`) et branché
   sur la transition WindowSearch→Qualification (`launch_window_gate` appelé par
   `Session::avancer_mission`). Une mission Mars (mars/mars_habite/surface) ne
   passe en qualification que si la fenêtre est ouverte ; sinon le refus chiffre
   l'attente. Familles à fenêtre permanente en V1 pour raison PHYSIQUE :
   near-Earth, NEP (poussée continue), « science » (cible non nommée). Oracles :
   `test_astro_core` (ouvre à l'optimum 2026-10-31, ferme à la conjonction,
   récurrence 779.9 j) + `test_mission_loop` (le gate lui-même).
   **Δv d'injection RÉEL (Oberth)** : `Transfers.hpp` fournit
   `injection_dv_from_circular` / `capture_dv_to_circular` / `capture_dv_to_ellipse` ;
   `WindowResult` expose le v_inf de l'optimum, d'où le Δv réel depuis une orbite
   de parking (fenêtre 2026 : 3636 m/s depuis LEO, pas les 2.95 km/s nus ni
   v_circ+v_inf). **BRANCHÉ sur `assess`** (décision utilisateur) : pour les
   familles Mars, `trajectory_dv_for_mission` remplace le forfait par
   injection(Oberth, LEO) + insertion(capture elliptique Mars) + marge de
   mi-parcours, calculés sur la fenêtre courante — le coût devient sensible à la
   géométrie (fenêtre 2026 : **4686 m/s** vs forfait 4800). Le driver le pose via
   `MissionPlan::dv_traj_override` dans `Session::evaluer_plan`. Familles sans
   fenêtre : forfait `trajectory_dv_for_family` conservé à l'identique.
   RESTE en V2 : le vol MANUEL (le joueur exécute les manœuvres au lieu d'un
   tirage) pour les missions vécues [GDD 9].
9. **Systèmes GDD** : rangs, 6 branches + verrou le plus fort, mails ARES,
   carnet, économie à paliers, Novellus 10 modules, fiabilité tracée.
10. **Dettes du passage en rendu total** (petites, mais à ne pas oublier) :
    - la voûte étoilée est vue **en miroir** et n'est pas calée sur le repère
      équatorial J2000 (approximation déclarée dans `SPSky.h`) ;
    - les **captures headless doivent laisser compiler les shaders** : à froid,
      300 frames ne suffisent pas et l'image sort avec des matériaux de repli
      (« Preparing Shaders » à l'écran). Utiliser `-spframes=900` après un
      changement de matériau, sous peine de diagnostiquer une image fausse ;
    - la barre de temps est un **indicateur** : le curseur n'est pas saisissable
      [GDD 14]. Le jour où le temps se pilote, ça passera par le système
      temporel de l'agence, avec ses coûts — jamais par ce curseur.
