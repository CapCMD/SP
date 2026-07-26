# GAME DESIGN DOCUMENT — ARES (édition concise)

> Simulateur d'architecture de mission spatiale à réalisme physique, opérationnel et mathématique maximal.

**Version 1.2** — document de référence, bureau d'études. En cas de divergence, la formulation v1.2 prévaut. Cette édition condense la version complète sans perte d'élément de design ; seul le bookkeeping de migration inter-versions (Annexes C/D, prose de consolidation) en est absent.

## Journal de décisions v1.2

| # | Domaine | Décision |
| :---- | :---- | :---- |
| 1 | Terminal | Le joueur écrit du **vrai C++**, compilé et exécuté par une toolchain embarquée |
| 2 | Terminal | Deux surfaces d'API distinctes : **sol** (analyse) et **vol** (logiciel embarqué) |
| 3 | Terminal | Tout code de vol passe par un **banc d'essai obligatoire** avant téléversement |
| 4 | Terminal | Le banc d'essai possède un **domaine de validité** ; il réduit le risque sans l'annuler |
| 5 | Modes | Normal = **programmation graphique par nœuds** ; Pro = **C++ écrit à la main** |
| 6 | Carrière | Le successeur **conserve le rang** atteint |
| 7 | Carrière | La **confiance personnelle** repart à 70 ; les **effets programmatiques persistent** |
| 8 | Carrière | Score de promotion à **pondération égale** des trois critères |
| 9 | Relativité | Aucune cible imposée : **seule la cohérence scientifique** gouverne |
| 10 | Relativité | β **découle de l'architecture** ; l'écart d'âge est un résultat possible, non un jalon |
| 11 | Relativité | Le **modèle de production d'antimatière** est le paramètre d'équilibrage réel |
| 12 | Missions | Les **missions longues vécues** sont réservées à la fin de carrière |
| 13 | Missions | ARES **fonctionne en autonomie** pendant l'absence du joueur |
| 14 | Économie | Budget annuel **~100 Md€** (ordre du cumul spatial mondial) |
| 15 | Économie | Confiance ARES sur **0-100**, départ à **70**, avec barème de récupération |
| 16 | Économie | Les paliers de trésorerie portent sur le **fonds de réserve** |
| 17 | Cadre | Départ **2026**, techno réelle ; **Novellus est l'ISS de cette réalité** |
| 18 | Périmètre | **Multijoueur hors v1** ; **vol habité vécu inclus**, *commande* médiée par le terminal (observation par caméra libre, voir décision 19) |
| 19 | Environnement 3D | **Scène unique persistante = le système solaire 1:1** ; **caméra libre** continue ; la « carte » est un cadrage lointain de cette même scène, pas un écran séparé |

---

# 1. Vision et identité

## 1.1 Pitch
Diriger une agence spatiale et concevoir des missions physiquement exactes, de l'orbite basse au régime relativiste, sur une échelle temporelle couvrant plusieurs vies de personnage. Le jeu se joue dans les interfaces, le calcul et la trajectoire, pas au manche.

## 1.2 Piliers de design
- **Réalisme physique non négociable** : n-corps, perturbations (dont J2), Tsiolkovsky, budgets de masse/delta-v, énergie, thermique, radiations, rentrée, délais, marges, conséquences.
- **Progression par la connaissance et la maturité**, pas par des points abstraits.
- **Temporalité réelle à la création, indépendante ensuite** : l'état du système solaire est synchronisé sur la date/heure réelle au seul lancement ; le temps devient ensuite pilotable.
- **Rigueur méthodologique** : toute techno est hiérarchisée (maturité, prérequis, incertitudes, domaine de validité) ; toute approximation moteur est documentée.

## 1.3 Ton et posture
Sérieux d'ingénierie, sans fantaisie ni raccourci arcade. L'échec est instructif, jamais punitif gratuitement.

## 1.4 Cadre fictionnel
Uchronie **strictement organisationnelle** : même physique et mêmes technologies que le réel, mais une **agence unique**, ARES, centralise tous les grands programmes (nulle rivalité inter-agences). Époque de départ : **2026**. **Novellus est une station** spatiale mature héritée — l'équivalent de l'ISS de cette réalité — déjà habitée en permanence au démarrage. Le poste de travail du joueur y est le **module Vigie**.

## 1.5 Anti-features (ce qu'ARES n'est pas)
- pas de pilotage manuel « stick » temps réel du véhicule ;
- pas de « moteur magique » contournant masse, énergie, chaleur, radiations, fiabilité ou budget ;
- pas de scénario dramatique scripté lourd ;
- pas d'automatisation par scripts réutilisables des calculs (voir 2.4 et 15.6) ;
- pas de raccourci arcade sur la physique : toute simplification est une approximation *documentée*.

---

# 2. Modes de difficulté

## 2.1 Principe
Le mode ne change que le **niveau d'assistance au calcul**, jamais le contenu accessible : aucune mission, techno ou zone n'est réservée à un mode.

## 2.2 Tableau des modes
| Mode | Expression du calcul | Assistance | Interface | Public visé |
| :---- | :---- | :---- | :---- | :---- |
| **Normal** | **Programmation graphique par nœuds** : le joueur assemble un graphe (éphéméride → solveur → budget de masse → journal) dont chaque nœud expose ses entrées et sorties typées | Rappels de formules, nœuds préconstruits documentés, tableaux recalculés à chaque modification, validation de typage | Terminal + éditeur de graphe + aides visuelles | Joueurs découvrant l'astrodynamique et l'architecture spatiale |
| **Pro** | **C++ écrit à la main** contre l'API ARES, compilé par la toolchain embarquée | Aucune. Ni rappel de formule, ni nœud préconstruit, ni recalcul automatique. Seules subsistent la compilation et les étapes de validation réalistes | Terminal technique pur | Joueurs avancés, profils ingénieur, passionnés de calcul manuel |

L'équivalence est stricte : les nœuds Normal exposent exactement les fonctions de l'API C++.

## 2.3 Réversibilité du choix
Le passage Normal → Pro est possible et **unidirectionnel** ; les graphes existants sont archivés en lecture seule dans le carnet (consultables, non exécutables), le joueur devant réécrire en C++. Cette perte est intentionnelle.

## 2.4 Absence de bibliothèque réutilisable
Pas de macros, de commandes sauvegardées ni de catalogue de procédures prêtes à rejouer : chaque analyse se reconstruit. **Posture de conception, non contrainte technique** — en Pro, le joueur garde ses fichiers sources sur son disque ; le jeu ne l'en empêche pas, il ne l'y assiste pas. Les **nœuds préconstruits** de Normal sont des **primitives** (équivalent visuel des fonctions d'API), pas des procédures assemblées rejouables : dans les deux modes, c'est l'assemblage du raisonnement qui reste à refaire.

---

# 3. Joueur, carrière et temps propre

## 3.1 Statut du personnage
L'Architecte Mission décide *comment* concevoir et conduire, dans des enveloppes budgétaires imposées par ARES. Il ne fixe pas le budget.

## 3.2 Échelle de carrière
1. **Stagiaire** — missions robotiques simples ; validation hiérarchique obligatoire.
2. **Architecte Junior** — satellites, sondes, rovers simples, premières missions orbitales complexes, premiers vols habités en orbite basse.
3. **Architecte Senior** — missions interplanétaires robotiques, opérations habitées orbitales étendues, architecture système élargie.
4. **Architecte Principal** — vol habité cislunaire, infrastructures orbitales lourdes, nucléaire spatial avancé.
5. **Directeur de Programme** — programmes habités longue durée et interplanétaires, architectures multi-branches, propulsion avancée de fin d'arbre.

Le rang est un **droit institutionnel durable** : il ne redescend que par déclassement.

## 3.3 Score et promotion
Score cumulé à **pondération égale** de trois critères ; seuils par rang franchis une seule fois par partie. La confiance ARES (voir 13.4) en est le filtre complémentaire et révocable.

| Critère | Ce qu'il mesure |
| :---- | :---- |
| **Réussite de mission** | Atteinte des objectifs primaires et secondaires, conformité au profil nominal |
| **Respect budgétaire** | Écart entre coût engagé et enveloppe contractuelle, tenue des marges |
| **Gestion de crise** | Qualité de la réponse aux anomalies : diagnostic, arbitrage, sauvegarde d'objectifs ou d'équipage |

La rétrogradation d'un demi-palier de 10.3 alimente directement le critère « gestion de crise ».

## 3.4 Fin de partie, vieillissement et temps propre
Le personnage vieillit ; mort naturelle vers **85 ans**. Trois fins de partie seulement : **mort naturelle** (ouvre une passation, voir 3.5), **mort opérationnelle** (Game Over sec, voir 10.3 niveau 5), **licenciement** pour effondrement financier durable (voir 13.5). En règle générale les horloges bord/Terre coïncident ; seules les architectures réellement relativistes (voir 6.7) créent un temps propre distinct.

**L'écart d'âge est un résultat, pas un jalon.** Le moteur calcule γ à partir du profil de vitesse obtenu : β ≈ 0,25 → ~1 an d'écart sur une décennie (invisible) ; β ≈ 0,9 → ~5 ans d'écart. Les deux issues sont légitimes ; la seconde se mérite par l'ingénierie.

## 3.5 Passation et succession
| Élément | Transmis | Justification |
| :---- | :---- | :---- |
| **Accès technologique** | Oui | Appartient à ARES et au programme global |
| **Rang** | Oui | Propriété du poste, non de la personne (voir 3.2) |
| **Carnet de notes** | Oui | Continuité personnelle et pédagogique (voir 15.4) |
| **Confiance ARES personnelle** | Non — remise à **70** | La crédibilité individuelle ne se lègue pas |
| **État programmatique** | Oui — **intégralement** | Voir ci-dessous |

Portée **multi-générationnelle** : atteindre la fin de la branche 6 demande souvent plusieurs vies. Une mort opérationnelle reste un Game Over — la passation ne l'annule jamais.

---

# 4. Boucle de gameplay principale

## 4.1 Cycle de mission
Réception d'un contrat par mail ARES (module Vigie) → conception → recherche/qualification → construction → lancement → conduite → conséquences. Chaque étape nourrit la suivante.

## 4.2 Déblocage et planification des missions
Les contrats disponibles dépendent du rang, de la maturité et de l'infrastructure. La planification s'appuie sur les fenêtres réelles (voir 7.3).

## 4.3 Rythme et parallélisation de la recherche
| Rang | Recherches simultanées |
| :---- | :---- |
| Stagiaire | 1 |
| Architecte Junior | 2 |
| Architecte Senior | 2 à 3 |
| Architecte Principal | 3 |
| Directeur de Programme | 4 (maximum) |

Arbitrage automatique en cas de conflit : **priorité scientifique, priorité programme, priorité institutionnelle**.

## 4.4 Système de ressources
| Niveau | Rôle | Contenu |
| :---- | :---- | :---- |
| **Ressources de programme** | Capacité globale d'ARES à financer, tester, lancer et soutenir plusieurs programmes | Trésorerie, capacité de lancement, fonds de réserve (voir 13) |
| **Ressources de recherche** | Maturation des technologies selon leur complexité et leur TRL | Laboratoires, bancs d'essai, temps de qualification |
| **Ressources de mission** | Contraintes physiques et opérationnelles propres à chaque architecture de vol | Propergol, puissance, masse, données, consommables, marges de sécurité |

---

# 5. Arbre technologique

## 5.1 Principe général
Six branches interdépendantes ; une capacité n'existe que par l'intersection rang / maturité / budget / infrastructure (voir 5.4), le plus contraignant faisant foi.

## 5.2 Les six branches
| # | Branche | Rôle systémique |
| :---- | :---- | :---- |
| 1 | **Accès à l'orbite** | Mise en orbite, rentrée, récupération, cadence, coût d'accès à l'espace |
| 2 | **Exploration robotique** | Sondes, orbiteurs, atterrisseurs, rovers, science automatique |
| 3 | **Vol habité proche Terre** | Capsules, EVA, amarrage, stations, opérations humaines en LEO |
| 4 | **Autonomie longue durée** | Support-vie, recyclage, médecine, habitabilité, redondance, psychologie |
| 5 | **Navigation et opérations interplanétaires** | Mécanique orbitale, transferts, assistances, capture, aérofreinage, navigation profonde |
| 6 | **Énergie et propulsion avancée** | Puissance embarquée, propulsion électrique, nucléaire, fusion, antimatière |

## 5.3 Logique de déblocage
Trois conditions cumulatives : le monde sait faire (TRL), le joueur peut financer/qualifier, et le rang autorise l'usage opérationnel. Recherche disponible ≠ autorisation opérationnelle.

## 5.4 Hiérarchie rang / maturité (TRL) / budget / infrastructure
| Axe | Ce qu'il contrôle | Échelle interne |
| :---- | :---- | :---- |
| **Rang** | Droit institutionnel de porter la capacité | Stagiaire → Directeur (5 niveaux, voir 3.2) |
| **Maturité (TRL)** | Ce que le monde sait faire | TRL 1–9 : 1–3 recherche fondamentale, 4–6 démonstration/qualification, 7–9 opérationnel (voir Annexe A) |
| **Budget** | Capacité à financer développement, essais et exploitation | Trésorerie et paliers de fonds de réserve (voir 13.4) |
| **Infrastructure** | Moyens physiques requis (bancs d'essai, modules Novellus, cadence de lancement, radiateurs, blindage) | Débloquée par les branches 1 et 6 et par Novellus (voir 11) |

Cette hiérarchie doit être **explicite** dans le moteur.

## 5.5 Recherche parallèle par rang
Le nombre de recherches simultanées croît avec le rang (voir 4.3).

## 5.6 Tableau synthétique détaillé
| Branche | Sous-branches | Disponible au départ | Futur crédible à rechercher | Missions / capacités débloquées | Rang dominant |
| :---- | :---- | :---- | :---- | :---- | :---- |
| **Accès à l'orbite** | Lanceurs, insertion, guidage, récupération, rentrée, rendez-vous | Lanceurs chimiques multi-étages, guidage inertiel, séparation d'étages, contrôle d'attitude, rentrée capsule, récupération partielle | Réutilisation poussée, transfert de propergol orbital, rendez-vous automatisé robuste, précision d'insertion accrue, cadence élevée, rentrée lourde réutilisable | Satellites, observation, télécom, cargos, stations, retours orbitaux, logistique lourde | Stagiaire → Junior |
| **Exploration robotique** | Orbiteurs, atterrisseurs, rovers, instruments, prélèvements, robotique de service | Sondes, rovers, instruments embarqués, télédétection, atterrissage robotique classique | Autonomie scientifique, sélection embarquée par IA, robotique d'assemblage, retour d'échantillons robuste, maintenance orbitale robotisée | Flybys, orbiteurs, atterrisseurs, rovers, pré-déploiement d'infrastructures | Junior → Senior |
| **Vol habité proche Terre** | Capsules, EVA, amarrage, opérations équipage, stations, sauvetage | Capsules habitées, EVA, amarrage, support-vie court terme, logistique LEO | Stations modulaires avancées, automatisation de bord, sauvetage robuste, maintenance humaine avancée | LEO habitée, inspection, réparation, stations scientifiques, présence prolongée en LEO | Junior → Senior |
| **Autonomie longue durée** | Recyclage, médecine, psychologie, redondance, santé, habitabilité | Recyclage partiel air/eau, gestion des consommables, protocoles médicaux, redondance de base | Recyclage quasi fermé, médecine embarquée assistée, support-vie long séjour, diagnostics autonomes, maintenance locale | Séjours longs, architectures lunaires, croisières habitées, habitats lointains | Senior → Principal |
| **Navigation et opérations interplanétaires** | Hohmann, corrections, navigation profonde, capture, aérofreinage, assistances | Transferts de Hohmann, corrections standards, navigation de précision, gravity assists, capture orbitale, aérofreinage | Optimisation multi-impulsions, navigation autonome profonde, pré-positionnement logistique, aérocapture avancée, rendez-vous lointains complexes | Missions martiennes, astéroïdes, missions lunaires avancées, transferts complexes | Senior → Directeur |
| **Énergie et propulsion avancée** | Puissance embarquée, propulsion électrique, nucléaire, fusion, antimatière | Panneaux solaires, batteries, propulsion chimique, propulsion électrique modérée, RTG | Réacteurs de fission spatiale, NTP, NEP, fusion spéculative, antimatière très tardive | Système solaire externe, hautes puissances embarquées, missions longues massives, fin de jeu relativiste | Principal → Directeur |

## 5.7 Branche Accès à l'orbite
Fonde le coût et la cadence d'accès à l'espace ; sa maturité conditionne tout le reste.

## 5.8 Branche Exploration robotique
Science automatique et pré-déploiement d'infrastructures avant toute présence humaine.

## 5.9 Branche Vol habité proche Terre
Robustesse des opérations humaines en LEO (capsules, EVA, amarrage, stations).

## 5.10 Branche Autonomie longue durée
Support-vie, recyclage, médecine et redondance. **Indispensable** pour la Lune durable, le cislunaire, Mars et toute mission sans évacuation immédiate — aussi importante que le moteur pour le vol habité lointain (voir 19.1).

## 5.11 Branche Navigation et opérations interplanétaires
Transferts, assistances gravitationnelles, capture, aérofreinage, navigation profonde.

## 5.12 Branche Énergie et propulsion avancée

### 5.12.1 Distinction fondamentale énergie vs propulsion
L'énergie (produire de la puissance) et la propulsion (produire de la poussée) sont deux problèmes distincts. Certaines technos servent l'une, l'autre, ou les deux.

### 5.12.2 Critères de classement
Chaque palier est classé par nature (énergie/propulsion), statut dans l'univers, effets de gameplay et rang requis.

### 5.12.3 Paliers énergie / propulsion
| Palier | Technologie | Nature | Statut dans l'univers | Effets de gameplay | Rang |
| :---- | :---- | :---- | :---- | :---- | :---- |
| 0 | Chimique classique | Propulsion | Maîtrisée au départ | Poussée élevée, Isp modéré ; missions orbitales et interplanétaires classiques | Stagiaire → Directeur |
| 1 | Solaire + batteries | Énergie | Maîtrisée au départ | Faible à moyenne puissance ; missions proches du Soleil | Stagiaire → Senior |
| 2 | RTG (radioisotopes) | Énergie | Mature, disponible tôt | Puissance faible mais très robuste et durable ; système solaire externe robotique | Junior → Senior |
| 3 | Propulsion électrique avancée | Propulsion | Crédible à moyen terme | Faible poussée, très haut Isp ; longs transferts robotisés | Senior |
| 4 | Réacteur de fission spatial | Énergie | Avancé, coûteux, lourd, infrastructure lourde | Forte puissance embarquée ; habitats, missions lointaines, remorqueurs | Principal |
| 5 | NTP — nucléaire thermique | Propulsion | Avancée mais crédible | Isp ~2× chimique à poussée utile ; réduction des temps de transit habité | Principal |
| 6 | NEP — nucléaire électrique | Énergie + propulsion | Très avancé | Haute puissance, faible poussée ; cargos lointains, remorqueurs, missions massives lentes | Principal → Directeur |
| 7 | Fusion pilotée spatiale | Énergie + propulsion | Spéculative mais défendable | Très longues missions, très grandes vitesses dans le système solaire | Directeur |
| 8 | **Antimatière** | Énergie / catalyse / propulsion extrême | **Extrêmement spéculative — fin d'arbre uniquement** | **Missions relativistes de fin de jeu ; coût et risque hors normes** | Directeur, fin de jeu |

### 5.12.4 Palier 0 — Propulsion chimique
Poussée élevée, Isp modéré ; socle de tout décollage et de toute manœuvre franche.

### 5.12.5 Palier 1 — Solaire + batteries
Puissance décroissant en 1/d² avec la distance au Soleil : efficace en interne, marginale au-delà de la ceinture d'astéroïdes, inutilisable seule en externe. Le stockage impose masse et cycles.

### 5.12.6 Palier 2 — RTG / radioisotopes
**Jamais une propulsion.** Puissance faible mais extrêmement robuste, indépendante de l'ensoleillement : ouvre le système solaire externe robotique. Contraintes : matière rare et coûteuse, chaleur de désintégration, sûreté au lancement, décroissance (demi-vie).

### 5.12.7 Palier 3 — Propulsion électrique avancée
Faible poussée, très haut Isp ; régime continu, incapable de décoller ou d'insérer rapidement.

### 5.12.8 Palier 4 — Réacteur de fission spatial
Forte puissance embarquée (branche 6 + Novellus, voir 11.4). Contraintes : qualification réacteur/cœur, sensibilité politique, essais (contamination, bancs dédiés), dégradation en service, architecture masse/blindage/refroidissement.

### 5.12.9 Palier 5 — NTP (propulsion nucléaire thermique)
**Ne casse pas Tsiolkovsky** : le gain vient d'un ve ~2× chimique, pas d'un contournement du bilan de masse.

### 5.12.10 Palier 6 — NEP (propulsion nucléaire électrique)
Haute puissance, faible poussée ; cargos lointains, remorqueurs, missions massives lentes. Rejet thermique dimensionnant.

### 5.12.11 Palier 7 — Fusion pilotée spatiale
Spéculative mais physiquement défendable. Prérequis : confinement/stabilité, bilan net positif, matériaux, refroidissement, tuyère magnétique, essais longs. Vraie transition vers le pré-relativiste : de très grandes vitesses **sans encore** rendre la dilatation significative (voir 6.7).

### 5.12.12 Palier 8 — Antimatière
Changement de régime physique, industriel et narratif, jamais un simple « meilleur moteur ». Fin d'arbre uniquement, coût exorbitant, prérequis multiples et indépendants. Sujet de recherche théorique très tardif.

#### Le modèle de production est le paramètre d'équilibrage
| Paramètre | Rôle | Effet sur le jeu |
| :---- | :---- | :---- |
| **Débit de production** (masse par unité de temps) | Combien l'infrastructure produit par an | Fixe la durée d'accumulation avant qu'une mission soit possible |
| **Rendement énergétique** | Énergie consommée par unité produite | Couple la production à la branche énergie et au budget |
| **Coût par unité** | Charge budgétaire | Rend l'accumulation concurrente des autres programmes |
| **Capacité de confinement** | Masse stockable en sécurité, taux de perte | Plafonne le stock utile et crée un risque permanent |

### 5.12.13 Cohérence relativiste (renvoi)
Les effets relativistes n'apparaissent qu'à une fraction significative de c. Aucune filière chimique, électrique, RTG, fission, NTP, NEP ni fusion classique ne produit de dilatation perceptible. L'écart d'âge de 3.4 est un **résultat possible** calculé sur le profil réel, pas une mécanique garantie. Modèle complet en 6.7.

## 5.13 Technologies transverses
Avionique, matériaux, autonomie logicielle, communications : facilitateurs communs à plusieurs branches, sans capacité de mission propre.

---

# 6. Physique de la propulsion et modélisation
Socle physique de la branche 5.12 et du temps propre (3.4). Valeurs = cibles de modèle ; technos spéculatives assorties d'incertitude explicite (voir 12.5).

## 6.1 Budget de masse et Tsiolkovsky
```
Δv = ve · ln(m0 / mf) = Isp · g0 · ln(m0 / mf)
```
La masse sèche et le propergol influencent réellement la conception des étages.

## 6.2 Poussée, impulsion spécifique, puissance
```
F = ṁ · ve        (terme de pression négligé ou intégré)
P_jet = ½ · ṁ · ve²
F = 2 · η · P / ve
```
Pour la propulsion électrique, la poussée est bornée par la puissance disponible.

## 6.3 Régimes impulsionnel et continu
| Régime | Rapport poussée/poids (T/W) | Modèle de manœuvre | Filières | Capacités / limites |
| :---- | :---- | :---- | :---- | :---- |
| **Impulsionnel (forte poussée)** | T/W ≳ 0,1 (et > 1 au décollage) | Burns brefs, approximation quasi-instantanée acceptable | Chimique, NTP | Décollage, atterrissage, insertion, évasion rapide, corrections franches |
| **Continu (faible poussée)** | T/W ~ 10⁻⁴ à 10⁻³ | Poussée intégrée sur semaines/mois, spirales, poussée quasi-tangentielle | Électrique, NEP | Transferts économes ; **incapable** de décoller, d'atterrir ou d'insérer rapidement |

Cette distinction interdit les incohérences classiques (moteur ionique « décollant », NEP « freinant » brutalement).

## 6.4 Modèles par filière
| Filière | Isp (s) | Poussée (ordre) | Régime | Facteur limitant dominant |
| :---- | :---- | :---- | :---- | :---- |
| Chimique solide | 250–280 | 10⁵–10⁷ N | Impulsionnel | Isp faible → ratio de masse |
| Chimique liquide (bipropergol) | 300–460 | 10⁴–10⁷ N | Impulsionnel | Ratio de masse pour missions lointaines |
| Propulsion électrique (Hall) | 1 500–3 000 | 0,1–1 N | Continu | Puissance disponible, poussée |
| Propulsion électrique (grille/ionique) | 3 000–10 000 | 0,01–0,5 N | Continu | Puissance, durée de poussée |
| NTP (nucléaire thermique) | 850–1 000 | 10⁴–10⁵ N | Impulsionnel | Masse sèche, blindage, qualification |
| NEP (nucléaire électrique) | 2 000–10 000 | 1–50 N | Continu | Rejet thermique (radiateurs), masse réacteur |
| Fusion (concepts) | 10⁴–10⁶ | variable | Impulsionnel/continu | Bilan net, confinement, matériaux |
| Antimatière (cœur annihilant) | 10⁵–10⁷ (ve → fraction de c) | variable, très faible densité | Continu/relativiste | Production d'antimatière, confinement, coût |

## 6.5 Contraintes thermiques
```
P_rejetée = ε · σ · A · (T_radiateur⁴ − T_environnement⁴)
```
À η ≈ 30 %, il faut rejeter ~2,3 fois la puissance électrique produite. La **gestion thermique est un verrou de mission** au même titre que la propulsion ; le moteur modélise un budget thermique et la masse/surface des radiateurs (avec leur fiabilité, voir 12.4).

## 6.6 Contraintes radiatives
Verrou réel pour le cislunaire, Mars, l'externe habité et les architectures nucléaires/relativistes. Le moteur modélise dose cumulative, événements solaires, exposition selon trajectoire et durée, qualité du blindage, et l'arbitrage masse/protection/mission.

## 6.7 Relativité restreinte
Le moteur ne vise aucun régime particulier ; **il calcule celui que le joueur atteint**.

### 6.7.1 Facteur de Lorentz et temps propre
```
γ = 1 / √(1 − β²)
dτ = dt / γ        →        τ_bord = ∫ dt / γ(t)
```

### 6.7.2 Seuils : à partir de quand l'effet est perceptible
| β (fraction de c) | v (m/s) | γ | Écart temporel (γ − 1) |
| :---- | :---- | :---- | :---- |
| 10⁻⁴ (missions chimiques/nucléaires) | ~3 × 10⁴ | 1,000000005 | ~5 × 10⁻⁹ (négligeable) |
| 0,01 | 3 × 10⁶ | 1,00005 | 0,005 % |
| 0,10 | 3 × 10⁷ | 1,005 | 0,5 % |
| 0,30 | 9 × 10⁷ | 1,048 | 4,8 % |
| 0,50 | 1,5 × 10⁸ | 1,155 | 15,5 % |
| 0,90 | 2,7 × 10⁸ | 2,294 | 129 % |
| 0,99 | 2,97 × 10⁸ | 7,089 | 609 % |

Effet **mesurable** dès β ≈ 0,1 ; **narrativement perceptible** vers β ≳ 0,7. Budgets Δv réalistes du système solaire : β ~ 10⁻⁴ (γ−1 ~ 10⁻⁸), imperceptible ; même la fusion (~0,05–0,10 c) reste sous le seuil narratif. Seule l'antimatière franchit β ≳ 0,3, et seule une antimatière **très aboutie** approche 0,9.

### 6.7.3 Équation de la fusée relativiste
```
m0 / mf = ( (1 + β) / (1 − β) ) ^ ( c / (2 · ve) )
```
Cas limite photonique (ve = c) : exposant ½. La **rapidité** φ (β = tanh φ) est additive, contrairement aux vitesses.

| β visé | Ratio de masse (une poussée) |
| :---- | :---- |
| 0,3 | 2,5 |
| 0,5 | 5,2 |
| 0,9 | 83 |

### 6.7.4 Le verrou de l'aller-retour
Quatre poussées (accélération, décélération, retour, freinage final) sans ravitaillement : le ratio de masse est élevé **à la puissance quatre**.

| β visé | Ratio unitaire | Ratio aller-retour complet |
| :---- | :---- | :---- |
| 0,3 | 2,5 | ~41 |
| 0,5 | 5,2 | ~730 |
| 0,9 | 83 | ~4,7 × 10⁷ |

L'étagement et le largage de structure atténuent, sans annuler.

### 6.7.5 Implications de gameplay
- En dessous du seuil, une seule horloge ; la dilatation n'est jamais un paramètre libre.
- Au-dessus, le moteur gère deux horloges (bord/Terre) ; l'écart pèse sur la narration, la carrière et la passation (voir 3.4, 3.5).

## 6.8 Traçabilité des approximations
Toute simplification (coniques raccordées en planification, intégrateur à pas variable en croisière, poussée moyennée…) doit être documentée, bornée et cohérente avec le gameplay. Le réalisme vient de la traçabilité, pas d'un calcul maximaliste uniforme.

---

# 7. Simulation physique et astrodynamique

## 7.1 Niveau de fidélité
Fidélité maximale : n-corps complet, perturbations (dont J2), budgets de masse/delta-v, temps de vol, corrections, navigation avec incertitudes, rentrée, **consommation réelle des ressources**. Aucune simplification arcade.

## 7.2 Hiérarchie de simulation
| Phase | Modèle | Exigence |
| :---- | :---- | :---- |
| Planification initiale | Modèles rapides et suffisamment précis (coniques raccordées, approximations d'impulsion) | Rapidité, ordre de grandeur fiable pour décider |
| Exécution mission critique | Simulation fine à haute précision (intégration n-corps, perturbations) | Précision maximale aux moments décisifs |
| Longues durées (croisière) | Intégration stable avec contrôle de la dérive numérique | Conservation, stabilité sur des mois/années |
| Approximation explicite | Autorisée quand nécessaire, **jamais** simplification cachée | Documentée, bornée, cohérente avec le gameplay |

## 7.3 Éphémérides et fenêtres de lancement
Positions planétaires calculées depuis la **date/heure réelle** au seul démarrage ; le temps devient ensuite indépendant (voir 14). Les fenêtres découlent des positions réelles — deux joueurs démarrant à six mois d'écart vivent des contraintes différentes. Pannes et anomalies suivent des probabilités calibrées.

## 7.4 Calculs manuels
Toutes les manœuvres (Hohmann, assistances, insertion, corrections) sont calculées par le joueur dans le terminal. L'assistance dépend **uniquement du mode** (voir 2), pas du rang.

## 7.5 Navigation et incertitude
Le joueur ne voit jamais une position vraie absolue, mais une **solution de navigation** avec incertitude en position et vitesse (voir 8).

## 7.6 Rentrée atmosphérique et EDL
Modèle de rentrée/descente/atterrissage avec seuils stricts ; corridor étroit, échauffement, marges.

## 7.7 Environnement radiatif et météo spatiale
L'environnement est un **acteur de mission** : GCR chroniques, SPE aigus, ceintures de Van Allen, selon trajectoire et durée.

## 7.8 Débris orbitaux
Population de débris modélisée ; risque de collision et empreinte durable des missions (voir 10.5).

---

# 8. Suivi de trajectoire et corrections en vol

## 8.1 Principe
Une fois lancée, le moteur calcule la **trajectoire réelle** ; la **trajectoire nominale** reste la référence ; le joueur compare via une **trajectoire estimée** reconstruite des mesures et agit quand l'écart devient significatif.

## 8.2 Origine des écarts
Erreurs d'injection, perturbations, poussée imparfaite, incertitude de mesure.

## 8.3 Interface de suivi
Un **plan terminal** et un **plan spatial** coexistent en permanence :
- **Terminal** : état nominal, état estimé, écarts Δr et Δv, erreur projetée au point cible, incertitude 1σ/3σ, réserve de delta-v, délai de communication, coût estimé de la correction, prochaine fenêtre.
- **Vue spatiale** : ce n'est **pas une carte séparée** mais la **scène 3D du système solaire 1:1 elle-même** (voir 17.3), parcourue par la caméra libre (voir 17.4). Trajectoires, position, **corridor d'incertitude** et prochain nœud y sont dessinés **dans le monde**, à leur position réelle. Le raccourci « vue système » (touche M) n'est qu'un cadrage de caméra prédéfini.

## 8.4 Processus de correction
Mesurer l'état réel → recalculer une solution depuis la position actuelle → déterminer le Δv et son orientation → exécuter → vérifier.

## 8.5 Seuils de dérive
| Type de mission | Tolérance avant correction |
| :---- | :---- |
| Orbital proche Terre (LEO) | Faibles erreurs seulement ; correction rapide |
| Croisière interplanétaire | Écarts absolus plus élevés tolérés tant que l'erreur finale reste corrigeable à coût raisonnable |
| Entrée, descente, atterrissage (EDL) | Seuils les plus stricts : marges kilométriques en position, métriques en vitesse |

## 8.6 Fréquence de vérification
Le joueur choisit son rythme de mesure ; trop rare laisse dériver, trop fréquent coûte des ressources et du temps.

## 8.7 Dimension pédagogique
Le cycle mesure/écart/correction enseigne la navigation réelle sans l'expliciter comme un didacticiel.

---

# 9. Missions habitées et rôle du joueur

## 9.1 Rôle embarqué et médiation par le terminal
En vivant une mission longue, le joueur devient **responsable scientifique à bord** : surveillance des systèmes vitaux (O2/CO2, eau, nourriture), expériences, diagnostics/réparations, communication avec **délai lumière réaliste**, suivi de trajectoire (voir chapitre 8).

## 9.2 Choix d'embarquement et éligibilité
Le joueur choisit d'embarquer ou de conduire depuis le sol. **Une seule mission vécue à la fois.** Les missions longues sont réservées à la fin de carrière (rang + maturité correspondants) : le personnage ne quitte ARES que lorsqu'il n'a plus de carrière à construire.

## 9.3 Fonctionnement d'ARES pendant l'absence
Pendant l'absence, ARES **fonctionne normalement** sous un adjoint : recherches, contrats et trésorerie évoluent selon la priorisation de 4.3, sans intervention. Ni pénalité ni dégradation punitive : l'agence est compétente.

**Protection pendant l'absence.** La **chaîne de fin de partie financière** (voir 13.5) est **suspendue** et la **confiance gelée** à sa valeur de départ : aucune faillite ni perte de crédibilité ne peut survenir en l'absence du joueur. Seul l'état programmatique évolue. Nécessaire car une mission relativiste peut couvrir plusieurs décennies terrestres (voir 6.7).

## 9.4 Ressources vitales
Eau, nourriture, oxygène et consommables sont finis ; une mission mal calculée avant lancement se traduit en dérives coûteuses, voire en échec si les réserves ne suffisent pas.

## 9.5 Événements aléatoires
Bibliothèque d'anomalies (pannes, incidents, aléas médicaux) à probabilités calibrées par phase et durée.

## 9.6 Délai de communication
Communication au délai lumière selon la distance ; le logiciel de vol embarqué (voir 15.3) prépare l'autonomie quand le sol est hors de portée.

---

# 10. Missions, risques et conséquences

## 10.1 Types de missions
Robotiques, habitées, logistiques, scientifiques, d'infrastructure ; du LEO au relativiste.

## 10.2 Origine des contrats
Contrats institutionnels et commerciaux, filtrés par rang, maturité et confiance.

## 10.3 Échelle de gravité à cinq niveaux
| Niveau | Description | Pénalité budgétaire | Confiance (sur 100) | Effet de carrière |
| :---- | :---- | :---- | :---- | :---- |
| **1 — Mineur** | Déviation maîtrisée, mission nominalement réussie | 1 à 5 % | −1 à −3 | Aucun blocage |
| **2 — Modéré** | Coût opérationnel réel, objectif secondaire perdu | 5 à 15 % | −4 à −8 | Retard de 1 à 3 mois sur le contrat suivant |
| **3 — Majeur** | Mission partiellement compromise | 15 à 35 % | −9 à −20 | Gel possible de promotion, 6 à 18 mois |
| **4 — Critique** | Mission perdue, dommage grave | 35 à 70 % | −21 à −40 | Blocage immédiat de promotion, enquête obligatoire |
| **5 — Catastrophe** | Mort du joueur, ou perte d'ampleur exceptionnelle | Maximale | > −40 | Game Over si décès ; sinon déclassement possible |

### Niveau 1 — Incident mineur
Déviation maîtrisée, marges entamées ; retour d'expérience sans blocage.

### Niveau 2 — Incident modéré
Objectif secondaire perdu, coût réel, léger retard programmatique.

### Niveau 3 — Incident majeur
Objectif important perdu, profil incomplet, marges quasi épuisées ; gel de promotion possible.

### Niveau 4 — Échec critique
Mission perdue ou dommage grave ; enquête obligatoire, perte de 35 à 70 % d'un programme.

### Niveau 5 — Catastrophe
Mort du joueur → Game Over immédiat (une passation ne relève que du mécanisme de fin de vie, jamais d'une annulation). Si le personnage survit, perte exceptionnelle : déclassement fonctionnel ou mise à l'écart temporaire.

### Modificateurs de palier
Niveau final augmenté d'un palier si une présence humaine est exposée à un risque létal ; rétrogradation possible d'un demi-palier selon les circonstances atténuantes.

## 10.4 Effets persistants et triple lecture
Chaque niveau agit sur plusieurs axes (budget, confiance, carrière, réputation) ; les conséquences persistent au-delà de la mission.

## 10.5 Débris et environnement durable
Les échecs et abandons génèrent des débris (voir 7.8) ; l'environnement orbital se dégrade durablement, ce qui pèse sur les missions futures.

---

# 11. Station Novellus (QG)

## 11.1 Rôle et statut
Station modulaire mature héritée en 2026, fictive mais inspirée de l'ISS : QG opérationnel, plateforme de R&D et de qualification. Elle abrite le poste de travail (module Vigie).

## 11.2 État initial et modules à construire
| # | Module | État au démarrage | Enjeu de jeu |
| :---- | :---- | :---- | :---- |
| 1 | **Noyau de commandement** | Opérationnel, génération ancienne | Modernisation de l'avionique et de la supervision |
| 2 | **Nœud d'amarrage** | Opérationnel | Extension du nombre de ports pour la croissance |
| 3 | **Habitat équipage** | Opérationnel, capacité limitée | Augmentation de la capacité et de la tolérance aux longs séjours |
| 4 | **Support-vie** | Opérationnel, recyclage partiel | Passage au recyclage quasi fermé (branche 4) |
| 5 | **Module énergétique** | Opérationnel, solaire | **Facteur limitant principal** — voir 11.4 |
| 6 | **Laboratoire scientifique** | Opérationnel, instrumentation datée | Rééquipement, ouverture de nouvelles filières de recherche |
| 7 | **Sas EVA** | Opérationnel | Modernisation, cadence de sortie accrue |
| 8 | **Atelier / maintenance** | **Absent** | À construire — réduit la dépendance aux cargos |
| 9 | **Logistique / stockage dédié** | **Absent** | À construire — profondeur stratégique, marges |
| 10 | **Module médical** | **Absent** | À construire — indispensable dès que l'évacuation n'est plus garantie |

## 11.3 Paliers de progression
Fondation structurelle et énergétique progressive ; les dix modules restent valides jusqu'à la branche 6.

## 11.4 Dépendance énergie / propulsion avancée
Le module énergétique est le **facteur limitant** de la station : la haute puissance embarquée (fission, NEP) dépend de la branche 6 et conditionne les capacités avancées de Novellus.

## 11.5 Catégorisation des modules
| Catégorie | Modules | Rôle |
| :---- | :---- | :---- |
| **Vitaux** | Noyau, nœud d'amarrage, support-vie, énergétique, habitat | Sans eux, la station ne peut ni fonctionner ni accueillir un équipage |
| **Opérationnels avancés** | Laboratoire, atelier/maintenance, logistique | Accroissent fortement la valeur stratégique et l'autonomie |
| **Robustesse** | Module médical, sas EVA | Sécurisent l'exploitation longue durée et les programmes complexes |

## 11.6 Effets gameplay
Chaque module a des effets concrets (capacité, autonomie, recherche, sûreté) ; leur construction arbitre entre coût et profondeur stratégique.

---

# 12. Construction des véhicules et fiabilité

## 12.1 Catalogue
Assemblage à partir de **pièces réelles ou extrapolées de lignées réelles** (moteurs type Merlin, RS-25, réservoirs, capsules), jamais génériques. Les composants spéculatifs arrivent **tardivement**, avec niveau de confiance, incertitude, domaine de validité et statut de qualification.

## 12.2 Interface d'assemblage
Éditeur en coupe fournissant la géométrie du véhicule, réutilisée directement au rendu (voir 17.2).

## 12.3 Base de données de fiabilité
**Référentiel central de sûreté de fonctionnement** : pour chaque composant, une estimation exploitable de défaillance/dégradation dans un contexte donné — un système d'aide à la décision évolutif, pas un tableau figé. Depuis la v1.2, le **logiciel de vol** y a ses propres fiches, produites au banc d'essai (voir 15.5) : un code certifié est une donnée de fiabilité comme une autre.

### 12.3.1 Champs obligatoires et hiérarchie des sources
Chaque fiche porte identité, contexte de référence, valeur nominale, source et niveau de confiance.

### 12.3.2 Niveaux de confiance
| Niveau | Signification |
| :---- | :---- |
| **A** | Donnée mesurée ou solidement documentée (retour d'expérience, publication, rapport institutionnel exploitable) |
| **B** | Donnée publique indirecte (constructeur partielle, base générique appliquée à un cas proche, extrapolation faible cohérente) |
| **C** | Estimation raisonnée (analogies crédibles, hypothèses explicites, mission comparable) |
| **D** | Hypothèse de simulation faute de mieux ; **exceptionnel**, identifié comme tel dans tous les calculs sensibles |

### 12.3.3 Fiabilité contextuelle et modificateurs
La valeur nominale **ne doit jamais** être utilisée telle quelle : elle est corrigée avant tout calcul par des modificateurs — environnement, maintenance, redondance, historique d'anomalies, qualité d'intégration, vieillissement calendaire et en service, écart contexte de référence/réel. **La fiabilité est une estimation évolutive, pas une constante.**

### 12.3.4 Incertitude, dégradation et révision
Chaque valeur porte une incertitude et se met à jour avec le retour d'expérience et le vieillissement.

### 12.3.5 Exploitation à trois niveaux
Composant → sous-système → mission : la fiabilité se propage et se compose.

### 12.3.6 Visibilité gameplay
Le joueur voit des estimations contextualisées et justifiables, jamais des pourcentages abstraits.

## 12.4 Fiabilité des filières avancées
Radiateurs, réacteurs, confinement : chaque sous-système avancé a sa propre fiabilité, souvent dimensionnante (voir 6.5).

## 12.5 Principe conservateur
Une approximation identifiée est autorisée ; une approximation déguisée en certitude ne l'est pas. Les technos spéculatives portent une incertitude explicite.

---

# 13. Économie et ressources

## 13.1 Budget annuel et sources de financement
Enveloppe de l'ordre de **100 Md€/an** (cumul spatial mondial, cohérent avec l'agence unique).

| Source | Part indicative | Nature |
| :---- | :---- | :---- |
| **Dotation étatique — base garantie** | ~30 % (≈ 30 Md€) | Socle versé quel que soit le niveau d'activité |
| **Dotation étatique — tranche liée aux programmes** | ~45 % (≈ 45 Md€) | Libérée par tranches contre jalons programmatiques ; **non versée si aucun programme n'avance** |
| **Contrats de service commerciaux** | ~20 % | Lancements, télécommunications, observation et services orbitaux vendus à des tiers |
| **Valorisation scientifique et licences** | ~5 % | Exploitation des données, brevets, retombées industrielles |

Pas de sponsoring : ARES vend des prestations, elle ne loue pas son image. RH hors périmètre v1.

## 13.2 Structure de coûts
| Poste | Ordre de grandeur | Fréquence |
| :---- | :---- | :---- |
| **Coûts fixes d'agence** | 35 à 45 % du budget, soit ~40 Md€/an | Mensuelle à semestrielle |
| **Exploitation de Novellus** | ~4 Md€/an | Continue |
| **Programmes et missions** | Solde, soit ~55 Md€/an | Par engagement |

**Pression d'inactivité (fonde la contrainte temporelle).** Les **recettes garanties hors activité** — base étatique (≈ 30 Md€) + valorisation (≈ 5 Md€), soit **≈ 35 Md€/an** — sont **inférieures aux coûts fixes** (≈ 44 Md€/an). Tranche programmes et commercial ne tombent que si l'agence produit.

| Situation | Recettes captées | Sorties | Solde annuel sur la réserve |
| :---- | :---- | :---- | :---- |
| **Inactif** (aucun programme, aucun service) | ~35 Md€ | ~44 Md€ | **≈ −9 Md€/an** — érosion lente |
| **Équilibré** (programmes soutenus, commercial actif) | ~100 Md€ | ~99 Md€ | **≈ stable** |
| **Sur-engagé** (programme phare ~90 Md€) | ~100 Md€ | ~134 Md€ | **≈ −34 Md€/an** — érosion rapide |

Le vrai gouffre est le **sur-engagement de programmes** (voir 13.3) ; l'agence n'est jamais insolvable par construction. Montants = ordres de grandeur à calibrer (Annexe E) ; seul compte l'invariant *recettes garanties < coûts fixes*.

## 13.3 Échelle des engagements
| Engagement | Ordre de grandeur | Part du budget annuel |
| :---- | :---- | :---- |
| Lancement lourd | 60 à 100 M€ | ~0,1 % |
| Mission robotique moyenne | 300 à 800 M€ | ~0,5 % |
| Mission scientifique phare | 2 à 3 Md€ | ~2,5 % |
| Module de station | 3 à 8 Md€ | ~5 % |
| Programme habité lourd (pluriannuel) | 20 à 90 Md€ | dominant |

Acheter un lancement ne ruine pas ; on se ruine en **engageant des programmes**, en accélérant le temps sans revenus, ou en perdant 35 à 70 % d'un programme (voir 10.3). L'arbitrage se joue au niveau du portefeuille.

## 13.4 Fonds de réserve et confiance ARES
**Fonds de réserve** : réserve obligatoire ; les paliers d'alerte portent sur **son niveau rapporté à sa cible**, non sur l'enveloppe annuelle. Le drain des coûts fixes s'impute d'abord sur la **trésorerie courante** ; quand elle ne couvre plus, le fonds est entamé et ses paliers déclenchent les alertes.

| Palier de réserve | État de l'agence |
| :---- | :---- |
| > 75 % | Fonctionnement normal |
| 75 – 50 % | Surveillance renforcée |
| 50 – 30 % | Retard automatique des activités secondaires |
| 30 – 15 % | Tension : capacité d'initiative réduite |
| 15 – 5 % | Crise budgétaire : gel des projets non prioritaires |
| < 5 % | Asphyxie : restructuration, recapitalisation ou arrêt partiel |

**Confiance ARES** : échelle **0 à 100**, valeur initiale **70** ; crédibilité personnelle de l'Architecte. *(À ne pas confondre avec le niveau de confiance A-D des données de fiabilité, voir 12.3.2, ni avec la confiance de certification d'un code, voir 15.5 : trois notions indépendantes.)*

| Événement | Effet |
| :---- | :---- |
| Mission nominale | **+2 à +5** |
| Réussite majeure (objectif difficile, crise bien tenue, première technologique) | **+8 à +10** |
| Incidents et échecs | Voir barème de 10.3 (−1 à plus de −40) |

Plafond 100, plancher 0 ; depuis 70, atteindre 100 demande 3-4 réussites majeures ou 6-15 missions nominales ; remonter d'un échec critique coûte le même effort.

| Confiance | Ce qui est accessible |
| :---- | :---- |
| 80 – 100 | Programmes phares, vol habité lointain, technologies de fin d'arbre |
| 60 – 79 | Fonctionnement normal, tous contrats de routine |
| 40 – 59 | Missions habitées suspendues, contrats restreints |
| 20 – 39 | Robotique et maintenance uniquement, surveillance permanente |
| < 20 | Aucun nouveau programme ; procédure institutionnelle engagée (voir ci-dessous) |

**Sort du plancher.** Sous 20, la procédure n'est **jamais une fin de partie en soi** et ne peut laisser un **état absorbant** : puisque la confiance ne remonte que par les missions et qu'à moins de 20 aucun programme n'est ouvert, elle se **résout** — déclassement (perte d'un rang, voir 3.2, 10.3) assorti d'un retour de la confiance à un niveau de reprise (bande 40-59), ce qui rouvre les contrats de routine du rang inférieur. Le déclassement étant défavorable, nul n'a intérêt à provoquer cette chute. Seules les conditions de 3.4 terminent la partie.

**Articulation rang × confiance** : deux filtres distincts et non redondants. Le **rang** est un plafond d'autorisation durable ; la **confiance** est la crédibilité courante révocable. Une capacité n'est accessible que si le rang l'autorise *et* si la confiance atteint le seuil : un Directeur à confiance 45 garde son rang mais voit ses missions habitées suspendues. La confiance est remise à 70 en passation ; l'état programmatique persiste (voir 3.5).

## 13.5 Chaîne de fin de partie financière
L'épuisement durable de la réserve déclenche : **avertissement → gel des contrats → mise à l'écart → licenciement** (fin de partie). Chaque étape est notifiée et laisse une fenêtre de correction ; le licenciement n'intervient jamais sur un accident isolé.

## 13.6 Sites de lancement
Sites de lancement réels, avec contraintes de latitude, d'azimut et de logistique.

## 13.7 Ressources vitales
Le vol habité consomme eau, nourriture et oxygène (voir 9.4) ; leur gestion est une contrainte de mission à part entière.

---

# 14. Échelle temporelle et persistance

## 14.1 Temps réel à la création
L'état du système solaire est synchronisé sur la date/heure **réelle uniquement au démarrage** ; ensuite le temps est indépendant et pilotable. Mode solo **hors-ligne complet**.

## 14.2 Accélération et contraintes
Le joueur accélère librement (jour/semaine/mois). L'accélération n'est pas neutre : les **recettes garanties hors activité** (≈ 35 Md€/an) sont inférieures aux **coûts fixes** (≈ 44 Md€/an), tranches et commercial ne tombant que si l'agence produit (voir 13.2). Accélérer sans programme ni commercial érode trésorerie puis réserve d'environ 9 Md€/an, jusqu'à la chaîne d'alerte et le licenciement (voir 13.4–13.5).

## 14.3 Temps vécu en mission
En mission vécue, certaines tâches se gèrent en temps réel (surveillance, expériences, corrections) ; le reste peut être accéléré. Toute manœuvre fine ramène le temps à un rythme lent.

## 14.4 Temps propre relativiste
Pour les seules architectures relativistes de fin de jeu (voir 5.12.13, 6.7), le moteur gère **deux horloges** (temps propre bord / temps terrestre) ; sous le seuil elles coïncident, au-dessus l'écart alimente le vieillissement (3.4). Multijoueur hors périmètre v1 (voir 16) : aucun traitement partagé requis.

---

# 15. Terminal, code et interface

## 15.1 Architecture
| Composant | Rôle |
| :---- | :---- |
| **Éditeur** | Saisie du code (Pro) ou du graphe (Normal), coloration, complétion sur l'API |
| **Compilateur embarqué** | Compilation du code joueur contre les en-têtes ARES |
| **Bac à sable d'exécution** | Exécution isolée en processus séparé ; un plantage du code joueur n'atteint jamais la simulation |
| **Banc d'essai** | Rejeu du code contre un environnement simulé, sous domaine de validité déclaré (voir 15.5) |
| **Carnet** | Documentation, formules, archives personnelles (voir 15.4) |

## 15.2 API sol
Surface `ares::sol` en **lecture seule, sans conséquence** : analyse et conception.
```cpp
#include <ares/sol.hpp>
using namespace ares::sol;

int main() {
    // Fenêtre de transfert vers Mars
    Corps terre = ephemeride("TERRE", date("2026-11-04T00:00:00Z"));
    Corps mars  = ephemeride("MARS",  date("2027-08-12T00:00:00Z"));

    Transfert t = lambert(terre.position(), mars.position(), jours(281));

    // Budget de masse du véhicule
    Vehicule v = charger("ARV-3");
    double dv    = t.dv_total();
    double ratio = std::exp(dv / v.ve_effective());
    double ergol = v.masse_seche() * (ratio - 1.0);

    journal("Δv requis      : %8.0f m/s", dv);
    journal("ratio de masse : %8.3f",     ratio);
    journal("ergols         : %8.1f t",   ergol / 1000.0);

    if (ergol > v.capacite_ergols()) {
        journal("INFAISABLE — dépassement de %.1f t",
                (ergol - v.capacite_ergols()) / 1000.0);
    }
    return 0;
}
```

## 15.3 API vol
Surface `ares::vol` du logiciel embarqué : **écriture, conséquences réelles, qualification obligatoire**.
```cpp
#include <ares/vol.hpp>
using namespace ares::vol;

// Correction mi-parcours autonome.
// S'exécute à 14 minutes-lumière de la Terre, sans validation possible du sol.
void sequence_correction(Contexte& ctx) {

    Etat estime = ctx.navigation().solution();

    // Refuser d'agir sur une solution dégradée
    if (estime.incertitude_3sigma() > metres(12000)) {
        ctx.alerte("Solution de navigation dégradée — correction reportée");
        ctx.replanifier(heures(48));
        return;
    }

    Ecart e = ctx.cible().ecart_projete(estime);
    if (e.norme() < ctx.cible().tolerance()) {
        return;                       // dans les marges, ne rien consommer
    }

    Manoeuvre m = ctx.solveur().corriger(estime, ctx.cible());

    // Garde-fou : ne jamais engager plus de 35 % des réserves sans le sol
    if (m.dv() > ctx.reserves().dv_disponible() * 0.35) {
        ctx.alerte("Correction > 35%% des réserves — validation sol requise");
        ctx.differer(m);
        return;
    }

    ctx.executer(m);
    ctx.journal_bord("Correction exécutée : %.2f m/s", m.dv());
}
```

## 15.4 Le carnet
Documentation personnelle du personnage : formules, procédures, man pages de l'API, archives de graphes, journal de reconstitution d'une absence (voir 9.3). Transmis en passation (voir 3.5).

## 15.5 Banc d'essai et qualification du code
| Étape | Coût | Ce qui est détecté |
| :---- | :---- | :---- |
| 1. **Compilation** | Nul, instantané | Erreurs de syntaxe et de typage |
| 2. **Banc d'essai** | **Temps et budget** (ressource de recherche, voir 4.4) | Comportement du code contre un environnement simulé |
| 3. **Certification** | — | Attribution d'un **domaine de validité** |
| 4. **Téléversement** | Délai lumière | — |
| 5. **Exécution en vol** | Conséquences réelles | Ce que le banc n'a pas couvert |

**Le banc a un domaine de validité** : c'est un modèle, donc une approximation bornée — comme toute approximation moteur (voir 6.8) et toute donnée de fiabilité (voir 12.3.3). Il réduit le risque sans l'annuler.

**Effets de jeu** : un code certifié devient une donnée de fiabilité (voir 12.3) ; hors de son domaine, le comportement n'est pas couvert et l'échec en vol reste possible.

## 15.6 Mode opératoire
En Normal comme en Pro, le raisonnement se reconstruit à chaque analyse (voir 2.4) ; aucune bibliothèque de procédures rejouables n'est fournie.

---

# 16. Multijoueur
**Hors périmètre de la version 1.** Motifs : le code de vol ne s'exécute que sur la machine de son auteur (voir 18), et les missions relativistes rompent une temporalité partagée (voir 14.4). Pistes conservées pour l'avenir : coopératif 2-4 joueurs, langage de script restreint ou exécution serveur, traitement spécifique du temps propre.

---

# 17. Direction artistique, environnement 3D et son

## 17.1 Principe
**Réalisme complet niveau UE5** pour tout (corps célestes, vaisseaux, stations, environnements). L'esthétique **sert la lisibilité technique** : vues en coupe, schémas, télémétrie et rendu réaliste coexistent.

## 17.2 Répartition procédural / authoré
| Catégorie | Traitement | Justification |
| :---- | :---- | :---- |
| **Corps célestes, terrains, ciels** | Procédural, piloté par les éphémérides et les données réelles | Problème largement résolu ; l'échelle interdit toute approche manuelle |
| **Véhicules** | Rendus depuis les données du ship builder | Le catalogue de pièces (voir 12.1) et l'éditeur en coupe (voir 12.2) fournissent déjà la géométrie : un véhicule assemblé par le joueur doit être rendu, pas modélisé |
| **Modules de Novellus** | Authorés, réutilisés entre paliers | Nombre fini et connu (dix modules), forte valeur d'identité |
| **Intérieurs** | Authorés, périmètre strictement limité | Poste Vigie et vues de contrôle |
| **Interfaces, schémas, télémétrie** | Authorés, priorité maximale | C'est là que se passe le jeu |

Aucun contenu visuel ne croît proportionnellement à la liberté de conception.

## 17.3 Environnement 3D : scène unique à l'échelle 1:1
Le monde d'ARES est **une seule scène 3D persistante qui *est* le système solaire à l'échelle 1:1** — mêmes distances, tailles et positions que le substrat physique (voir 7.1, 7.3). **Pas** de « niveau de mission » distinct de la carte ni d'instance par vaisseau : tous les corps, stations et véhicules actifs (y compris les missions PNJ en vol, voir 9.3) coexistent dans **ce même espace**, à leur position réelle, et s'y déplacent réellement.
- **La carte et le monde sont la même chose** : ce que d'autres jeux appellent « carte » est la scène vue de loin (voir 8.3).
- **Le contenu ne se duplique pas** : corps procéduraux (voir 17.2), véhicules rendus depuis le ship builder (voir 12.1, 12.2).
- **Deux LOD à ne jamais confondre** : LOD de *simulation* (finesse de calcul par phase, voir 7.2) et LOD de *rendu* (finesse d'affichage selon la distance caméra, voir 17.4, 18).

## 17.4 Caméra libre et continuité de vue
Caméra **libre et continue** : de la vue système (milliards de km) au plan vaisseau (mètres) par simple zoom, car le vaisseau **est déjà dans la scène** — rien à charger.
- **Cibles** : verrouillage sur tout objet actif ; la touche M est un signet de caméra, pas un écran (voir 8.3).
- **Observer, pas piloter** : la caméra ne pilote pas et ne modifie **jamais** la simulation ; le stick reste hors périmètre (voir 1.5), les manœuvres se commandent au terminal (voir 7.4, 9.1).
- **Couplage au temps** : sous forte accélération (voir 14.2), le mouvement affiché est interpolé ; les manœuvres fines forcent un rythme lent (voir 14.3).
- **Superposition technique** : trajectoires, corridor, vecteurs, télémétrie (voir 8.3) ancrés aux positions réelles, lisibles à toute distance.

## 17.5 Son
**Silence spatial réaliste** dans le vide, ambiances crédibles à l'intérieur (ventilation, structure, alarmes, radio). Le délai de communication est audible autant qu'informatif.

---

# 18. Plateforme et contraintes techniques
- **Plateforme cible** : Windows.
- **Solo hors-ligne complet** : aucune dépendance serveur pour la persistance temporelle.
- **Moteur physique** : n-corps, J2, budgets de masse/delta-v, rentrée, navigation avec incertitudes ; hiérarchie de simulation par phase (voir 7.2).

**Contraintes issues du terminal (voir 15.1).**
| Contrainte | Exigence | Conséquence de production |
| :---- | :---- | :---- |
| **Taille de distribution** | Embarquer compilateur, en-têtes et outils associés | Plusieurs centaines de Mo à budgéter explicitement dans la distribution |
| **Isolation** | Exécution du code joueur en processus séparé, limites de temps et de mémoire, interception des signaux | Un pointeur invalide produit un échec de mission, jamais un crash du jeu |
| **Déterminisme** | Journalisation des exécutions en vol avec leurs entrées | Un rechargement rejoue le résultat enregistré au lieu de recalculer ; les sauvegardes restent reproductibles |
| **Solo hors-ligne** | Le code joueur ne s'exécute que sur la machine de son auteur | Aucun partage de code entre joueurs ; contrainte structurante pour le multijoueur (voir 16) |

**Environnement 3D à l'échelle 1:1 : exigences moteur (voir 17.3–17.4).**
| Exigence | Ce qu'elle impose | Raison |
| :---- | :---- | :---- |
| **Coordonnées monde larges (double précision)** | Positions stockées en double précision dans un repère héliocentrique (ou barycentrique) absolu | À l'échelle du système solaire, la simple précision provoque un tremblement métrique inacceptable |
| **Rebasing de l'origine du monde** | L'origine de rendu suit la caméra ; le GPU calcule en simple précision *près* de la caméra, la simulation vit dans le repère absolu double précision | Découple la stabilité d'affichage de l'immensité des distances |
| **LOD de rendu par taille apparente** | Chaque corps et véhicule change de représentation selon sa taille à l'écran (point ou impostor à l'échelle système, géométrie complète de près), en transition continue | Autorise le zoom carte→vaisseau sans chargement ni changement de scène |
| **Découplage rendu / simulation** | La caméra et le rendu sont en **lecture seule** sur la simulation déterministe (voir la contrainte de déterminisme ci-dessus) | La vue n'altère jamais l'état ; un rechargement rejoue à l'identique |
| **Couplage au facteur temps** | Le rendu du mouvement est interpolé sous forte accélération temporelle ; les manœuvres fines forcent un rythme lent (voir 14.2–14.3) | Un système solaire 1:1 ne peut afficher un mouvement fluide à temps très accéléré |

Le **LOD de rendu est distinct du LOD de *simulation*** de 7.2. Seuils exacts à calibrer (voir Annexe E).

---

# 19. Règles de cohérence scientifique

## 19.1 Invariants physiques
Aucune technologie ne contourne masse, énergie, chaleur, radiations, fiabilité ou budget. Le vol habité lointain dépend autant du support-vie, de la médecine et des radiations que du moteur (voir 5.10, 6.6).

## 19.2 Hiérarchie de déblocage
La hiérarchie rang / maturité / budget / infrastructure (voir 5.4) est explicite et non contournable.

## 19.3 Statut de l'antimatière
Jamais un simple « meilleur moteur » : changement de régime physique, industriel et narratif, fin d'arbre uniquement, jamais disponible pour un usage ordinaire dans le système solaire interne.

## 19.4 Relativité émergente
Les effets relativistes n'apparaissent **que** si le véhicule atteint effectivement une fraction significative de c. Aucun β cible : γ découle de l'architecture, donc du modèle de production d'antimatière (voir 5.12.12).

## 19.5 Cohérence du cadre
Uchronie strictement organisationnelle : Novellus est la station, Vigie le poste. Aucune rivalité inter-agences, aucune techno hors du réel.

## 19.6 Traçabilité
Toute approximation moteur est documentée et cohérente avec l'usage gameplay (voir 6.8). Le principe conservateur (voir 12.5) s'applique à la physique comme au logiciel : une approximation identifiée est autorisée, une approximation déguisée en certitude ne l'est pas.

## 19.7 Matrice de verrouillage
| Classe de mission | Puissance | Masse | Thermique | Radiations | Maintenance / durée |
| :---- | :---- | :---- | :---- | :---- | :---- |
| Satellite / LEO robotique | – | X | – | – | – |
| Interplanétaire robotique (électrique) | X | X | – | – | X |
| Système solaire externe (RTG) | X | – | – | – | X |
| Vol habité LEO | – | X | – | X (Van Allen) | X |
| Vol habité lunaire / cislunaire | X | X | X | X | X |
| Vol habité martien | X | X | X | X | X |
| Habitat / remorqueur nucléaire | X | X | X (radiateurs) | X (blindage) | X |
| Cargo lointain NEP | X | X | X (radiateurs) | X | X |
| Mission fusion longue portée | X | X | X | X | X |
| **Mission quasi-relativiste antimatière** | **X** | **X** | **X** | **X** | **X** |

Lecture : chaque X est un verrou à lever ; le plus contraignant fait foi.

---

# 20. Backlog de rédaction (versions ultérieures)
- formalisation numérique de la hiérarchie de simulation (tolérances, pas d'intégration) ;
- extension de la matrice de verrouillage (19.7) en table de coûts par verrou ;
- calibration des probabilités d'événements aléatoires (9.5) par phase et par durée ;
- paramétrage complet du modèle de débris (7.8) ;
- **modèle de production d'antimatière** (5.12.12) : débit, rendement, coût, confinement ;
- **calibration du banc d'essai** (15.5) : coût d'une campagne, largeur des domaines, taux d'échec logiciel visé ;
- **catalogue de nœuds du mode Normal** (2.2) : liste, typage, équivalence stricte avec l'API C++ ;
- **surface exacte des API `ares::sol` et `ares::vol`** (15.2–15.3) : signatures, types, erreurs, versionnement ;
- **modalités de reprise du multijoueur** (16).

---

# Annexe A — Glossaire
| Terme | Définition |
| :---- | :---- |
| **Δv (delta-v)** | Variation de vitesse ; « monnaie » énergétique d'une manœuvre orbitale. |
| **Isp (impulsion spécifique)** | Efficacité d'un propulseur ; en secondes, `ve = Isp · g0`. Plus élevé = moins de propergol pour un Δv donné. |
| **ve (vitesse d'éjection effective)** | Vitesse effective des gaz/particules éjectés ; détermine le Δv via Tsiolkovsky. |
| **Tsiolkovsky** | Équation reliant Δv, ve et ratio de masse `m0/mf`. |
| **T/W (poussée/poids)** | Rapport poussée sur poids ; > 1 requis pour décoller, ≪ 1 pour la propulsion électrique. |
| **Régime impulsionnel** | Manœuvre brève à forte poussée (chimique, NTP). |
| **Régime continu** | Poussée faible appliquée sur des semaines/mois (électrique, NEP). |
| **TRL** | Technology Readiness Level, 1 (concept) à 9 (opérationnel qualifié en vol). |
| **RTG** | Générateur thermoélectrique à radioisotopes ; source d'énergie robuste, faible puissance. |
| **NTP** | Nuclear Thermal Propulsion ; cœur nucléaire chauffant un propergol, Isp ~2× chimique. |
| **NEP** | Nuclear Electric Propulsion ; réacteur alimentant une propulsion électrique, faible poussée. |
| **EDL** | Entry, Descent, Landing ; phase de rentrée/descente/atterrissage. |
| **GCR** | Galactic Cosmic Rays ; rayonnement cosmique galactique, dose chronique difficile à blinder. |
| **SPE** | Solar Particle Event ; événement de particules solaires, dose aiguë sporadique. |
| **Ceintures de Van Allen** | Zones de radiation piégée autour de la Terre (protons/électrons). |
| **Gy / Sv** | Gray (dose absorbée) / Sievert (dose équivalente, pondérée par la qualité du rayonnement). |
| **β (bêta)** | Fraction de la vitesse de la lumière, `β = v/c`. |
| **γ (facteur de Lorentz)** | `γ = 1/√(1 − β²)` ; quantifie la dilatation du temps. |
| **Temps propre (τ)** | Temps mesuré à bord ; `dτ = dt/γ`. |
| **J2** | Terme dominant de l'aplatissement terrestre dans les perturbations orbitales. |
| **Coniques raccordées** | Approximation de trajectoire par arcs képlériens, utilisée en planification rapide. |
| **API sol (`ares::sol`)** | Surface d'API en lecture seule, sans conséquence, dédiée à l'analyse et à la conception (voir 15.2). |
| **API vol (`ares::vol`)** | Surface d'API du logiciel embarqué : écriture, conséquences réelles, qualification obligatoire (voir 15.3). |
| **Banc d'essai** | Environnement de rejeu du code de vol produisant une certification assortie d'un domaine de validité (voir 15.5). |
| **Domaine de validité** | Plage d'environnements et d'entrées sur laquelle un modèle, une donnée de fiabilité ou un code certifié est réputé valable ; hors de cette plage, le comportement n'est pas couvert. |
| **Carnet** | Documentation personnelle du personnage : formules, procédures, API, archives de graphes, journal d'absence (voir 15.4). |
| **Module Vigie** | Poste de travail de l'Architecte Mission à bord de la station Novellus (voir 1.4 et 11.1). |
| **Fonds de réserve** | Réserve budgétaire obligatoire d'ARES ; ses paliers pilotent les alertes de l'agence (voir 13.4). |
| **Confiance ARES** | Crédibilité personnelle de l'Architecte Mission auprès de l'institution, sur une échelle de 0 à 100 (voir 13.4). |
| **Rapidité (φ)** | Variable relativiste telle que `β = tanh φ` ; additive, contrairement aux vitesses (voir 6.7.3). |

---

# Annexe B — Constantes et ordres de grandeur

## Constantes physiques
| Constante | Valeur |
| :---- | :---- |
| Accélération standard `g0` | 9,80665 m/s² |
| Vitesse de la lumière `c` | 299 792 458 m/s |
| Constante de Stefan-Boltzmann `σ` | 5,670374 × 10⁻⁸ W·m⁻²·K⁻⁴ |
| Énergie d'annihilation (1 g d'antimatière) | ~9 × 10¹³ J (ordre de grandeur) |

## Impulsion spécifique par filière (référence de modèle)
| Filière | Isp (s) | Régime |
| :---- | :---- | :---- |
| Chimique solide | 250–280 | Impulsionnel |
| Chimique liquide | 300–460 | Impulsionnel |
| NTP | 850–1 000 | Impulsionnel |
| Électrique (Hall) | 1 500–3 000 | Continu |
| Électrique (ionique/grille) | 3 000–10 000 | Continu |
| NEP (thruster électrique) | 2 000–10 000 | Continu |
| Fusion (concepts) | 10⁴–10⁶ | Variable |
| Antimatière (cœur annihilant) | 10⁵–10⁷ | Continu/relativiste |

## Ordres de grandeur de Δv (référence)
| Manœuvre | Δv approximatif |
| :---- | :---- |
| Sol → LEO (avec pertes) | ~9,3–10 km/s |
| LEO → GTO | ~2,4 km/s |
| GTO → GEO | ~1,5 km/s |
| LEO → injection translunaire | ~3,1 km/s |
| LEO → injection transmartienne | ~3,6 km/s |
| Évasion depuis LEO | ~3,2 km/s |

Ces budgets se comptent en dizaines de km/s (β ~ 10⁻⁴) : aucun effet relativiste **perceptible** hors architectures antimatière ; même la fusion (~0,05–0,10 c) reste sous le seuil narratif (voir 6.7.2).

## Dose radiative (référence de modèle)
| Situation | Ordre de grandeur |
| :---- | :---- |
| Limite de dose de carrière (référence institutionnelle) | plusieurs centaines de mSv à ~1 Sv |
| Aller-retour martien (GCR, sans blindage lourd) | ~0,3–0,7 Sv |
| Grande éruption solaire non blindée | plusieurs Gy en heures (risque aigu) |

---

# Annexe E — Paramètres restant à calibrer
| Paramètre | Section | Dépend de |
| :---- | :---- | :---- |
| Seuils de score par rang | 3.3 | Durée de partie visée par acte |
| Débit, rendement, coût et capacité de confinement de l'antimatière | 5.12.12 | Vitesse maximale souhaitée en fin d'arbre |
| Taux exact des coûts fixes annuels | 13.2 | Tension recherchée sur l'accélération du temps |
| Base garantie de la dotation étatique (≈ 30 Md€ par défaut) | 13.1–13.2 | Sévérité de la pression d'inactivité : plus la base est basse sous les coûts fixes, plus l'oisiveté coûte |
| Seuils de bascule du LOD de rendu (taille apparente) | 17.4, 18 | Continuité visuelle carte↔vaisseau contre coût GPU |
| Distance de rebasing de l'origine monde | 18 | Stabilité numérique de l'affichage à l'échelle 1:1 |
| Accélération temporelle maximale à mouvement fluide | 14.2, 18 | Au-delà, le mouvement affiché est interpolé |
| Cible du fonds de réserve | 13.4 | Amplitude des chocs budgétaires acceptables |
| Coût en temps et en budget d'une campagne de banc d'essai | 15.5 | Fréquence souhaitée des défauts logiciels en vol |
| Largeur des domaines de validité délivrés par le banc | 15.5 | Taux d'échec logiciel visé |
| Barème de points du score de promotion | 3.3 | Rythme de progression de carrière |

---

*Fin du document — GDD ARES v1.2, édition concise.*
