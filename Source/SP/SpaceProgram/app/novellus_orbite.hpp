// app/novellus_orbite.hpp — L'ORBITE ET L'ATTITUDE DE NOVELLUS.
//
// C++ pur : inclus des deux côtés de la frontière, JAMAIS d'entête UnrealEngine.
//
// ═══ CE QUE CE FICHIER REMPLACE, ET POURQUOI ═══ (2026-07-27)
// Novellus était publiée par le helper de la FLOTTE (`Jeu::flotte_position_rel`,
// type `RelaisGeo`) : un cercle képlérien dans le PLAN ÉCLIPTIQUE. Le rayon était
// juste, donc la période l'était aussi — mais le PLAN était faux de 51,6°, et
// c'est visible dès qu'on regarde la Terre depuis la station : la station passait
// éternellement au-dessus de l'équateur… écliptique, c'est-à-dire nulle part.
// L'ISS ne survole pas l'écliptique, elle survole la Terre, sur un plan incliné
// de 51,64° sur l'ÉQUATEUR TERRESTRE — et cet équateur est lui-même à 23,44° de
// l'écliptique. Le vrai plan est donc doublement absent du modèle précédent.
//
// Ce fichier porte les trois grandeurs que le monde doit voir :
//   . LE PLAN     — inclinaison 51,64° sur l'équateur terrestre, nœud qui
//                   RÉGRESSE sous l'aplatissement de la Terre (J2, ~ −4,95°/jour) ;
//   . LA PÉRIODE  — 3e loi de Kepler à 418 km d'altitude, soit 92,9 min ;
//   . L'ATTITUDE  — cupola au NADIR, axe de vol dans la vitesse (vol « XVV » réel).
//
// ═══ MODÈLE DÉCLARÉ [GDD 6.8] ═══
// Orbite CIRCULAIRE d'éléments moyens : e = 0 (l'ISS vole à e ≈ 0,0005), un seul
// terme séculaire J2 (la régression du nœud), pas de traînée ni de reboosts.
// L'altitude est tenue à 418 km — c'est-à-dire que le reboost est supposé parfait,
// ce qui est plus honnête qu'une décroissance non modélisée. Position publiée
// comme ESTIMATION de navigation [GDD 7.5], comme le reste de la flotte.
#pragma once
#include <algorithm>
#include <cmath>

#include "fen/core/Constants.hpp"
#include "fen/core/Vec3.hpp"
#include "fen/ephem/BodyOrientation.hpp"
#include "fen/ephem/Ephemeris.hpp"

namespace fen::app {

// ═══ LES ÉLÉMENTS, ET D'OÙ ILS VIENNENT ═══
// Altitude et inclinaison sont les valeurs NOMINALES publiées de la Station
// spatiale internationale — les deux seuls chiffres d'orbite que ce fichier
// saisit ; tout le reste (période, vitesse, régression du nœud) en DÉCOULE.
inline constexpr double NOVELLUS_ALTITUDE_M = 418000.0;   // altitude moyenne entretenue
inline constexpr double NOVELLUS_INCLINAISON_DEG = 51.64; // sur l'équateur TERRESTRE
// PHASE À J2000 : longitude du nœud ascendant et argument de latitude. Il n'y a
// AUCUNE source de phase dans le projet (pas de TLE embarqué, et un TLE serait
// périmé en quelques semaines) : ces deux angles sont donc une CONVENTION
// déclarée, et non une mesure. Ce qu'ils ne changent pas : le plan a la bonne
// inclinaison, l'orbite la bonne période, la station la bonne attitude — c'est
// l'heure de passage au-dessus d'un point donné qui est conventionnelle.
inline constexpr double NOVELLUS_RAAN0_RAD = 0.0;
inline constexpr double NOVELLUS_ARGLAT0_RAD = 0.0;

// Demi-grand axe [m] : le rayon terrestre du CŒUR + l'altitude. Une seule source
// pour le rayon de la Terre, celle de l'éphéméride.
inline double novellus_sma_m() {
  return ephem::body_radius(ephem::Body::EarthBary) + NOVELLUS_ALTITUDE_M;
}

// Moyen mouvement [rad/s] : 3e loi de Kepler. La PÉRIODE n'est pas saisie, elle
// se déduit — 2π/n = 5 576 s = 92,9 min, la valeur réelle de l'ISS.
inline double novellus_moyen_mouvement() {
  const double a = novellus_sma_m();
  return std::sqrt(ephem::body_mu(ephem::Body::EarthBary) / (a * a * a));
}
inline double novellus_periode_s() { return cst::TWO_PI / novellus_moyen_mouvement(); }

// ═══ LA RÉGRESSION DU NŒUD (J2) ═══
// La Terre n'est pas une sphère : son bourrelet équatorial exerce un couple qui
// fait TOURNER le plan orbital autour de l'axe des pôles. Pour l'ISS c'est
// −4,95°/jour, soit un tour complet en 73 jours — l'effet qui commande le cycle
// bêta, les périodes d'éclairement permanent et l'heure locale des survols.
// Ignorer cette dérive, c'est figer le plan dans l'inertiel : faux au bout d'une
// journée de jeu, et le jeu se joue en mois.
//   dΩ/dt = −3/2 · J2 · (R/p)² · n · cos(i),  p = a (orbite circulaire)
// [Vallado §9.6 — terme séculaire du premier ordre]. Négatif pour une orbite
// prograde : le nœud recule vers l'ouest.
inline double novellus_raan_rate_rad_s() {
  const double a = novellus_sma_m();
  const double R = ephem::body_radius(ephem::Body::EarthBary);
  const double ratio = R / a;
  return -1.5 * cst::J2_EARTH * ratio * ratio * novellus_moyen_mouvement() *
         std::cos(NOVELLUS_INCLINAISON_DEG * cst::DEG);
}

// ═══ LE REPÈRE ÉQUATORIAL TERRESTRE, VU DE L'ÉCLIPTIQUE ═══
// L'inclinaison de l'ISS se compte sur l'ÉQUATEUR, l'éphéméride travaille dans
// l'ÉCLIPTIQUE : il faut donc le repère équatorial exprimé en écliptique. Il est
// bâti sur la MÊME primitive que l'orientation des corps
// (`ephem::equatorial_to_ecliptic`), pas sur une obliquité recopiée.
//   x = équinoxe vernal (axe commun aux deux repères, invariant) ;
//   z = pôle nord céleste ; y = z × x.
struct RepereEquatorial { Vec3 x, y, z; };

inline RepereEquatorial repere_equatorial_terrestre() {
  RepereEquatorial e;
  e.x = Vec3{1.0, 0.0, 0.0};
  e.y = ephem::equatorial_to_ecliptic(Vec3{0.0, 1.0, 0.0});
  e.z = ephem::equatorial_to_ecliptic(Vec3{0.0, 0.0, 1.0});
  return e;
}

// État de la station relativement à la Terre, repère ÉCLIPTIQUE J2000, SI.
struct NovellusEtat {
  Vec3 r;   // position [m]
  Vec3 v;   // vitesse  [m/s]
};

// ═══ L'ÉTAT À L'ÉPOQUE ═══ (t : secondes TDB depuis J2000)
// Deux angles avancent linéairement — l'argument de latitude (une orbite en
// 92,9 min) et la longitude du nœud (un tour en 73 jours) — et l'état s'écrit
// dans la base (n̂, m̂) du plan orbital courant :
//   n̂ = direction du nœud ascendant,  m̂ = 90° plus loin le long du mouvement.
//   r = a(cos u · n̂ + sin u · m̂)   ;   v = a·n(−sin u · n̂ + cos u · m̂)
// LA VITESSE EST ANALYTIQUE, pas une différence finie : elle est exacte, et elle
// reste exacte quelle que soit la cadence du temps de jeu.
// APPROXIMATION DÉCLARÉE : le terme de vitesse dû à la rotation du plan (a·dΩ/dt,
// soit 6,8 m/s sur 7 660 — 0,09 %) est omis, ce qui garde v exactement
// perpendiculaire à r. L'attitude n'en consomme que la DIRECTION.
inline NovellusEtat novellus_etat(double t_tdb) {
  const RepereEquatorial e = repere_equatorial_terrestre();
  const double a = novellus_sma_m();
  const double n = novellus_moyen_mouvement();
  const double i = NOVELLUS_INCLINAISON_DEG * cst::DEG;
  const double raan = NOVELLUS_RAAN0_RAD + novellus_raan_rate_rad_s() * t_tdb;
  const double u = NOVELLUS_ARGLAT0_RAD + n * t_tdb;

  const Vec3 noeud = e.x * std::cos(raan) + e.y * std::sin(raan);
  const Vec3 perp = e.x * (-std::sin(raan)) + e.y * std::cos(raan);
  const Vec3 m = perp * std::cos(i) + e.z * std::sin(i);

  NovellusEtat s;
  s.r = (noeud * std::cos(u) + m * std::sin(u)) * a;
  s.v = (noeud * (-std::sin(u)) + m * std::cos(u)) * (a * n);
  return s;
}

// ═══ L'ATTITUDE DE NOVELLUS : LA CUPOLA REGARDE LA TERRE ═══
//
// L'ISS ne dérive pas, cap fixe, dans l'inertiel : elle tient une attitude LVLH
// (« XVV », torque equilibrium) — axe de vol dans le vecteur vitesse, NADIR vers
// la Terre, où sont la cupola et les fenêtres d'observation. Elle fait donc un
// tour complet par orbite dans le repère inertiel, et c'est exactement ce qu'on
// doit voir.
//
// ═══ LE REPÈRE DU MODÈLE EST MESURÉ, PAS DEVINÉ ═══
// `Tools/diag_iss_repere.py` lit les boîtes des meshes du modèle extérieur et les
// confronte à la disposition RÉELLE de la station. Trois repères nommés,
// indépendants, sont d'accord (mesures en mètres, relatives au centre du modèle) :
//   . Zvezda x = −29,15 / Node2 x = +9,77 -> Zvezda est à l'ARRIÈRE, Node 2 à
//     l'avant : +X du modèle = l'AVANT (direction de vol) ;
//   . Node3 y = −5,51 (bâbord de Node 1 dans la vraie station) et Airlock
//     y = +4,94 (tribord) -> +Y du modèle = TRIBORD ;
//   . Cupola z = −0,90 contre Node3 z = +2,05, et la cupola est sur le port NADIR
//     de Node 3 -> −Z du modèle = LE NADIR, +Z = le zénith.
// Et l'envergure confirme l'ensemble : 108,3 m sur y (la poutre), 73,4 m sur x
// (la pile de modules), 30,6 m sur z. Ce faisceau de trois indices concordants
// est ce qui distingue une mesure d'un tirage à une chance sur six.
//
// ═══ POURQUOI L'ATTITUDE EST CALCULÉE ICI, EN C++ PUR ═══
// Elle l'était côté rendu. Or TROIS consommateurs en ont besoin, et ils doivent
// voir EXACTEMENT la même :
//   . le modèle EXTÉRIEUR (SPSolarSystem) ;
//   . la géométrie INTÉRIEURE (SPStation) — sans quoi la bascule de LOD à la
//     traversée de la coque ferait SAUTER l'orientation de la station ;
//   . la POSE DE CAMÉRA du handoff (`Session::pose_bord`), qui vit ici, en C++
//     pur, et qui doit sortir la caméra de la station dans la bonne direction.
// Une seule source, sous oracle [doctrine du pont : le rendu ne dérive rien].
//
// ═══ REPÈRE DE RENDU ═══
// Les axes sont rendus DÉJÀ MIRROITÉS en y (écliptique droitier -> monde de rendu
// gaucher), la conversion que tout le projet applique aux positions. Le rendu n'a
// plus qu'à en faire les lignes d'une matrice — les lignes d'une FMatrix UE sont
// les IMAGES des axes locaux, et ce triplet est propre (det = +1 : avec
// t = z × a et a ⊥ z unitaires, a·((z×a)×z) = a·a = 1).
struct AttitudeRendu {
  Vec3 avant;    // image de +X du modèle : la direction de vol
  Vec3 tribord;  // image de +Y du modèle
  Vec3 zenith;   // image de +Z du modèle : à l'opposé de la Terre (cupola en −Z)
};

// Miroir en y : écliptique (droitier) -> monde de rendu (gaucher). DÉCLARÉ, et
// identique à celui des positions (`EclToUeKmd`) et de la pose de bord.
inline Vec3 ecl_vers_rendu(const Vec3& v) { return Vec3{v.x, -v.y, v.z}; }

// r et v sont RELATIFS À LA TERRE, en écliptique (ce que publie `novellus_etat`).
// Le radial est ORTHOGONALISÉ contre l'avant : sur une orbite circulaire la
// correction est nulle, mais le repère reste orthonormé quoi qu'il arrive au
// modèle d'orbite plus tard. Repli sur l'identité si l'état est dégénéré.
inline AttitudeRendu novellus_attitude_rendu(const Vec3& r_ecl, const Vec3& v_ecl) {
  const AttitudeRendu identite{Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, 1}};
  const Vec3 avant = unit(ecl_vers_rendu(v_ecl));
  Vec3 zenith = unit(ecl_vers_rendu(r_ecl));
  if (norm2(avant) < 0.5 || norm2(zenith) < 0.5) return identite;
  zenith = unit(zenith - avant * dot(zenith, avant));
  if (norm2(zenith) < 0.5) return identite;   // vitesse purement radiale : indéfini
  return {avant, cross(zenith, avant), zenith};
}

// Applique l'attitude à un vecteur exprimé dans le repère du MODÈLE (repère de
// rendu, station non tournée) : c'est le produit v_monde = v_local × M, avec M la
// matrice dont les lignes sont les images des axes locaux. C'est ce dont
// `Session::pose_bord` a besoin pour faire sortir la caméra du bon côté.
inline Vec3 appliquer_attitude(const AttitudeRendu& a, const Vec3& v_local) {
  return a.avant * v_local.x + a.tribord * v_local.y + a.zenith * v_local.z;
}

} // namespace fen::app
