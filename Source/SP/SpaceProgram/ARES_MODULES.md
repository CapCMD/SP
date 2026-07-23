# ARES — correspondance carte mentale → code

> Où vit chaque module de la carte mentale de développement (GDD v1.1).
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
