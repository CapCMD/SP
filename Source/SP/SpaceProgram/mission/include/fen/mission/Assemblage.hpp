// fen/mission/Assemblage.hpp — L'ASSEMBLAGE EN ORBITE [GDD 5.2 branche 1, 11]
//
// « Transfert de propergol orbital, rendez-vous automatisé robuste, cadence
// élevée » : le GDD nomme les trois pièces depuis toujours, l'arbre porte les
// trois nœuds (`transfert_ergols`, `rdv_automatise`, `robotique_orbitale`), et
// AUCUN d'eux ne débloquait quoi que ce soit. Mesuré avant d'écrire ce fichier :
// deux contrats du catalogue étaient hors de portée d'un lancement unique, et un
// aller-retour martien BLINDÉ (182 t) dépassait le plus gros lanceur (130 t) —
// c'est-à-dire que protéger son équipage rendait le vol impossible.
//
// CE N'EST PAS UN CONTOURNEMENT DU PLAFOND DE MASSE : c'est un ARBITRAGE, et
// chacun de ses termes est un fait d'ingénierie réel.
//   . N lancements, c'est N FOIS le prix, et c'est surtout R^N de fiabilité :
//     neuf tirs à 0,98 rendent 0,83. La masse s'achète en risque.
//   . N-1 amarrages, chacun une opération qui peut échouer.
//   . L'assemblage DURE : la cadence d'un pas de tir n'est pas instantanée, et
//     ce temps se paie sur le calendrier du contrat.
//   . ET LES ERGOLS CRYOGÉNIQUES S'ÉVAPORENT PENDANT CE TEMPS. C'est LA
//     contrainte réelle des architectures assemblées, celle qui a tué plus d'un
//     projet sur le papier : il faut donc en lancer PLUS que nécessaire, ce qui
//     rallonge l'assemblage, ce qui en évapore davantage — un POINT FIXE, qui
//     peut diverger. Quand il diverge, l'architecture est impossible, et pour une
//     raison physique qu'aucun budget ne lève.
//
// C++ pur, aucune dépendance au reste de la boucle de mission.
#pragma once
#include <algorithm>
#include <cmath>
#include <string>

namespace fen::mission {

// --- Ce que l'agence sait faire en orbite, tiré de l'ARBRE [GDD 5.4] ---------
struct CapaciteAssemblage {
  // Sans rendez-vous automatisé robuste, il n'y a pas d'assemblage du tout : on
  // ne rejoint pas un élément en orbite par chance. C'est le verrou d'entrée.
  bool rdv_automatise{false};
  // La robotique d'assemblage fiabilise l'amarrage et raccourcit l'intégration.
  bool robotique_orbitale{false};
  // Le transfert d'ergols orbital, c'est le ravitaillement APRÈS assemblage :
  // les ergols ne passent plus des mois en orbite à s'évaporer, ils arrivent en
  // dernier. C'est très exactement la raison d'être de cette technologie.
  bool transfert_ergols{false};
};

// --- Constantes DÉCLARÉES, chacune adossée à un fait réel [GDD 6.8] ----------
// Intervalle entre deux tirs d'une même campagne. Navette : ~2 mois entre vols
// d'un même orbiteur ; Ares V visait ~1 tir/mois pour l'assemblage martien.
inline constexpr double INTERVALLE_TIRS_JOURS = 30.0;
// ... raccourci par la robotique orbitale : l'intégration ne mobilise plus une
// équipe d'EVA à chaque élément.
inline constexpr double INTERVALLE_TIRS_ROBOT_JOURS = 20.0;
// Ébullition passive d'un étage cryogénique, par jour. Réel : 0,1 à 1 %/jour
// selon l'isolation (Centaur ~1-3 %/j sur les premières versions ; le « zéro
// boil-off » est un objectif de recherche, pas un acquis). On retient 0,2 %/j,
// milieu bas de la fourchette — hypothèse OPTIMISTE, donc honnête à déclarer.
inline constexpr double EBULLITION_PAR_JOUR = 0.002;
// Fiabilité d'un amarrage automatisé. Réel : le rendez-vous est une opération
// mûre (Progress, ATV, Cargo Dragon), mais pas gratuite.
inline constexpr double P_AMARRAGE = 0.990;
inline constexpr double P_AMARRAGE_ROBOT = 0.996;
// Au-delà, ce n'est plus une campagne de lancement, c'est un programme national
// à soi seul. Borne DÉCLARÉE, et elle sert aussi de garde-fou au point fixe.
inline constexpr int MAX_LANCEMENTS = 20;

inline double intervalle_tirs_jours(const CapaciteAssemblage& c) {
  return c.robotique_orbitale ? INTERVALLE_TIRS_ROBOT_JOURS : INTERVALLE_TIRS_JOURS;
}
inline double p_amarrage(const CapaciteAssemblage& c) {
  return c.robotique_orbitale ? P_AMARRAGE_ROBOT : P_AMARRAGE;
}

// ═══ LA FRACTION D'ERGOLS QUI SURVIT À L'ASSEMBLAGE — FORME CLOSE EXACTE ═══
// Les ergols n'arrivent PAS d'un coup : la charge i-ème arrive à (i−1)·Δt et
// attend le départ, à (n−1)·Δt. Son temps de séjour est donc (n−i)·Δt, et la
// fraction survivante moyenne sur n charges égales est
//     (1/n) · Σ_{k=0}^{n-1} e^(−λ·k·Δt)
// c'est-à-dire une SÉRIE GÉOMÉTRIQUE, qui vaut exactement
//     (1 − e^(−λ·n·Δt)) / ( n · (1 − e^(−λ·Δt)) ).
// Aucune intégration numérique, et le cas n=1 rend 1 — un lancement unique
// n'attend pas. Utiliser la durée TOTALE aurait doublement puni : les premières
// charges attendent longtemps, la dernière pas du tout.
inline double fraction_ergols_survivante(int n, double lambda_par_jour,
                                         double intervalle_jours) {
  if (n <= 1 || lambda_par_jour <= 0.0 || intervalle_jours <= 0.0) return 1.0;
  const double x = lambda_par_jour * intervalle_jours;
  const double d = -std::expm1(-x);                 // 1 − e^(−x), stable près de 0
  if (d <= 0.0) return 1.0;                         // λ·Δt sous le plancher machine
  return -std::expm1(-x * n) / (n * d);
}

// --- LE PLAN D'ASSEMBLAGE ---------------------------------------------------
struct PlanAssemblage {
  bool   possible{false};
  int    n_lancements{1};
  double duree_jours{0.0};        // de la première charge au départ
  double p_segment{1.0};          // R_lanceur^n · R_amarrage^(n−1)
  double ergols_a_lancer_kg{0.0}; // ergols utiles + ce qui s'évaporera
  double ergols_evapores_kg{0.0};
  double masse_lancee_kg{0.0};    // ce qu'il faut mettre en orbite, en tout
  std::string why;
};

// `m_sec_kg` : ce qui ne s'évapore pas (structure, charge utile, étages secs).
// `m_ergols_kg` : les ergols qui doivent être PRÉSENTS au départ.
// Rend un plan à un seul lancement si tout tient d'un coup — auquel cas rien de
// ce fichier ne change quoi que ce soit au résultat, et c'est voulu.
inline PlanAssemblage planifier_assemblage(double m_sec_kg, double m_ergols_kg,
                                           double capacite_lanceur_kg,
                                           double fiabilite_lanceur,
                                           const CapaciteAssemblage& cap) {
  PlanAssemblage p;
  const double m0 = m_sec_kg + m_ergols_kg;
  if (!(capacite_lanceur_kg > 0.0) || !(m0 > 0.0)) {
    p.why = "MASSE OU CAPACITE INVALIDE";
    return p;
  }
  // Cas trivial : ça tient en un tir. Aucun amarrage, aucune attente, aucune
  // évaporation — le comportement d'avant, à l'identique.
  if (m0 <= capacite_lanceur_kg) {
    p.possible = true; p.n_lancements = 1;
    p.p_segment = fiabilite_lanceur;
    p.ergols_a_lancer_kg = m_ergols_kg;
    p.masse_lancee_kg = m0;
    return p;
  }
  if (!cap.rdv_automatise) {
    p.why = "ASSEMBLAGE ORBITAL NON QUALIFIE : RECHERCHER rdv_automatise";
    return p;
  }

  const double dt = intervalle_tirs_jours(cap);
  // ═══ LE TRANSFERT D'ERGOLS ORBITAL CHANGE LA NATURE DU PROBLÈME ═══
  // Sans lui, les ergols montent avec les éléments et attendent en orbite. Avec
  // lui, on assemble d'abord la structure SÈCHE puis on ravitaille à la fin :
  // les ergols n'attendent plus que l'intervalle d'un seul tir. C'est exactement
  // ce que cette technologie achète, et c'est ce qui la rend décisive.
  const double lambda = EBULLITION_PAR_JOUR;

  // POINT FIXE : il faut lancer plus d'ergols que nécessaire pour compenser
  // l'évaporation, ce qui ajoute des tirs, ce qui allonge l'attente, ce qui en
  // évapore davantage. On itère jusqu'à stabilité du NOMBRE DE TIRS (un entier,
  // donc la convergence est franche ou elle n'a pas lieu).
  int n = static_cast<int>(std::ceil(m0 / capacite_lanceur_kg));
  double ergols_lances = m_ergols_kg;
  for (int iter = 0; iter < 64; ++iter) {
    // Avec transfert d'ergols, seule la dernière livraison compte : le séjour
    // est celui d'UN intervalle, pas de toute la campagne.
    const double frac = cap.transfert_ergols
                          ? std::exp(-lambda * dt)
                          : fraction_ergols_survivante(n, lambda, dt);
    if (!(frac > 0.0)) { n = MAX_LANCEMENTS + 1; break; }
    ergols_lances = m_ergols_kg / frac;
    const double total = m_sec_kg + ergols_lances;
    const int n2 = static_cast<int>(std::ceil(total / capacite_lanceur_kg));
    if (n2 > MAX_LANCEMENTS) { n = n2; break; }
    if (n2 == n) break;                     // stable : le point fixe est atteint
    n = n2;
  }
  if (n > MAX_LANCEMENTS) {
    // DEUX FAÇONS DE NE PAS Y ARRIVER, et le refus doit les distinguer : trop
    // lourd pour la cadence, ou l'ébullition mange la campagne plus vite qu'on
    // ne la remplit. La seconde ne se résout pas en ajoutant des tirs.
    p.why = cap.transfert_ergols
              ? "ASSEMBLAGE HORS D'ECHELLE : PLUS DE 20 LANCEMENTS"
              : "L'EBULLITION DES ERGOLS DIVERGE : RECHERCHER transfert_ergols";
    return p;
  }

  p.possible = true;
  p.n_lancements = n;
  p.duree_jours = (n - 1) * dt;
  p.ergols_a_lancer_kg = ergols_lances;
  p.ergols_evapores_kg = ergols_lances - m_ergols_kg;
  p.masse_lancee_kg = m_sec_kg + ergols_lances;
  // ═══ LA MASSE S'ACHÈTE EN RISQUE ═══ N tirs, N−1 amarrages, tous à réussir.
  p.p_segment = std::pow(fiabilite_lanceur, n) * std::pow(p_amarrage(cap), n - 1);
  return p;
}

} // namespace fen::mission
