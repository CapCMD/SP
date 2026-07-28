// app/impesanteur.hpp — SE DÉPLACER EN IMPESANTEUR, À BORD DE NOVELLUS.
//
// C++ pur, SI strict (m, m/s, m/s²) : inclus des deux côtés de la frontière,
// JAMAIS d'entête UnrealEngine. Le rendu détecte le contact et pousse la capsule ;
// la LOI, elle, vit ici et est sous oracle.
//
// ═══ POURQUOI CE FICHIER EXISTE ═══ (2026-07-27)
// Le pawn utilisait `UFloatingPawnMovement`, le composant de vol libre d'UE. Il
// ne convenait pas, et pour DEUX raisons dont aucune n'est un réglage :
//
//   1. IL NE BOUGEAIT PAS DU TOUT. `UFloatingPawnMovement::TickComponent` teste
//      `PawnOwner->GetController()` et ne fait RIEN si le pawn n'est pas possédé.
//      Or il ne l'est jamais : `SetStationInControl` pose la CIBLE DE VUE, pas la
//      possession — l'entrée arrive par le HUD et le pont [doctrine du pont].
//      Le regard marchait (appliqué directement), le déplacement non.
//   2. IL NE SAIT PAS LAISSER DÉRIVER. `ApplyControlInputToVelocity` finit par
//      `Velocity = Velocity.GetClampedToMaxSize(MaxSpeed * entrée)` : sans entrée,
//      le plafond vaut ZÉRO et la vitesse est annulée à chaque frame. À quoi
//      s'ajoute un `Deceleration` explicite. C'est un composant de caméra libre —
//      on lâche la touche, on s'arrête net. C'est l'exact contraire de
//      l'impesanteur, et aucun réglage de ses trois paramètres n'y change rien.
//
// ═══ CE QU'EST VRAIMENT LA SENSATION D'IMPESANTEUR ═══
// Trois faits, et c'est tout le modèle :
//
//   . ON DÉRIVE. Rien ne freine. On garde sa vitesse jusqu'à ce que quelque chose
//     l'arrête. C'est LA sensation, et c'est celle qu'un amortissement supprime.
//   . ON N'ACCÉLÈRE QU'EN POUSSANT SUR QUELQUE CHOSE. Troisième loi de Newton :
//     sans point d'appui, pas de force. En plein volume on est presque impuissant —
//     « nager » dans l'air fonctionne (on déplace de l'air) mais c'est célèbre pour
//     être dérisoire, et c'est modélisé comme tel : vingt fois moins efficace.
//   . ON S'ARRÊTE EN S'AGRIPPANT. Il n'y a pas de « frein » : il y a une main
//     courante qu'on attrape. D'où une seule touche (MAJ) qui fait les deux choses
//     qu'une main courante fait — pousser fort, et retenir.
//
// APPROXIMATIONS DÉCLARÉES [GDD 6.8] :
//   . le corps est un POINT : pas de rotation propre, pas de moment cinétique. Un
//     astronaute qui pousse de travers se met à tourner ; ici le regard reste
//     commandé à la souris. Une rotation subie serait fidèle et injouable (le mal
//     des transports est une vraie sensation d'impesanteur, pas une qu'on veut) ;
//   . le choc est PARFAITEMENT INÉLASTIQUE sur la normale : on absorbe avec les
//     bras, on ne rebondit pas comme une bille. La composante tangentielle, elle,
//     survit intégralement (on glisse le long de la paroi) ;
//   . la traînée de l'air est négligée. Elle est réelle mais sa constante de temps
//     se compte en minutes : sur la traversée d'un module, elle ne se voit pas.
#pragma once
#include <cmath>

#include "fen/core/Vec3.hpp"

namespace fen::app {

// ═══ LES QUATRE GRANDEURS, ET D'OÙ ELLES VIENNENT ═══
// Ce ne sont pas des curseurs de maniabilité : chacune est une vitesse ou une
// accélération que le corps humain produit réellement en microgravité.
struct ReglagesImpesanteur {
  double v_max_ms;      // plafond de vitesse
  double poussee_ms2;   // accélération en appui sur une paroi / main courante
  double brasse_ms2;    // « nage » dans l'air, en plein volume
  double prise_ms2;     // décélération en s'agrippant
};

inline constexpr ReglagesImpesanteur IMPESANTEUR = {
  // 1,2 m/s. Les équipages se déplacent DÉLIBÉRÉMENT : une translation de routine
  // est de l'ordre de 0,2 à 0,5 m/s, une poussée franche approche 1 m/s. Traverser
  // un module à 2,2 m/s (l'ancienne valeur) n'est pas de l'impesanteur, c'est du
  // vol : à cette vitesse on se blesse contre une cloison.
  1.2,
  // 2,5 m/s² : une poussée des jambes amène à 0,5 m/s en 0,2 s.
  2.5,
  // 0,12 m/s² : vingt fois moins. On y arrive — donc jamais bloqué au milieu d'un
  // module — mais il faut dix secondes pour prendre de la vitesse. C'est
  // exactement le rapport qu'on veut faire SENTIR : sans point d'appui, on subit.
  0.12,
  // 4,0 m/s² : attraper une main courante à 0,5 m/s arrête en ~0,13 s. C'est un
  // choc, pas un freinage progressif.
  4.0,
};

// ═══ UN PAS DE LA LOI ═══
//   `dir`     — direction voulue dans le repère du regard (nulle = aucune entrée) ;
//   `appui`   — une paroi est-elle à portée de bras ? (le rendu le mesure) ;
//   `agrippe` — le joueur tient-il la main courante ? (MAJ)
//
// TROIS BRANCHES, ET AUCUNE QUATRIÈME. C'est la structure même du modèle :
//   . une direction est demandée -> on pousse (fort en appui, dérisoirement sinon) ;
//   . rien de demandé, mais on s'agrippe EN APPUI -> on retient ;
//   . tout le reste -> ON DÉRIVE, la vitesse est rendue TELLE QUELLE.
// Il n'y a nulle part d'amortissement passif, et c'est le point du fichier.
inline Vec3 avancer_vitesse(const Vec3& v, const Vec3& dir, bool appui, bool agrippe,
                            double dt, const ReglagesImpesanteur& r = IMPESANTEUR) {
  Vec3 nv = v;
  const double n = norm(dir);
  if (n > 1.0e-9) {
    nv += (dir / n) * ((appui ? r.poussee_ms2 : r.brasse_ms2) * dt);
  } else if (agrippe && appui) {
    // S'AGRIPPER EXIGE UN APPUI : on ne se retient pas au vide. C'est ce qui rend
    // une dérive mal engagée irrattrapable jusqu'à la paroi suivante — et c'est
    // la bonne leçon, celle que le vide enseigne.
    const double s = norm(nv);
    const double ds = r.prise_ms2 * dt;
    nv = (s > ds) ? nv * ((s - ds) / s) : Vec3{};   // jamais d'inversion
  }
  const double s = norm(nv);
  if (s > r.v_max_ms) nv = nv * (r.v_max_ms / s);
  return nv;
}

// ═══ ET LE CHOC ? IL N'EST PAS ICI, ET C'EST VOULU ═══
// La loi du choc est simple à écrire — la composante normale est absorbée, la
// tangentielle survit — et elle a d'abord été écrite ici. Elle en a été RETIRÉE :
// l'appliquer demande la normale de la surface heurtée, et cette normale est
// précisément ce que le moteur ne rapporte PAS de façon fiable (deux prédicats de
// contact essayés, deux échecs mesurés — voir `AvancerEnImpesanteur`).
// Ce qui est fiable, c'est le DÉPLACEMENT OBTENU : la géométrie a laissé passer ce
// qu'elle a laissé passer, et c'est cela la vitesse d'après. Cette mesure DIT la
// même loi (contre un plan, glisser rend exactement la tangentielle) et couvre en
// plus les coins et les contacts multiples, qu'une normale unique ne sait pas
// décrire. Une loi qu'on ne peut pas alimenter correctement vaut moins qu'une
// mesure — et deux mécanismes pour un seul phénomène en valent zéro.

} // namespace fen::app
