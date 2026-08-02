// fen/mission/Assistance.hpp — L'ASSISTANCE GRAVITATIONNELLE [GDD 3.x, 5.11, 14.3]
//
// [GDD, table des compétences] « Navigation et opérations interplanétaires :
// Hohmann, corrections, navigation profonde, capture, aérofreinage, ASSISTANCES »
// — colonne **Senior → Directeur**, pour « missions martiennes, astéroïdes,
// missions lunaires avancées, transferts complexes ». C'est donc une capacité de
// joueur, pas un détail d'implémentation.
//
// TOUTE LA BRANCHE ÉTAIT MORTE. Le balayage par graphe d'inclusion (piège n°85) a
// sorti `astro/Mga.hpp`, `astro/Mga1Dsm.hpp`, `astro/LocalRefine.hpp` et
// `astro/BPlane.hpp` : quatre en-têtes, aucun appelant hors des tests. Et
// contrairement à la branche de propagation numérique — qui est inatteignable
// POUR UNE RAISON ÉCRITE [GDD 6.8] — aucune décision ne justifiait celle-ci.
// `MgaProblem` porte même déjà les contraintes du JEU dans ses commentaires :
// `c3_max` « ce que le lanceur VEND », `tof_total_max` « ce que le RTG supporte ».
// La brique avait été écrite pour être branchée, et ne l'avait jamais été.
//
// ═══ CE QUE ÇA COÛTE, MESURÉ ═══
// Une évaluation unitaire prend **0,002 ms** ; la résolution complète d'un tour,
// avec ses multi-départs, **~1 s pour un survol et ~6 s pour trois**. C'est un
// calcul de PLANIFICATION : il ne tourne que sur demande explicite du joueur
// (`Session::choisir_tour`), et son résultat est ensuite LU par l'évaluation de
// plan, qui est rappelée à chaque reconstruction d'écran.
//
// ═══ CE QUE ÇA CHANGE, POUR LA MISSION QUI L'ACHÈTE (CAT-13, orbiteur externe) ═══
//                  Δv        masse au décollage   lanceur      coût     transit
//     DIRECT     8 524 m/s        10,4 t        L-C lourd    165 M$     893 j
//     E-E-J      6 301 m/s         5,4 t        L-B moyen    125 M$   1 717 j
// Deux ans et demi de vol de plus pour la MOITIÉ de la masse. Et le temps de vol
// n'est pas gratuit : il consomme les vivres, brûle le cœur du réacteur
// [GDD 12.4], perce les radiateurs, tire les avaries, et surtout il OCCUPE
// l'agence, qui paie ses coûts fixes sans encaisser son jalon [GDD 13.2]. Le troc
// « énergie de départ contre années de vol » a ses deux plateaux chargés, ce qui
// est exactement la décision que le GDD veut faire prendre.
//
// ═══ LE TOUR LONG EXISTE (2026-07-31, seconde passe) ═══
//
// LA PASSE PRÉCÉDENTE CONCLUAIT « les séquences à plusieurs survols ne convergent
// pas ; à trois survols le problème passe à 18 dimensions et le budget ne suffit
// pas ». **LE DIAGNOSTIC ÉTAIT FAUX, ET LA DIMENSION N'Y ÉTAIT POUR RIEN.** Deux
// causes indépendantes, toutes deux mesurées :
//
// 1. L'ÉLAGAGE INTERDISAIT LE VOL RÉEL. `vinf_min` était calculé, pour CHAQUE
//    survol, contre la cible FINALE. Or la borne de Hohmann ne dit qu'une chose :
//    après un survol, il faut assez d'énergie pour atteindre **le corps suivant**.
//    Exiger d'un survol de Vénus qu'il ouvre Jupiter demande |v∞| ≥ 11 380 m/s
//    quand la route vers la Terre n'en réclame que **2 704** — et Galileo, lui, y
//    est passé à ~5 000. Le vrai vol tombait donc dans la pénalité (5 × écart),
//    et l'optimiseur, poussé à faire l'impossible, payait en DSM géante. Le tour
//    « Galileo » à 18 019 m/s n'était pas un problème d'optimiseur : c'était
//    **notre contrainte qui refusait la trajectoire que Galileo a volée**.
// 2. LE RAFFINEUR ÉTAIT AVEUGLE. L'affinage interne du MBH était une DE dans une
//    boîte resserrée. `astro/LocalRefine.hpp` (gradient projeté, écrit et sous
//    oracle, sans appelant lui non plus) le remplace.
// 3. L'OBJECTIF IGNORAIT LE Δv DE DÉPART (voir plus bas, dans `evaluer_tour`).
//
// IL LES FALLAIT TOUTES. Mesuré sur E-V-E-E-J, Δv total :
//     élagage cible finale + DE resserrée  : 20 547 m/s     (l'état d'avant)
//     élagage cible finale + gradient      : 11 946 m/s
//     élagage corps suivant + DE resserrée : 12 495 m/s
//     élagage corps suivant + gradient     :  5 205 m/s
//     … + multi-départs + objectif total   : **5 372 m/s** (sur son opportunité)
//
// ═══ ET LE RÉSULTAT SE RECOUPE SUR DU PUBLIÉ ═══
// Le tour trouvé n'est pas « un tour » : c'est CELUI DE GALILEO, retrouvé sans
// qu'on lui donne autre chose que sa séquence et ses durées de jambe.
//
//     grandeur               modèle (2030)     Galileo (1989-1995)
//     C3 de départ           14,7 km²/s²       15,9 km²/s² (Shuttle/IUS)
//     Δv en espace profond   **56 m/s**        quelques dizaines de m/s
//     durée totale           6,25 ans          6,14 ans
//
// Un DSM de 56 m/s est la SIGNATURE d'un VEEGA réellement raccordé : la géométrie
// fait le travail, la propulsion ne fait que la mise en forme.
//
// ═══ CE QUI EST LIVRÉ, ET CE QUI NE L'EST PAS — MESURÉ ═══
//     tour                        Δv total     direct        verdict
//     E-E-J (Juno)                6 204-6 536  8 144-8 565   au catalogue, stable toute l'année
//     E-V-E-E-J (Galileo)         5 372-5 429  8 144-8 565   au catalogue, sur son OPPORTUNITÉ
//     E-V-V-E-J-S (Cassini)      23 281 m/s    9 614 m/s     refusé (non convergent)
//     E-J-S (Voyager, capture)   29 473 m/s    9 614 m/s     refusé — et c'est PHYSIQUE :
//                                                            un survol de Jupiter fait
//                                                            ARRIVER trop vite à Saturne
//                                                            (11 154 m/s d'insertion).
//                                                            Voyager ne s'y est pas inséré.
// LA GARDE EST DANS LE CODE, pas seulement dans ce commentaire : un tour dont le
// Δv total ne bat pas le transfert direct est REFUSÉ et le dit. Ajouter une
// séquence au catalogue est sûr — si elle ne converge pas, elle se refuse.
//
// ═══ UNE APPROXIMATION DÉCLARÉE [GDD 6.8] ═══
// La DISPERSION DE NAVIGATION d'une mission qui vole un tour est évaluée sur l'arc
// DIRECT : `mission/FlightTrace.hpp` sait construire un arc de Lambert, pas une
// suite de jambes. L'erreur va dans le sens CONSERVATEUR [GDD 12.5] — l'injection
// d'un tour est bien moins énergique (C3 16 contre 78), donc son erreur d'exécution
// est plus petite que celle qu'on lui impute.
//
// ═══ CE QUE LE MODÈLE NE DÉCIDE PAS [GDD 12.2, anti-feature 1.5] ═══
// LE JOUEUR CHOISIT LA SÉQUENCE. Le modèle ne cherche pas « le meilleur tour » à
// sa place : il calcule les CONSÉQUENCES de la séquence demandée, exactement comme
// l'atelier applique Tsiolkovsky au partage de Δv choisi. Trouver les époques d'une
// séquence donnée n'est pas décider — c'est résoudre.
#pragma once
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "fen/astro/LocalRefine.hpp"
#include "fen/astro/Mga1Dsm.hpp"
#include "fen/astro/Transfers.hpp"
#include "fen/career/Career.hpp"
#include "fen/core/Constants.hpp"
#include "fen/ephem/Ephemeris.hpp"

namespace fen::mission {

// RANG REQUIS — la table des compétences du GDD place les assistances dans la
// colonne Senior → Directeur. Sous ce rang, l'agence ne sait pas en planifier.
inline constexpr career::Rank RANG_ASSISTANCE = career::Rank::Senior;

// ═══ ET LA MATURITÉ, QUI EST L'AUTRE AXE DE [GDD 5.4] ═══
// L'arbre PORTAIT DÉJÀ les deux nœuds, et aucun des deux ne gardait quoi que ce
// soit : `gravity_assist` (branche 5, Junior) n'était qu'un prérequis de contrat,
// et `multi_survols` — « Architectures multi-survols », Senior, 210 jours et
// 45 M$ — ne débloquait RIEN. C'est le même défaut que les quatre nœuds de
// lanceurs et les trois d'assemblage orbital : nommé, payant, sans effet.
// Ils gardent maintenant exactement ce que leur nom dit : le premier l'assistance,
// le second les tours à PLUSIEURS survols.
//
// Posé par le driver, comme `lanceurs_qualifies` : le modèle pur ne connaît pas
// l'arbre. Les deux valent `true` par défaut = mode MODÈLE (oracles de physique),
// jamais le mode JEU.
struct CapaciteAssistance {
  bool gravity_assist{true};
  bool multi_survols{true};
};

// Une séquence CANONIQUE, réellement volée. On ne propose pas au joueur un espace
// de séquences infini : on lui propose ce qu'un programme a fait, avec le nom de
// ce programme — même principe que le catalogue de pièces [GDD 12.1].
//
// ⚠ LES BORNES DE DURÉE SONT CELLES DU VOL RÉEL, ET CE N'EST PAS DE LA COULEUR.
// La première rédaction annonçait « Juno » avec une première jambe bornée à
// 300-500 jours : Juno a volé un Δv-EGA de **796 jours**, donc le catalogue
// portait un héritage que ses propres bornes INTERDISAIENT. Élargir la jambe pour
// admettre la trajectoire volée n'a pas seulement rendu l'étiquette vraie, ça a
// aussi rendu le tour MEILLEUR (6 402 → 6 302 m/s) et l'optimisation quatre fois
// plus rapide : le bon bassin était juste hors de la boîte.
struct TourType {
  const char* id;
  const char* nom;
  const char* heritage;              // la mission qui l'a volé
  std::vector<ephem::Body> seq;      // [départ, survols..., arrivée]
  std::vector<double> rp_min_m, rp_max_m;
  std::vector<double> tof_lo_j, tof_hi_j;
  double tof_total_max_ans;
};

// ═══ PÉRIASTRES DE SURVOL : L'ALTITUDE RÉELLEMENT VOLÉE, PAS « AU-DESSUS DE
// L'ATMOSPHÈRE » ═══ [défaut mesuré le 2026-07-31]
// La première rédaction posait 6 600 km pour la Terre — 222 km d'altitude, choisi
// comme « on ne rase pas une planète ». Or **l'optimiseur colle TOUJOURS le
// périastre à sa borne basse** (mesuré : sept plancher différents, sept fois
// rp = plancher), parce qu'un survol plus près dévie plus et coûte moins. Cette
// borne n'est donc pas un garde-fou : c'est LA DÉCISION. Et à 222 km, le corridor
// du plan-B ne fait plus que 110 km contre 113 km de dispersion résiduelle, soit
// **P(survol) = 0,38** — un tour à pile ou face, pour une valeur qui n'avait été
// choisie que pour ne pas frotter l'atmosphère.
//
// LES VRAIES MISSIONS VOLENT PLUS HAUT, et leurs chiffres sont publiés :
//     Juno, survol terrestre 2013         : **559 km**
//     Galileo, Terre 1990 / Terre 1992    : 960 km / 303 km
//     Galileo, Vénus 1990                 : 16 106 km
//     Cassini, Terre 1999                 : 1 171 km
// À 559 km le corridor passe à ~440 km et P dépasse 0,99, pour **26 m/s** de plus.
// Le plancher du catalogue est donc l'altitude du VOL RÉEL, comme les durées de
// jambe — et le joueur peut l'élever (voir `plancher_survol_m`), ce qui achète du
// corridor et se paie en Δv.
//
// ⚠ ET C'EST LE PLUS BAS DES SURVOLS DE LA MISSION DE RÉFÉRENCE, PAS CHACUN LE
// SIEN — mesuré, puis corrigé. Poser à Vénus les 16 106 km que Galileo y a
// réellement volés ÉTOUFFE le tour : la déviation manque et la DSM repart à
// plusieurs km/s. Ces 16 106 km étaient un RÉSULTAT de la géométrie de 1989, pas
// une contrainte de conception. Ce qu'une mission de référence démontre, c'est
// jusqu'où elle est descendue (Galileo : 303 km) — le reste appartient à
// l'optimiseur. Même leçon que les bornes de jambe résonnantes : une valeur tirée
// d'UN vol réel n'est pas une loi.
//
// Le maximum, lui, borne seulement la recherche : ce n'est pas une règle physique.
//
// DURÉES DE JAMBE, RELEVÉES SUR LES DEUX VOLS :
//   Juno    2011-08-05 -> survol Terre 2013-10-09 (796 j) -> Jupiter 2016-07-05 (999 j)
//   Galileo 1989-10-18 -> Vénus 1990-02-10 (115 j) -> Terre 1990-12-08 (301 j)
//                      -> Terre 1992-12-08 (731 j) -> Jupiter 1995-12-07 (1094 j)
// Les bornes encadrent ces valeurs ; elles ne les imposent pas — l'optimiseur
// choisit dans la fenêtre, et la géométrie de 2030 n'est pas celle de 1989.
//
// ⚠ UNE TENTATIVE DE RESSERRAGE A ÉTÉ MESURÉE PUIS ANNULÉE. La jambe Terre-Terre
// de Galileo dure 731 j, c'est-à-dire la résonance 2:1 exacte (2 × 365,25) ; j'ai
// donc borné cette jambe à [710, 750] en croyant aider l'optimiseur à trouver un
// creux étroit. Résultat mesuré : le tour de 2030 passe de **5 205 à 9 556 m/s**
// — la bonne solution de cette année-là n'est PAS résonante 2:1, et je venais de
// l'interdire. Une hypothèse tirée d'UN vol réel n'est pas une loi ; les bornes
// encadrent, elles ne prescrivent pas.
inline const std::vector<TourType>& tour_catalog() {
  static const std::vector<TourType> v = {
    {"E-E-J", "Terre - Terre - Jupiter", "Juno (lancement 2011, survol terrestre 2013)",
     {ephem::Body::EarthBary, ephem::Body::EarthBary, ephem::Body::Jupiter},
     {cst::R_EARTH + 559.0e3}, {5.0e8}, {300.0, 900.0}, {850.0, 1600.0}, 7.0},
    {"E-V-E-E-J", "Terre - Venus - Terre - Terre - Jupiter",
     "Galileo VEEGA (lancement 1989, Jupiter 1995)",
     {ephem::Body::EarthBary, ephem::Body::Venus, ephem::Body::EarthBary,
      ephem::Body::EarthBary, ephem::Body::Jupiter},
     {6.0518e6 + 303.0e3, cst::R_EARTH + 303.0e3, cst::R_EARTH + 303.0e3},
     {5.0e8, 5.0e8, 5.0e8},
     {85.0, 225.0, 620.0, 820.0}, {200.0, 400.0, 840.0, 1350.0}, 7.0},
  };
  return v;
}

inline const TourType* find_tour(const std::string& id) {
  for (const auto& t : tour_catalog()) if (id == t.id) return &t;
  return nullptr;
}

// Δv de départ depuis l'orbite de parking pour atteindre un C3 donné. C'est LA
// grandeur qui relie l'assistance au reste du jeu : elle entre dans Tsiolkovsky
// comme n'importe quel autre Δv.
inline double dv_depart_pour_c3(double c3_m2s2, double r_park_m,
                                double mu = cst::MU_EARTH) {
  const double vinf = c3_m2s2 > 0.0 ? std::sqrt(c3_m2s2) : 0.0;
  return astro::injection_dv_from_circular(vinf, r_park_m, mu);
}

struct BilanTour {
  bool   evalue{false};
  bool   faisable{false};
  bool   rang_suffisant{true};
  double c3_m2s2{};
  double dv_depart_ms{};             // ce que coûte le C3 depuis le parking
  double dv_bord_ms{};               // manœuvres propulsives aux survols
  double dv_insertion_ms{};
  double dv_total_ms{};              // départ + bord + insertion
  double tof_ans{};
  double epoque_depart_tdb{};
  std::vector<double> rp_survol_m;
  std::vector<double> vinf_survol_ms;  // |v∞| à chaque survol — conservé par construction
  std::vector<double> epoques_tdb;     // départ, survols, arrivée : le tour est DATÉ
  // ═══ ET LA TRAJECTOIRE ELLE-MÊME ═══ [GDD 8.3, 17.3]
  // Deux morceaux par jambe (dérive vers la DSM, puis arc vers le corps suivant),
  // avec leur état de départ et leur durée. C'est ce qui permet de DESSINER le vol
  // dans le monde en propageant par Kepler — exactement le calcul que
  // l'évaluation vient de faire, donc la même trajectoire, pas une voisine.
  std::vector<astro::Mga1DsmArc> arcs;
  std::string cause;
};

// |v∞| MINIMAL AU SURVOL, DÉRIVÉ — et c'est la brique qui achète du temps de
// calcul. Après un survol, v_helio = v_planète + v∞, donc |v_helio| ≤ |v_p| + |v∞|.
// Pour ouvrir le corps VISÉ il faut la vitesse héliocentrique d'un Hohmann depuis
// le pivot, d'où |v∞| ≥ v_Hohmann − v_pivot. Un optimiseur qui l'ignore explore
// des tours PHYSIQUEMENT incapables et paie l'impossibilité en DSM géante.
//
// ⚠ « LE CORPS VISÉ » EST LE SUIVANT DE LA SÉQUENCE, JAMAIS LA CIBLE FINALE.
// La première rédaction appliquait la cible finale à tous les survols, ce qui
// exige d'un survol de Vénus qu'il ouvre Jupiter (11 380 m/s) là où la route vers
// la Terre n'en demande que 2 704 — et interdisait donc, littéralement, le vol de
// Galileo. Un élagage plus dur que la physique n'élague pas : il ment.
//
// CONTRÔLE : pour un survol terrestre vers Jupiter, cette formule rend
// **8 796 m/s**, et l'en-tête de `Mga1Dsm.hpp` annonce 8,79 km/s — obtenu
// indépendamment.
inline double vinf_min_survol(double r_pivot_m, double r_cible_m, double v_pivot_ms) {
  if (r_pivot_m <= 0.0 || r_cible_m <= 0.0) return 0.0;
  const double a = 0.5 * (r_pivot_m + r_cible_m);
  const double v_h = std::sqrt(cst::MU_SUN * (2.0 / r_pivot_m - 1.0 / a));
  return std::max(0.0, v_h - v_pivot_ms);
}

// PARAMÈTRES DE RECHERCHE — déclarés, et le déterminisme en dépend. Même graine,
// même tour : sans ça, « le meilleur tour trouvé » ne serait pas un résultat mais
// une anecdote, et deux affichages successifs se contrediraient.
// MBH ET NON DE SEULE : `Mga1Dsm.hpp` le dit — « le MBH saute de bassin en bassin ;
// il ne descend pas au fond », et DE seule reste coincée. Mesuré : sur E-E-J, DE
// seule rend 8 583 m/s (PIRE que le direct) et MBH rend **6 504** (mieux de 1 640).
//
// LE BUDGET N'EST PAS UN RÉGLAGE DE CONFORT, IL EST BALAYÉ. Sur le tour à trois
// survols, Δv total selon (sauts × itérations de raffinage) : 10×120 → 10 315 ;
// 20×120 → 9 532 ; 20×400 → 6 653 ; **30×400 → 5 205**. Le tour à un survol, lui,
// est au fond dès 10×60. C'est un calcul de PLANIFICATION, fait quand le joueur
// demande un tour — jamais dans une frame de rendu.
inline constexpr int TOUR_DE_POP = 60, TOUR_DE_GENS = 300;
inline constexpr int TOUR_MBH_HOPS = 6;
inline constexpr int TOUR_REFINE_ITERS = 400;
inline constexpr double TOUR_MBH_RADIUS = 0.25;
inline constexpr std::uint64_t TOUR_SEED = 4242;
inline constexpr int TOUR_MBH_STALL = 0;   // le redémarrage aléatoire ne sert pas (voir ci-dessous)
//
// ═══ ET UNE INSTABILITÉ QUI N'EN ÉTAIT PAS UNE ═══
// LE MÊME TOUR, DEMANDÉ À HUIT DATES espacées de 45 jours, rendait 5 374 m/s
// cinq fois et jusqu'à 10 976 les autres — alors que la fenêtre de recherche fait
// TROIS ANS, donc que le résultat aurait dû à peine bouger. J'y ai vu de la
// non-convergence et j'ai dépensé trois budgets à la combattre : redémarrage sur
// stagnation (aucun effet — en 18 dimensions un point aléatoire ne tombe dans
// aucun bassin), multi-départs (6 → 24 essais), raffinage plus ou moins profond
// (6 à 30 sauts). Aucun ne l'a fait disparaître, et **les mêmes trois dates
// échouaient à chaque fois** — ce qui n'est pas le comportement d'un tirage.
//
// LA DATE DE DÉPART TROUVÉE A TOUT DIT (piège n°90 : imprimer avant de supposer).
// Les bons résultats partaient tous à la MÊME DATE ABSOLUE — quatre balayages
// lancés entre décembre 2026 et avril 2027 visaient tous décembre 2029, à quinze
// jours près, pour 5 376 à 5 429 m/s — et les échecs étaient exactement ceux dont
// la fenêtre de trois ans **se terminait avant cette date**. Ce n'était donc pas
// l'optimiseur : c'est **une opportunité de lancement**, et le modèle la trouve
// dès qu'elle est dans la fenêtre. Un VEEGA n'est pas disponible tous les ans —
// Galileo, Cassini et Juno ont tous attendu leur alignement. La « variance »
// mesurée était de la mécanique céleste, et le refus disait vrai.
//
// CE QUI RESTE UTILE DE CETTE CHASSE : le multi-départ, et l'objectif corrigé
// (point 4 ci-dessus). Relevé final sur les mêmes huit dates :
//     E-E-J     : **6 229 à 6 396 m/s, huit fois sur huit** (avant : 6 276 à 9 933)
//     E-V-E-E-J : 5 160 à 5 445 six fois, 6 256 une fois, et un refus — ce dernier
//                 partant au BORD de la fenêtre, c'est-à-dire hors opportunité.
// Un tour à un survol est donc reproductible d'un bout de l'année à l'autre, et le
// tour long dit « oui » quand le ciel le permet et « non » sinon.
inline constexpr int TOUR_ESSAIS_BASE = 6, TOUR_ESSAIS_PAR_SURVOL = 9;
inline int tour_essais(std::size_t n_survols) {
  return TOUR_ESSAIS_BASE
       + TOUR_ESSAIS_PAR_SURVOL * static_cast<int>(n_survols > 0 ? n_survols - 1 : 0);
}

// Résout les ÉPOQUES d'une séquence choisie par le joueur. `c3_max` est ce que
// l'architecture accepte de payer au départ, et c'est une contrainte DURE ici —
// dans `Mga1Dsm` ce n'est qu'une pénalité de coût, ce qui convient à un optimiseur
// mais pas à un verdict de mission.
// ═══ CE QUE L'ARCHITECTE PEUT EXIGER DU SURVOL ═══ [GDD 3.1, 8.5]
// « L'Architecte décide COMMENT concevoir. » Puisque l'optimiseur colle le
// périastre à sa borne basse, cette borne EST une décision d'architecte, au même
// titre que la marge de correction ou le blindage : viser plus haut élargit le
// corridor du plan-B (donc la probabilité de réussir le survol) et se paie en Δv,
// parce qu'un survol plus lointain dévie moins. `plancher_survol_m` = altitude
// minimale imposée à TOUS les survols du tour ; 0 = ce que le vol de référence a
// fait (le catalogue).
inline BilanTour evaluer_tour(const TourType& t, const ephem::IEphemeris& eph,
                              Epoch fenetre_debut, double fenetre_jours,
                              double r_park_m, career::Rank rang,
                              double c3_max_m2s2 = 30.0e6,
                              const CapaciteAssistance& cap = {},
                              double plancher_survol_m = 0.0) {
  BilanTour b;
  b.evalue = true;
  if (rang < RANG_ASSISTANCE) {
    b.rang_suffisant = false;
    b.cause = std::string("assistance gravitationnelle : rang ")
            + career::rank_name(RANG_ASSISTANCE) + " exige";
    return b;
  }
  // LES DEUX AXES DE [GDD 5.4] SONT DISTINCTS : le rang AUTORISE, la maturité
  // REND CAPABLE. Un refus nomme la direction, comme partout ailleurs.
  if (!cap.gravity_assist) {
    b.cause = "NON QUALIFIE : RECHERCHER gravity_assist";
    return b;
  }
  if (t.seq.size() > 3 && !cap.multi_survols) {
    b.cause = "NON QUALIFIE : RECHERCHER multi_survols";
    return b;
  }
  astro::Mga1DsmProblem p;
  p.seq = t.seq;
  p.rp_min = t.rp_min_m;
  p.rp_max = t.rp_max_m;
  // LE PLANCHER DE L'ARCHITECTE, s'il en a posé un : il ne peut que RELEVER le
  // périastre (on ne descend jamais sous ce que le vol de référence a fait), et
  // il s'applique corps par corps, en altitude — 500 km au-dessus de Vénus n'est
  // pas le même rayon que 500 km au-dessus de la Terre.
  if (plancher_survol_m > 0.0)
    for (std::size_t k = 0; k + 2 < t.seq.size() + 1 && k < p.rp_min.size(); ++k) {
      const double r_corps = ephem::body_radius(t.seq[k + 1]);
      p.rp_min[k] = std::max(p.rp_min[k], r_corps + plancher_survol_m);
      if (p.rp_min[k] >= p.rp_max[k]) {
        b.cause = "plancher de survol au-dela de ce que la recherche explore";
        return b;
      }
    }
  p.t0_lo = fenetre_debut.tdb;
  p.t0_hi = fenetre_debut.tdb + fenetre_jours * cst::DAY;
  p.vinf_lo = 1000.0; p.vinf_hi = 6000.0;
  for (std::size_t i = 0; i < t.tof_lo_j.size(); ++i) {
    p.tof_lo.push_back(t.tof_lo_j[i] * cst::DAY);
    p.tof_hi.push_back(t.tof_hi_j[i] * cst::DAY);
  }
  p.tof_total_max = t.tof_total_max_ans * 365.25 * cst::DAY;
  p.tof_penalty = 1.0e-3;         // pénalité dérivable plutôt que falaise
  p.c3_max = c3_max_m2s2;
  const double R_final = ephem::body_radius(t.seq.back());
  p.rp_insert = 10.0 * R_final;
  p.a_insert  = 100.0 * R_final;
  // Élagage physique, dérivé (voir ci-dessus) : chaque survol doit ouvrir le
  // corps SUIVANT, jamais la cible finale.
  for (std::size_t k = 1; k + 1 < t.seq.size(); ++k) {
    const auto sp = eph.state(t.seq[k], ephem::Body::Sun, fenetre_debut);
    const auto sc = eph.state(t.seq[k + 1], ephem::Body::Sun, fenetre_debut);
    p.vinf_min.push_back(vinf_min_survol(norm(sp.r), norm(sc.r), norm(sp.v)));
  }

  const int F = astro::d1_flybys(p), D = astro::d1_nvars(p);
  std::vector<double> lo(D), hi(D);
  lo[0] = p.t0_lo;   hi[0] = p.t0_hi;
  lo[1] = p.vinf_lo; hi[1] = p.vinf_hi;
  lo[2] = 0.0; hi[2] = 1.0;  lo[3] = 0.0; hi[3] = 1.0;
  lo[4] = 0.05; hi[4] = 0.95;
  lo[5] = p.tof_lo[0]; hi[5] = p.tof_hi[0];
  for (int k = 0; k < F; ++k) {
    const int o = 6 + 4 * k;
    lo[o + 0] = -cst::PI;      hi[o + 0] = cst::PI;
    lo[o + 1] = p.rp_min[k];   hi[o + 1] = p.rp_max[k];
    lo[o + 2] = 0.05;          hi[o + 2] = 0.95;
    lo[o + 3] = p.tof_lo[k + 1]; hi[o + 3] = p.tof_hi[k + 1];
  }

  // ═══ ON MINIMISE CE QUE LA MISSION PAIE, DÉPART COMPRIS ═══
  // `Mga1Dsm::cost` est le Δv EMBARQUÉ (DSM + insertion) avec le C3 en simple
  // pénalité au-delà du plafond : formulation standard des problèmes MGA, où le
  // lanceur offre le C3. Ici le vaisseau part d'une orbite de PARKING et paie son
  // injection : sous le plafond, l'optimiseur était donc indifférent à un C3 de 30
  // km²/s² qui coûte pourtant 4 512 m/s au véhicule. Conséquence mesurée : les
  // solutions ratées sont toutes « C3 collé au plafond + DSM de 2 à 5 km/s », et
  // rien ne les distinguait des bonnes dans l'objectif.
  auto cout = [&](const std::vector<double>& x) {
    const astro::Mga1DsmResult r = astro::mga1dsm_evaluate(p, eph, x);
    if (!r.feasible) return r.cost;
    return r.cost + dv_depart_pour_c3(r.c3, r_park_m);
  };
  // DE pour trouver un bassin, puis MBH À RAFFINEUR LOCAL pour en atteindre le
  // fond. L'affinage n'est plus une DE dans une boîte resserrée mais le gradient
  // projeté de `LocalRefine.hpp`, LIBRE dans toute la boîte : il suit la vallée
  // jusqu'au bout au lieu de raboter au bord d'une sous-boîte.
  // ET LE TOUT EST RELANCÉ `TOUR_ESSAIS` FOIS, sur des graines DÉRIVÉES d'une
  // seule constante : c'est la DE qui choisit le bassin, donc c'est elle qu'il
  // faut diversifier (voir la note du budget). Le déterminisme est intact — les
  // graines sont fixes, l'ordre aussi, et l'on garde strictement le meilleur.
  astro::RefineOptions ro;
  ro.max_iter = TOUR_REFINE_ITERS;
  std::vector<double> x_best;
  double f_best = 1e300;
  const int n_essais = tour_essais(static_cast<std::size_t>(F));
  for (int essai = 0; essai < n_essais; ++essai) {
    const std::uint64_t graine = TOUR_SEED + 977ull * static_cast<std::uint64_t>(essai);
    const astro::DeResult d0 = astro::differential_evolution(
        cout, lo, hi, TOUR_DE_POP, TOUR_DE_GENS, graine);
    const astro::MbhLocalResult mb = astro::mbh_refine(
        cout, lo, hi, d0.x, TOUR_MBH_HOPS, TOUR_MBH_RADIUS, graine + 1, ro,
        TOUR_MBH_STALL);
    if (mb.f < f_best) { f_best = mb.f; x_best = mb.x; }
  }
  if (x_best.empty()) {
    b.cause = "aucun tour faisable sur cette fenetre";
    return b;
  }
  // SEULE CETTE ÉVALUATION-CI publie les arcs : c'est le point retenu.
  const astro::Mga1DsmResult r = astro::mga1dsm_evaluate(p, eph, x_best, true);

  if (!r.feasible) {
    b.cause = "aucun tour faisable sur cette fenetre";
    return b;
  }
  // LE PLAFOND DE C3 EST DUR ICI. Dans `Mga1Dsm` il n'est qu'une pénalité de coût
  // (`cost += 50 × dépassement`) — correct pour guider un optimiseur, faux pour
  // un verdict : le point resterait « faisable » avec un C3 que le lanceur ne
  // vend pas. Une première rédaction s'y est laissé prendre, et l'oracle du
  // plafond irréaliste est passé au vert sans rien vérifier.
  if (r.c3 > c3_max_m2s2) {
    b.cause = "C3 au-dela de ce que le lanceur vend";
    return b;
  }
  b.faisable = true;
  b.c3_m2s2 = r.c3;
  b.dv_depart_ms = dv_depart_pour_c3(r.c3, r_park_m);
  b.dv_bord_ms = r.dv_dsm_total;
  b.dv_insertion_ms = r.dv_insert;
  b.dv_total_ms = b.dv_depart_ms + b.dv_bord_ms + b.dv_insertion_ms;
  b.tof_ans = r.tof_total / (365.25 * cst::DAY);
  b.epoque_depart_tdb = r.t.front();
  b.rp_survol_m = r.rp;
  b.vinf_survol_ms = r.vinf_fb;
  b.epoques_tdb = r.t;
  b.arcs = r.arcs;
  return b;
}

// LE TROC, CHIFFRÉ. Un transfert direct vers la même cible, pour que le joueur
// voie ce que l'assistance lui achète — et ce qu'elle lui coûte en années.
struct Comparaison {
  double dv_direct_ms{}, tof_direct_ans{};
  double dv_tour_ms{},   tof_tour_ans{};
  double dv_economise_ms{}, annees_payees{};
  bool   vaut_le_coup{false};        // moins de Δv, quel que soit le temps
};

inline Comparaison comparer_au_direct(const BilanTour& tour, double c3_direct_m2s2,
                                      double vinf_arr_direct_ms, double tof_direct_ans,
                                      double r_park_m, double rp_insert_m,
                                      double a_insert_m, double mu_arrivee) {
  Comparaison c;
  c.dv_direct_ms = dv_depart_pour_c3(c3_direct_m2s2, r_park_m)
                 + astro::capture_dv_to_ellipse(vinf_arr_direct_ms, rp_insert_m,
                                                a_insert_m, mu_arrivee);
  c.tof_direct_ans = tof_direct_ans;
  c.dv_tour_ms = tour.dv_total_ms;
  c.tof_tour_ans = tour.tof_ans;
  c.dv_economise_ms = c.dv_direct_ms - c.dv_tour_ms;
  c.annees_payees = c.tof_tour_ans - c.tof_direct_ans;
  c.vaut_le_coup = tour.faisable && c.dv_economise_ms > 0.0;
  return c;
}

// ═══ LA GARDE : UN TOUR QUI NE BAT PAS LE DIRECT EST REFUSÉ ═══
// C'est ce qui empêche le modèle de mentir quand l'optimiseur n'a pas convergé.
// Un tour non convergé n'est pas « un tour cher » : c'est un résultat FAUX, et on
// ne le présente pas au joueur comme une option.
inline BilanTour evaluer_tour_utile(const TourType& t, const ephem::IEphemeris& eph,
                                    Epoch fenetre_debut, double fenetre_jours,
                                    double r_park_m, career::Rank rang,
                                    double dv_direct_ms,
                                    double c3_max_m2s2 = 30.0e6,
                                    const CapaciteAssistance& cap = {},
                                    double plancher_survol_m = 0.0) {
  BilanTour b = evaluer_tour(t, eph, fenetre_debut, fenetre_jours, r_park_m, rang,
                             c3_max_m2s2, cap, plancher_survol_m);
  if (b.faisable && dv_direct_ms > 0.0 && b.dv_total_ms >= dv_direct_ms) {
    b.faisable = false;
    b.cause = "tour non convergent : il coute plus cher que le transfert direct";
  }
  return b;
}

} // namespace fen::mission
