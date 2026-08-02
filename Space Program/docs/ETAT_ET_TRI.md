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

**`-spvecu`** **met le joueur À BORD** [GDD 9, décision 18] : la MISSION VÉCUE.
Réutilise toute la machinerie de `-spvol` (dont il implique `=croisiere`, la seule
phase où le monde peut être accéléré) au lieu d'en poser une seconde, et **pose
les conditions de [GDD 9.2] dans le MODÈLE plutôt que de désactiver la porte** :
rang terminal, `sejour_long` et recyclage qualifiés, puis `Session::embarquer()` —
la porte du bouton. L'état demande autrement une carrière entière : sans ce
drapeau, ni la télémétrie vitale du poste CONTRÔLE ni le gel de l'agence [GDD 9.3]
ne se photographient. À combiner avec `-sppost=3` et `-spcadence=4`.

**`-spcode[=vol]`** **ouvre l'ATELIER LOGICIEL du mode PRO** [GDD 15.1, 15.5] :
la partie de capture démarre en PRO, et le poste CONTRÔLE s'ouvre sur sa face
éditeur. `-spcode=vol` reste sur la CONDUITE DE MISSION, en PRO — l'autre face,
celle où le logiciel embarqué décide. Même office que `-spvol` : le mode d'aide
se choisit à la création d'une partie, écran qu'une capture ne traverse pas ;
sans ce drapeau, l'éditeur serait un écran que rien ne peut photographier.
À combiner avec `-sppost=3`.

**`-spantimatiere`** **qualifie la filière de fin d'arbre et fait couler son
stock** [GDD 5.12.12, 19.3]. Le bloc ANTIMATIÈRE du poste AGENCE — débit de
l'usine avec sa puissance et son rendement, plafond réel **avec sa cause**, écart
au seuil relativiste — ne s'affiche que filière qualifiée, délibérément (sinon il
ferait du bruit pendant toute la partie). Il était donc invisible à toute
capture, alors qu'il porte toute la calibration de fin de jeu [Annexe E]. Même
office que `-spvol` et `-spvecu` : on pose l'**état du modèle**, et le stock est
obtenu en faisant **couler la production réelle** sur l'horizon de calibration —
jamais en écrivant un nombre de grammes, qui ne prouverait que l'existence de la
ligne d'affichage. À combiner avec `-sppost=0`.

**`-sppassation`** **amène l'Architecte en FIN DE VIE** [GDD 3.4, 3.5]. La
passation demande une carrière entière (53 ans de temps de jeu) et une agence qui
laisserait couler ce temps sans rien entreprendre ferait faillite bien avant
(mesuré : **six ans**) : l'instant est inatteignable en capture. Le drapeau pose
l'ÂGE — un fait du personnage — et laisse le MODÈLE en tirer la fin de fonction
au tick suivant ; il ne pose ni la modale ni le drapeau de passation, sinon la
capture ne prouverait que lui-même. Il monte aussi le rang à Principal, comme
`-spvecu` monte le rang terminal : une passation de Stagiaire ne montrerait pas ce
que [décision 6] a de particulier. Archivé : `ue_passation.png`, et
`ue_architecte_age.png` pour la ligne d'âge du poste AGENCE (`-sppost=0`).

**`-spvaisseau[=<mètres>]`** **cadre LE VAISSEAU CONÇU, dans son monde** [GDD 12.2,
17.2, 17.4]. « Du plan système au plan vaisseau (mètres) par simple zoom » : c'est
un instant qui existe, mais il demande un vol EN COURS *et* l'œil à quelques
dizaines de mètres d'un objet situé à des centaines de millions de kilomètres —
aucune capture ne l'atteint autrement. Le drapeau réutilise `-spvol=croisiere` (il
ne pose pas un second état) et verrouille le focus sur `FOCUS_VAISSEAU`, le même id
qu'un clic du joueur. ⚠ **La distance est en MÈTRES ENTIERS, et pas par
coquetterie** : `-spdist=` passe par un parse flottant **dépendant de la locale**,
et « 0.4 » vaut 0,0 sur une machine française — le drapeau paraissait appliqué
alors que la caméra ne bougeait pas (piège n°97). Archivé :
`ue_vaisseau_concu_monde.png`. La COUPE du même véhicule se capture par
`-spscene=iss -sppost=4` (`ue_coupe_conception.png`).

**`-spnep`** **pose une FILIÈRE ALIMENTÉE dans l'atelier** [GDD 5.12.1, 6.2, 6.5].
Le poste CONCEPTION s'ouvre sur une pile chimique, qui ne porte ni centrale ni
radiateurs — correctement : un moteur chimique n'en a pas. La ligne qui prouve que
« énergie ≠ propulsion » a un consommateur n'apparaît donc sur **aucune** capture
par défaut. Même office que `-spvol` et `-spantimatiere` : on pose l'**état du
modèle** (un étage `NEP-1MW` sur réacteur, réservoir au xénon), et la puissance,
la masse de centrale, la surface de radiateur et la masse au décollage sont
ensuite calculées par le chemin du jeu (`evaluate_design`) — jamais écrites à la
main, ce qui ne prouverait que l'existence de la ligne d'affichage.
À combiner avec `-sppost=4`. Archivé : `ue_centrale_nep_conception.png`.
**`-spnep=qualifie`** ajoute la QUALIFICATION de la branche 6 *et de la branche 1*
[GDD 5.4] : sans elle l'étude s'arrête au verrou « NON QUALIFIÉ » — d'abord le
moteur, puis le lanceur, un étage NEP pesant 41 t avec sa centrale — et le bilan
de viabilité n'est jamais calculé, donc la ligne SOUS-SYSTÈMES AVANCÉS de
[GDD 12.4] n'est photographiable par aucune capture. Archivé :
`ue_filieres_avancees_12_4.png`.
Le drapeau pose AUSSI le même moteur au **programme de mission**, ce qui rend
photographiable le second effet de la fusion des catalogues : avec `-sppost=3`,
le prix retenu **avec sa confiance et son intervalle** (« 600.0 M$ retenu
(D : 80.0-600.0) ») et le verdict de l'arbre quand le nœud manque (« NON QUALIFIE :
RECHERCHER nep_megawatt »). Archivé : `ue_catalogue_moteurs_reels.png`.

**`-sptour[=<id>]`** **pilote CAT-13 et choisit un TOUR d'assistance** [GDD 5.11].
La ligne TRAJECTOIRE du poste CONTRÔLE n'existe que pour une mission qu'un tour du
catalogue peut servir — jamais la mission de départ, qui va en orbite basse. Le
drapeau pose donc l'ÉTAT (rang Senior, les quatre nœuds de CAT-13, une marge de
correction de conception normale) et **le tour est ensuite pris par la porte du
jeu** (`Session::choisir_tour`), qui fait tourner le vrai optimiseur : le Δv, la
masse et la durée affichés sont CALCULÉS, jamais écrits. `-sptour` nu montre le
transfert DIRECT — l'autre moitié du troc, et le couple se lit ensemble
(`ue_tour_direct.png` / `ue_tour_assistance.png`). À combiner avec `-sppost=3`.
**Avec `-spscene=map`, le drapeau fait en plus VOLER le tour** : le monde attend
l'opportunité (exactement ce que le gate impose au joueur), la trajectoire trouvée
est figée sur la mission, puis le temps court jusqu'au milieu de la première
jambe. C'est le seul moyen de photographier la trace MULTI-JAMBES dans le monde
(`ue_tour_trace_monde.png`) — l'arc qui repart de la Terre, la manœuvre profonde à
l'aphélie et le survol de retour.

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

| **LA MISSION VÉCUE [GDD 9, décision 18]** : le joueur EMBARQUE | **FAIT (2026-07-28), VÉRIFIÉ PAR CAPTURE.** Le chapitre 9 avait un modèle complet et **aucune porte d'entrée** : `try_embark` sans appelant, `VitalState` jamais instancié, `suspended` posé par les seuls tests, `journal_absence` orpheline. Quatre choses désormais réelles : (1) **les vivres pèsent** — leur masse entre dans Tsiolkovsky, la durée est la **période synodique** calculée (779,9 j, écrite nulle part), les boucles se déduisent de la branche 4, et le recyclage quasi fermé rend **16,1 t au décollage** ; (2) **la porte** [GDD 9.2] — mission habitée, une seule à la fois, confiance au seuil habité, et pour une mission LONGUE (> incrément ISS) le **rang terminal + `sejour_long`** ; on embarque **avant le feu vert** (piège n°71) ; (3) **l'absence** [GDD 9.3] — chaîne financière suspendue, confiance REPOSÉE à chaque tick, `journal_absence` écrite au retour ; (4) **les réserves épuisées tuent** — Critique + exposition humaine = catastrophe par le barème de 10.3, et la fin de partie tient à `lived.active` parce que `consequences_for` refuse expressément de trancher. Sauvegarde **V2** (`Reader::version()`, premier usage : une V1 se relit intacte). Surface : bloc « VIE À BORD » au poste CONTRÔLE + EMBARQUER/DÉBARQUER avec motif de refus. **PREUVE** : `ue_vecu_poste_controle.png` (`-spvecu`) — 6 à bord, 896,9 j d'autonomie, boucles 93 %/85 %, « ARES : sous l'adjoint — confiance gelée à 70 », croisière, arrivée dans 322 j. Voir §2, « LA MISSION VÉCUE » |

| **LE VERROU DES RADIATIONS [GDD 6.6, 7.7, 19.7]** : l'environnement devient un ACTEUR | **FAIT (2026-07-28), VÉRIFIÉ PAR CAPTURE.** `env/Radiation.hpp` était complet, ancré sur l'Annexe B, et **sans aucun consommateur** ; `debris.tick` ne vivait que dans le tick mort, si bien que les nuages s'accumulaient **sans jamais retomber**. Désormais : (1) **le blindage est une masse DÉRIVÉE** — 25 m³/personne (NASA), cylindre L=2D, d'où 164 m² pour six et **32,8 t à 20 g/cm²** ; (2) **la dose s'accumule** sur le chemin vif, géométrie du ciel lue sur la phase, GCR anti-corrélé au cycle solaire ; (3) **elle appartient au personnage** (`dose_architecte`), verrouille `peut_embarquer` à 1 Sv et se sérialise ; (4) **les débris retombent** — 300 km : 698 → 0 objets en 2 ans, 1200 km : inchangé [GDD 7.8, 10.5]. **ET LE MODÈLE ENSEIGNE CE QUE PERSONNE N'A ÉCRIT** : doubler la masse au décollage (205 → 397 t) n'achète que **11 % de dose en moins** (0,94 → 0,83 Sv) — le GCR ne se blinde pas, la réponse est un transit plus court. Un aller-retour martien nu = 0,94 Sv contre 1,0 Sv de limite de carrière. Surface : curseur de blindage (masse + dose promise) et deux lignes de dose dans « VIE À BORD ». Voir §2, « LE VERROU DES RADIATIONS » |

| **LES ANOMALIES [GDD 9.5, 9.1]** : la bibliothèque se produit enfin | **FAIT (2026-07-28), VÉRIFIÉ PAR CAPTURE.** `Events.hpp` tirait des événements calibrés depuis toujours et **personne ne les consommait**. `mission/Avaries.hpp` : une éruption est un INSTANT (une dose, le blindage décide), une panne est un ÉTAT qui coûte CHAQUE JOUR — boucles dégradées, épuration usée, fuites, surconsommation, boucle sol coupée. **Réparer est une CAPACITÉ, pas un dé** [GDD 5.10] : la branche 4 l'achète, deux nœuds raccourcissent le travail, et l'avarie coûte pendant les travaux. **SYMÉTRIE DU BLINDAGE** : inutile contre le GCR, **exponentiel contre un SPE** — 5,00 Gy létaux nus, 1,32 Gy derrière 20 g/cm² (survivable, pas inoffensif) : la raison d'être des abris anti-tempête. **TROIS DÉFAUTS DU MODÈLE TROUVÉS PAR LA MESURE** : paramètres SPE incohérents avec l'Annexe B (19,9 Gy par croisière — recalibrés sur cible vérifiée par oracle, 1,55 Sv SPE contre 0,91 GCR, 3,8 % au-dessus du gray contre 41 %) ; blindage partant de zéro (un équipage n'est jamais nu — coque 7,5 g/cm² déclarée, non facturée) ; activité solaire évaluée à « maintenant » au lieu de la date de fenêtre. Tirage **rejouable** : 400 j d'un bloc ou 8 × 50 j → mêmes pannes, mêmes dates, même dose. Voir §2, « LES ANOMALIES SE PRODUISENT ENFIN » |

| **LES DEUX HORLOGES [GDD 6.7, 14.4, 3.4]** : le vieillissement différentiel existe | **FAIT (2026-07-29), VÉRIFIÉ PAR MESURE.** `rel::DualClock` était déclaré, **sauvegardé, rechargé — et `advance` n'avait aucun appelant** : l'écart d'âge que 3.4 fait peser sur la passation valait zéro pour toute mission, et l'âge biologique avançait du **calendrier**. Taux retenu : `dτ/dt = (1 + Φ/c²)/γ(v)` — **exact en v** (tout β), premier ordre en Φ/c² (borné à 1e-8, terme négligé en 1e-16, déclaré). **Deux identités képlériennes EXACTES** remplacent toute intégration : ⟨1/r⟩ = 1/a ⇒ ⟨v²⟩ = μ/a et ⟨Φ⟩ = −μ/a, sans hypothèse sur l'excentricité ; corollaire vérifié à 1e-12, **|⟨Φ⟩| vaut exactement 2× le terme cinétique**. **CONFRONTÉ AUX VALEURS PUBLIÉES** : GPS **+38,574 µs/j** (publié +38,6), ISS **−24,584** (≈ −25), v_RMS Terre **29 784,7 m/s** (29 784,8), Hohmann **258,9 j** (259). **LE SIGNE CONTREDIT LE CLICHÉ** : vers Mars le vaisseau est plus HAUT dans le potentiel ET plus LENT que la Terre — les deux termes vont dans le même sens, le voyageur revient **PLUS VIEUX de 0,25 s** ; en orbite basse le signe s'inverse, avec une seule formule. Et 6.7.2 est **démontré** au lieu d'être affirmé : sous la seconde par aller-retour, contre **2,9 ans sur 10** à β = 0,7. Géométrie **gelée au départ** (mêmes éphémérides que la fenêtre), sauvegarde **V3**. Au passage : **`medical_risk_factor` écrit en dur à 1,0** — le module médical de Novellus coûtait 110 M€ pour rien [GDD 11.6] ; et **`GameState::tick` SUPPRIMÉ** (piège n°72 soldé). Voir §2, « LES DEUX HORLOGES » |

| **L'ANTIMATIÈRE [GDD 5.12.12, 19.3, 11.5]** : le verrou de fin de jeu s'exécute | **FAIT (2026-07-29), VÉRIFIÉ PAR MESURE.** Dernier maillon de la chaîne relativiste **sans aucun appelant hors tests** : quatre paramètres de production, **pas un gramme nulle part**. Le **débit n'est pas un paramètre libre** — `ṁ = P/E` — donc il se dérive de la **marge de puissance de Novellus**, ce qui fait de l'infrastructure le levier annoncé (les 5 MW de CAT-11 rendent 1,58e-3 g/an, l'ordre de grandeur exact du défaut tabulé : les deux champs étaient cohérents, seulement débranchés). **Un stock qui fuit est un ÉQUILIBRE, pas un cumul** : `dS/dt = ṁ − λS` intégrée **exactement**, donc 1 bloc de 3 650 j = 365 tranches de 10 j à 1e-15. **VERDICT MESURÉ** : équilibre **4,32 mg** sous 5 MW (38 kW seulement au départ), quand β = 10⁻⁴ — indétectable — réclame **750 g** pour 1 g de confinement. Ce n'est pas long, c'est **impossible** : c'est le **confinement** le verrou, et la **fuite** qui borne (230× sous la capacité). [GDD 19.3] le disait ; le modèle le chiffre. **Non recalibré** : le GDD diffère ces nombres [Annexe E] et son invariant est justement le hors-échelle. CAT-11 reste volable, aucun verrou nouveau. Le poste AGENCE **affiche le cul-de-sac au lieu de le subir** (plafond réel *avec sa cause*). `beta_croisiere` bascule l'horloge sur la cinématique **pure** au-delà du seuil — exercé à β = 0,7 : le bord bat à 71 % du sol. Sauvegarde **V4**. Voir §2, « L'ANTIMATIÈRE EXISTE — ET ELLE DIT NON ». ⚠ **LE VERDICT « HORS D'ÉCHELLE » ÉTAIT FAUX, ET SA CAUSE AUSSI** — voir la ligne suivante : la puissance était prise sur la **marge de Novellus**, donc aucune recherche de branche 6 ne pouvait la déplacer |

| **LA CALIBRATION DE FIN DE JEU [Annexe E, 6.7.2, 3.5]** : le régime relativiste devient atteignable | **FAIT (2026-07-29), TRANCHÉ PAR LE CORPS DU GDD.** La passe précédente concluait « structurellement impossible, c'est une donnée de calibration, pas un défaut de code », et laissait trois issues au choix du GDD. **Les deux affirmations étaient fausses.** (1) **L'Annexe E ne diffère pas la question, elle en nomme la dépendance** — « vitesse maximale souhaitée en fin d'arbre » — et le corps du GDD la dit à trois endroits : [6.7.2] « seule l'antimatière **franchit** β ≳ 0,3 », [3.4] β ≈ 0,9 « se mérite par l'ingénierie », [3.5] la fin de la branche 6 « demande souvent **plusieurs vies** ». L'issue (a) — « c'est voulu, CAT-11 est une asymptote » — est donc **réfutée par le document**. (2) **C'était bien un défaut de code, le 8ᵉ « nommé mais non connecté » de la série** : la puissance de production était `station.power_margin_kw()`, la **marge de Novellus** — 38 kW au départ, 5 MW au mieux. Le « vrai levier d'équilibrage » du GDD n'était pas branché sur son levier : *aucune* recherche de branche 6 ne pouvait bouger le débit. Or [5.12.12] dit où le prendre — « le rendement énergétique **couple la production à la branche énergie** » — et une usine à antimatière n'est pas un module de station. **CE QUI N'EST PLUS UN RÉGLAGE** : `energy_j_per_g` avait un **plancher physique** ignoré — on ne fait pas un antiproton sans son proton, donc E ≥ 2mc² = **1,7975e14 J/g**. Le champ est remplacé par un **rendement η**, et l'ancien défaut de 1e17 J/g **supposait η = 1,8e-3**, soit six ordres au-dessus du CERN (≈ 1e-9) — une hypothèse de fin d'arbre posée sans être déclarée [GDD 6.8, 12.5]. **LES PALIERS SONT INVERSÉS DES ÉNONCÉS, PAS CHOISIS** : fusion (η = 1e-6, 1e13 W) doit rester pré-relativiste [5.12.11] → β = 6e-7, **sous le seuil** ✓ ; abouti (η = 1e-4, 2 PW, confinement 1e7 g, fuite 1e-5/j) doit franchir 0,3 [6.7.2] → **β = 0,481 à l'équilibre**, atteint en **139 ans** ✓ = « plusieurs vies » [3.5]. Et le confinement à 1 g n'était pas un plafond [5.12.12] mais **un mur** : la cible en réclame 3,83e6. **CE QUI RESTE HORS D'ATTEINTE L'EST PAR LA PHYSIQUE** : l'aller-retour [6.7.4] coûte le ratio **puissance quatre** (26× l'aller simple) et reste refusé ; β = 0,9 est **impossible sur la sonde de 5 t, possible sur 100 kg en 152 ans** — donc « β découle de l'**architecture** » [décision 10] cesse d'être une phrase. **ET UN ORACLE QUI PASSAIT POUR UNE RAISON FAUSSE** : « le stock CONVERGE — 200 ans de plus n'ajoutent rien » était vert parce que **le calendrier s'arrêtait à la faillite**, pas parce que le stock convergeait. Le fait de jeu qui le remplace vaut mieux : les 140 ans de [3.5] **exigent une agence qui produit pendant 140 ans** — la pression d'inactivité [13.2] et le programme de fin de jeu sont couplés. Piège **n°82**. **PREUVE** : `SPEditor` Succeeded, **3 600 oracles au vert** sur les 12 suites, nouveau drapeau `-spantimatiere`, et `ue_antimatiere_calibree.png` — débit 3,511e4 g/an sous 2e15 W à rendement 1e-4, plafond réel 9,613e6 g *borné par la fuite*, seuil β = 0,3 **ATTEINT** |

| **LE BILAN DE VIABILITÉ DIT ENFIN QUELQUE CHOSE [GDD 4.1]** | **FAIT (2026-07-29), TROUVÉ EN AUDITANT UNE CAPTURE.** Trois défauts en chaîne, chacun masquant le suivant. (1) La table des **termes de contrat** vivait dans une boucle interne à `seed_catalogue` : toute mission hors catalogue naissait avec des termes NULS, d'où « 0 / 0 M EUR » et un VERROU rouge dans **chaque capture en vol** — que la doc *expliquait* au lieu de corriger. (2) `MissionPlan::evaluated` existait et **personne ne le lisait** : une mission fraîchement acceptée, donc avant tout passage au poste CONCEPTION, s'annonçait ratée sur les quatre axes. (3) `finalize` faisait `why.clear()` et remplaçait la cause exacte (« AUCUN LANCEUR NE SOULEVE CETTE MASSE ») par une liste de symptômes dont **trois quarts n'avaient jamais été calculés** — piège n°42 au cœur du modèle. La capture montre désormais : masse 193 235 kg, *« coût, calendrier et fiabilité NON ÉVALUÉS : l'étude s'arrête à la masse »*, `VERROU : AUCUN LANCEUR NE SOULEVE CETTE MASSE`. **Et le chiffre révélé** : 193 t pour un aller-retour martien habité, hors de portée de tout lanceur — la réponse réelle est l'assemblage en orbite, désormais dit au lieu d'être noyé. Piège **n°77** : une alarme rouge dans TOUTES les images de référence est un bug de l'alarme jusqu'à preuve du contraire. Voir §2, « L'AUDIT DES POSTES » |

| **LE CATALOGUE EST RÉALISABLE [GDD 4.1, 5.4, 12.1]** : il ne l'était qu'à 45 % | **FAIT (2026-07-29), MESURÉ CONTRAT PAR CONTRAT.** Arbre entier TRL 9, rang Directeur — donc la limite PHYSIQUE : **6 contrats sur 11 étaient inachevables**, dont 3 dont la charge **nue** dépassait le plafond des lanceurs. Deux causes, toutes deux « nommé mais non connecté » : **`lanceur_super_lourd` était un prérequis de CAT-09 qui ne débloquait AUCUN lanceur** (le catalogue s'arrêtait à 8,3 t), et **les 4 nœuds « lanceur » ne gardaient RIEN** — toute la gamme était disponible dès la première mission, la branche 1 était décorative. Corrections **ancrées sur des lignées réelles** [GDD 12.1] : `L-D super-lourd` 130 t / 1 400 M$ / R = 0,970 (Saturn V, SLS Block 2 — fiabilité **plus basse** que le lourd, car un géant vole peu donc démontre peu) ; `L-C lourd` **8,3 → 22,8 t** (Falcon 9 Block 5 — un « lourd » à 8,3 t n'a aucune lignée réelle, et ce trou de **15,7×** forçait CAT-04, dépassant de 4 %, à acheter un Saturn V à 1 457 M$ pour un contrat à 240) ; **filtrage par l'arbre** avec deux verdicts distincts — « NON QUALIFIÉ : RECHERCHER *x* » (direction) vs « AUCUN LANCEUR NE SOULÈVE » (impasse) ; **budget CAT-09 1 200 → 3 000 M$**, il ne payait pas son propre lanceur et violait l'invariant déclaré de sa table. **RÉSULTAT : 9 sur 11 réalisables.** Les 2 restants sont physiques, vérifiés en balayant 2/3/4 étages : CAT-10 plafonne à 152 t (assemblage orbital, non nommé par le GDD — à ne pas inventer), CAT-11 demande **346 495 t** en chimique — d'où [GDD 19.3] et l'antimatière. **Et [GDD 6.6] mord enfin** : **121 t sans blindage → 182 t avec 10 g/cm², plafond 130 t** — la seule décision de protection rend le vol non lançable. Voir §2, « LE CATALOGUE ÉTAIT INACHEVABLE À 55 % » |

| **L'ASSEMBLAGE EN ORBITE [GDD 5.2 br.1]** : la masse s'achète, elle ne se contourne pas | **FAIT (2026-07-29), MESURÉ.** Le GDD nommait « transfert de propergol orbital, rendez-vous automatisé robuste » et l'arbre portait les trois nœuds — **aucun ne débloquait rien** (7ᵉ cas de la série). `mission/Assemblage.hpp` en fait un **arbitrage**, jamais un contournement : N tirs = N fois le prix ET **R^N de fiabilité** (neuf tirs à 0,98 → 0,83), la campagne **dure** (cadence du pas de tir, 30 j / 20 j avec robotique), et **les ergols cryogéniques s'évaporent** pendant l'attente — 0,2 %/j, réel 0,1 à 1 %/j. La fraction survivante a une **forme close exacte** (série géométrique sur les temps de séjour, vérifiée terme à terme contre la somme brute). L'ébullition est un **point fixe qui peut diverger**, et le refus le distingue d'un « trop lourd » : *« RECHERCHER `transfert_ergols` »*. **Défaut trouvé en route** : « le moins cher qui soulève » devenait un mauvais conseil (CAT-05 : 2 tirs légers, −13 M$, −14 pts de fiabilité, VIABLE → RISQUE). Corrigé **par déduction** — `p_success ≤ p_segment`, donc une campagne sous l'exigence est *garantie* d'échouer. Résultat : c'est désormais **la fiabilité qui force le super-lourd** sur CAT-09, la raison réelle d'un SLS. **10 contrats sur 11 réalisables** (CAT-10 : 2 super-lourds, P = 0,937) ; seul CAT-11 reste hors de portée — 346 495 t, aucun assemblage ne rattrape une exponentielle [GDD 19.3]. **Et [GDD 6.6] devient gradué** : 121 t / 1 tir / P = 0,970 / 1 560 M€ → **182 t / 2 tirs / P = 0,931 / 3 016 M€**. Un arbitrage vaut mieux qu'un mur. Voir §2, « L'ASSEMBLAGE EN ORBITE » |

| **LA RENTRÉE EST UN VERROU [GDD 9.2, 7.6]** : 120 oracles, cinq champs de capsule, trois nœuds d'arbre — zéro appelant | **FAIT (2026-07-30).** Trouvé par le balayage du graphe d'inclusion (piège n°85), pas au jugé : `flight/Reentry.hpp` était le plus gros « modèle sans consommateur » restant, et `CapsulePart` traînait cinq champs qui n'existent QUE pour lui pendant que l'arbre VENDAIT la rentrée. **RIEN N'EST DÉCLARÉ** : la vitesse d'interface sort de l'énergie (`v = √(v∞² + 2µ/r)`, avec le `vinf_arr` que la fenêtre calculait déjà) et retrouve **11 074 m/s** contre les **11 030** publiés d'Apollo 11, **7 912** contre ~7 800 pour une LEO. **LA TENUE DU BOUCLIER EST DÉRIVÉE DE SA QUALIFICATION** — le pic de flux de l'entrée qu'il a réellement survécue, à la pente la plus raide que son g autorise ; seules des données publiées entrent. Conséquence structurelle verrouillée par oracle : une capsule qui refait son entrée de qualification passe **toujours**. **LE VERDICT REPRODUIT L'HISTOIRE** : Apollo et Orion rentrent de la Lune, Soyouz et Dragon non (*flux à 222 % du tenable*), aucune capsule volée ne rentre d'un retour interplanétaire rapide. **TROIS DÉFAUTS TROUVÉS EN BRANCHANT** : (1) le corridor était **fermé pour les cinq capsules** — la forme close d'Allen–Eggers est BALISTIQUE et annonce 35 g là où Apollo en a mesuré 6,5 ; la portance manquait, et la réponse fut d'utiliser `integrate_entry`, **le troisième modèle inutilisé du fichier** ; (2) mes deux bornes de bissection testaient la même chose alors que le ricochet n'a rien à faire dans la borne raide (prédicat non monotone) ; (3) le refus se chiffrait à une pente où l'engin ressort sans chauffer — « flux à **1 %** du tenable » sur une rentrée refusée. **CE QUE JE N'AI PAS INVENTÉ** : il n'existe pas de facteur de marge TPS publié (le margining réel est quadratique sur l'ÉPAISSEUR) — la marge est donc **affichée**, pas maquillée, et le corridor lunaire mesuré fait **0,22°**. **LIMITE DÉCLARÉE** : Sutton–Graves ne donne que le **convectif**, ~la moitié du flux réel à 11 km/s ; le verdict est un rapport donc il tient, mais le modèle est optimiste aux vitesses très élevées. **PREUVE** : `SPEditor` Succeeded, **4 007 oracles au vert**, drapeau `-sprentree[=<id>]`, `ue_rentree_verrou.png`. Voir §2, « LA RENTRÉE EST UN VERROU » |
| **L'ASSISTANCE A ENFIN UNE MISSION [GDD 5.11, 5.4, 10.1]** : quatre modules d'astro_core, une couche mission, et aucun chemin depuis le jeu | **FAIT (2026-07-31).** La passe précédente avait branché l'assistance dans une couche mission qui n'avait elle-même **aucun appelant hors des oracles** : le catalogue n'avait pas une seule mission qu'un tour puisse servir (la seule cible externe était CAT-10, un cargo NEP — la poussée continue, c'est-à-dire le régime où une assistance impulsive n'a rien à faire [GDD 6.3]). Le manque n'était pas un appelant, c'était **une MISSION** — même diagnostic que pour la descente propulsée en juillet. **CAT-13 « Orbiteur du système solaire externe »**, rang Senior, quatre prérequis, budget ancré sur **Juno (1 460 M$ publiés)**. Et la liaison a sorti **quatre défauts**, dont deux graves. (1) **LES RÉGLAGES DE FENÊTRE ÉTAIENT CEUX DE MARS**, et leurs propres commentaires le disaient (« horizon 800 j >= 1 période synodique Terre-Mars », durées 150-400 j) : appliqués à Jupiter, dont le Hohmann dure **997 j**, ils butaient sur leur plafond et rendaient un arc de 400 jours à **17 621 m/s** d'injection. Les bornes sont désormais **DÉRIVÉES** de la géométrie (Hohmann + synodique, demi-grands axes lus par vis-viva) — et **Mars ne bouge pas d'un bit**, ses bornes par défaut la décrivant déjà. (2) **L'ÉLAGAGE `vinf_min` VISAIT LA CIBLE FINALE À CHAQUE SURVOL** : exiger d'un survol de Vénus qu'il ouvre Jupiter demande 11 380 m/s là où la route vers la Terre en réclame 2 704 — le modèle **interdisait littéralement la trajectoire de Galileo**, et c'est ÇA (pas les 18 dimensions) qui rendait le VEEGA à 18 019 m/s. (3) `astro/LocalRefine.hpp`, écrit et sous oracle, **sans appelant** : branché dans le MBH à la place de la DE resserrée. (4) L'optimiseur minimisait le Δv **embarqué** alors que la mission paie aussi son **départ** depuis le parking. **AVEC LES QUATRE, LE MODÈLE RETROUVE LE VOL DE GALILEO** sans qu'on lui donne autre chose que la séquence : **C3 16,3 km²/s² contre 15,9 publiés, DSM 3 m/s, 5,86 ans contre 6,14**. **ET UNE « INSTABILITÉ » QUI N'EN ÉTAIT PAS UNE** : le même tour rendait 5 372 ou 10 976 selon la date du balayage ; j'ai dépensé trois budgets à combattre une non-convergence avant d'**imprimer la date de départ trouvée** (piège n°90) — les bons résultats partent tous à la même date absolue, et les échecs sont exactement ceux dont la fenêtre se termine avant elle. C'était **une opportunité de lancement**. **PREUVE** : `SPEditor` Succeeded, **4 111 oracles au vert**, drapeau `-sptour[=<id>]`, et le couple `ue_tour_direct.png` / `ue_tour_assistance.png` — 8 524 m/s en 893 j et 10,4 t contre **6 301 m/s en 4,7 ans et 5,4 t**, choisi au poste CONTRÔLE. **ET LA TRACE SUIT** (même journée, section suivante) : le vol se dessine dans le monde par ses vrais morceaux, sauvegarde **V7**, et au nœud de survol le vaisseau est à **0 km de la Terre** (`ue_tour_trace_monde.png`). Voir §2, « L'ASSISTANCE A ENFIN UNE MISSION » puis « LE TOUR SE DESSINE DANS LE MONDE » |
| **LA PERFORATION SE CALCULE [GDD 12.4, 6.5]** : le dernier mécanisme déclaré non modélisable | **FAIT (2026-07-30).** Le §2 portait « il faudra un modèle de flux sub-millimétrique (Grün) que rien dans le dépôt ne porte ». Vrai du dépôt, faux du monde — **troisième** exemplaire du piège n°86. Les quatre pièces sont publiées : flux cumulé de **Grün et al. (1985)**, densités par palier de **SSP-30425B**, limite balistique de **Cour-Palais** (k = 1,8 perforation / 2,2 / 3,0), et la géométrie de circuit de l'**ISS HRS** (panneau 8,79 m², **22 tubes** ⇒ 0,40 m²). **LDEF** tranche la population : les débris dominent sous 30 µm de pénétration, les météoroïdes au-dessus — une paroi de caloduc fait 500 à 2 000 µm, donc Grün seul EST le bon choix. **RECOUPEMENT NON FLATTEUR ET DÉCLARÉ** : SSP-30425B est ×8,45 / ×6,16 / ×1,63 au-dessus de Grün à 10 µm / 100 µm / 1 mm ; l'écart se resserre là où le modèle travaille mais va dans le mauvais sens. Gardé **sans correctif** (la croisière interplanétaire est le régime de Grün), et l'hypothèse **paroi simple** (pas de Whipple, ~2 000 en flux) domine largement : net **pessimiste** [GDD 12.5]. **LE RÉSULTAT CONTRE-INTUITIF** : la capacité résiduelle ne dépend PAS de la surface totale, seulement de celle d'UN circuit — le levier est l'épaisseur et la segmentation. **ET LE FORFAIT 1,15 EST DÉRIVÉ** (moyenne + 3σ de circuits morts) : **1,053** sur 1 000 m², **1,173** sur 10 m² — le forfait unique les encadrait, trop généreux pour une grande aile et trop chiche pour une petite (3σ/N en 1/√N). **CALIBRATION MESURÉE, PAS ESPÉRÉE** : sur l'aile dominante (38 647 m²) le passage au calcul déplace **2,9 %** ; sur 1,8 m² l'écart relatif est gros mais pèse **3,8 kg**. **CONSÉQUENCE** : dépassement d'endurance nommé — NEP 1 MWe à 900 j = 0,791 (cœur), à 8 365 j = **7,47e-117 (perforation)**. **QUATRE DÉFAUTS** : point fixe qui **oscillait** (résolu en forme close par un trinôme), paramètre déclaré qui a mordu (piège **n°91**), **zéro exact** rendu par cancellation d'`erf` (piège **n°92**), et ligne de HUD **tronquée** trouvée en regardant la capture. **PREUVE** : `SPEditor` Succeeded, **3 974 oracles au vert**, `ue_perforation_radiateurs.png`. Voir §2, « LA PERFORATION SE CALCULE » |
| **LE SUPPORT-VIE TOMBE TOUS LES 74 JOURS [Annexe E, GDD 12.3.1]** : « non touché faute de source » était faux | **FAIT (2026-07-29).** La ligne du §7 portait depuis deux passes que les avaries étaient BASSES mais qu'il n'y avait pas de source. Il y en a une, publiée, NASA : les quatre sous-systèmes ECLSS de l'ISS (OGS, CDRA, UPA, WPA) ont des MTBF **en vol** de 5 000 à 14 000 h ; en série, l'ECLSS de l'ISS a au plus **1 780 h = 74,2 jours**. Le modèle disait 8,0e-4/j, soit 1 250 jours — **17 fois trop optimiste**. Recoupement : le modèle prédit maintenant **12,1 pannes sur un aller-retour de 900 j**, ce que décrivent très exactement les **3,9-6,0 t de rechanges d'ECLSS** du même corpus. La normalisation tombe juste (le taux vaut pour R = 0,98, du matériel mûr — ce qu'est l'ISS). Total : une avarie tous les **66 jours** contre 400, dont **89 % de support-vie**, comme sur l'ISS. Les quatre autres taux **n'ont pas bougé** : pas de source pour eux, et les inventer serait le piège n°86 par l'autre bout. **LE TAUX RÉALISTE A RÉVEILLÉ DEUX CHOSES** : (1) la consommation s'intégrait **par frame** sur une linéarité que le code déclarait lui-même — vraie seulement tant que l'état d'avarie ne changeait pas dans une frame, ce qui n'arrivait qu'à cause du taux trop bas ; passée en sous-pas de 1/64 j sur grille absolue ; (2) l'oracle de sous-pas comparait **deux parties aux passés différents** (piège n°82 à nouveau) — mesuré : une avarie de chaque côté et **21,6 jours d'écart**. Il compare maintenant deux parties fraîches de même graine : écart **7,1e-14**. **ET LA MÉTHODE** : trois diagnostics faux (grille de tirage déjà absolue, verrou de rattrapage hors de cause) avant d'imprimer les deux nombres, qui ont tranché en une ligne — piège **n°90**. **PREUVE** : `SPEditor` Succeeded, **3 903 oracles au vert**. Voir §2, « LE SUPPORT-VIE TOMBE TOUS LES 74 JOURS » |

| **LE CISLUNAIRE EXISTE [GDD 3.3, 7.6, 19.7]** : le GDD le nommait quatre fois, le catalogue zéro | **FAIT (2026-07-29).** Le manque était du **CONTENU**, pas du code : `flight/Descent.hpp` — seul module du cœur qu'aucune suite n'exerçait, et dont l'en-tête **affirmait une validation que rien ne vérifiait** (piège **n°89**) — attendait une MISSION. `CAT-12 « Vol habite cislunaire et alunissage »`, rang Principal (que [3.3] *définit* par ce vol), trois à bord (Apollo), six prérequis = les cinq colonnes de la matrice [19.7]. **LE Δv N'EST PAS UN FORFAIT** : TLI **3 100 m/s** est la ligne de l'annexe Δv du GDD, LOI 900 et TEI 1 000 sont Apollo, et **l'alunissage sort de l'intégration** (poussée constante, gravity-turn, gravité centrale exacte) — donc il dépend du **moteur que le joueur a choisi**. **CONTRÔLE** : Δv de conception **8 512 m/s** contre le budget post-LEO d'Apollo **~8 900** — **4 % d'écart** sur un total dont la moitié est calculée. **L'ARBITRAGE EST RÉEL DANS LE BON RÉGIME** : un RL10 sur 5 t donne T/W 12,3, déjà au **plancher impulsionnel** (1 680 m/s) — plus de poussée n'achète rien, et c'est la physique ; l'AJ10-190 de la navette (26,7 kN) donne T/W 1,59 et **157 m/s de pertes de gravité par allumage** ; un SPT-100 de 83 mN est refusé en disant pourquoi. **DEUX DÉFAUTS TROUVÉS EN BRANCHANT** : (1) `descent_dv_required` rendait **ZÉRO** hors domaine — « atterrir est gratuit », le mensonge silencieux qu'un module jamais exécuté garde des années — réparé par un **théorème** (on ne se pose pas pour moins que sa vitesse orbitale : plancher `sqrt(μ/R)`, qui EST la bonne réponse à T/W élevé) ; (2) je mesurais le T/W contre la seule charge utile (**T/W 53**, sans sens physique), au lieu de la masse **allumée** de l'atterrisseur — d'où `Assessment::m0_dernier_etage_kg`, résolu par une passe amorcée sur la limite impulsionnelle. **PREUVE** : `SPEditor` Succeeded, **3 894 oracles au vert**. ⚠ **Catalogue : 11 réalisables sur 12** (nouveau dénominateur). Voir §2, « LE CISLUNAIRE EXISTE » |

| **RADIATEURS, RÉACTEURS, CONFINEMENT [GDD 12.4]** : un chapitre modélisé, zéro appelant | **FAIT (2026-07-29).** [GDD 12.4] tient en une phrase — « chaque sous-système avancé a sa propre fiabilité, souvent **dimensionnante** » — et `reliability/AdvancedFilieres.hpp` l'implémentait mécanisme par mécanisme **sans un seul appelant vivant** : choisir un NEP ne coûtait rien de plus qu'un chimique, sa centrale payée. Les trois sont au produit de `p_success`, dans un facteur SÉPARÉ et NOMMÉ (`p_filieres`, `cause_filieres`) — un chiffre sans cause n'est pas actionnable. **(1) LE CŒUR VIEILLIT** : une NEP pousse en CONTINU, deux ans de croisière consomment deux ans sur les sept de vie nominale (SP-100, borne basse documentée) ; un NTP ne brûle rien mais se fissure au cyclage. Mesuré : **17,3 % de risque à 730 j, 76,9 % sur un aller-retour**, contre 25,9 % pour le moteur — « souvent dimensionnante » est exact, **et la mesure dit quand** (sur deux ans un moteur spéculatif domine encore ; sur l'aller-retour, c'est le cœur). **(2) UNE AILE DE RADIATEUR EST UNE CIBLE** [GDD 7.8, 10.5] : mille mètres carrés changent la section de collision, dans le couloir que l'agence a **pollué elle-même**, pendant la campagne d'assemblage — **3,5 %** sur 180 j en LEO à 50 000 objets. Un tir unique n'a aucune exposition, et c'est vrai. **(3) LE CONFINEMENT** : le taux n'est pas inventé, c'est celui que le palier d'antimatière **DÉCLARE** déjà (`loss_rate_per_day`) — **8,0 % de perte sur 22,9 ans** au palier abouti, **97,4 % sur UN an** au palier du CERN. `collision_probability` était **lui aussi** sans consommateur (9ᵉ cas). **TROIS FAUTES À MOI, ATTRAPÉES PAR LA MESURE** (piège **n°88**, « actif pour une mauvaise raison ») : vieillissement piloté par le DÉLAI d'approvisionnement (−27 % avant décollage), densité de débris de test **7 ordres** trop haute, et « cause dominante » comparée au produit accumulé. **RESTE DÉCLARÉ** : la perforation sub-millimétrique, que `env::Debris` ne peut pas nourrir. **PREUVE** : `SPEditor` Succeeded, **3 864 oracles au vert**, drapeau `-spnep=qualifie`, `ue_filieres_avancees_12_4.png`. Voir §2, « RADIATEURS, RÉACTEURS, CONFINEMENT » |

| **LE VÉHICULE CONÇU EST CELUI QUI VOLE [GDD 4.1, 12.2]** : la boucle de 4.1 se referme | **FAIT (2026-07-29).** Dernier morceau : `assess_multistage` dimensionnait encore N étages IDENTIQUES pendant que l'atelier empilait des pièces hétérogènes dans son coin. Il accepte désormais la **pile conçue**. **ET LA LIAISON A RÉVÉLÉ QUE LES DEUX COUCHES NE MODÉLISAIENT PAS LE MÊME OBJET** (piège **n°87**) : l'atelier concevait une FUSÉE (RD-180 au sol, « masse au décollage »), la mission ACHÈTE son lanceur au catalogue et ne fait voler que la charge orbitale — désaccord total (Isp 338 contre 449,7 sur l'étage du bas), **invisible tant que l'atelier ne nourrissait rien**. C'est la mission qui a raison : on achète un Falcon 9, on construit la sonde. **CE QUI SE TRANSMET EST L'ARCHITECTURE, PAS LE Δv ABSOLU** — le Δv tombe de l'objectif et de la géométrie de la fenêtre, que la conception ignore ; l'architecte décide le **partage** entre étages [anti-feature 1.5]. Doubler les deux Δv ne change rien, changer leur partage change la masse. **NON-RÉGRESSION MESURÉE** : la pile de départ rend la même masse **au kg près**, le même coût, la même fiabilité, le même calendrier que le mode modèle — calibration inchangée (10/11, 128 t, P = 0,937, 2 973 M$). **ET LA CONCEPTION MORD** : les mêmes deux étages en Aestus (Isp 324) font passer la mission de **7,7 t à 11,2 t (+46 %)** ; une pile hétérogène se paie étage par étage ; un étage NEP fait porter sa centrale à la mission ; **un seul** étage non qualifié suffit à refuser, en nommant son nœud. **UN ORACLE DE NON-RÉGRESSION A ATTRAPÉ UNE VRAIE FAUTE** : la première rédaction comptait **trois allumages pour deux manœuvres** (n_burns au dernier étage EN PLUS d'un allumage à chacun des autres) — corrigé en répartissant. **ET LA DUPLICATION QUITTE L'ÉCRAN** : les sélecteurs « MOTEUR » et « ÉTAGES » du poste CONTRÔLE sont retirés — ils ne décidaient plus rien et auraient menti sur qui décide ; le poste MONTRE le véhicule, un étage par ligne, prix et confiance, rouge si non qualifié. **PREUVE** : `SPEditor` Succeeded, **3 847 oracles au vert**, `ue_vehicule_concu_vole.png`. Voir §2, « LE VÉHICULE CONÇU EST CELUI QUI VOLE » |

| **TOUTES LES PIÈCES SONT COMMANDABLES [GDD 12.1, 5.4, 12.3.2, 12.5]** | **FAIT (2026-07-29), SUR DÉCISION DE L'UTILISATEUR** — « n'invente pas, utilise des vraies pièces avec leurs vraies stats, je veux toutes les pièces sans exception ». J'avais différé en invoquant 54 chiffres que [GDD 20] diffère ; **l'objection était mal posée**, elle supposait qu'il fallait les inventer. Piège **n°86**. La couche gestion tenait TROIS moteurs dont un sans lignée (« MTX-1 neuf », que [GDD 12.1] interdit), et réécrivait la physique des deux autres — les tables **avaient déjà divergé** (Aestus 29 400 / 29 600 N). **LES DIX-HUIT PIÈCES SONT COMMANDABLES.** Le point dur est réel : la plupart des prix unitaires de moteurs-fusées **ne sont pas publiés**. La réponse est le schéma que le GDD impose déjà pour la fiabilité — **triplet {bas, nominal, haut} + confiance + source** [12.3.2, 12.3.4] : `{1, 2, 5}` en C avec « NON PUBLIÉ par SpaceX » dit ce qu'on sait, un nombre nu ment. **5 pièces sur 18 ont une source publiée, 13 sont des estimations déclarées, et le modèle l'imprime.** Ancrages cherchés : RS-25 **146 M$** (3,5 Md$ / 24), F-1 **21 M$** (76 moteurs pour 158,4 M$ en 1964), RD-180 **9,9-70 M$**, RL10 **17-20 M$**, NEXT-C via le contrat NASA GRC (18,41 M$ pour 2 propulseurs + 2 PPU). **LE CONSERVATISME [12.5] JOUE EN MIROIR DE LA FIABILITÉ** : confiance basse ⇒ borne PESSIMISTE, qui est le HAUT d'un prix — le NEP est facturé 600 M$. **TROIS FAMILLES DE CHIFFRES ONT DISPARU AU LIEU D'ÊTRE INVENTÉES** : la courbe d'essais se DÉRIVE du statut de qualification (et **reproduit à la valeur près** les deux triplets écrits à la main qu'elle remplace), le délai se DÉRIVE du TRL, et le développement est **nul** parce que l'arbre le paie déjà — le porter aussi sur la pièce le facturerait deux fois. **ET LA BRANCHE 6 NE S'OUVRE PLUS SEULE** [GDD 5.4] : 7 moteurs sur 18 gardés par leur nœud, refus qui NOMME la direction. **DEUX CONVERGENCES QUE JE M'ÉTAIS INTERDITES NE COÛTENT RIEN** : fractions sèches issues du vrai couple d'ergols et base de fiabilité semée depuis le catalogue — calibration **inchangée** (10/11 contrats réalisables ; masses 131 → 128 t, coût 2 969 → 2 973 M$). **PREUVE** : `SPEditor` Succeeded, **3 833 oracles au vert**, `ue_catalogue_moteurs_reels.png`. Voir §2, « TOUTES LES PIÈCES, SANS EXCEPTION » |

| **UNE FILIÈRE ALIMENTÉE TRAÎNE SA CENTRALE [GDD 5.12.1, 6.2, 6.5]** : la branche 6 était gratuite | **FAIT (2026-07-29), TROUVÉ EN RE-BALAYANT AUTREMENT.** Le §7 déclarait la famille « modèle sans consommateur » épuisée après huit cas ; un balayage par **accessibilité du graphe d'inclusion** (et non par recherche d'appelants) en a sorti **six en-têtes d'un coup**, ~750 lignes — plus un septième cas *à l'intérieur* d'un fichier inclus. Piège **n°85**. Traité ici : la moitié ÉNERGIE de `Propulsion.hpp` (`energy_source`, `source_mass_kg`, `PoweredPropulsion`, `radiator_mass_kg`) et tout `env/Thermal.hpp` — **aucun appelant vivant**, dans un fichier dont l'en-tête dit « une erreur de design fréquente consiste à confondre produire de l'énergie et produire de la poussée ». **CE QUE ÇA COÛTAIT** : un étage `NEP-1MW` coûtait ses **900 kg** de tuyère et rendait **Isp 5 000 s** — toute la branche 6 était une amélioration STRICTE, sans arbitrage, ce que [GDD 6.2] interdit en une ligne. **RIEN N'EST UN RÉGLAGE** : la puissance se déduit de la pièce (F = 2ηP/ve retourné), la centrale de la puissance, la chaleur du rendement, le radiateur de Stefan-Boltzmann. **ET LA CHAÎNE SE RECOUPE SUR DU PUBLIÉ** : NSTAR 2 011 W déduits contre 2 300 publiés (13 %), NEXT-C 6 927/7 400 (6 %), SPT-100 1 302/1 350 (4 %), et `NEP-1MW` **1 021 526 W — soit le nom de la pièce à 2 % près** ; la centrale mégawatt sort à **20,8 kg/kWe** dans la bande publiée 20-45, le panneau d'une sonde ionique à **40 kg** contre ~50 pour le SCARLET de DS1. **UNE FOURCHETTE N'ÉTAIT PAS UNE INCERTITUDE MAIS UNE ÉCHELLE** : appliquer les 5 W/kg d'un Kilopower de 10 kWe à un réacteur de 1 MWe est une erreur de catégorie qui refusait la NEP **pour une mauvaise raison** (piège n°77) — la puissance spécifique s'interpole en log entre les deux points que la table nommait déjà. **AU PASSAGE** : `mission::engines()` réécrivait la physique de moteurs déjà au catalogue, et les deux tables **avaient déjà divergé** (Aestus 29 400 N / 29 600 N) — une seule source de vérité désormais, l'oracle vérifiant qu'il n'y a plus qu'un seul endroit où la valeur puisse être fausse. **PREUVE** : `SPEditor` Succeeded, **3 700 oracles au vert** sur les 12 suites, nouveau drapeau `-spnep`, et `ue_centrale_nep_conception.png` — « CENTRALE + RADIATEURS 21 281 kg (19 % du décollage) ». Voir §2, « UNE FILIÈRE ALIMENTÉE TRAÎNE SA CENTRALE » |

| **ARES IMPOSE L'ENVELOPPE, L'ARCHITECTE DÉDUIT LE VÉHICULE [GDD 3.1]** | **FAIT (2026-07-29), sur précision de l'utilisateur** — « c'est le joueur qui crée la mission de A à Z, ARES dit juste : on doit aller là pour faire ça », que [GDD 3.1] confirme mot pour mot (« décide COMMENT concevoir, dans des enveloppes imposées ; il ne fixe pas le budget »). `payload_kg = 20 000` pour Mars habité n'était pas un objectif mais **une masse d'habitat** — une architecture imposée par le client, superposée à des consommables et un blindage déjà calculés. La coque pressurisée est désormais **déduite** de deux décisions d'architecte (équipage, volume par personne) et d'un fait mesuré : **137 kg/m³**, la densité des modules pressurisés de l'ISS — Destiny, Columbus et Kibo donnent la même valeur **à 1 % près**. Contrôle : 6 × 25 m³ × 137 = 20 550 kg, soit l'ancien forfait à **2,7 % près** — il était juste, il était simplement **posé au lieu d'être déduit** (piège n°81 : vérifier QUI décide d'une grandeur avant de vérifier COMBIEN elle vaut). **Et la décision mord** : 25 → 15 m³/personne fait passer la croisière martienne de **131 t à 100 t** au décollage, et joue deux fois puisque serrer l'habitat réduit aussi la surface à blinder. Un vol near-Earth n'emporte **pas** d'habitat — il s'amarre à une station, et le critère est un fait déjà calculé (aller-retour daté), pas une liste de familles. CAT-10 devient **VIABLE**. Voir §2, « ARES DIT OÙ ALLER » |
| **LE VÉHICULE CONÇU A UNE FORME [GDD 12.2, 17.2, 17.4]** : trois lignes du GDD, aucune pièce dimensionnée | **FAIT (2026-08-01).** « L'éditeur en coupe fournit LA GÉOMÉTRIE DU VÉHICULE, réutilisée directement au rendu » [12.2] + « un véhicule assemblé par le joueur doit être RENDU, pas modélisé » [17.2] + « du plan système au PLAN VAISSEAU (mètres) par simple zoom » [17.4] — et le vaisseau du joueur était un **point émissif de taille écran constante**, à dix mètres comme à dix UA. **AUCUNE COTE N'EST POSÉE** : la sortie d'ajutage d'un moteur allumable au sol sort d'une **identité exacte de la poussée** (A = F(1 − Isp_sol/Isp_vide)/p0, zéro paramètre), le volume d'un réservoir de la **densité du couple déjà au catalogue**, le diamètre d'une capsule de la **section de rentrée** qu'exige déjà `flight/Reentry.hpp`. **RECOUPEMENTS** : RS-25 **2,34 m × 3,96** (2,30 × 4,24 publiés), F-1 **3,63 × 6,15** (3,72 × 5,79), RL10C-1 **1,46 × 2,30** (1,45 × 2,22), et les quatre capsules à moins de 1 %. **LA SEULE APPROXIMATION EST MESURÉE** [GDD 6.8] : là où les deux routes s'appliquent, l'oracle publie l'écart — **±29 % sur le diamètre** — et elle ne coûte rien parce que cette géométrie **ne nourrit aucune physique** (oracle : « la coupe ne déplace aucune masse »). **LE VAISSEAU QUI VOLE EST FIGÉ AU FEU VERT (V8)**, avec ses ergols étage par étage : sans ce gel, retoucher l'atelier **déformerait un vol déjà parti** — même doctrine que `tof_days` et `tour_arcs`. **UNE COUPE, DEUX CONSOMMATEURS** : le monde 3D (cylindres et cônes à l'échelle réelle, LOD à deux crans comme Novellus) et le **dessin en coupe** qui manquait au poste CONCEPTION. **ET LA VÉRIFICATION A COÛTÉ TROIS CAPTURES POUR RIEN** : à 60 pixels, une cloche et une pointe ne se distinguent pas — il a fallu colorier les rôles puis **dessiner l'ajutage à 6 m** pour trancher (piège **n°96**), et `-spdist=0.4` ne faisait rien du tout, `FParse::Value` sur un flottant dépendant de la **locale** (piège **n°97**). **PREUVE** : `SPEditor` Succeeded, **4 175 oracles au vert**, sauvegarde **V8**, drapeau `-spvaisseau[=<mètres>]`, `ue_vaisseau_concu_monde.png` et `ue_coupe_conception.png`. Voir §2, « LE VÉHICULE CONÇU A UNE FORME » |
| **LA PASSATION [GDD 3.4, 3.5, décisions 6 et 7]** : le personnage vieillissait, pouvait mourir, et rien ne se passait | **FAIT (2026-08-02).** L'âge biologique avançait en temps PROPRE depuis le 2026-07-29, `natural_death_due()` savait dire qu'une vie s'achève, et `career::Succession` était écrite et sous oracle : **aucun des trois n'avait de lecteur**. Un Architecte de 120 ans gardait son poste ; un Architecte MORT d'un cancer radio-induit aussi — alors que le modèle qualifie déjà cette mort de naturelle, donc « ouvrant une passation ». La portée multi-générationnelle qu'exige [3.5] pour finir la branche 6 n'existait pas. **LA FIN DE VIE SE CONSTATE LÀ OÙ L'ÂGE AVANCE** (`AresLayer::avancer`), une seule fois, et **ce n'est pas une fin de partie** : l'ordre même de la chaîne de modales porte l'invariant — `GameOver` d'abord, `Passation` ensuite, « aucune passation n'annule un décès opérationnel ». **CE QUI PASSE, ET CE QUI MEURT AVEC LA PERSONNE** : rang **conservé** [décision 6], état programmatique et carnet **intégralement** transmis, contre confiance **remise à 70** [décision 7], score à zéro, **dose remise à zéro** (c'est un CORPS neuf — ce qui ROUVRE les vols terminaux que le prédécesseur ne pouvait plus faire [6.6, 9.2]) et écart d'horloge annulé. La ligne « intégralement » se tient en **n'écrivant rien** : `passer_la_main` ne touche que le personnel, et l'oracle vérifie trésorerie et arbre au bit près. **SI LE DÉFUNT ÉTAIT EN VOL**, la mission survit à son responsable scientifique ; ce qui cesse est l'ABSENCE, donc la protection financière de [9.3] se lève. **ET DEUX MESURES ONT CORRIGÉ L'ORACLE, PAS LE MODÈLE** (piège **n°98**) : la couche ARES ne rattrape le calendrier qu'au tick suivant, et surtout **on ne peut pas laisser couler 53 ans** — une agence inactive fait faillite en **six**, ce qui est la troisième issue de [3.4] et non un défaut. **PREUVE** : `SPEditor` Succeeded, **4 208 oracles au vert** (`test_session` 764 → **797**), sauvegarde **V9**, drapeau `-sppassation`, `ue_passation.png` et `ue_architecte_age.png`. Voir §2, « LA PASSATION » |
| **LE SCORE AVAIT UN CRITÈRE SUR TROIS [GDD 3.3]** : la carrière ne jugeait ni le budget ni la crise | **FAIT (2026-08-02).** Le GDD écrit « score cumulé à PONDÉRATION ÉGALE de trois critères » et les nomme : réussite, **respect budgétaire**, **gestion de crise**. Le code comptait `+40 par réussite, −10 par échec` sur les COMPTEURS de l'agence — si bien que dépenser deux fois son enveloppe ne coûtait rien, et que perdre un équipage par impréparation valait le malus d'un satellite raté. **Un compteur ne pouvait pas faire mieux** : il ignore ce qu'une mission a coûté et ce qu'elle a traversé. Le score se juge donc **au débrief**, seul endroit où les trois faits coexistent, chaque critère rendant une note dans [−1, +1] — un oracle vérifie qu'un point de budget déplace le total **autant** qu'un point de crise, ce qui est la définition de « pondération égale ». **LE DEMI-PALIER DE [10.3] EXISTE ENFIN** : `brilliant_recovery`, seul modificateur ADOUCISSANT du barème, était sauvegardé, relu, et **posé par personne** ; il se pose sur un fait — toutes les pannes survenues en vol ont été menées à réparation —, ce qui demande des technos qualifiées et du temps de vol, donc un ACTE. **LA CALIBRATION NE BOUGE PAS** : une mission nominale vaut toujours **40 points** (les seuils gardent leur sens), mais la même réussie **à +60 % de budget** ne rapporte plus que **13,3**, et une mission perdue coûte **−13,3**. [Annexe E] diffère le barème, [3.3] fixe la structure — seule la seconde est implémentée. **ET UN ÉCHEC RÉEL S'EST AFFICHÉ EN VERT** (piège **n°99**) : mon relevé comptait `ECHEC`, `test_ares_modules` écrit `[FAIL]` ; c'est le TOTAL (188 → 187) qui a trahi l'oracle disparu — lequel testait précisément la règle retirée. **PREUVE** : `SPEditor` Succeeded, **4 221 oracles au vert** (`test_session` **809**, `test_ares_modules` **189**), sauvegarde **V10**, `ue_score_promotion.png`. Voir §2, « LE SCORE AVAIT UN CRITÈRE SUR TROIS » |

Oracles hors moteur — **compteurs RELEVÉS en exécutant les 12 suites le
2026-07-29, après la calibration de fin de jeu.** Ne pas les recopier : les remesurer.
⚠ Le relevé ci-dessous **date d'avant cette passe** et sa ligne TOTAL était déjà
fausse : la somme de ses propres lignes donne 3 577, pas 3 430. Relevé du
2026-07-29 après calibration, suite par suite : `test_astro_core` **1 626**,
`test_contenu_gdd` **613**, `test_session` **684**, `test_carte_flotte` 135,
`test_reentry_perturb` 108, `test_api_sol` 59, `test_ares_modules` **128**,
`test_gdd_manques` 57, `test_mission_loop` 95, `test_economie_v12` 46,
`test_code_qualif` 25, `test_toolchain` 24 — **TOTAL 3 600, tous au vert**.
Puis **3 615** après le verrou de l'aller-retour, **3 635** après la destination
relativiste, **3 643** après les trois murs du vol habité lointain, **3 649**
après le recalibrage sur l'ancre habitée, **3 661** après le modèle de risque
chronique (`test_ares_modules` **175**, `test_session` **698**), **3 700**
après la centrale des filières alimentées, **3 833** après la fusion des
catalogues de moteurs, **3 847** après la liaison de la pile conçue, et **3 864**
après les sous-systèmes avancés de [GDD 12.4], et **3 876** après le premier
oracle de la descente propulsée, **3 894** après la mission cislunaire, et
**3 903** après le recalibrage des avaries sur l'ISS, **3 974** après le modèle
de perforation des radiateurs, **4 007** après le branchement de la rentrée, et
**4 033** après l'assistance gravitationnelle, et **4 038** après l'unification des
réglages de fenêtre.
Relevé COMPLET du 2026-07-31 (après l'assistance branchée sur sa mission, la trace
multi-jambes ET le corridor de survol), les 12 suites relancées d'affilée :
`test_astro_core` **1 626**, `test_contenu_gdd` **843**, `test_session` **758**,
`test_carte_flotte` 135, `test_reentry_perturb` **219**, `test_api_sol` 59,
`test_ares_modules` **188**, `test_gdd_manques` **102**, `test_mission_loop` 95,
`test_economie_v12` 46, `test_code_qualif` 25, `test_toolchain` 24 —
**TOTAL 4 120, tous au vert**.
*(Relevé précédent, 2026-08-01 : 4 038. Les +82 se répartissent en +37 sur la
rentrée/assistance, +41 sur la session (dont +9 pour la trace du tour, +6 pour le
corridor de survol et +3 pour le plancher d'altitude), et +4 que `test_contenu_gdd`
ajoute tout seul en balayant un contrat de plus.)*
Relevé COMPLET du **2026-08-01, après la géométrie du véhicule** (12 suites
relancées d'affilée, objets recompilés de zéro) : `test_astro_core` **1 626**,
`test_contenu_gdd` **892**, `test_session` **764**, `test_reentry_perturb` 219,
`test_ares_modules` 188, `test_carte_flotte` 135, `test_gdd_manques` 102,
`test_mission_loop` 95, `test_api_sol` 59, `test_economie_v12` 46,
`test_code_qualif` 25, `test_toolchain` 24 — **TOTAL 4 175, tous au vert**.
*(+55 sur 4 120 : +49 dans `test_contenu_gdd` — les recoupements de cotes, la borne
d'erreur de la route de classe, la coupe comme conséquence du dimensionnement — et
+6 dans `test_session` pour le vaisseau figé au feu vert et sa survie en V8.)*
Puis **4 208** le **2026-08-02, après la passation** (`test_session` **797**) : la
fin de vie, ce qui se transmet et ce qui meurt avec la personne, la survie en V9,
et l'invariant qui interdit une passation après un décès opérationnel. Puis
**4 221** après les trois critères du score (`test_session` **809**,
`test_ares_modules` 188 → **189**).
⚠ **LIRE LA LIGNE DE VERDICT DE CHAQUE SUITE, TELLE QU'ELLE L'ÉCRIT** : elles ne
disent pas toutes « ECHEC ». `test_ares_modules` écrit `[FAIL]`, `test_api_sol`
conclut par `0 ECHEC` au singulier, les neuf autres par `N en echec`. Un relevé
qui ne cherche qu'un seul de ces mots rend du vert sur du rouge (piège n°99) —
et c'est le TOTAL qui l'a trahi, pas le compteur d'échecs.
⚠ **`test_toolchain` EST INSTABLE PAR NATURE** : un de ses oracles vérifie que le
bac à sable tue un processus fils **volontairement planté** (0xC0000005), et il a
échoué une fois sur quatre exécutions consécutives, puis passé 24/24 trois fois de
suite. Un échec isolé de CET oracle-là n'est pas une régression — le relancer
seul avant de chercher une cause.
Le relevé précédent était faux de plus du double, et pas d'un peu — il donnait
300 à `test_astro_core` là où la suite en imprime 1 626. Un compteur recopié de
mémoire ne vaut rien ; seule la sortie du binaire compte.

| Suite | Oracles |
| :--- | ---: |
| `Space Program/tests/test_astro_core.cpp` | **1 626** (+6 pour la durée de transit de la fenêtre) |
| `tests/test_contenu_gdd.cpp` | 612 |
| `tests/test_session.cpp` | **672** (222 avant, **+42** chronologie, **+29** trace, **+65** navigation et manœuvre, **+9** graphe, **+43** logiciel de vol du mode Pro, **+28** le prix de l'inaction, **+12** délai de communication, **+7** boucle sol, **+13** rythme de mesure, **+22** carnet et bascule, **+46** mission vécue, **+21** dose et débris, **+27** anomalies et réparations, **+25** les deux horloges et la préparation médicale, **+12** le stock d'antimatière, **+13** les termes de contrat et le bilan de viabilité, **+11** l'audit exhaustif du catalogue et le verrou des lanceurs, **+4** l'assemblage orbital vu du catalogue, **+21** ARES impose l'enveloppe, l'architecte deduit le vehicule) |
| `tests/test_carte_flotte.cpp` | 135 |
| `tests/test_reentry_perturb.cpp` | 108 |
| `tests/test_api_sol.cpp` | 59 |
| `tests/test_ares_modules.cpp` | **118** (+15 : les deux horloges, confrontées aux valeurs publiées GPS / ISS / GEO ; **+19** : production, fuite et verdict de l'antimatière ; **+26** : l'assemblage orbital, forme close de l'ébullition et divergence du point fixe) |
| `tests/test_gdd_manques.cpp` | 57 |
| `tests/test_mission_loop.cpp` | **95** (+7 : le logiciel de vol hors domaine ; **+14** : ce que pèse un équipage ; **+17** : le verrou des radiations ; **+3** : la cible de calibration SPE) |
| `tests/test_economie_v12.cpp` | 46 |
| `tests/test_code_qualif.cpp` | **25** (+3 : la dilution du domaine) |
| `tests/test_toolchain.cpp` | **24** (toolchain embarquee, bac a sable, job object, horizon) |
| **TOTAL** | **3 430**, tous au vert |

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

### LE CARNET ET LA BASCULE (2026-07-28) — deux manques du GDD, un seul chantier

**`career::Notebook` était sérialisé, transmis au successeur en passation… et
VIDE.** Personne n'y écrivait, personne ne le lisait. La famille est connue :
`Mission::phase` (piège n°20b), `show_moons` (n°41), `ModeAide`, `comms_delay_s`
— *un conteneur fidèlement persisté n'est pas une fonctionnalité*.

**ET LA BASCULE NORMAL → PRO N'EXISTAIT PAS.** `GameState.hpp` la déclarait
pourtant en commentaire depuis toujours (« Bascule Normal → Pro unidirectionnelle
et irréversible [GDD 2.3] ») ; en fait `agence.mode` n'était écrit qu'à la
CRÉATION d'une partie et au chargement. Les deux manques n'en font qu'un, parce
que [GDD 2.3] les relie : « les graphes existants sont **archivés en lecture seule
dans le carnet** ».

**`career/Carnet.hpp`** (C++ pur, sous oracle) — quatre pages, toutes DÉRIVÉES
d'un événement du modèle. *Le carnet ne s'écrit pas à la main : un carnet qu'il
faudrait remplir soi-même serait vide chez tout le monde, et « transmis en
passation » ne vaudrait rien.*

| Page | Quand | Contenu |
| :--- | :--- | :--- |
| **Man pages de l'API** | à la bascule | une LECTURE de `noeuds_disponibles()` : chaque primitive avec la fonction qu'elle EST, plus son typage. Pas une copie — le jour où un nœud change, la page suit |
| **Archive de graphe** | à la bascule [GDD 2.3] | les nœuds assemblés, en texte |
| **Débrief de mission** | à chaque débrief | qui a conduit les corrections, manque au but, Δv dépensé/provisionné, arc de poursuite, logiciel embarqué et son domaine |
| **Reconstitution d'absence** | au retour [GDD 9.3] | ce qu'ARES a fait sans l'Architecte |

**« LECTURE SEULE » SE TIENT SANS DRAPEAU.** Une entrée de carnet est du TEXTE, et
il n'existe aucun chemin qui reconstruise un graphe exécutable depuis une chaîne
de caractères. L'archive est consultable et rien d'autre **par construction, pas
par interdiction** — c'est plus solide qu'un booléen `readonly` que quelqu'un
finirait par contourner.

**LA BASCULE COÛTE, ET C'EST ÉCRIT DANS LE GDD** : « cette perte est
intentionnelle ». `Session::basculer_en_pro()` archive, VIDE le graphe, bascule, et
refuse la seconde fois — sous oracle. Le poste VIGIE annonce le prix AVANT le clic
(« passer en PRO archive vos N nœud(s) et les efface », piège n°64) et porte le
carnet, les pages les plus récentes d'abord, tronquées : le cadre d'un poste
CLIPPE (piège n°42) et une carrière en écrit des dizaines.

**PREUVE** : build `SPEditor` Succeeded, **3 302 oracles au vert** (+22), dont la
man page qui NOMME les 8 primitives et leur appel d'API, l'archive où chaque nœud
se relit, l'unidirectionnalité, et la survie du carnet à la sauvegarde **au
caractère près** — c'est LE bien transmis en passation [GDD 3.5], il ne peut pas
s'évaporer.

### LE CIEL N'ÉTAIT PAS EN MIROIR — IL ÉTAIT À L'ANTIPODE (2026-07-28)

La dette de §6 point 10 disait « la voûte étoilée est vue EN MIROIR et n'est pas
calée sur le repère équatorial J2000 ». Les deux moitiés étaient à revoir.

**LE DÉFAUT ÉTAIT PLUS FORT QUE « MIROIR ».** L'échelle du dôme valait
`-SKY_RADIUS_UU / RayonMesh` — un rapport négatif sur les TROIS axes, c'est-à-dire
une **INVERSION PAR LE CENTRE**. L'œil étant à l'origine, regarder dans la
direction *v* atteignait le point de maillage situé en *−v* : **tout le ciel était
vu à son antipode**, bulbe galactique compris.

**ET VOICI POURQUOI PERSONNE NE L'AVAIT VU** : la Voie lactée est un GRAND CERCLE,
et un grand cercle est **invariant par inversion centrale**. La bande tombait donc
au bon endroit à l'écran ; seule sa garniture était retournée. *Un défaut qui
préserve la silhouette de ce qu'il casse ne se voit pas — il se démontre.*

**LA CAUSE ÉTAIT PÉRIMÉE.** L'échelle négative existait pour « voir l'intérieur
même avec un matériau à une seule face ». Or `M_SP_Starfield` est **deux faces
depuis sa création** (`Tools/make_sky.py` : `two_sided = True`). La négation ne
servait plus à rien depuis ce jour-là. Passée en positif : chaque direction
retrouve son texel. Vérifié par capture (`echelle=X=Y=Z=+100000`,
`rendu_recemment=1`, scène intacte).
Bénéfice de sûreté : le repli du moteur (`EmissiveTexturedMaterial`) étant à une
seule face, s'il sert un jour la voûte DISPARAÎT — un défaut visible, donc
diagnosticable, là où l'antipode était silencieux.

**LE REPÈRE DE LA CARTE EST GALACTIQUE, ET C'EST MESURÉ.** On ne peut pas caler
un ciel sans savoir ce qu'il représente. Diagnostic temporaire (retiré après
lecture) : centroïde des 1 % de pixels les plus brillants de
`8k_stars_milky_way.jpg` (8192x4096) → **U = 0,5133, V = 0,4962**.
**C'est le V qui tranche** : en galactique la bande suit exactement b = 0, donc
V = 0,5 ; en équatorial c'est un grand cercle incliné de 62,9° qui balaie V de
0,16 à 0,84, et son centroïde serait tiré vers le bulbe, à V = 0,661. Le U, lui,
ne prouve rien (la bande est symétrique en longitude dans les deux repères) —
*mesurer deux grandeurs et ne retenir que celle qui discrimine*.

**LA CONVENTION UV N'ÉTAIT PAS UN OBSTACLE — ELLE ÉTAIT DÉJÀ MESURÉE.** J'avais
écrit qu'elle « se mesure, elle ne se devine pas » et laissé le point ouvert :
`Tools/diag_body_uv.py` l'avait mesurée sur l'asset livré **le 2026-07-27**, et son
en-tête porte le résultat (`U = 0,5 + lon/360`, `V = (90 − lat)/180`, −X = lon 0,
+Z = pôle nord). *Chercher avant de déclarer un blocage — l'outil existait, avec sa
réponse dans son en-tête.*

**RESTAIT UN SEUL BIT : LE SENS DE LA LONGITUDE GALACTIQUE.** Se tromper mirore le
ciel en longitude, soit exactement la faute qu'on venait de corriger — donc pas
question de le deviner. On le mesure sur les **Nuages de Magellan**, les deux
objets compacts les plus nets hors du plan, à coordonnées connues.

**TROIS MESURES, DEUX INSTRUMENTS, AUCUNE RÉPONSE :**
1. brillance absolue hors plan (V > 0,60) → maxima à U ≈ 0,50 : le **halo du
   bulbe**, pas les Nuages ;
2. fenêtre resserrée (b de −29° à −54°) → encore U ≈ 0,50, luminances de 2 à 4 sur
   255. *Deux mesures qui donnent la même mauvaise réponse accusent l'INSTRUMENT,
   pas la fenêtre* ;
3. **contraste local** (case moins couronne) — le bon détecteur pour une tache
   compacte sur fond lisse → trois candidats à contraste 1,58 / 1,65 / 1,67 sur
   255, **indiscernables entre eux**, et aucun aux positions attendues.

**ET CE RÉSULTAT VAUT MIEUX QUE LA MESURE MANQUÉE.** Les Nuages de Magellan ne
sont pas détectables dans cette image. Joint au centroïde suspectement symétrique
(U = 0,5133, V = 0,4962), cela dit que `8k_stars_milky_way.jpg` est un panorama
**STYLISÉ**, pas une carte photométrique du ciel. **Le caler sur J2000 serait une
fausse précision** — habiller un décor en instrument, ce que [GDD 12.5, 19.6]
refuse en toutes lettres. On s'arrête donc, et on le déclare.

**LE VRAI REMÈDE EST AILLEURS**, et le §6 point 10 le disait déjà : des étoiles en
POINTS depuis un **catalogue Hipparcos**. Nettes à tout zoom (taille donnée à
l'écran), vraies constellations, et un catalogue **EST daté en J2000** —
l'alignement devient exact par construction au lieu d'être ajusté sur une image.
La voûte resterait ce qu'elle est : une nébulosité de fond.

Le diagnostic est **archivé sous `#if 0`** plutôt que supprimé : il porte la seule
instrumentation qui sache répondre à « ce champ d'étoiles est-il une vraie carte
du ciel ? ». Le supprimer obligerait à réécrire les deux instruments ratés.

### LA MISSION VÉCUE (2026-07-28) — le chapitre 9 avait un MODÈLE et AUCUNE PORTE

**Décision 18 du GDD : « vol habité vécu INCLUS ». Elle était inatteignable.**
`mission/Crew.hpp` était complet, juste et sous oracle depuis toujours — et rien
n'y menait :

| Ce qui existait | Ce qui l'appelait |
| :--- | :--- |
| `CrewMissionSlot::try_embark` [GDD 9.2] | **personne** — et le champ n'était même pas sérialisé |
| `VitalState` / `vital_budget` [GDD 9.4] | **un test unitaire**, jamais le jeu |
| `AgencyFinance::suspended` [GDD 9.3] | **les tests eux-mêmes**, qui posaient le drapeau à la main |
| `career::journal_absence` [GDD 9.3, 15.4] | **personne** |
| `Contract::payload_kg` d'un vol habité | un **forfait par famille** : deux ans à bord pesaient le poids de deux semaines |

C'est la famille déjà nommée ici quatre fois (`Mission::phase` n°20b,
`show_moons` n°41, `ModeAide`, le carnet) : *un conteneur fidèlement persisté
n'est pas une fonctionnalité*. La nouveauté est l'échelle — un CHAPITRE entier.

**1. LE POIDS DE LA VIE — les vivres entrent dans Tsiolkovsky [GDD 6.1, 9.4].**
« Une mission mal calculée AVANT LANCEMENT se traduit en dérives coûteuses, voire
en échec si les réserves ne suffisent pas » : la phrase était inapplicable tant
que les consommables ne pesaient rien. `MissionPlan::evaluate` ajoute désormais
leur masse à ce qu'il faut propulser — **sans toucher aux termes du contrat**,
qui restent ce que le client veut voir livré.
- **LA DURÉE N'EST PAS UN RÉGLAGE** : un vol habité vers une cible datée est un
  ALLER-RETOUR, et sa durée est la **période synodique** des deux corps. On la
  calcule sur les deux états héliocentriques du moment (vis-viva → demi-grand axe
  → Kepler → `astro::synodic_period`). **779,9 j sortent sans être écrits nulle
  part** — le chiffre que `launch_window` cite déjà comme récurrence.
- **DIMENSIONNER SUR L'ALLER SEUL AURAIT ÉTÉ PIRE QUE DE NE RIEN FAIRE** : un
  chiffre calculé, donc crédible, mais faux d'un facteur ~2,4 — et l'équipage
  meurt au retour. [GDD 12.5, 19.6] refusent exactement cela.
- **LA BRANCHE 4 ACHÈTE ENFIN QUELQUE CHOSE** [GDD 5.10, 19.1] : les boucles se
  DÉDUISENT de l'arbre (`recyclage_partiel` → ISS, `recyclage_ferme` → quasi
  fermé), et les deux valeurs traînaient depuis toujours en commentaire dans
  `Crew.hpp` sans être atteignables.
- **MESURÉ** (`test_mission_loop`) : 6 personnes, 780 jours →
  **33,7 t** de vivres sans recyclage, **14,6 t** avec les boucles ISS, **11,9 t**
  en quasi fermé. Au décollage : **127,4 t → 205,4 t** en passant d'un séjour de
  30 j à l'aller-retour réel — 14,6 t de vivres en coûtent **78** *(c'est
  Tsiolkovsky, pas une pénalité)*. Le recyclage quasi fermé rend **16,1 t**.
- La nourriture ne se recycle jamais : c'est le plancher, identique dans les
  trois cas, et un oracle l'exige — un modèle qui la ferait tomber serait faux.

**2. LA PORTE [GDD 9.2] — et ses conditions sont toutes DÉRIVÉES.**
`Session::peut_embarquer()` / `embarquer()` / `debarquer()`. Aucun seuil libre :
- mission **habitée**, **une seule à la fois**, et la **confiance** au seuil qui
  filtre déjà l'acceptation d'un contrat habité [GDD 13.4] — un barème, pas deux ;
- **mission LONGUE ⇒ rang TERMINAL + maturité** : « le personnage ne quitte ARES
  que lorsqu'il n'a plus de carrière à construire » désigne UN état, et
  `promotion_ready` l'exprimait déjà en dur — il devient `terminal_rank()`, lu par
  les deux. La frontière du mot « longue » est l'**incrément ISS (180 j)**, seul
  étalon réel d'un tour de service ordinaire. La maturité est `sejour_long`,
  littéralement le support-vie long séjour de la branche 4.
- **ON MONTE À BORD AVANT LE FEU VERT**, jamais après — voir piège n°71.

**3. CE QUE DEVIENT ARES PENDANT L'ABSENCE [GDD 9.3].** `finance.suspended` est
enfin levé par le jeu, et la confiance est **REPOSÉE** à sa valeur de départ à
chaque tick plutôt que gardée écriture par écriture : l'adjoint conduit de vraies
missions, donc de vraies anomalies passent par `apply_anomaly` ; les intercepter
une à une demanderait de n'en oublier aucune, et il suffirait d'en ajouter une
demain pour trouer la promesse. *Restaurer un état la tient par construction.*
Au retour, `journal_absence` trouve enfin son appelant.

**4. LES RÉSERVES ÉPUISÉES TUENT [GDD 9.4, 10.3].** Et la gravité n'est pas
décrétée : on déclare une perte CRITIQUE assortie du modificateur d'exposition
humaine, et c'est le barème (« niveau final augmenté d'un palier si une présence
humaine est exposée ») qui en fait une catastrophe. La **fin de partie**, elle,
tient à `lived.active` — `consequences_for` refuse expressément de trancher
(« SEUL le décès du PERSONNAGE, pas d'un équipage PNJ ») parce que seul l'appelant
sait qui volait. Deux faits distincts, deux mécanismes distincts.

**5. SÉRIALISATION V2.** `SCHEMA_VERSION` passe à 2 et `Reader::version()` trouve
son premier usage : le bloc `lived` se lit sous `if (r.version() >= 2)`, donc une
sauvegarde V1 se recharge **sans rien perdre** — elle décrit une partie où
personne n'était embarqué, ce qu'elle était. Sans ce bloc, recharger remettrait le
joueur au sol au milieu de sa croisière et dégèlerait une chaîne que 9.3 promet
suspendue.

**6. LA SURFACE.** Poste CONTRÔLE : bloc « VIE À BORD » (autonomie restante,
O2/eau/vivres/épuration CO2, boucles réellement embarquées, et ce que devient
l'agence), bouton EMBARQUER/DÉBARQUER, et **le motif du refus en clair** — c'est
ce qui rend lisibles les conditions de 9.2 au lieu de les laisser deviner
(piège n°42).

**PREUVE** : build `SPEditor` Succeeded, **3 362 oracles au vert** (+60), et
`ue_vecu_poste_controle.png` (`-spvecu -sppost=3 -spcadence=4`) — 6 à bord,
**896,9 jours d'autonomie**, boucles eau 93 % / O2 85 %, « ARES : sous l'adjoint —
confiance gelée à 70 », phase de vol CROISIÈRE, arrivée dans 322 jours, cran MOIS
vert. Le journal du moteur confirme le chemin : `embarque=1 — autonomie 897 j,
agence gelee=1`, passé par `Session::embarquer()`, la porte du bouton.
*~~Réserve d'affichage, antérieure et sans rapport : le contrat fabriqué par
`-spvol` a des termes nuls, d'où le « BILAN DE VIABILITÉ » en rouge sur cette
capture comme sur `ue_chrono_poste_controle.png`.~~* — **CORRIGÉ le 2026-07-29,
et ce n'était PAS une simple réserve d'affichage** : c'était le premier des trois
défauts en chaîne de la section « L'AUDIT DES POSTES », et il rendait le bilan de
viabilité invérifiable par capture. Avoir écrit ici qu'un rouge était normal l'a
rendu invisible pendant des semaines — c'est le piège n°77.

**`-spvecu`** (nouveau drapeau, §1) réutilise toute la machinerie de `-spvol` au
lieu d'en poser une seconde, et **pose les conditions dans le MODÈLE plutôt que de
désactiver la porte** : rang terminal, `sejour_long` et recyclage qualifiés, puis
`embarquer()`. Sans lui, l'état demanderait une carrière entière et aucune capture
ne pourrait le photographier.

**TROIS PIÈGES PAYÉS.**
- **n°71 — ON NE RATTRAPE PAS UN VAISSEAU EN ROUTE.** La première version
  autorisait l'embarquement sur `state == Launched` : l'Architecte rejoignait un
  véhicule déjà parti vers Mars. Trouvé **en regardant la capture**, pas en
  relisant le code. Embarquer est une décision de PLANIFICATION [GDD 9.2, 4.1] ;
  corollaire, les vivres ne se consomment qu'une fois le vol parti (pendant la
  qualification l'équipage est à terre), et cette condition se **lit** sur l'état
  de la mission au lieu d'être un second drapeau à tenir à jour.
- **n°72 — `GameState::tick` EST DU CODE MORT, et j'y ai branché la vie à bord.**
  Aucun appelant dans tout le dépôt : ni le jeu, ni le pont, ni une suite
  d'oracles. Le tick vivant est `AresLayer::avancer` (`app/ares.hpp`), piloté par
  le calendrier de l'agence depuis que le temps coule. Tout compilait, les oracles
  de `Crew.hpp` passaient, et rien ne se consommait jamais. *Avant de brancher un
  système sur un tick, vérifier que ce tick est appelé* — la fonction porte
  désormais l'avertissement en tête. Les deux ne font d'ailleurs pas la même
  chose (`age_by_proper_time` ici, `age_bio_s +=` là-bas).
- **n°73 — L'ORACLE DEMANDAIT L'IMPOSSIBLE, ET LE MODÈLE AVAIT RAISON DEUX FOIS.**
  Un `printf` de diagnostic (un chiffre mesuré, pas une relecture) a montré
  `cadence_ok=0`, cadence bornée à `REEL`, calendrier immobile : (a) le **plafond
  de cadence** [GDD 14.3] mordait — la mission venait de décoller, donc phase
  critique ; (b) au temps réel, 0,5 s n'atteint pas le **sous-pas de 1/64 j** et
  ne convertit rien. Puis, l'oracle des sous-pas comparait 40 frames de 0,05 s à
  4 frames de 0,5 s : le **garde-fou des 0,25 s** (« un gel de shaders ne coûte
  pas des mois ») tronquait les secondes. Trois refus du modèle, trois fois
  justifiés. Le premier est devenu un oracle à part entière — *une mission vécue a
  besoin que le temps passe, et [GDD 14.3] dit exactement quand il le peut*.

**Constat annexe, non traité (hors périmètre de cette passe)** : `env::Debris`
n'est tické par aucun chemin vivant non plus — il ne l'était que par
`GameState::tick`. À vérifier avant de croire la pollution orbitale vivante.

**Correction GDD v1.1 → v1.2 au passage** : `career::Succession::inherit_career`
appliquait encore la règle v1.1 (rang ramené à Junior, confiance à 40). Les
décisions **6 et 7** du journal v1.2 disent l'inverse, et le tableau de 3.5 le
répète : le rang est **transmis** (« propriété du POSTE, non de la personne »), la
confiance repart à **70**. Corrigé. *(La fonction n'a pas encore d'appelant — la
passation reste un chantier à part.)*

### LE VERROU DES RADIATIONS (2026-07-28) — l'environnement cesse d'être un décor

**[GDD 7.7] déclare l'environnement « ACTEUR de mission ». Il était SPECTATEUR.**
Constat fait en cherchant les appelants, pas en relisant les modèles :

| Modèle | Ce qui le consommait |
| :--- | :--- |
| `env/Radiation.hpp` — GCR, SPE, Van Allen, `DoseAccumulator`, limite de carrière, seuils aigus, tout ancré sur l'Annexe B | **rien** |
| `env/Debris.hpp` — `add_breakup` | `apply_anomaly` ✔ (vif) |
| `env/Debris.hpp` — `tick` (la DÉCROISSANCE) | **`GameState::tick`, c'est-à-dire personne** (piège n°72) |
| `mission/Events.hpp` — `EventSampler` [GDD 9.5] | **rien** (reste à faire) |
| `env/SpaceWeather.hpp` — `SolarCycle` | *sans état, fonction pure de l'époque : rien à ticker, pas un défaut* |

Les débris **s'accumulaient sans jamais retomber** : la promesse de [GDD 7.8] —
« les couloirs LEO se nettoient, les couloirs hauts restent pollués » — n'avait
que sa moitié punitive.

**1. LE BLINDAGE EST UNE MASSE, et elle est DÉRIVÉE** [GDD 6.6 « l'arbitrage
masse / protection / mission »]. Un seul nombre sourcé — le **volume habitable
par personne** (~25 m³, limite basse NASA en longue durée) ; le reste est de la
géométrie : un module pressurisé est un cylindre, élancement L = 2·D déclaré,
celui d'un Destiny à 1 % près. D'où une surface, d'où une masse (1 g/cm² =
10 kg/m²). **164 m² pour six personnes** → 8,2 t à 5 g/cm², **32,8 t à 20**.

**2. ET LA MESURE DIT QUELQUE CHOSE QUE PERSONNE N'A ÉCRIT** :

| | nu | 5 g/cm² | 20 g/cm² |
| :--- | ---: | ---: | ---: |
| Dose aller-retour Mars (780 j, **minimum** solaire) | 0,94 Sv | 0,91 Sv | **0,83 Sv** |
| Masse au décollage | 205,4 t | — | **397,3 t** |

**Doubler la masse au décollage n'achète que 11 % de dose en moins.** C'est le
plancher de `gcr_transmission` (les secondaires de spallation), et c'est la vraie
physique : le GCR ne se blinde pas. La leçon — *la réponse n'est pas la masse,
c'est un transit plus court ou un départ au maximum solaire* — **sort du modèle,
elle n'a été écrite nulle part**. Et 0,94 Sv nu contre une limite de carrière de
1,0 Sv : un seul aller-retour martien consomme presque une carrière, comme dans
la réalité (Annexe B : 0,3-0,7 Sv).

**3. LA DOSE S'ACCUMULE, ET AU BON ENDROIT.** Sur le chemin VIF
(`AresLayer::avancer`, jamais `GameState::tick` — piège n°72 déjà payé), avec le
facteur de géométrie du ciel LU sur la phase de vol (0 au sol, 0,4 en LEO sous la
magnétosphère, 0,5 sur un sol planétaire, 1 en croisière — les chiffres déclarés
de `Radiation.hpp`) et la modulation GCR **anti-corrélée** à l'activité solaire.
Sous oracle : 30 j de croisière coûtent plus que 30 j en LEO, et la dose baisse au
maximum solaire.

**4. LA DOSE APPARTIENT AU PERSONNAGE, PAS À LA MISSION.** Corrigé en cours de
route : je l'avais d'abord posée sur `LivedMission`, qui se vide au débarquement —
« un personnage consommé ne revole pas » [GDD 6.6] n'aurait eu aucun sens avec un
compteur remis à zéro à chaque retour. Elle vit sur `GameState::dose_architecte`,
et `DoseAccumulator` portait DÉJÀ la distinction `mission_sv` / `career_sv` : il
suffisait de la ranger au bon endroit. Le verrou ferme `peut_embarquer`, et il est
sérialisé — le perdre au rechargement rendrait apte quelqu'un qui ne l'est plus.

**5. DEUX FAÇONS DE NE PAS RENTRER, et une seule est une faute.** Les réserves
épuisées portent `player_error_causal` (provisionner était son métier) ; la dose
aiguë létale ne le porte PAS — elle vient d'une éruption, et [GDD 7.7] fait de
l'environnement un acteur, pas un piège à cocher. Le motif affiché dit laquelle
des deux morts c'est.

**6. LES DÉBRIS RETOMBENT.** Une ligne sur le chemin vif, et [GDD 7.8] devient
vrai. **PREUVE** : deux nuages identiques, 2 ans de temps de jeu → **300 km :
698 → 0 objets ; 1200 km : 698 → 698**. La traînée fait son travail en bas, et
l'empreinte reste durable en haut [GDD 10.5].

**7. SURFACE.** Poste CONTRÔLE : curseur de **BLINDAGE ÉQUIPAGE** (visible pour
les seuls vols habités — un curseur inutile enseigne une fausse leçon) avec sa
masse et **la dose qu'il promet** à côté, sans quoi il ne serait pas décidable ;
et deux lignes de dose dans « VIE À BORD », mission et carrière, juste sous les
vivres — c'est le même équipage et ce sont ses deux horloges.

**PREUVE** : build `SPEditor` Succeeded, **3 400 oracles au vert** (+38), et
`ue_vecu_poste_controle.png` recapturée — arrivée dans **204 jours**, autonomie
**778,4 j**, **DOSE MISSION 0,093 Sv** à 10 g/cm², carrière 9 %. L'écran concorde
au chiffre près avec le journal du modèle (784 j et 0,0886 Sv relevés à f=801).

**PIÈGE PAYÉ, et il ne concernait pas les radiations.**
- **n°74 — LES POSTES ÉTAIENT FIGÉS À LEUR OUVERTURE.** `SSPPoste::Tick` ne
  reconstruisait qu'au CHANGEMENT de poste, avec un commentaire qui l'assumait :
  « pas à chaque frame — les actions internes appellent Rebuild() elles-mêmes ».
  Règle juste **tant que rien ne bougeait sans clic** ; fausse depuis que le temps
  COULE [GDD 14.2]. La capture annonçait encore *897 jours d'autonomie* et
  *« arrivée dans 322 jours »* après quatre mois de vol — pendant que le modèle
  était à 784 jours. **Ce n'est pas ma télémétrie qui était morte : c'était tout
  l'écran**, et il aura fallu la première grandeur qui varie CONTINÛMENT pour que
  ça se voie. Corrigé : reconstruction quand le calendrier a bougé, **bornée à
  5 Hz** (une télémétrie n'a pas besoin de la fréquence d'image), et rien ne se
  reconstruit en pause — l'état par défaut d'une partie.
- **LA MÉTHODE, une fois de plus** : un `UE_LOG` de trois lignes dans le vrai
  chemin a tranché en une exécution entre « le modèle ne consomme rien » et
  « l'écran ne se rafraîchit pas » — deux hypothèses qu'une capture ne peut pas
  départager, puisque l'image est justement ce qui est en cause. Le diagnostic est
  gardé sous `#if 0`, comme celui du ciel.

### LES ANOMALIES SE PRODUISENT ENFIN (2026-07-28) — `mission/Avaries.hpp`

**Le dernier gros modèle sans appelant.** `mission/Events.hpp` tirait des
événements calibrés (processus de Poisson par type, substream dédié, taux modulés
par la phase, la fiabilité et le cycle solaire) depuis le premier jour, et
**personne ne les consommait** : la « bibliothèque d'anomalies » de [GDD 9.5]
existait sans qu'aucune anomalie ne se produise jamais.

**DEUX NATURES, et la distinction est physique.** `Avaries.hpp` est ce qui
manquait — ce qu'un événement FAIT, combien de temps, et ce qu'on peut y opposer :
- l'**ÉRUPTION SOLAIRE** est un INSTANT : une dose reçue, rien à réparer, et c'est
  le blindage embarqué qui décide seul ;
- les **PANNES** sont des ÉTATS : elles durent tant qu'on ne les répare pas, et
  leur effet se paie CHAQUE JOUR sur les consommables.

**AUCUN « MALUS » ABSTRAIT.** Une avarie dégrade des grandeurs qui existent déjà
et qui sont déjà vivantes : une panne de support-vie **dégrade les boucles de
recyclage** (donc `VitalState::consume` mange la marge), une panne électrique
**use l'épuration du CO2** (poste électrique) et l'ECLSS avec, une micrométéorite
**fait fuir** l'air et l'eau, une urgence médicale **fait surconsommer**, une perte
de communication **coupe la boucle sol** [GDD 9.6]. Réparer, c'est arrêter une
hémorragie de vivres — pas éteindre une icône.

**RÉPARER EST UNE CAPACITÉ, PAS UN DÉ** [GDD 9.1, 5.10]. « Rien n'est un coup de
dé nu » : on répare ce que l'architecture permet de réparer, et ça prend le temps
que ça prend. La branche 4 achète cette capacité (`maintenance_locale`,
`medecine_embarquee`), et deux nœuds RACCOURCISSENT le travail plutôt que de
l'autoriser — le diagnostic autonome trouve la panne, la redondance permet de
basculer pendant les travaux. **L'avarie continue de coûter pendant la
réparation** : c'est ce qui fait qu'on répare tôt.

**ET VOICI LA SYMÉTRIE QUE LE GCR SEUL CACHAIT** [GDD 6.6] : contre le fond
permanent, blinder ne sert presque à rien (11 % gagnés en doublant la masse au
décollage) ; contre une éruption, l'atténuation est **EXPONENTIELLE**. Mesuré :
une éruption majeure fait **5,00 Gy non blindée — létale — et 1,32 Gy derrière
20 g/cm²**. Le blindage ne rend pas l'éruption inoffensive (1,32 > 1,0 Gy de seuil
de syndrome aigu, et **c'est la mesure qui a corrigé ce que j'avais écrit ici**),
il fait rentrer l'équipage. C'est exactement la raison d'être des abris
anti-tempête réels : *la masse ne sert pas contre le fond, elle sauve la vie le
jour où le Soleil s'emporte.*

**TROIS DÉFAUTS DU MODÈLE TROUVÉS PAR LA MESURE.**

**A. Les paramètres SPE étaient incohérents avec l'Annexe B du GDD.**
`spe_unshielded_gy` disait en commentaire « les monstres sont rares » et faisait
l'inverse : une loi log-uniforme sur une magnitude UNIFORME donne **41 %
d'événements au-dessus du gray**. Couplée aux 1-10 SPE/an de `spe_rate_per_year`,
elle produisait **19,9 Gy d'aigu sur une seule croisière martienne** (mesuré) —
un équipage y mourait systématiquement, quelle que soit l'architecture, alors que
l'Annexe B ancre l'aller-retour à ~0,3-0,7 Sv. Recalibré avec **trois paramètres
déclarés** et une **CIBLE explicite vérifiée par oracle** : derrière la seule
coque, la dose SPE d'un aller-retour martien doit être du même ordre que la dose
GCR du même trajet. Résultat mesuré : **1,55 Sv de SPE contre 0,91 Sv de GCR**, et
**3,8 %** d'éruptions au-dessus du gray. *Si l'un des trois nombres bouge,
l'oracle le dit.*

**B. Un équipage n'est jamais nu dans le vide.** Le modèle partait d'un blindage
ZÉRO, ce qui décrit un astronaute sans véhicule. La structure, les équipements et
les consommables forment un blindage de fait : **7,5 g/cm² déclarés** (ordre de
grandeur du module de commande Apollo). La distinction compte pour la masse — la
coque est déjà dans la masse sèche, **seul l'ajout se facture**. La richesse en
hydrogène se moyenne au prorata : ajouter du polyéthylène améliore la QUALITÉ du
blindage, pas seulement sa quantité, et c'est pourquoi on choisit ce matériau.

**C. L'activité solaire était évaluée à « maintenant », pas à la date de la
fenêtre.** Trouvé par l'oracle de rejouabilité, qui a refusé de valider : évaluée
une fois par tick, elle appliquait à 400 fenêtres la valeur du DERNIER jour, si
bien qu'avancer d'un bloc ou par tranches ne donnait pas les mêmes éruptions. Le
cycle solaire a une période de onze ans — une année de fenêtres n'est pas un
instant. `WorldEpoch::at` existait déjà pour ça.

**LE TIRAGE EST REJOUABLE, et c'est la même exigence que les sous-pas du temps**
[GDD 14.2] : fenêtres d'UN JOUR indexées sur le calendrier, ancrées sur la **date
de décollage** et non sur la première frame (sans quoi un saut de 400 jours
n'ouvrait aucune fenêtre et un vol traversait deux ans sans la moindre panne —
défaut trouvé et corrigé). Sous oracle : une avance de 400 jours d'un bloc et huit
de cinquante donnent **les mêmes pannes, aux mêmes dates, et la même dose aiguë**.

**PREUVE** : build `SPEditor` Succeeded, **3 430 oracles au vert** (+30), et
`ue_vecu_avaries.png` — « AVARIES EN COURS (1) : défaut électrique (gravité 64 %),
boucles dégradées eau 84 % O2 84 % du nominal », dose 0,194 Sv derrière 17,5 g/cm²
(coque + conception), autonomie 490 j, arrivée dans 130 jours.

**UN PIÈGE PAYÉ, d'ergonomie.**
- **n°75 — LE CADRE CLIPPE, IL NE REPLIE PAS.** La conduite de mission a fini par
  porter plus de lignes qu'un poste n'en affiche : « ARES » chevauchait
  « [ECHAP] FERMER » et les boutons sortaient du cadre. Vu **en capture**, pas en
  relisant le code. Même remède que les postes AGENCE et PLANIFICATION, qui
  portaient déjà des listes longues : **la LECTURE défile, les ACTIONS restent
  ancrées en bas** — un bouton qu'il faut aller chercher en défilant est un bouton
  qu'on ne trouve pas. Et **les avaries sont remontées EN TÊTE du poste** : sous
  la ligne de flottaison, une alarme n'est pas une alarme ; le bilan de viabilité,
  lui, est une préoccupation de conception, pas de conduite.

### LES DEUX HORLOGES (2026-07-29) — `rel/Relativity.hpp` enfin branché

**LE MÊME DÉFAUT, POUR LA CINQUIÈME FOIS, ET C'ÉTAIT LE DERNIER GROS.**
`rel::DualClock` était déclaré sur `GameState`, **sauvegardé**, **rechargé** — et
`advance` n'avait **aucun appelant**. Le « vieillissement différentiel qui pèse
sur la carrière et la passation » [GDD 3.4, 14.4] valait donc rigoureusement
**zéro**, pour toute mission, y compris celles que le GDD range en régime
relativiste. Et l'âge biologique avançait du temps du **calendrier**, ce qui n'est
vrai que si l'on ne quitte jamais le sol.

**LA DILATATION CINÉMATIQUE N'EST QUE LA MOITIÉ DU TIERS DE L'HISTOIRE.** Une
horloge bat aussi selon le **potentiel** où elle se trouve, et dans le système
solaire les deux termes sont du même ordre (~1e-9). N'en garder qu'un ferait
mentir le modèle d'un **facteur 3**, pas d'un epsilon. Le taux retenu :

> `dτ/dt = (1 + Φ/c²) / γ(v)` — **exact en v** (tout β, ce que le régime
> antimatière exige), **premier ordre en Φ/c²**, borné et déclaré [GDD 6.8] :
> |Φ|/c² ≤ 1e-8 partout où un vaisseau peut aller, le terme négligé est en 1e-16.

**DEUX IDENTITÉS KÉPLÉRIENNES EXACTES REMPLACENT TOUTE INTÉGRATION.** Sur une
orbite, la moyenne **temporelle** de 1/r vaut **exactement** 1/a (l'intégrale en
anomalie excentrique se simplifie). D'où, sans hypothèse sur l'excentricité :
⟨v²⟩ = μ/a et ⟨Φ⟩ = −μ/a. Corollaire vérifié par oracle à 1e-12 : **|⟨Φ⟩|/c² vaut
exactement le DOUBLE du terme cinétique**, et de signe opposé.

**LE MODÈLE EST CONFRONTÉ À DES VALEURS PUBLIÉES, PAS À LUI-MÊME.** C'est le rare
cas où la réalité a déjà mesuré la réponse :

| | modèle | valeur publiée |
|---|---:|---:|
| GPS (r = 26 560 km) | **+38,574 µs/j** | +38,6 µs/j |
| ISS (400 km) | **−24,584 µs/j** | ≈ −25 µs/j |
| GEO | **+46,58 µs/j** | ≈ +46 µs/j |
| Vitesse orbitale RMS de la Terre | **29 784,7 m/s** | 29 784,8 m/s |
| Hohmann Terre→Mars | **258,9 j** | 259 j |

**ET LE RÉSULTAT DE FOND CONTREDIT LE CLICHÉ.** Sur un transfert vers Mars, le
vaisseau est **plus haut dans le potentiel solaire ET plus lent que la Terre** :
les deux termes vont dans le **même sens**, son horloge **gagne**, et le voyageur
revient **PLUS VIEUX** de **0,25 s** sur un aller-retour complet. Le signe est un
RÉSULTAT du calcul, pas une hypothèse ; `aging_gap()` documente désormais ses deux
signes, et `diverged()` prend la valeur absolue. En orbite basse le signe
s'inverse (la vitesse l'emporte), avec **une seule formule pour les deux régimes**.

Et c'est **précisément pourquoi [GDD 6.7.2] a raison** de dire l'effet
imperceptible sous β ≈ 0,7 : le GDD l'**affirmait**, le modèle le **chiffre** —
sous la seconde sur un aller-retour, contre **2,9 ans sur 10** à β = 0,7, par le
même code.

**CE QUI EST GELÉ AU DÉPART L'EST POUR UNE RAISON DE REJOUABILITÉ**, pas de
commodité : les demi-grands axes sont lus **une fois**, à l'embarquement, sur les
**mêmes éphémérides** que la fenêtre de lancement et le Δv (`geometrie_horloge`).
Un vol rechargé bat donc au même rythme. Même motif pour le facteur médical
ci-dessous. Sauvegarde **V3** ; une archive V2 retombe sur des défauts qui
reproduisent **exactement** son comportement d'origine (géométrie invalide ⇒
rapport 1), sans dérive silencieuse.

**DEUXIÈME DÉFAUT DE LA MÊME FAMILLE, RÉGLÉ AU PASSAGE.**
`EventContext::medical_risk_factor` était **écrit en dur à 1,0** dans le tick
alors que `station::effects` calculait 0,6 depuis toujours et qu'un oracle le
vérifiait : **le module médical de Novellus coûtait 110 M€ et ne changeait aucun
tirage** [GDD 11.6, 9.4]. Gelé à l'embarquement comme la fiabilité — c'est un
**entraînement reçu avant le décollage**, et le lire en vol laisserait l'adjoint
changer les taux de panne d'un vol en cours en démontant un module.

**TROISIÈME : `GameState::tick` EST SUPPRIMÉ.** Piège n°72 soldé. Chaque système
qu'il contenait a été **cherché par appelants, pas relu** : `research.tick`,
`debris.tick`, `deliver_unlocked_contracts` et le vieillissement vivent dans
`AresLayer::avancer` ; `treasury.tick` ne vit **nulle part, et c'est correct** —
la trésorerie en M$ est l'économie v1.1, neutralisée à l'initialisation, dont
l'autorité v1.2 (`AgencyFinance::tick_month`, `FinancialStage`) est vivante. Il
n'y a plus **qu'un seul endroit** où brancher un système, donc plus d'endroit où
se tromper.

**PREUVE** : build `SPEditor` Succeeded, **3 470 oracles au vert** (+40), et — le
bloc « VIE À BORD » étant sous la ligne de flottaison du défilement, **aucune
capture ne peut montrer ce chiffre** — une **mesure en jeu** :
`[DIAG horloge] t_terre=182,6 j → 212,9 j, ecart +32,26 → +40,77 ms,
a_croisiere=1,2816 UA`. Soit **0,10264 s/an mesuré** contre **0,10259 s/an**
prédit par 3μ☉/2c²·(1/a⊕ − 1/a_transfert) : **quatre chiffres significatifs**.
`ue_horloges_poste_controle.png` montre le poste vivant (avaries, 656,8 j
d'autonomie, 6 à bord) ; le diagnostic est réarchivé sous `#if 0`.

**UNE ERREUR DE MANIPULATION, PAS DU MODÈLE** : la première capture est revenue
sur le MENU. `ArmerCapture` sort si `-spscene` vaut 0 — j'avais omis le drapeau de
scène, que `-spvecu` n'implique pas. À retenir : **`-spvecu` exige `-spscene=iss`**.

### L'ANTIMATIÈRE EXISTE — ET ELLE DIT NON (2026-07-29)

**LE DERNIER MAILLON DE LA CHAÎNE RELATIVISTE SANS CONSOMMATEUR.**
`AntimatterProduction`, `antimatter_needed_g`, `beta_from_antimatter`,
`annihilation_energy_j` : **aucun appelant hors des suites d'oracles**. Quatre
paramètres décrivaient le processus qui, dit le GDD, « est le vrai levier
d'équilibrage » de la fin de jeu — et **pas un gramme n'existait nulle part**.

**LE DÉBIT N'EST PAS UN PARAMÈTRE LIBRE : C'EST DE LA PUISSANCE.**
`production_g_per_yr` et `energy_j_per_g` décrivaient le même processus sans
jamais se parler. On ne peut pas choisir les deux : `ṁ = P / E`. Le débit se
dérive donc de la **marge de puissance de Novellus** [GDD 11.5], ce qui fait de
l'infrastructure le levier annoncé au lieu d'un nombre écrit à la main.
Vérification qui rassure : les 5 000 kW que CAT-11 exige rendent **1,58e-3 g/an**,
l'ordre de grandeur exact du débit tabulé — les deux champs étaient cohérents,
seulement débranchés.

**UN STOCK QUI FUIT EST UN ÉQUILIBRE, PAS UN CUMUL.** `dS/dt = ṁ − λS`, intégrée
**exactement** (`S∞ + (S − S∞)·e^(−λdt)`), jamais par un pas d'Euler : un bloc de
3 650 jours et 365 tranches de 10 donnent le même stock à **1e-15 près**. Même
exigence que les sous-pas du temps [GDD 14.2], appliquée à une ressource.

**ET LE VERDICT MESURÉ, QUI EST CELUI DU GDD.**

| | |
|---|---:|
| Stock d'équilibre sous les 5 MW de CAT-11 | **4,32 mg** |
| Marge réelle de Novellus au départ | **38 kW** → 9,9e-6 g/an |
| Antimatière pour β = 10⁻⁴ (γ−1 = 5e-9, indétectable) | **750 g** |
| Capacité de confinement déclarée | **1 g** |

Ce n'est pas « long », c'est **structurellement impossible** : même à puissance
infinie, le confinement à 1 g interdit β = 10⁻⁴. **C'est le confinement le
verrou, pas le débit** — et l'équilibre est 230 fois sous la capacité, donc c'est
la **fuite** qui borne le stock, pas le réservoir. [GDD 19.3] le disait déjà
(« viser β = 0,3 demande des tonnes — hors échelle ») ; le modèle branché le
**chiffre**. C'est une donnée de calibration [Annexe E], **pas un défaut de code**,
et je ne l'ai pas recalibrée : le GDD diffère explicitement ces nombres, et son
invariant déclaré est précisément que le régime reste hors d'échelle.

**CE QUI CHANGE QUAND MÊME, ET CE QUI NE CHANGE PAS.** CAT-11 reste volable comme
aujourd'hui (Δv de 30 km/s) : **aucun verrou nouveau**, la physique se contente de
dire ce qu'elle achète. Le poste AGENCE affiche désormais les quatre nombres qui
décident — stock, débit sous la marge courante, **plafond réel avec sa cause**
(« borné par la fuite » ≠ « borné par le confinement »), et l'écart au seuil
relativiste. **Le cul-de-sac est affiché au lieu d'être subi** ; le bloc n'apparaît
que si la filière est qualifiée, pour ne pas faire de bruit pendant toute la
partie. Et `GeometrieHorloge::beta_croisiere` bascule l'horloge sur la
**cinématique pure** au-delà du seuil [GDD 6.7.2] — le vaisseau ne suit alors plus
d'ellipse, il s'échappe, et ⟨1/r⟩ = 1/a n'a plus de sens. Exercé par oracle à
β = 0,7 : le bord ne bat plus qu'à **71 %** du sol. Le jour où la calibration
changera, le code sera déjà juste.

**PREUVE** : build `SPEditor` Succeeded, **3 501 oracles au vert** (+31),
sauvegarde **V4**, et `ue_antimatiere_poste_agence.png` — le poste rend propre,
cadre intact, bloc correctement **masqué** filière non qualifiée. La mise en page
est sûre **par construction** et non par chance : `Col->AddSlot().FillHeight(1)`
donne au défilement la hauteur restante, donc des lignes ajoutées au-dessus le
rétrécissent sans jamais déborder (contrairement au piège n°75).

### LA CALIBRATION DE FIN DE JEU (2026-07-29) — LE GDD RÉPOND À SA PROPRE ANNEXE

**LA PASSE PRÉCÉDENTE S'EST TROMPÉE DEUX FOIS, ET LES DEUX ERREURS SE TENAIENT.**
Elle concluait : « le régime relativiste est hors d'atteinte par construction ;
c'est une donnée de calibration [Annexe E], **pas un défaut de code** ; seul le
GDD peut trancher entre trois issues ». Fausse sur le diagnostic, fausse sur la
méthode.

**1. LE GDD AVAIT DÉJÀ TRANCHÉ.** L'Annexe E ne diffère pas la question : elle en
**nomme la dépendance**, « vitesse maximale *souhaitée* en fin d'arbre ». Et le
corps du document la dit, à trois endroits :

| Énoncé | Ce qu'il impose |
| :--- | :--- |
| [6.7.2] « seule l'antimatière **franchit** β ≳ 0,3 » | 0,3 doit être ATTEIGNABLE au palier abouti |
| [5.12.11] la fusion donne de grandes vitesses « **sans encore** rendre la dilatation significative » | au palier fusion, β doit rester SOUS le seuil |
| [3.4] β ≈ 0,9 « se mérite par l'ingénierie » · [3.5] la fin de branche 6 « demande souvent **plusieurs vies** » | l'accumulation est une DURÉE, de l'ordre du siècle |

L'issue (a) — « CAT-11 est une asymptote, le vol relativiste n'a jamais été
destiné à partir » — est donc **réfutée par le document lui-même**. Il n'y avait
pas de question de game design à poser : il y avait trois énoncés à **inverser**.

**2. C'ÉTAIT BIEN UN DÉFAUT DE CODE — LE 8ᵉ « NOMMÉ MAIS NON CONNECTÉ ».**
La puissance de production était `G.station.power_margin_kw() * 1000.0` : la
**marge de Novellus**, 38 kW au départ, 5 MW au mieux. Conséquence exacte :
*aucune recherche de branche 6 ne pouvait déplacer le débit d'un facteur.* Le
« vrai levier d'équilibrage » annoncé par le GDD n'était pas branché sur son
levier — et le verdict « impossible » qu'on en tirait mesurait donc une station,
pas une filière. [GDD 5.12.12] dit pourtant où prendre la puissance : « le
rendement énergétique **couple la production à la branche énergie** et au
budget ». Une usine à antimatière n'est pas un module de station.

**CE QUI CESSE D'ÊTRE UN RÉGLAGE.** `energy_j_per_g` avait un **plancher
physique** que personne n'avait écrit : on ne fabrique pas un antiproton sans
fabriquer son proton, donc E ≥ 2·m·c² = **1,7975e14 J/g**. Le champ libre est
remplacé par un **rendement η ∈ (0,1]**, et la mesure a sorti l'hypothèse muette :
l'ancien défaut de **1e17 J/g supposait η = 1,8e-3**, six ordres au-dessus de ce
que le CERN sait faire (≈ 1e-9). Une hypothèse de fin d'arbre était posée au
milieu du modèle sans être déclarée [GDD 6.8, 12.5] — piège n°81, encore.

**LES PALIERS SONT DÉDUITS DES TROIS ÉNONCÉS.**

| Palier (branche 6) | η | Puissance usine | Confinement | Énoncé vérifié |
| :--- | ---: | ---: | ---: | :--- |
| `fission_spatiale` | 1e-9 (réel) | 1 GW | 1e-3 g | — |
| `fusion` | 1e-6 | 10 TW | 1e3 g | [5.12.11] pré-relativiste ✓ |
| `antimatiere` | **1e-2** | **2e16 W** | **1e10 g** | [6.7.2] et [3.4] ✓ |

Le confinement à **1 g** n'était pas « un plafond du stock utile » [5.12.12] mais
**un mur**. La fuite de 1e-5/j reste assez serrée pour que ce soit encore **la
fuite qui borne** (9,613e9 g d'équilibre contre 1e10 g de réservoir), ce que le
poste AGENCE dit en toutes lettres.

⚠ **CES TROIS LIGNES ONT ÉTÉ RECALIBRÉES UNE SECONDE FOIS** le même jour, sur une
décision de l'utilisateur qui change l'ancre — voir « LE RELATIVISME EST POUR
L'HABITÉ » plus bas. La première calibration visait une **sonde**, et c'était le
seul cas qui ne sert à rien.

**LE RÉSULTAT MESURÉ, ET IL EST EXACTEMENT CELUI QUE LE GDD DEMANDE.**

| | |
|---|---:|
| Débit au palier abouti | **3,511e4 g/an** |
| Stock d'équilibre | **9,613e6 g** |
| β = 0,3 sur une sonde de 5 t, aller simple | **3,83e6 g → 139 ans** |
| β = 0,9 sur la même sonde | **hors d'atteinte** |
| β = 0,9 sur 100 kg secs | **4,09e6 g → 152 ans** |
| Aller-retour à β = 0,3 [6.7.4] | 26× l'aller simple → **hors d'atteinte** |

**ET C'EST LA DERNIÈRE LIGNE QUI FAIT LE DESIGN.** « β découle de
l'**architecture** » [décision 10] cesse d'être une phrase : le même stock donne
0,48 à une sonde de 5 t et rien du tout à un vaisseau habité, et l'architecte qui
veut 0,9 doit descendre à quelques dizaines de kg. Ce qui reste refusé l'est par
la **physique** — le ratio à la puissance quatre de l'aller-retour [6.7.4] — et
non par un réservoir arbitrairement petit.

**UN ORACLE QUI PASSAIT POUR UNE RAISON FAUSSE (piège n°82).** « le stock
CONVERGE — 200 ans de plus n'ajoutent rien » était vert depuis toujours. Il ne
mesurait pas une convergence : au bout de quelques années sans aucun programme,
l'agence fait **faillite** et `Jeu::avancer_temps` s'arrête net (« faillite : le
calendrier s'arrête là »). **Le stock ne convergeait pas, le temps s'arrêtait.**
Tant que la calibration rendait le stock dérisoire, les deux se ressemblaient
assez pour que la différence ne se voie pas. Le fait de jeu qui le remplace vaut
mieux que l'oracle perdu : **les 140 ans de [GDD 3.5] exigent une agence qui
produit pendant 140 ans.** La pression d'inactivité [13.2] et le programme de fin
de jeu sont **couplés** — l'accumulation ne s'obtient pas en laissant filer le
temps. C'est sous oracle, dans les deux sens.

**CONSTANTE DE TEMPS.** 1/λ = **274 ans** : quatre siècles ne suffisent pas à
converger (mesuré : 8,54e6 g sur 9,61e6), cinq constantes y amènent à 1 %. C'est
elle qui dicte les horizons des oracles, pas un chiffre rond.

**PREUVE** : build `SPEditor` Succeeded ; **3 600 oracles au vert** sur les
12 suites ; nouveau drapeau **`-spantimatiere`** (§1) et
`ue_antimatiere_calibree.png` — le bloc affiche débit de l'usine *avec sa
puissance et son rendement*, plafond réel *avec sa cause*, et le seuil β = 0,3
**ATTEINT**. Au passage, un troisième verdict manquait à cette ligne : « 0 ans »
se lisait « c'est instantané » là où cela voulait dire « c'est fait ».

### LE VERROU DE L'ALLER-RETOUR NE MORDAIT PAS SUR L'HORLOGE (2026-07-29)

**TROUVÉ EN SONDANT UN VOL CAT-11 RÉEL**, une fois la calibration faite : quel β
l'antimatière achète-t-elle *vraiment* à un vol joué ? La sonde a répondu autre
chose que ce qu'on cherchait.

`antimatter_needed_g(m, β, **n_burns**)` connaissait le nombre de poussées.
`beta_from_antimatter(m, g)` — **son inverse, dix lignes plus bas dans le même
fichier** — ne le connaissait pas : il rendait toujours le β d'un **aller
simple**. Donc tout ce qui LISAIT un β le lisait **surestimé**, et d'autant plus
que l'architecture était contraignante. Or c'est exactement ce que [GDD 6.7.4]
appelle « le verrou de l'aller-retour » et qu'il chiffre au **ratio à la puissance
quatre** — un verrou que le modèle savait calculer (`mass_ratio_for_burns`,
`round_trip_mass_ratio`) et qui ne s'appliquait pas **là où il compte : sur
l'horloge de l'équipage**.

**LA CORRECTION EST UNE IDENTITÉ, PAS UN COEFFICIENT.** Le ratio total valant
R^n et la **rapidité étant additive** [GDD 6.7.3, Annexe A], la rapidité par
poussée est simplement `(1/n)·(ve/c)·ln(R_total)`. L'inversion redevient donc
**exacte pour tout n** — sous oracle : partir d'un β, en déduire la masse, la
relire, retrouver le β à 1e-12, pour n ∈ {1, 2, 4} et β ∈ {0,05 · 0,3 · 0,7}.

**ET LE NOMBRE DE POUSSÉES SE LIT, IL NE SE CHOISIT PAS** (`burns_for_architecture`) :
un équipage **revient** (4), une sonde qui se pose **freine** (2), un survol ne
freine pas (1). Mesure, à stock d'équilibre égal sur 5 t secs :

| Architecture | Poussées | β |
| :--- | ---: | ---: |
| Survol | 1 | **0,482** |
| Aller simple avec insertion | 2 | **0,257** |
| Aller-retour habité | 4 | **0,131** |

Les trois franchissent encore le seuil : la calibration tient, et le verrou du
GDD **gradue** au lieu d'interdire. L'ancre de calibration est reformulée en
conséquence — 5 t **en survol**, donc un **majorant** assumé, et non « un aller
simple » comme elle le disait.

**CE QUE LA SONDE A AUSSI MESURÉ** : sur un véhicule habité réel (coque 6
personnes + vivres + blindage ≈ 100 à 200 t secs), le même stock donne β ≈ 0,015.
Le régime relativiste est donc, en pratique, **robotique** — cohérent avec
[décision 10]. C'est ce constat qui a ouvert la question de la DESTINATION, voir
la section suivante.

**PREUVE** : `SPEditor` Succeeded, **3 615 oracles au vert** (+15).

### LA MISSION RELATIVISTE A UNE DESTINATION (2026-07-29)

**LE VOL DE FIN DE JEU N'ALLAIT NULLE PART.** `window_target_for_family` ne
nommait **aucune cible** à la famille « relativiste » : `transfer_tof_days`
rendait 0, la croisière restait ouverte (`dated == false`) et le vol **n'arrivait
jamais**. Toute la chaîne — production d'antimatière, β, deux horloges, temps
propre — existait pour une mission **sans destination**.

**LE GDD SE CONTREDISAIT, DONC ON A DEMANDÉ.** [9.3] « plusieurs décennies
terrestres » et [3.4] « β ≈ 0,9 → ~5 ans d'écart » (soit ~9 ans de vol à
γ = 2,294) désignent des **années-lumière** ; [17.3] borne la scène au **système
solaire 1:1** et [5.6] range la fin de branche 6 sous « système solaire externe ».
Choisir changeait le PÉRIMÈTRE de la scène : ce n'était pas une déduction mais un
arbitrage. **Décision de l'utilisateur : l'étoile la plus proche.**

**LA DISTANCE EST UN FAIT, PAS UN RÉGLAGE** : Proxima Centauri, parallaxe
Gaia DR3 768,0665 mas → 1,30197 pc → **4,2465 al**. C'est aussi le **minorant
absolu** de toute distance interstellaire — aucune architecture ne fera mieux.

**ET LA MESURE A TRANCHÉ DEUX CHOSES QUE PERSONNE N'AVAIT POSÉES.** L'oracle
visait « ~5 ans d'écart à β = 0,9 » [GDD 3.4] et **a échoué en rendant 2,66** —
puis 2 × 2,66 = **5,32**. Donc :

| | |
|---|---:|
| Proxima à β = 0,9, aller | **4,72 ans Terre · 2,06 à bord · écart 2,66** |
| ... **aller-retour** | **écart 5,32 ans** — [GDD 3.4] dit « ~5 » |

Le chiffre du GDD **confirme la destination** (à 2 al ou à 8, il ne tomberait pas)
**et** dit que le voyage dont il parle est un **aller-retour** — ce que [6.7.4]
affirme par ailleurs en comptant quatre poussées. Le document ne nommait pas sa
cible ; ses propres nombres la désignaient. *L'oracle qui échoue vaut mieux que
celui qu'on ajuste : c'est lui qui a trouvé les deux.*

**`rel::proper_time` A ENFIN SON CONSOMMATEUR** — le dernier modèle de la série.
τ n'est pas *divisé*, il est **intégré** [GDD 6.7.1], et `relativistic_transit`
est le seul point d'entrée : le jour où le profil aura une rampe d'accélération,
rien d'autre ne bougera. Vérifié à profil constant : τ intégré == t/γ à 1e-6.

**APPROXIMATION DÉCLARÉE, ET ELLE BORNE** [GDD 6.8] : trajet rectiligne à β
constant, poussées impulsionnelles — la même approximation que [GDD 6.3], mais
ici **optimiste**, car un cœur annihilant est en régime **continu** [GDD 6.4].
La durée rendue est donc un **MINORANT** du vol réel, et l'écart d'âge un
minorant de l'écart réel. Dit dans le code.

**β DEVIENT UNE PROPRIÉTÉ DU VOL.** Il vivait sur `Lived::horloge`, donc il
n'existait **que pour une mission vécue** : une sonde relativiste robotique n'en
avait pas, alors que c'est elle qui va le plus vite. Il est désormais sur
`Mission`, **figé au feu vert** comme `tof_days` et le tirage de navigation, et
**sérialisé (V5)** — un vol déjà parti ne change pas de vitesse parce que l'usine
a produit trois grammes de plus. Une seule expression le calcule
(`Session::beta_croisiere_de`), trois sites la lisent.

**L'ORDRE COMPTE, ET C'EST NOUVEAU** : pour cette famille, la durée de transit
n'est **pas** une géométrie de ciel — c'est la distance divisée par la vitesse que
l'architecture achète. `beta_croisiere` se fige donc **avant** `tof_days`.

**MESURÉ DE BOUT EN BOUT** : une sonde de 5 t sur le stock d'équilibre, deux
poussées → β = 0,257 → **Proxima en 16,6 ans** (6 045 j), chronologie **datée**,
gate d'arrivée capable de chiffrer l'attente. Et un véhicule plus lourd met
**plus** longtemps : la durée découle de l'architecture [décision 10], elle n'est
pas une propriété du contrat.

**PREUVE** : `SPEditor` Succeeded, **3 635 oracles au vert** (+20),
sauvegarde **V5** (une archive V4 se relit, β retombant à 0 — la valeur de toute
mission non relativiste).

#### CE QUE LA DESTINATION A RÉVÉLÉ — TROIS MURS, ET AUCUN N'EST LE MOTEUR

**LE VOL LE PLUS LONG DU JEU EMPORTAIT TRENTE JOURS DE VIVRES.**
`crew_round_trip_days` dérivait de `window_target_for_family`, qui ne nommait
aucune cible à cette famille : elle retombait donc sur
`crew_stay_days_for_family` — le **séjour court par défaut, 30 jours** — pour un
vol de seize ans. Trois conséquences d'un seul manque : les vivres étaient sous-
dimensionnés d'un facteur 200, la mission n'était **pas classée « longue »**
(donc le gate de [GDD 9.2] — rang terminal + `sejour_long` — ne s'appliquait pas
à la plus longue mission du jeu), et le véhicule ne portait **aucune coque
pressurisée** (`masse_habitat_kg_` est conditionnée à cette durée). Corrigé :
une étoile n'a pas de période synodique, on ne l'attend pas — la durée
d'occupation est **2× le transit**.

**ET LE BILAN DE MASSE EST UN POINT FIXE QUI DIVERGE.** Même structure que
l'ébullition des ergols (`Assemblage.hpp`), même raison de fond :

> m_sec → β(m_sec, stock) → durée = 2·d/(βc) → vivres(durée) → m_sec

Alourdir le vaisseau le **ralentit**, ce qui **allonge** le voyage, ce qui
demande **plus de vivres**. `mission::bilan_relativiste` le détecte en 2
itérations et **nomme la cause** au lieu du symptôme — sans lui, `assess`
produisait un véhicule de 1 742 t et refusait par « AUCUN LANCEUR NE SOULÈVE
CETTE MASSE », verdict exact et inutilisable (piège n°42). Contre-épreuve dans
l'oracle : la **même structure sans équipage** ne diverge pas — la boucle vient
bien des vivres, pas de la masse.

**MESURÉ, avec l'arbre entier et le stock d'équilibre :**

| Masse sèche | β (4 poussées) | Aller | Aller-retour | Vivres pour 6 |
| ---: | ---: | ---: | ---: | ---: |
| 20 t | 0,0271 | 156,6 ans | 313,1 ans | **1 742 t** |
| 50 t | 0,0119 | 355,9 ans | 711,9 ans | 3 961 t |
| 100 t | 0,0062 | 687,3 ans | 1 374,5 ans | 7 649 t |

**Les deux autres murs, indépendants du premier :**
- **DOSE** — 12,9 Sv derrière 20 g/cm² sur 33 ans, soit **13× la limite de
  carrière** [Annexe B]. Passer à 50 g/cm² ne la ramène qu'à 11,3 : le GCR ne se
  blinde pas, il se **fuit**, et on ne fuit pas un voyage de trois siècles.
- **DURÉE DE VIE** — départ à 32 ans, mort naturelle vers 85 [GDD 3.4] : 53 ans
  de vie contre 313 ans de vol.

**ET IL EST BRANCHÉ, PAS SEULEMENT ÉCRIT** — un modèle sans consommateur est un
défaut, et il n'était pas question d'en créer le 9ᵉ cas dans la passe qui déclare
la famille épuisée. `MissionPlan::evaluate` appelle `bilan_relativiste` **avant**
`assess_multistage` et le refus **court-circuite** `finalize`, exactement comme
`Assemblage.hpp` pour l'ébullition. Le stock est posé par le driver
(`mission_plan.antimatiere_g`), comme les boucles et les lanceurs qualifiés — le
plan pur ne connaît pas l'état de l'agence. Sous oracle **dans les deux sens** :
le plan habité est refusé avec la cause exacte, et le **même plan sans
antimatière** retombe sur l'évaluation ordinaire — le court-circuit ne mord que
là où la boucle existe.

**CONCLUSION DE CETTE PASSE : avec la calibration visant une SONDE, la mission
relativiste était robotique** — [GDD 19.1] et [19.7] au mot près. **Et c'est
précisément ce constat qui a été renversé une heure plus tard**, voir ci-dessous.

### LE RELATIVISME EST POUR L'HABITÉ (2026-07-29) — L'ANCRE ÉTAIT FAUSSE

**DÉCISION DE L'UTILISATEUR, ET ELLE EST STRUCTURANTE** : « en gros le relativisme
a un intérêt seulement pour les vols habités ». Elle tranche la question laissée
ouverte, et elle le fait en **rejetant le cadrage** plutôt qu'en choisissant une
option : une dilatation que **personne ne vit** n'a aucune conséquence de jeu.
[3.4] fait peser l'écart d'âge sur la carrière et la passation, [14.4] sur le
vieillissement — et une sonde ne vieillit pas. **Calibrer sur une sonde de 5 t
revenait donc à calibrer sur le seul cas qui ne sert à rien**, et c'est ce que
faisait `CALIB_DRY_MASS_KG`.

**LE SEUIL D'EXISTENCE EST MESURÉ, PAS CHOISI.** Dichotomie sur le stock jusqu'à
ce que le point fixe converge :

| | |
|---|---:|
| Seuil sous lequel le vol habité **n'existe pas** | **3,0e8 g** |
| Stock pour β = 0,3 habité (6 personnes, aller-retour) | **4,26e9 g** |
| Facteur manquant sur l'ancienne capacité (1e7 g) | **×426** |

**LA NOUVELLE ANCRE EST UN POINT FIXE, PAS UN CHIFFRE ROND.**
`CALIB_DRY_MASS_KG` = **183 t** — la masse que `bilan_relativiste` rend pour
l'architecture habitée (coque 20,6 t + blindage 32,8 t + 2 t de charge, plus les
vivres que la durée impose). Elle vit dans `astro_core`, qui ne peut pas appeler
`mission/` ; **un oracle interdit aux deux de diverger** en vérifiant que la
constante EST le point fixe, à 5 %.

**CE QUE LE VOL DEVIENT, MESURÉ :**

| | |
|---|---:|
| β de l'architecture habitée | **0,370** |
| Aller-retour Proxima | **22,9 ans Terre · 21,3 vécus** |
| Écart d'âge | **1,63 an** |
| L'architecte part à 32 ans et rentre à | **53 ans** |

Et **[GDD 3.4] tombe une seconde fois** : « β ≈ 0,25 → **~1 an d'écart sur une
décennie** (invisible) ». C'est exactement ce vol. Les deux points de données du
chapitre 3.4 sont désormais reproduits par le modèle — le « ~1 an » par le vol
habité, le « ~5 ans » par l'aller-retour à β = 0,9.

**ET LE VERROU DE [6.7.4] TIENT LÀ OÙ IL COMPTE.** β = 0,9 est désormais
atteignable — **pour une sonde dépouillée de 5 t** (2,05e8 g) — et reste hors
d'atteinte de **cinq ordres** en aller-retour habité (4,3e15 g contre 9,6e9).
C'est [GDD 6.7.2] (« seule une antimatière très aboutie **approche** 0,9 ») et
[décision 10] (« β découle de l'**architecture** ») dans la même mesure.

**QUATRE ORACLES ONT ÉCHOUÉ EN CHANGEANT L'ANCRE, ET C'EST LEUR MÉTIER.** Trois
comparaient encore à la sonde ; le quatrième — « un véhicule plus lourd met plus
longtemps » — s'était **silencieusement inversé** (son comparant de 50 t était
devenu plus léger que la référence de 183 t). Corrigé en **dérivant** le comparant
de l'ancre (4 × `CALIB_DRY_MASS_KG`) : le jour où elle bougera, l'oracle suivra au
lieu de mentir. Le HUD avait le même défaut — il calculait la cible à **une**
poussée, annonçant un seuil **26 fois trop bas** et donc « ATTEINT » bien avant
que le vol soit payé.

**RESTE UN MUR, ET IL EST DIFFÉRENT DES AUTRES** : la dose. Traité juste après —
voir « CHRONIQUE ET AIGU » ci-dessous.

**PREUVE** : `SPEditor` Succeeded, **3 649 oracles au vert**,
`ue_antimatiere_calibree.png` — « VOL HABITE b=0,3 (aller-retour) : 3,66e9 g
requis — ATTEINT », stock 4,254e9 g, plafond réel 9,613e9 g *borné par la fuite*.

### CHRONIQUE ET AIGU NE TUENT PAS DE LA MÊME FAÇON (2026-07-29)

**LA DOSE CHRONIQUE N'AVAIT AUCUNE CONSÉQUENCE DE SANTÉ.** Le modèle savait tuer
par dose **aiguë** (déterministe : `ACUTE_LETHAL_GY` = 4,5 Gy, la DL50) et ne
savait **rien faire** du cumul chronique : il se contentait de verrouiller les
vols suivants — c'est-à-dire **rien du tout sur un vol terminal** [GDD 9.2], qui
est justement le seul où de telles doses arrivent. Un aller-retour interstellaire
rapporte ~10 Sv ; le modèle les enregistrait et les oubliait. Ce n'était pas un
modèle sans consommateur mais un modèle **sans conséquence** — variante nouvelle,
et plus discrète, de la même famille.

**LA DIFFÉRENCE EST RÉELLE ET SE CHIFFRE.** Un effet chronique est
**stochastique** : il ne fixe pas un seuil de mort mais une **probabilité** —
le REID (*Risk of Exposure-Induced Death*), l'instrument dont les agences se
servent réellement. Deux constantes sourcées, aucune ajustée :
- coefficient ICRP pour des **travailleurs adultes** : **4,1 %/Sv** ;
- **DDREF** (*Dose and Dose Rate Effectiveness Factor*) = **2** : à faible débit,
  le même Sv fait deux fois moins de dégâts — l'ADN a le temps de se réparer.
  C'est exactement la distinction qui manquait, et c'est elle qui rend un
  aller-retour interstellaire pensable.

**ET LA LIMITE D'ANNEXE B CESSE D'ÊTRE UN NOMBRE NU.** 1 Sv vaut
`reid_from_chronic_sv(1,0)` = **2,05 %**, à comparer aux **3 % de REID** qui sont
la norme NASA. La constante tombe donc dans la bonne bande **pour une raison**, et
non parce qu'elle est ronde — vérification croisée qu'on n'avait jamais faite.

| Dose | Reçue d'un coup | Reçue sur 23 ans |
| ---: | :--- | :--- |
| **10 Sv** | très au-delà de la DL50 — **mort certaine** | **REID 20,5 %** — un pari |

**LA LIMITE PROTÈGE UNE CARRIÈRE, PAS LE DERNIER VOL** [GDD 9.2]. Elle est
l'instrument qui protège un astronaute **réutilisable** ; sur le vol terminal —
celui qu'on prend « lorsqu'il n'a plus de carrière à construire » — elle
protégeait une carrière qui n'existe plus, et interdisait donc **le seul vol pour
lequel tout le reste existe**. Elle reste opposable à toute mission ordinaire, et
cesse de l'être sur la mission longue, déjà gardée par le rang et la maturité.
Ce n'est pas une porte qu'on ouvre : c'est un **risque qu'on accepte**, et le
verdict le chiffre.

**ET LA MORT QUI EN DÉCOULE N'EST PAS LA MÊME** : un cancer radio-induit se
déclare **au retour**, pas en vol. C'est donc une **mort naturelle anticipée**
[GDD 3.4], **qui ouvre une passation** — et non une mort opérationnelle, qui n'en
ouvre aucune. La distinction est celle du GDD et elle change tout pour la partie.
Le tirage passe par la **graine de mission**, comme l'issue du vol et l'erreur
d'injection : un rechargement rejoue le même sort [GDD 18]. Et la part **aiguë**
est retirée du risque chronique avant conversion — elle a déjà son barème, la
compter deux fois surestimerait [GDD 12.5].

**PREUVE** : `SPEditor` Succeeded, **3 661 oracles au vert**
(`test_ares_modules` **175**, `test_session` **698**).

### UNE FILIÈRE ALIMENTÉE TRAÎNE SA CENTRALE (2026-07-29) — la branche 6 était gratuite

**LA FAMILLE « MODÈLE SANS CONSOMMATEUR » N'ÉTAIT PAS ÉPUISÉE.** Le §7 la
déclarait close après huit cas et demandait, pour la passe suivante, de
**re-balayer plutôt que de continuer une liste**. Le re-balayage a été fait
autrement : non pas « qui appelle ce modèle ? », mais **accessibilité du graphe
d'inclusion** — quels en-têtes du cœur n'ont AUCUN `#include` hors des tests.
Six sont sortis d'un coup, ~750 lignes de physique : `astro/Mga1Dsm.hpp`,
`flight/Descent.hpp`, `force/Drag.hpp`, `force/Srp.hpp`, `nav/Statistics.hpp`,
`reliability/AdvancedFilieres.hpp`. **La méthode par appelants les avait tous
manqués** parce qu'on ne cherche des appelants que dans les fichiers qu'on
regarde ; le graphe, lui, ne connaît pas les fichiers qu'on oublie.

> ⚠ **CORRECTION MESURÉE LE 2026-07-29** (voir « LA BRANCHE NUMÉRIQUE » plus bas).
> « ~750 lignes de physique morte » était une SURESTIMATION de ma part : quatre
> de ces six modules sont **exercés par un oracle** (`force/Drag` et `force/Srp`
> dans `test_reentry_perturb`, `nav/Statistics` et `astro/Mga1Dsm` dans
> `test_astro_core`). Ils sont écrits, VALIDÉS, et seulement inatteignables
> depuis le jeu — ce qui n'est pas la même chose que mort. Un seul l'était
> vraiment : `flight/Descent.hpp`, qu'aucune suite n'incluait.

**ET LE SEPTIÈME CAS NE SE VOYAIT MÊME PAS DANS CE BALAYAGE**, parce qu'il est
dans un fichier bel et bien inclus. `vehicle/Propulsion.hpp` est atteint par
`PartsCatalog.hpp` — mais seule sa MOITIÉ PROPULSEUR l'est. Sa moitié ÉNERGIE —
`PropTier`, `energy_source`, `source_mass_kg`, `rtg_power_after`,
`solar_power_at`, `PoweredPropulsion`, `radiator_mass_kg`, et tout
`env/Thermal.hpp` derrière — n'avait **aucun appelant vivant**. Le fichier dit en
tête « une erreur de design fréquente consiste à confondre produire de l'énergie
et produire de la poussée » [GDD 5.12.1] : il énonçait la faute et la commettait.

**CE QUE ÇA COÛTAIT, MESURÉ.** Un étage `NEP-1MW` posé à l'atelier coûtait ses
**900 kg** de tuyère et rendait **Isp 5 000 s** — onze fois le meilleur chimique,
pour rien. Toute la branche 6 était donc une **amélioration stricte, sans
arbitrage**, ce que [GDD 6.2] interdit en une ligne : « personne n'a haute
poussée ET haut rendement sans puissance colossale ».

**RIEN DE CE QUI A ÉTÉ AJOUTÉ N'EST UN RÉGLAGE.** La puissance se DÉDUIT de la
pièce (F = 2ηP/ve retourné), la masse de centrale se déduit de la puissance, la
chaleur perdue se déduit du rendement, la surface de radiateur se déduit de
Stefan-Boltzmann. Et la chaîne se **recoupe sur des chiffres publiés** — c'est
l'oracle, pas la formule :

| Pièce | Puissance déduite | Puissance d'entrée publiée | Écart |
| :--- | ---: | ---: | ---: |
| NSTAR (DS1, Dawn) | 2 011 W | 2 300 W | 13 % |
| NEXT-C (DART) | 6 927 W | 7 400 W | 6 % |
| SPT-100 (GEO) | 1 302 W | 1 350 W | 4 % |
| NEP-1MW | 1 021 526 W | **le nom de la pièce** | 2 % |

Les trois rendements de jet (Hall 0,50 · grilles 0,70 · NEP 0,60) sont donc
**tenus par la mesure**, pas posés. Deuxième recoupement, sur la centrale :
la NEP mégawatt sort à **20,8 kg/kWe**, dans la bande publiée **20-45 kg/kWe** ;
et le panneau d'une sonde ionique sort à **40 kg**, quand le SCARLET de Deep
Space 1 en pesait ~50.

**UNE FOURCHETTE N'ÉTAIT PAS UNE INCERTITUDE, C'ÉTAIT UNE ÉCHELLE.** `5-100 W/kg`
pour un réacteur n'encadre pas la même machine : le commentaire de la table le
disait déjà — « Kilopower ~10 kW pour ~1500 kg (6,7 W/kg) ; les concepts de forte
puissance visent 20-100 W/kg ». Appliquer les 5 W/kg d'un réacteur de 10 kWe à un
réacteur de 1 MWe est une **erreur de catégorie** : elle rendait la NEP
absurdement lourde, donc refusée **pour une mauvaise raison** — exactement le
piège n°77. La puissance spécifique s'interpole donc en logarithme entre les deux
points que le commentaire nomme. `source_mass_kg` reste la primitive linéaire
(son oracle de proportionnalité est intact) ; `power_plant_mass_kg` est la forme
qui entre au budget de masse.

**ET SEUL UN CYCLE THERMIQUE PAIE DES RADIATEURS.** Un panneau et un RTG rejettent
leur chaleur par leur propre surface — c'est ainsi qu'ils sont construits. Un
réacteur à 30 % de rendement jette **2,33 fois** ce qu'il produit et lui faut une
aile : même tuyère, **936 kg** de radiateur sur panneau contre **6 396 kg** sur
réacteur. La fusion, elle, EST sa propre source : pas de réacteur en plus, mais
**256 t de radiateurs** pour 112 MW rejetés à 500 K — et « matériaux » est
précisément le facteur limitant que [GDD 6.4] lui attribue. Il sort du calcul
sans qu'on l'y ait mis.

**PREUVE (première moitié)** : `SPEditor` Succeeded, **3 700 oracles au vert**,
nouveau drapeau `-spnep`, et `ue_centrale_nep_conception.png` — « CENTRALE +
RADIATEURS 21 281 kg (19 % du décollage) », « E2 Propulseur NEP 1 MW · 1022 kW ·
RÉACTEUR · centrale 21 281 kg · radiateur 1066 m2 », décollage 111 059 kg.

### TOUTES LES PIÈCES, SANS EXCEPTION (2026-07-29) — sur décision de l'utilisateur

**J'AVAIS DIFFÉRÉ CE CHANTIER, ET L'UTILISATEUR A TRANCHÉ CONTRE** : « n'invente
pas, utilise des vraies pièces comme indiqué dans le GDD avec leurs vraies stats,
je veux toutes les pièces sans exception ». Mon objection était que lier l'atelier
à la mission demandait 54 chiffres de coûts et de durées que [GDD 20] diffère.
**L'objection était mal posée** : elle supposait qu'il fallait *inventer* ces
chiffres. La consigne en donne la sortie — on cherche les chiffres RÉELS, et là
où ils n'existent pas, **on le dit** au lieu d'en écrire un.

**CE QUE LA COUCHE GESTION ÉTAIT.** Trois moteurs, dont un — « MTX-1 (neuf) » —
**sans aucune lignée**, ce que [GDD 12.1] interdit en toutes lettres (« jamais
génériques »). Elle réécrivait par-dessus la physique des deux autres, et les deux
tables **avaient déjà divergé** : l'Aestus poussait 29 400 N d'un côté et 29 600 N
de l'autre. Il n'y a plus qu'un catalogue, et **les dix-huit pièces sont
commandables**.

**LE POINT DUR, ET IL EST RÉEL** : la plupart des prix unitaires de moteurs-fusées
**ne sont pas publiés**. C'est un fait sur le monde, pas une lacune de recherche.
La réponse est le schéma que le GDD impose déjà pour la fiabilité [12.3.2] : un
**triplet obligatoire** {bas, nominal, haut} avec sa **confiance** et sa
**source** — « pas de précision artificielle » [12.3.4]. Écrire un nombre nu pour
le Merlin 1D serait une approximation déguisée en certitude ; écrire
`{1, 2, 5}` en confiance C avec « NON PUBLIÉ par SpaceX » dit exactement ce qu'on
sait. **Cinq pièces sur dix-huit ont une source publiée (A/B), treize sont des
estimations déclarées comme telles** — et le modèle l'imprime.

Les ancrages mesurés, chacun sur sa ligne :

| Pièce | Prix | Source |
| :--- | ---: | :--- |
| RS-25 | 99,4 / **146** / 146 M$ | contrats NASA/Aerojet : 1,79 Md$ pour 18 ; 3,5 Md$ pour 24 tout compris |
| F-1 | 19 / **21** / 25 M$ | contrat Rocketdyne 1964 : 76 moteurs pour 158,4 M$ (2,08 M$ l'unité) |
| RD-180 | 9,9 / **25** / 70 M$ | Energomash 9,9 M$ (2018) ; ULA ~25 M$ ; jusqu'à 70 selon la source |
| RL10C-1 | 17 / **18,5** / 20 M$ | prix constructeur rapporté publiquement |
| NEXT-C | 4,6 / **9,2** / 12 M$ | contrat NASA GRC : 18,41 M$ pour DEUX propulseurs et DEUX PPU |

**ET LE PRINCIPE CONSERVATEUR JOUE SUR LE PRIX** [GDD 12.5], en **miroir exact**
de `reliability::evaluate` : là-bas une confiance basse tire vers la borne
pessimiste, qui est le BAS d'une fiabilité ; ici la borne pessimiste d'un prix est
le HAUT. Une estimation floue ne rend jamais un programme moins cher qu'une donnée
mesurée. Le NEP-1MW est donc facturé **600 M$**, sa borne haute — et la capture le
montre : « 600.0 M$ retenu (D : 80.0-600.0) ».

**TROIS FAMILLES DE CHIFFRES ONT DISPARU AU LIEU D'ÊTRE INVENTÉES**, et c'est le
cœur de la passe :
- **La courbe de fiabilité par heures d'essai** ne se publie pour aucun moteur.
  Mais chaque pièce porte déjà son **statut de qualification** et sa
  **confiance** — c'est la DÉFINITION d'un statut de qualification que de dire ce
  qu'on a démontré. Une correspondance déclarée remplace dix-huit triplets, et
  elle est légitime pour une raison vérifiable : **elle reproduit à la valeur près
  les deux triplets écrits à la main qu'elle remplace** (RL10 volé/A → 0,998 ;
  Aestus volé/B → 0,995). C'est l'oracle qui la tient.
- **Le délai d'approvisionnement** se dérive du TRL, qui est *défini* comme la
  distance à la maturité de vol : chaque cran sous 9 est une campagne de
  qualification. 12 mois en production, 180 mois pour un TRL 1 — « ce n'est pas un
  achat, c'est un programme ».
- **Le coût de développement est zéro sur la pièce, et c'est voulu** : c'est
  l'ARBRE qui paie la mise au point (`TechNode::research_cost_musd`), et le
  `tech_id` de la pièce est ce qui l'exige. Le porter aussi sur le moteur le
  facturerait **deux fois**, une fois en recherche et une fois par mission.

**ET LA BRANCHE 6 NE S'OUVRE PLUS TOUTE SEULE** [GDD 5.4]. Rendre les dix-huit
pièces commandables aurait donné NEP et fusion dès la première mission — le
**même défaut exactement** que les quatre nœuds « lanceur » qui ne gardaient rien.
Chaque pièce nomme donc le nœud qui la qualifie (`electrique_avancee`, `ntp`,
`nep_megawatt`, `fusion`) : **7 moteurs sur 18 sont gardés**, et le refus DIT la
direction au lieu de constater une impasse — « RECHERCHER nep_megawatt » (piège
n°42).

**DEUX CONVERGENCES QUE JE M'ÉTAIS INTERDITES, ET QUI NE COÛTENT RIEN.** Les
fractions sèches de réservoir viennent maintenant du couple d'ergols réel, plus
d'une constante recopiée — et la calibration **n'a pas bougé** : le catalogue reste
à **10 contrats réalisables sur 11**, les masses baissent seulement de 131 → 128 t
(la vraie fraction cryogénique est 0,11, pas 0,12) et le coût de 2 969 → 2 973 M$.
Mon refus reposait sur une crainte que la mesure ne confirme pas. Et la base de
fiabilité, qui portait encore une fiche pour « MTX-1 », est désormais **semée
depuis le catalogue** : dix-huit pièces, dix-huit fiches, pas une de plus.

**UN SOLIDE N'A PAS DE RÉSERVOIR** — trouvé en faisant tomber un oracle existant,
qui exigeait à juste titre qu'un réservoir ait une fraction sèche non nulle. Ma
première réponse (un « TANK-SOLIDE » à fraction zéro) était un faux réservoir
inventé pour contenter le modèle. La vérité est plus simple : le bloc est coulé
dans l'enveloppe, le catalogue compte déjà cette enveloppe dans la masse du moteur
(7 330 kg pour le P80), et lui donner une fraction la compterait deux fois. La
conséquence est déclarée et **vraie des solides** : on ne redimensionne pas un
propulseur à poudre, on en choisit un autre.

**PREUVE** : `SPEditor` Succeeded, **3 833 oracles au vert** sur les 12 suites
(`test_contenu_gdd` 613 → **782**, `test_ares_modules` 175 → **178**), et
`ue_catalogue_moteurs_reels.png` — « MOTEUR < NEP-1MW > · 600.0 M$ retenu
(D : 80.0-600.0) · concept, lignée Hall haute puissance », « NON QUALIFIE :
RECHERCHER nep_megawatt [GDD 5.4] », « VERROU : LE MOTEUR CHOISI N'EST PAS
QUALIFIE ».

### LE VÉHICULE CONÇU EST CELUI QUI VOLE (2026-07-29) — la boucle de 4.1 se referme

**LE DERNIER MORCEAU.** Le catalogue, le prix, la fiabilité, le réservoir et
l'arbre étaient devenus communs ; il restait la **PILE** — `assess_multistage`
dimensionnait encore N étages *identiques* du moteur de programme, pendant que
l'atelier empilait des pièces hétérogènes dans son coin.

**ET EN LA BRANCHANT, UNE INCOHÉRENCE PLUS PROFONDE EST APPARUE : LES DEUX
COUCHES NE MODÉLISAIENT PAS LE MÊME OBJET.** La conception de départ était une
**fusée** — RD-180 au sol, puis RL10, « masse au décollage », contrôle de
capacité à décoller [GDD 6.3]. Le modèle de mission, lui, **achète son lanceur au
catalogue** et ne fait voler que ce que le lanceur met en orbite. Brancher
naïvement aurait fait dimensionner un premier étage de lanceur comme un étage
orbital, avec l'Isp de 338 s d'un RD-180 là où un vaisseau utilise 449,7 s.
L'incohérence était **invisible tant que l'atelier ne nourrissait rien** — c'est
la signature de cette famille de défauts.

**C'est la mission qui a raison**, et c'est ainsi qu'une agence procède : on
achète un Falcon 9, on construit la sonde. L'atelier conçoit donc le **vaisseau**,
pas le lanceur. Sa pile de départ devient deux étages RL10C-1 à parts égales,
structure 150 kg — **exactement le véhicule que la mission dimensionnait jusque-là**.

**CE QUI SE TRANSMET EST L'ARCHITECTURE, PAS LE Δv ABSOLU.** Le Δv à fournir
n'appartient pas à l'architecte : il tombe de l'objectif et de la **géométrie de
la fenêtre**, que la conception ignore. Ce que l'architecte décide, c'est comment
le **répartir** entre ses étages — « gros étage lent en bas, petit étage vif en
haut » —, et c'est cette décision-là qui passe. La marge, elle, est déjà une
décision séparée (`dv_margin`). Doubler les deux Δv de l'atelier ne change donc
rien ; changer leur **partage** change la masse, et c'est le joueur qui tranche
[anti-feature 1.5].

**LA NON-RÉGRESSION N'EST PAS UNE OPINION, ELLE EST MESURÉE** : la pile de départ
rend la même masse **au kg près**, le même coût, la même fiabilité et le même
calendrier que le mode modèle. Et la calibration ne bouge pas d'un chiffre —
10 contrats réalisables sur 11, 128 t, P = 0,937, 2 973 M$.

**ET LA CONCEPTION MORD** : les mêmes deux étages en Aestus (Isp 324 au lieu de
449,7) font passer la mission de **7,7 t à 11,2 t, +46 %**. Une pile hétérogène se
paie **étage par étage** (un RS-25 en bas et un RL10 en haut coûtent 164,5 M$, pas
deux fois la même chose), un étage NEP fait porter **sa centrale** à la mission par
le même chemin que l'atelier, et **un seul** étage non qualifié suffit à refuser —
en nommant son nœud.

**UN ORACLE DE NON-RÉGRESSION A ATTRAPÉ UNE VRAIE FAUTE DE MODÈLE.** La première
rédaction imputait `n_burns` manœuvres au dernier étage **en plus** d'un allumage
à chacun des autres : sur deux étages et deux manœuvres, cela comptait **trois
allumages pour deux** — un risque que le véhicule ne court pas. C'est la
comparaison au mode modèle, que personne n'avait touché, qui l'a fait tomber. Le
partage juste : chaque étage inférieur fait une manœuvre puis est largué, le
dernier fait celles qui restent (au moins une). La somme vaut exactement
`n_burns` dès qu'il y a plus de manœuvres que d'étages.

**ET LA DUPLICATION DISPARAÎT DE L'ÉCRAN, PAS SEULEMENT DU MODÈLE.** Le poste
CONTRÔLE portait ses propres sélecteurs « MOTEUR » et « ÉTAGES ». Depuis que la
pile conçue vole, ces boutons ne décideraient plus rien : les laisser aurait été
pire qu'inutile, cela aurait **menti sur qui décide**. Le poste MONTRE désormais
le véhicule — un étage par ligne, avec son prix et sa confiance, en rouge s'il
n'est pas qualifié — et renvoie à CONCEPTION.

**PREUVE** : `SPEditor` Succeeded, **3 847 oracles au vert** sur les 12 suites
(`test_contenu_gdd` **796**), et `ue_vehicule_concu_vole.png` — « VEHICULE —
concu au poste CONCEPTION [GDD 4.1] », « E1 RL10C-1 18.9 M$ (B : 17.0-20.0) »,
« E2 Propulseur NEP 1 MW 600.0 M$ (D : 80.0-600.0) — NON QUALIFIE : RECHERCHER
nep_megawatt », « 2 etage(s), 618.9 M$ de moteurs ». La pile posée au poste
CONCEPTION est bien celle que le poste CONTRÔLE juge.

### RADIATEURS, RÉACTEURS, CONFINEMENT (2026-07-29) — [GDD 12.4] avait un module et zéro appelant

**LE CHAPITRE ENTIER ÉTAIT MODÉLISÉ ET DÉBRANCHÉ.** [GDD 12.4] tient en une
phrase — « radiateurs, réacteurs, confinement : chaque sous-système avancé a sa
propre fiabilité, souvent **dimensionnante** » — et `reliability/AdvancedFilieres.hpp`
l'implémentait mécanisme par mécanisme, **sans un seul appelant vivant**. Choisir
un NEP ne coûtait donc rien de plus qu'un chimique, une fois sa centrale payée en
masse. Les trois mécanismes sont désormais au produit de `p_success`, dans un
facteur SÉPARÉ et NOMMÉ (`p_filieres`, `cause_filieres`) — parce qu'un chiffre
sans cause n'est pas actionnable.

**ET `collision_probability` ÉTAIT AUSSI SANS CONSOMMATEUR** — trouvé en cherchant
où brancher les radiateurs. Neuvième cas de la famille.

**(1) LE CŒUR VIEILLIT, ET LA DURÉE DÉCIDE.** Une NEP pousse en **continu** : deux
ans de croisière consomment deux ans de vie de cœur sur les sept qu'il a
(`CORE_FULL_POWER_YEARS` = 7, la spécification de SP-100, borne basse documentée).
Un NTP tire quelques minutes et ne brûle rien — son cyclage thermique le fissure,
et c'est `is_ntp`. Mesuré : **17,3 % de risque à 730 jours, 76,9 % sur un
aller-retour de 8 365 jours**, contre 25,9 % pour le moteur lui-même. « SOUVENT
dimensionnante » est donc exact **et la mesure dit quand** : sur deux ans, un
moteur spéculatif (R0 = 0,75) domine encore son réacteur ; sur l'aller-retour,
c'est le cœur qui décide.

**(2) UNE AILE DE RADIATEUR EST UNE CIBLE** [GDD 7.8, 10.5]. La NEP mégawatt
traîne **mille mètres carrés** : à surface pareille, la section de collision du
véhicule n'est plus celle d'une sonde. Le couloir traversé est celui que l'agence
a **pollué elle-même**, et le temps qu'on y passe est celui de la **campagne
d'assemblage** — déjà calculé, jamais posé à la main. Mesuré : **3,5 % de risque**
sur une campagne de 180 jours dans une LEO basse à 50 000 objets. Un tir unique,
lui, n'a aucune exposition — il injecte sans traîner, et c'est vrai, pas une
omission.

**(3) LE CONFINEMENT DE L'ANTIMATIÈRE, ET LE TAUX NE VIENT PAS DE NULLE PART.**
« Perte de confinement = ÉVÉNEMENT CATASTROPHIQUE » : on modélise donc la
probabilité qu'aucune perte n'ait lieu de tout le vol, sur une durée qui se compte
en décennies. Le taux est celui que le **palier d'antimatière DÉCLARE**
(`AntimatterProduction::loss_rate_per_day`), déjà calibré avec la fin de jeu — une
seconde constante aurait été un nombre que personne n'a calibré. Résultat :
**8,0 % de perte sur l'aller-retour de 22,9 ans** au palier abouti (1e-5/j), et
**97,4 % sur un seul an** au palier d'aujourd'hui (1e-2/j, le CERN). Un risque à
la fin de l'arbre, une interdiction au début : c'est exactement ce que
[GDD 5.12.12] décrit comme un changement de régime. L'identification du risque
d'une perte catastrophique au taux de perte continue du même confinement est une
approximation, et elle est **déclarée** [GDD 12.5].

**TROIS FAUTES À MOI, TOUTES ATTRAPÉES PAR LA MESURE**, et elles valent d'être
dites parce qu'elles se ressemblent — chacune donnait un mécanisme *actif pour une
mauvaise raison* :
- **le vieillissement calendaire était piloté par le DÉLAI D'APPROVISIONNEMENT.**
  Le NEP étant TRL 2, donc 180 mois de délai, il perdait **27 % de fiabilité avant
  de décoller**. Or un réacteur se construit à la FIN de ce délai : il ne vieillit
  pas pendant qu'on l'attend. Seules l'intégration puis le vol comptent.
- **ma densité de débris de test était SEPT ORDRES trop haute** (1e-10 contre
  2e-16 pour une LEO à 50 000 objets). Le radiateur y était détruit à coup sûr, et
  l'oracle aurait validé le mécanisme sur un nombre commode. Le couloir 200-600 km
  fait 2,3e20 m³ : la densité se CALCULE, elle ne se choisit pas.
- **la « cause dominante » se comparait au produit accumulé** et non aux autres
  facteurs : dès que deux mécanismes jouaient, le second ne pouvait plus jamais
  être nommé. On garde donc le pire facteur INDIVIDUEL — et l'oracle vérifie que
  le verdict change de cause quand la domination change (cœur sur deux ans,
  radiateurs sur un vol court en couloir pollué).

**CE QUI RESTAIT DÉLIBÉRÉMENT NON MODÉLISÉ** ~~, ET DÉCLARÉ~~ : la **perforation**
par particules sub-millimétriques, l'autre mécanisme radiateur de 12.4.
`env::Debris` ne porte que les objets **catalogués** — ceux d'une collision, pas
la population qui perce un tube. J'écrivais alors : « il faudra un modèle de flux
sub-millimétrique (Grün) que rien dans le dépôt ne porte aujourd'hui ».

> ⚠ **SOLDÉ LE 2026-07-30.** La phrase était vraie du dépôt et fausse du monde :
> le modèle de Grün est publié depuis 1985, et les trois pièces qui lui manquaient
> (limite balistique, densité des météoroïdes, géométrie de circuit) le sont aussi.
> Troisième fois que « je ne peux pas sans inventer » signifie « je n'ai pas
> cherché » — piège **n°86**. Voir §2, « LA PERFORATION SE CALCULE ».

**PREUVE** : `SPEditor` Succeeded, **3 864 oracles au vert** sur les 12 suites
(`test_contenu_gdd` **809**, `test_session` **702**), nouveau drapeau
`-spnep=qualifie`, et `ue_filieres_avancees_12_4.png` — « SOUS-SYSTEMES AVANCES
-0,7 % · vieillissement du coeur nucleaire » sur une sonde courte, le bilan
complet calculé (41 205 kg, 2 354 M€, 184 mois, P = 66,8 %).

### LA BRANCHE NUMÉRIQUE (2026-07-29) — cinq modules, une seule décision

**J'AVAIS ANNONCÉ CINQ MODULES À BRANCHER UN PAR UN. C'ÉTAIT UNE MAUVAISE
LECTURE.** En cherchant où brancher `force/Drag.hpp`, la mesure a montré autre
chose : `force/Drag`, `force/Srp`, `nav/Statistics`, `prop/Propagator`,
`nav/OrbitDetermination` et `flight/Session.hpp` ne sont pas cinq oublis, ce sont
les **feuilles d'UNE SEULE branche** — la propagation NUMÉRIQUE avec pile de
forces. Aucune n'est atteignable depuis le jeu **pour une raison écrite**, et elle
est écrite depuis toujours dans `NavSolution.hpp` [GDD 6.8] : la machinerie
complète « demande une propagation numérique par mesure, toutes les 60 s sur un
arc de deux semaines : hors de portée d'un écran qui se rafraîchit », et on garde
donc « la MÊME algèbre et les MÊMES partielles, sur des états KÉPLÉRIENS ».

**CE N'EST DONC PAS LA FAMILLE « MODÈLE SANS CONSOMMATEUR » : c'est une DÉCISION
D'ARCHITECTURE dont la conséquence n'avait jamais été inscrite ici.** Le jeu
propage analytiquement (`kepler_propagate`) partout ; la branche numérique reste
comme **implémentation de référence**, et elle a ce rôle réellement : quatre de
ces modules sont exercés par oracle (`force/Drag` et `force/Srp` dans
`test_reentry_perturb`, `nav/Statistics` et `astro/Mga1Dsm` dans
`test_astro_core`). Écrits, validés, inatteignables depuis le jeu — ce qui n'est
pas la même chose que morts. **Ma phrase « ~750 lignes de physique morte » était
une surestimation, et elle est corrigée ci-dessus.**

**RESTAIT UN VRAI CAS, ET UN SEUL** : `flight/Descent.hpp` — descente propulsée
sur corps sans atmosphère — que **ni le jeu ni AUCUNE suite** n'incluait. Or son
en-tête affirmait une validation : « Vérifié : Lune, Isp 311 s, TWR 2-3 ->
~1730-1750 m/s (Apollo LM : ~2000 m/s marge comprise) ». Personne ne l'avait
jamais exécuté. **Un commentaire « vérifié » sans oracle est une opinion**
(piège **n°89**).

**L'ORACLE EXISTE MAINTENANT, ET LE MODÈLE TIENT** — mais il corrige la note :

| TWR de surface | Δv de freinage mesuré | PDI optimal |
| ---: | ---: | ---: |
| 1,2 | **2 028 m/s** | — |
| 2 | **1 798 m/s** | 31 km |
| 3 | **1 731 m/s** | 14 km |
| 6 | **1 691 m/s** | — |

La note disait « TWR 2-3 → ~1730-1750 » : c'est vrai de TWR 3, **pas de TWR 2**.
Et les deux bornes physiques sortent du calcul sans qu'on les y ait mises : à
forte poussée le freinage tend vers la **vitesse orbitale rasante (1 680 m/s)**,
limite impulsionnelle ; à TWR 1,2 il la **dépasse** — les pertes de gravité sont
réelles, et c'est « le vrai arbitrage d'ingénierie de tout alunisseur » que
l'en-tête revendiquait. Tout reste sous les ~2 000 m/s qu'Apollo emportait.

**CE QUI LUI MANQUE N'EST PAS UN APPELANT, C'EST UNE MISSION** — et c'est un
manque de CONTENU contre le GDD, pas de code. Le GDD nomme le lunaire **quatre
fois** : [3.3] le rang Principal est défini par « vol habité **cislunaire** »,
[5.9] « architectures lunaires », [5.10] « missions lunaires avancées », et
[19.7] donne à « Vol habité lunaire / cislunaire » ses **cinq verrous** (puissance,
masse, thermique, radiations, maintenance). L'annexe Δv donne même le chiffre
d'entrée : **LEO → injection translunaire ~3,1 km/s**. Le catalogue, lui, n'a
**aucune** mission lunaire sur ses onze entrées. C'est le prochain pas, et il est
maintenant entièrement spécifié par le document.

**PREUVE** : **3 876 oracles au vert** sur les 12 suites (`test_reentry_perturb`
108 → **120**).

### LE CISLUNAIRE EXISTE (2026-07-29) — et l'alunissage se CALCULE

**LE MANQUE ÉTAIT DU CONTENU, PAS DU CODE.** Le modèle de descente propulsée
était écrit et désormais validé ; ce qui lui manquait était une **mission**. Le
GDD nomme le lunaire quatre fois — [3.3] le rang Principal est *défini* par « vol
habité **cislunaire** », [5.9] « architectures lunaires », [5.10] « missions
lunaires avancées », [19.7] lui donne ses **cinq verrous** — et le catalogue n'en
avait aucune sur onze entrées. `CAT-12 « Vol habite cislunaire et alunissage »`
existe, rang Principal, trois à bord (Apollo), et ses six prérequis sont les cinq
colonnes de la matrice 19.7 traduites en nœuds d'arbre.

**LE Δv N'EST PAS UN FORFAIT.** Ce qui ne dépend pas du véhicule vient de sources
nommées : **TLI 3 100 m/s est la ligne de l'annexe Δv du GDD**, insertion lunaire
900 et injection retour 1 000 sont les valeurs d'Apollo. L'**alunissage**, lui,
sort de l'intégration — descente à poussée constante, guidage gravity-turn,
gravité centrale exacte — et **il dépend du moteur que le joueur a choisi**.

**LE CONTRÔLE EST LE TOTAL, ET IL TOMBE JUSTE** : Δv de conception **8 512 m/s**
contre un budget post-LEO d'Apollo de **~8 900 m/s** (TLI 3 050 + LOI 900 +
descente 2 050 + remontée 1 850 + TEI 1 000). **4 % d'écart**, sur un total dont la
moitié est calculée et non posée.

**ET L'ARBITRAGE EST RÉEL, DANS LE BON RÉGIME.** Un RL10 sur un atterrisseur de
5 t donne **T/W 12,3** : il est déjà au **plancher impulsionnel** (1 680 m/s), et
plus de poussée n'y achète rien — c'est la physique, pas une limite du modèle.
Comparer un RS-25 n'y mesurerait donc rien (8 510 contre 8 512, du bruit).
Le régime qui mesure est celui d'un atterrisseur **sous-dimensionné** : l'AJ10-190
de la navette (26,7 kN) donne **T/W 1,59** et **157 m/s de pertes de gravité par
allumage**. Et un SPT-100 de 83 mN est refusé net, en disant pourquoi.

**DEUX DÉFAUTS TROUVÉS EN BRANCHANT, ET LE PREMIER EST DANS LE MODULE QUE PERSONNE
N'AVAIT JAMAIS EXÉCUTÉ** :
- **`descent_dv_required` rendait ZÉRO hors de son domaine.** À T/W élevé, le
  freinage anti-vitesse résiste AUSSI à la chute : le véhicule annule sa vitesse
  quasiment sans perdre d'altitude, aucune solution de la dichotomie n'« atteint
  le sol », et la fonction retournait 0 — c'est-à-dire **« atterrir est gratuit »**.
  Un mensonge silencieux, et exactement le genre qu'un module jamais exécuté
  garde des années. Réparé par un **théorème, pas un rattrapage** : on ne se pose
  pas depuis une orbite pour moins que l'annulation de sa vitesse orbitale, donc
  le Δv de freinage est minoré par `sqrt(μ/R)` — et dans ce régime, cette borne
  EST la bonne réponse.
- **je mesurais le T/W contre la seule charge utile** : 1 181 kg pour un RL10 de
  102 kN, soit **T/W 53**, un chiffre sans aucun sens physique — et c'est lui qui
  poussait la fonction hors de son domaine. La masse qui compte est celle de
  l'atterrisseur **allumé**, structure et ergols de descente comprises. Elle
  dépend du Δv qu'on cherche : résolue par une passe préalable amorcée sur la
  limite impulsionnelle, comme partout ailleurs dans ce fichier. `Assessment`
  expose désormais `m0_dernier_etage_kg` — `m0_kg` compterait des étages déjà
  largués, `payload_kg` oublierait l'étage lui-même.

**PREUVE** : `SPEditor` Succeeded, **3 894 oracles au vert** sur les 12 suites
(`test_session` 702 → **716**, `test_contenu_gdd` 809 → **813**).
⚠ **LA MESURE DU CATALOGUE A CHANGÉ DE DÉNOMINATEUR** : **11 contrats réalisables
sur 12** (contre 10 sur 11). CAT-12 est réalisable ; le seul hors de portée reste
CAT-11, le relativiste, pour la raison physique déjà documentée.

### LE SUPPORT-VIE TOMBE TOUS LES 74 JOURS (2026-07-29) — la source existait

**« NON TOUCHÉ FAUTE DE SOURCE » ÉTAIT FAUX.** Le §7 portait depuis deux passes :
« les taux de `event_library` donnent ~1 avarie par 400 jours toutes causes
confondues, ce qui est BAS pour un véhicule habité réel. Non touché faute de
source. » C'est exactement le piège n°86 — et cette fois je l'ai appliqué à
moi-même. **La source est publiée, et elle est de la NASA.**

L'ISS a quatre sous-systèmes de support-vie — OGS (oxygène), CDRA (épuration du
CO2), UPA (urine), WPA (eau) — dont les MTBF **en vol** vont de 5 000 à 14 000 h.
Mis en série, l'ECLSS de l'ISS a **au plus 1 780 h de MTBF, soit 0,20 an =
74,2 jours**. C'est le chiffre du corpus de dimensionnement des rechanges
martiennes (NTRS/ICES), et **il est cohérent avec lui-même** : quatre sous-systèmes
à ~7 100 h en série donnent bien 1 780 h.

| | Modèle (avant) | ISS mesuré |
| :--- | ---: | ---: |
| MTBF support-vie | 1 250 jours | **74,2 jours** |
| Taux | 8,0e-4 /j | **1,348e-2 /j** |

**Dix-sept fois trop optimiste.** Ce n'est pas un durcissement de gameplay, c'est
la mesure — et le même corpus explique pourquoi une mission martienne emporte
**3,9 à 6,0 t de rechanges d'ECLSS** : le modèle prédit désormais **12,1 pannes de
support-vie sur un aller-retour de 900 jours**, ce qui est très exactement ce que
cette masse de rechanges décrit. La normalisation tombe juste au passage :
`effective_rate` module ce taux par (1 − R)/0,02, donc il vaut pour **R = 0,98** —
du matériel réel et mûr, ce que l'ISS est. Un vaisseau mieux conçu casse moins.

**LES QUATRE AUTRES TAUX N'ONT PAS BOUGÉ**, et c'est délibéré : je n'ai de source
publiée que pour l'ECLSS, et la corriger suffit à redresser le total — une avarie
tous les **66 jours** au lieu de 400, dont **89 % de support-vie**, exactement
comme l'ECLSS domine la maintenance de l'ISS. Les inventer serait retomber dans le
piège n°86 par l'autre bout.

**ET LE TAUX RÉALISTE A RÉVEILLÉ UNE HYPOTHÈSE ENDORMIE.** L'oracle « 10 frames ou
40 frames consomment AUTANT » est tombé. Le commentaire du code énonçait
lui-même ce qui venait de cesser d'être vrai : *« pas besoin de sous-pas ici, et ce
n'est pas un relâchement : la consommation est LINÉAIRE en dt »*. Elle l'était
**tant que l'état d'avarie ne changeait pas dans une frame** — ce qui n'arrivait
presque jamais à 17 fois trop peu d'avaries. À taux réel, les avaries commencent
et se réparent au milieu d'une frame, `effets_avaries` devient une fonction du
TEMPS, et l'évaluer une fois par frame attribue à toute la frame l'état d'un seul
instant. La consommation s'intègre désormais par **sous-pas de 1/64 j sur une
grille absolue**. C'est le troisième exemplaire de la même leçon (pièges n°36-38) :
**rendre une grandeur fréquente réveille tout ce qui la supposait rare.**

**ET L'ORACLE LUI-MÊME MESURAIT AUTRE CHOSE QUE CE QU'IL DISAIT** (piège n°82).
Il comparait **deux parties aux passés différents** — son propre commentaire
l'assumait (« les deux parties n'ont pas le même passé ») — ce qui était sans
conséquence quand rien ne dépendait de l'état, et faux dès que les avaries
comptent : mesuré, **une avarie de chaque côté et 21,6 jours d'autonomie d'écart**.
Il compare maintenant deux parties **fraîches et identiques** (même nom, donc même
graine), et seul le découpage change : écart **7,1e-14**, du bruit de flottant.

**TROIS DIAGNOSTICS FAUX AVANT DE MESURER, ET C'EST LA VRAIE LEÇON DE LA PASSE.**
J'ai accusé successivement la grille de tirage (elle était déjà absolue —
`jour = floor(maintenant)` — mon « correctif » ne corrigeait rien, c'est écrit dans
le code), puis le verrou de rattrapage (`MAX_FENETRES_PAR_FRAME` = 400, hors de
cause), avant de simplement **imprimer les deux valeurs et le nombre d'avaries**.
Le diagnostic est tombé en une ligne. Piège **n°90** : sur une divergence
numérique, imprimer les deux nombres AVANT de former une hypothèse.

**PREUVE** : `SPEditor` Succeeded, **3 903 oracles au vert** sur les 12 suites
(`test_ares_modules` 178 → **188**).

### LA PERFORATION SE CALCULE (2026-07-30) — le dernier mécanisme de 12.4

**LE DERNIER MANQUE DÉCLARÉ DE [GDD 12.4] N'EN ÉTAIT PAS UN.** Le §2 portait :
« il faudra un modèle de flux sub-millimétrique (Grün) **que rien dans le dépôt ne
porte aujourd'hui** ». Vrai du dépôt, faux du monde — et c'est la **troisième**
fois que « je ne peux pas sans inventer » veut dire « je n'ai pas cherché »
(piège n°86). Les quatre pièces sont publiées, et aucune n'a demandé d'invention :

| Pièce | Source | Ce qu'elle donne |
| :--- | :--- | :--- |
| Flux cumulé | **Grün et al. (1985)**, référence du flux interplanétaire à 1 UA (Pioneer/HEOS, microcratères lunaires, lumière zodiacale) | F(m) en particules·m⁻²·an⁻¹, m en grammes, plaque d'orientation aléatoire sous 2π, domaine 1e−18 à 1 g |
| Densité des grains | **SSP-30425B** (environnement naturel de dimensionnement de la Station) | 2,0 / 1,0 / 0,5 g·cm⁻³ par palier de masse |
| Limite balistique | **Cour-Palais**, p = 5,24·d^(19/18)·BH^(−1/4)·(ρp/ρt)^(1/2)·(v/c)^(2/3), avec k = **1,8** perforation / 2,2 écaillage détaché / 3,0 naissant (Cour-Palais/Christiansen, établis sur Al 7075-T6) | le diamètre qui perce une paroi donnée |
| Géométrie de circuit | **ISS HRS** : panneau 3,33 × 2,64 m (8,79 m²), **22 tubes en parallèle** | 0,40 m² par circuit indépendant |

**ET LDEF TRANCHE LA QUESTION QUI DÉCIDE DE TOUT** — quelle population perce ?
5,7 ans en orbite, 130 m² récupérés et analysés cratère par cratère : les **débris**
dominent sous **30 µm** de profondeur de pénétration dans l'aluminium, les
**météoroïdes** au-dessus. Une paroi de caloduc fait 500 à 2 000 µm. Modéliser Grün
seul n'est donc pas une approximation commode, c'est **le bon choix de population** —
et l'omission d'ORDEM ne mordrait que sur une phase d'assemblage en orbite basse.

**LE RECOUPEMENT, ET IL N'EST PAS FLATTEUR.** SSP-30425B est le seul point de
comparaison indépendant, et il ne donne pas raison à Grün :

| Flux cumulé (/m²/an) | Grün 1985 | SSP-30425B (orbite ISS) | Écart |
| :--- | ---: | ---: | ---: |
| ≥ 10 µm | 9,36e1 | 7,91e2 | SSP **×8,45** |
| ≥ 100 µm | 1,43e0 | 8,78e0 | SSP **×6,16** |
| ≥ 1 mm | 3,20e-3 | 5,22e-3 | SSP **×1,63** |

L'écart **se resserre** exactement là où ce modèle travaille, mais il ne s'annule
pas et il va dans le **mauvais sens**. Trois raisons connues : SSP est un
environnement de *dimensionnement* (majorant par construction), il s'applique en
orbite terrestre où la focalisation gravitationnelle concentre le flux, et il
descend d'un modèle antérieur à Grün. **On garde Grün sans facteur correctif** : la
mission passe l'essentiel de son temps en croisière interplanétaire, le régime pour
lequel Grün *est* le modèle juste, et corriger pour rejoindre un standard terrestre
serait calibrer sur le mauvais milieu. L'écart est **inscrit dans l'en-tête** pour
que personne ne le reprenne pour un bug. Et il faut dire dans quel sens le modèle
penche **au total** : l'hypothèse **paroi simple** (un radiateur réel est blindé en
Whipple, ce qui relève la limite balistique de près d'un ordre de grandeur en
diamètre, soit **~2 000 en flux**) domine de loin le déficit de ~6 contre SSP. Le
modèle est **net pessimiste, largement** — la seule direction acceptable pour un
verdict de survie [GDD 12.5].

**LE RÉSULTAT CONTRE-INTUITIF, ET C'EST LUI QUI FAIT LE GAMEPLAY.** Une aile de
radiateur n'est pas une surface, c'est **N circuits** : chacun meurt de *sa*
première perforation, donc la capacité survivante est la probabilité qu'un circuit
soit intact, `exp(−Φ·a_segment·t)`. **Elle ne dépend pas de la surface totale.**
Mille mètres carrés découpés en mille circuits vieillissent comme un seul mètre
carré. Le levier n'est pas la taille, c'est l'**épaisseur** et la **segmentation** :

| Paroi Al 6061-T6 | Φ perforation (/m²/an) | Capacité à 900 j (circuit 0,40 m²) |
| ---: | ---: | ---: |
| 0,5 mm | 1,09e0 | 0,341 |
| 1,0 mm | 1,46e-1 | 0,866 |
| **1,5 mm** | **4,01e-2** | **0,961** |
| 2,0 mm | 1,54e-2 | 0,985 |
| 3,0 mm | 3,81e-3 | 0,996 |

**ET LE FORFAIT 1,15 A ENFIN UN CALCUL DERRIÈRE.** `RadiatorSpec::redundancy_margin`
valait 1,15 avec pour toute justification « surface excédentaire, perforations
tolérées ». Il est maintenant **dérivé** : on tolère moyenne + 3σ de circuits morts
sur la durée pour laquelle on construit. Mesuré, à 900 j et 1,5 mm : **1,053** sur
1 000 m² et **1,173** sur 10 m². Le forfait unique **encadrait** les deux — trop
généreux pour une grande aile, trop chiche pour une petite, parce que 3σ/N décroît
en 1/√N et qu'une grande aile *moyenne* ses pertes. Un seul chiffre là où la
statistique en exige deux.

**LA CALIBRATION NE BOUGE PAS, ET C'EST MESURÉ, PAS ESPÉRÉ** :

| Aile | Forfait 1,15 | Marge dérivée + blindage | Écart |
| :--- | ---: | ---: | ---: |
| FUSION-DD, 38 647 m² (96 617 circuits) | 255 823 kg | 263 186 kg | **×1,029** (+7,4 t sur 256 t) |
| SPT-100, 1,8 m² (4,5 circuits) | 8,5 kg | 12,2 kg | ×1,446 (**+3,8 kg**) |

Sur l'aile **dominante** — celle qui pèse des tonnes et fixe la calibration du jeu —
le remplacement du forfait par le calcul déplace **2,9 %** : la marge dérivée y est
plus basse que 1,15 et le blindage à payer reprend à peu près ce qu'elle rend.
L'écart relatif sur une petite aile est gros et ne veut rien dire (c'est la sortie
la **moins fiable** du modèle : un pas de tube mesuré sur un panneau de 8,8 m²
appliqué à 1,8 m²), et il porte sur **quelques kilos**. L'oracle verrouille donc
l'**absolu** de ce côté, pas le ratio.

**LA CONSÉQUENCE DE MISSION EST UN DÉPASSEMENT D'ENDURANCE, ET ELLE SE NOMME.** Le
radiateur est *payé* pour une endurance (900 j = l'aller-retour martien de
conjonction, la classe de mission que le jeu porte). La sanction ne tombe pas parce
qu'il se fait percer — il se fait *toujours* percer — mais quand la mission dure
plus longtemps que ce qu'on a acheté. Mesuré sur une NEP 1 MWe (976 m²) :

| Durée du vol | p_filieres | Cause dominante nommée |
| ---: | ---: | :--- |
| 900 j (= endurance) | 0,7913 | vieillissement du cœur nucléaire |
| 8 365 j (23 ans) | **7,47e-117** | **perforation des radiateurs : vol au-delà de l'endurance** |

C'est le seul mécanisme qui reste quand le vol dure des années : le burnup du cœur
**plafonne** (borné à 2), la perforation non. Et un chimique ne paie rien, même sur
vingt-trois ans — le mécanisme est propre à la filière alimentée [GDD 5.12.1].

**QUATRE DÉFAUTS TROUVÉS, DONT DEUX PAR MES PROPRES ORACLES** :

- **le point fixe de la marge n'en était pas un.** La marge dimensionne la surface,
  qui rentre dans N : j'itérais quatre fois en écrivant « ça converge en deux tours ».
  **C'était faux** — la pente est négative, l'itération **oscille** sur une petite
  aile. Elle n'a pas besoin d'itérer : en posant u = √(1/M), c'est le trinôme
  `u² + c·u − (1 − q) = 0` avec `c = k√(q·a/A₀)`, dont on prend la racine positive.
  **Exact, sans boucle.** Un oracle réinjecte le résultat pour le vérifier.
- **le paramètre que j'avais déclaré « à calibrer » a mordu tout de suite.** J'avais
  posé la surface de circuit à 1,0 m² « ordre de grandeur », en signalant qu'elle
  avait un levier direct. Elle en avait un : sur une petite aile, elle faisait
  exploser la marge de **60 %** au lieu de 20. **L'oracle a refusé mon chiffre**, et
  la bonne réponse n'était pas de déplacer le seuil — c'était d'aller chercher le pas
  de tube réel (ISS HRS : 8,79 m² / 22 tubes = **0,40 m²**). Déclarer un paramètre ne
  le rend pas innocent.
- **la probabilité rendait un ZÉRO EXACT**, donc un verdict binaire déguisé —
  exactement ce que [GDD 12.4] interdit (« jamais un verdict binaire décrété »).
  Cause : `0,5·(1 + erf(z/√2))` s'annule par **cancellation** dès |z| > 6, alors que
  la vraie valeur (1e-115 à 23 ans) est largement représentable. Passé à `erfc`, qui
  ne s'annule pas : 7,47e-117. Le vrai plancher est z ≈ −37, où c'est le **double**
  qui sature — et un oracle le nomme au lieu de prétendre qu'il n'existe pas.
- **la ligne du HUD était TRONQUÉE au bord du panneau**, et c'est le mot
  « endurance » qui disparaissait — le seul qui portait l'information. Trouvé en
  **regardant la capture**, pas en relisant le code.

**PREUVE** : `SPEditor` Succeeded, **3 974 oracles au vert** sur les 12 suites
(`test_gdd_manques` 57 → **102**, `test_contenu_gdd` 813 → **839**), et
`ue_perforation_radiateurs.png` — « radiateur 976 m2 (paroi 1.5 mm, 900 j) » au
poste CONCEPTION.

### L'ASSISTANCE GRAVITATIONNELLE (2026-07-31) — et ce que j'ai refusé de livrer

**QUATRE EN-TÊTES MORTS D'UN COUP**, trouvés par le même balayage : `astro/Mga`,
`astro/Mga1Dsm`, `astro/LocalRefine`, `astro/BPlane`. Et contrairement à la branche
de propagation numérique — inatteignable **pour une raison écrite** [GDD 6.8] —
aucune décision ne justifiait celle-ci. Le GDD nomme les assistances dans la table
des compétences, colonne **Senior → Directeur**, et `MgaProblem` portait déjà les
contraintes du JEU dans ses propres commentaires : `c3_max` « ce que le lanceur
VEND », `tof_total_max` « ce que le RTG supporte ». La brique avait été écrite pour
être branchée et ne l'avait jamais été.

**LE COÛT, MESURÉ AVANT DE CONCEVOIR** : une optimisation complète prend **20 à
60 ms**, une évaluation unitaire **0,002 ms**. Aucun argument de performance ne
tenait.

**LE TROC EST RÉEL, ET IL EST BEAU** — cible Jupiter, depuis un parking à 200 km :

| | C3 | Δv total | transit |
| :--- | ---: | ---: | ---: |
| Direct | 78,0 km²/s² | **8 144 m/s** | 2,46 ans |
| E-E-J (survol terrestre de Juno) | 29,0 | **6 680 m/s** | 4,47 ans |

**1 464 m/s économisés pour deux ans de vol.** Et depuis cette session, ces deux
ans ne sont plus gratuits : ils consomment les vivres, brûlent la vie du cœur
[GDD 12.4], percent les radiateurs et tirent les avaries. Les deux plateaux de la
balance sont chargés — c'est exactement la décision que le GDD veut faire prendre.

**UN CONTRÔLE QUI TOMBE JUSTE.** Le |v∞| minimal au survol se dérive : après un
survol, |v_helio| ≤ |v_planète| + |v∞|, donc il faut au moins la vitesse
héliocentrique d'un Hohmann depuis le pivot. Pour un survol **terrestre** vers
Jupiter, ma formule rend **8 796 m/s** ; l'en-tête de `Mga1Dsm.hpp` annonce
**8,79 km/s**, obtenu indépendamment. Cet élagage n'est pas une optimisation de
confort : sans lui, l'optimiseur explore des tours physiquement impossibles et paie
l'impossibilité en manœuvre profonde géante.

**DEUX DÉFAUTS À MOI, ET LE PREMIER EST UNE RÉCIDIVE DU PIÈGE n°93.** (1) J'ai
d'abord branché `Mga.hpp` — le MGA **pur**, sans manœuvre en espace profond. Les
tours sortaient PIRES que le direct. Ce n'était pas un bug : deux arcs de Lambert
consécutifs n'ont aucune raison de se rejoindre au même |v∞|, et le MGA pur paie le
désaccord AU MOTEUR. **Son successeur le dit dans ses dix premières lignes**, avec
le chiffre (4 935 m/s payés sur un seul survol mal raccordé) — et je ne l'avais pas
lu. Encore une fois : le voisin d'à côté levait l'hypothèse. (2) `c3_max` n'est
qu'une **pénalité de coût** dans le module (`cost += 50 × dépassement`), correct
pour guider un optimiseur, faux pour un verdict de mission ; mon oracle du « plafond
irréaliste » est passé au vert sans rien vérifier. La contrainte dure est
maintenant appliquée dans la couche mission.

**CE QUE J'AI REFUSÉ DE LIVRER, ET C'EST LE POINT.** Le catalogue de tours ne porte
**qu'une seule séquence**. Mesuré, cible Jupiter (direct = 8 144 m/s) :

| tour | optimiseur | Δv total | verdict |
| :--- | :--- | ---: | :--- |
| E-E-J (Juno) | DE seule | 8 583 m/s | **pire que direct** |
| E-E-J (Juno) | DE + MBH 30 sauts | **6 504 m/s** | −1 640, +2,0 ans |
| E-V-E-E-J (Galileo VEEGA) | DE + MBH 30 sauts | 18 019 m/s | DSM de 13 km/s |

À trois survols le problème passe à 18 dimensions et les ~110 000 évaluations du
budget ne suffisent pas. **Livrer ce tour-là aurait été livrer un modèle qui ment** :
il aurait proposé au joueur, sous le nom de « Galileo », une trajectoire deux fois
plus chère qu'un transfert direct. J'ai donc mis la garde DANS LE CODE — un tour
dont le Δv ne bat pas le direct est refusé et le dit — de sorte qu'ajouter une
séquence soit **sûr**. Ce qu'il faudrait pour les tours longs est chiffré :
`astro/LocalRefine.hpp` (écrit, sous oracle, sans appelant lui aussi) branché dans
le MBH à la place de la DE resserrée ; son en-tête annonce « 13 200 évaluations pour
faire ce qu'un gradient fait en 50 ». C'est le prochain incrément de cette branche.

**⚠ TROIS ORACLES DE `test_session` SONT TOMBÉS PENDANT CETTE PASSE — ET CE N'ÉTAIT
PAS ELLE.** Voir la section suivante : diagnostiqués et **réparés** dans la foulée.

**PREUVE** : `SPEditor` Succeeded, **4 033 oracles** (`test_reentry_perturb`
153 → **182**).

### L'ASSISTANCE A ENFIN UNE MISSION (2026-07-31) — et quatre défauts trouvés en la branchant

**LA PASSE PRÉCÉDENTE AVAIT BRANCHÉ UNE COUCHE QUI N'AVAIT ELLE-MÊME AUCUN
APPELANT.** `mission/Assistance.hpp` consommait bien les quatre modules morts
d'astro_core — mais rien, dans le jeu, ne consommait `Assistance.hpp` : ses seuls
appelants étaient les oracles. Le balayage par graphe d'inclusion (piège n°85) ne
l'aurait pas vu, parce que le fichier EST inclus… par `tests/`. **Le manque n'était
pas un appelant, c'était une MISSION** : le catalogue n'avait aucun contrat qu'un
tour puisse servir. La seule cible externe était **CAT-10, un cargo NEP** —
c'est-à-dire de la poussée CONTINUE, le régime où une assistance impulsive n'a
rien à faire [GDD 6.3]. Même diagnostic que pour `flight/Descent.hpp`, qui
attendait le cislunaire.

**CAT-13 « ORBITEUR DU SYSTÈME SOLAIRE EXTERNE »**, rang Senior. Le GDD le nomme
trois fois : branche 2 « orbiteurs », branche 6 palier 2 (« le RTG ouvre le
**système solaire externe robotique** »), et la table des compétences qui place
les assistances en colonne Senior pour les « transferts complexes ». Prérequis =
un par verrou (`sondes`, `rtg`, `gravity_assist`, `nav_profonde`) ; charge utile
**400 kg** (Galileo : 118 kg d'orbiteur + 339 de sonde ; Juno : 173) ; budget
**1 460 M$**, le coût publié de Juno sur tout son cycle de vie.

**QUATRE DÉFAUTS TROUVÉS EN BRANCHANT — c'est la raison d'être de l'exercice.**

**1. LES RÉGLAGES DE FENÊTRE ÉTAIENT CEUX DE MARS, ET ILS LE DISAIENT.** Les
valeurs par défaut de `WindowParams` portent leur propre aveu en commentaire :
horizon 800 j « >= 1 période synodique **Terre-Mars** », durées explorées
150-400 j. Un transfert de Hohmann vers Jupiter dure **997 jours** : le balayage
butait sur son plafond et rendait le seul arc qu'il connaissait.

| | avant | après |
| :--- | ---: | ---: |
| Δv de trajectoire, Terre → Jupiter | **17 621 m/s** | **8 540 m/s** |
| durée de transit retenue | 400,0 j (= le plafond) | 893 j |
| masse au décollage de CAT-13 | 2 209 t (aucun lanceur) | 10,4 t |

Aucune alerte, aucun symptôme : la structure était cohérente avec elle-même —
exactement le piège n°94, un cran plus haut. **La réparation n'est pas une seconde
table mais une DÉRIVATION** : `mission_window_params_for(dep, arr)` tire les
bornes de la géométrie (Hohmann `π√(a³/µ)`, synodique par les deux périodes), et
les demi-grands axes sont lus **par vis-viva** — une première rédaction prenait le
RAYON du moment, et sur Mars (e = 0,093) la synodique estimée oscillait entre 700
et 920 jours selon la date, ce qui faisait basculer Mars hors de ses propres
bornes une fois sur deux. **Mars ne bouge pas d'un bit** : quand les bornes par
défaut contiennent déjà le Hohmann de la paire (258,9 j ∈ [150, 400]), on les rend
telles quelles, et toute la calibration martienne est préservée — un oracle le
verrouille. Au passage, `trajectory_dv_for_mission` était le **troisième** appel de
`launch_window` du jeu et le seul qui gardait encore les valeurs par défaut.

**2. L'ÉLAGAGE INTERDISAIT LE VOL DE GALILEO.** `vinf_min` — la borne de Hohmann
qui empêche l'optimiseur d'explorer des tours physiquement incapables — était
calculée, **pour chaque survol, contre la cible FINALE**. Or elle ne dit qu'une
chose : après un survol, il faut assez d'énergie pour atteindre **le corps
suivant**.

| survol de Vénus, il faut ouvrir… | |v∞| minimal |
| :--- | ---: |
| … la Terre (la vraie jambe suivante) | **2 527 m/s** |
| … Jupiter (la cible finale) | **11 451 m/s** |
| Galileo y est réellement passé à | ~5 000 m/s |

Le vol réel tombait donc dans la pénalité (5 × l'écart), et l'optimiseur, poussé à
faire l'impossible, payait l'impossibilité en DSM géante. **Le « Galileo à
18 019 m/s » de la passe précédente n'était pas un problème d'optimiseur : c'était
notre contrainte qui refusait la trajectoire que Galileo a volée.** Un élagage plus
dur que la physique n'élague pas, il ment.

**3. `astro/LocalRefine.hpp` ÉTAIT ÉCRIT, SOUS ORACLE, ET SANS APPELANT** — c'était
l'incrément annoncé. Le gradient projeté remplace la DE resserrée dans le MBH.

**4. L'OPTIMISEUR NE MINIMISAIT PAS CE QUE LA MISSION PAIE.** `Mga1Dsm::cost` est
le Δv **embarqué** (DSM + insertion), le C3 n'étant qu'une pénalité au-delà du
plafond : c'est la formulation standard, où le lanceur OFFRE le C3. Ici le vaisseau
part d'une orbite de parking et paie son injection — l'optimiseur était donc
indifférent à un C3 collé au plafond qui coûte 4 512 m/s au véhicule. Toutes les
solutions ratées ont cette signature.

**IL FALLAIT LES QUATRE.** Mesuré sur E-V-E-E-J :

| | Δv total |
| :--- | ---: |
| élagage cible finale + DE resserrée (l'état d'avant) | 20 547 m/s |
| élagage cible finale + gradient | 11 946 |
| élagage corps suivant + DE resserrée | 12 495 |
| élagage corps suivant + gradient | 5 205 |
| … + multi-départ + objectif total | **5 372** |

**ET LE RÉSULTAT N'EST PAS « UN TOUR » : C'EST CELUI DE GALILEO**, retrouvé sans
qu'on lui donne autre chose que sa séquence et des bornes de jambe.

| | modèle | Galileo (1989-1995) |
| :--- | ---: | ---: |
| C3 de départ | **16,3 km²/s²** | 15,9 (Shuttle/IUS) |
| Δv en espace profond | **3 m/s** | quelques dizaines |
| durée totale | **5,86 ans** | 6,14 |

Une DSM quasi nulle est la signature d'un VEEGA réellement raccordé : la géométrie
fait le travail, la propulsion ne fait que la mise en forme.

**ET UNE « INSTABILITÉ » QUI N'EN ÉTAIT PAS UNE — piège n°90, à nouveau.** Le même
tour, demandé à huit dates espacées de 45 jours, rendait 5 372 m/s cinq fois et
jusqu'à 10 976 les autres, alors que la fenêtre de recherche fait TROIS ANS. J'y ai
vu de la non-convergence et j'ai dépensé **trois budgets** à la combattre :
redémarrage aléatoire sur stagnation (aucun effet — en 18 dimensions un point tiré
au hasard ne tombe dans aucun bassin), multi-départs de 6 à 24, raffinage de 6 à 30
sauts. Aucun ne l'a fait disparaître, et **les mêmes dates échouaient à chaque
fois**, ce qui n'est pas le comportement d'un tirage. **La date de départ TROUVÉE a
tout dit** : les bons résultats partent tous à la même date absolue, et les échecs
sont exactement ceux dont la fenêtre de trois ans **se termine avant elle**. Ce
n'était pas l'optimiseur, **c'était une opportunité de lancement** — Galileo,
Cassini et Juno ont tous attendu leur alignement. Le modèle avait raison de
refuser, et la garde (« un tour qui ne bat pas le direct est refusé ») dit
désormais quelque chose de vrai sur le ciel.

**CE QUE J'AI ANNULÉ APRÈS L'AVOIR MESURÉ.** La jambe Terre-Terre de Galileo dure
731 j, soit la résonance 2:1 exacte ; j'ai resserré cette borne à [710, 750] en
croyant aider l'optimiseur à trouver un creux étroit. **Le tour de 2030 est passé
de 5 205 à 9 556 m/s** : la bonne solution de cette année-là n'est pas résonante
2:1, et je venais de l'interdire. Une hypothèse tirée d'UN vol réel n'est pas une
loi — les bornes encadrent, elles ne prescrivent pas.

**LE TROC, TEL QUE LE JOUEUR LE VOIT** (poste CONTRÔLE, mission CAT-13) :

| | Δv | masse au décollage | lanceur | coût | transit |
| :--- | ---: | ---: | :--- | ---: | ---: |
| DIRECT | 8 524 m/s | 10,4 t | L-C lourd | 165 M$ | 893 j |
| E-E-J (Juno) | **6 301 m/s** | **5,4 t** | L-B moyen | **125 M$** | **1 717 j** |

Deux ans et demi de vol de plus pour la moitié de la masse et 41 M$ — et ces
années ne sont pas gratuites : elles occupent l'agence, qui paie ses coûts fixes
sans encaisser le jalon [GDD 13.2].

**LES DEUX AXES DE [GDD 5.4] GARDENT ENFIN.** `gravity_assist` n'était qu'un
prérequis de contrat et **`multi_survols` — « Architectures multi-survols »,
Senior, 210 jours et 45 M$ — ne débloquait RIEN** : même défaut que les quatre
nœuds de lanceurs et les trois d'assemblage orbital. Ils gardent maintenant ce que
leur nom dit, et le refus NOMME la direction (« NON QUALIFIE : RECHERCHER
multi_survols »). Le rang reste le second verrou, distinct.

**CE QUI EST DÉCLARÉ.** La dispersion de navigation est évaluée sur l'arc DIRECT
même quand un tour est choisi (un tour a une trace multi-jambes que
`build_flight_trace` ne sait pas construire) : l'injection d'un tour étant bien
moins énergique, l'erreur va dans le sens **conservateur** [GDD 12.5]. Le calcul
d'un tour coûte 1 s (un survol) à 6 s (trois) : il ne tourne QUE sur demande
explicite du joueur, jamais dans `evaluer_plan`, qui se contente de lire le bilan.

**PREUVE** : `SPEditor` Succeeded, **4 102 oracles au vert** sur les 12 suites
(`test_reentry_perturb` 182 → **219**, `test_session` 717 → **740**), sauvegarde
**V6** (le tour figé), drapeau `-sptour[=<id>]`, et le couple `ue_tour_direct.png`
/ `ue_tour_assistance.png`.

### LE SURVOL SE VISE DANS LE PLAN-B (2026-07-31) — le dernier en-tête mort de la série

**QUATRIÈME ET DERNIER DES EN-TÊTES SORTIS PAR LE BALAYAGE DES ASSISTANCES.**
`astro/BPlane.hpp` disait lui-même à quoi il devait servir : « on ne cible pas une
orbite, on cible un point dans un plan perpendiculaire à l'asymptote ; l'ellipse
de dispersion superposée au corridor admissible EST l'interface de la sanction ».
Il n'avait aucun appelant **parce que le jeu n'avait aucun survol**. Il en a un.

**CE QU'UN SURVOL EXIGE, ET QUE PERSONNE N'EXIGEAIT.** Un transfert direct arrive
« à Jupiter » : rater de 100 000 km se corrige. Un survol est un rendez-vous de
précision — la déviation qu'il fournit dépend du périastre, et **100 km d'écart en
paramètre d'impact valent 91 km d'altitude**. Sous l'interface atmosphérique, le
véhicule ne survole plus : il rentre à 9 km/s. Le corridor est donc **borné par la
physique** (R + 122 km), pas par une règle de jeu, et la conversion b ↔ rp est
l'identité exacte de `BPlane.hpp` — un oracle vérifie l'aller-retour.

**LA CASCADE, ET SON RÉSULTAT INATTENDU.** TCM-1 annule le manque prédit ; son
exécution est imparfaite (Gates) ; une dernière correction quelques jours avant le
survol annule ce résidu ; et c'est **son** erreur d'exécution qui décide. Mesuré :

| dernière correction | son Δv | résidu au survol |
| :--- | ---: | ---: |
| E − 3 j | 255,0 m/s | 111 km |
| E − 10 j | 76,5 m/s | 113 km |
| E − 30 j | 25,5 m/s | 123 km |

**Le résidu ne dépend pas de la date choisie** — corriger plus tard coûte plus
cher exactement dans la proportion où le bras de levier raccourcit. Le délai
déclaré (10 j, la pratique de Galileo, Cassini et Juno) n'est donc **pas un
réglage caché**, et c'est ce qui rend ce modèle publiable.

**LA LOI EST CELLE DU PLAN-B, ET C'EST POUR ÇA QU'ON Y TRAVAILLE** : le manque au
but y est un vecteur à DEUX composantes (B·T, B·R), donc sa norme suit une
**Rayleigh** — pas la Maxwell (3D) qui vaut pour un Δv.

**ET LA PENTE EST BRUTALE, CE QUI EST LE FAIT PHYSIQUE.** Sur le même vol, en ne
changeant QUE l'altitude visée : **+50 km au-dessus de l'interface → P = 0,114** ;
+2 000 km → P = 1,000. Un survol rasant est une prise de risque, et le modèle la
chiffre au lieu de la décorer. La capture du jour le montre en rouge sur un tour
que l'optimiseur avait trouvé **bon marché** (6 271 m/s) : *« survol : corridor
110 km, residu 109 km apres TCM finale de 74 m/s → P 0.400 »*. Le Δv le moins cher
n'est pas la trajectoire la plus sûre — et c'est exactement l'arbitrage que
[GDD 8.5] veut faire prendre.

**ET LA MESURE SUIVANTE A MONTRÉ QUE LE DÉFAUT ÉTAIT DANS LE CATALOGUE.** En
balayant le plancher de périastre, **l'optimiseur s'y colle à chaque fois** (sept
planchers, sept fois rp = plancher) : un survol plus près dévie plus et coûte
moins. Cette borne n'était donc pas un garde-fou, **c'était LA décision** — et
elle valait 6 600 km, choisis pour « ne pas frotter l'atmosphère », soit 222 km
d'altitude et **P = 0,38**. Les vrais vols passent bien plus haut, et c'est publié :
Juno **559 km**, Galileo 960 et 303 km, Cassini 1 171 km.

| plancher exigé | Δv | corridor | P(survol) |
| :--- | ---: | ---: | ---: |
| 78 km (rase l'atmosphère) | 6 422 m/s | 18 km | **0,012** |
| 222 km (l'ancien défaut) | 6 397 | 110 km | **0,377** |
| **559 km (Juno, le nouveau défaut)** | 6 423 | 482 km | **0,999** |
| 2 000 km | 6 370 | 2 021 km | 1,000 |
| 20 000 km | 8 150 | 20 659 km | 1,000 |

**Vingt-six mètres par seconde achètent la mission.** Le plancher du catalogue est
donc désormais l'altitude du VOL RÉEL — comme les durées de jambe — et
**l'architecte peut l'élever** (`Session::alt_survol_min_km`, réglable au poste par
pas de 250 km) : plus haut = plus sûr et plus cher, la ligne le dit en toutes
lettres. Chaque clic REFAIT le tour, parce que c'est un calcul de bureau d'études.

**⚠ ET C'EST LE PLUS BAS DES SURVOLS DE LA RÉFÉRENCE, PAS CHACUN LE SIEN.** Poser à
Vénus les **16 106 km** que Galileo y a réellement volés a ÉTOUFFÉ le tour — la
déviation manque, la DSM repart à plusieurs km/s, et trois oracles sont tombés.
Ces 16 106 km étaient un **résultat** de la géométrie de 1989, pas une contrainte
de conception. Ce qu'une mission de référence démontre, c'est **jusqu'où elle est
descendue** ; le reste appartient à l'optimiseur. Deuxième fois dans la même
journée qu'une valeur tirée d'un vol réel est prise pour une loi (la première :
les bornes de jambe résonnantes).

**PREUVE** : `SPEditor` Succeeded, **4 120 oracles au vert**, `ue_tour_assistance.png`
— « survol : corridor 733 km, residu 131 km apres TCM finale de 89 m/s → **P 1.000** »
en vert, sous un sélecteur d'altitude qui affiche « vol reel ».

### LE TOUR SE DESSINE DANS LE MONDE (2026-07-31) — et il PASSE par son survol

**LA LIGNE QUE J'AVAIS ÉCRITE COMME « RESTE » A ÉTÉ FAITE DANS LA FOULÉE.** Un vol
E-E-J durait 4,8 ans, passait par la Terre à 2,1 ans et s'affichait avec l'arc
direct de 893 jours : **une trajectoire que personne ne volait**, dessinée « à sa
position réelle » [GDD 8.3, 17.3]. La trace est désormais celle du tour.

**RIEN N'EST RE-RÉSOLU POUR DESSINER.** `mga1dsm_evaluate` calculait déjà chaque
morceau parcouru (dérive vers la manœuvre profonde, puis arc vers le corps
suivant) et **les jetait**. Il les publie maintenant — mais **seulement sur
demande** : l'optimiseur appelle cette fonction un à deux MILLIONS de fois par
tour, y allouer deux vecteurs par jambe coûterait plus cher que toute la physique.
Seule l'évaluation du point retenu porte le drapeau.

**LES MORCEAUX SONT UN FAIT DU VOL, DONC ILS SONT SAUVEGARDÉS (V7).** Un tour se
recalcule différemment à chaque date de balayage : recalculer au chargement ferait
voler un vol déjà parti sur une AUTRE trajectoire que la sienne. Même doctrine que
`tof_days`, que le β de croisière et que le tirage de navigation.

**LE CONTRÔLE QUI PROUVE QUE C'EST BIEN UN SURVOL** : au nœud de survol, le
vaisseau est à **0 km de la Terre**, 2,10 ans après le départ. Ce n'est pas une
tolérance de dessin — dans le modèle à coniques raccordées, le survol A LIEU à la
position du corps, et l'oracle le vérifie contre l'éphéméride.

**ET LA NAVIGATION A CHANGÉ DE CIBLE, PARCE QUE LA PHYSIQUE L'EXIGE.** La
dispersion se propageait de l'injection jusqu'à l'ARRIVÉE. Sur un tour, cela
reviendrait à corriger « pour Jupiter dans cinq ans » d'un seul coup, avec une
matrice de transition sur cinq ans qui n'a aucune validité. Elle vise désormais la
**première visée** — la manœuvre profonde de la première jambe — et le corridor
cesse de croître au-delà, ce qu'une correction fait par définition. **Pour un
transfert direct, la première visée EST l'arrivée** : `t_nav_fin_days` vaut alors
exactement la date d'arrivée, et rien ne bouge (740 oracles inchangés).

**ET UN TOUR A SA PROPRE FENÊTRE, QUI FAIT FOI.** Le gate de lancement testait la
fenêtre du transfert DIRECT, qui ne dit rien d'un tour : l'optimiseur a trouvé une
date de départ précise, souvent des mois plus loin, et **c'est elle
l'opportunité**. Partir un autre jour, c'est voler une trajectoire que personne n'a
calculée — le défaut du 2026-08-01, qu'on ne repaiera pas. Le refus chiffre
l'attente (« depart du tour E-E-J dans N jours (son opportunite) »), et une
opportunité passée demande un recalcul au lieu de mentir.

**PREUVE** : `SPEditor` Succeeded, **4 111 oracles au vert**, sauvegarde **V7**, et
`ue_tour_trace_monde.png` — l'arc du Δv-EGA qui monte au-delà de l'orbite de Mars,
sa manœuvre profonde à l'aphélie et son survol de retour, dessinés dans le monde à
l'échelle 1:1.

### DEUX RÉGLAGES POUR LA MÊME FENÊTRE (2026-08-01) — le gate mentait au vol

**TROIS ORACLES SONT TOMBÉS, ET J'AI D'ABORD CRU QUE C'ÉTAIT MOI.** `trace : un
transfert EN FENETRE ne plonge pas vers le Soleil`, `nav : l injection martienne
coute 3-4,5 km/s`, `nav : effet Oberth`. Deux hypothèses fausses avant de mesurer,
et c'est la mesure qui a tout donné :

1. **« C'est ma passe. »** Réfuté en une minute par `git stash --include-untracked` :
   sur l'arbre PROPRE les trois échouent avec des valeurs identiques. Le stash est
   le bissecteur le moins cher du dépôt, et j'aurais dû commencer par là.
2. **« C'est la bascule de date. »** Réfuté par la mesure : la fenêtre Terre-Mars
   est identique les 30 et 31 juillet (v∞ départ 3 035 puis 3 034 m/s).

**LA VRAIE CAUSE, TROUVÉE EN IMPRIMANT LES DATES.** Un `printf` de l'attente et des
époques a montré `attente 0,0 j | open = 1 | next_open_days = 50,6` — **une
contradiction visible dans la structure elle-même**. La fenêtre se déclarait
ouverte, et annonçait dans le même souffle que la prochaine ouverture était dans
50 jours.

**IL Y AVAIT DEUX RÉGLAGES DE FENÊTRE POUR LE MÊME VOL.** Le gate de lancement
appelait `launch_window` avec les paramètres par défaut (`slop_days` = **60**),
tandis que `transfer_tof_days` resserrait à un pas de balayage (**10 j**).
Conséquence exacte :

- le **gate** ouvrait parce qu'un bon transfert existait quelque part dans les
  60 jours ;
- la **trajectoire** partait le jour même, avec la durée de transit du meilleur
  transfert des 10 jours — c'est-à-dire un mauvais ;
- et le vol emportait donc la durée d'un départ qu'il ne faisait pas.

| | avant | après |
| :--- | ---: | ---: |
| attente avant départ | 0,0 j | **50,6 j** |
| périhélie de l'arc | **0,862 UA** (sous l'orbite terrestre) | **0,963 UA** |
| injection depuis LEO | **5 827 m/s** | **4 107 m/s** |
| amplification d'Oberth | ×1,70 | **×2,65** |

Un transfert martien réel coûte ~3,6 km/s d'injection : le modèle en demandait
**5,8**, et personne ne le voyait parce que les deux couches étaient chacune
cohérente avec elle-même.

**LA RÉPARATION EST UNE SUPPRESSION.** `mission_window_params()` est désormais le
**seul** réglage de fenêtre du jeu ; le gate, la durée de transit, l'attente et
l'avance du monde côté UE l'utilisent tous. `WindowResult` publie en outre
`local_dep_tdb` / `local_wait_days` — **la date de départ du meilleur transfert
local était calculée puis jetée**, ce qui rendait le désaccord indétectable depuis
l'extérieur.

**ET L'INVARIANT QUI MANQUAIT EST MAINTENANT UN ORACLE**, indépendant de la date :
*attendre `transfer_wait_days` DOIT rendre le gate ouvert*, et la durée de transit
doit être celle de la date de **départ**. Il est vrai tous les jours de l'année —
c'est tout l'intérêt, puisque l'ancien défaut ne mordait qu'à certaines dates.

**PREUVE** : `SPEditor` Succeeded, **4 038 oracles au vert** sur les 12 suites
(`test_session` 715 → **717**).

### LE SCORE AVAIT UN CRITÈRE SUR TROIS (2026-08-02) — [GDD 3.3]

**« SCORE CUMULÉ À PONDÉRATION ÉGALE DE TROIS CRITÈRES »**, dit le GDD, et il les
nomme : réussite de mission, **respect budgétaire**, **gestion de crise**. Le code
comptait `+40 par réussite, −10 par échec` sur les **compteurs de l'agence**.
Conséquence : dépenser deux fois son enveloppe ne coûtait rien à la carrière, et
perdre un équipage par impréparation valait exactement le même malus qu'un
satellite raté. Les deux tiers du barème n'étaient pas approximés — ils étaient
**absents**.

**UN COMPTEUR NE POUVAIT PAS FAIRE MIEUX**, et c'est le vrai diagnostic : il ne
sait ni ce qu'une mission a coûté, ni ce qu'elle a traversé. Le score se juge donc
désormais **au débrief, mission par mission**, seul endroit où les trois faits
coexistent. Chaque critère rend une note dans [−1, +1] ; la somme divisée par
trois donne la fraction du barème — c'est *exactement* ce que « pondération
égale » veut dire, et un oracle le vérifie en mesurant qu'un point de budget
déplace le total autant qu'un point de crise.

| Critère | Ce qui le calcule | Fait déjà présent dans le modèle |
| :--- | :--- | :--- |
| Réussite | l'issue du vol | `flight_success` |
| **Budget** | marge relative laissée sur l'enveloppe ; le quart vaut la note pleine | coût **engagé au feu vert** (V10) contre `contract.terms.budget_musd` |
| **Crise** | note pleine, moins un demi-point par cran de gravité, plus la part des pannes **réparées** | `worst_severity` + deux compteurs incrémentés en vol |

**LE DEMI-PALIER DE [10.3] EXISTE ENFIN.** `brilliant_recovery` — « rétrogradation
possible d'un demi-palier selon les circonstances atténuantes », le **seul**
modificateur adoucissant du barème — était sauvegardé, relu… et **posé par
personne**. Il se pose maintenant sur un fait : *toutes* les pannes survenues en
vol ont été menées à réparation. Réparer demande des technos qualifiées et du
temps de vol : c'est un acte, pas une case. Et [GDD 3.3] dit précisément que ce
demi-palier « alimente directement le critère gestion de crise ».

**LA CALIBRATION NE BOUGE PAS, ET C'EST VÉRIFIÉ** : une mission nominale — réussie,
dans son enveloppe avec de la marge, sans anomalie — vaut **toujours 40 points**,
donc les seuils de promotion gardent le sens qu'ils avaient. Ce qui change, c'est
tout le reste : la même mission réussie **à +60 % de budget** ne rapporte plus que
**13,3**, et une mission perdue avec une anomalie critique coûte **−13,3**.
[Annexe E] diffère le *barème* (`POINTS_PAR_MISSION`, choisi pour ne rien
déplacer) ; [3.3] fixe la *structure*, qui n'est pas différée.

**ET UN ÉCHEC RÉEL S'EST AFFICHÉ EN VERT.** Mon relevé des douze suites comptait
les lignes `ECHEC` — or `test_ares_modules` écrit `[FAIL]`. Le compteur affichait
`188 → 187 OK, 0 échec` : un oracle avait **disparu** sans qu'aucune alerte ne
sorte, et il fallait remarquer le −1 pour aller voir. C'était un vrai échec, et il
était juste : l'oracle affirmait « 3 réussites → Junior », la règle que cette passe
retire. Piège **n°99** — un tableau de bord qui ne connaît pas tous les formats de
verdict rend du vert sur du rouge.

**PREUVE** : `SPEditor` Succeeded, **4 221 oracles au vert** (`test_session`
797 → **809**, `test_ares_modules` 188 → **189**), sauvegarde **V10**, et
`ue_score_promotion.png` — « SCORE DE PROMOTION 0 / 100 vers Architecte Junior »
au poste AGENCE, avec le détail par critère du dernier vol sous la ligne.

### LA PASSATION (2026-08-02) — [GDD 3.4, 3.5, décisions 6 et 7]

**LE PERSONNAGE VIEILLISSAIT, POUVAIT MOURIR, ET RIEN NE SE PASSAIT.** L'âge
biologique avançait en temps PROPRE (écart relativiste compris) depuis le
2026-07-29 ; `natural_death_due()` savait dire qu'une vie s'achève ;
`career::Succession` était écrite, commentée, sous oracle. **Aucun des trois
n'avait de lecteur.** Un Architecte de 120 ans gardait son poste, et un
Architecte mort d'un cancer radio-induit — que le modèle déclare déjà comme une
mort *naturelle* qui « OUVRE une passation » — le gardait aussi. La portée
multi-générationnelle que [GDD 3.5] exige pour atteindre la fin de la branche 6
n'existait donc pas du tout.

**LA FIN DE VIE SE CONSTATE LÀ OÙ L'ÂGE AVANCE**, dans `AresLayer::avancer`, une
seule fois (drapeau idempotent quelle que soit la cadence). Et **ce n'est pas une
fin de partie** : [GDD 3.4] distingue trois issues, celle-ci ouvre une passation.
La chaîne des modales le dit dans son ordre même — `GameOver` d'abord, `Passation`
ensuite : « une mort opérationnelle reste un Game Over, la passation ne l'annule
jamais ».

| | transmis | motif du GDD |
| :--- | :--- | :--- |
| **Rang** | **oui** | décision 6 : propriété du POSTE, pas de la personne |
| État programmatique (arbre, finances, missions, station) | **oui, intégralement** | il appartient à ARES |
| Carnet | **oui** | continuité personnelle et pédagogique [15.4] |
| Confiance ARES | non — **remise à 70** | décision 7 : la crédibilité ne se lègue pas |
| Score | non | il est personnel |
| **Dose reçue** | non | c'est un CORPS neuf [6.6] — ce qui **rouvre** les vols terminaux que le prédécesseur ne pouvait plus faire [9.2] |
| Écart d'horloge bord/Terre | non | il mesure ce que ce corps a vécu de moins [6.7.5] |

La meilleure façon de tenir la ligne « état programmatique : intégralement » est
de **ne rien écrire** : `passer_la_main` ne touche que le personnel, et l'oracle
vérifie que trésorerie et arbre sont au bit près ce qu'ils étaient.

**ET SI LE DÉFUNT ÉTAIT EN VOL** [GDD 9.2, 9.3] : la mission survit à son
responsable scientifique — l'équipage est toujours à bord. Ce qui cesse, c'est
l'ABSENCE : le successeur est à son poste, donc la protection financière de [9.3]
se lève. Le vol n'est pas touché ; le perdre pour une raison qui ne le concerne
pas serait une punition gratuite.

**DEUX MESURES ONT CORRIGÉ L'ORACLE, PAS LE MODÈLE.** (1) `avancer_temps` déplace
le CALENDRIER ; la couche ARES ne rattrape qu'au tick suivant (`assurer` appelle
`avancer`) — un oracle qui fait couler le temps doit le dire, le jeu le faisant à
chaque frame. (2) Surtout : **on ne peut pas laisser couler cinquante-trois ans**.
Une agence qui n'entreprend rien fait **faillite en six ans** — mesuré ici même,
c'est la pression d'inactivité de [GDD 13.2] qui fonctionne, et c'est la
TROISIÈME issue de [3.4]. L'oracle pose donc l'âge, qui est un fait du personnage,
et laisse le temps le constater. Piège **n°98**.

**PREUVE** : `SPEditor` Succeeded, **4 208 oracles au vert** (`test_session`
764 → **797**), sauvegarde **V9** (sans elle, quitter pendant la modale
ressusciterait le défunt), drapeau `-sppassation`, et deux images :
`ue_passation.png` — « RANG CONSERVE : Architecte Principal . CONFIANCE REMISE
A 70 . CARNET TRANSMIS . ETAT PROGRAMMATIQUE INTACT », génération 2 — et
`ue_architecte_age.png` — « ARCHITECTE 32 ans . generation 1 . 53 ans de fonction
devant lui », au poste AGENCE, pour qu'une fin de fonction soit une échéance et
non une surprise.

### LE VÉHICULE CONÇU A UNE FORME (2026-08-01) — [GDD 12.2, 17.2, 17.4]

**TROIS LIGNES DU GDD DEMANDAIENT LA MÊME CHOSE, ET RIEN NE LA PORTAIT.**
« L'éditeur en coupe fournit **la géométrie du véhicule**, réutilisée directement
au rendu » [12.2] ; « un véhicule assemblé par le joueur doit être **RENDU**, pas
modélisé » [17.2] ; « de la vue système au **plan vaisseau (mètres)** par simple
zoom, car le vaisseau **est déjà dans la scène** » [17.4]. Or **aucune pièce du
catalogue ne portait de dimension** : le vaisseau du joueur était un point émissif
de taille écran constante, à dix mètres comme à dix unités astronomiques, et
l'atelier « en coupe » était un tableau de masses sans dessin.

**AUCUNE COTE N'A ÉTÉ POSÉE : TOUT EST DÉRIVÉ DE CE QUE LE CATALOGUE PORTE DÉJÀ.**
`vehicle/Geometry.hpp` ne fait qu'exploiter des données qui existaient :

| Pièce | D'où sort sa cote | Recoupement |
| :--- | :--- | ---: |
| Ajutage (moteur allumable au sol) | **Identité exacte de la poussée** : A_sortie = F_vide (1 − Isp_sol/Isp_vide) / p0. **Zéro paramètre.** | RS-25 **2,34 m** (2,30 publié) · F-1 **3,63** (3,72) |
| Ajutage (moteur à vide) | A = F · ε/(C_F·p_c), ε et p_c pris **par classe** à la littérature | RL10C-1 **1,46 m** (1,45 publié) |
| Longueur de moteur | cloche à 80 % dans un cône à 15°, plus une tête de **cinq rayons de col** | RS-25 **3,96 m** (4,24) · F-1 **6,15** (5,79) · RL10 **2,30** (2,22) |
| Réservoir | volume = ergols / **densité du couple** (déjà au catalogue) + ullage ; fonds bombés | — |
| Capsule | diamètre = **section de rentrée** que `flight/Reentry.hpp` exige déjà | Apollo **3,91** · Orion **5,00** · Dragon **3,70** · MSL **4,50** |
| Diamètre de pile | le plus contraignant, élargi tant que l'élancement dépasse 12 (Saturn V 11, Falcon 9 19, Centaur 4) | — |

**LA SEULE APPROXIMATION EST MESURÉE, PAS ESPÉRÉE** [GDD 6.8] : là où les deux
routes s'appliquent (les cinq moteurs allumables au sol), l'oracle les compare et
publie l'écart — **±29 % sur le diamètre**. C'est la borne déclarée de tout ce que
la route exacte ne couvre pas. Et elle ne coûte rien au jeu, parce que **cette
géométrie ne nourrit aucune physique** : c'est un produit d'AFFICHAGE, la frontière
est tenue par un oracle (`la coupe ne déplace aucune masse`), et c'est elle qui
autorise l'approximation.

**LE VAISSEAU QUI VOLE EST FIGÉ AU FEU VERT (sauvegarde V8).** La conception vit au
poste CONCEPTION et le joueur continue de la retoucher pendant qu'une mission est en
route : lire la conception COURANTE ferait **changer de forme un vaisseau déjà
parti**. `Mission::vaisseau_etages` porte donc la pile, ses **ergols étage par
étage** (publiés par `assess_multistage`, une seule vérité) et sa capsule — même
doctrine que `tof_days`, `tour_arcs` et le β de croisière. Oracle : retoucher
l'atelier après le départ ne déplace pas la coque d'un millimètre.

**DEUX CONSOMMATEURS, UNE SEULE COUPE.** Le pont publie `hull_vol` (le vol, figé) et
`hull_design` (l'atelier, qui bouge à chaque clic). Le monde 3D en fait des cylindres
et des cônes à l'échelle réelle — **1 m = 100 u, et les primitives d'UE font 100 u :
l'échelle d'un composant EST sa cote en mètres** — avec le LOD à deux crans de
Novellus (marqueur de loin, coque dès que la taille apparente dépasse 3e-3). Le poste
CONCEPTION en fait le **dessin en coupe** qui manquait à [12.2], à la même échelle sur
les deux axes (un profil qui étirerait la longueur mentirait sur l'élancement).

**L'AXE DU VAISSEAU EST SA VITESSE, PUBLIÉE PAR LE MODÈLE** — jamais dérivée de deux
frames : à « mois/s » une frame avance de douze heures. Approximation déclarée : c'est
l'attitude de POUSSÉE ; un engin réel en croisière pointe son antenne, ce que rien ne
modélise.

**CE QUE LA VÉRIFICATION A COÛTÉ, ET CE QU'ELLE A APPRIS.** L'orientation de la
cloche a résisté à **trois captures** : à 60 pixels, une pointe et une sortie ne se
distinguent pas, et j'ai « corrigé » un défaut inexistant avant de vérifier la
correction sur une image tout aussi ambiguë. Deux mesures ont tranché — colorier les
rôles, puis **dessiner l'ajutage à 6 m** (piège **n°96**). Au passage, `-spdist=0.4`
ne faisait rien du tout : `FParse::Value` sur un flottant **dépend de la locale**, et
« 0.4 » vaut 0,0 en français (piège **n°97**) — d'où `-spvaisseau=<mètres>`, en
entiers.

**PREUVE** : `SPEditor` Succeeded, **4 175 oracles au vert** sur les 12 suites
(`test_contenu_gdd` 843 → **892**, `test_session` 758 → **764**), sauvegarde **V8**,
nouveau drapeau `-spvaisseau[=<mètres>]`, et deux images :
`ue_vaisseau_concu_monde.png` — la coque de 18,86 m × 1,69 m éclairée par le Soleil,
à sa position réelle dans le monde 1:1 — et `ue_coupe_conception.png` — « COUPE
20,6 m × 2,05 m (élancement 10,1) » avec son profil, dans l'atelier.

### LA RENTRÉE EST UN VERROU (2026-07-30) — le plus gros modèle sans consommateur

**BALAYAGE PAR GRAPHE D'INCLUSION (piège n°85), PAS PAR JUGEMENT.** Le §7 étant
soldé, j'ai relancé la boucle « quel en-tête du cœur n'est inclus nulle part hors
`tests/` ? ». Dix réponses, dont sept déjà couvertes par une décision écrite (la
branche numérique) ou anecdotiques. **Une seule était grosse** :
`flight/Reentry.hpp` — 120 oracles, trois modèles (formes closes d'Allen–Eggers,
corridor, intégration planaire RK4) — **aucun appelant**. Pendant ce temps :

- `CapsulePart` traînait **cinq champs** — Cd hypersonique, section, rayon de nez,
  finesse, limite en g — qui n'existent QUE pour ce module ;
- l'arbre technologique **vendait trois nœuds** de rentrée (`rentree_capsule`,
  `reutilisation`, `rentree_lourde`) ;
- et l'équipage revenait de Mars sans que rien ne vérifie qu'il survive.

**RIEN N'EST DÉCLARÉ, ET DEUX CONTRÔLES PUBLIÉS LE PROUVENT.** La vitesse
d'interface sort de l'énergie, pas d'une table : `v = √(v∞² + 2µ/r)`, où `v∞` est
l'excès hyperbolique que la fenêtre de tir **calculait déjà**
(`astro::WindowResult::vinf_arr`).

| | Modèle (dérivé) | Publié |
| :--- | ---: | ---: |
| Retour lunaire (v∞ ≈ 0, quasi parabolique) | **11 074 m/s** | Apollo 11 : **11 030** |
| Retour d'orbite à 400 km (vis-viva) | **7 912 m/s** | rentrées LEO : **~7 800** |

**ET LA TENUE DU BOUCLIER NON PLUS.** Le flux admissible d'un ablatif n'est
presque jamais public ; ce qui l'est toujours, c'est **l'entrée qu'il a survécue**.
La capacité est donc calculée comme le pic de flux de sa propre entrée de
qualification, à la pente la plus raide que sa limite en g autorise — le point le
plus chaud du domaine certifié. Seules des données publiées entrent : masse,
géométrie, g admissible, vitesse d'interface qualifiée (Apollo 11 à 11,03 km/s,
Artemis I à 10,95, Soyouz et Dragon en LEO, MSL à 5,845 sur Mars). **Conséquence
structurelle** : une capsule qui refait son entrée de qualification passe TOUJOURS
— le modèle ne *peut pas* déclarer Apollo incapable du retour lunaire, et un oracle
le verrouille pour les cinq pièces.

**LE VERDICT REPRODUIT L'HISTOIRE** :

| | LEO 400 km | Retour lunaire |
| :--- | :--- | :--- |
| APOLLO-CM | ✔ 4,8 g | **✔ 11,0 g** |
| ORION-CM | ✔ 4,9 g | **✔ 10,7 g** |
| SOYUZ-SA | ✔ 3,3 g | ✘ *flux à 222 % du tenable* |
| DRAGON-2 | ✔ 3,3 g | ✘ |

Les deux véhicules qui ont fait les deux les font ; les deux qui n'ont jamais
dépassé l'orbite basse ne les font pas. **Aucune capsule volée ne rentre d'un
retour interplanétaire rapide**, et le modèle le dit au lieu de l'inventer.

**TROIS DÉFAUTS TROUVÉS EN BRANCHANT — c'est la raison d'être de l'exercice.**

1. **LE CORRIDOR ÉTAIT FERMÉ POUR LES CINQ CAPSULES, À TOUTES LES VITESSES.** Y
   compris Apollo sur le retour lunaire qu'il a réellement volé. Pas un bug :
   `flight::entry_corridor` est la forme close d'Allen–Eggers, **balistique**, et à
   −6,5° et 11 km/s elle annonce ~35 g là où Apollo en a mesuré 6,5. L'écart, c'est
   la **portance** — Apollo entrait à L/D = 0,3. *Aucune capsule ne revient de la
   Lune en balistique, et la table le disait.* La réponse n'était pas de corriger
   la forme close mais d'utiliser `integrate_entry`, qui porte la portance dans ses
   équations et détecte le skip-out lui-même — **le troisième modèle inutilisé du
   fichier**. Le corridor se bissecte maintenant sur l'intégration.
2. **LES DEUX BORNES NE MESURENT PAS LA MÊME CHOSE.** Ma première bissection
   testait la borne RAIDE sur « survit », qui incluait l'absence de ricochet. Or la
   pente la plus rasante ricoche **par définition** : l'amorçage échouait toujours,
   et le prédicat n'était pas monotone (un ricochet décélère PEU, donc « ne dépasse
   pas le g » y est vrai). Séparées : la borne raide tient les *limites*, la borne
   rasante ne *ricoche* pas.
3. **LE REFUS SE CHIFFRAIT AU MAUVAIS ENDROIT.** Il mesurait la marge à une pente
   quasi horizontale, où l'engin ressort sans chauffer : la capture affichait
   « flux à **1 %** du tenable » sur une rentrée **refusée**. On mesure désormais à
   la pente la plus rasante *qui dissipe* — la trajectoire la plus froide qu'on
   pourrait réellement voler : **222 %**.

**CE QUE JE N'AI PAS INVENTÉ, ET POURQUOI.** À la masse de qualification exacte, un
kilo de plus ferme le corridor. La tentation était d'appliquer un facteur de marge
sur le flux ; **il n'en existe pas de publié** — le margining réel de TPS est une
somme quadratique sur l'ÉPAISSEUR (procédés Orion/MSL/HEEET), pas un multiplicateur
de flux. Je l'ai cherché, je ne l'ai pas trouvé, je ne l'ai pas fabriqué : la marge
est **affichée** (`flux à N % du tenable`) au lieu d'être maquillée. Et le corridor
lunaire mesuré fait **0,22°** — la lame de couteau d'Apollo est une mesure, pas une
dureté de jeu.

**ET UNE LIMITE QUI CHANGE LA LECTURE DU CHIFFRE.** Sutton–Graves ne donne que le
flux **convectif**. Au retour lunaire le radiatif est du même ordre : le pic total
d'Apollo était de ~4-5 MW/m², le modèle en annonce 2,07. **Le chiffre affiché vaut
environ la moitié du flux réel à 11 km/s.** Ce qui sauve le verdict : la capacité
est dérivée avec la MÊME formule, donc le jugement est un *rapport* entre deux flux
convectifs — juste là où l'absolu ne l'est pas. La conséquence à connaître est
ailleurs : le radiatif croît bien plus vite que le convectif, donc le modèle est
**optimiste aux vitesses très élevées**. C'est le seul endroit où l'erreur ne va pas
dans le bon sens, et il est déclaré dans l'en-tête.

**PREUVE** : `SPEditor` Succeeded, **4 007 oracles au vert** sur les 12 suites
(`test_reentry_perturb` 120 → **153**), nouveau drapeau `-sprentree[=<id>]`, et
`ue_rentree_verrou.png` — « rentree refusee : flux a 222 % du tenable ; capable :
APOLLO-CM [9.2] » en rouge au poste CONCEPTION. Le pas d'intégration est vérifié
convergé (écart 8,5e-07 entre dt = 0,25 s et dt = 0,05 s).

### L'AUDIT DES POSTES (2026-07-29) — ce qu'une capture cachait depuis le début

**L'AUDIT PROMIS, ET IL A TROUVÉ.** L'invariant de `SPHud.h` (« le HUD ne calcule
RIEN ») tient : les seules opérations dans les postes sont des conversions
d'unités, et je n'ai pas inventé de défaut pour justifier la passe. Mais en
**regardant** la capture du poste CONTRÔLE, une chaîne de trois défauts est
apparue, chacun masquant le suivant.

**A. UNE TABLE CACHÉE DANS UNE BOUCLE.** Les termes physiques du contrat
(masse, budget, délai, P(succès) exigée) vivaient dans une boucle interne à
`seed_catalogue`. Toute mission construite **hors catalogue** naissait donc avec
des termes NULS — et le harnais de capture fabrique la sienne à la main. Chaque
image en vol affichait « BUDGET CONTRAT 0 M EUR », « CALENDRIER 0 / 0 mois »,
« P(SUCCES) 0,0 % » et un VERROU rouge. La doc classait ça en « réserve
d'affichage » ; c'était en réalité **un bilan de viabilité invérifiable par
capture**, et *une alarme fausse dans chaque image est pire qu'une alarme
absente : elle apprend à ne plus les lire*. `contract_terms_for_family` vit
désormais dans `MissionLoop.hpp`, à côté des autres tables par famille, avec un
seul appelant possible.

**B. « PAS ENCORE ÉVALUÉ » N'EST PAS « RATÉ SUR QUATRE AXES ».** Une fois les
termes réparés, le poste montrait toujours des zéros. `Assessment` naît avec ses
quatre `fits_*` à false ; le poste les affichait tels quels. Conséquence pour un
vrai joueur, pas seulement pour une capture : **toute mission fraîchement
acceptée** — donc avant tout passage au poste CONCEPTION, ce qui est l'ordre
normal de la boucle [GDD 4.1] — s'annonçait comme une catastrophe sur les quatre
axes. `MissionPlan::evaluated` existait exactement pour ça et **personne ne le
lisait** : même famille que les modèles sans consommateur.

**C. `finalize` DÉTRUISAIT LE DIAGNOSTIC — un piège n°42 en plein cœur du
modèle.** `assess` s'arrête net dès qu'aucun lanceur ne soulève la masse, et il
DIT pourquoi : « AUCUN LANCEUR NE SOULEVE CETTE MASSE ». `finalize` faisait
ensuite `why.clear()` et reconstruisait la chaîne depuis les quatre `fits_*`,
remplaçant **une cause précise par une liste de symptômes** — « MASSE BUDGET
CALENDRIER RISQUE » — dont **trois quarts n'avaient jamais été calculés**. Coût,
calendrier et fiabilité valaient zéro parce que l'étude ne les avait pas
atteints, et le poste les peignait en rouge comme des échecs. *Un refus doit
nommer ce qui manque, pas énumérer tout ce qu'on ignore.*

**CE QUE LA CAPTURE MONTRE MAINTENANT**, et c'est enfin exploitable :
`MASSE AU DECOLLAGE 193 235 kg` · *« coût, calendrier et fiabilité NON ÉVALUÉS :
l'étude s'arrête à la masse »* · `VERROU : AUCUN LANCEUR NE SOULEVE CETTE MASSE`.
Les trois lignes libérées rendent au passage visibles les vivres et les boucles
de recyclage.

**ET LE CHIFFRE QUE TOUT CELA RÉVÈLE** : un aller-retour martien habité pèse
**193 t au décollage** — consommables et blindage compris — et **aucun lanceur du
catalogue ne le soulève**. C'est physiquement juste (un vol martien réel demande
un assemblage en orbite, pas un lancement unique), et c'est désormais **dit** au
lieu d'être noyé. À reprendre avec l'assemblage orbital, pas avec un lanceur
imaginaire.

**PREUVE** : build `SPEditor` Succeeded, **3 514 oracles au vert** (+13), et
`ue_horloges_poste_controle.png` recapturée.

### LE CATALOGUE ÉTAIT INACHEVABLE À 55 % (2026-07-29)

**LA QUESTION EXACTE, POSÉE À TOUS LES CONTRATS.** Le « 193 t, aucun lanceur »
de la section précédente n'était pas une curiosité : c'était la pointe d'un
défaut structurel. Mesure de départ, **arbre entier qualifié TRL 9 et rang
Directeur** — donc la limite PHYSIQUE, pas un manque de progression :

| | contrats | verdict |
|---|---|---|
| CAT-01, 02, 03, 05 | 4 | viables |
| CAT-04, 06, 08, 09, 10 | **5** | **bloqués net par le plafond des lanceurs** |
| CAT-11 | 1 | Δv hors de portée |

**Six contrats sur onze étaient inachevables quoi que fasse le joueur**, et trois
d'entre eux avaient une charge utile **nue** au-dessus du plafond — aucune
ingénierie ne pouvait les sauver.

**DEUX CAUSES, TOUTES DEUX « NOMMÉ MAIS NON CONNECTÉ ».**
1. **`lanceur_super_lourd` était un nœud d'arbre, prérequis de CAT-09, qui ne
   débloquait AUCUN lanceur.** Le catalogue s'arrêtait à 8,3 t. *Un prérequis qui
   ne débloque rien est un prérequis qui ment.*
2. **Les quatre nœuds « lanceur » de la branche 1 ne gardaient RIEN.** `launchers()`
   était une liste plate sans aucun filtre : le joueur disposait de toute la gamme
   dès la première mission, et « Accès à l'orbite » était une branche décorative.

**LES CORRECTIONS, ANCRÉES SUR DES LIGNÉES RÉELLES** [GDD 12.1 : « pièces réelles
ou extrapolées de lignées réelles, jamais génériques »] :
- **`L-D super-lourd`, 130 t, 1 400 M$, R = 0,970** — Saturn V (140 t) et SLS
  Block 2 (130 t). Fiabilité **plus basse** que le lourd, et c'est le fait réel :
  un lanceur géant vole peu, donc il démontre peu. *On n'achète pas de la
  fiabilité en achetant de la taille.*
- **`L-C lourd` : 8,3 t → 22,8 t (Falcon 9 Block 5)**. Un « lourd » à 8,3 t n'a
  aucune lignée réelle — c'est un moyen déguisé, et cette sous-évaluation créait
  un **trou de 15,7×** entre 8,3 t et 130 t. Conséquence mesurée : CAT-04, qui
  dépassait le plafond de **356 kg (4 %)**, devait acheter un Saturn V à
  1 457 M$ pour un contrat à 240 M$. Prix inchangé à 95 M$ — conservateur, un
  Falcon 9 réel se vend ~67 M$.
- **Filtrage par l'arbre**, avec le même prédicat que partout (TRL ≥ 7). Et le
  refus **distingue deux verdicts** que le joueur ne doit pas confondre :
  « LANCEUR NON QUALIFIÉ : RECHERCHER *x* » est une **direction**, « AUCUN LANCEUR
  NE SOULÈVE CETTE MASSE » est une **impasse**. Une seule des deux se résout en
  cherchant (piège n°42).
- **CAT-09 : budget 1 200 → 3 000 M$.** Il ne payait même pas son lanceur
  (1 400 M$), ce qui **violait l'invariant déclaré de sa propre table** (« calé
  pour qu'un plan raisonnable au rang requis soit VIABLE »). 3 000 M$ ≈ 1,9× le
  coût nu mesuré, et reste très en dessous du réel — Artemis III : ~4 100 M$ pour
  **un** lancement.

**RÉSULTAT MESURÉ : 9 contrats sur 11 réalisables en masse** (contre 5). Les deux
restants le sont pour des raisons **physiques**, vérifiées en faisant varier le
nombre d'étages — une décision du joueur que ma première mesure avait oubliée :

| | 2 étages | 3 étages | 4 étages |
|---|---:|---:|---:|
| CAT-10 (cargo NEP) | 181 t | 158 t | **152 t** — jamais sous 130 t |
| CAT-11 (relativiste) | infaisable | infaisable | **346 495 tonnes** |

CAT-10 demanderait un **assemblage en orbite** — *fait le jour même, voir la
section suivante : le GDD le nommait bien, au tableau de la branche 1, et j'avais
conclu à une absence sur une recherche incomplète*. CAT-11 à 346 000 t, c'est Tsiolkovsky appliqué à 30 km/s en
chimique : **c'est précisément pourquoi [GDD 19.3] réserve ce régime à
l'antimatière**, et la cohérence avec la passe précédente est totale.

**ET L'ARBITRAGE MASSE / PROTECTION / MISSION MORD ENFIN** [GDD 6.6]. Le GDD le
nommait depuis toujours ; il ne coûtait rien tant qu'aucun plafond n'existait.
Mesuré sur l'aller-retour martien habité :

> **121 t sans blindage ajouté → 182 t avec 10 g/cm², contre un plafond de 130 t.**

**La seule décision de blindage fait basculer une architecture lançable en
architecture qui ne l'est pas.** C'est exactement pourquoi les architectures
martiennes réelles assemblent en orbite (DRA 5.0 : ~850 t, 7 à 9 lancements) —
le modèle **redécouvre** la contrainte au lieu de la contourner. La capture du
poste CONTRÔLE montre donc toujours un verrou rouge : il est désormais **vrai**,
et il nomme sa cause.

**PREUVE** : build `SPEditor` Succeeded, **3 525 oracles au vert** (+11), dont un
**audit exhaustif du catalogue** qui épingle pour de bon : bijection lanceur ↔
nœud d'arbre, la branche 1 garde vraiment, aucun budget sous le prix de sa fusée,
et **exactement deux** contrats hors de portée d'un lancement unique — une
régression sur l'un de ces nombres devient impossible à ne pas voir.

### L'ASSEMBLAGE EN ORBITE (2026-07-29) — `mission/Assemblage.hpp`

**RIEN À INVENTER : LE GDD LE NOMMAIT DÉJÀ.** Branche 1, tableau du ch. 5.2 —
« transfert de propergol orbital, **rendez-vous automatisé robuste**, cadence
élevée » — et l'arbre portait les trois nœuds (`rdv_automatise`,
`transfert_ergols`, `robotique_orbitale`). **Aucun ne débloquait quoi que ce
soit.** C'est la septième fois de la série, et la méthode reste la même : chercher
les appelants, pas relire le modèle.

**CE N'EST PAS UN CONTOURNEMENT DU PLAFOND DE MASSE, C'EST UN ARBITRAGE.** Chacun
de ses quatre termes est un fait d'ingénierie réel :

1. **N tirs, c'est N fois le prix**, et le sélecteur choisit désormais la
   *campagne* la moins chère, pas le *tir* le moins cher.
2. **Et surtout R^N de fiabilité** : le segment de mise en orbite vaut
   `R_lanceur^N · R_amarrage^(N−1)`. Neuf tirs à 0,98 rendent **0,83**. *La masse
   s'achète en risque.*
3. **L'assemblage dure** — la cadence d'un pas de tir n'est pas instantanée — et
   ce temps tombe sur l'axe CALENDRIER du contrat.
4. **Les ergols cryogéniques s'évaporent pendant ce temps.** C'est LA contrainte
   réelle des architectures assemblées.

**L'ÉBULLITION EST UN POINT FIXE, ET IL PEUT DIVERGER.** Il faut lancer plus
d'ergols que nécessaire, ce qui ajoute des tirs, ce qui allonge l'attente, ce qui
en évapore davantage. Le modèle **itère** jusqu'à stabilité du nombre de tirs (un
entier : la convergence est franche ou elle n'a pas lieu), borné à 20 tirs. Quand
il diverge, le refus le **distingue** d'un simple « trop lourd » :
*« L'ÉBULLITION DES ERGOLS DIVERGE : RECHERCHER `transfert_ergols` »* — parce que
cette impasse-là ne se résout pas en ajoutant des tirs.

**LA FRACTION SURVIVANTE A UNE FORME CLOSE EXACTE.** Les ergols n'arrivent pas
d'un coup : la charge *i* attend (n−i)·Δt. La moyenne des survies est une **série
géométrique**, donc

> (1 − e^(−λ·n·Δt)) / ( n · (1 − e^(−λ·Δt)) )

Aucune intégration numérique, et n=1 rend exactement 1. Vérifié par oracle
**terme à terme** contre la somme brute, pour n ∈ {1, 2, 5, 9, 20}. Utiliser la
durée totale aurait doublement puni : la première charge attend tout, la dernière
rien.

**CONSTANTES DÉCLARÉES, CHACUNE ADOSSÉE À UN FAIT** [GDD 6.8] : intervalle entre
tirs **30 j** (Navette ~2 mois entre vols d'un orbiteur ; Ares V visait ~1/mois),
**20 j** avec robotique d'assemblage ; ébullition **0,2 %/jour** (réel : 0,1 à
1 %/j — Centaur ~1-3 %/j sur les premières versions ; hypothèse **optimiste**,
donc honnête à déclarer) ; amarrage **0,990** / **0,996** avec robotique.

**UN DÉFAUT TROUVÉ EN COURS DE ROUTE, ET IL COMPTE.** Avec l'assemblage, « le
moins cher qui soulève » est devenu un mauvais conseil : CAT-05 basculait sur
**2 tirs légers** (126 M$, P = 0,843) au lieu d'un lourd (139 M$, P = 0,980) —
13 M$ économisés contre 14 points de fiabilité, un marché qu'aucun responsable de
programme ne prendrait, et le contrat passait de VIABLE à RISQUE. Corrigé **par
déduction, pas par réglage** : puisque `p_success ≤ p_segment`, une campagne dont
le segment est déjà sous l'exigence du client est **garantie d'échouer** — l'écarter
ne retire aucune option viable. **Et le résultat est superbe** : c'est
maintenant l'**exigence de fiabilité** qui force CAT-09 sur le super-lourd (six
tirs moyens rendent 0,868 quand le contrat exige 0,90), très exactement la raison
réelle pour laquelle on construit SLS au lieu d'empiler des Falcon 9 sous un
équipage.

**CE QUE ÇA CHANGE, MESURÉ.**

| | avant | après |
|---|---:|---:|
| Contrats réalisables en masse | 9 / 11 | **10 / 11** |
| CAT-10 (cargo NEP, 181 t) | impossible | **2 super-lourds, 20 j, P = 0,937** |
| CAT-09 blindé (182 t) | **non lançable** | 2 tirs, P = 0,931 |

Ne reste hors de portée que **CAT-11** : 346 495 t en chimique pour 30 km/s, soit
plus de vingt lancements. *Aucun assemblage ne rattrape une exponentielle* — et
c'est pourquoi [GDD 19.3] réserve ce régime à l'antimatière. Cohérence complète
avec les deux passes précédentes.

**ET L'ARBITRAGE DU [GDD 6.6] A CHANGÉ DE NATURE, PAS DE SÉVÉRITÉ.** Avant,
protéger l'équipage rendait le vol **non lançable** — une falaise. Mesuré
maintenant :

> **121 t / 1 tir / P = 0,970 / 1 560 M$  →  182 t / 2 tirs / P = 0,931 / 3 016 M$**

Le vol reste possible, mais la protection se **paie** — en tirs, donc en risque et
en argent. *Un arbitrage gradué vaut mieux qu'un mur* : le joueur choisit combien
de protection il achète au lieu de buter dessus. Le budget CAT-10 est passé de
650 à 4 500 M$ pour la même raison que CAT-09 : il ne payait pas le tiers de sa
campagne (Europa Clipper, sans propulsion nucléaire : ~5 200 M$).

**PREUVE** : build `SPEditor` Succeeded, **3 555 oracles au vert** (+30), et
`ue_assemblage_poste_controle.png` — « ASSEMBLAGE EN ORBITE : 2 lancements sur
30 jours », « SEGMENT DE MISE EN ORBITE : P = 0,931 (3,9 t d'ergols évaporés) »,
coût 3 030 / 3 000 M€ (dépassement de 1 %, la bonne tension), VIE À BORD et
avaries présentes.

### ARES DIT OÙ ALLER ; L'ARCHITECTE DIT COMMENT (2026-07-29)

**PRÉCISION DE L'UTILISATEUR, ET LE GDD LA CONFIRME MOT POUR MOT.** « C'est le
joueur qui crée la mission de A à Z, ARES dit juste : on doit aller là pour faire
ça. » [GDD 3.1] : « L'Architecte Mission décide ***comment*** concevoir et
conduire, **dans des enveloppes budgétaires imposées par ARES**. Il ne fixe pas
le budget. »

**LE DÉFAUT QUE ÇA DÉSIGNE.** `Contract::payload_kg = 20 000` pour Mars habité
n'était pas un objectif : c'était **une masse d'habitat**, donc une architecture
déjà choisie, imposée par le client. Pire, elle se superposait à des consommables
et à un blindage déjà calculés, sans qu'on sache ce que ces 20 t contenaient.

**CE QUI RESTE AU CONTRAT, ET CE QUI PASSE À L'ARCHITECTE.**
- ARES impose l'**enveloppe** — budget, délai, fiabilité exigée — et l'**objectif**
  (où, pour quoi, avec combien de personnes). Inchangé.
- ARES garde `payload_kg` **quand la masse EST l'objectif** : un satellite, du
  fret, des instruments. Là, le client fournit la charge.
- La **coque pressurisée** devient une **conséquence** de deux décisions
  d'architecte — combien d'équipage, combien de volume chacun — et d'un fait.

**ET LE FAIT EST REMARQUABLEMENT STABLE.** Modules pressurisés de l'ISS :

| | masse | volume | densité |
|---|---:|---:|---:|
| Destiny | 14 515 kg | 106 m³ | **137 kg/m³** |
| Columbus | 10 275 kg | 75 m³ | **137 kg/m³** |
| Kibo JPM | 15 900 kg | 116 m³ | **137 kg/m³** |

Trois modules, trois agences, trois décennies : **137 kg/m³ à 1 % près**. Ce n'est
pas une coïncidence, c'est ce que coûte une coque qualifiée pour du vol habité.
Vérification qui rassure : 6 personnes × 25 m³ × 137 = **20 550 kg**, soit le
forfait de 20 000 kg à **2,7 % près**. Le forfait était bien choisi — il était
simplement **posé** au lieu d'être **déduit**.

**LE VOLUME PAR PERSONNE EST DEVENU UNE DÉCISION, ET ELLE MORD.** Mesuré sur la
croisière martienne :

> **25 m³/personne → 20,6 t d'habitat, 131 t au décollage
> 15 m³/personne → 12,3 t d'habitat, 100 t au décollage**

**31 tonnes** d'écart pour une seule décision d'architecte — et elle joue deux
fois, parce que serrer l'habitat réduit aussi la **surface à blinder** [GDD 6.6].
Les deux décisions ne sont pas indépendantes, et le modèle le montre au lieu de
le dire.

**UN VOL NEAR-EARTH N'EMPORTE PAS D'HABITAT.** Il s'amarre à une station
existante — c'est très exactement à ça qu'une station sert. Le critère n'est pas
une liste de familles à tenir à jour mais un fait **déjà calculé** : y a-t-il un
aller-retour daté vers une cible nommée. CAT-06/07/08 sont donc **inchangés**.

**CE QUE ÇA CHANGE AU CATALOGUE, MESURÉ.**

| | avant | après |
|---|---:|---:|
| CAT-09 (Mars habité) | 121 t, 1 tir, 1 560 / 3 000 M€ | **131 t, 2 tirs, 2 969 / 3 000** |
| CAT-10 (cargo NEP) | 3 060 / 650 → BUDGET | **3 060 / 4 500 → VIABLE** |
| CAT-06/07/08 (LEO) | inchangés | inchangés |

**PREUVE** : build `SPEditor` Succeeded, **3 573 oracles au vert** (+18), dont la
densité confrontée aux trois modules de l'ISS, la linéarité de la déduction, le
fait qu'un vol near-Earth n'emporte rien, et — le plus important — que la décision
de volume **traverse tout le modèle jusqu'à Tsiolkovsky** au lieu de rester un
champ décoratif.

**ET L'ARBITRAGE DU [GDD 6.6] A ÉTÉ REMESURÉ, avec une correction de ma part.**
J'affirmais que blinder ajoute *toujours* un lancement. **Faux** : c'était décrire
l'endroit où la base se trouvait, pas une loi. Ce qui est une loi : blinder ajoute
toujours de la masse et toujours du coût, et ne fait jamais baisser le nombre de
tirs ni monter la fiabilité. Le **franchissement de palier**, lui, se démontre
séparément — 30 g/cm² font passer de 2 à **3 tirs** et P de 0,937 à 0,905.

**SUITE (même jour) — L'EFFECTIF REJOINT L'OBJECTIF, ET LA ZONE D'OMBRE SE FERME.**

**1. L'ÉQUIPAGE EST PORTÉ PAR LE CONTRAT, plus par une table indexée sur une
chaîne de famille.** `Contract::crew_required` : c'est ARES qui dit combien de
personnes doivent être là-bas, et deux contrats de la même filière peuvent donc
demander des équipages différents. La table par famille reste la **source** (elle
porte ses références réelles — incrément ISS, STS-125, DRA 5.0) mais elle
**alimente** désormais le contrat au lieu d'être relue en six endroits.

**ET J'AI TRANCHÉ LA QUESTION QUE J'AVAIS LAISSÉE OUVERTE**, faute de réponse et
parce que le modèle la tranche lui-même : l'effectif **n'est pas** une décision
d'architecte. Embarquer plus de monde n'achète **rien** de modélisé — ni la
réparation ni la redondance médicale ne dépendent de l'effectif — et coûte de la
coque, des vivres et du blindage. *Un « choix » à une seule bonne réponse n'en
est pas un.* Le jour où l'effectif achètera quelque chose, il deviendra une
décision et déménagera dans `MissionPlan` ; c'est écrit dans le code, à l'endroit
où quelqu'un le lira.

**2. LA ZONE D'OMBRE DU POSTE EST FERMÉE** — signalée trois fois, jamais traitée.
Le bloc « VIE À BORD » était sous la ligne de flottaison du défilement : dose,
boucles et écart d'horloge n'étaient contrôlables par **aucune capture**, ce qui
est exactement la condition dans laquelle le piège n°74 (postes figés) avait
prospéré des semaines. Remède, et c'est le raisonnement du mode console
(piège n°65) étendu à tout le vol : **une fois parti, le bilan de viabilité est
de l'histoire** — aucune de ses sept lignes n'est actionnable, alors que la
télémétrie vitale l'est à chaque instant. On garde **une** ligne — ce qu'on a
emporté et sous quel verdict — et on rend la place à ce qui vit.

`ue_vie_a_bord_poste_controle.png` montre le résultat, et **tout ce qui a été
construit ces dernières passes y est enfin vérifiable** : autonomie 698,8 j
(6 à bord), O2/eau, vivres/CO2, boucles 93 %/85 %, dose mission 0,135 Sv derrière
17,5 g/cm², dose de carrière 13 %, et **« HORLOGE DE BORD +35,4 ms — l'équipage
vieillit PLUS vite »**. Le bilan tient en une ligne : « ARCHITECTURE EMPORTÉE :
203 t en 2 lancements — BUDGET RISQUE ».

**PREUVE** : **3 577 oracles au vert** (+4), build Succeeded.

**QUATRE PIÈGES PAYÉS.**
- **n°82 — UNE BASCULE DE PROPRIÉTAIRE RÉVÈLE LES FIXTURES INCOMPLÈTES, et c'est
  un service.** Déplacer l'effectif vers le contrat a fait tomber **neuf
  oracles** dans deux suites : des missions fabriquées à la main qui posaient
  leurs termes champ par champ, donc avec `crew_required = 0` — un **équipage
  fantôme**, sans vivres, sans dose, aux réserves inépuisables. Ces fixtures
  testaient depuis toujours autre chose que le jeu, et rien ne le disait.
  Corrigées en leur donnant les termes de leur famille, comme le harnais de
  capture. Un oracle vérifie désormais que `crewed` et `crew_required` disent la
  même chose pour les onze contrats.
- **n°83 — UN ORACLE QUI ÉPINGLE UN LITTÉRAL TESTE LE CHIFFRE, PAS LA RÈGLE.**
  `CHECK(payload_kg == 20000, "la charge du CONTRAT reste celle du client")` :
  l'intention était juste et la valeur a changé légitimement. Réécrit contre **la
  table** plutôt que contre `20000` — la règle survit à la calibration, le
  littéral non.
- **n°84 — UN CHAMP AJOUTÉ AU MILIEU D'UN AGRÉGAT CASSE QUINZE SITES.**
  `Contract` est initialisé POSITIONNELLEMENT dans la liste de contrats du
  prototype (`{1200.0, 115.0, 18.0, 0.85}`). Insérer un `int` entre deux `double`
  y produisait quinze erreurs de rétrécissement. Champ déplacé **en queue**, avec
  l'avertissement à l'endroit où le prochain ajout se fera.
- **n°96 — UN JEU DE PARAMÈTRES CALIBRÉ POUR UNE PAIRE DE CORPS MENT SUR TOUTES
  LES AUTRES.** `astro::WindowParams` porte ses valeurs par défaut EN COMMENTAIRE :
  « horizon 800 j >= 1 période synodique **Terre-Mars** », durées 150-400 j. Trois
  appels sur quatre les utilisaient tels quels. Sur Jupiter (Hohmann = 997 j) le
  balayage butait sur son propre plafond et rendait **17 621 m/s** d'injection au
  lieu de 8 540, sans la moindre alerte — la structure restait cohérente avec
  elle-même, comme au n°94. Remède : DÉRIVER les bornes de la géométrie (Hohmann,
  synodique), en lisant les demi-grands axes **par vis-viva** et non le rayon du
  moment (sur Mars, e = 0,093 fait osciller la synodique estimée de 700 à 920 j,
  ce qui faisait basculer Mars hors de ses propres bornes une fois sur deux). Et
  garder la voie par défaut quand elle contient déjà le Hohmann de la paire : une
  calibration existante se préserve au bit près, elle ne se « rafraîchit » pas.
- **n°99 — UN TABLEAU DE BORD QUI NE CONNAÎT PAS TOUS LES FORMATS DE VERDICT REND
  DU VERT SUR DU ROUGE.** Mon relevé des douze suites comptait les lignes
  commençant par `ECHEC` ; `test_ares_modules` écrit `  [FAIL]`, et
  `test_api_sol` conclut par `0 ECHEC` au singulier. Le tableau a donc affiché
  `187 OK, echecs:0` là où la suite disait `187 OK, 1 FAIL`. **Ce qui a sauvé la
  mesure n'est pas le compteur d'échecs mais le TOTAL** : 188 la veille, 187
  ce jour-là — un oracle avait disparu, et un oracle qui disparaît est toujours
  un défaut (ici : il testait la règle que la passe venait de retirer). Remède :
  lire la ligne de verdict de CHAQUE suite, telle qu'elle l'écrit, et surveiller
  le total autant que les échecs.
- **n°98 — UN ORACLE QUI FAIT COULER DES DÉCENNIES SE HEURTE AUX AUTRES SYSTÈMES,
  ET C'EST LE MODÈLE QUI A RAISON.** Pour voir mourir un Architecte de 85 ans,
  j'ai fait avancer le calendrier de cinquante-trois ans : il est mort à **38**.
  Deux causes, aucune dans le code testé. (1) `avancer_temps` déplace le
  CALENDRIER ; la couche ARES — donc le vieillissement — ne rattrape qu'au tick
  suivant, `assurer` étant ce qui appelle `avancer`. Le jeu le fait à chaque
  frame ; un oracle doit l'écrire. (2) Une agence qui n'entreprend rien fait
  **faillite en six ans** [GDD 13.2], et `avancer_temps` s'arrête net sur un
  game over : le calendrier ne pouvait PAS atteindre la fin de vie. Remède :
  poser l'état (l'âge est un fait du personnage) et laisser le modèle le
  constater — la doctrine des drapeaux de capture, appliquée aux oracles.
- **n°96 — UN DÉTAIL TROP PETIT POUR LA CAPTURE SE MESURE EN L'AGRANDISSANT.**
  L'ajutage du vaisseau conçu fait 1,5 m au bout d'une coque de 19 m : à l'écran,
  une soixantaine de pixels. J'ai lu **trois fois de suite l'inverse de la vérité**
  sur son orientation, en comparant des images où la cloche et sa pointe sont à la
  limite du discernable — et j'ai « corrigé » un défaut qui n'existait pas, puis
  vérifié la correction sur une image tout aussi ambiguë. Deux mesures ont tranché
  en une passe : **colorier chaque rôle** (on sait alors QUELLE pièce on regarde),
  puis **dessiner l'ajutage à 6 m** le temps d'une capture (les deux orientations
  deviennent impossibles à confondre). Corollaire du n°90 : quand l'œil ne peut pas
  trancher, ce n'est pas une mesure — il faut changer l'expérience, pas la refaire.
- **n°97 — `FParse::Value` SUR UN FLOTTANT DÉPEND DE LA LOCALE.** `-spdist=0.4`
  rendait **0,0** sur une machine française (séparateur décimal = virgule), sans la
  moindre erreur : le drapeau semblait appliqué — le journal imprimait bien une
  valeur —, et deux captures censées être prises à 18 m et 400 m étaient à la même
  distance. Diagnostic en une ligne : imprimer la distance CAMÉRA-VAISSEAU mesurée
  au lieu du paramètre demandé. Remède : les drapeaux de capture prennent des
  **entiers** (`-spvaisseau=<mètres>`), qui n'ont pas de séparateur décimal. À
  garder en tête pour tout futur drapeau à valeur fractionnaire.
- **n°95 — UNE STRUCTURE DE 13 Ko QUI VOYAGE PAR VALEUR FINIT PAR DÉBORDER LA
  PILE.** `FlightTrace` portait sa polyligne dans un tableau FIXE de 512 Vec3
  (12 Ko) ; `ContexteVol` en contient une, et la campagne de correction en tient
  **trois vivantes** dans une même frame. La pile de 1 Mo passait déjà de justesse
  — ajouter un kilo-octet (les morceaux d'un tour) l'a fait déborder. **Le symptôme
  ne ressemblait pas à sa cause** : une suite d'oracles qui s'arrête NET au milieu,
  sans message, code de sortie 253 (= 0xC00000FD tronqué à un octet). Diagnostic en
  une minute : relier avec `/STACK:8388608` — si tout passe, c'est la pile.
  Le remède n'est pas d'agrandir la pile (le jeu tourne sur les threads d'UE, à
  1 Mo eux aussi) mais de sortir l'artefact de RENDU du chemin de calcul : la
  polyligne est passée sur le tas. **Un chemin qui frôle la pile est un défaut
  latent** — il attend la prochaine structure qui grandit.
- **n°94 — DEUX RÉGLAGES POUR LA MÊME GRANDEUR SE CONTREDISENT TOUJOURS, ET CHACUN
  RESTE COHÉRENT AVEC LUI-MÊME.** Le gate de lancement lisait la fenêtre avec un
  slop de 60 jours, la durée de transit avec 10. Aucun des deux n'était faux
  isolément ; ensemble ils faisaient décoller un vol le jour même en lui donnant la
  durée d'un départ 50 jours plus tard — arc sous l'orbite terrestre, injection à
  5 827 m/s au lieu de 3 600. Le défaut a survécu des mois parce qu'il ne mordait
  qu'à certaines dates. **Signe avant-coureur à connaître : une structure qui se
  contredit elle-même** (`open = 1` ET `next_open_days = 50,6`). Un seul réglage
  partagé (`mission_window_params`), et l'invariant « attendre l'attente annoncée
  ouvre le gate » est devenu un oracle indépendant de la date. Corollaire :
  **une date de départ calculée puis jetée** (`local_dep_tdb`) rend le désaccord
  invisible de l'extérieur — publier le couple, jamais la moitié.
- **n°93 — UN MODÈLE QUI REFUSE TOUT N'EST PAS FORCÉMENT FAUX : IL PEUT ÊTRE HORS
  DE SON DOMAINE.** Branché sur les capsules, le corridor de rentrée est ressorti
  **fermé pour les cinq, à toutes les vitesses**, Apollo compris sur le vol qu'il a
  réellement fait. Le réflexe naturel — « le modèle est cassé, corrigeons-le » —
  était faux : Allen–Eggers est **balistique**, et il disait la vérité (personne ne
  revient de la Lune sans portance). Le fichier PORTAIT déjà l'outil qu'il fallait
  (`integrate_entry`, avec la portance dans ses équations), lui aussi inutilisé.
  **Avant de corriger un modèle qui refuse tout, vérifier l'hypothèse qu'il déclare
  dans son en-tête** — et regarder si le voisin d'à côté ne la lève pas déjà.
- **n°92 — UNE ANNULATION FLOTTANTE TRANSFORME UNE PROBABILITÉ EN DÉCRET.**
  `0,5·(1 + erf(z/√2))` pour une queue basse : `erf` sature à −1,0 dès |z| > 6, la
  somme rend **zéro exact**, et le modèle qui promettait « jamais un verdict binaire
  décrété » [GDD 12.4] en rend un. La valeur vraie (1e-115 pour un vol de 23 ans)
  était largement représentable — le problème n'était pas la magnitude mais la
  **cancellation**. `erfc` la calcule directement. **Sur toute queue de distribution,
  utiliser la forme complémentaire** ; et si un plancher existe quand même (ici
  z ≈ −37, où c'est le `double` qui sature), le NOMMER dans un oracle plutôt que de
  laisser croire qu'il n'y en a pas.
- **n°91 — DÉCLARER UN PARAMÈTRE NE LE REND PAS INNOCENT.**
  J'ai posé une surface de circuit de radiateur à 1,0 m² « ordre de grandeur, à
  calibrer [Annexe E] », en signalant honnêtement qu'elle avait un levier direct sur
  le résultat. La déclaration ne coûtait rien et ne protégeait de rien : le
  paramètre a immédiatement fait **+60 % de masse** au lieu de +20 sur une petite
  aile, et c'est mon **propre oracle** qui a refusé le chiffre. Le réflexe à ne pas
  avoir : déplacer le seuil de l'oracle. Le bon : chercher la valeur réelle — un
  panneau du HRS de l'ISS fait 8,79 m² et porte 22 tubes, donc **0,40 m²**.
  **Un paramètre à fort levier se cherche, il ne se déclare pas.**
- **n°90 — SUR UNE DIVERGENCE NUMÉRIQUE, IMPRIMER LES DEUX NOMBRES D'ABORD.**
  Un oracle de sous-pas fixe est tombé après le recalibrage des avaries. J'ai
  formé et implémenté **trois hypothèses fausses** avant de mesurer : la grille de
  tirage (déjà absolue — le « correctif » ne corrigeait rien), le verrou de
  rattrapage (hors de cause), puis l'intégration par frame (celle-là juste, mais
  insuffisante). Un `printf` des deux valeurs **et du nombre d'avaries de chaque
  côté** a tranché en une ligne : même nombre d'avaries, 21,6 jours d'écart ⇒ ce
  n'était pas le découpage, c'étaient deux états différents. Coût de l'ordre : deux
  correctifs inutiles écrits et compilés. **Mesurer l'écart avant de l'expliquer.**
- **n°89 — UN COMMENTAIRE « VÉRIFIÉ » SANS ORACLE EST UNE OPINION.**
  `flight/Descent.hpp` portait « Vérifié : Lune, Isp 311 s, TWR 2-3 →
  ~1730-1750 m/s » et **aucune suite ne l'incluait** : le chiffre n'avait jamais
  été exécuté. L'oracle écrit après coup confirme le modèle mais **corrige la
  note** (1 798 m/s à TWR 2, pas 1 730). Le mot « vérifié » dans un commentaire
  doit pointer vers une suite, sinon c'est une note d'auteur. **Corollaire de
  méthode** : le balayage par graphe d'inclusion doit se faire DEUX FOIS — une
  fois hors `tests/` (« inatteignable depuis le jeu ») et une fois DANS `tests/`
  (« jamais exécuté »). Les deux réponses sont différentes, et seule la seconde
  désigne du code auquel on ne peut rien croire.
- **n°88 — UN MÉCANISME PEUT ÊTRE ACTIF POUR UNE MAUVAISE RAISON.**
  Trois fautes de la même passe [GDD 12.4], toutes de cette forme : (1) le
  vieillissement d'un réacteur piloté par le **délai d'approvisionnement** — le
  NEP perdait 27 % avant de décoller, alors qu'un cœur se construit à la FIN de ce
  délai ; (2) une densité de débris de test **sept ordres** trop haute, qui
  détruisait le radiateur à coup sûr et aurait validé le mécanisme sur un nombre
  commode — la densité d'un couloir se CALCULE (volume de la coquille), elle ne se
  choisit pas ; (3) la « cause dominante » comparée au **produit accumulé** au lieu
  des autres facteurs, si bien que le second mécanisme ne pouvait plus jamais être
  nommé. Un oracle qui vérifie seulement qu'un mécanisme *a un effet* les laisse
  tous les trois passer. Il faut vérifier **l'ORDRE DE GRANDEUR et le SIGNE de la
  dépendance** : de quoi cet effet dépend-il, et est-ce la bonne grandeur ?
- **n°87 — DEUX COUCHES PEUVENT NOMMER LE MÊME OBJET SANS LE MODÉLISER.**
  L'atelier appelait « masse au décollage » celle d'une FUSÉE partant du sol ; la
  mission appelait du même nom la charge qu'un lanceur ACHETÉ met en orbite. Le
  désaccord était total (Isp 338 contre 449,7 sur l'étage du bas) et **invisible
  tant que l'atelier ne nourrissait rien** — c'est la signature de la famille
  « modèle sans conséquence » : brancher un modèle mort ne révèle pas seulement
  qu'il était mort, ça révèle **ce qu'il croyait décrire**. Avant de câbler deux
  modèles qui partagent un vocabulaire, vérifier qu'ils partagent l'OBJET.
  Corollaire pratique qui a sauvé la calibration : faire en sorte que l'état par
  défaut du modèle nouvellement branché **reproduise exactement** l'ancien
  comportement, puis le prouver par oracle — ici « même masse au kg près, même
  coût, même fiabilité, même calendrier ». C'est ce même oracle qui a ensuite
  attrapé une vraie faute (trois allumages comptés pour deux manœuvres).
- **n°86 — « JE NE PEUX PAS SANS INVENTER » CACHE SOUVENT « JE N'AI PAS CHERCHÉ ».**
  J'ai différé la fusion des catalogues de moteurs en invoquant 54 chiffres de
  coûts et de délais que [GDD 20] diffère — argument juste sur les prémisses,
  faux sur la conclusion. Trois familles sur quatre **n'avaient pas à être
  écrites** (fiabilité dérivable du statut de qualification, délai dérivable du
  TRL, développement déjà payé par l'arbre), et la quatrième se **cherche** :
  cinq prix viennent de contrats publics. Là où le prix n'existe vraiment pas, le
  GDD avait déjà la réponse — **le triplet + confiance + source de 12.3.2**, qui
  dit « on ne sait pas » sans mentir. Avant de conclure qu'une donnée manque,
  vérifier (1) si elle se DÉRIVE d'une donnée déjà présente, (2) si elle est
  publiée, (3) si le schéma du GDD sait déjà représenter l'ignorance.
  **Corollaire mesuré** : les deux convergences que je m'étais interdites « parce
  qu'elles déplaceraient la calibration » ne l'ont pas déplacée (10/11 contrats
  réalisables, inchangé). Le refus reposait sur une crainte, pas sur une mesure.
- **n°85 — « QUI APPELLE ? » NE TROUVE QUE DANS LES FICHIERS QU'ON REGARDE.**
  La famille « modèle sans consommateur » était déclarée épuisée après huit cas,
  tous trouvés en cherchant des appelants. Un balayage par **accessibilité du
  graphe d'inclusion** — quels en-têtes n'ont aucun `#include` hors des tests —
  en a sorti **six de plus d'un coup**, ~750 lignes. La recherche d'appelants
  suppose qu'on a la liste des modèles ; le graphe la produit. Et il ne suffit pas
  non plus : le septième cas de la passe (la moitié ÉNERGIE de
  `Propulsion.hpp`) vit dans un fichier **bel et bien inclus**, dont seule l'autre
  moitié était consommée. **La granularité de la question doit descendre au
  symbole, pas s'arrêter au fichier.**
- **n°84 — UN MODÈLE PEUT AVOIR UN CONSOMMATEUR ET AUCUNE CONSÉQUENCE.**
  La dose chronique était accumulée, sérialisée, affichée — et ne faisait
  strictement rien : elle ne verrouillait que les vols suivants, donc rien sur un
  vol terminal. La famille « modèle sans consommateur » se cherchait par
  « qui appelle ? » ; celle-ci demande **« que se passe-t-il si cette valeur
  double ? »**. Si la réponse est « rien », le modèle est décoratif quel que soit
  le nombre d'appelants.
- **n°82 — UN ORACLE VERT PEUT MESURER AUTRE CHOSE QUE CE QU'IL DIT.**
  « le stock CONVERGE — 200 ans de plus n'ajoutent rien » passait depuis toujours.
  Il ne mesurait aucune convergence : l'agence faisait **faillite** au bout de
  quelques années d'inactivité et `Jeu::avancer_temps` s'arrêtait net. Le stock ne
  convergeait pas, **le temps s'arrêtait** — et les deux se ressemblent
  parfaitement tant que le stock est petit. Règle : un oracle qui avance le temps
  doit **vérifier que le temps a avancé**, sinon il teste l'immobilité. Corollaire
  utile : le défaut trouvé était un *fait de jeu* meilleur que l'oracle perdu (les
  140 ans de [GDD 3.5] exigent une agence qui produit pendant 140 ans).
- **n°83 — NE JAMAIS FAIRE `Get-Content -Raw | Set-Content` SUR UN SOURCE UTF-8.**
  Windows PowerShell 5.1 lit avec la codepage ANSI et écrit en UTF-8 : tout
  caractère accentué ressort **doublement encodé** (`à` → `Ã `, `—` → `â€"`), sur
  le fichier ENTIER, plus un BOM. `test_session.cpp` y est passé. Réparable
  (relire en UTF-8, ré-encoder en cp1252 avec les 5 positions non définies mappées
  à la main, réécrire sans BOM) mais gratuitement risqué : utiliser **Edit**, ou
  Python si le remplacement doit être global.
- **n°81 — UN FORFAIT BIEN CALIBRÉ RESTE UN FORFAIT.** Les 20 t de Mars habité
  reproduisaient la déduction à 2,7 % près : impossible à repérer en comparant des
  nombres, puisqu'ils étaient *justes*. Ce qui les trahissait n'était pas leur
  valeur mais **leur place** — dans le contrat, donc du côté du client, alors que
  la masse d'un véhicule est le métier de l'architecte. Règle : vérifier **qui
  décide** d'une grandeur avant de vérifier **combien** elle vaut.
- **n°79 — UN `std::move` REND LA SOURCE INUTILISABLE, Y COMPRIS POUR UN
  `if`.** Le harnais de capture accorde désormais les prérequis **du contrat**
  plutôt qu'une liste écrite à la main (qui se périme — c'est arrivé : l'assemblage
  est devenu nécessaire et la liste ne le savait pas). Ma boucle lisait
  `M.contract.family` **après** `push_back(std::move(M))` : chaîne vidée,
  comparaison jamais vraie, aucun prérequis accordé. La capture ne montrait qu'un
  bloc manquant ; **le journal de bord a donné la cause en une ligne**
  (« maturite requise : support-vie long sejour »). Encore une fois : un chiffre
  mesuré vaut dix captures. Le harnais lisait déjà `Entree` et `Tof` avant le
  move — la précaution existait, je ne l'ai pas suivie.
- **n°80 — LE SEUL ORACLE QUI DÉPEND DE L'OS EST INSTABLE SOUS CHARGE.**
  `test_toolchain` (« un pointeur invalide produit un ÉCHEC DE MISSION ») a
  échoué **une fois** dans un sweep de douze suites enchaînées, puis **quatre
  exécutions consécutives l'ont donné vert**. C'est le seul test qui fait
  délibérément planter un processus dans un job object : son verdict dépend de
  l'ordonnancement. Noté plutôt que tu — un test intermittent qu'on ignore est un
  test mort. À revoir si la fréquence augmente.

**UN PIÈGE PAYÉ, de méthode.**
- **n°78 — MESURER LE DÉFAUT AVANT DE LE FRANCHIR.** Ma première sonde a rendu
  « CAT-11 : AUCUN LANCEUR NE SOULÈVE » — **c'était ma sonde qui mentait**, elle
  imprimait une chaîne fixe au lieu de `Assessment::why` ; la vraie cause était le
  Δv. Et ma première mesure a déclaré CAT-10 impossible **en oubliant que le
  nombre d'étages est une décision du joueur** : il fallait balayer 2/3/4 avant de
  conclure. Deux fois, l'instrument a failli faire condamner le modèle. Règle :
  quand une mesure accuse le contenu, **vérifier l'instrument d'abord**.

**UN AUTRE PIÈGE PAYÉ, de méthode.**
- **n°77 — UNE ALARME PERMANENTE N'EST PAS UNE ALARME.** Les trois défauts
  ci-dessus étaient visibles depuis des semaines, dans chaque capture en vol, et
  la documentation les avait **expliqués au lieu de les corriger** (« réserve
  d'affichage, antérieure et sans rapport »). Écrire qu'un rouge est normal, c'est
  le rendre invisible. Règle : un indicateur qui est rouge dans TOUTES les images
  de référence est un bug de l'indicateur jusqu'à preuve du contraire — et la
  preuve se fait en réparant, pas en annotant.

**UN AUTRE PIÈGE PAYÉ, de méthode.**
- **n°76 — UN MODÈLE QUI NE SE CONFRONTE QU'À LUI-MÊME NE PROUVE RIEN.** Les
  premiers oracles d'horloge que j'allais écrire vérifiaient la cohérence interne
  (le rapport est bien > 1 en croisière, il croît avec `a`…) : tous auraient passé
  sur un modèle **purement cinématique**, c'est-à-dire faux d'un facteur 3. Ce qui
  a rendu la vérification mordante, c'est d'être allé chercher des **valeurs que
  personne dans ce dépôt n'a choisies** — la correction GPS, le retard de l'ISS,
  la vitesse orbitale d'almanach. Règle générale : quand un domaine a des mesures
  publiées, l'oracle doit les viser, pas se contenter de la cohérence interne.
  Corollaire : `-spvecu` seul rend le MENU (`ArmerCapture` sort si `-spscene`
  vaut 0) — une capture qu'on ne regarde pas est une preuve qu'on n'a pas.

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
    - ~~la voûte étoilée est vue **en miroir**~~ — **CORRIGÉ le 2026-07-28** : ce
      n'était pas un miroir mais une INVERSION CENTRALE (échelle négative sur
      trois axes), invisible parce qu'un grand cercle y est invariant. Échelle
      passée en positif. Le repère de la carte est **GALACTIQUE**, mesuré
      (U = 0,5133, V = 0,4962). **Le calage J2000 est ABANDONNÉ, et c'est motivé** :
      trois mesures établissent que cette texture est un panorama stylisé (Nuages
      de Magellan indétectables) — l'aligner serait une fausse précision. Le
      remède est un catalogue Hipparcos en points, pas une rotation de photo.
      Voir §2, « LE CIEL N'ÉTAIT PAS EN MIROIR » ;
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
~~4. Tri §5.4 étape 4~~ — **FAIT le 2026-07-28 par l'utilisateur** (commit
   `27458f8`). `_archive` : **149 Mo → 26 Mo**, ~123 Mo récupérés.
   ⚠ **LA PURGE A EMPORTÉ `build_vk/`, QUI DEVAIT RESTER** — restauré depuis le
   commit parent (`git checkout ed839de -- ".../build_vk"`, 44 fichiers). Sans
   lui, la RÈGLE DE MÉTHODE du §1 n'a plus d'oracle : « avant toute passe UI/3D,
   lancer le binaire de référence et REGARDER » suppose qu'il existe.
   **VÉRIFIÉ APRÈS RESTAURATION** : `solar_system_map.exe` recapture, et l'image
   est **identique AU BIT PRÈS** à celle prise avant la purge (2 764 854 octets).
   Les 45 captures de `docs/reference_solar_system_map/` n'ont jamais été
   menacées — elles vivent hors de `_archive`.
   *Le binaire de référence ne dépendait bien pas de `extern/` (glfw lié
   statiquement, vérifié sur la table d'imports avant la suppression) : c'est la
   seule raison pour laquelle la purge n'a rien coûté.*

~~2. LA MISSION VÉCUE~~ — **fait le 2026-07-28** (section « LA MISSION VÉCUE » du
   §2). La décision 18 (« vol habité vécu inclus ») avait un modèle complet et
   aucune porte d'entrée. Elle en a une : les vivres pèsent dans Tsiolkovsky sur
   une durée qui est la période synodique CALCULÉE, la porte de [GDD 9.2] est
   gardée par des conditions toutes dérivées, l'agence tourne sous l'adjoint et ne
   peut plus être perdue [GDD 9.3], et l'épuisement des réserves tue par le barème
   de 10.3 et non par décret.

Reste, par ordre de valeur :

~~2. `env::Debris` n'est tické par aucun chemin vivant~~ — **mesuré et corrigé le
   2026-07-28** (section « LE VERROU DES RADIATIONS »). C'était exact : les nuages
   s'accumulaient sans décroître. Une ligne sur le chemin vif, et 300 km se
   nettoie (698 → 0 en 2 ans) pendant que 1200 km reste pollué.

~~1. LES ÉVÉNEMENTS ALÉATOIRES~~ [GDD 9.5] — **fait le 2026-07-28** (section « LES
   ANOMALIES SE PRODUISENT ENFIN »). La boucle est complète : tirer, subir,
   réparer. Les SPE sont réels et le blindage y sert enfin à quelque chose.

Reste, par ordre de valeur :

~~1. `GameState::tick` à supprimer~~ — **fait le 2026-07-29** (section « LES DEUX
   HORLOGES »). La vérification demandée a été faite **par recherche d'appelants**
   et a tranché : `deliver_unlocked_contracts` **avait** un appelant vif
   (`AresLayer::livrer_courrier`), et `treasury.tick` n'en a aucun **à juste
   titre** — la trésorerie en M$ est l'économie v1.1, neutralisée à
   l'initialisation, dont l'autorité v1.2 (`AgencyFinance::tick_month`,
   `FinancialStage`) tourne. `age_by_proper_time` était le seul écart réel, et il
   est désormais tranché dans le bon sens : le tick vif fait **plus**, pas moins.
   Piège n°72 soldé — il n'y a plus qu'un seul endroit où brancher un système.

~~2. `EventContext::medical_risk_factor` câblé à 1,0~~ — **fait le 2026-07-29**
   (même section). Le module médical de Novellus [GDD 11.6] coûtait 110 M€ et ne
   changeait aucun tirage. Gelé à l'embarquement, comme la fiabilité : c'est un
   entraînement reçu avant de partir, pas un état lu en vol.

Reste, par ordre de valeur :

~~1. Auditer les postes maintenant qu'ils vivent~~ — **fait le 2026-07-29**
   (section « L'AUDIT DES POSTES »). L'invariant « le HUD ne calcule rien » tient ;
   en revanche la capture du poste CONTRÔLE cachait **trois défauts en chaîne** —
   termes de contrat nuls hors catalogue, plan non évalué présenté comme raté, et
   `finalize` écrasant la cause exacte par une liste de symptômes. Piège n°77.

   ~~**RESTE de cet audit**~~ — **fait le 2026-07-29** (section « ARES DIT OÙ
   ALLER », suite). C'est la seconde piste qui a été retenue, et pour la raison
   qui la rendait juste : **une fois parti, le bilan de viabilité est de
   l'histoire** — aucune de ses lignes n'est actionnable, alors que la télémétrie
   vitale l'est à chaque instant. Réduit à une ligne en vol ; « VIE À BORD »
   remonte, et `ue_vie_a_bord_poste_controle.png` contrôle enfin dose, boucles et
   écart d'horloge. La zone d'ombre signalée trois fois est fermée.

   ~~**ET UNE QUESTION DE CONTENU OUVERTE PAR LA MESURE**~~ — **instruite le
   2026-07-29** (section « LE CATALOGUE ÉTAIT INACHEVABLE À 55 % »). Le chiffre
   cachait un défaut structurel : 6 contrats sur 11 inachevables, un nœud d'arbre
   qui ne débloquait rien, et une branche entière qui ne gardait rien. **9 sur 11
   réalisables**, puis **10 sur 11** après l'assemblage orbital.

   ~~Reste l'assemblage en orbite~~ — **fait le 2026-07-29** (section
   « L'ASSEMBLAGE EN ORBITE »), sur décision de l'utilisateur. **ET J'AVAIS TORT
   EN ÉCRIVANT QUE LE GDD NE LE NOMMAIT PAS** : il est au tableau de la branche 1
   du ch. 5.2 — « transfert de propergol orbital, rendez-vous automatisé robuste,
   cadence élevée » — et l'arbre portait déjà les trois nœuds correspondants.
   J'avais cherché « assemblage » dans le corps du texte et pas dans les tableaux
   de branches, et j'ai conclu à une absence sur une recherche incomplète.
   **Leçon** : avant de déclarer qu'un mécanisme manque au GDD, chercher son
   VOCABULAIRE (ici « transfert de propergol », « rendez-vous »), pas son nom.
~~2. LA CALIBRATION DE LA FIN DE JEU [Annexe E]~~ — **faite le 2026-07-29**
   (section « LA CALIBRATION DE FIN DE JEU » du §2). **Les deux prémisses de cette
   entrée étaient fausses.** « Seul le GDD peut trancher » : il avait déjà tranché
   — l'Annexe E nomme la dépendance (« vitesse maximale souhaitée en fin
   d'arbre ») et le corps du document la dit trois fois [6.7.2, 3.4, 3.5], ce qui
   **réfute l'issue (a)**. « Pas un défaut de code » : c'en était un, le **8ᵉ
   « nommé mais non connecté »** — la puissance de production était la marge de
   Novellus, donc aucune recherche de branche 6 ne pouvait la déplacer. Résultat
   mesuré : β = **0,481** à l'équilibre au palier abouti, cible 0,3 atteinte en
   **139 ans**, l'aller-retour et β = 0,9 restant hors d'atteinte **par la
   physique** [6.7.4] et non par un réservoir arbitraire.
~~2b. LA DESTINATION DE LA MISSION RELATIVISTE~~ — **tranchée par l'utilisateur
   le 2026-07-29** (section « LA MISSION RELATIVISTE A UNE DESTINATION ») :
   **l'étoile la plus proche**. Proxima à 4,2465 al (Gaia DR3), transit rectiligne
   à β constant, `proper_time` en intégrateur, β figé sur `Mission` et sérialisé
   (V5). Le chiffre de [GDD 3.4] est retrouvé — et il a révélé, en faisant
   ÉCHOUER l'oracle, que les « ~5 ans » sont ceux de l'**aller-retour**.
~~2c. [GDD 3.4] EST-IL VÉCU OU LU ?~~ — **tranché par l'utilisateur le
   2026-07-29** (section « LE RELATIVISME EST POUR L'HABITÉ ») : « le relativisme
   a un intérêt seulement pour les vols habités ». L'ancre de calibration passe
   d'une sonde de 5 t à l'architecture habitée (183 t, aller-retour), le
   confinement de 1e7 à 1e10 g, et le vol habité **existe** : β = 0,370,
   aller-retour Proxima en 22,9 ans terrestres, écart d'âge **1,63 an**, retour à
   53 ans. Les deux points de données de [GDD 3.4] sont reproduits.
~~2d. LA DOSE EST LE DERNIER MUR~~ — **fait le 2026-07-29** (section « CHRONIQUE
   ET AIGU »). La dose chronique n'avait **aucune conséquence de santé** : un
   modèle sans *conséquence*, variante plus discrète du modèle sans consommateur.
   Elle donne désormais un **REID** (ICRP 4,1 %/Sv, DDREF 2) — 10 Sv sur 23 ans
   valent **20,5 %** de risque là où 10 Sv d'un coup tuent à coup sûr. La limite
   d'Annexe B est recoupée (1 Sv ≈ 2,05 % contre 3 % de norme NASA), elle cesse
   d'être opposable au **vol terminal** [GDD 9.2], et le cancer qui en découle est
   une mort **naturelle** qui OUVRE une passation, pas une mort opérationnelle.
~~3. La famille « modèle sans consommateur » est ÉPUISÉE~~ — **ELLE NE L'ÉTAIT
   PAS, et c'est la ligne elle-même qui avait tort** (2026-07-29, section « UNE
   FILIÈRE ALIMENTÉE TRAÎNE SA CENTRALE »). Le re-balayage demandé ici a été fait
   par **accessibilité du graphe d'inclusion** au lieu de la recherche
   d'appelants, et a sorti **six en-têtes d'un coup** (~750 lignes) plus un
   septième cas *à l'intérieur* d'un fichier inclus. Piège **n°85** : « qui
   appelle ? » ne trouve que dans les fichiers qu'on regarde, et la question doit
   descendre au **symbole**, pas s'arrêter au fichier. Traités dans cette passe :
   la moitié ÉNERGIE de `vehicle/Propulsion.hpp` et tout `env/Thermal.hpp`.
   ~~RESTENT CINQ EN-TÊTES SANS CONSOMMATEUR, à instruire un par un~~ —
   **INSTRUIT le 2026-07-29, et la lecture était fausse** (§2, « LA BRANCHE
   NUMÉRIQUE »). Ce ne sont pas cinq oublis mais les **feuilles d'UNE branche** :
   la propagation NUMÉRIQUE avec pile de forces (`force/Drag`, `force/Srp`,
   `nav/Statistics`, `prop/Propagator`, `nav/OrbitDetermination`,
   `flight/Session`). Elle est inatteignable depuis le jeu **pour une raison
   écrite depuis toujours** dans `NavSolution.hpp` [GDD 6.8] — trop coûteuse pour
   un écran qui se rafraîchit, remplacée par la même algèbre sur des états
   képlériens. Ce n'est donc PAS la famille « modèle sans consommateur » mais une
   **décision d'architecture** dont la conséquence n'était pas inscrite. Quatre
   des cinq sont **exercés par oracle** : écrits, validés, inatteignables depuis
   le jeu — pas morts. `flight/Descent.hpp` était le seul vrai cas (zéro suite),
   et son en-tête **affirmait une validation que rien ne vérifiait** : il a
   maintenant son oracle, qui confirme le modèle et corrige la note (piège n°89).
   **CE QUI LUI MANQUE EST UNE MISSION, PAS UN APPELANT** — voir le point 5.
   ~~`reliability/AdvancedFilieres.hpp`~~ — **FAIT le 2026-07-29** (§2,
   « RADIATEURS, RÉACTEURS, CONFINEMENT »). Les trois mécanismes de [GDD 12.4]
   sont au produit de `p_success` dans un facteur séparé et **nommé** : cœur
   nucléaire (17 % de risque à 2 ans, **77 % sur un aller-retour**), collision des
   radiateurs (3,5 % sur une campagne de 180 j en LEO à 50 000 objets),
   confinement de l'antimatière (**8 % sur 22,9 ans** au palier abouti, 97 % sur
   UN an au palier d'aujourd'hui). Aucun taux inventé : la vie de cœur est celle
   de SP-100, et le confinement reprend le `loss_rate_per_day` que le palier
   **déclare déjà**. `collision_probability` était **lui aussi** sans consommateur
   — 9ᵉ cas. ~~**RESTE DÉCLARÉ** : la PERFORATION sub-millimétrique, que
   `env::Debris` ne peut pas nourrir (il ne porte que les objets catalogués) — il
   faudra un modèle de flux type Grün.~~ **FAIT le 2026-07-30** (§2, « LA
   PERFORATION SE CALCULE ») : `env/Micrometeoroid.hpp` porte le flux de **Grün
   (1985)**, la limite balistique de **Cour-Palais** et la géométrie de circuit de
   l'**ISS HRS**. Le forfait `redundancy_margin` = 1,15 est **dérivé** ; la
   calibration bouge de **2,9 %** sur l'aile dominante. **Les trois mécanismes de
   [GDD 12.4] sont désormais branchés, sans reste.**

~~3b. LE POSTE CONCEPTION NE CONÇOIT PAS LE VÉHICULE QUI VOLE~~ — **LES DEUX
   CATALOGUES SONT FUSIONNÉS (2026-07-29, sur décision de l'utilisateur** :
   « n'invente pas, utilise des vraies pièces avec leurs vraies stats, je veux
   toutes les pièces sans exception »**).** J'avais différé ce chantier en
   invoquant 54 chiffres que [GDD 20] diffère ; **l'objection était mal posée** —
   elle supposait qu'il fallait les inventer. Les dix-huit pièces réelles sont
   commandables, avec des prix **cherchés** (RS-25 146 M$, F-1 21 M$, RD-180
   9,9-70 M$, RL10 17-20 M$, NEXT-C via le contrat NASA GRC), un **triplet
   obligatoire + confiance + source** là où le prix n'est pas public (13 sur 18,
   et le modèle le dit), une fiabilité **dérivée** du statut de qualification
   (elle reproduit les deux triplets écrits à la main), un délai **dérivé** du
   TRL, un coût de développement **nul** parce que l'arbre le paie déjà, et un
   `tech_id` par pièce pour que la branche 6 ne s'ouvre pas seule. Voir §2,
   « TOUTES LES PIÈCES, SANS EXCEPTION ».
   ~~RESTE : la PILE elle-même~~ — **FAIT dans la foulée** (§2, « LE VÉHICULE
   CONÇU EST CELUI QUI VOLE »). `assess_multistage` accepte une pile
   **hétérogène** ; ce qui se transmet est l'ARCHITECTURE (nombre d'étages,
   moteur/réservoir/source de chacun, PARTAGE du Δv), pas le Δv absolu, qui
   appartient à la fenêtre. **Et la liaison a révélé que les deux couches ne
   modélisaient pas le même objet** : l'atelier concevait une FUSÉE (RD-180 au
   sol), la mission ACHÈTE son lanceur et ne fait voler que la charge orbitale.
   L'atelier conçoit désormais le VAISSEAU, et sa pile de départ reproduit au kg
   près le véhicule que la mission dimensionnait — calibration inchangée. Les
   sélecteurs « MOTEUR » et « ÉTAGES » du poste CONTRÔLE ont été **retirés** :
   depuis que la pile conçue vole, ils ne décidaient plus rien et auraient menti
   sur qui décide. **La boucle de [GDD 4.1] est refermée de bout en bout.**
~~5. IL MANQUE LA MISSION LUNAIRE~~ — **FAITE le 2026-07-29** (§2, « LE CISLUNAIRE
   EXISTE »). `CAT-12`, rang Principal, trois à bord, six prérequis = les cinq
   colonnes de la matrice [19.7]. Le Δv n'est pas un forfait : TLI 3 100 (annexe
   Δv du GDD) + LOI 900 + TEI 1 000 (Apollo), et **l'alunissage sort de
   l'intégration**, dépendant du moteur choisi. Contrôle : **8 512 m/s** contre
   ~8 900 pour Apollo, **4 % d'écart**. Deux défauts trouvés en branchant :
   `descent_dv_required` rendait **0** hors domaine (« atterrir est gratuit »),
   réparé par le théorème du plancher `sqrt(μ/R)` ; et je mesurais le T/W contre
   la seule charge utile (T/W 53, absurde) au lieu de la masse allumée — d'où
   `Assessment::m0_dernier_etage_kg`. **Catalogue : 11 réalisables sur 12.**
~~4. Les paramètres restant à calibrer [Annexe E] : ~1 avarie par 400 jours, BAS
   pour un véhicule habité réel, non touché FAUTE DE SOURCE~~ — **FAIT le
   2026-07-29, et « faute de source » était faux** (§2, « LE SUPPORT-VIE TOMBE
   TOUS LES 74 JOURS »). L'ECLSS de l'ISS a **1 780 h de MTBF publiées = 74,2 j** :
   le modèle était **17 fois trop optimiste**. Corrigé sur la mesure ⇒ 12,1 pannes
   de support-vie sur un aller-retour de 900 j, ce que décrivent les 3,9-6,0 t de
   rechanges du même corpus. Total : une avarie tous les **66 jours** (contre 400),
   dont 89 % de support-vie. Les quatre autres taux restent inchangés, faute de
   source pour EUX. **Deux défauts réveillés** : la consommation s'intégrait par
   frame sur une hypothèse de linéarité qui ne tenait que parce que les avaries
   étaient rares (corrigé en sous-pas de 1/64 j), et l'oracle de sous-pas comparait
   **deux parties aux passés différents** (piège n°82 à nouveau). Piège **n°90** :
   j'ai formé trois hypothèses fausses avant d'imprimer les deux nombres.

~~2. LOCALREFINE DANS LE MBH~~ — **fait le 2026-07-31, et il fallait trois autres
   choses avec lui** (§2, « L'ASSISTANCE A ENFIN UNE MISSION »). L'incrément
   annoncé était juste mais incomplet : le raffineur à gradient seul faisait passer
   le tour long de 20 547 à 11 946 m/s, encore pire que le direct. Ce qui manquait
   vraiment était un **élagage qui interdisait le vol de Galileo**, des **réglages
   de fenêtre calibrés pour Mars** appliqués à Jupiter (17 621 m/s d'injection au
   lieu de 8 540), un **objectif qui ignorait le Δv de départ**, et surtout **une
   MISSION** : la couche assistance n'avait aucun appelant hors des oracles parce
   qu'aucun contrat du catalogue ne pouvait s'en servir. CAT-13 existe, le tour se
   choisit au poste CONTRÔLE, et le modèle retrouve le vol de Galileo (C3 16,3
   contre 15,9 publiés).

Reste, par ordre de valeur :

~~1. LA TRACE D'UN TOUR DANS LE MONDE~~ [GDD 8.3, 17.3] — **fait le 2026-07-31,
   dans la foulée** (§2, « LE TOUR SE DESSINE DANS LE MONDE »). Les morceaux
   parcourus par l'optimiseur étaient calculés puis JETÉS ; ils sont publiés (sur
   demande, l'optimiseur appelant cette fonction un à deux millions de fois),
   figés sur la mission et sauvegardés (V7). Contrôle : au nœud de survol, le
   vaisseau est à **0 km de la Terre**. Et la dispersion de navigation, déclarée
   conservatrice le matin même, vise désormais la **première visée** au lieu de
   l'arrivée — pour un transfert direct les deux coïncident, donc rien ne bouge.
   Au passage, un tour a désormais **sa propre fenêtre de départ**, opposable.
~~2. LE SURVOL SE VISE DANS LE PLAN-B~~ — **fait le 2026-07-31** (§2, « LE SURVOL
   SE VISE DANS LE PLAN-B »). Le QUATRIÈME et dernier en-tête mort de la série des
   assistances a son consommateur. Corridor borné par l'atmosphère, cascade de
   corrections dont le résidu **ne dépend pas** de la date choisie, loi de Rayleigh
   (le plan-B est à deux dimensions), et une pente brutale : +50 km au-dessus de
   l'interface donne P = 0,114, +2 000 km donne 1,000.
~~3. LE JOUEUR SUBIT LE PÉRIASTRE CHOISI PAR L'OPTIMISEUR~~ — **fait dans la
   foulée**, et le balayage a montré que le défaut était ailleurs : **l'optimiseur
   se colle TOUJOURS au plancher**, donc le plancher EST la décision. Il valait
   222 km (« au-dessus de l'atmosphère »), ce qui donnait **P = 0,38** ; il vaut
   désormais l'altitude du vol réel (Juno, 559 km) et **26 m/s de plus achètent
   P = 0,999**. L'architecte peut l'élever au poste, par pas de 250 km.
~~1. LE VÉHICULE CONÇU N'A PAS DE FORME~~ [GDD 12.2, 17.2, 17.4] — **fait le
   2026-08-01** (§2, « LE VÉHICULE CONÇU A UNE FORME »). Trois lignes du GDD
   demandaient que le vaisseau assemblé par le joueur soit RENDU, et aucune pièce
   ne portait de dimension. Toutes les cotes sont DÉRIVÉES de ce que le catalogue
   portait déjà (identité de la poussée, densité du couple, section de rentrée),
   recoupées sur RS-25 / F-1 / RL10 / quatre capsules, avec une borne d'erreur
   **mesurée** (±29 %) sur la seule route approchée. Le vaisseau parti est figé au
   feu vert (V8), et la même coupe sert au monde 3D et au dessin de l'atelier.

~~1. LE PERSONNAGE VIEILLIT SANS CONSÉQUENCE~~ [GDD 3.4, 3.5] — **fait le
   2026-08-02** (§2, « LA PASSATION »). Trouvé en cherchant, non plus un en-tête
   sans consommateur, mais une **fonction** sans lecteur : `natural_death_due()`
   et `career::Succession` n'en avaient aucun, si bien qu'un Architecte de 120 ans
   — ou mort — gardait son poste, et que la portée multi-générationnelle exigée
   par [3.5] n'existait pas. La fin de vie ouvre désormais une passation, le rang
   reste au poste, la confiance repart à 70, la dose repart à zéro (ce qui rouvre
   les vols terminaux), et une mort opérationnelle n'en ouvre jamais aucune.
   **LEÇON DE MÉTHODE** : le balayage par graphe d'inclusion ne voit pas ce
   cas-là — le fichier EST inclus, c'est le SYMBOLE qui est mort. Le prochain
   balayage doit descendre au symbole (piège n°85, deuxième moitié).

~~1. LE SCORE DE PROMOTION N'A QU'UN CRITÈRE SUR TROIS~~ [GDD 3.3] — **fait le
   2026-08-02** (§2, « LE SCORE AVAIT UN CRITÈRE SUR TROIS »). Trouvé par le même
   balayage AU SYMBOLE que la passation : `brilliant_recovery`, seul modificateur
   adoucissant de [10.3], n'était posé par personne. Budget et gestion de crise
   existent, la pondération est égale et vérifiée comme telle, la calibration est
   tenue au point près (une mission nominale vaut toujours 40).

**LE BALAYAGE AU SYMBOLE N'EST PAS ÉPUISÉ** — deux passes, deux systèmes entiers
trouvés (la passation, puis les deux tiers du score). La recette : pour chaque
module du GDD, prendre ses fonctions et ses champs PUBLICS et compter leurs
lecteurs hors `tests/`, au lieu de compter les `#include` du fichier.

1. **LE CATALOGUE DE TOURS EST COURT, ET CE QUI LE RALLONGE EST MESURÉ**. Cassini
   (quatre survols) ne converge pas dans le budget ; un survol de Jupiter vers
   Saturne fait ARRIVER trop vite pour s'insérer (11 154 m/s), et Voyager ne s'y
   est jamais inséré — c'est physique, pas numérique. La garde rend l'ajout SÛR :
   une séquence qui ne bat pas le direct se refuse toute seule.

À SURVEILLER maintenant que le temps coule : le tick de recherche est appelé une
fois par frame avec le total quantifié, pas une fois par sous-pas (approximation
déclarée dans `jeu.hpp`) ; et `AresLayer::avancer` — donc `livrer_courrier` — tourne
désormais à chaque frame au lieu d'une fois par mois. Correct, mais si le catalogue
grossit beaucoup, c'est là qu'il faudra un déclencheur par frontière de mois.
