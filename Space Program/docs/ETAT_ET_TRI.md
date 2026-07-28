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

**`-spvol[=<phase>]`** **épingle une mission EN VOL** : c'est l'oracle visuel du
RYTHME IMPOSÉ [GDD 14.3] (§2), un instant qui ne s'atteint autrement qu'en jouant
toute la boucle de mission. Comme `-sphandoff`, il pose l'ÉTAT DU MODÈLE et non
le pont — sinon la capture ne prouverait que l'existence du drapeau. À combiner
avec `-spcadence=4` : la demande « mois/s » doit ressortir bornée au temps réel.
`<phase>` = `ascension` (défaut) · `parking` · `injection` · `croisiere` ·
`insertion` · `edl`. Depuis que la chronologie DATE le vol (§2, « LA CHRONOLOGIE
DE VOL »), ces instants EXISTENT et se capturent donc. La famille du contrat est
choisie pour PORTER la phase demandée (EDL ⇒ `surface`), la durée de transit est
celle de la VRAIE fenêtre, et la position dans la chronologie est **calculée**
(milieu du segment visé), jamais un décalage écrit à la main — le jour où une
durée change, la capture suit toute seule. `injection` et `insertion` sont la
MÊME phase (manœuvre critique) à deux instants : la première et la dernière.

**`-spcode[=vol]`** **ouvre l'ATELIER LOGICIEL du mode PRO** [GDD 15.1, 15.5] :
la partie de capture démarre en PRO, et le poste CONTRÔLE s'ouvre sur sa face
éditeur. `-spcode=vol` reste sur la CONDUITE DE MISSION, en PRO — l'autre face,
celle où le logiciel embarqué décide. Même office que `-spvol` : le mode d'aide
se choisit à la création d'une partie, écran qu'une capture ne traverse pas ;
sans ce drapeau, l'éditeur serait un écran que rien ne peut photographier.
À combiner avec `-sppost=3`.

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
| **LE RYTHME DU TEMPS EN MISSION [GDD 14.3]** : une phase critique IMPOSE un plafond de cadence | **FAIT (2026-07-27), VÉRIFIÉ PAR CAPTURE.** « Toute manœuvre fine RAMÈNE le temps à un rythme lent » — le verbe du GDD est actif, donc le plafond est OPPOSABLE, pas suggéré. **`mission/MissionTempo.hpp`** (C++ pur) le DÉDUIT au lieu de le saisir : à la cadence r, une phase de durée propre D disparaît en D/r secondes réelles ; on exige qu'elle en dure au moins `OBSERVATION_MIN_S` = 20 s (SEUL paramètre libre de la loi, déclaré [GDD 6.8]), d'où le cran le plus rapide admissible. Les DURÉES sont des grandeurs sourcées, pas des réglages : ascension ~9 min (Falcon 9 SECO T+8 min 40), EDL ~7 min (MSL), insertion ~10 min (Apollo LOI 6 min 2 s). Aux cinq crans de `TimeRate`, toute phase < ~10 jours retombe sur le TEMPS RÉEL — résultat attendu, mais CALCULÉ, donc il se déplacera tout seul quand une phase durera des semaines. PLANCHER : jamais la pause — le modèle empêche d'aller trop vite, il ne fige pas le monde à la place du joueur [GDD 14]. **La phase de vol est DÉRIVÉE** (`flight_phase_of` : état FSM + temps passé dedans + famille) : `Mission::phase` était un drapeau que RIEN ne renseignait (piège n°20b) et que seule une saisie manuelle aurait pu remplir ; elle est maintenant vivante, rejouable, et rien de plus à sauvegarder. Le prédicat de phase critique est **partagé avec `Events.hpp`** (taux d'anomalie) : une seule définition pour deux lois qui disent la même chose. Enforcement à DEUX niveaux, pour que la faute soit impossible et non corrigée après coup : `Jeu::regler_cadence()` est la porte unique (les 4 écritures — bandeau, poste AGENCE, touches [P]/[1-5], `-spcadence` — y passent) ET `faire_couler_le_temps()` rappelle le plafond avant de convertir la moindre seconde. Le feu vert d'une mission appelle `appliquer_plafond()` DANS SA FRAME : c'est la manœuvre qui freine le monde, pas le joueur qui doit y penser. AFFICHÉ sur les trois surfaces (piège n°40) : bandeau `SSPTemps` (crans fermés en rouge + « RYTHME IMPOSE : ASCENSION »), poste AGENCE (boutons éteints + motif en toutes lettres), et la barre de la carte qui montre la cadence RÉELLEMENT appliquée. **PREUVE** (`-spvol`, nouveau drapeau §1) : deux captures à 900 frames, même `-spcadence=4` demandé — libre, le monde avance de 3 mois (OCT 27 2026, cran MOIS vert) ; sous ascension, il ne bouge pas (JUL 26 2026, `REAL RATE`, JOUR/SEM/MOIS en rouge). `ue_tempo_libre.png`, `ue_tempo_impose.png`, `ue_tempo_poste_agence.png`. RESTE : ~~l'insertion et l'EDL ne sont pas encore DATÉES~~ — **FAIT le 2026-07-27**, voir la ligne suivante |
| **LA CHRONOLOGIE DE VOL [GDD 4.1, 9, 14.3]** : le vol DURE, et ses phases ont une date | **FAIT (2026-07-27), VÉRIFIÉ PAR CAPTURE.** `mission/FlightTimeline.hpp`. « Lancer » et « débriefer » étaient deux clics : un vol vers Mars ne consommait PAS UNE SECONDE de temps de jeu, et une phase sans date n'arrive jamais — d'où un plafond de cadence qui ne mordait qu'à l'ascension. Le vol a désormais une suite de segments jointifs depuis le feu vert (ascension → parking → injection → croisière → **insertion** ou **EDL** → exploitation), et **`flight_phase_of` LIT cette chronologie** au lieu de deviner. **AUCUNE DURÉE N'EST UN RÉGLAGE** : les trois durées critiques restent sourcées (Falcon 9 / MSL / Apollo LOI) et **tout le reste est dérivé par Kepler** de l'orbite concernée — une révolution de parking à 200 km (88,5 min), le transit GTO comme demi-période d'ellipse (5 h 15), le phasage de rendez-vous comme profil à 4 orbites. **L'orbite géostationnaire n'est même pas un chiffre** : `geo_radius_m()` = (µ/ω²)^⅓ sur la rotation SIDÉRALE, et on retrouve 42 164 km sans l'avoir écrit. **La croisière interplanétaire n'est pas inventée du tout** : c'est la DURÉE DE TRANSIT de la fenêtre réellement visée — `astro::WindowResult` la calculait déjà (c'est l'axe des durées du porkchop) **sans jamais la publier**, elle ne répondait qu'à « quand partir ? ». Fenêtre 2026 : optimum 310 j, transfert disponible **329 j**. **CE QU'ON NE SAIT PAS CALCULER EST DÉCLARÉ** [GDD 6.8] : une famille dont le contrat ne nomme pas de cible (« science ») et une spirale NEP n'ont PAS de date d'arrivée (`dated == false`), et un vol non daté ne bloque rien. **CONSÉQUENCE, et c'est le point** : `Launched → Debrief` a maintenant un **GATE D'ARRIVÉE** dont le refus CHIFFRE l'attente (« vol en cours : arrivee dans 249 jours »), et le plafond de cadence mord enfin **à l'insertion et à l'EDL**. **PREUVE** : `-spvol=insertion -spcadence=4` → bandeau « RYTHME IMPOSE : MANOEUVRE CRITIQUE », cran REEL vert, JOUR/SEM/MOIS rouges, calendrier immobile (`ue_chrono_insertion.png`) ; `-spvol=edl` → « RYTHME IMPOSE : EDL » (`ue_chrono_edl.png`) ; `-spvol=croisiere` → cran MOIS vert, le monde avance de 4 mois pendant que le poste CONTROLE affiche « PHASE DE VOL : CROISIERE / ARRIVEE : dans 165 jours » (`ue_chrono_poste_controle.png`) |

Oracles hors moteur — **compteurs RELEVÉS en exécutant les 11 suites le
2026-07-27, après la chronologie de vol.** Ne pas les recopier : les remesurer.
Le relevé précédent était faux de plus du double, et pas d'un peu — il donnait
300 à `test_astro_core` là où la suite en imprime 1 626. Un compteur recopié de
mémoire ne vaut rien ; seule la sortie du binaire compte.

| Suite | Oracles |
| :--- | ---: |
| `Space Program/tests/test_astro_core.cpp` | **1 626** (+6 pour la durée de transit de la fenêtre) |
| `tests/test_contenu_gdd.cpp` | 612 |
| `tests/test_session.cpp` | **470** (222 avant, **+42** chronologie, **+29** trace, **+65** navigation et manœuvre, **+9** graphe, **+43** logiciel de vol du mode Pro, **+28** le prix de l'inaction, **+12** délai de communication, **+7** boucle sol, **+13** rythme de mesure) |
| `tests/test_carte_flotte.cpp` | 135 |
| `tests/test_reentry_perturb.cpp` | 108 |
| `tests/test_api_sol.cpp` | 59 |
| `tests/test_ares_modules.cpp` | 58 |
| `tests/test_gdd_manques.cpp` | 57 |
| `tests/test_mission_loop.cpp` | **60** (+7 : le logiciel de vol hors domaine) |
| `tests/test_economie_v12.cpp` | 46 |
| `tests/test_code_qualif.cpp` | **25** (+3 : la dilution du domaine) |
| `tests/test_toolchain.cpp` | **24** (toolchain embarquee, bac a sable, job object, horizon) |
| **TOTAL** | **3 280**, tous au vert |

⚠ `test_mission_loop` et `test_session` ont besoin de **`app/jeu.cpp`** en plus des
TU du cœur (ils incluent `app/session.hpp`) : sans lui, huit symboles de
`fen::app::Jeu` manquent au lien. Et `/Fo:` sur un dossier exige que le dossier
EXISTE (`fatal error C1083` sinon) — deux minutes perdues chacune.

⚠ `test_session` a besoin en plus de **`mission/src/Toolchain.cpp`** (depuis que
`Session` pilote la chaîne du mode Pro) et de **tout `astro_core/src/*.cpp`**, pas
des cinq TU seuls : `nav_solution` tire `OrbitDetermination.cpp`, qui tire
`Propagator.cpp`, qui tire les intégrateurs. `test_toolchain` a besoin de
`Toolchain.cpp` **et** de `Kepler.cpp`, et prend la racine du dépôt en argv[1] ;
`test_session` aussi, pour la même raison.

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

**7. L'ORBITE ET L'ATTITUDE RÉELLES DE NOVELLUS (2026-07-27).**
La station était publiée par le helper de la FLOTTE (`flotte_position_rel`, type
`RelaisGeo`) : un cercle képlérien dans le **plan écliptique**. Le rayon était bon,
donc la période aussi — mais le PLAN était faux de 51,6°, et un plan faux, c'est
une Terre qui défile n'importe où sous la cupola. Elle gardait de plus un **cap
fixe dans l'inertiel**, donc sa cupola balayait le vide, la Terre, le vide.

- **LE MODÈLE, EN C++ PUR** : `app/novellus_orbite.hpp`. Deux chiffres seulement
  sont SAISIS — altitude 418 km, inclinaison 51,64° — et tout le reste en DÉCOULE :
  la période par la 3e loi (5 576 s = **92,9 min**, la valeur réelle de l'ISS), la
  vitesse circulaire (7,66 km/s), et la **régression du nœud par J2**
  (−3/2·J2·(R/p)²·n·cos i = **−4,95°/jour**, un tour de plan en 73 jours — ce qui
  commande le cycle bêta et l'heure locale des survols). L'inclinaison se compte
  sur l'**équateur terrestre**, qui est lui-même à 23,44° de l'écliptique : le
  repère équatorial est bâti sur `ephem::equatorial_to_ecliptic`, la primitive
  déjà utilisée pour l'orientation des corps, pas sur une obliquité recopiée.
- **LA VITESSE EST ANALYTIQUE**, plus une différence finie : exacte, et exacte
  quelle que soit la cadence du temps. *Une dérivée numérique sur une horloge
  accélérée n'est pas une approximation, c'est une valeur fausse : à mois/s une
  frame avance de huit orbites LEO, et la corde ne dit plus rien de la tangente.*
- **L'ATTITUDE A TROIS CONSOMMATEURS, DONC UNE SEULE SOURCE.** Elle était calculée
  côté rendu pour le seul modèle extérieur. Or la géométrie INTÉRIEURE et le
  modèle extérieur **se relaient** à la traversée de la coque (bascule de LOD du
  vol [M]) : l'intérieur non tourné faisait PIVOTER la station à l'écran à cet
  instant précis — la coupure même que ce mécanisme existe pour supprimer. Et la
  pose de caméra du handoff (`Session::pose_bord`) doit la porter aussi, sinon la
  caméra sort de la station par un flanc qui n'est plus le bon. Le modèle la
  publie donc sur le pont (`StationWorld::att_*`, trois vecteurs déjà mirrorés en
  y) et les trois la LISENT. Le repère du modèle (+X avant, +Y tribord, +Z zénith)
  est MESURÉ par `Tools/diag_iss_repere.py`, pas deviné.
- **LE BOUT « BORD » DU VOL SUIT L'ATTITUDE VIVANTE.** La pose d'amarrage était
  figée au départ du vol ; elle ne peut plus l'être sur une station tournante — un
  vol dure 0,9 s réelles, soit des centaines d'orbites à pleine cadence.
  `publier_camera_vol` la resynchronise chaque frame ; le déplacement est pondéré
  par (1 − lissage(progrès)), donc sans à-coup et exact à l'arrivée.
- **PREUVE.** Oracles `test_session` **200/200** (dont l'inclinaison mesurée sur
  `r × v` contre le pôle IAU, le retour après une période égal à la dérive J2
  a·|dΩ/dt|·T = 38 km, et le nadir tenu sur 64 points d'une orbite entière).
  En rendu : le transform réellement appliqué donne
  `cupola · versTerre = 1,000000000` (**0,000000°**) sur toutes les frames à
  mois/s. Et le handoff reste invisible : `-sphandoff` contre `-spscene=iss`,
  décalage optimal **dx = 0, dy = 0**. *Caméra et géométrie subissant la même
  rotation rigide, l'image est inchangée — c'est ce qui permet d'appliquer
  l'attitude hors du repère canonique sans toucher à la collision des 310 corps.*

**8. LE MONDE REND AUSSI À BORD — LA TERRE PAR LA CUPOLA (2026-07-27).**
Dernier endroit où Novellus restait un MONDE À PART : à bord, `SPSolarSystem` ne
tickait pas. On était dans une boîte noire, et l'attitude « cupola au nadir »
n'avait aucun témoin. C'est le point 7 qui devient visible.

- **LE REPÈRE DE RENDU N'EST PLUS TOUJOURS L'INERTIEL.** À bord, la caméra est
  celle du PAWN et la station rend dans son repère CANONIQUE — c'est là que le
  joueur marche et que vit la collision des 310 corps, et on ne va pas remuer
  310 corps de collision par frame pour faire tourner un décor. On tourne donc le
  MONDE de l'inverse de l'attitude (`GetRenderRot`) : **une** rotation, appliquée
  à une cinquantaine de positions. *Tourner la caméra et la géométrie ensemble, ou
  tourner tout le reste en sens inverse, c'est le même changement de repère* —
  d'où un handoff qui reste invisible alors que les deux côtés de la reprise
  emploient des conventions différentes.
- **« LE MONDE REND » ≠ « LA CARTE A L'ŒIL ».** `SetMapActive` (visibilité) et
  `SetMapHasEye` (caméra + remises à zéro du cadrage) sont séparés, exactement
  comme `SetStationVisible` / `SetStationInControl` le sont déjà côté station. À
  bord : orbites, marqueurs, labels et modèle extérieur ÉTEINTS — ce sont des
  symboles de carte, et on est DANS le monde, on ne le survole pas.
- **LA CUPOLA EST OUVERTE.** `Tools/diag_iss_cupola.py` la situe à (−18,2 ; +7,2 ;
  −5,4) m sous Node 3, et deux repères NOMMÉS confirment l'axe indépendamment
  (`Node1_..._Hatch_Nadir` à −3,05 m du centre du nœud contre `..._Hatch_Zenith`
  à +2,63) : **−Z = nadir**, la même convention que le modèle extérieur. Le même
  diag mesure que `Cupola_Glass` est en **BLEND_OPAQUE** — la vitre est un mur, et
  aucun réglage d'instance n'y peut rien (le mode de fusion appartient au matériau
  compilé). Les 7 vitres et les 7 volets ne sont donc pas rendus : c'est l'état
  NOMINAL de la cupola en observation, volets ouverts et vitre propre.
- **LE PIÈGE, ET IL ÉTAIT DÉJÀ LÀ (n°53).** L'homothétie unique des corps était
  calibrée sur le corps le plus LOINTAIN rendu en géométrie. Depuis Novellus, la
  dynamique est démente — Terre à 6 796 km, Soleil à 1,5e8, rapport 22 000 — et le
  facteur tombait à 6,6e-8 : **la Terre était rendue en sphère de 42 cm à 45 cm de
  l'œil**. Position juste (mesurée : le centre tombe pile au nadir), taille
  angulaire juste (70°), et RIEN à l'écran — tout l'hémisphère visible tenait entre
  2,7 et 5,5 u de profondeur, donc entièrement devant le plan de clipping proche
  (10 u). On voyait les étoiles À TRAVERS la Terre. Ce n'est pas un défaut du rendu
  à bord : il frappait déjà tout cadrage serré depuis un objet proche, et c'est
  pourquoi la Terre manquait aussi sur les gros plans de Novellus.
  Le facteur est maintenant **borné par le bas** par ce que le premier plan exige
  (`RENDER_MIN_UU` = 100 m sous la surface la plus proche) et le lointain se
  rattrape par une COURBURE (`RemapDist`, hyperbole strictement croissante) au lieu
  d'un écrasement uniforme — donc pas de retour de l'interpénétration que la borne
  individuelle avait causée. *Toute mise à l'échelle RADIALE de centre l'œil laisse
  la projection exactement invariante : changer de facteur ne change pas l'image,
  seulement la profondeur — c'est-à-dire uniquement ce qui était cassé.*
- **NOUVEAU DRAPEAU `-spoeil=x,y,z[,yaw,pitch]`** : pose l'œil du pawn dans la
  station. Le jeu fait apparaître le joueur dans Novellus ; la cupola est à 38 m
  de là, derrière deux nœuds, et une capture ne peut pas y aller à pied. Piège
  payé au passage : `FParse::Value` s'arrête sur une VIRGULE par défaut
  (`bShouldStopOnSeparator`, Parse.h:71) et ne rendait que le premier champ.
- **PREUVE.** `-spscene=iss -spoeil=-18.2,7.2,-4.6,0,-80` : la **Terre remplit les
  sept fenêtres de la cupola**. Aucune régression : vue système et corps focalisé
  identiques, module pressurisé identique (aucune fuite de lumière), handoff
  toujours superposable au pixel (**dx = 0, dy = 0**), oracles 200/135/1622 au vert.

**RESTE** : le Soleil et la Lune vus de la cupola tombent dans la partie courbée
du remappage — ils sont à la bonne place et à la bonne taille angulaire, mais leur
profondeur est comprimée ; sans conséquence tant qu'ils ne se croisent pas à
l'écran (une éclipse serait le cas limite). Et la vitre n'a ni reflet ni
réfraction : cela demande un vrai matériau translucide, donc un travail d'asset
(`Tools/`), pas de code.

**9. LES 8 POSTES DANS LEUR MODULE (2026-07-27).**
Le sous-titre de chaque poste NOMME son module depuis le premier jour
(« COUPOLE — TRANQUILITY . OBSERVATION »), mais les huit étaient alignés le long
du couloir à 1,7 m d'intervalle à partir du point d'apparition : la station était
un décor de panneaux flottants, pas un lieu. Et la cupola, que le point 8 vient de
rendre habitable, n'avait rien à y faire.

- **LES POSITIONS SONT MESURÉES** (`Tools/diag_iss_modules.py`) : inventaire des
  310 meshes groupés par préfixe, avec pour chaque module la **médiane** des
  centres — robuste à l'intrus isolé, et il y en a un (`Cupola_Int_Glass` est à
  16 m des six autres pièces de la cupola et faussait à lui seul le centre de sa
  boîte englobante ; la médiane ne bouge pas pour un intrus sur onze).
  *Les sigles de l'asset ne sont pas ceux du GDD* : un premier essai rendait
  « Destiny : absent » et « Zvezda : absent » — Destiny y est `US_Lab_*`, Kibo
  `JPM_*`. Chercher le nom du GDD dans un asset importé ne prouve rien ; il faut
  lire l'inventaire.
- **DEUX MODULES N'EXISTENT PAS**, et c'est déclaré [GDD 6.8] : `ISS_Internal` ne
  couvre que le **segment américain** (de BEAM à Kibo). ZVEZDA et ZARYA n'y sont
  pas ; AGENCE et ANALYSE restent donc dans NOVELLUS, échelonnées le long de son
  axe. Six postes sur huit rejoignent leur module, deux gardent le statu quo — pas
  de régression, et le sous-titre continue de nommer le module visé.
  Au passage : **NOVELLUS**, le module fictif du jeu, EST dans ce modèle la copie
  de Kibo posée en avant de Node 2 (`JPM_*_001`, x ≈ 11,8 à 22,8 m) — là où le jeu
  de référence plaçait déjà son point d'apparition.
- **UE VÉRIFIE LA DONNÉE DU C++ PUR** (`USPStationSubsystem::VerifierPostes`). Les
  positions sont mesurées sur des BOÎTES, pas sur la géométrie : un poste posé
  40 cm dans une cloison serait injoignable, et le seul symptôme serait une invite
  qui ne s'allume jamais — un silence, c'est-à-dire le pire des retours. UE
  possède la vraie collision : il balaie la capsule du joueur à chaque position et
  crie si elle ne tient pas. *8 publiés, 0 bloqués.*
- **LA PORTÉE RESTE À 1,5 m**, et c'est un oracle qui l'a imposé. La tentation
  était de l'ouvrir à 2 m maintenant que les modules sont distants de dizaines de
  mètres — mais trois postes partagent encore NOVELLUS, qui ne fait que 11 m, et
  « les portées ne se recouvrent pas » a mordu immédiatement.
- **PREUVE.** Au point d'apparition l'invite est passée de CONTROLE à **VIGIE**
  (Destiny est parti à 27 m de là) ; dans la cupola, **« [ E ] OUVRIR — COUPOLE »**
  avec les lumières des villes par les hublots (côté nuit à l'époque du test).
  Oracles `test_session` **210/210** (+10 sur le placement : fidélité de la
  publication à la table, dispersion sur les trois axes, non-recouvrement, la
  cupola comme poste le plus bas, VIGIE à portée du point d'apparition).
  Handoff toujours superposable au pixel (**dx = 0, dy = 0**).

**RESTE** : la traversée réelle depuis NOVELLUS jusqu'à la cupola (deux nœuds et
quatre écoutilles) n'est pas vérifiée bout en bout — les écoutilles du chemin ne
portent pas le suffixe `_Closed` du modèle et les huit positions sont dans du vide,
mais seule une navigation effective le prouverait.

**10. L'IMPESANTEUR (2026-07-27) — ON NE POUVAIT PAS SE DÉPLACER DU TOUT.**
Retour d'essai de l'utilisateur. Le pawn portait un `UFloatingPawnMovement`, et
**deux blocages distincts** l'empêchaient de servir — aucun des deux n'étant un
réglage, ils se lisent tous les deux dans `FloatingPawnMovement.cpp` :

- **IL NE BOUGEAIT PAS.** `TickComponent` teste `PawnOwner->GetController()` et ne
  fait *rien* si le pawn n'est pas possédé. Or il ne l'est jamais :
  `SetStationInControl` pose la CIBLE DE VUE, pas la possession — l'entrée arrive
  par le HUD et le pont [doctrine du pont]. Le regard marchait (appliqué
  directement), le déplacement non. *Un composant du moteur peut avoir une
  précondition que l'architecture du projet ne remplit pas ; le symptôme est alors
  un silence total, pas une erreur.*
- **IL NE SAIT PAS LAISSER DÉRIVER.** `ApplyControlInputToVelocity` finit par
  `Velocity.GetClampedToMaxSize(MaxSpeed * entrée)` : sans entrée le plafond vaut
  ZÉRO, donc la vitesse est annulée à chaque frame — plus un `Deceleration`
  explicite. C'est un composant de caméra libre : on lâche la touche, on s'arrête
  net. **C'est l'exact contraire de l'impesanteur**, et ses trois paramètres n'y
  peuvent rien.

**LA LOI EST EN C++ PUR ET SOUS ORACLE** (`app/impesanteur.hpp`), en trois faits :
on DÉRIVE (rien ne freine, jamais) ; on n'accélère qu'en POUSSANT sur quelque
chose (3ᵉ loi — sans point d'appui, pas de force ; la « nage » dans l'air existe
et est vingt fois moins efficace, ce qui évite le blocage sans en faire un mode de
jeu) ; on s'arrête en S'AGRIPPANT. Les quatre grandeurs sont des valeurs de corps
humain, pas des curseurs : plafond **1,2 m/s** (un équipage se déplace
délibérément, ~0,2 à 0,5 m/s en routine ; les 2,2 m/s d'avant étaient une vitesse
de vol, à laquelle on se blesse contre une cloison), poussée 2,5 m/s², brasse
0,12 m/s², prise 4,0 m/s².

**MAJ N'EST PLUS UN « BOOST » MAIS UNE MAIN COURANTE.** ×3 la vitesse est un
réflexe de jeu de vol ; en microgravité il n'y a pas de frein, il y a une main
courante qu'on attrape. Une seule touche fait donc les deux choses qu'une main
courante fait — pousser fort, et retenir — et elle n'a d'effet **qu'à portée d'une
paroi** (65 cm, un bras tendu, mesuré par balayage de la capsule gonflée). Elle est
NOMMÉE dans le bandeau du HUD : c'est la commande la moins devinable du jeu, et un
joueur qui l'ignore dérive jusqu'à la cloison suivante en croyant à un bug.

**LE PIÈGE, PAYÉ DEUX FOIS (n°55).** L'absorption de la vitesse au contact ne se
lit PAS dans le résultat de collision rapporté. Mesuré : le joueur s'arrêtait bien
contre le plafond (le moteur bloquait) mais **gardait 0,59 m/s indéfiniment** —
une vitesse fantôme, invisible tant qu'on pousse, qui repart d'un bond dès qu'on
s'écarte. `IsValidBlockingHit()` vaut `bBlockingHit && !bStartPenetrating` et
devient faux dès qu'on est à fleur de paroi ; et `bBlockingHit` seul ne suffit pas
non plus, parce que `SafeMoveUpdatedComponent` RÉESSAIE le déplacement après avoir
résolu la pénétration et **écrase `OutHit`** avec le second essai. La réponse est
dans le **DÉPLACEMENT OBTENU** : ce que la géométrie a laissé passer EST la vitesse
d'après — bloqué net donne zéro, effleuré donne la tangentielle, et les coins et
contacts multiples se règlent sans cas particulier. C'est pourquoi la loi du choc a
été RETIRÉE de `impesanteur.hpp` : une loi qu'on ne peut pas alimenter correctement
vaut moins qu'une mesure, et deux mécanismes pour un phénomène en valent zéro.

**PREUVE**, mesurée dans le vrai chemin de code (poussée 1 s puis relâchement) :
accélération à 0,56 m/s, puis **|v| = 0,560 m/s CONSTANT sur 130 frames** de dérive
(le seuil de 0,2 % laisse le vol libre bit pour bit intact), puis **|v| = 0,000 à
l'impact** avec le plafond du module, position figée. Oracles `test_session`
**222/222** (dont : sans entrée la vitesse est rendue TELLE QUELLE dans les quatre
combinaisons appui/prise sauf celle qui veut dire « je tiens la main courante » ;
une seconde entière de dérive sans perdre un iota ; le rapport poussée/brasse ; la
diagonale qui ne pousse pas plus fort qu'un axe ; le freinage qui s'arrête à zéro
sans repartir en arrière). Handoff toujours superposable (**dx = 0, dy = 0**).

**APPROXIMATION DÉCLARÉE** [GDD 6.8] : le corps est un POINT — pas de rotation
propre ni de moment cinétique. Un astronaute qui pousse de travers se met à
tourner ; ici le regard reste commandé à la souris. Une rotation subie serait
fidèle et injouable — le mal des transports est une vraie sensation d'impesanteur,
mais pas une qu'on veut infliger.

### LA CHRONOLOGIE DE VOL (2026-07-27) — LE VOL DURE ENFIN

Le manque que le §7 annonçait : « l'insertion et l'EDL sont des phases critiques
du modèle mais ne sont pas encore DATÉES ». **Une phase sans date n'arrive
jamais.** Le moteur savait qu'une insertion est critique — `Events.hpp` majore
ses taux d'anomalie, `MissionTempo.hpp` en déduit un plafond de cadence — il ne
savait pas QUAND elle a lieu. D'où un plafond qui ne mordait qu'à l'ascension, et
un vol vers Mars qui ne consommait **pas une seconde** de temps de jeu : « lancer »
et « débriefer » étaient deux clics consécutifs.

**LE MODÈLE : `mission/FlightTimeline.hpp`** (C++ pur). Depuis le feu vert, la
mission porte des segments JOINTIFS, et le PROFIL suit la physique de la famille,
pas un genre littéraire — une charge GEO fait un transit d'ellipse, un cargo NEP
spirale, un rover entre dans une atmosphère :

| Profil | Familles | Chronologie |
| :--- | :--- | :--- |
| Rendez-vous LEO | logistique, service, habité | ascension → phasage → **amarrage** |
| Transfert GEO | sat | ascension → parking → **injection GTO** → transit → **circularisation** |
| Interplanétaire | mars, mars_habite, science, relativiste | ascension → parking → **injection** → croisière → **insertion** |
| Surface | surface | … → croisière → **EDL** |
| Continu | nep | ascension → parking → spirale (non datée) |

**AUCUNE DURÉE N'EST UN RÉGLAGE.** Trois seulement sont SOURCÉES et elles
existaient déjà (ascension 9 min / Falcon 9 SECO ; EDL 7 min / MSL ; manœuvre
critique 10 min / Apollo LOI). **Tout le reste est DÉRIVÉ par Kepler** de
l'orbite concernée :
- une révolution d'attente en **orbite de parking** — la MÊME orbite à 200 km
  dont `trajectory_dv_for_mission` paie déjà l'injection Oberth (un chiffre, une
  source) : 88,5 min ;
- le **transit GTO** comme demi-période de l'ellipse parking→GEO : 5 h 15, la
  valeur réellement volée ;
- **l'orbite géostationnaire n'est même pas un chiffre** : `geo_radius_m()` =
  (µ/ω²)^⅓ sur la vitesse de rotation SIDÉRALE de la Terre. On retrouve
  42 164 km sans jamais l'avoir écrit — c'est la solution de « période orbitale
  == jour sidéral », pas une constante recopiée ;
- le **phasage de rendez-vous** comme profil à 4 orbites (Soyuz MS, Dragon).

**ET LA CROISIÈRE N'EST PAS INVENTÉE DU TOUT.** C'est la durée de transit de la
fenêtre RÉELLEMENT visée. `astro::WindowResult` la calculait déjà — c'est l'axe
des durées de la carte porkchop — **sans jamais la publier** : la fenêtre ne
répondait qu'à « quand partir ? », jamais à « quand arrive-t-on ? ». Deux champs
ajoutés (`tof_days` à l'optimum, `local_tof_days` pour le transfert disponible
maintenant), et la seconde réponse tombe : **fenêtre 2026, optimum 310 j,
transfert disponible 329 j**. Elle est **FIGÉE AU FEU VERT** (`Mission::tof_days`)
et jamais recalculée : la géométrie du ciel au décollage date l'arrivée, et un
recalcul en route ferait glisser l'arrivée d'un vol déjà parti.

**CE QU'ON NE SAIT PAS CALCULER, ON LE DÉCLARE** [GDD 6.8]. Une famille dont le
contrat ne nomme pas de cible (« science ») et une spirale à poussée continue
n'ont pas de date d'arrivée : `dated == false`, et **un vol non daté ne bloque
rien**. On n'oppose jamais au joueur une date qu'on ne sait pas calculer.

**CONSÉQUENCES, et c'est tout l'intérêt :**
- **GATE D'ARRIVÉE** sur `Launched → Debrief` : on ne débriefe pas une sonde
  encore en croisière, et le refus CHIFFRE l'attente (« vol en cours : arrivee
  dans 249 jours ») exactement comme le gate de fenêtre. Le temps de jeu
  [GDD 14.2] devient ce qui CONSOMME un vol.
- **le plafond de cadence mord ailleurs qu'à l'ascension** — la loi de
  `MissionTempo` n'a pas changé d'une ligne, il lui manquait un événement daté
  auquel s'appliquer.
- **le motif de refus est enfin AFFICHÉ.** Il était calculé et jeté : le bouton
  du poste CONTROLE refusait en silence (piège n°42), ce qui se lit comme une
  panne. `Session::dernier_refus_mission` le porte, le poste l'imprime.

**L'ISSUE DU VOL A CHANGÉ DE PROPRIÉTAIRE.** Elle vivait sur `Session`, l'objet
d'UI, qui ne se sauvegarde pas — et **les missions en cours ne se sauvegardaient
PAS DU TOUT** (`GameState::save` le déclarait « V2 »). Invisible tant qu'un vol
durait zéro seconde ; fatal dès qu'il dure 259 jours, puisque *quitter au menu
sauvegarde*. Corrigé des deux côtés : le résultat du vol est un fait de la
mission (`Mission::flight_flown/success/anomaly`), et les missions en cours sont
sérialisées. On n'écrit que les FAITS — identité du contrat (**RÉAPPARIÉ par id
depuis le catalogue**, qui est reconstruit par la graine : même doctrine que son
état `suspended`), état FSM et sa date, durée de transit figée, issue du vol.
`Mission::phase` n'y est pas : elle se DÉRIVE. *La chronologie n'est pas un état,
c'est un calcul* — d'où l'oracle qui exige que la date d'arrivée reconstruite
après rechargement soit identique au bit près.

**PREUVE** (build `SPEditor` Succeeded, 17 s ; oracles 3 040/3 040) :

| Capture | Ce qu'elle montre |
| :--- | :--- |
| `ue_chrono_insertion.png` | `-spvol=insertion -spcadence=4` : « RYTHME IMPOSE : MANOEUVRE CRITIQUE », cran REEL vert, JOUR/SEM/MOIS **rouges**, calendrier immobile au 27 JUL 2026 alors que « mois/s » était demandé |
| `ue_chrono_edl.png` | idem à l'EDL (« RYTHME IMPOSE : EDL »), et le poste affiche PHASE DE VOL = EDL en orange |
| `ue_chrono_poste_controle.png` | `-spvol=croisiere` : cran MOIS **vert** (aucun plafond en croisière), le monde avance de JUL à NOV 2026 pendant que le poste affiche « PHASE DE VOL : CROISIERE / ARRIVEE : dans 165 jours » |

### LA TRACE DU VOL DANS LE MONDE (2026-07-27) — LE VOL EST QUELQUE PART

La chronologie dit QUAND ; il manquait OÙ. `mission/FlightTrace.hpp` (C++ pur).
Le tracé du pont EXISTAIT et DORMAIT depuis la scission de `jeu.cpp` : le rendu
savait dessiner trajectoire, corridor et nœuds, plus personne ne les publiait.

**L'ARC EST CALCULÉ, PAS DESSINÉ.** C'est la solution de LAMBERT entre la
position réelle de la Terre à l'instant de l'injection et celle de la cible à
l'instant de l'insertion — **les deux dates venant de la chronologie**, les deux
positions de l'éphéméride. La polyligne (512 points) est obtenue en PROPAGEANT
cet état par Kepler, pas en interpolant : ce qui est tracé est donc la
trajectoire, et la position du vaisseau à l'instant t est un point DE cette
courbe, par construction — l'oracle l'exige au mètre près à la date d'un
échantillon. L'oracle central n'est pas une plage de valeurs mais un
**invariant** : l'énergie spécifique et le moment cinétique sont CONSTANTS le
long de la polyligne, ce qu'aucune courbe interpolée ne ferait.

**L'ARC EST FIGÉ AU FEU VERT** (date + durée de transit + destination = sa
signature) : reconstruire Lambert et 512 propagations par frame serait un
gaspillage pur, et le pont prévoyait déjà `last_arc_sig` pour ça. Seule la
POSITION avance, pour UNE propagation — jamais par relecture de l'échantillon le
plus proche (0,64 jour d'écart, soit plus d'un million de km : un arc tracé
finement mais parcouru par sauts serait pire que pas d'arc).

**CE QUI N'EST PAS ENCORE LÀ EST DÉCLARÉ** [GDD 6.8] : le corridor est NUL et la
position publiée est la NOMINALE. Ce n'est pas une fuite de vérité au sens de
[GDD 7.5] — c'est une absence d'écart : à ce stade le vaisseau suit son arc PAR
CONSTRUCTION, l'issue restant un tirage à l'arrivée. Estimé et corridor
divergeront quand le vol manuel introduira l'erreur d'exécution et la poursuite.
Les phases proches de la Terre (ascension, parking, rendez-vous LEO, mise à poste
GEO) n'ont PAS de trace : à l'échelle du système elles tiennent dans le pixel de
la Terre, et ce qui n'est pas séparable ne doit pas être désignable (piège n°41).

**LE DÉFAUT QUE LE RENDU A RÉVÉLÉ, ET QUI ÉTAIT DANS LE MODÈLE** (piège n°63) :
`transfer_tof_days` prenait la durée du meilleur transfert des **60 jours à
venir** (`slop_days`, la largeur opérationnelle de « maintenant » — la bonne
question pour dire si la fenêtre est ouverte, la mauvaise ici) et l'appliquait à
un départ AUJOURD'HUI. Lambert répond quand même : par un arc valide reliant deux
dates sans rapport, **plongeant à 0,26 UA du Soleil pour aller chercher Mars**.
Invisible dans les chiffres, évident à l'écran — l'arc partait hors du champ. Le
slop est resserré à un pas de balayage de la carte porkchop, et l'oracle qui
l'aurait attrapé existe maintenant : **un transfert pris dans sa fenêtre reste
ENTRE les deux orbites** (mesuré : périhélie 0,970 UA, aphélie 1,561 UA, transit
329 j), au lieu du vague « l'arc reste dans le système interne » qui laissait
passer le plongeon.

**PREUVE** : `ue_trace_croisiere.png` — le monde ATTEND l'ouverture de la
fenêtre (comme le gate l'impose au joueur), lance, et court jusqu'à mi-croisière :
au 10 MAR 2027 l'arc part d'un nœud posé sur l'orbite terrestre, s'incurve entre
les deux orbites et aboutit sur un nœud posé sur l'orbite de Mars, le vaisseau
dessus. `ue_chrono_insertion.png` (refaite) montre la même chose à l'arrivée : au
22 AOÛT 2027 l'extrémité de l'arc COÏNCIDE avec Mars, et le bandeau affiche
« RYTHME IMPOSE : MANOEUVRE CRITIQUE ».

### LA DISPERSION DE NAVIGATION (2026-07-27) — LA FIN DU 0,985

`MissionPlan::p_physics` valait **0,985**, commenté « issue du MC, simplifiée » :
un quart de la probabilité de succès d'un vol ne dépendait de RIEN — ni du
moteur, ni de la marge provisionnée, ni de la géométrie du transfert. C'était le
dernier chiffre magique de la boucle de mission. `mission/Navigation.hpp` le
remplace par un calcul, **entièrement bâti sur des modules déjà là et déjà sous
oracle** :

1. **L'injection n'est jamais exacte** — `nav/Gates.hpp` (Gates, 1963) donne son
   écart-type à partir du Δv commandé.
2. **Une erreur au périgée est AMPLIFIÉE** : v∞² = v_p² − v_esc² donne
   v∞ δv∞ = v_p δv_p, donc un gain v_p/v∞ (mesuré **×2,8** pour Mars). C'est
   l'effet Oberth lu à l'envers, et c'est pour cela qu'une injection
   interplanétaire se mesure au dixième de m/s.
3. **Elle se propage en manque au but** par la matrice de transition de l'arc,
   obtenue par différences finies centrées sur le propagateur képlérien — même
   doctrine que `nav/OrbitDetermination.hpp` sur le propagateur de vérité :
   aucun modèle dupliqué, et c'est ce que le joueur peut faire lui-même.
4. **La correction qui annule ce manque** est linéaire en δv, donc sa norme suit
   une **loi de Maxwell** (norme d'un vecteur gaussien 3D) — forme close.
5. **P(navigation) = P(|Δv_corr| ≤ marge provisionnée).**

**LES CHIFFRES, mesurés** (fenêtre Mars 2026, transit 329 j) : injection
**3 979 m/s** depuis le parking à 200 km, erreur d'exécution **11,6 m/s** (1σ),
amplifiée à 33 m/s en héliocentrique, d'où un **manque au but de 1 238 000 km**
sans correction — 350 rayons martiens. La correction à TCM-1 (J+14, valeur
SOURCÉE : MSL L+15 j, Mars 2020 L+14 j) ne coûte que **75 m/s au 99e centile** :
*corriger tôt coûte bien moins que l'erreur qu'on corrige*, et ce n'est pas un
réglage, ça sort de la matrice de transition.

**CE QUE ÇA CHANGE** : provisionner de la marge (`Program::dv_margin`) et choisir
un moteur précis ACHÈTENT enfin quelque chose de calculé. Le couplage annoncé par
`Program.hpp` depuis toujours — « plus de marge → étage plus lourd → lanceur plus
cher » — se ferme : les deux bouts existent. Sous oracle : marge 0 → P = 0,000 ;
marge 400 m/s → P = 1,000 ; moteur deux fois plus précis → couverture doublée.

**LE PIÈGE ÉVITÉ DE JUSTESSE (n°64)** : la marge était **affichée sans être
réglable**. Tant que `p_physics` valait 0,985 elle ne servait qu'à alourdir
l'étage et personne n'y touchait ; dès qu'elle COMMANDE la probabilité, ne pas
pouvoir la régler rendait **toute mission interplanétaire impossible** — piège
n°40 à l'identique. Le poste porte donc un réglage `− / +`, et surtout la LECTURE
qui le motive (injection, manque au but, correction à prévoir) : on ne provisionne
pas à l'aveugle, on couvre un chiffre.

**LE CORRIDOR 3σ** [GDD 8.3] est publié et dessiné, mais il se LIT au terminal :
mesuré 49 000 km à J+10, 608 000 km à J+100, **1 329 000 km à mi-croisière** —
soit 2 pixels au plan système. Ce qui n'est pas séparable à l'écran doit être
CHIFFRÉ ailleurs (même doctrine que Novellus, piège n°41), et l'oracle fixe
l'ordre de grandeur pour que la règle ne se perde pas. Il CROÎT et rien ne le
rétrécit encore : la poursuite (achat de passes, détermination d'orbite) est la
brique suivante — DÉCLARÉ, pas simulé.

**AU PASSAGE, UNE CORRECTION DE CONCEPTION** : le poste débordait de son cadre
(piège n°42). Le remède n'est pas de raccourcir le texte mais de reconnaître
qu'**on ne reconçoit pas un véhicule en vol** : commandes de programme et étude
de navigation appartiennent à la CONCEPTION et s'effacent au feu vert, laissant
la place aux données de VOL, qui ne servent qu'après.

**PREUVE** : `ue_nav_conception.png` (26 SEP 2026, la chaîne entière lisible :
injection 3 979 m/s ±11,6 Oberth ×2,8 → manque au but 1 238 449 km → correction
75 m/s → P(succès) 0,0 % faute de marge) et `ue_nav_poste_controle.png` (en vol :
phase, arrivée, corridor 1 328 690 km, sans les commandes de conception). L'écran
et l'oracle donnent les MÊMES chiffres — contrôle croisé modèle/affichage.
Nouveau drapeau **`-spvol=conception`** : la mission n'est pas partie, ce cadran
ne se capturait pas autrement.

### L'ÉTAT VRAI (2026-07-27) — L'ERREUR EST RÉELLEMENT COMMISE

La dispersion ci-dessus était STATISTIQUE : on savait ce qui POUVAIT arriver,
rien n'arrivait. `nav_realisation` fait exécuter l'injection pour de bon —
`nav::apply_gates` tire l'écart sur un **sous-flux dédié** de la graine de
mission, donc rejouable, et ajouter d'autres sources d'aléa ailleurs ne décalera
pas ce tirage. L'écart subit la MÊME amplification d'Oberth que sa statistique :
une seule loi pour la dispersion et pour sa réalisation, sinon les deux
divergeraient en silence.

Le manque au but n'est pas linéarisé : à plus d'un million de km de 1σ, un
premier ordre ne serait pas honnête — on propage les DEUX états et on compare.
Mesuré sur un tirage : **erreur commise 15,3 m/s → manque au but 4 200 000 km →
correction requise 48,6 m/s**.

**L'ISSUE DE NAVIGATION N'EST PLUS UN DÉ.** `fly_mission` comparait tout à une
probabilité ; il compare maintenant un NOMBRE à la marge provisionnée. Deux plans
identiques, deux marges différentes : celui qui a sous-provisionné échoue, pour un
motif nommé (« dérive de navigation hors corridor ») et avec
`player_error_causal` — sous-provisionner est une décision de conception, donc une
cause racine documentée [GDD 10.3]. **Et le risque n'est pas compté deux fois** :
la navigation résolue, son facteur SORT du tirage — sinon le même risque serait
une estimation *et* un fait. C'est la distinction que tout ce chantier installe :
**à la conception on estime une probabilité, en vol on subit un résultat.**

Le joueur ne voit toujours pas cet écart pendant le vol [GDD 7.5] : il l'apprend
au DÉBRIEF, où le poste affiche le manque au but réel et la correction qu'il
aurait fallu — de quoi juger sa marge sur autre chose qu'une statistique. Les
trois faits (`nav_evaluee`, `nav_dv_required`, `nav_miss_km`) sont sérialisés :
**on ne retire pas une erreur déjà commise.**

### LA POURSUITE ET LA CAMPAGNE DE CORRECTION (2026-07-28)

`mission/NavSolution.hpp`. L'en-tête de `nav/Tracking.hpp` promettait ceci depuis
le premier jour : « **sans poursuite, le joueur ne SAIT PAS que son erreur
d'exécution a eu lieu. Il croit que sa manœuvre est passée au nominal. Sa
correction est donc calculée sur un état faux — donc inutile.** » C'est cette
phrase qu'on rend vraie.

**UN VRAI FILTRE PAR LOTS**, forme bayésienne standard (Tapley, Schutz & Born
ch. 4) : Λ = P0⁻¹ + Σ (HΦ)ᵀW(HΦ), δx̂ = Λ⁻¹Σ(HΦ)ᵀW·résidu, P = Λ⁻¹. **Rien n'est
inventé** : H vient de `nav::predict` (partielles exactes distance / vitesse
radiale), la visibilité de `nav::station_visible` (masque d'élévation réel des
trois complexes du DSN), Φ de la matrice de transition képlérienne de
`Navigation.hpp`, et **l'a priori EST la dispersion d'injection** — avant de
mesurer, le joueur ne connaît que la loi de son erreur.

`nav::batch_least_squares` (propagateur n-corps, STM sur la vérité) serait plus
fidèle mais demande une propagation numérique par mesure toutes les 60 s sur
deux semaines : hors de portée d'un écran. On garde la MÊME algèbre et les MÊMES
partielles sur des états képlériens — la même approximation que l'arc qu'on
estime, donc **pas de modèle plus grossier que ce qu'il mesure**.

**LA CAMPAGNE DE CORRECTION** enchaîne deux manœuvres aux dates de la pratique
(TCM-1 à J+14, TCM-2 à A−45 j), chacune **calculée sur ce que le joueur CROIT** et
exécutée avec l'erreur de Gates. Et le verdict est net :

| | TCM-1 | TCM-2 | total | manque final |
| :--- | ---: | ---: | ---: | ---: |
| **sans poursuite** | 55,1 m/s | 281,8 m/s | **336,9 m/s** | **92 115 km** |
| **14 j de poursuite** | 48,7 m/s | 7,3 m/s | **56,0 m/s** | **120 km** |

Sans mesures, le joueur dépense **six fois plus** de Δv pour manquer Mars de
27 rayons planétaires. Avec, il arrive au but et TCM-2 n'est plus qu'une retouche.
`Program::tracking_days`, acheté et facturé depuis toujours, décide enfin de
quelque chose — et il a son bouton au poste, comme la marge (piège n°64, qui
serait revenu à l'identique).

**DEUX FOIS LE MODÈLE A CORRIGÉ CELUI QUI L'ÉCRIVAIT** :
- **le filtre s'annonçait 300 fois plus précis qu'il ne l'était** (σ 0,001 m/s
  pour une erreur résiduelle de 0,3). Cause : une seule linéarisation, autour du
  nominal, à 11 000 km de la vérité. Trois itérations de Gauss-Newton (ce que
  `batch_least_squares` fait déjà) et la covariance cesse de mentir : erreur
  résiduelle 0,001 m/s pour un σ de 0,001. **Un filtre trop confiant est pire
  qu'un filtre imprécis** (piège n°66) ;
- **TCM-2 à trois jours de l'arrivée coûtait 112 m/s** même avec une poursuite
  parfaite. Le modèle avait raison : à trois jours du but, Φ_rv est presque
  singulière, la LEVIÉE a disparu, et rattraper quelques milliers de km coûte
  plus cher que l'injection ratée. C'est pourquoi les campagnes réelles corrigent
  TÔT (MSL : TCM-1 L+15 j, TCM-3 A−45 j). Date corrigée sur la pratique, pas sur
  le confort.

### LA MANŒUVRE EST UN ACTE DU JOUEUR (2026-07-28) — `mission/Manoeuvre.hpp`

« Toutes les manœuvres sont calculées **PAR LE JOUEUR** dans le terminal.
L'assistance dépend UNIQUEMENT du mode. » [GDD 7.4, 2.2]. Tout ce qui précédait
calculait la correction À SA PLACE : c'était de la conception, pas du pilotage.

- **LES TCM SONT DES RENDEZ-VOUS DATÉS.** La croisière se coupe en trois autour
  de ses deux corrections (TCM-1 à J+14, TCM-2 à A−45 j), et chacune est une
  MANŒUVRE CRITIQUE de la chronologie. Conséquence : **le plafond de cadence
  ramène le monde au temps réel au moment exact où il faut agir** — les trois
  chantiers (chronologie, trace, navigation) se referment ici l'un sur l'autre,
  et la question « le joueur n'a rien à faire pendant ces 10 minutes » n'existe
  plus : c'est là qu'il corrige.
- **IL COMMANDE TROIS COMPOSANTES EN RSW** — le repère que `fen/core/Vec3.hpp`
  déclare depuis toujours comme « LE repère dans lequel le joueur exprime ses
  Delta-v », et celui des opérations réelles. La base est construite sur l'état
  qu'il CROIT : commander dans un repère qu'on ne connaît pas exactement fait
  partie du problème.
- **LE MODÈLE APPLIQUE LITTÉRALEMENT**, erreur de Gates comprise, à l'état vrai.
  Aucun rattrapage silencieux. Sous oracle : le Δv du solveur divise le manque au
  but par **151** (4 200 735 km → 27 769 km, puis TCM-2 finit le travail) ; ne
  rien commander ne coûte rien et ne corrige rien ; **un Δv mal orienté AGGRAVE**
  la trajectoire ; et corriger sans poursuite ne sert à rien, puisque le joueur
  aveugle ne voit aucun écart à corriger.
- **L'ASSISTANCE DÉPEND DU MODE, ET DE RIEN D'AUTRE** [GDD 2.2]. Le bouton
  SOLVEUR — l'équivalent du nœud préconstruit du graphe — existe en **Normal** ;
  en **Pro** le poste affiche « aucun solveur — le calcul est à vous ».
  **`ModeAide` n'avait jamais eu le moindre effet nulle part : c'est son premier.**
- Ce que fait `nav_campagne` automatiquement devient le comportement de
  l'ADJOINT pendant une absence [GDD 9.3], pas celui du joueur présent.

**PREUVE** : `ue_manoeuvre_tcm.png` (`-spvol=tcm`, nouveau drapeau) — 10 OCT 2026,
« RYTHME IMPOSE : MANOEUVRE CRITIQUE », le monde au temps réel malgré `mois/s`
demandé, et le poste qui offre les trois axes, le solveur et l'exécution. Le
manque au but projeté y affiche **0 km** parce que la capture n'achète aucune
poursuite : le joueur ne voit rien à corriger — la doctrine de `nav/Tracking.hpp`,
enfin visible à l'écran.

### LE GRAPHE DE NŒUDS (2026-07-28) — `mission/Graphe.hpp`, mode NORMAL [GDD 2.2]

Le joueur COMMANDAIT son Δv avec trois boutons ; il le CALCULE maintenant, en
assemblant des primitives typées. **Un nœud, un appel d'API** — l'équivalence
stricte que [GDD 2.2] exige, et l'écran la NOMME (chaque bouton porte la
fonction qu'il est : `navigation().solution()`, `stm(etat, duree)`,
`solveur().corriger(phi, ecart)`…). Huit primitives : SOLUTION NAV, TEMPS
RESTANT, PROPAGER, ÉCART/CIBLE, TRANSITION, RÉSOUDRE Δv, VERS RSW, COMMANDE.

**LE BOUTON « SOLVEUR » A ÉTÉ RETIRÉ**, et c'est une correction de doctrine, pas
une régression : une réponse en un clic est exactement la « procédure prête à
rejouer » que [GDD 2.4] interdit. Ce que Normal accorde, ce sont des primitives
et une **validation de typage** — la contrepartie exacte de ce que le
compilateur ferait en Pro. Brancher un VECTEUR là où on attend une DURÉE est
refusé, avec le nœud et le type nommés (« PROPAGER attend DUREE, recoit
VECTEUR »). En **Pro**, aucune assistance : « le calcul est à vous ».

**L'ORACLE QUI COMPTE** : le graphe assemblé à la main rend **exactement** le
même Δv que le solveur interne (R −9,44 · S −38,31 · W 28,46 m/s), et le vol
obtenu est le même au mètre près. Si les deux divergeaient, l'un mentirait sur ce
que fait l'autre. Sont aussi sous oracle : le refus typé et son motif, un nœud
qui exige une TRANSITION en amont, un graphe qui ne commande rien.

**LE GRAPHE NE SE SAUVEGARDE PAS** — [GDD 2.4] veut qu'on REFASSE l'assemblage à
chaque analyse. Ce n'est pas un oubli de sérialisation, c'est la règle.

**APPROXIMATION DÉCLARÉE** [GDD 6.8] : le graphe est LINÉAIRE (chaque nœud
consomme la sortie du précédent, plus l'ÉTAT et la TRANSITION mémorisés en
amont). Un graphe à branches convergentes demande une surface d'édition en deux
dimensions ; la chaîne couvre exactement le raisonnement que le jeu pose
aujourd'hui, et l'évaluateur n'a rien qui l'empêche d'accepter des branches le
jour où l'éditeur les dessinera.

**PREUVE** : `ue_graphe_normal.png`. Au passage, deux corrections d'écran de la
même famille que le piège n°65 : la palette passe sur DEUX rangées (un bouton
dont le nom est tronqué ne dit plus quelle fonction il est — ce qui ruine
justement l'équivalence), et **pendant une manœuvre critique le poste devient une
CONSOLE DE VOL** : la fiche de viabilité s'efface, comme les commandes de
conception. Le joueur pilote, il n'évalue pas un programme.

### LA TOOLCHAIN EMBARQUÉE (2026-07-28) — `code/Toolchain.hpp` + `mission/src/Toolchain.cpp`

« Le joueur écrit du **VRAI C++**, compilé et exécuté par une toolchain
embarquée » [GDD décision 1]. Pas un langage maison, pas un interpréteur : le
compilateur du système, les en-têtes `ares::vol`, un exécutable, un processus.
**Les quatre exigences de [GDD 18] sont tenues et sous oracle** :

| Exigence [GDD 18] | Ce qui la tient | Oracle |
| :--- | :--- | :--- |
| **Isolation** — « un pointeur invalide produit un échec de mission, jamais un crash du jeu » | processus SÉPARÉ (`CreateProcess`) | déréférencement nul → **0xC0000005**, `IssueCode::Plantage`, le testeur survit pour l'écrire |
| **Limite de temps** | `WaitForSingleObject` + `TerminateProcess` | boucle infinie → `IssueCode::Delai`, rien ne gèle |
| **Déterminisme** — « journalisation des exécutions avec leurs entrées » | `EntreesVol` écrit à côté du résultat, en TEXTE | mêmes entrées → mêmes décisions ; les entrées se relisent à l'identique |
| **Hors-ligne** | rien ne sort de la machine | par construction |

**L'EXEMPLE DE [GDD 15.3] TOURNE LITTÉRALEMENT.** Le squelette que le poste
propose au joueur est le code du document, mot pour mot : il compile contre
`ares::vol`, s'exécute, et rend **Δv = 57,870 m/s** avec son journal de bord
(« Correction executee : 57.87 m/s »). Si ce test tombait, le GDD décrirait une
API que le jeu n'a pas. Les **quatre décisions** du `Contexte` remontent :
exécuter, différer, alerter, replanifier — et « ne rien exécuter » en est une.

Ce qui ne compile pas est refusé **à coût nul** [GDD 15.5 étape 1], avec les
diagnostics du compilateur tels quels.

**CE QUI EST DÉCLARÉ** [GDD 6.8, 18] : on utilise le compilateur DÉJÀ présent
(`cl.exe` ; plateforme cible Windows). L'EMBARQUER dans la distribution — les
« plusieurs centaines de Mo » que [GDD 18] budgète — est une tâche de
**packaging**, pas de modèle : le mécanisme, lui, est réel et vérifié de bout en
bout. Sur une machine sans compilateur, la chaîne rend `Indisponible` et le dit,
au lieu de faire semblant.

**TROIS PIÈGES PAYÉS**, tous du même genre — *un échec silencieux ressemble à une
absence de capacité* :
- **n°68** : `cmd /c` découpe au premier guillemet si la commande entière n'est
  pas encadrée d'une paire supplémentaire. Résultat : ni erreur, ni journal, rien
  — et la chaîne concluait « compilateur absent ».
- **n°69** : classer l'issue sur une phrase du shell (« n'est pas reconnu »)
  faisait passer un code FAUX pour une machine mal installée — le script
  d'environnement du compilateur imprime lui-même cette phrase, sans rapport. On
  tranche désormais sur la présence d'un diagnostic `error C…` / `erreur C…`.
- **n°70** : Windows garde un binaire verrouillé un instant après la fin du
  processus qui l'exécutait ; le lieur échouait sur `LNK1104`. **Un artefact qui
  doit être remplacé doit être NEUF, pas écrasé** — un nom unique par compilation
  supprime la question au lieu de mieux la contourner.

### L'ATELIER LOGICIEL DU MODE PRO (2026-07-28) — la SURFACE, et ce qu'elle a révélé

Le poste CONTRÔLE a désormais **deux faces** en mode Pro : la conduite de mission,
et l'atelier logiciel (`SSPPoste::BuildCodeVol`). Le drapeau vit sur la session
(`Session::atelier_logiciel`), à côté de `poste_ouvert` et pour la même raison —
*une vue qu'aucune capture ne peut atteindre est une vue que personne ne vérifie*.
Le cadre d'un poste CLIPPE (piège n°42) : un éditeur, ses diagnostics et sa fiche
de qualification ne tiennent pas sous dix lignes de vol.

**LA CHAÎNE DE [GDD 15.5], DANS L'ORDRE ET SANS RACCOURCI**, chaque étape gardée
par la précédente : COMPILER (coût nul, diagnostics tels quels) → BANC D'ESSAI
(prélève la trésorerie par `finance.engage`, avance le calendrier par
`avancer_temps`) → TÉLÉVERSER (refusé sans fiche valide) → et, en vol, EXÉCUTER
LE CODE DE VOL, dont le Δv passe en RSW dans `tcm_commande`. **Le code PROPOSE ;
le joueur appuie encore sur EXECUTER.** Réglages du banc montrés AVEC leur
conséquence chiffrée AVANT le clic (« SI ON LANCE LE BANC : couverture 62 %,
10,0 M€, +4,0 j ») — même doctrine que la marge (piège n°64).

**UNE FICHE APPARTIENT À UN TEXTE, PAS À UN JOUEUR.** `source_compilee` /
`source_certifiee` / `source_bord` sont gardées : éditer une ligne après le banc
PÉRIME la qualification et rouvre l'interdiction de téléverser. Sans cela, la
fiche aurait porté sur un code que personne n'a jamais exercé.

**LE BANC NE SE PAIE PLUS EN DÉCLARATIONS.** Cocher « profils dégradés » et
« interfaces » ÉLARGIT le domaine : à heures constantes la couverture BAISSE
(`bench_h_char`, ×2 et ×1,5, [Annexe E — à calibrer]). Sans cette dilution, deux
cases gratuites achetaient une confiance A — *un domaine plus large certifié au
même prix, c'est-à-dire une déclaration que rien ne paie*.

**DEUX PIÈGES PAYÉS, ET LE SECOND EST LE PLUS GRAVE DE LA PASSE :**
- **n°71 — un bac à sable qui laisse échapper ses détenus n'en est pas un.**
  `TerminateProcess` au délai tuait le `cmd.exe` intermédiaire, **pas le programme
  du joueur**, qui continuait de brûler un cœur indéfiniment. L'oracle du délai
  passait quand même (`depasse` était bien vrai) : le seul symptôme arrivait AU
  RUN SUIVANT, en `LNK1104` sur un binaire qu'un fuyard de la veille tenait
  ouvert. Mesuré : **trois processus fuyards, ~16 minutes de CPU chacun**. Le code
  du joueur est désormais lancé DIRECTEMENT (sans shell) dans un **job object**
  tué en bloc — ce qui donne du même coup la **limite de mémoire** que [GDD 18]
  exigeait à côté de la limite de temps. Nouvel oracle : après le délai, le
  binaire doit être EFFAÇABLE, sinon quelqu'un le tient encore.
- **n°72 — un outil fourni doit être juste, ou ne pas être fourni.**
  `ares::vol::Solveur` corrigeait « proportionnellement à l'écart sur un temps
  caractéristique » : exact en champ nul, FAUX dès qu'un arc courbe. Mesuré sur
  une croisière de Mars (4,2 M km de manque, 315 j de reste), il commandait
  **158 m/s dans une direction qui portait le manque à 5,2 M km**. Le joueur qui
  recopiait l'exemple de [GDD 15.3] aggravait donc son vol, et la faute était
  dans l'API — pas dans son code, donc introuvable. Le solveur résout maintenant
  Δv = −Φ_rv⁻¹·Δr sur la vraie matrice de transition : **57,0 m/s, manque divisé
  par 140**. Contrôle croisé : sur un horizon d'UN JOUR les deux formules
  coïncident à 0,03 % (57,851 contre 57,870) — *un arc court EST une droite*, et
  l'ancien modèle ne se trompait que là où la courbure compte.

**CONSÉQUENCE D'ARCHITECTURE** : `M3`, `inverse3`, `StmBlocks` et `kepler_stm`
ont quitté `fen/mission/Navigation.hpp` pour **`fen/astro/Stm.hpp`**. Ils ne
connaissent que Kepler et deux corps — c'est de l'astrodynamique, pas de la
logique de mission — et les laisser dans `mission/` les mettait hors de portée de
`ares/vol.hpp`. `fen::mission` les ré-exporte par `using`, donc aucun appel
existant n'a bougé. Il n'y a toujours **qu'une** matrice de transition dans le
moteur, et le nouveau `astro::dv_correction` est **la** source de la formule de
correction : le graphe du mode Normal, le solveur du mode Pro et le bureau
d'études la partagent. Corollaire : l'API du joueur n'est plus faite que
d'en-têtes — `ToolchainConfig::sources` porte les TU à lier (`Kepler.cpp`).

**CAPTURE** : `-spcode` ouvre l'atelier, `-spcode=vol` reste sur la conduite de
mission en mode Pro. La partie de capture démarrait en NORMAL, et le mode d'aide
se choisit à la création d'une partie — un écran qu'une capture ne traverse pas ;
sans ce drapeau, l'éditeur aurait été un écran que rien ne pouvait photographier.

### LE HORS-DOMAINE MORD (2026-07-28) — la qualification cesse d'être un décor

`fly_mission` lit désormais deux faits figés au feu vert : `Mission::code_embarque`
et `Mission::code_non_couvert`. Un logiciel embarqué sur un vol que son banc n'a
jamais exercé produit une **anomalie majeure** — « exécuter hors du domaine =
comportement non couvert = cause d'anomalie légitime » [GDD 15.5, ch.10]. Sans
cette porte, acheter des heures d'essai ne changeait **rien** à l'issue : le banc
prélevait un budget et retardait une fenêtre pour un résultat purement décoratif.

**LA PORTE EST AVANT LE VERDICT DE NAVIGATION**, et c'est un choix : quand les
deux tombent, la cause proximale est le logiciel. Un code dont on ne sait rien a
pu commander n'importe quoi — le manque au but qu'on mesurerait ensuite serait sa
conséquence, pas une seconde faute.

**CE N'EST PAS UNE PÉNALITÉ POUR AVOIR ÉCRIT DU CODE.** Rester dans son domaine
est gratuit, et l'atelier le dit **avant le décollage**, quand c'est encore
actionnable : « HORS DOMAINE AU DÉCOLLAGE : qualifié "croisiere", cette mission
vole "surface" ». Le dire au débrief aurait été une sanction ; le dire là est une
décision. Les deux faits se **sauvegardent** — sinon quitter au menu absoudrait un
logiciel hors domaine, et l'issue changerait au rechargement (le piège des
missions en vol non sérialisées, repayé une seconde fois pour rien).

**PIÈGE VOISIN PAYÉ AU PASSAGE** — un oracle de la trace exigeait que l'arc
touche Mars **à moins d'un mètre**. Il est tombé tout seul un matin, à 1,359 m,
sans qu'une ligne du modèle ait bougé : 512 propagations de Kepler sur 2,3 UA
accumulent quelques 1e-12 relatifs, et la géométrie de l'arc CHANGE avec la date
réelle (la fenêtre synodique est cherchée depuis l'époque courante). *Un seuil
absolu sur une grandeur relative finit par mentir.* La borne est désormais
relative à l'arc (1e-10, soit ~34 m ici — des ordres de grandeur sous les 3 400 km
de rayon martien) : la preuve est intacte, la fragilité est partie. Même famille
que le piège n°67.

~~**RESTE, ET C'EST DÉCLARÉ** : la couverture du banc (`code_success_prob`) n'est
pas encore tirée en vol~~ — **LEVÉ le 2026-07-28**, voir « LE PRIX DE L'INACTION »
ci-dessous. La condition posée ici (« il faut d'abord que ne rien embarquer coûte
quelque chose ») a été remplie : la campagne automatique n'est plus le
comportement par défaut, et la couverture décide de la tenue des rendez-vous par
le logiciel de bord. Le plafond est juste (une manœuvre fine ne
peut pas défiler à mois/s) mais il n'est supportable qu'une fois la manœuvre
JOUABLE — c'est le vol manuel [GDD 9], §7 point 2, et les deux chantiers se
rejoignent bien là où le document l'annonçait. **À noter aussi** : les crans de
`TimeRate` sautent de ×1 à ×86 400, alors que la loi d'observation réclamerait
~×30 pour une phase de 10 min. La loi choisit donc le temps réel faute de mieux
— ce n'est pas la loi qui est trop stricte, c'est l'échelle des crans qui est
trop grossière, et le GDD 14.2 ne nomme que jour/semaine/mois.

### LE PRIX DE L'INACTION (2026-07-28) — ne rien embarquer coûte enfin quelque chose

La question ouverte que la passe précédente déclarait en toutes lettres : « la
campagne de correction de l'adjoint est **gratuite et quasi optimale** ; tant que
c'est vrai, on ne peut pas tirer contre la couverture du banc sans rendre
l'écriture de code strictement pire que son absence ». **Décision de
l'utilisateur : c'est la CORRECTION TARDIVE qui coûte** — pas un adjoint rendu
incompétent, pas une facture.

**LE FILET EST RETIRÉ, ET C'EST TOUT LE CHANGEMENT.** `tirer_navigation` posait
`nav_dv_required`/`nav_miss_km` depuis `nav_campagne` **dès le feu vert** : le vol
arrivait donc corrigé sans que personne n'ait rien commandé. Le commentaire du
code disait pourtant, depuis le premier jour, que cette campagne était « le
comportement d'ARES **pendant une absence** [GDD 9.3] ». *Le code appliquait
toujours ce qu'il déclarait lui-même comme un repli.* Ces deux chiffres portent
maintenant l'état du vol tel qu'il vient d'être injecté : rien dépensé, et le
manque au but de la trajectoire réellement volée.

**UNE CORRECTION EST UN RENDEZ-VOUS DATÉ**, et il n'a lieu que si quelqu'un est
là pour le commander. `Session::resoudre_vol`, appelé une fois à l'arrivée,
solde le vol depuis l'état VRAI où il en est — les corrections que le joueur a
commandées de sa main y sont déjà. Restent les rendez-vous qu'il n'a pas tenus :

| Qui | Quand | Ce que ça donne (mission Mars 2026, mesuré) |
| :--- | :--- | ---: |
| **Personne** | — | **4 205 263 km** de manque — tolérance 1 000 km, mission perdue |
| **Le logiciel de bord**, couvert | à la date, sans le sol [GDD 9.6] | **19 km** pour 48,7 m/s |
| **Le logiciel de bord**, banc à vide | le tirage tombe [GDD 15.5] | **4 205 263 km** — il ne tient rien |
| **L'adjoint**, joueur absent [GDD 9.3] | campagne complète | **19 km** |

**AUCUN MALUS N'EST APPLIQUÉ NULLE PART, et c'est le point.** Ce qui coûte est le
bras de levier qu'on a laissé passer : Φ_rv devient quasi singulière près du but,
et le modèle l'avait déjà mesuré (112 m/s à A−3 j contre 7,3 à A−45 j). La loi de
campagne n'a pas changé d'une ligne — elle est seulement devenue **REPRENABLE**
(`nav_campagne_depuis` : un rendez-vous antérieur à l'état vrai n'est pas à
prendre, sous oracle). `nav_campagne` n'en est plus que le cas « depuis
l'injection » : **une seule loi, deux points d'entrée**, sinon l'adjoint et le
logiciel de bord corrigeraient différemment pour la même physique.

**`code_success_prob` MORD ENFIN.** La couverture du banc est figée au feu vert
(`Mission::code_couverture` — on ne relit pas la fiche à l'arrivée, le joueur peut
avoir rouvert son éditeur) et le logiciel ne tient ses rendez-vous que si le
tirage passe. « Le banc rassure sans garantir », « un état non imaginé passe
toujours » [GDD 15.5] : c'est le premier endroit du moteur où ces deux phrases
changent une issue. Et ce n'est **pas** une punition pour avoir écrit du code —
rester dans son domaine reste gratuit, et ne rien embarquer n'est plus neutre.

**LE GDD 9.3 EST RESPECTÉ À LA LETTRE** : « ARES fonctionne normalement sous un
adjoint, ni pénalité ni dégradation punitive ». L'adjoint n'est pas dégradé — il
conduit exactement la même campagne qu'avant. Il ne la conduit simplement plus
quand le joueur est **présent** : c'est le drapeau `finance.suspended`, celui-là
même qui gèle déjà la chaîne de fin de partie financière, et pour la même raison.

**DEUX MANQUES TROUVÉS EN CHEMIN, dont un grave :**
- **L'ÉTAT VRAI D'UN VOL EN COURS NE SE SAUVEGARDAIT PAS.** `vol_vrai_*`,
  `tcm_dv_depense`, `tcm_faits`, `nav_connu_dv`, `nav_sigma_*` : rien. Invisible
  tant que l'issue était décidée au feu vert (les deux chiffres la portaient) —
  **fatal** dès qu'elle se joue en vol, puisque *quitter au menu sauvegarde* : les
  corrections commandées à la main s'effaçaient. C'est le piège des missions en
  vol non sérialisées, qui serait revenu une **troisième** fois. Sérialisé, sous
  oracle de relecture au bit près.
- **UN MÉCANISME QUE LE JOUEUR NE VOIT PAS NE LUI APPREND RIEN** (piège n°42). Un
  vol perdu faute d'avoir corrigé ne se lisait que comme un manque au but
  inexpliqué. `Mission::vol_conduit_par` (fait du vol, donc sauvegardé) et une
  ligne au débrief : « CORRECTIONS CONDUITES PAR — **PERSONNE, aucun rendez-vous
  tenu** » / « VOUS, depuis le terminal » / « LE LOGICIEL DE BORD » / « L'ADJOINT,
  en votre absence ». Le crédit va au joueur dès qu'il a tenu ses rendez-vous
  lui-même, même avec du code à bord : l'agent ne prend que ce qui reste, et il ne
  reste alors rien.

**PREUVE** : build `SPEditor` Succeeded (11 s), **3 248 oracles au vert** sur les
12 suites, dont 28 nouveaux sur cette passe. Le chiffre qui tranche est celui du
tableau ci-dessus : le MÊME vol, même graine, même erreur d'injection, donne
4 205 263 km ou 19 km selon qui a tenu ses rendez-vous. *Un chiffre mesuré vaut
dix captures* — et ici la capture serait impuissante, l'écart ne vivant que dans
le débrief d'un vol de 329 jours.

**RESTE** : le délai lumière ne pèse pas encore sur la commande du joueur
[GDD 9.6]. Une commande émise du poste met d/c à atteindre le vaisseau ; à bord,
le logiciel ne l'a pas. C'est le second argument physique en faveur de
l'embarquement, et il devient **décisif aux phases courtes** (EDL 7 min contre 14
minutes-lumière — le fait historique lui-même). Sur un TCM de croisière il est
négligeable devant des mois de bras de levier, ce qui est pourquoi il n'était pas
le bon levier pour CETTE décision.

### LE DÉLAI DE COMMUNICATION (2026-07-28) — et ce qu'il ne fait PAS

**`mission::comms_delay_s` existait depuis le premier jour et PERSONNE ne
l'appelait.** Un modèle que rien ne consomme — même famille que `Mission::phase`
(piège n°20b), `show_moons` (piège n°41) ou `ModeAide` avant le mode d'aide. Et
[GDD 8.3] liste pourtant « **délai de communication** » parmi ce que le plan
terminal doit afficher, aux côtés de l'incertitude 3σ et de la réserve de Δv.

Trois branchements, aucun chiffre nouveau :
- **LA VUE LE PORTE** : `VueNavigation::delai_com_s`, rempli par `Session::vue_vol`
  depuis la distance Terre↔vaisseau de l'éphéméride. **Calculé sur l'ESTIMÉ, pas
  sur la vérité**, et c'est doctrinal : le joueur ne voit jamais sa position vraie
  [GDD 7.5], donc pas davantage un délai qui en découlerait. *Approximation
  déclarée* [GDD 6.8] : l'écart entre les deux vaut le manque au but rapporté à la
  distance, ~1e-5, soit quelques millisecondes sur un délai en minutes. Le poste
  du joueur étant en LEO, on prend la Terre (418 km = 1,4 ms, six ordres sous la
  distance interplanétaire) — déclaré aussi.
- **LE TERMINAL L'AFFICHE** : « DELAI DE COMMUNICATION — 13 min 04 s (aller) —
  la commande arrive après ».
- **IL S'APPLIQUE** : `executer_tcm` fait agir le Δv à `now + d/c`, sur l'état que
  le vaisseau aura ALORS, avec la base RSW de l'estimé au moment de la commande —
  le joueur ne peut pas faire mieux. Le logiciel de bord, lui, est déjà sur place.

**CE QUE ÇA COÛTE, MESURÉ ET NON SUPPOSÉ — ET C'EST PEU.**

| Instant | Distance Terre↔vaisseau | Délai (aller) |
| :--- | :--- | ---: |
| TCM-1 (J+14) | ~7 M km | **24 s** |
| TCM-2 (A−45 j) | ~2,3e8 km | **13,0 min** |

Effet du retard sur le manque au but, à Δv identique, graine identique, état de
départ identique : **moins de 1 km** — contre une tolérance d'arrivée de 1 000 km.
L'oracle exige les deux choses : que le retard **change** le vol (ce n'est pas un
affichage) et que son coût en croisière **reste sous la tolérance**.

**C'EST UN RÉSULTAT, PAS UN ÉCHEC, et il corrige une prévision de la veille.** Le
§7 annonçait un « gros effet sur l'EDL » ; la mesure dit que **d/c est minuscule
devant le bras de levier d'une croisière** (24 s contre 300 jours), donc le délai
n'est PAS ce qui rend l'autonomie nécessaire en croisière. Ce qui la rend
nécessaire reste ce qu'a établi « LE PRIX DE L'INACTION » : tenir le rendez-vous.
Le délai deviendra décisif là où d/c dépasse la durée de la manœuvre — l'EDL, 7
min contre 13 — **mais l'EDL n'est pas encore un événement que le joueur commande**
(la chronologie la date, `resoudre_vol` ne traite que les TCM). Le levier existe,
sa cible n'existe pas encore. Déclaré plutôt que maquillé [GDD 12.5, 19.6].

**PREUVE** : build `SPEditor` Succeeded, **3 260 oracles au vert** (+12 sur cette
passe).

### LA BOUCLE SOL (2026-07-28) — le délai lumière DÉCIDE quelque chose

Le délai était branché et affiché ; il ne décidait encore de rien. Ce qu'il
décide, la réalité le dit sans qu'on ait à choisir : **pour agir sur une phase,
le sol doit voir, décider et commander DANS la fenêtre.** Il lui faut donc un
aller-retour court devant la durée propre de la manœuvre.

**`mission::ground_loop_closes(distance, duree_phase)`** — deux grandeurs
physiques comparées, **aucun seuil libre**. Les durées étaient déjà sourcées
(`phase_duration_s` : EDL 7 min / MSL, manœuvre critique 10 min / Apollo LOI) et
`comms_roundtrip_s` existait depuis le premier jour. Rien de neuf n'a été inventé :
deux pièces qui ne se parlaient pas se parlent.

| Phase | Aller-retour | Durée propre | Boucle sol |
| :--- | ---: | ---: | :--- |
| Amarrage en orbite basse | **0,00 s** | heures | **FERMÉE** — le sol est dans la boucle, comme en LEO réel |
| **EDL martienne** | **26 min** | **7 min** | **OUVERTE** — la descente est conduite À BORD |

C'est le fait historique lui-même : le sol de MSL a regardé « seven minutes of
terror » sans pouvoir rien faire. La raison pour laquelle tout atterrisseur
martien descend sous le contrôle de son propre logiciel n'est pas un choix de
design — c'est 26 contre 7.

**UNE CROISIÈRE N'A PAS DE BOUCLE À FERMER**, et c'est le garde-fou qui empêche la
loi de déborder : `phase_duration_s(TransferCruise) == 0`, donc le prédicat rend
faux. On ne « conduit » pas une croisière, on y tient des rendez-vous **préparés à
l'avance** — c'est exactement pourquoi une TCM se commande très bien depuis le
sol malgré 13 minutes-lumière, et pourquoi la mesure de la section précédente
donnait un coût sous le kilomètre. Les deux résultats disent la même chose.

**PAS DE TIRAGE SUPPLÉMENTAIRE, ET C'EST DÉLIBÉRÉ.** La conséquence mécanique
d'une EDL ratée est **déjà** dans le moteur, à deux endroits : `p_success` porte
la fiabilité achetée par `Program::test_hours`, et `code_non_couvert` fait échouer
une mission dont le logiciel embarqué ne couvre pas l'environnement du vol — un
code qualifié « croisiere » sur un vol « surface » tombe depuis la passe du
2026-07-28. Ajouter ici un troisième tirage compterait le même risque deux fois :
« une approximation déguisée en certitude » [GDD 12.5, 19.6]. Ce qui manquait
n'était pas la sanction, c'était que le joueur **sache pourquoi** — le poste
affiche maintenant « BOUCLE SOL — OUVERTE : 26 min aller-retour contre 7 min :
conduite A BORD ».

**PREUVE** : build `SPEditor` Succeeded, **3 267 oracles au vert** (+7).

### LE RYTHME DE MESURE (2026-07-28) — [GDD 8.6] enfin opposable

« Le joueur choisit son rythme de mesure ; trop rare laisse dériver, trop
fréquent coûte des ressources et du temps. » **Il n'y avait rien à choisir** : la
poursuite s'achetait UNE FOIS à la conception (`Program::tracking_days`) et la
connaissance du joueur restait ensuite figée pendant 329 jours de vol.

**DEUX TROUS TROUVÉS EN LISANT, ET LE PREMIER EST UNE FAUTE DE PHYSIQUE :**

- **ON POUVAIT MESURER LE FUTUR.** Rien ne bornait `arc_days`. `nav_solution`
  échantillonne l'état VRAI de 8 h en 8 h jusqu'à l'arc demandé — un
  `tracking_days` généreux donnait donc **au feu vert** une solution que seules
  deux semaines d'écoute peuvent produire. `Session::arc_poursuite_disponible`
  borne désormais l'arc par le temps ÉCOULÉ depuis l'injection : à l'injection on
  ne sait RIEN, quelle que soit la somme engagée, et acheter n'avance aucune
  horloge. C'était invisible tant que TCM-1 tombait à J+14 — soit après que les
  14 jours se soient réellement écoulés.
- **LA CONNAISSANCE NE GRANDISSAIT PAS.** `nav_connu_dv` et `nav_sigma_*` étaient
  figés au feu vert. `Session::rafraichir_poursuite` les recalcule à mesure que
  les antennes écoutent — **par PASSE, pas par frame** : le filtre ne gagne une
  observation que toutes les `TRACKING_SAMPLE_S` (8 h), et relancer un fit sur un
  arc long à chaque frame coûterait des milliers de propagations de Kepler. Le
  seuil de recalcul EST la cadence de mesure : un chiffre, une source.

**LA CAMPAGNE CORRIGE AVEC CE QU'ELLE SAIT À CHAQUE RENDEZ-VOUS.**
`nav_campagne_depuis` prend maintenant **deux** solutions (TCM-1 et TCM-2) au lieu
d'une : l'arc grandit entre les deux, et corriger la première avec les mesures de
la seconde serait tricher avec le temps. Quand la poursuite ne bouge pas, les deux
sont la même et le résultat est celui d'avant — l'ancien appel reste exact.

**ET ÉCOUTER SE PAIE.** `Program::tracking_musd` et `Program::tracking_days`
étaient deux nombres **libres et indépendants** : on achetait cent jours de
poursuite pour zéro. `mission::cout_poursuite_me` dérive le prix de la passe de
8 h **déjà dans le modèle** et d'un tarif d'antenne DSN sourcé (~1 100 $/h pour
une 34 m). *Approximation déclarée* [GDD 6.8, Annexe E] : le coût complet d'une
campagne de navigation réelle dépasse largement l'ouverture d'antenne, et c'est le
TEMPS qui porte l'arbitrage — 14 jours d'écoute valent **0,739 M€**, une paille
devant une mission, mais 14 jours qu'il faut avoir laissé passer.

**MESURÉ**, à la même date et sur le même vol :

| Arc acheté | Mesures | σ en vitesse |
| ---: | ---: | ---: |
| 2 j | 6 | **0,002 m/s** |
| 14 j | 42 | **0,000 m/s** |

Le poste CONTRÔLE montre l'arc exploité, le σ qu'il produit, le prix de sept jours
de plus **avant** le clic (piège n°64) et le bouton qui les achète.

**UNE FAUTE DE JUSTESSE CORRIGÉE EN CHEMIN** : `rafraichir_poursuite` prenait la
date d'injection dans `trace_vol`, qui n'est celle **que du premier** vol en
cours — donc fausse dès qu'il y en a deux, et invisible tant qu'il n'y en a
qu'un. Extraite en `mission::flight_injection_days`, lue dans la chronologie de
CHAQUE mission (une chronologie coûte un parcours de segments ; la reconstruire
par `build_flight_trace` coûterait un Lambert et 512 propagations par frame).

**UN SEGFAULT PAYÉ** : `rafraichir_poursuite` déréférençait `ares.etat` sans la
garde `initialisee()` que `publier_trace_vol` porte trois lignes plus haut — au
Titre, aucune partie n'existe. Crash sur la toute première frame, avant même
qu'il y ait une mission à poursuivre. *Une fonction ajoutée à côté d'une autre
hérite de ses préconditions, pas seulement de sa place.*

**PREUVE** : build `SPEditor` Succeeded, **3 280 oracles au vert** (+13).

⚠ **`test_toolchain` a flanché UNE FOIS (23/24) sous charge** — pendant que dix
autres suites et un build UE tournaient en parallèle — puis **24/24 trois fois de
suite** au calme. C'est la suite qui compile du vrai C++, tue de vrais processus
au délai et vérifie que le binaire est effaçable (piège n°71) : elle est sensible
à la charge machine, pas cassée. À ne pas confondre avec une régression.

### EMPAQUETER LA TOOLCHAIN (2026-07-28) — [GDD 18], le mécanisme

**L'ATELIER LOGICIEL NE MARCHAIT QUE SUR LA MACHINE DE DÉVELOPPEMENT, et rien ne
le disait.** Deux dépendances au dépôt, invisibles tant qu'on joue depuis
l'éditeur :
- les chemins d'inclusion pointaient dans `Source/SP/SpaceProgram/…` — **un
  chemin qui n'existe pas dans un build packagé** ;
- le compilateur était cherché dans quatre installations Visual Studio 2022 de la
  machine. Une hypothèse qu'on ne peut pas faire chez le joueur.

**LES EN-TÊTES PARTENT AVEC LE JEU.** `Tools/stage_sdk.py` produit
**`Content/SP/Sdk`** — **49 fichiers, 283 Ko** : tout `astro_core/include` plus
`Kepler.cpp` (que le code du joueur doit LIER depuis que le solveur résout la
correction sur la vraie matrice de transition, piège n°72). On copie l'arbre
entier plutôt qu'une liste triée à la main : une liste se périme au premier
`#include` ajouté, et 283 Ko ne valent pas ce risque.
- **LA SOURCE DE VÉRITÉ NE BOUGE PAS** : `Content/SP/Sdk` est un ARTEFACT, ignoré
  par git. En versionner une copie ferait deux `fen/astro/Kepler.hpp`, dont un
  périmerait en silence.
- **NON-UFS, et c'est tout le point** : ces fichiers sont lus par `cl.exe`, pas
  par Unreal. Empaquetés en UFS ils finiraient dans un `.pak`, où aucun
  compilateur ne sait aller les chercher. D'où
  `+DirectoriesToAlwaysStageAsNonUFS` dans `Config/DefaultGame.ini` — clé dont
  les chemins sont **relatifs à `Content/`**, ce qui est la raison pour laquelle
  la toolchain vit sous `Content/SP/Toolchain` et non à la racine du projet (un
  premier essai l'y avait mise : elle n'aurait tout simplement pas été empaquetée).
- **PREUVE, et elle est directe** : l'exemple de [GDD 15.3] compile contre
  `Content/SP/Sdk` **SEUL**, sans l'arbre source — `cl /I Sdk\include
  vol_joueur.cpp Sdk\src\Kepler.cpp` produit un binaire.

**LE COMPILATEUR SE CHERCHE D'ABORD LÀ OÙ LA DISTRIBUTION LE DÉPOSE** :
`Content/SP/Toolchain/VC/Auxiliary/Build/vcvars64.bat`, puis seulement les quatre
installations de la machine (mode développement). Une distribution qui pose les
Build Tools à cet endroit fonctionne **sans toucher une ligne de code**.

**CE QUI RESTE EST UN ACTE D'EXPLOITATION, PAS DE CODE**, et il faut le dire
franchement : déposer les MSVC Build Tools dans ce dossier. Ils pèsent de 300 Mo à
plusieurs Go — **c'est LE poste de budget que [GDD 18] demande de déclarer** — et
portent leur propre licence de redistribution. Le dossier est donc vide dans le
dépôt, avec un `LISEZMOI.txt` qui dit quoi y mettre.

**ET L'ABSENCE EST ACTIONNABLE.** Le poste imprimait « tâche de packaging » — vrai
mais inutile au joueur. Il imprime maintenant **le chemin exact attendu**
(`Session::toolchain_depot`, renseigné par la couche plateforme qui seule sait où
le jeu est installé). Un atelier qui refuse sans dire ce qu'il attend est une
panne (piège n°42), alors qu'ici la réparation est à portée.

### L'ÉDITEUR DE GRAPHE ÉTAIT DÉJÀ FAIT (constaté le 2026-07-28)

Le §7 listait encore « Ch.15 restant : éditeur de graphe (Normal), toolchain C++
embarquée + bac à sable (Pro) ». **Les deux étaient faits** et la ligne n'avait pas
suivi. Vérifié dans `UEBridge/SPHud.cpp` : palette de primitives sur deux rangées
(chacune NOMMANT la fonction d'API qu'elle est, l'équivalence stricte de
[GDD 2.2] lisible à l'écran), liste des nœuds avec leur type de sortie, nœud
FAUTIF surligné et motif du refus, retrait d'un nœud, et report du résultat dans
les trois composantes RSW. La toolchain et son bac à sable (job object, limite de
mémoire, oracle d'effaçabilité) datent de la même journée.
*Une ligne de reste-à-faire qu'on ne raye pas devient un mensonge sur l'état du
projet — plus coûteux qu'un oubli, parce qu'elle envoie retravailler du fait.*

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

53. **UN OBJET « INVISIBLE » PEUT ÊTRE À LA BONNE PLACE, À LA BONNE TAILLE, ET
    ENTIÈREMENT DEVANT LE PLAN PROCHE.** La Terre ne s'affichait pas depuis la
    cupola. Trois hypothèses plausibles (culling, canal d'éclairage, côté nuit) —
    toutes fausses. Un `UE_LOG` de quatre lignes sur le transform RÉELLEMENT
    appliqué a tranché en une exécution : position `(0, 0, −44,7)`, donc pile au
    nadir, et rayon rendu 42 u. Le calcul suivait tout seul : hémisphère visible
    entre 2,7 et 5,5 u de profondeur, plan proche à 10 u — tout clippé, on voyait
    les étoiles à travers. *Devant un objet absent, MESURER son transform avant de
    soupçonner la visibilité : « au bon endroit » et « visible » sont deux
    questions différentes, et la seconde dépend d'échelles qu'on n'a pas en tête.*
    Corollaire : une capture ne dit que « je ne le vois pas » ; elle ne distingue
    pas « absent » de « clippé » de « noir » de « derrière ». Un chiffre, si.

54. **UN COMPOSANT DU MOTEUR PEUT AVOIR UNE PRÉCONDITION QUE LE PROJET NE REMPLIT
    PAS — et le symptôme est un SILENCE.** `UFloatingPawnMovement::TickComponent`
    ne fait rien sans `PawnOwner->GetController()`. Le pawn de la station n'est
    jamais possédé (l'entrée vient du HUD par le pont) : le déplacement était
    simplement inerte, sans erreur, sans avertissement, pendant que le regard —
    appliqué directement — fonctionnait. *Quand une moitié d'un système marche et
    l'autre pas, la frontière entre les deux dit où chercher : ici, tout ce qui
    passait par le composant du moteur.* Corollaire : lire la source du composant
    AVANT de régler ses paramètres. Les trois paramètres de celui-ci ne pouvaient
    rien, et son second défaut (annuler la vitesse sans entrée) le rendait de toute
    façon inutilisable pour de l'impesanteur.

55. **LA RÉPONSE AU CONTACT SE LIT DANS LE DÉPLACEMENT OBTENU, PAS DANS LE HIT.**
    Deux prédicats essayés, deux échecs mesurés : `IsValidBlockingHit()` devient
    faux à fleur de paroi (`bStartPenetrating`), et `bBlockingHit` non plus ne tient
    pas parce que `SafeMoveUpdatedComponent` réessaie le déplacement après avoir
    résolu la pénétration et ÉCRASE `OutHit`. Résultat : un joueur bloqué contre le
    plafond qui conserve 0,59 m/s de vitesse fantôme. Comparer position d'avant et
    position d'après règle tous les cas d'un coup, coins compris. *Le moteur dit
    mal ce qu'il a touché ; il dit exactement où il vous a laissé aller.*

56. **`FParse::Value` s'arrête sur une virgule.** `bShouldStopOnSeparator` vaut
    `true` par défaut (Parse.h:71) : `-spoeil=-18.2,7.2,-4.6` ne rendait que
    `-18.2`, et le drapeau était silencieusement ignoré (repli sur le point
    d'apparition, donc une capture qui a l'air de marcher et ne prouve rien). Pour
    un triplet, passer explicitement `false`.

Payés en DATANT le vol [GDD 9, 14.3] (2026-07-27) :

57. **UNE PHASE SANS DATE N'ARRIVE JAMAIS — et le mécanisme qui la lit paraît
    faux.** L'insertion et l'EDL étaient modélisés (taux d'anomalie majorés,
    plafond de cadence déduit) et n'ont JAMAIS pu se produire : rien ne les
    datait. Le symptôme n'est pas une erreur, c'est un mécanisme qui *semble*
    ne pas marcher — on soupçonne la loi qui le lit (ici `MissionTempo`), alors
    qu'elle est juste et attend un événement qui n'existe pas. *Devant une règle
    qui ne se déclenche jamais, vérifier d'abord que sa CONDITION peut se
    produire, avant de la relire.* Corollaire : la loi n'a pas changé d'une
    ligne en gagnant deux points d'application.
58. **UNE GRANDEUR CALCULÉE ET NON PUBLIÉE EST UNE GRANDEUR ABSENTE.**
    `astro::launch_window` balaie une carte porkchop dont l'un des deux axes EST
    la durée de transit — et n'exposait que la date de départ. Le moteur savait
    donc « quand partir » et pas « quand on arrive », ce qui a fait croire
    pendant tout un chantier que dater l'arrivée demanderait un nouveau calcul.
    Il ne manquait que deux `double` dans une structure de sortie. *Avant
    d'ajouter un calcul, relire ce que le calcul voisin jette déjà.*
59. **UN ORACLE QUI SUPPOSE UNE DONNÉE DE CONTENU SE TROMPE SUR LE CONTENU, PAS
    SUR LA LOI.** Un oracle de la chronologie prenait `catalog.entries()[0]` en
    supposant une mission martienne : le premier contrat est une constellation
    LEO, et l'échec accusait la persistance. Le catalogue n'a d'ailleurs pas de
    famille `mars` du tout (il a `mars_habite` et `surface`). Réécrit pour
    CHERCHER une entrée avec le MÊME prédicat que le modèle
    (`window_target_for_family(...).impose`), au lieu de recopier une liste de
    familles dans le test. *Un oracle qui duplique une table du modèle teste sa
    propre copie.*
60. **CE QUI VIT SUR L'OBJET D'UI NE SURVIT PAS À UNE SAUVEGARDE.** L'issue du
    vol vivait sur `Session` ; les missions en cours n'étaient pas sérialisées du
    tout. Les deux défauts étaient INOBSERVABLES tant qu'un vol durait zéro
    seconde de temps de jeu — la fenêtre pour sauvegarder au milieu était nulle.
    Faire durer le vol les a rendus certains, et *quitter au menu sauvegarde*.
    *Allonger la durée d'un état rend atteignables tous les bugs de cet état :
    quand une phase passe d'instantanée à longue, inventorier ce qui n'y
    survivait pas.*

Payés en TRAÇANT le vol [GDD 8.3] (2026-07-27) :

61. **UN SENTINELLE NE DOIT PAS VIVRE DANS LE DOMAINE DE LA VALEUR.** La
    signature de l'arc (« a-t-il changé ? ») servait aussi de prédicat (« y
    a-t-il un arc ? ») via un `< 0` réservé. Or elle est bâtie sur la date du feu
    vert, qui est NÉGATIVE dès qu'un vol est parti avant l'origine du calendrier
    — ce que fait toute capture épinglant une croisière. Résultat : un vol
    parfaitement valide rejeté, et un écran vide sans le moindre message.
    Prédicat et signature sont maintenant deux fonctions. *Une valeur réservée
    n'est sûre que si le domaine ne peut pas l'atteindre — et « une date » peut
    toujours être négative.*
62. **UN ORACLE QUI ÉCHANTILLONNE LAISSE PASSER EXACTEMENT CE QU'ON CHERCHE.**
    L'oracle de l'arc balayait un point sur seize et déclarait la polyligne
    saine ; le rendu montrait des segments partant à l'infini. Ce qu'un
    échantillonnage lâche rate, ce sont les points ISOLÉS — c'est-à-dire le seul
    défaut qu'une courbe par ailleurs correcte puisse avoir. *Sur une structure
    de données produite en masse, balayer TOUT ; l'échantillonnage est pour les
    mesures coûteuses, pas pour les invariants.*
63. **DEUX GRANDEURS COHÉRENTES SÉPARÉMENT PEUVENT ÊTRE INCOHÉRENTES ENSEMBLE —
    ET LE SOLVEUR NE PROTESTERA PAS.** La durée de transit venait du meilleur
    transfert des 60 jours à venir (`slop_days`, la largeur opérationnelle de
    « maintenant » : la bonne notion pour dire si une fenêtre est OUVERTE) et
    était appliquée à un départ AUJOURD'HUI. Chaque moitié est juste ; le couple
    (date de départ, durée) ne décrit aucun transfert réel. **Lambert répond
    quand même** — il relie toujours deux points en un temps donné — par un arc
    valide qui plonge à 0,26 UA du Soleil. Aucun chiffre n'avait l'air faux :
    c'est le RENDU qui a montré l'arc sortir du champ. *Quand deux paramètres se
    déterminent l'un l'autre, les prendre à la même source ou les résoudre
    ensemble ; un solveur qui accepte tout ne signale jamais l'incohérence de ses
    entrées.* Corollaire de méthode : **le rendu est un oracle**, et il attrape
    précisément les erreurs de COHÉRENCE que les oracles numériques, écrits
    grandeur par grandeur, ne voient pas.

Payés en CALCULANT la navigation [GDD 7.5, 8.4] (2026-07-27) :

64. **DONNER DU POUVOIR À UN PARAMÈTRE OBLIGE À VÉRIFIER QU'IL EST RÉGLABLE.**
    `Program::dv_margin` était AFFICHÉE au poste mais sans bouton : tant que
    `p_physics` valait 0,985 elle ne servait qu'à alourdir l'étage, et son
    absence de réglage ne gênait personne. Le jour où elle COMMANDE la
    probabilité de navigation, un joueur ne pouvait plus faire aboutir AUCUNE
    mission interplanétaire — piège n°40 rejoué à l'identique, à un chantier de
    distance. *Quand un champ passe de décoratif à décisif, la première question
    n'est pas « le calcul est-il juste » mais « le joueur peut-il l'actionner ».*
    Corollaire : un réglage décisif demande la LECTURE qui le motive — ici le
    Δv de correction au 99e centile, sans quoi on provisionne à l'aveugle.
65. **UN PANNEAU QUI DÉBORDE SIGNALE SOUVENT UNE FAUTE DE CONCEPTION, PAS DE
    MISE EN PAGE.** Cinq lignes ajoutées ont fait sortir le poste CONTROLE de son
    cadre (qui CLIPPE, piège n°42). Le réflexe — raccourcir les libellés — aurait
    masqué le vrai constat : les commandes de programme et l'étude de navigation
    n'ont AUCUN sens une fois le véhicule parti. Les masquer en vol n'est pas un
    gain de place, c'est la vérité de la phase. *Quand un écran ne tient plus,
    demander d'abord ce qui n'aurait pas dû y être à ce moment-là.*

66. **UN FILTRE TROP CONFIANT EST PIRE QU'UN FILTRE IMPRÉCIS.** Une seule
    linéarisation, autour du nominal à 11 000 km de la vérité, donnait un estimé
    correct et une covariance MENSONGÈRE : σ annoncé 0,001 m/s pour une erreur
    résiduelle de 0,3. Le filtre ne se trompait pas sur la valeur, il se trompait
    sur sa propre fiabilité — et c'est la seconde qui décide des marges. Trois
    itérations de Gauss-Newton (ce que `batch_least_squares` fait déjà) et les
    deux coïncident. *Vérifier un estimateur, c'est comparer son erreur RÉELLE à
    l'erreur qu'il ANNONCE, jamais la première seule.*
67. **UNE INTERPOLATION DOIT ATTERRIR EXACTEMENT SUR SON EXTRÉMITÉ.** Le vol de
    caméra [M] repliait son yaw dans ±π pour prendre le plus court chemin, et
    arrivait donc à `yaw_arrivee ± 2π` quand l'écart de départ franchissait un
    demi-tour : le même angle à l'écran, un chiffre différent. L'oracle est passé
    pendant des mois puis est tombé du jour au lendemain — l'attitude de Novellus
    dépend de l'ÉPOQUE RÉELLE [GDD 14.1], et la date avait changé. *Un oracle qui
    dépend de la date du jour finira par tomber ; quand il tombe, c'est le code
    qu'il faut regarder d'abord — ici l'arrivée n'était exacte que par chance.*

Payés en EMBARQUANT la toolchain [GDD 15.1, 18] (2026-07-28). Morale commune :
**un échec silencieux ressemble à une absence de capacité** — trois fois de
suite, un défaut mécanique s'est déguisé en « la machine n'a pas de compilateur ».

68. **`cmd /c` DÉCOUPE AU PREMIER GUILLEMET** si la commande entière n'est pas
    encadrée d'une paire supplémentaire. Ni erreur, ni journal, ni processus :
    juste un silence, et une chaîne qui conclut à une machine mal installée.
69. **NE PAS CLASSER UNE ISSUE SUR UNE PHRASE DU SHELL.** Le script
    d'environnement du compilateur imprime lui-même « n'est pas reconnu » sans
    aucun rapport avec le code du joueur : le prendre pour un compilateur absent
    faisait passer un code FAUX pour un défaut d'installation. On tranche sur la
    présence d'un DIAGNOSTIC (`error C…` / `erreur C…`, même code dans les deux
    langues), c'est-à-dire sur une preuve, pas sur une tournure.
70. **UN ARTEFACT QUI DOIT ÊTRE REMPLACÉ DOIT ÊTRE NEUF, PAS ÉCRASÉ.** Windows
    garde un binaire verrouillé un instant après la fin du processus qui
    l'exécutait ; le lieur échouait sur `LNK1104`, sans jamais émettre d'erreur de
    compilation. Un nom unique par compilation supprime la question au lieu de
    mieux la contourner. *Supprimer-puis-recréer est une course ; créer neuf n'en
    est pas une.*
71. **ON TUE UN ARBRE, PAS UN PROCESSUS.** Le bac à sable lançait le code du
    joueur derrière un `cmd.exe` et, au délai, appelait `TerminateProcess` sur ce
    shell : le programme du joueur, lui, survivait et brûlait un cœur
    indéfiniment. **L'oracle du délai passait quand même** — il vérifiait la
    DÉTECTION, pas la MORT. Le symptôme n'apparaissait qu'au run suivant, en
    `LNK1104` sur un binaire qu'un fuyard de la veille tenait ouvert ; mesure :
    trois processus fuyards, ~16 min de CPU chacun. Lancer directement + **job
    object** tué en bloc (ce qui donne aussi la limite de MÉMOIRE de [GDD 18]).
    *Un bac à sable qui laisse échapper ses détenus n'en est pas un — et un
    oracle qui teste l'intention plutôt que l'effet ne prouve rien.* L'oracle
    vérifie désormais que le binaire est EFFAÇABLE après le délai.
72. **UN OUTIL FOURNI DOIT ÊTRE JUSTE, OU NE PAS ÊTRE FOURNI.**
    `ares::vol::Solveur` corrigeait Δv = −Δr/τ : exact en champ nul, faux dès
    qu'un arc courbe. Sur une croisière de Mars il commandait 158 m/s dans une
    direction qui AGGRAVAIT le manque au but (4,2 → 5,2 M km). Le joueur qui
    recopiait l'exemple de [GDD 15.3] cassait son vol, et **la faute était dans
    l'API, donc introuvable depuis son code**. Une approximation déclarée reste
    légitime ; une approximation qui fait pire que ne rien faire est un outil
    cassé. Corollaire d'architecture : ce dont le joueur a besoin doit être À SA
    PORTÉE — c'est ce qui a fait descendre la matrice de transition dans
    `astro_core`.

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
  `SPSolarSystem`).
  **NUANCE (2026-07-27)** : le plafond de précision GPU impose quand même une
  HOMOTHÉTIE de centre l'œil sur les corps rendus en géométrie. Elle est encadrée
  des deux côtés — `RENDER_MIN_UU` (la surface la plus proche) et `RENDER_MAX_UU`
  (le plafond GPU) — et les lointains sont repliés par une hyperbole croissante
  (`RemapDist`) quand les deux ne peuvent pas tenir ensemble, ce qui arrive dès
  qu'on regarde depuis Novellus. **Ce n'est pas une compression de profondeur au
  sens de `scaled_space`** : une mise à l'échelle radiale de centre l'œil laisse la
  projection EXACTEMENT invariante (position écran et taille angulaire), elle ne
  touche qu'à la profondeur. Voir §3 point 8 et le piège n°53. Le « km » du pont (`cam.dist_km`, `distance_cadrage`, vol_cam)
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

## 7. LE PROCHAIN PAS (état au 2026-07-27, après la chronologie de vol)

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
~~2. La CHRONOLOGIE de vol (dater l'insertion et l'EDL)~~ — **fait le 2026-07-27**
   (section « LA CHRONOLOGIE DE VOL » du §2). Le vol DURE : segments jointifs
   dérivés de Kepler et de la durée de transit de la vraie fenêtre, gate
   d'arrivée dont le refus chiffre l'attente, plafond de cadence qui mord enfin
   à l'insertion et à l'EDL, missions en cours sérialisées. Ce qui manque n'est
   plus la date de la manœuvre : c'est la manœuvre elle-même.

~~2a. La TRACE du vol (l'arc dans le monde)~~ — **fait le 2026-07-27** (section
   « LA TRACE DU VOL DANS LE MONDE » du §2). Le tracé endormi de `SPSolarSystem`
   est réveillé : arc de Lambert résolu entre les deux corps aux dates de la
   chronologie, propagé par Kepler, figé au feu vert, avec ses nœuds de manœuvre.

~~2b. La DISPERSION de navigation (corridor, P(nav) calculé)~~ — **fait le
   2026-07-27** (section « LA DISPERSION DE NAVIGATION » du §2). `p_physics`
   n'est plus 0,985 : c'est P(la marge provisionnée couvre la correction de
   mi-parcours), calculée de Gates à Maxwell en passant par Oberth et la matrice
   de transition. Le corridor 3σ est publié, dessiné et chiffré au terminal.

2. **Le vol MANUEL des missions vécues** [GDD 9] : le joueur exécute les manœuvres
   au lieu d'un tirage déterministe. **La chronologie lui donne son squelette, la
   trace son décor, la dispersion son enjeu** : chaque segment critique est un
   rendez-vous daté, à une position connue, avec un Δv chiffré à trouver — et
   c'est ce qui rendra supportables les 10 minutes réelles que le plafond impose
   aujourd'hui à une insertion pendant lesquelles il n'y a rien à faire. Restent :
~~2b2. La POURSUITE et la CAMPAGNE de correction~~ — **fait le 2026-07-28**
   (section « LA POURSUITE ET LA CAMPAGNE DE CORRECTION » du §2). Filtre par lots
   bayésien itéré sur les vraies stations du DSN ; la correction se calcule sur
   ce que le joueur CROIT ; `tracking_days` décide du sort du vol.

~~2c1-c2. La MANŒUVRE comme acte du joueur, et l'assistance par MODE~~ — **fait
   le 2026-07-28** (section « LA MANŒUVRE EST UN ACTE DU JOUEUR »).

~~2. L'ÉDITEUR DE TEXTE DU MODE PRO~~ — **fait le 2026-07-28** (section
   « L'ATELIER LOGICIEL DU MODE PRO » du §2). Le poste CONTRÔLE a deux faces ;
   la chaîne COMPILER → BANC D'ESSAI → TÉLÉVERSER → EXÉCUTER est complète et
   gardée étape par étape ; le Δv du code du joueur arrive dans `tcm_commande`.
   Mesuré de bout en bout par oracle : son C++ divise le manque au but par 140,
   et son propre garde-fou refuse d'agir sur une solution dégradée.

~~2. LEVER LE HORS-DOMAINE EN ANOMALIE~~ — **fait le 2026-07-28** (section « LE
   HORS-DOMAINE MORD » du §2). `fly_mission` lit `code_embarque` /
   `code_non_couvert`, figés au feu vert et sauvegardés ; l'atelier avertit AVANT
   le décollage, quand c'est encore actionnable. Le banc d'essai n'est plus un
   décor payant.

Reste, par ordre de valeur :

~~2. CE QUE COÛTE DE NE RIEN EMBARQUER~~ — **fait le 2026-07-28** (section « LE
   PRIX DE L'INACTION » du §2). Décision de l'utilisateur : **la correction
   tardive**. Le filet posé au feu vert est retiré, une correction redevient un
   rendez-vous daté, et le même vol donne 4 205 263 km ou 19 km selon qui l'a
   tenu. `code_success_prob` mord enfin, et le GDD 9.3 est respecté à la lettre
   (l'adjoint n'est pas dégradé, il n'agit qu'en l'absence du joueur).

~~2. LE DÉLAI LUMIÈRE SUR LA COMMANDE~~ — **fait le 2026-07-28** (section « LE
   DÉLAI DE COMMUNICATION » du §2). `comms_delay_s` est enfin consommé, le
   terminal affiche le délai que [GDD 8.3] réclamait, et la commande s'applique à
   `now + d/c`. **MAIS la mesure a corrigé la prévision écrite ici** : 24 s à
   TCM-1, 13 min à TCM-2, et un coût sur le manque au but **sous le kilomètre**.
   Le délai n'est donc pas le levier de l'autonomie en croisière.

~~2. L'EDL COMME ÉVÉNEMENT COMMANDÉ~~ — **fait le 2026-07-28** (section « LA
   BOUCLE SOL » du §2), et la réalité a répondu à la question qui semblait ouverte :
   l'EDL n'est PAS un événement commandé, elle ne peut pas l'être. 26 min
   d'aller-retour contre 7 min de descente — la boucle sol ne se ferme pas, donc
   la descente est conduite à bord, comme tout atterrisseur martien réel. Rien à
   arbitrer, rien à gater : le véhicule porte son logiciel, sa fiabilité est déjà
   celle qu'achète `test_hours`, et un code joueur hors domaine tombe déjà. Le
   moteur DIT désormais pourquoi, au lieu de le laisser deviner.
~~3. EMPAQUETER LE COMPILATEUR~~ — **fait le 2026-07-28, côté MÉCANISME** (section
   « EMPAQUETER LA TOOLCHAIN » du §2). Les en-têtes partent avec le jeu
   (`Content/SP/Sdk`, 283 Ko, staged en NON-UFS, prouvé en compilant l'exemple
   de [GDD 15.3] contre lui seul) ; le compilateur se cherche d'abord dans
   `Content/SP/Toolchain` ; l'absence imprime le chemin attendu.
   **RESTE, et c'est de l'exploitation** : déposer les MSVC Build Tools (300 Mo à
   plusieurs Go, licence de redistribution propre) dans ce dossier.

   ~~(b2-reliquat) la POURSUITE en cours de vol~~ [GDD 8.6] — **fait le
   2026-07-28** (section « LE RYTHME DE MESURE » du §2). L'arc est borné par le
   temps écoulé (on ne mesure plus le futur), la connaissance grandit passe par
   passe, écouter se paie au tarif d'antenne, et le poste porte le bouton.
~~3. Ch.15 restant~~ — **SANS OBJET (constaté le 2026-07-28)** : l'éditeur de
   graphe (Normal) ET la toolchain + bac à sable (Pro) étaient faits depuis le
   2026-07-28 ; la ligne n'avait pas suivi. Voir « L'ÉDITEUR DE GRAPHE ÉTAIT DÉJÀ
   FAIT » au §2.
4. **Tri §5.4 étape 4** : purger `_archive` sauf `build_vk/` (≈ 115 Mo).
   ⚠ **TENTÉ le 2026-07-28 sur demande de l'utilisateur, BLOQUÉ** par le contrôle
   de sécurité du harnais (suppression récursive refusée deux fois, en `rm -rf`
   comme en `Remove-Item -Recurse`). **Tout le travail de sûreté est fait et reste
   valable** :
   - le binaire de référence NE DÉPEND PAS de `extern/` — vérifié sur sa table
     d'imports (aucun `glfw3.dll` : glfw est lié statiquement) **puis par une
     capture réelle** de `solar_system_map.exe` (2,76 Mo de BMP, exit 0) ;
   - `_archive` est **entièrement commité** (652 fichiers suivis, dont 527 dans
     `build`/`dist`/`extern`, **aucune modification non commitée**) : la
     suppression est récupérable dans l'historique ;
   - liste exacte à supprimer, `build_vk/` excepté :
     `app ui astro_core mission extern scripts dist build _backup_render
     CMakeLists.txt space_program_3d.cpp` (garder aussi `README.md` et `saves/`).
   Après suppression, relancer la capture de référence pour confirmer que
   `build_vk/` est toujours autonome.

À SURVEILLER maintenant que le temps coule : le tick de recherche est appelé une
fois par frame avec le total quantifié, pas une fois par sous-pas (approximation
déclarée dans `jeu.hpp`) ; et `AresLayer::avancer` — donc `livrer_courrier` — tourne
désormais à chaque frame au lieu d'une fois par mois. Correct, mais si le catalogue
grossit beaucoup, c'est là qu'il faudra un déclencheur par frontière de mois.
