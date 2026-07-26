# Architecture logicielle ARES — Répartition C++ / Blueprint

> Document de référence technique, complément au GDD ARES v1.2.
> Objet : figer quel système vit en **C++**, lequel en **Blueprint**, lequel est **hybride**, et pourquoi.

---

## 1. Principe de découpe

Quatre contraintes du GDD déterminent la frontière presque à elles seules :

| # | Contrainte | Source | Conséquence |
| :-- | :-- | :-- | :-- |
| 1 | Le joueur écrit du **vrai C++ compilé** | Décisions 1 & 5, §15 | Toolchain, sandbox et API en C++ natif |
| 2 | Réalisme numérique/physique **maximal** | §6–7 | Noyau de calcul en C++ |
| 3 | Système solaire **1:1 en double précision** | §17.3, §18 | Coordonnées monde impossibles en Blueprint (`float`) |
| 4 | Sauvegardes **déterministes rejouables** | §18 | Tout *stepping* d'état reste déterministe → C++ |

**Règle de frontière.** Le calcul, la toolchain et les coordonnées → **C++**. Les données (pièces, contrats, modules, fiches de fiabilité, seuils économiques) → **DataAssets / DataTables**, calibrables sans recompiler. L'orchestration lente et l'UI → **Blueprint**.

---

## 2. Verdict d'optimalité

La découpe est **optimale pour les contraintes du GDD** : elle place dans C++ tout ce qui est immuable (calcul, toolchain, coordonnées, déterminisme) et garde éditable tout ce qui se recalibre souvent. Le critère de frontière est le bon.

**Point de vigilance** — la découpe est juste ; le vrai risque du projet n'est pas la frontière mais la **capacité d'exécution C++** : toolchain embarquée, sandbox par processus et noyau n-corps double précision sont des chantiers lourds à budgéter comme tels (cohérent avec §18, « plusieurs centaines de Mo » et isolation par processus).

---

## 3. Classification par système

### 3.1 Noyau — **C++** (non négociable)

| Système | Raison |
| :-- | :-- |
| Intégrateur n-corps + perturbations (J2) | Boucle numérique lourde et déterministe |
| Astrodynamique (Tsiolkovsky, Lambert, Hohmann, assistances) | Maths de précision en masse ; back-end de `ares::sol` |
| Coordonnées monde double précision + rebasing d'origine | Transforms BP en `float` ; repère 1:1 hors de portée de BP |
| Relativité restreinte (γ, τ = ∫dt/γ, deux horloges) | Intégration numérique du noyau physique |
| Navigation & suivi de trajectoire (covariance 1σ/3σ, corrections) | Estimation statistique couplée au solveur |
| Rentrée / EDL | Intégration fine à seuils stricts |
| Toolchain embarquée (compilateur) | Embarquer/piloter un compilateur : impossible en BP |
| Bac à sable d'exécution (processus séparé, limites, signaux) | BP ne gère ni processus ni signaux |
| API `ares::sol` / `ares::vol` | Par définition : en-têtes contre lesquels le joueur écrit |
| Banc d'essai & certification (rejeu + domaine de validité) | Réutilise noyau + isolation |
| Déterminisme / journalisation & rejeu au rechargement | Contrôle serré de la sim et du RNG |
| Horloge de sim & pas d'intégration | Pilote l'intégrateur ; sync date réelle au démarrage |
| RNG déterministe & planification des anomalies | Reproductibilité des sauvegardes |
| Génération procédurale corps/terrains/ciels (+ matériaux) | Génération custom à l'échelle, pilotée éphémérides |
| Rendu procédural des véhicules (depuis ship builder) | Mesh généré depuis les données de l'éditeur |
| LOD de rendu par taille apparente | Par objet, chaque frame, couplé au rebasing |

### 3.2 Systèmes **hybrides** (cœur C++, périphérie BP/data)

| Système | Partie C++ | Partie Blueprint / data |
| :-- | :-- | :-- |
| Caméra libre continue | Coords double précision, lecture seule sur la sim | Ressenti, mapping des entrées |
| Débris orbitaux | Propagation + proba de collision (nombreux objets) | Règles d'apparition, effets |
| Simulation autonome pendant l'absence (§9.3) | Ordonnanceur qui avance l'état (déterministe) | Règles métier, contenu |
| Ship builder | Calcul masse/centrage/structure + géométrie | Éditeur d'assemblage (UMG) |
| Base de données de fiabilité (§12.3) | Moteur contextuel (modificateurs, propagation, incertitude) | Fiches en DataAssets |
| Évaluation du graphe (mode Normal) | Runtime/compilation (voir §4) | Éditeur de nœuds (canvas UMG/Slate) |

### 3.3 Périphérie — **Blueprint + DataAssets**

| Système | Raison |
| :-- | :-- |
| Arbre techno & déblocage (rang/TRL/budget/infra) | Règles data-driven, pas de calcul lourd |
| Économie (budget, réserve, confiance ARES, paliers) | Bookkeeping à tick lent, seuils sans cesse retunés |
| Carrière (rang, score de promotion, passation) | Règles et transitions d'état |
| Échelle de gravité & conséquences (§10.3) | Table de pénalités / effets |
| Missions & contrats (types, filtrage, déblocage) | Contenu + logique de filtrage |
| Modules Novellus & effets | Ensemble fini authoré (10) ; effets = logique de jeu |
| HUD, télémétrie, schémas, superpositions monde | Présentation, alimentée par des fournisseurs C++ |
| Terminal/éditeur de code (UI) | Édition + coloration sur gros buffer → **Slate/C++** ; panneaux annexes → UMG |
| Son (silence spatial, ambiances, délai comm audible) | Logique audio événementielle (MetaSounds) |
| Ressources vitales en mission (O2/eau/nourriture) | Consommation à tick lent |

---

## 4. Trois optimisations à intégrer

1. **Stepping déterministe en C++, même pour l'économie et l'absence.**
   Blueprint ne sert qu'à la *config* et aux *hooks d'événements* ; il ne fait jamais avancer l'état. Sinon les sauvegardes rejouables (§18) et les missions relativistes multi-décennies (§9.3, §14.4) deviennent fragiles. Ordonner les opérations et éviter le float non maîtrisé côté bookkeeping sensible.

2. **Le mode Normal compile le graphe vers le *même* C++ et passe par la toolchain / sandbox / banc identiques.**
   L'« équivalence stricte » nœuds ↔ API (§2.2) devient **structurelle** au lieu d'être maintenue à la main, et il n'existe qu'**un seul chemin d'exécution** à sécuriser au lieu de deux. Le graphe n'est alors qu'un générateur de code ; l'éditeur de nœuds reste de l'UI (UMG/Slate).

3. **Frontière disciplinée C++ ↔ Blueprint.**
   Noyau C++ exposé via `UFUNCTION` / `UPROPERTY` ; le Blueprint *pilote* et *affiche*, il ne *recalcule* jamais. Toute donnée calibrable vit en DataAsset/DataTable.

---

## 5. Règle de frontière C++ ↔ Blueprint

```
        ┌──────────────────────────────────────────────┐
        │  NOYAU C++ (déterministe, double précision)   │
        │  physique · astrodynamique · toolchain ·      │
        │  sandbox · API ares:: · banc d'essai · rendu  │
        │  procédural · LOD · RNG · ordonnanceur        │
        └───────────────┬──────────────────────────────┘
                        │  UFUNCTION / UPROPERTY
                        │  (lecture seule sur la sim pour le rendu/UI)
        ┌───────────────┴──────────────────────────────┐
        │  BLUEPRINT (orchestration lente) + UMG/Slate  │
        │  progression · économie · carrière · missions │
        │  · modules · HUD · télémétrie · audio         │
        └───────────────┬──────────────────────────────┘
                        │  lit / écrit
        ┌───────────────┴──────────────────────────────┐
        │  DONNÉES — DataAssets / DataTables            │
        │  pièces · contrats · modules · fiches de      │
        │  fiabilité · seuils économiques · barèmes     │
        └──────────────────────────────────────────────┘
```

**Invariant :** aucun flux ne remonte — l'UI et le Blueprint n'altèrent jamais l'état de la simulation déterministe (§18, §17.4). Un rechargement rejoue à l'identique.

---

*Complément au GDD ARES v1.2 — répartition C++ / Blueprint.*
