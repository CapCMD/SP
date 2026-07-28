// fen/mission/Graphe.hpp — LA PROGRAMMATION GRAPHIQUE PAR NŒUDS [GDD 2.2, 15.1]
//
// « Normal : le joueur assemble un GRAPHE (éphéméride → solveur → budget de
// masse → journal) dont chaque nœud expose ses entrées et sorties TYPÉES.
// L'équivalence est stricte : les nœuds Normal exposent exactement les fonctions
// de l'API C++. » [GDD 2.2]
//
// Jusqu'ici le joueur commandait sa correction avec trois boutons « − / + ». Il
// COMMANDAIT, mais il ne CALCULAIT pas — et [GDD 7.4] est formel : « toutes les
// manœuvres sont calculées PAR LE JOUEUR ». Ce fichier est la surface qui lui
// permet de les calculer.
//
// ═══ CE QUE CE GRAPHE N'EST PAS ═══ [GDD 1.5, 2.4]
// Pas une bibliothèque de procédures rejouables : les nœuds sont des PRIMITIVES
// (l'équivalent visuel d'un appel de fonction), jamais des recettes assemblées.
// Ce qui reste à refaire à chaque analyse, c'est l'ASSEMBLAGE du raisonnement —
// et c'est voulu.
//
// ═══ TYPAGE ═══
// Un nœud déclare le type de chacune de ses entrées et de sa sortie ; brancher
// un Δv là où on attend une durée est REFUSÉ, avec un motif. La validation de
// typage est l'assistance que [GDD 2.2] accorde au mode Normal — et elle est la
// contrepartie exacte de ce que le compilateur ferait en mode Pro.
//
// ═══ APPROXIMATION DÉCLARÉE ═══ [GDD 6.8]
// Le graphe est LINÉAIRE : chaque nœud consomme la sortie du précédent plus, le
// cas échéant, une constante saisie. Un graphe quelconque (plusieurs branches
// convergentes) demande une surface d'édition à deux dimensions ; la chaîne
// couvre exactement le raisonnement que le jeu pose aujourd'hui — mesurer,
// propager, écarter, résoudre, exprimer — et l'évaluateur ci-dessous n'a rien
// qui l'empêche d'accepter des branches le jour où l'éditeur les dessinera.
//
// C++ pur, aucune dépendance UE.
#pragma once
#include <string>
#include <vector>

#include "fen/core/Constants.hpp"
#include "fen/core/Vec3.hpp"
#include "fen/mission/Manoeuvre.hpp"

namespace fen::mission {

// ═══ LES TYPES QUI CIRCULENT ═══ — ceux de l'API, pas des « nombres ».
enum class TypeSignal { Aucun = 0, Etat, Vecteur, Duree, Transition, DvRsw };

inline const char* type_nom(TypeSignal t) {
  switch (t) {
    case TypeSignal::Etat:       return "ETAT";
    case TypeSignal::Vecteur:    return "VECTEUR";
    case TypeSignal::Duree:      return "DUREE";
    case TypeSignal::Transition: return "TRANSITION";
    case TypeSignal::DvRsw:      return "DV RSW";
    default:                     return "-";
  }
}

// ═══ LES NŒUDS ═══ — chacun EST une fonction de l'API `ares::sol`/`ares::vol`.
// L'équivalence stricte de [GDD 2.2] se lit dans cette table : un nœud, un
// appel. Rien ici ne fait plus que ce que le joueur pourrait écrire en Pro.
enum class TypeNoeud {
  SolutionNav = 0,   // ctx.navigation().solution()   -> ETAT estimé
  TempsRestant,      // ctx.cible().date() - maintenant -> DUREE
  Propager,          // kepler_propagate(etat, duree)  -> ETAT
  EcartCible,        // ctx.cible().ecart_projete(etat) -> VECTEUR
  Transition,        // stm(etat, duree)              -> TRANSITION
  ResoudreDv,        // solveur.corriger(transition, ecart) -> VECTEUR (inertiel)
  VersRsw,           // rsw_basis(etat) . dv          -> DV RSW
  Commande,          // ctx.executer(m)               -> la sortie du graphe
};

struct NoeudDef {
  TypeNoeud   type;
  const char* nom;
  const char* appel;        // la fonction d'API que ce nœud EST
  TypeSignal  entree;       // ce qu'il consomme du nœud précédent
  TypeSignal  sortie;
  bool        besoin_etat;  // consomme AUSSI l'état courant (mémorisé en amont)
};

inline const std::vector<NoeudDef>& noeuds_disponibles() {
  static const std::vector<NoeudDef> v = {
    {TypeNoeud::SolutionNav,  "SOLUTION NAV",  "navigation().solution()",
     TypeSignal::Aucun,      TypeSignal::Etat,       false},
    {TypeNoeud::TempsRestant, "TEMPS RESTANT", "cible().date() - maintenant()",
     TypeSignal::Aucun,      TypeSignal::Duree,      false},
    {TypeNoeud::Propager,     "PROPAGER",      "kepler_propagate(etat, duree)",
     TypeSignal::Duree,      TypeSignal::Etat,       true},
    // L'écart se mesure sur l'ÉTAT, qu'il prend en MÉMOIRE et non dans le fil :
    // dans le raisonnement réel, on calcule la transition PUIS l'écart, tous
    // deux à partir du même état. Le déclarer autrement obligerait à repasser
    // par un état qu'on a déjà — et c'est l'oracle du graphe de référence qui
    // l'a montré, en refusant une chaîne pourtant juste.
    {TypeNoeud::EcartCible,   "ECART / CIBLE", "cible().ecart_projete(etat)",
     TypeSignal::Aucun,      TypeSignal::Vecteur,    true},
    {TypeNoeud::Transition,   "TRANSITION",    "stm(etat, duree)",
     TypeSignal::Duree,      TypeSignal::Transition, true},
    {TypeNoeud::ResoudreDv,   "RESOUDRE Dv",   "solveur().corriger(phi, ecart)",
     TypeSignal::Vecteur,    TypeSignal::Vecteur,    false},
    {TypeNoeud::VersRsw,      "VERS RSW",      "rsw_basis(etat) . dv",
     TypeSignal::Vecteur,    TypeSignal::DvRsw,      true},
    {TypeNoeud::Commande,     "COMMANDE",      "executer(manoeuvre)",
     TypeSignal::DvRsw,      TypeSignal::Aucun,      false},
  };
  return v;
}

inline const NoeudDef& noeud_def(TypeNoeud t) {
  for (const NoeudDef& d : noeuds_disponibles()) if (d.type == t) return d;
  return noeuds_disponibles()[0];
}

// ═══ L'ÉTAT D'ÉVALUATION ═══ — ce qui circule dans le fil.
struct Signal {
  TypeSignal type{TypeSignal::Aucun};
  Vec3   r{}, v{};        // ETAT
  Vec3   vec{};           // VECTEUR ou DV RSW
  double duree_s{0.0};    // DUREE
  StmBlocks phi{};        // TRANSITION
};

struct ResultatGraphe {
  bool        valide{false};    // typage correct de bout en bout
  bool        evalue{false};    // et l'évaluation a abouti
  int         noeud_fautif{-1};
  std::string motif;
  Vec3        dv_rsw{};         // ce que le graphe COMMANDE, s'il aboutit
  std::vector<TypeSignal> sorties;   // le type après chaque nœud, pour l'écran
};

// Évalue la chaîne. `vn` est la vue de navigation du vol en cours : c'est le
// CONTEXTE que les nœuds sources lisent, exactement comme `ares::vol::Contexte`.
inline ResultatGraphe evaluer_graphe(const std::vector<TypeNoeud>& chaine,
                                     const VueNavigation& vn) {
  ResultatGraphe res;
  res.sorties.assign(chaine.size(), TypeSignal::Aucun);
  if (chaine.empty()) { res.motif = "graphe vide"; return res; }

  Signal sig;                       // ce qui circule
  Signal etat_courant;              // le dernier ETAT vu (les nœuds `besoin_etat`)
  bool a_etat = false;

  for (std::size_t i = 0; i < chaine.size(); ++i) {
    const NoeudDef& d = noeud_def(chaine[i]);
    // ---- TYPAGE : le refus est motivé, comme une erreur de compilation -------
    if (d.entree != TypeSignal::Aucun && sig.type != d.entree) {
      res.noeud_fautif = static_cast<int>(i);
      res.motif = std::string(d.nom) + " attend " + type_nom(d.entree) +
                  ", recoit " + type_nom(sig.type);
      return res;
    }
    if (d.besoin_etat && !a_etat) {
      res.noeud_fautif = static_cast<int>(i);
      res.motif = std::string(d.nom) + " exige un ETAT en amont";
      return res;
    }

    // ---- ÉVALUATION ---------------------------------------------------------
    switch (d.type) {
      case TypeNoeud::SolutionNav:
        if (!vn.ok) { res.noeud_fautif = (int)i; res.motif = "aucune solution de navigation"; return res; }
        sig = {}; sig.type = TypeSignal::Etat; sig.r = vn.r_estime; sig.v = vn.v_estime;
        break;
      case TypeNoeud::TempsRestant:
        sig = {}; sig.type = TypeSignal::Duree; sig.duree_s = vn.reste_jours * cst::DAY;
        break;
      case TypeNoeud::Propager: {
        const auto K = astro::kepler_propagate(etat_courant.r, etat_courant.v,
                                               sig.duree_s, cst::MU_SUN);
        if (!K.converged) { res.noeud_fautif = (int)i; res.motif = "propagation non convergente"; return res; }
        sig = {}; sig.type = TypeSignal::Etat; sig.r = K.r; sig.v = K.v;
        break;
      }
      case TypeNoeud::EcartCible: {
        // L'écart projeté est celui que la VUE porte : le nœud le LIT, il ne le
        // recalcule pas — sinon deux endroits diraient la même chose.
        sig = {}; sig.type = TypeSignal::Vecteur; sig.vec = vn.manque_projete;
        break;
      }
      case TypeNoeud::Transition: {
        const StmBlocks P = kepler_stm(etat_courant.r, etat_courant.v,
                                       sig.duree_s, cst::MU_SUN);
        if (!P.ok) { res.noeud_fautif = (int)i; res.motif = "transition non calculable"; return res; }
        sig = {}; sig.type = TypeSignal::Transition; sig.phi = P;
        break;
      }
      case TypeNoeud::ResoudreDv: {
        // Δv = −Φ_rv⁻¹ · écart. Le nœud EXIGE d'avoir vu une TRANSITION : sans
        // elle, il n'y a rien à inverser — et c'est un refus de typage, pas un
        // plantage.
        if (!etat_courant.phi.ok) {
          res.noeud_fautif = (int)i; res.motif = "RESOUDRE Dv exige une TRANSITION en amont"; return res;
        }
        M3 inv;
        if (!inverse3(etat_courant.phi.rv, inv)) {
          res.noeud_fautif = (int)i; res.motif = "transition singuliere : correction impossible"; return res;
        }
        const Vec3 e = sig.vec;
        sig = {}; sig.type = TypeSignal::Vecteur; sig.vec = inv * (Vec3{} - e);
        break;
      }
      case TypeNoeud::VersRsw: {
        const Basis3 b = rsw_basis(etat_courant.r, etat_courant.v);
        const Vec3 dv = sig.vec;
        sig = {}; sig.type = TypeSignal::DvRsw;
        sig.vec = {dot(dv, b.R), dot(dv, b.S), dot(dv, b.W)};
        break;
      }
      case TypeNoeud::Commande:
        res.dv_rsw = sig.vec;
        sig = {}; sig.type = TypeSignal::Aucun;
        break;
    }

    // MÉMOIRE DU FIL : le dernier ETAT et la dernière TRANSITION restent
    // disponibles en aval — c'est ce qui permet à une chaîne LINÉAIRE d'exprimer
    // un calcul qui, dessiné, aurait deux branches.
    if (sig.type == TypeSignal::Etat) { etat_courant.r = sig.r; etat_courant.v = sig.v; a_etat = true; }
    if (sig.type == TypeSignal::Transition) etat_courant.phi = sig.phi;
    res.sorties[i] = sig.type;
  }

  if (chaine.back() != TypeNoeud::Commande) {
    res.noeud_fautif = static_cast<int>(chaine.size()) - 1;
    res.motif = "le graphe doit se terminer par COMMANDE";
    return res;
  }
  res.valide = true;
  res.evalue = true;
  return res;
}

// LE GRAPHE DE RÉFÉRENCE — celui que le carnet documente [GDD 15.4], et que le
// joueur doit REFAIRE à chaque analyse [GDD 2.4]. Il n'est PAS proposé comme
// point de départ : il sert d'oracle et de page de manuel.
inline std::vector<TypeNoeud> graphe_correction_reference() {
  return {TypeNoeud::SolutionNav, TypeNoeud::TempsRestant, TypeNoeud::Transition,
          TypeNoeud::EcartCible,  TypeNoeud::ResoudreDv,   TypeNoeud::VersRsw,
          TypeNoeud::Commande};
}

} // namespace fen::mission
