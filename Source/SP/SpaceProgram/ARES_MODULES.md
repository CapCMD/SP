# ARES — correspondance carte mentale → code

> Où vit chaque module de la carte mentale de développement (GDD v1.2 ;
> copie autoritaire : `SP/GDD_ARES.md`).
> Racines d'include : `astro_core/include` (physique pure) et `mission/include` (gestion).
> Tout est C++ pur, sans dépendance UnrealEngine — la frontière UE5 est `UEBridge/`.

## Déjà en place avant cette passe (migration Space Program)

| Carte | Fichiers (`fen/...`) |
| :--- | :--- |
| M0 Fondations | `core/Vec3.hpp`, `core/Matrix.hpp`, `core/Epoch.hpp`, `core/Units.hpp`, `core/Constants.hpp`, `core/Rng.hpp`, `core/State.hpp` |
| M1.a États & solveurs | `astro/Kepler.hpp`, `astro/Elements.hpp`, `astro/Lambert.hpp`, `astro/Transfers.hpp`, `astro/Flyby.hpp`, `astro/Mga.hpp`, `astro/Mga1Dsm.hpp`, `astro/BPlane.hpp`, `astro/Porkchop.hpp`, `astro/LocalRefine.hpp` |
| M1.b Forces | `force/Forces.hpp` |
| M1.c/d Intégrateurs & propagateur | `prop/Integrator.hpp`, `prop/Ias15.hpp`, `prop/Propagator.hpp` |
| M1.e Éphémérides | `ephem/Ephemeris.hpp` |
| M1.f Budget de masse | `vehicle/Vehicle.hpp` (Tsiolkovsky, étagement, sizing inverse) |
| M1.g Navigation & incertitude | `nav/OrbitDetermination.hpp`, `nav/Tracking.hpp`, `nav/Statistics.hpp`, `nav/Gates.hpp` |
| Exécution de vol | `flight/FlightPlan.hpp`, `flight/Session.hpp`, `flight/Descent.hpp`, `io/Fpl.hpp` |
| M3 embryon (coûts/contrats) | `mission/Program.hpp` |

## Ajouté par cette passe

| Carte | GDD | Fichier |
| :--- | :--- | :--- |
| M1.h Relativité restreinte | 6.7, 19.4 | `rel/Relativity.hpp` — γ, τ=∫dt/γ, fusée relativiste, seuil β≥0.1, DualClock |
| M1.i Thermique | 6.5 | `env/Thermal.hpp` — Stefan-Boltzmann, RadiatorSizing, chaleur réacteur, ThermalBudget |
| M1.i Météo spatiale | 7.7 | `env/SpaceWeather.hpp` — cycle 11 ans, taux SPE, traînée, modulation GCR |
| M1.i Radiations | 6.6, 7.7 | `env/Radiation.hpp` — GCR/SPE/Van Allen, blindage massique, DoseAccumulator, source réacteur |
| M4.a Filières propulsion | 5.12, 6.2–6.4 | `vehicle/Propulsion.hpp` — paliers 0–8, F=2ηP/ve, régimes, radiateurs induits |
| M4.b Base de fiabilité | 12.3–12.5 | `reliability/Reliability.hpp` — fiche tracée, confiance A–D, historique immuable, modificateurs, rollup RBD |
| M5.a Arbre techno | 5 | `tech/TechTree.hpp` — 6 branches, DAG, TRL, ResearchQueue par rang, requalification |
| M5.b Verrou le plus fort | 5.4, 19.2 | `tech/Unlock.hpp` — IGate 4 axes, IInfrastructureProvider (interface) |
| M5.c Carrière | 3 | `career/Career.hpp` — 5 rangs, score, personnage/vieillissement, carnet, passation |
| M3.e Économie | 13, 4.4 | `economy/Economy.hpp` — trésorerie, paliers d'alerte, coûts fixes, sites, 3 niveaux de ressources |
| M3.f Novellus | 11 | `station/Novellus.hpp` — 10 modules, 4 paliers, effets, implémente IInfrastructureProvider |
| M3.b Gravité & conséquences | 10.3–10.4 | `mission/Severity.hpp` — 5 niveaux, modificateurs de palier, triple lecture |
| M3.c Événements | 9.4 | `mission/Events.hpp` — bibliothèque calibrée, Poisson par substream Rng |
| M3.d Habité | 9, 13.4 | `mission/Crew.hpp` — ressources vitales + recyclage, délai lumière, une mission vécue |
| M3.a Cycle de mission | 4.1–4.2 | `mission/MissionFsm.hpp` — FSM stricte, contrat mail, catalogue verrouillé |
| M7 Sauvegarde | 18 | `save/Save.hpp` — archive binaire versionnée, ISerializable, StateHasher FNV-1a |
| M2 Temps & déterminisme | 14 | `game/GameClock.hpp` — WorldEpoch, accélération à paliers, sous-pas fixes |
| M2/M7 Assemblage | — | `game/GameState.hpp` — état complet, tick monde, ConsequenceEngine, save/load/hash |

## Ajouté par la relecture du GDD (2026-07-24)

Relecture chapitre par chapitre du GDD v1.1 contre le code : **deux systèmes
exigés par le GDD n'existaient pas**. Les voici.

| Carte | GDD | Fichier |
| :--- | :--- | :--- |
| M1.j Débris orbitaux | 7.8, 10.5 | `env/Debris.hpp` — atmosphère exponentielle par morceaux, durée de vie orbitale sous traînée, modèle de rupture NASA `N=k·M^0.75·Lc^-1.71`, couloirs, densité spatiale, probabilité de collision poissonienne |
| M6 Boîte mail ARES | 4.1, 4.2, 10.2, 15.3 | `mission/Mail.hpp` — `MailInbox`, notification de contrat, `deliver_unlocked_contracts` |

**Pourquoi ils manquaient, et ce que ça cassait :**

- `SpaceWeather::atmo_density_factor` annonçait déjà « alimente le modèle de
  débris [GDD 7.8] » — le modèle n'avait jamais été écrit. `Severity` exposait
  un drapeau `massive_debris` que **personne ne pouvait renseigner autrement
  qu'à la main**, c'est-à-dire exactement le « malus abstrait » que le GDD
  interdit. Désormais la fragmentation est COMPTÉE et le modificateur en est
  DÉDUIT (`GameState::apply_anomaly`).
- `MissionContract::mail_body` renvoyait à un « MailInbox (M6) » inexistant :
  le catalogue était donc de fait consultable librement, contraire à [GDD 10.2]
  (« les missions sont EXCLUSIVEMENT proposées par ARES par mail »). Un contrat
  n'est maintenant visible que s'il a été NOTIFIÉ, et la notification n'a lieu
  qu'au franchissement des quatre verrous [GDD 4.2].

Les deux états sont sérialisés dans `GameState::save/load` et couverts par le
hash : la pollution d'un couloir et la mémoire des contrats survivent au
rechargement (sinon un échec grave s'effacerait — contraire à [GDD 10.4]).

Oracles : `tests/test_gdd_manques.cpp` (57).

## Audit FIN de la physique (2026-07-24)

Le premier audit était au niveau des FICHIERS (« un module existe-t-il pour le
chapitre X ? »). Celui-ci regarde DEDANS. Trois trous, tous dans la physique :

| Carte | GDD | Fichier |
| :--- | :--- | :--- |
| M1.b Traînée atmosphérique | 7.1, 7.7 | `force/Drag.hpp` — a = −½ρ\|v_rel\|v_rel·CdA/m, **vitesse relative à l'atmosphère en rotation** |
| M1.b Pression de radiation solaire | 7.1, 7.5, 8.2 | `force/Srp.hpp` — P0 = S0/c, loi en 1/d², **ombre CONIQUE** (umbra / pénombre / transit annulaire) |
| M1.k Atmosphères | 7.1, 7.6, 7.7, 7.8 | `env/Atmosphere.hpp` — exponentielle par morceaux, Terre + Mars, hauteur d'échelle **dérivée de la table** |
| M1.l Rentrée, EDL, aérofreinage | **7.6**, 5.11, 8.5 | `flight/Reentry.hpp` — Allen–Eggers, Sutton–Graves, corridor d'entrée, intégration RK4 de vérité, aérocapture |

**Ce qui manquait vraiment, et ce que ça cassait :**

- `force/Forces.hpp` n'avait que gravité centrale, troisième corps, J2 et poussée
  finie : **aucune traînée, aucune SRP**. Conséquence : une orbite basse ne
  décroissait jamais, l'activité solaire n'avait aucun effet sur la durée de vie
  orbitale, et l'aérofreinage était impossible. La SRP est par ailleurs la
  première cause de dérive après la gravité sur une croisière : sans elle, le
  chapitre 8 (écart nominal / estimé / réel) n'avait pas de cause physique.
- `flight/Descent.hpp` traite explicitement l'atterrissage **sans atmosphère**.
  Le chapitre 7.6 — rentrée, aérofreinage, EDL — n'avait donc **aucune
  implémentation**, alors que [GDD 8.5] lui assigne les seuils les plus stricts
  du jeu.

**Deux niveaux qui doivent s'accorder** (doctrine de `Reentry.hpp`) : les formes
closes servent à DIMENSIONNER, l'intégration RK4 est la VÉRITÉ, et les oracles
mesurent leur écart. Là où l'hypothèse d'Allen–Eggers tient (entrée raide),
l'accord est meilleur que 12 % sur le pic de g. Là où elle ne tient pas (entrée
rasante super-circulaire, la trajectoire s'aplatit avant le pic), le modèle le
DÉCLARE (`constant_gamma_ok = false`) et la forme close reste une **borne
supérieure** — se tromper dans le sens sévère, jamais dans l'autre [GDD 12.5].

Oracles : `tests/test_reentry_perturb.cpp` (108).

## Passe de CONTENU — ce que le GDD nomme (2026-07-24)

Cadrage retenu : remplir tout ce que le CORPS du GDD nomme, rien de ce que son
chapitre 20 diffère. Trois manques comblés, tous dans des tables que le GDD
écrit noir sur blanc :

| Où | GDD | Ce qui manquait |
| :--- | :--- | :--- |
| `vehicle/Propulsion.hpp` | 6.4 | Le tableau était **agrégé** : « Chimique » couvrait solide et liquide, « Électrique » couvrait Hall et grille — alors que le GDD les sépare parce que leurs fourchettes ne se recouvrent pas. Désormais 8 filières, une par ligne, chacune avec son *facteur limitant dominant*. |
| `vehicle/Propulsion.hpp` | 5.12.1, 5.12.3, 5.12.5-8 | Les **sources d'ÉNERGIE pures** (solaire, RTG, fission) n'existaient nulle part : `prop_class` renvoyait `nullptr` pour les paliers 1, 2 et 4. Or 5.12.1 fait de la distinction énergie/propulsion un invariant. Ajouté : puissance massique, rendement, décroissance du Pu-238 (demi-vie 87,7 ans), loi en 1/d² du solaire, masse de source au budget. |
| `station/Novellus.hpp` | 11.2, 11.6 | Trois modules sur dix n'avaient **aucun effet** — noyau, nœud d'amarrage, module énergétique, c'est-à-dire les trois OBLIGATOIRES du palier 1. La station pouvait donc être opérationnelle sans centre nerveux, ce que 11.2 interdit. Ajouté aussi la **demande** électrique par module : sans elle, « grand facteur limitant de croissance » ne limitait rien. |

Puis, dans la même passe :

| Où | GDD | Contenu |
| :--- | :--- | :--- |
| `app/ares.hpp` `seed_arbre` | 5.7–5.13 | **66 nœuds** couvrant toutes les sous-branches nommées ; les **10 transverses** de 5.13 sont RÉPARTIES sur quatre branches et bloquent réellement (l'exemple littéral du GDD — NEP mégawatt bloquée par thermique/radiateurs et matériaux haute température — est câblé tel quel) |
| `app/ares.hpp` `seed_catalogue` | 10.1, 10.2 | **11 entrées**, du satellite LEO à la mission relativiste ; chacune porte son corps de mail, puisqu'un contrat n'existe que porté par un courrier |
| `vehicle/PartsCatalog.hpp` | 12.1, 12.5 | **18 moteurs, 5 réservoirs, 5 capsules** — chacun avec sa LIGNÉE réelle, sa source, sa confiance A–D, son statut de qualification et son incertitude |

Puis, deux systèmes que le GDD nomme mais qui étaient incomplets ou absents :

| Où | GDD | Ce qui manquait |
| :--- | :--- | :--- |
| `economy/Economy.hpp` `LaunchSite` | 13.3 | La géographie était un DÉCOR : `reachable()` n'imposait que « inclinaison ≥ latitude » et ignorait le couloir d'azimut que le GDD nomme. Ajouté : la trigonométrie sphérique exacte `cos(i)=sin(β)·cos(φ)`, la vérification du couloir d'azimut (pas de tir polaire depuis la Floride), et l'assist de rotation `ω·R·cos(φ)·sin(β)` — c'est lui, ~463 m/s à Kourou, qui fait « GTO roi », pas un bonus arbitraire. |
| `reliability/AdvancedFilieres.hpp` | 12.4 | Les mécanismes de dégradation NOMMÉS par le GDD n'existaient pas (le module de fiabilité n'avait que des modificateurs génériques). Ajouté, chacun avec sa physique : vieillissement des cœurs (burnup + calendaire, NTP plus fragile), érosion des radiateurs **branchée sur `env/Debris`** (un grand radiateur NEP/fusion est plus vulnérable — le point précis du GDD), et confinement antimatière modélisé en SURVIE poissonienne (perte = catastrophe [19.3]). |

Puis l'atelier d'assemblage [GDD 12.2] et l'acceptation de contrat [GDD 4.1] :

| Où | GDD | Contenu |
| :--- | :--- | :--- |
| `app/vehicle_design.hpp` | 12.2, 6.1 | L'ATELIER, côté modèle : le joueur choisit pièces + partage du Δv, `evaluate_design` recalcule les masses via `size_multistage_for_dv` (point fixe de Tsiolkovsky). N'optimise RIEN [anti-feature 1.5] ; signale l'infaisable (non-convergence, étage du bas en régime continu qui ne décolle pas [6.3]). Vue = poste CONCEPTION (`UEBridge/SPHud.cpp`). |
| `app/session.hpp` `accepter_contrat` | 4.1, 10.2 | Accepter un contrat NOTIFIÉ crée une `mission::Mission` en phase PRÉREQUIS et répond au mail. Refuse un contrat non notifié (10.2), déjà accepté, ou en double. Vue = poste PLANIFICATION. |
| `mission/MissionLoop.hpp` + `app/session.hpp` (drive) | **4.1** | LA BOUCLE : gates réels par transition (pas de fenêtre sans conception VIABLE via `assess`, pas de lancement sans qualification), commit financier irréversible au feu vert, issue du vol DÉTERMINISTE (`fly_mission`, tirée contre la P(succès), graine agence+mission), conséquences à triple lecture (`GameState::apply_anomaly`). Vue = poste CONTROLE. Contrats dotés de leurs termes physiques (`seed_catalogue`, budgets calés pour qu'un plan de départ soit viable). Oracles : `tests/test_mission_loop.cpp` (**43**). |

Oracles : `tests/test_contenu_gdd.cpp` (**595**, dont l'atelier 12.2) +
`tests/test_session.cpp` (accept-contrat) — ils vérifient la conformité aux
TABLES du GDD, pas des valeurs inventées.

**Deux enseignements des oracles, à ne pas perdre :**

1. **Le rang ne descend jamais en remontant l'arbre** [GDD 19.2]. L'oracle a
   attrapé une inversion : `amarrage_habite` (disponible au départ selon 5.6)
   dépendait du rendez-vous AUTOMATISÉ, qui est une techno future. Or l'équipage
   a amarré à la main (Gemini, 1966) bien avant que l'automatisme soit robuste.
2. **Entre une donnée mesurée et un ordre de grandeur rédactionnel, la donnée
   gagne** [GDD 12.3.1]. Deux pièces réelles sortent des bornes littérales de la
   colonne « poussée » de 6.4 : le SPT-100 (83 mN contre 0,1 N annoncé pour le
   Hall) et le NERVA NRX (334 kN contre 10–100 kN pour le NTP). Le GDD écrit
   « poussée (**ordre**) » : la colonne Isp reste une borne dure, la colonne
   poussée tolère une décade, et les débordements sont SIGNALÉS
   (`engine_outside_literal_thrust_band`) au lieu d'être maquillés.

## Invariants gardés par le code (à tester en CI)

- `GameState::hash()` identique quelle que soit la découpe du temps réel (sous-pas fixes).
- `save → load → save` byte-identique (vérifié par `state_hash`).
- Fiabilité : jamais la nominale brute (evaluate passe par les modificateurs) ;
  historique en append-only ; base qui rejette les fiches sans provenance.
- Déblocage : les 4 axes évalués, le verrou le plus fort affiché ; le rang ne
  remplace jamais la science.
- Relativité : aucun effet sous β = 0.1 ; tout est calculé, rien n'est posé.
- Modes : Normal→Pro unidirectionnel ; la physique ne change jamais.

Harnais de non-régression hors moteur : `scratchpad/compile_check.cpp`
(à promouvoir en `tests/` avec les golden tests de la Partie 3 de la carte).

## Passage au GDD v1.2 (2026-07-24/25)

Le GDD est passé en **v1.2** (copie autoritaire : `SP/GDD_ARES.md`). Traité :

| GDD v1.2 | Module | Ce qui a changé |
| :--- | :--- | :--- |
| 13 économie Md€ | `economy/Economy.hpp` `AgencyFinance` | L'économie n'est plus une trésorerie abstraite en M$ mais un budget d'agence à l'échelle réelle (M€). Deux jauges (trésorerie/réserve), recettes conditionnées à l'activité, invariant *garanti < coûts fixes* (pression d'inactivité), chaîne graduée avertissement→gel→mise à l'écart→licenciement. Autorité de l'économie native (le vieux `Treasury` M$ est relégué). Oracles : `tests/test_economie_v12.cpp` (44). |
| 13.4 confiance = filtre | `economy/Economy.hpp` (`access_band`) | La confiance 0-100 (déjà stockée) devient un FILTRE d'éligibilité, croisé avec le rang : `Session::accepter_contrat` refuse un contrat habité sous 60, tout contrat sous 20. |
| 6.7.4 verrou aller-retour | `rel/Relativity.hpp` | `round_trip_mass_ratio` = ratio unitaire^4 (quatre poussées). Un aller-retour à β=0,9 → ~4,7×10⁷, hors de portée sans ravitaillement. |
| 5.12.12 production antimatière | `rel/Relativity.hpp` (`AntimatterProduction`) | La chaîne masse↔β : `antimatter_needed_g`, `beta_from_antimatter`. Quelques grammes → β minuscule ; viser β=0,3 → des tonnes. Le débit/confinement est le vrai levier d'équilibrage. |
| 15.5 banc d'essai | `code/CodeQualification.hpp` | Le banc = un MODÈLE avec domaine de validité (pas un oracle). Rassure sans garantir (couverture < 1). Hors domaine = comportement non couvert = anomalie. Slice modèle ; l'API `ares::sol`/`ares::vol`, l'éditeur de graphe et la toolchain embarquée restent à faire. Oracles : `tests/test_code_qualif.cpp` (22). |
| 11.1 Vigie | `app/postes.hpp`, HUD | La station = **Novellus** ; le poste de l'architecte = **Vigie**. |

**Décision utilisateur** : tout reste en 3D — la marche 1re personne dans l'ISS
est conservée (les postes SONT le terminal, atteints en 3D), malgré la lettre de
1.5/9.1 ; pas d'interface terminale pure.
