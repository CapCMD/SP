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
   ⚠ **DIVERGENCE VOULUE depuis le 2026-07-27** (décision de l'utilisateur) : le
   portage n'a plus NI la barre de temps du bas NI le témoin « LIVE ». Le bandeau
   permanent (`SSPTemps`, haut-droite) les répétait sur ce seul écran et en dit
   plus (crans cliquables, plafond de mission [GDD 14.3]). Ce point de la
   référence est donc volontairement non suivi — ne pas le « rétablir » en croyant
   corriger une régression. Le reste du format reste la référence.
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

**`-spcadence=<0..4>`** fait **COULER le temps** d'emblée (`fen::game::TimeRate` :
0 pause, 1 réel, 2 jour/s, 3 semaine/s, 4 mois/s). Vérification de bout en bout :
deux captures aux `-spframes` différents doivent montrer date, heure, rotation des
corps et position de Novellus qui ont avancé.

**`-spvol`** **épingle une mission EN ASCENSION** : c'est l'oracle visuel du
RYTHME IMPOSÉ [GDD 14.3] (§2), un instant qui ne s'atteint autrement qu'en jouant
toute la boucle de mission. Comme `-sphandoff`, il pose l'ÉTAT DU MODÈLE et non
le pont — sinon la capture ne prouverait que l'existence du drapeau. À combiner
avec `-spcadence=4` : la demande « mois/s » doit ressortir bornée au temps réel.

**`-sphandoff`** (avec `-spscene=map`) **gèle l'instant de la reprise** en 1re
personne au bout du vol [M] : plan système actif, caméra amarrée sur l'œil du
pawn, intérieur en coexistence. C'est l'ORACLE VISUEL du handoff — l'image doit
être celle de `-spscene=iss`. Le couple est archivé
(`ue_handoff_ref_iss.png` / `ue_handoff_reprise.png`) et se compare au pixel : la
recherche de meilleur recalage donne dx=dy=0 et l'écart moyen 1,7/255 (HUD exclu,
absent en transit par construction) — voir §2, incr. 3c-3.

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
| Terre jour/nuit + nuages, orientations IAU, 19 lunes, anneaux | **orientations IAU : FAIT et VÉRIFIÉ AU RENDU**. `ephem/BodyOrientation.hpp` (WGCCRE 2015) donne axe écliptique, obliquité, méridien origine W(t), latitude sub-solaire (oracle couronne : Terre +23.4° au solstice de juin). `SPSolarSystem::OrientationAt` le CONSOMME : incline le mesh de l'obliquité réelle + le tourne de W(t) (miroir y -> −W), repli sidéral si pas d'éléments IAU. **Preuve par capture** (build SPEditor OK 19 s ; `-spfocus` 3/7/9 → `ue_{terre,saturn,uranus}_iau_natif.png`) : **les anneaux de Saturne sont nettement INCLINÉS à 26.7°** (avant : plats dans l'écliptique), la Terre est inclinée + jour/nuit, HUD « orientation = IAU WGCCRE ». Bonus : Lune synchrone, Vénus/Uranus rétrogrades (signe de W). **QUALITÉ DU MESH — FAIT (planètes rondes)** : GLB peu denses (~960 tris → silhouette polygonale). Pipeline SANS rien inventer (on n'agit que sur géométrie/normales du GLB, textures intactes) : `Tools/subdivide_planets.py` = tessellation PN ×16 (960→15360 tris) via Geometry Script (plugin **GeometryScripting** activé dans `SP.uproject`), puis `Tools/smooth_normals_geo.py` = normales lisses par sommet (`set_per_vertex_normals`, écrit `recompute_normals=False` sinon le build les re-facette). **Terre : lisse, ronde, jour/nuit + continents** (`ue_terre_iau_natif.png`). ⚠ **CORRECTIF 2026-07-26 — le jour/nuit était CASSÉ depuis le passage à l'échelle réelle** : la position de la lumière du Soleil passait par le borneur `R()` (plafond `RENDER_MAX_UU` = 1e6 u), soit **10 km à 1 u = 1 cm** alors que le corps regardé est à des milliards d'u — la lumière était ~4000 fois plus près que la planète, donc pratiquement sur l'œil, et **le terminateur disparaissait** (hémisphère visible intégralement éclairé). La capture qui « validait » le jour/nuit LUI EST ANTÉRIEURE (son HUD dit encore « ECHELLE VRAIE 1 u = 1 km ») : la vérification était périmée, pas fausse au moment où elle a été faite. Corrigé (piège n°29) : le Soleil est placé sur sa VRAIE direction à 1e12 u (1e7 km), assez loin pour des rayons quasi parallèles ; le terminateur est revenu, vérifié par capture. RESTE (mineur, distinct de l'orientation ET des textures) : corps quasi SANS relief (surface Saturne, Uranus) gardent un léger banding au terminateur sous éclairage mono-source ; 19 lunes présentes dans les GLB, pas encore placées ; la coquille de nuages de la Terre est un GLB low-poly dont les facettes translucides se voient au gros plan |
| Postes : contenu complet + arbre de compétences + catalogue de missions | **à faire** |
| Boucle de mission [GDD 4.1] : contrat → conception → fenêtre → qualif → lancement → débrief | **fait** (`mission/MissionLoop.hpp` + poste CONTROLE) : gates réels par phase, commit financier, issue déterministe, conséquences à triple lecture |
| **LE TEMPS QUI COULE [GDD 14.2]** : accélération pilotable et non neutre | **FAIT (2026-07-26), VÉRIFIÉ DE BOUT EN BOUT.** Le calendrier de l'agence (`agence.mois`) est devenu CONTINU ; la comptabilité reste MENSUELLE et se solde à chaque frontière franchie (`Jeu::avancer_temps`). Le temps n'avance que par SOUS-PAS FIXES de 1/64 j (`Jeu::PAS_JOURS`) : 4 frames ou 100 frames donnent le MÊME calendrier — sous oracle. Cinq cadences (`fen::game::TimeRate`), pilotables de **TROIS** endroits, tous équivalents : le **BANDEAU DU TEMPS en haut à droite** (`SSPTemps`, présent PARTOUT dans le Monde — les deux cadrages, poste ouvert compris ; date, heure, cinq crans cliquables, légende des touches), le **clavier** ([P] pause/reprise, [1-5] les crans — `ASPPlayerController::TickCadence`, touches lues par POSITION physique donc AZERTY comme QWERTY, aucune collision avec l'ambulation), et le **poste AGENCE**, qui reste le réglage « institutionnel » et affiche en plus le PRIX du temps tiré du modèle (`AgencyFinance::annual_idle_balance_me()` = −9,0 Md€/an d'inactivité). Aucun de ces trois n'est le « curseur de temps » que [GDD 14] interdit : ce sont des crans DISCRETS passant par le système temporel de l'agence, jamais un accès à une date arbitraire. **Deux corrections d'ergonomie payées à l'essai** (voir pièges n°40) : la première version ne réglait la cadence QU'au poste AGENCE — depuis le plan système il fallait rentrer à bord et marcher jusqu'au module pour démarrer le monde ; et l'état du temps ne s'affichait que sur la carte, ce qui faisait passer le reste du jeu pour figé. **CHOIX ASSUMÉ, PUIS RENVERSÉ (2026-07-27)** : la barre de temps du bas avait d'abord été gardée telle que dans `ref_systeme.png` (date | cadence | heure + rail), la fidélité à la référence l'emportant sur la non-redondance. L'utilisateur a tranché l'inverse à l'usage : **barre du bas et témoin « LIVE » RETIRÉS** de la carte (`ue_carte_hud_minimal.png`). Le bandeau reste seul, et c'est cohérent — il est partout, il en dit plus (crans cliquables, plafond de mission) et le HUD de la carte doit être MINIMAL. Le bandeau, lui, ne porte pas la cadence en clair (le cran actif est nommé et coloré) pour rester assez court et ne pas empiéter sur le cadre d'un poste. Toute partie DÉMARRE EN PAUSE (fondation, chargement, reset) : le temps est une dépense. Garde-fou : une frame > 0,25 s est bornée (un gel de shaders ne coûte pas des mois). La faillite arrête le calendrier. **PREUVE** : `-spcadence=4` à 900 puis 1500 frames → NOV 3 2026 puis DEC 27 2026, Terre tournée, terminateur déplacé, Novellus avancé sur son orbite |
| **LE RYTHME DU TEMPS EN MISSION [GDD 14.3]** : une phase critique IMPOSE un plafond de cadence | **FAIT (2026-07-27), VÉRIFIÉ PAR CAPTURE.** « Toute manœuvre fine RAMÈNE le temps à un rythme lent » — le verbe du GDD est actif, donc le plafond est OPPOSABLE, pas suggéré. **`mission/MissionTempo.hpp`** (C++ pur) le DÉDUIT au lieu de le saisir : à la cadence r, une phase de durée propre D disparaît en D/r secondes réelles ; on exige qu'elle en dure au moins `OBSERVATION_MIN_S` = 20 s (SEUL paramètre libre de la loi, déclaré [GDD 6.8]), d'où le cran le plus rapide admissible. Les DURÉES sont des grandeurs sourcées, pas des réglages : ascension ~9 min (Falcon 9 SECO T+8 min 40), EDL ~7 min (MSL), insertion ~10 min (Apollo LOI 6 min 2 s). Aux cinq crans de `TimeRate`, toute phase < ~10 jours retombe sur le TEMPS RÉEL — résultat attendu, mais CALCULÉ, donc il se déplacera tout seul quand une phase durera des semaines. PLANCHER : jamais la pause — le modèle empêche d'aller trop vite, il ne fige pas le monde à la place du joueur [GDD 14]. **La phase de vol est DÉRIVÉE** (`flight_phase_of` : état FSM + temps passé dedans + famille) : `Mission::phase` était un drapeau que RIEN ne renseignait (piège n°20b) et que seule une saisie manuelle aurait pu remplir ; elle est maintenant vivante, rejouable, et rien de plus à sauvegarder. Le prédicat de phase critique est **partagé avec `Events.hpp`** (taux d'anomalie) : une seule définition pour deux lois qui disent la même chose. Enforcement à DEUX niveaux, pour que la faute soit impossible et non corrigée après coup : `Jeu::regler_cadence()` est la porte unique (les 4 écritures — bandeau, poste AGENCE, touches [P]/[1-5], `-spcadence` — y passent) ET `faire_couler_le_temps()` rappelle le plafond avant de convertir la moindre seconde. Le feu vert d'une mission appelle `appliquer_plafond()` DANS SA FRAME : c'est la manœuvre qui freine le monde, pas le joueur qui doit y penser. AFFICHÉ sur les trois surfaces (piège n°40) : bandeau `SSPTemps` (crans fermés en rouge + « RYTHME IMPOSE : ASCENSION »), poste AGENCE (boutons éteints + motif en toutes lettres), et la barre de la carte qui montre la cadence RÉELLEMENT appliquée. **PREUVE** (`-spvol`, nouveau drapeau §1) : deux captures à 900 frames, même `-spcadence=4` demandé — libre, le monde avance de 3 mois (OCT 27 2026, cran MOIS vert) ; sous ascension, il ne bouge pas (JUL 26 2026, `REAL RATE`, JOUR/SEM/MOIS en rouge). `ue_tempo_libre.png`, `ue_tempo_impose.png`, `ue_tempo_poste_agence.png`. RESTE : l'insertion et l'EDL sont des phases critiques du modèle mais ne sont pas encore DATÉES (seule l'ascension l'est) — elles le seront quand la mission vécue [GDD 9] portera sa chronologie de vol, c'est le point 2 ci-dessous |

Oracles hors moteur — **compteurs RELEVÉS en exécutant les 12 suites le
2026-07-27** (les totaux de ce document ont déjà été périmés d'une trentaine
d'unités : ne pas les recopier, les remesurer) :

| Suite | Oracles |
| :--- | ---: |
| `tests/test_contenu_gdd.cpp` | 612 |
| `tests/test_session.cpp` | **175** (82 avant l'incr. 3c-3, 107 après, 137 avec le temps qui coule, +38 pour le rythme en mission) |
| `Space Program/tests/test_astro_core.cpp` | **300** (141 avant, +159 pour les 19 lunes) |
| `tests/test_carte_flotte.cpp` | 135 |
| `tests/test_reentry_perturb.cpp` | 108 |
| `tests/test_api_sol.cpp` | 59 |
| `tests/test_ares_modules.cpp` | 58 |
| `tests/test_gdd_manques.cpp` | 57 |
| `tests/test_mission_loop.cpp` | 53 |
| `tests/test_economie_v12.cpp` | 46 |
| `tests/test_code_qualif.cpp` | 22 |
| **TOTAL** | **1 625**, tous au vert |

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
- **Incr. 3c-3 — HANDOFF CONTINU VERS L'INTÉRIEUR AMBULABLE : FAIT (2026-07-26),
  VÉRIFIÉ AU PIXEL.** La dernière coupure du vol [M] est supprimée. Le vol n'est
  plus ancré sur la Terre à 7 327 km puis coupé : il est ancré sur **NOVELLUS**
  (`FOCUS_STATION`) et **s'amarre sur l'œil du pawn**.
  - `Session::pose_bord()` (C++ pur, sous oracle) convertit la position de l'œil
    publiée par UE (`station_out`, repère station en m) en pose d'orbite caméra
    `(dist, yaw, pitch)` autour du centre de la station — miroir en y. Invariant
    central sous oracle : la reconstruction cartésienne de cette pose REDONNE
    l'offset de l'œil. `VolCamera` interpole aussi yaw/pitch (yaw par le plus
    court chemin dans ±π), plus seulement la distance.
  - **DEUX SEUILS distincts**, et c'est le cœur du dispositif : l'**enveloppe**
    (demi-envergure, 27,5 m) déclenche la **coexistence** — la géométrie
    INTÉRIEURE rend, à la position réelle de Novellus, et le modèle extérieur
    s'efface (bascule de LOD au franchissement de la coque, là où elle est le
    moins visible) ; la **fin du vol** (amarrage, ~20 m) fait passer LA MAIN. Un
    oracle vérifie que la main ne passe jamais avant la fin du vol.
  - `RenderBridge` : `interieur_coexiste`, `cam.look_to_bord` (mélange
    d'orientation ET de champ, piloté par la DISTANCE donc symétrique entrée /
    sortie), `cam.vol_camera`, `station_out.fov_deg`.
  - `SPStation` : « rendre » et « avoir la main » sont désormais deux états
    distincts (`SetStationVisible` / `SetStationInControl`). En coexistence,
    `AppliquerDecalage` rebase la station + ses lampes + le pawn sur la position
    de rendu de Novellus que la carte publie (`GetNovellusRenderUU` — aucun
    rebasage dupliqué), collision coupée hors du repère canonique.
  - `station_out.ready` signifie maintenant « la scène existe », plus « le joueur
    a la main » : sans quoi le retour retombait au point d'apparition au lieu de
    l'endroit quitté.
  - HUD : aucun HUD en transit (les deux plans se relaient) ; l'affordance
    « NOVELLUS / [M] ENTRER » est gardée par la taille apparente.
  - **PREUVE** : `-sphandoff` vs `-spscene=iss` — recalage optimal dx=dy=0, écart
    moyen **1,7/255**, 1,3 % de pixels au-delà de 8/255. Trois écarts ont été
    trouvés et corrigés en chemin, chacun par diagnostic et non par tâtonnement :
    le CHAMP DE VISION (45° vs 90° = ×2,4 de grossissement, pièges n°31),
    l'EXPOSITION (deux politiques pour un seul monde, n°30) et la LUMIÈRE DU
    SOLEIL traversant la coque (n°29).
  - RESTE, assumé et déclaré : la bascule intérieur/extérieur est un échange de
    deux modèles distincts de la même station, co-centrés (leurs modules ne se
    correspondent pas un à un) ; Échap reste une coupure sèche, c'est sa raison
    d'être (sortie de secours).

### LES CORPS : LES LUNES, ET LA FIN DES GLB (2026-07-27)

**1. Les 19 lunes [GDD 7.1] — FAIT, sous oracle.**
`astro_core/include/fen/ephem/Satellites.hpp` : Titan n'était qu'un satellite
câblé à la main ; son modèle (orbite circulaire dans le plan équatorial du
parent, plan tiré du pôle IAU) est devenu la RÈGLE GÉNÉRALE des 19 lunes dont le
projet possède la texture. L'enum `fen::ephem::Body` est étendu **en fin de
liste** : les indices documentés (`-spfocus 3` = Terre, 7 = Saturne, 9 = Uranus)
restent valides, et les lunes commencent à 12 (Phobos) — Io = 14.
- **LA TABLE SE VÉRIFIE ELLE-MÊME** : elle porte la période sidérale PUBLIÉE, qui
  n'entre dans AUCUN calcul. Le modèle dérive la sienne de (a, GM) par Kepler et
  l'oracle exige l'accord à 1 %. Pire écart : **0,13 %** (Mimas).
- Ce que cet oracle a attrapé : **Pluton-Charon est une quasi-binaire** (Charon
  pèse 12 % du système) et `MU_PLUTO` est « hors Charon » — il manquait la SOMME
  des masses, d'où 5,9 % d'erreur sur la période. Le problème à deux corps se
  résout sur G(M+m) : corrigé, déclaré, et l'écart tombe à 0,2 %.
- Sortent de la table **sans y avoir été écrits** : la résonance de Laplace des
  galiléennes (2,007 et 2,014) et le système d'Uranus **couché** (plan orbital à
  82,4° de l'écliptique, conséquence directe du pôle IAU).
- Triton est **rétrograde** (i > 90°) sans aucun cas particulier dans le code :
  c'est l'inclinaison qui bascule le moment cinétique.
- La Lune garde sa série Montenbruck & Gill : **un bon modèle ne se remplace pas
  par un modèle générique**.
- Rendu : les lunes suivent leur parent dans `GBodies` (ordre imposé), leur
  `SpinH` est leur période orbitale (rotation synchrone — un FAIT, pas un
  réglage), et leur orbite est tracée RELATIVE au parent puis recentrée à
  l'ÉMISSION (la cuire en absolu la détachait, cf. piège n°45).
- **`show_moons` SUPPRIMÉ** : booléen que RIEN n'écrivait, qui éteignait la Lune
  et Titan pourtant câblées. Remplacé par la SÉPARABILITÉ à l'écran — la règle
  déjà payée pour Novellus (piège n°41), désormais partagée par les deux.

**2. LES CORPS NE SONT PLUS DES GLB — ils sont faits par UE (décision de
l'utilisateur).** Les lunes ont révélé un défaut qui touchait TOUS les corps
depuis au moins le 2026-07-25 : au gros plan, on voyait l'INTÉRIEUR de
l'hémisphère arrière (« dôme » translucide, pans manquants). Quatre pistes
éliminées **par l'expérience** avant de trouver : matériaux translucides,
normales facettées, Nanite, précision GPU (l'artefact est INVARIANT D'ÉCHELLE).
La cause : **les faces des GLB regardaient vers l'intérieur**.
- Pipeline de remplacement, deux outils :
  `Tools/make_body_sphere.py` → **`/Game/SP/SM_SP_Body`**, sphère lat-long
  128 x 256 (**64 512 tris**, 4x les GLB tessellés), UV équirectangulaires, faces
  sorties PAR CONSTRUCTION ;
  `Tools/make_body_materials.py` → importe les 30 textures de
  `assets/textures`, crée `M_SP_Body` (éclairé), `M_SP_Star` (non éclairé) et
  `M_SP_Ring` (masqué, deux faces), plus une instance `MI_SP_<Corps>` par corps.
- **JOUR/NUIT RÉEL** : le masque de nuit vient du produit scalaire entre la
  normale du pixel et la DIRECTION DU SOLEIL, passée au matériau par le C++
  (`BodyMids`, une MID par corps) — donc exacte pour CHAQUE corps, et vivante
  quand le temps coule. La carte des lumières de villes s'allume côté sombre.
- **LES NUAGES SONT UNE CARTE, plus une coquille** : la seconde sphère GLB
  low-poly translucide de la Terre, dont les facettes se voyaient au gros plan,
  n'existe plus. Même mécanique pour l'ATMOSPHÈRE de Vénus.
- **PREUVE** : `ue_corps_terre.png` (sphère parfaite, terminateur net, continents
  et nuages, lumières des villes des deux Amériques), `ue_corps_io.png` (Io
  texturée, silhouette lisse), `ue_corps_saturne.png` (bandes réelles).

**3. Deux corrections de rendu payées en chemin.**
- **Le Soleil est passé en lumière DIRECTIONNELLE.** C'était une `PointLight`
  qu'on plaçait à 1e12 u pour obtenir des rayons parallèles — six ordres de
  grandeur au-dessus du plafond de précision GPU. Une directionnelle n'a pas de
  position : insensible par construction, et c'est le modèle juste (le Soleil est
  une source à l'infini). Disparaissent avec elle : rayon d'atténuation, exposant
  de décroissance et l'erreur d'angle déclarée du placement.
- **Les corps passent enfin par le borneur de précision**, sous forme d'une
  homothétie de centre l'œil et de facteur **UNIQUE** (voir piège n°46).

**4. LES ANNEAUX DE SATURNE, faits par UE aussi — FAIT.**
Ils venaient du nœud « Circle » du GLB et ne rendaient plus (défaut antérieur au
chantier : encore une vérification périmée). `Tools/make_ring_mesh.py` génère
**`/Game/SP/SM_SP_Ring`** : couronne de 16 384 triangles, **rayons RÉELS**
(bord interne C 74 500 km, bord externe A 136 780 km, rapportés au rayon
équatorial de Saturne = 100 dans le repère du mesh parent, dont l'anneau est
l'enfant — il hérite donc de l'inclinaison IAU de 26,7°).
- **LA TEXTURE COMMANDE LES UV** : `8k_saturn_ring_alpha.png` est un ruban
  8192 x 500 RGBA dont le profil radial court le long de U et dont l'ALPHA porte
  les divisions. On pose donc **U = rayon normalisé, V = 0,5**. Conséquence
  heureuse : U ne dépend pas de l'angle, l'anneau n'a **aucune couture** à traiter.
- **DIFFUSION DES ANNEAUX, approximation DÉCLARÉE [GDD 6.8]** : un disque
  lambertien plat s'éteint dès que le Soleil rase son plan — et c'est exactement
  la configuration actuelle, le Soleil ayant traversé le plan des anneaux en 2025.
  Les anneaux ressortaient donc NOIRS. Le même matériau en ÉMISSIF les montrait
  parfaitement : c'est ce test qui a innocenté géométrie, UV et alpha, et isolé
  l'ombrage. Les vrais anneaux restent lumineux dans cette configuration parce
  qu'ils ne sont pas une surface mais des milliards de blocs de glace qui
  DIFFUSENT la lumière entre eux. On l'approche par une composante émissive tirée
  de la MÊME texture (donc respectant les divisions, sans rien inventer), dosée
  par `RingScatter` (0,45) — à 0, on retrouve le disque lambertien pur.
- **PREUVE** : `ue_corps_saturne.png` — division de Cassini, structure radiale,
  inclinaison réelle, bandes du corps et terminateur net.

**5. LES TRAJECTOIRES ET LA NAVIGATION (2026-07-27, retours d'essai).**
- **UN TRAIT FIN, PAS UN RUBAN (piège n°49).** L'épaisseur des tracés était donnée
  en unités MONDE. Or `DrawLine` construit alors UN QUAD FACE CAMÉRA PAR SEGMENT,
  et les quads consécutifs ne se raccordent pas : on voyait la chaîne de tuiles,
  ce qui se lisait en jeu comme un défaut d'ALIGNEMENT des corps. Le piège est
  vicieux car il pousse à la faute : plus on épaissit pour « mieux voir », plus
  les jointures se voient. Deux passes d'épaississement ont ainsi AGGRAVÉ le mal
  avant qu'une capture au gros plan ne montre les tuiles.
  **Épaisseur 0** fait passer le batcher sur son chemin de LIGNES FINES : un trait
  d'un pixel, continu, antialiasé, indépendant de la distance ET de la résolution.
  C'est ce que fait la référence, et la règle vaut pour TOUS les tracés de la carte
  (orbites, trajectoire de mission, corridor, nœuds) — une seule doctrine de trait.
- **512 points par orbite** (au lieu de 192) : vu de près, le polygone se voyait.
- **PAS DE TRAÎNÉE (piège n°50).** Une version a allumé la portion d'orbite que le
  corps venait de parcourir, en fondu vers l'arrière. À l'essai : « horrible ». Sur
  un trait d'un pixel un dégradé d'opacité se lit en PALIERS, pas en fondu, et la
  trajectoire semblait se déliter. La référence trace une ligne UNIFORME et elle a
  raison : une trajectoire est un LIEU GÉOMÉTRIQUE, pas un événement — ce qui doit
  attirer l'œil est le corps et sa désignation, pas un artifice sur sa courbe.
  Retiré. Seule subsiste la mise en avant de l'orbite SURVOLÉE ou focalisée (×2,4).
  Leçon : imiter une référence, c'est copier ce qu'elle fait, pas ce qu'on croit
  qu'elle fait.
- **SENS DE ROTATION CORRIGÉ.** La caméra étant en (cos p·cos y, cos p·sin y,
  sin p) et regardant le point visé, un yaw CROISSANT la déplace vers SA GAUCHE,
  donc l'objet vers la droite. Le glisser faisait `yaw -= dx` : tirer à droite
  envoyait l'objet à gauche. Passé en `+` — on SAISIT le monde et on le tire.
  Le pitch, lui, était déjà juste (tirer vers le bas monte l'œil).
- **ORIENTATION LISSÉE** (τ = 90 ms), comme la distance : le glisser écrivait
  yaw/pitch en direct, la caméra collait au pixel — donc au tremblement de la main
  — et s'arrêtait sec. COURT-CIRCUITÉE pendant un vol scripté (`cam.vol_camera`),
  sinon le handoff manque son amarrage (piège n°32).

**6. LA NETTETÉ DE TOUTE L'IMAGE — TROIS CAUSES EMPILÉES (2026-07-27).**
Le ciel paraissait flou et les traits crénelés. La texture, elle, était intacte
(8192 x 4096, groupe Skybox, mip 0, jamais streamée, aucun biais ; le groupe
Skybox du moteur ne borne rien : MaxLODSize 16384, LODBias 0). Il a fallu trois
corrections, dont la dernière est la principale et explique aussi les traits.

- **(a) LE MAILLAGE DE LA VOÛTE.** Elle prenait la sphère du MOTEUR, ~700
  triangles. Sur une carte ÉQUIRECTANGULAIRE l'interpolation d'UV est linéaire par
  triangle alors que la projection ne l'est pas : les dérivées d'UV par pixel
  explosent et UE choisit un mip grossier. Elle prend désormais `SM_SP_Body`
  (64 512 triangles, UV lat-long exactes). *Une carte 8K sur 700 triangles ne vaut
  pas mieux qu'une carte 1K : la densité du maillage fait partie de la résolution.*
- **(b) BC1 SUR UN CHAMP D'ÉTOILES (piège n°51).** `TC_DEFAULT` compresse en BC1 :
  DEUX couleurs par bloc de 4x4, en 5:6:5. Sur une image continue (une planète)
  c'est invisible ; sur des points isolés d'un pixel sur du noir, c'est le cas
  PATHOLOGIQUE du format — l'étoile est moyennée avec le noir de son bloc, elle
  s'étale et perd son éclat. Passé en **BC7**. L'ancien moteur Vulkan, lui, lisait
  le JPG en RGBA brut : d'où sa netteté, et c'est ce témoignage qui a mis sur la
  piste. *Choisir la compression d'après le CONTENU de l'image, pas par défaut.*
- **(c) L'ANTI-ALIASING TEMPOREL (piège n°52) — LA CAUSE PRINCIPALE.** Le projet ne
  fixait AUCUN réglage d'AA : UE5 appliquait donc **TSR**, qui reconstruit l'image
  à partir des frames précédentes. Or toute cette carte EST du détail sub-pixel :
  étoiles d'un ou deux pixels, traits d'orbite d'un pixel. TSR les dissout — ciel
  « flou » ET trajectoires qui fourmillent. Passé en **FXAA** (purement spatial,
  aucune mémoire de frame) + `r.ScreenPercentage=100`, dans `DefaultEngine.ini`.
  **LA PREUVE QUI TRANCHE, et elle était sous les yeux depuis le début** : dans
  TOUTES les captures, le TEXTE du HUD est parfaitement net alors que la 3D est
  molle. Le HUD est du Slate, dessiné APRÈS le post-traitement. La mollesse ne
  pouvait donc pas venir des textures ni des maillages — elle venait du pipeline.
  *Quand une partie de l'image est nette et l'autre non, la frontière DIT où
  chercher. Comparer les deux au lieu d'accuser la moitié floue.*
  Bénéfice collatéral : les corps aussi gagnent en finesse (structures nuageuses,
  amas de lumières urbaines de la Terre).

RESTE, et c'est géométrique : 8192 px sur 360° font ~23 px/degré quand un écran
4K en affiche ~85 à 45° de champ. Au très gros plan sur le ciel, la source finira
par manquer — le remède serait alors des étoiles en POINTS depuis un catalogue
réel (Hipparcos), plus juste ET plus net que n'importe quelle photo.

**RESTE** : une petite encoche au pôle de la sphère lat-long (singularité du
maillage), visible seulement au très gros plan sur Io. Mineure.

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
| `UEBridge/SPHud` | HUD natif : couche carte/station peinte + menu, modales, **postes interactifs** (`SSPPoste`) et **bandeau du temps** (`SSPTemps`, haut-droite, partout dans le Monde) en widgets Slate |
| `UEBridge/SPPlayerController` | entrée native par scène + `ASPGameMode` (`GlobalDefaultGameMode`) |
| `UEBridge/SPSky` | voûte étoilée, visible dans toutes les scènes |
| `UEBridge/SPCameraPost.h` | **l'image du monde unique** : le post-traitement partagé par les DEUX caméras (piège n°30) |
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

Payés au handoff continu (incr. 3c-3, 2026-07-26). Les quatre premiers ont la même
morale : **un seul monde impose que TOUT converge à la jonction** — géométrie,
champ, exposition, éclairage. Ce qui diverge fait une coupure, même quand la
position est juste au pixel.

28. **`FindConsoleVariable("r.SetNearClipPlane")` rend TOUJOURS nullptr** : c'est
    un `FAutoConsoleCommand` (UnrealEngine.cpp), pas une variable, et
    `FindConsoleVariable` appelle `AsVariable()` sur l'objet trouvé. Le
    « near-clip adaptatif » de la carte, présenté comme une brique du ch.18,
    **n'a donc jamais rien fait** : tout ce qui a été vérifié en capture l'a été
    avec le plan par défaut du moteur (`UEngine::NearClipPlane` = 10 u = 10 cm).
    Retiré plutôt que réparé, et c'est le bon choix : la profondeur d'UE5 est en
    reversed-Z flottant (précision maximale PRÈS de l'œil, là où sont les seuls
    objets géométriques — les corps lointains sont des marqueurs HUD), et surtout
    **l'intérieur ambulable rend avec CE plan** : un plan adaptatif rendrait le
    handoff visible, les deux côtés de la reprise ne clippant pas au même endroit.
    Leçon générale : un réglage moteur posé par CVar doit être VÉRIFIÉ comme
    appliqué, sinon on documente une intention pour une réalité.
29. **Une lumière n'est pas de la géométrie : ne pas lui appliquer le borneur de
    précision.** La position du Soleil passait par `R()`, plafonnée à
    `RENDER_MAX_UU` = 1e6 u. Le plafond protège la précision GPU des PRIMITIVES ;
    appliqué à la lumière, il la ramenait à 10 km de l'œil (échelle réelle) alors
    que le corps regardé est à des milliards d'u — éclairage venant de l'œil, donc
    **plus de terminateur**. Symptôme trompeur : la planète paraissait « délavée »,
    ce qui ressemble à un problème de matériau ou d'exposition. Repère : à 1 u =
    1 km le même plafond valait 1e6 km et la direction restait bonne — la
    régression est née du passage à l'échelle réelle, silencieusement.
30. **UN SEUL MONDE = UN SEUL POST-TRAITEMENT.** La caméra de la carte fige son
    exposition (EV100 = 0, sinon un ciel noir semé d'étoiles s'ouvre à fond) ; la
    caméra de bord, neuve, gardait l'auto-exposition du projet. Deux politiques
    d'exposition dans une scène unique = un saut de luminance à la reprise du vol
    [M] (mesuré : +18/255). Le réglage vit maintenant dans `UEBridge/SPCameraPost.h`
    et les DEUX caméras l'appellent : la faute devient impossible au lieu d'être
    corrigée après coup. À rappeler pour toute troisième caméra.
31. **Le CHAMP DE VISION fait partie de la pose.** Position, orientation et
    géométrie concordaient au pixel, et l'image de la reprise était quand même
    2,4 fois plus grosse : la carte cadre à 45°, la caméra de bord à 90°, soit
    `tan(45°)/tan(22,5°)`. Le handoff mélange donc aussi le champ — **en tangente
    de demi-champ**, la grandeur linéaire à l'écran (interpoler les degrés fait
    « respirer » le zoom en milieu de course).
32. **Ne pas lisser ce qui est déjà une trajectoire.** Le rendu possède son propre
    lissage de la distance de vue (τ = 0,35 s) : indispensable quand la cible SAUTE
    (un clic sur un corps devient un vol), néfaste quand la cible est déjà un vol
    lissé — le retard laissait la caméra plusieurs mètres avant l'amarrage, et la
    coupure revenait. D'où `cam.vol_camera` : sous ce drapeau le rendu SUIT la pose
    publiée, distance ET point visé (le lissage du point visé était pire encore :
    `FMath::VInterpTo` ne colle qu'à `UE_KINDA_SMALL_NUMBER`, soit 10 m en unités
    km — grossier devant un amarrage à 20 m).
33. **Le plancher de distance de vue était resté à 1 km**, hérité de
    l'avant-échelle-réelle. À l'échelle réelle le zoom doit descendre à l'INTÉRIEUR
    de Novellus (amarrage à ~20 m) : il aurait bloqué la caméra 50 fois trop loin.
    Passé à 1 mm. Repère : après un changement d'échelle, relire TOUS les clamps.
34. **`ready` ne doit pas dire deux choses.** `station_out.ready` valait « le
    joueur a la main » ; or la session s'en sert pour savoir où RAMENER la caméra.
    Lié aux commandes, il retombait sur le point d'apparition à chaque retour au
    lieu de l'endroit quitté. Il signifie maintenant « la scène existe », et la
    pose est publiée dès la construction (sinon `-spscene=map` mélangerait le
    regard vers un cap nul).
35. **La coque n'occulte rien si personne ne porte d'ombre.** Tout est en
    `SetCastShadow(false)` (coût) : le Soleil de la carte éclairait donc
    l'intérieur À TRAVERS la coque pendant la coexistence. L'occultation est un
    FAIT physique — on l'exprime par **canal d'éclairage** (intérieur et ses seules
    lampes sur le canal 1, hors d'atteinte des lumières du plan système ; et
    réciproquement les plafonniers n'éclairent plus la Terre au hublot).
    Approximation DÉCLARÉE [GDD 6.8] : une vraie occultation demanderait des ombres.

Payés en faisant COULER LE TEMPS [GDD 14.2] (2026-07-26). Morale commune :
**rendre continue une grandeur qui était discrète réveille tout ce qui la
supposait immobile** — un arrondi, un amortisseur, un défaut d'état.

36. **`round(delta)` sur une grandeur qui devient continue = la grandeur
    disparaît.** Le tick financier mensuel comptait
    `int mois = round(a.mois - dernier_mois)`. Correct tant que le seul moteur du
    calendrier était « passer 1 mois » (delta = 1). Dès que le temps COULE, cette
    couche est appelée chaque frame avec un delta de ~1e-5 : `round` rend 0, **zéro
    tick, l'agence vivait gratuitement** — exactement ce que l'invariant de
    pression d'inactivité interdit, et un bug qu'aucune capture n'aurait montré.
    Remplacé par un comptage de FRONTIÈRES (`floor(avant)` → `floor(après)`),
    identique à l'ancien sur les sauts entiers. Règle : quand une grandeur passe de
    discrète à continue, chercher tous les `round`/`int` sur ses deltas.
37. **Le temps est une DÉPENSE : il ne coule jamais par défaut ni par héritage.**
    La cadence vit sur `Jeu`, pas sur `Agence` — elle échappait donc à
    `agence = Agence{}` et une nouvelle partie (ou un chargement) héritait la
    cadence de la précédente : on chargeait dans une partie qui défilait à un mois
    par seconde, avec ses charges. `remettre_horloge_en_pause()` est appelée à la
    fondation, au chargement et au reset, sous oracle.
38. **Un amortisseur de premier ordre garde un retard permanent sur une cible en
    MOUVEMENT.** Le suivi du point visé (`VInterpTo`, vitesse 3,5) était conçu pour
    lisser un CHANGEMENT de focus, à une époque où l'époque de jeu ne bougeait pas.
    Dès que le temps coule, la Terre défile à 29,8 km/s et le retard d'équilibre
    vaut v/vitesse ≈ 8,5 km : invisible sur une planète cadrée à 38 000 km,
    **Novellus cadrée à 1 km sortait de l'écran**. Correction : ADVECTER le point
    lissé du déplacement propre de la cible avant d'amortir le reste — le lissage
    n'amortit alors que ce qu'il doit, l'écart d'un changement de focus. (Le garde
    `FMath::VInterpTo` ne collait qu'à `UE_KINDA_SMALL_NUMBER`, soit 10 m en unités
    km : trop grossier pour servir de rattrapage.)
39. **Une frame anormalement longue est une avance que le joueur n'a pas
    demandée.** Sans borne, une compilation de shaders de 30 s à la cadence
    « mois/s » téléportait le calendrier de 30 mois, charges comprises. `dt` est
    borné à 0,25 s par frame et le surplus est PERDU, pas différé.
40. **UN MÉCANISME CORRECT ET INATTEIGNABLE EST UN MÉCANISME ABSENT.** La cadence
    a d'abord été livrée réglable au SEUL poste AGENCE — conforme au GDD, sous
    oracle, vérifié en capture… et jugé cassé à l'essai (« je ne peux toujours pas
    accélérer le temps »). Depuis le plan système il fallait revenir à bord, marcher
    jusqu'au module et ouvrir le panneau pour mettre le monde en marche. Ce que
    [GDD 14] interdit est un **curseur de temps libre**, pas un raccourci vers les
    mêmes crans costés : d'où [P] et [1-5], partout dans le Monde. Corollaire, payé
    d'un second aller-retour : **une ressource permanente demande une surface
    permanente**. Un raccourci clavier plus une ligne d'aide au fond d'un écran ne
    suffisaient pas — il fallait un BANDEAU (`SSPTemps`) en haut à droite, visible
    et cliquable partout, poste ouvert compris. Un jeu dont l'horloge n'est pas à
    l'écran passe pour figé.
    Mise en œuvre : le bandeau et ses conteneurs sont `SelfHitTestInvisible`, seuls
    ses BOUTONS prennent la souris — sinon on rejouait le piège n°6 (un panneau qui
    vole les clics du monde 3D). À bord, le curseur est capturé : ce sont les
    touches qui servent, d'où la légende dans le bandeau lui-même.
41. **Ce qui n'est pas séparable ne doit pas être désignable.** Novellus orbite à
    418 km de la Terre : au plan système les deux tombent sur le même pixel, et les
    deux libellés s'imprimaient l'un sur l'autre (« DAYRTHLUS », vu en capture)
    tandis que le picking tirait au sort entre la planète et la station. `PublishScreen`
    ne la déclare plus à l'écran que si elle se détache de son parent d'au moins la
    taille d'un marqueur — même doctrine de LOD que les corps. Piège général : deux
    objets du monde unique séparés de 4 ordres de grandeur sous la distance de vue
    fusionnent, et c'est la LISTE ÉCRAN qui doit trancher, pas le dessin.

Payés en imposant le RYTHME EN MISSION [GDD 14.3] (2026-07-27) :

42. **Le cadre d'un poste CLIPPE le texte, il ne le replie pas.** L'explication du
    plafond tenait sur une ligne de ~118 caractères : la capture l'a montrée coupée
    en plein milieu d'un mot (« elle ne se su »). Une explication tronquée vaut
    moins que pas d'explication — c'est le motif du refus qui disparaît, et le
    bouton éteint redevient une panne aux yeux du joueur. Règle : dans un poste,
    deux lignes courtes plutôt qu'une longue, et le nom affiché d'une donnée doit
    tenir dans le cadre le plus étroit qui l'accueille (d'où `FlightPhase::Edl`
    nommée « EDL », le terme du glossaire [GDD Annexe A], et non épelée).
    Corollaire : ce défaut ne se voit QUE par capture — le code, lui, était juste.
43. **Ne pas mémoriser une valeur que le joueur n'a pas choisie.** [P] mémorise la
    cadence courante pour y revenir. Sous plafond, cette valeur n'est plus celle
    que le joueur jouait mais celle que la mission impose : mettre en pause pendant
    une ascension lui faisait perdre définitivement son réglage de croisière. La
    mémoire ne se met donc à jour que HORS contrainte. Piège général : dès qu'une
    grandeur peut être BORNÉE par le modèle, tout ce qui la mémorise doit distinguer
    la valeur DEMANDÉE de la valeur OBTENUE.
44. **Une loi déduite corrige celui qui l'a écrite.** L'oracle affirmait qu'une
    phase d'un an n'imposerait aucun plafond ; la loi a répondu « semaine/s », et
    elle avait raison — à « mois/s » une année défile en 12 s réelles, sous les 20 s
    exigées. C'est l'intérêt d'un plafond CALCULÉ plutôt que tabulé : il contredit
    l'intuition quand l'intuition a tort. L'oracle a été corrigé, pas la loi.

Payés en plaçant les LUNES et en abandonnant les GLB (2026-07-27) :

45. **Une orbite de lune se recentre à l'ÉMISSION, jamais au cache.** Le cache
    d'orbites ne se refait que tous les 2 jours d'époque ; en 2 jours la Terre
    parcourt 2,6 millions de km, pour une orbite lunaire de 384 000 km. Cuire les
    points en absolu détachait donc l'anneau de sa planète de plusieurs fois son
    propre diamètre. Les points d'une lune sont RELATIFS à son parent et
    l'émission y ajoute la position courante. Règle : ce qui est attaché à un
    objet MOBILE se stocke dans SON repère.
46. **Borner chaque objet séparément détruit l'ordre des profondeurs.** Première
    version du borneur de précision : chaque corps ramené indépendamment à
    `RENDER_MAX_UU`. Résultat, la Terre et la Lune atterrissaient sur la MÊME
    sphère et s'interpénétraient (dôme pâle en travers de la Terre, vu en
    capture). Une homothétie de centre l'œil et de facteur **UNIQUE** conserve à
    la fois les tailles angulaires ET les profondeurs relatives : c'est la seule
    contraction qui ne mente sur rien. On la calibre sur le corps le plus éloigné
    effectivement rendu en géométrie.
47. **NE PAS GÉNÉRALISER UNE CORRECTION D'ASSET SUR UN SEUL ESSAI.** Ayant
    diagnostiqué des faces inversées et vérifié le retournement sur Mars, la
    correction a été appliquée aux 32 corps d'un coup. Mars s'est retrouvée
    retournée DEUX FOIS. L'outil ne retourne plus à l'aveugle : il MESURE
    l'orientation de chaque corps (vote sur 64 triangles, n·c > 0 pour une sphère
    fermée) et ne corrige que ce qui regarde dedans — donc idempotent. Il a
    d'ailleurs rattrapé tout seul le double retournement. Règle : une passe de
    masse sur des assets se conditionne à une MESURE, jamais à une hypothèse
    validée sur un cas.
48. **Les `unreal.log` des commandlets vont dans `Saved/Logs/SP.log`, PAS dans la
    sortie standard.** Une console muette a fait conclure à tort qu'un script
    n'avait rien fait, et fait ajouter un `scan_paths_synchronous` qui n'était pas
    le problème (il reste, en filet de sécurité). Vingt minutes perdues : lire le
    log du projet AVANT de diagnostiquer un outil silencieux.

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
  ×100 avec miroir en y. Gabarit et pose d'apparition vivent en **C++ pur**
  (`app/postes.hpp` : `STATION_ENVERGURE_M`, `NOVELLUS_OEIL_M`, cap) parce que la
  session en a besoin pour la pose d'amarrage du handoff — un chiffre, une source.
- **UNE SEULE IMAGE POUR UN SEUL MONDE** (2026-07-26) : les deux caméras qui
  regardent la scène unique (plan système et 1re personne) partagent le MÊME
  post-traitement, `UEBridge/SPCameraPost.h` — exposition figée à EV100 = 0, bloom,
  ni flou ni grain, motion blur coupé. Toute nouvelle caméra doit l'appeler
  (piège n°30). Le **plan de clipping proche est celui du moteur** (10 cm),
  identique de bout en bout du zoom : c'est ce qui permet à la reprise en 1re
  personne de clipper au même endroit que le plan système (piège n°28).
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

~~1. Porter le contenu de `ares_ecrans.hpp` dans les postes de l'ISS → supprimer le fichier.~~
~~2. Amputer `jeu_ecrans.hpp` de ses écrans morts → supprimer `hud.hpp`, `panels.hpp`, `implot`.~~
   — **SANS OBJET (constaté le 2026-07-26)** : `Source/SP/SpaceProgram/` ne contient
   plus que `app/ ares.hpp bridge_flags.hpp jeu.{hpp,cpp} postes.hpp scaled_space.hpp
   session.hpp vehicle_design.hpp`, `astro_core/`, `mission/`, `tests/`. Il n'y a
   plus de dossier `ui/` dans le module : `jeu_ecrans.hpp`, `hud.hpp`, `panels.hpp`,
   `ares_ecrans.hpp` et ImPlot ne vivent que dans `_archive/` — hors build, donc
   sans poids. Le contenu est passé dans les postes natifs (`SSPPoste`).
3. ~~Construire la boucle de mission cible (jalon D) → scinder `jeu.cpp`.~~
   — **fait** (2026-07-26) : boucle cible = `MissionLoop.hpp` ; `jeu.cpp` scindé
   (vol 2D retiré, cf. §5.3).
4. **Purger `_archive` sauf `build_vk/` — SEULE ÉTAPE RESTANTE, à décider par
   l'utilisateur (suppression, donc pas faite d'office).** Tailles mesurées le
   2026-07-26 : `build/` 62 Mo, `dist/` 33 Mo, `extern/` 20 Mo (≈ 115 Mo à
   récupérer) ; **à GARDER : `build_vk/` 34 Mo**, qui porte le binaire de référence
   et ses shaders — indispensable aux captures de référence. Le reste (`app/`,
   `ui/`, `astro_core/`, `mission/`, `scripts/`, `_backup_render/`) pèse < 1 Mo et
   documente le prototype. `_archive` est SUIVI PAR GIT (652 fichiers) : une
   suppression reste donc récupérable dans l'historique.

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
   **MAJ 2026-07-26** : facettage, orientations IAU, anneaux de Saturne et
   jour/nuit sont FAITS (cf. §2) ; le terminateur, cassé par l'échelle réelle, est
   réparé (piège n°29). Restent sur cette ligne : la **coquille de nuages low-poly**
   de la Terre (facettes translucides visibles au gros plan), l'atmosphère de Vénus
   et le **placement des 19 lunes** (présentes dans les GLB, pas encore posées).
~~6. **ISS extérieure** sur la carte + « [M] ENTRER DANS L'ISS »~~ — **fait**
   (incr. 3c-2/3c-3) : modèle Nanite à l'échelle réelle, focalisable et cliquable,
   label « [M] ENTRER », et **entrée sans coupure** jusqu'à l'ambulation.
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

---

## 7. LE PROCHAIN PAS (état au 2026-07-27, après le rythme en mission)

Le monde unique 1:1 est **fait de bout en bout** : plus une seule coupure de scène
entre le plan système et l'ambulation dans Novellus (incr. 1 → 3c-3). Ce qui reste,
par ordre de valeur :

~~1. LE TEMPS QUI COULE~~ — **fait le 2026-07-26** (voir la ligne dédiée du tableau
   §2). Le prédit s'est produit : le retard de poursuite du point visé était réel
   (piège n°38), et faire couler le temps a réveillé deux autres suppositions
   d'immobilité (pièges n°36 et 37).

~~1. LE RYTHME DU TEMPS EN MISSION~~ — **fait le 2026-07-27** (voir la ligne dédiée
   du tableau §2). La MÉCANIQUE DE PHASE annoncée est en place et sous oracle : la
   phase de vol est dérivée, le plafond est déduit de sa durée propre, et il
   s'applique par une porte unique. Ce qui manque n'est plus le mécanisme mais la
   CHRONOLOGIE : seule l'ascension est datée, faute d'une trace de vol.

Reste, par ordre de valeur :

~~1. Les 19 lunes et la coquille de nuages de la Terre~~ — **fait le 2026-07-27**
   (section « LES CORPS » du §2). Les lunes sont placées, sous oracle ; la coquille
   de nuages n'existe plus (les nuages sont une carte du matériau) ; et les corps
   ne sont plus des GLB mais une sphère faite par UE, texturée depuis
   `assets/textures`. Les ANNEAUX de Saturne sont faits par UE eux aussi (rayons
   réels, UV radiales, alpha des divisions). Reste une encoche au pôle de la
   sphère lat-long, visible au très gros plan seulement.
2. **Le vol MANUEL des missions vécues** [GDD 9] : le joueur exécute les manœuvres
   au lieu d'un tirage déterministe. C'est ce qui réveillera le tracé
   vaisseau/corridor de `SPSolarSystem`, en place mais endormi (§5.3) — **et c'est
   ce qui DATERA l'insertion et l'EDL**, donc ce qui fera mordre le plafond de
   cadence ailleurs qu'à l'ascension (`MissionTempo` les connaît déjà : il ne leur
   manque qu'une date). Les deux chantiers se rejoignent ici.
3. **Ch.15 restant** : solveur `ares::vol` sur la nav réelle, éditeur de graphe
   (Normal), toolchain C++ embarquée + bac à sable (Pro). Bloqué tant qu'il n'y a
   pas de source de solution de navigation de premier ordre — donc après le point 2.
4. **Tri §5.4 étape 4** : purger `_archive` sauf `build_vk/` (≈ 115 Mo) — décision
   de l'utilisateur, pas faite d'office.

À SURVEILLER maintenant que le temps coule : le tick de recherche est appelé une
fois par frame avec le total quantifié, pas une fois par sous-pas (approximation
déclarée dans `jeu.hpp`) ; et `AresLayer::avancer` — donc `livrer_courrier` — tourne
désormais à chaque frame au lieu d'une fois par mois. Correct, mais si le catalogue
grossit beaucoup, c'est là qu'il faudra un déclencheur par frontière de mois.
