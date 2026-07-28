// fen/ephem/Satellites.hpp — LES LUNES MAJEURES [GDD 7.1, 7.3]
//
// Standish ne tabule que les planètes majeures. Titan avait donc été placé à la
// main dans Ephemeris.cpp, sur une orbite circulaire dans le plan équatorial de
// Saturne, plan tiré du pôle IAU. Ce fichier GÉNÉRALISE ce modèle unique à toutes
// les lunes dont le projet possède le mesh — un seul modèle, une seule table, une
// seule approximation déclarée, au lieu d'un cas particulier par lune.
//
// ═══ LE MODÈLE, ET CE QU'IL VAUT [GDD 6.8, 12.5] ═══
// Orbite CIRCULAIRE de rayon `sma_m` autour du parent, dans un plan incliné de
// `incl_eq_deg` sur l'ÉQUATEUR du parent (plan de Laplace approché par l'équateur,
// ce qui est le bon plan pour les satellites proches), le pôle venant de
// `BodyOrientation` (WGCCRE) — le MÊME pôle qui incline les anneaux de Saturne à
// 26,7° au rendu. Les lunes tournent donc dans le plan de leur système, pas dans
// l'écliptique.
// CE QUI EST JUSTE : le plan, le rayon, la période, le SENS (Triton rétrograde).
// CE QUI EST NÉGLIGÉ, ET DÉCLARÉ : l'excentricité (Titan 0,029 ; les autres sont
// plus circulaires encore), la précession du nœud, et surtout la PHASE — l'origine
// est prise au nœud ascendant à J2000, faute d'éphéméride satellitaire. Une lune
// est donc au bon endroit SUR SON ORBITE À UN DÉPHASAGE PRÈS. C'est un modèle,
// pas un mensonge : rien de ce qui se joue (fenêtres, transferts, budgets) ne
// dépend aujourd'hui de la phase d'une lune.
// V2 : éphéméride satellitaire (SAT441/JUP365) sans changer cette interface.
//
// ═══ LA TABLE SE VÉRIFIE ELLE-MÊME ═══
// `period_days_ref` est la période sidérale PUBLIÉE. Elle n'entre dans AUCUN
// calcul : elle sert d'ORACLE. Le modèle dérive sa période de (a, GM du parent)
// par la 3e loi de Kepler, et le test exige l'accord. Une faute de frappe dans un
// demi-grand axe se voit donc immédiatement, au lieu de placer silencieusement une
// lune au mauvais endroit — c'est la leçon du piège n°21 rendue systématique.
#pragma once
#include <cmath>
#include <cstddef>

#include "fen/core/Constants.hpp"
#include "fen/core/Vec3.hpp"
#include "fen/core/Epoch.hpp"
#include "fen/ephem/BodyOrientation.hpp"
#include "fen/ephem/Ephemeris.hpp"

namespace fen::ephem {

struct SatelliteDef {
  Body        b;
  Body        parent;
  double      sma_m;             // demi-grand axe autour du parent (m)
  double      radius_m;          // rayon moyen (m)
  double      mu;                // GM du satellite (m^3/s^2)
  double      incl_eq_deg;       // inclinaison sur l'équateur du parent (>90 = rétrograde)
  double      period_days_ref;   // période sidérale PUBLIÉE — ORACLE, jamais un calcul
  const char* name;
};

// ═══ LES LUNES MAJEURES ═══
// Sources : JPL Solar System Dynamics (paramètres physiques et satellitaires) et
// IAU/WGCCRE pour les rayons moyens. Les GM sont ceux des satellites eux-mêmes.
// La Lune n'est PAS ici : elle a un modèle meilleur (série Montenbruck & Gill,
// Ephemeris.cpp), et un bon modèle ne se remplace pas par un modèle générique.
inline const SatelliteDef* satellite_table(std::size_t& n) {
  static const SatelliteDef T[] = {
    // --- Mars : deux cailloux, très proches et très rapides -------------------
    {Body::Phobos,   Body::Mars,     9.37600e6,  1.1267e4, 7.087e5,   1.075,   0.31891, "PHOBOS"},
    {Body::Deimos,   Body::Mars,     2.34632e7,  6.2000e3, 9.615e4,   1.788,   1.26244, "DEIMOS"},
    // --- Jupiter : les galiléennes, en résonance de Laplace 1:2:4 -------------
    {Body::Io,       Body::Jupiter,  4.21800e8,  1.8216e6, 5.9599e12, 0.036,   1.769138, "IO"},
    {Body::Europa,   Body::Jupiter,  6.71100e8,  1.5608e6, 3.2027e12, 0.466,   3.551181, "EUROPA"},
    {Body::Ganymede, Body::Jupiter,  1.07040e9,  2.6312e6, 9.8878e12, 0.177,   7.154553, "GANYMEDE"},
    {Body::Callisto, Body::Jupiter,  1.88270e9,  2.4103e6, 7.1793e12, 0.192,  16.689017, "CALLISTO"},
    // --- Saturne : Titan, plus les glacées ------------------------------------
    {Body::Mimas,    Body::Saturn,   1.85540e8,  1.9820e5, 2.5026e9,  1.574,   0.942422, "MIMAS"},
    {Body::Enceladus,Body::Saturn,   2.38040e8,  2.5210e5, 7.2027e9,  0.003,   1.370218, "ENCELADUS"},
    {Body::Tethys,   Body::Saturn,   2.94670e8,  5.3110e5, 4.12067e10,1.091,   1.887802, "TETHYS"},
    {Body::Dione,    Body::Saturn,   3.77420e8,  5.6140e5, 7.3113e10, 0.028,   2.736915, "DIONE"},
    {Body::Rhea,     Body::Saturn,   5.27070e8,  7.6380e5, 1.5394e11, 0.331,   4.518212, "RHEA"},
    {Body::Titan,    Body::Saturn,   1.22187e9,  2.5747e6, 8.978139e12, 0.348, 15.945421, "TITAN"},
    {Body::Iapetus,  Body::Saturn,   3.56084e9,  7.3450e5, 1.2051e11, 15.470, 79.330183, "IAPETUS"},
    // --- Uranus : le système est couché avec la planète (pôle IAU à ~98°) -----
    {Body::Miranda,  Body::Uranus,   1.29900e8,  2.3580e5, 4.4e9,     4.232,   1.413479, "MIRANDA"},
    {Body::Umbriel,  Body::Uranus,   2.66000e8,  5.8470e5, 8.61e10,   0.128,   4.144177, "UMBRIEL"},
    {Body::Titania,  Body::Uranus,   4.36300e8,  7.8890e5, 2.282e11,  0.079,   8.705872, "TITANIA"},
    {Body::Oberon,   Body::Uranus,   5.83500e8,  7.6140e5, 1.924e11,  0.068,  13.463239, "OBERON"},
    // --- Neptune : Triton est RÉTROGRADE (i > 90°) — capture, pas accrétion ---
    {Body::Triton,   Body::Neptune,  3.54759e8,  1.3534e6, 1.4285e12, 156.865, 5.876854, "TRITON"},
    // --- Pluton : Charon, quasi-binaire, verrouillé des deux côtés ------------
    {Body::Charon,   Body::Pluto,    1.95910e7,  6.0600e5, 1.059e11,  0.080,   6.387230, "CHARON"},
  };
  n = sizeof(T) / sizeof(T[0]);
  return T;
}

inline const SatelliteDef* satellite_def(Body b) {
  std::size_t n = 0;
  const SatelliteDef* T = satellite_table(n);
  for (std::size_t i = 0; i < n; ++i) if (T[i].b == b) return &T[i];
  return nullptr;
}

inline bool is_satellite(Body b) { return satellite_def(b) != nullptr; }

// Moyen mouvement DÉRIVÉ (rad/s) : 3e loi de Kepler. Jamais lu dans la table —
// c'est ce qui permet à `period_days_ref` de servir d'oracle.
//
// LA SOMME DES MASSES, PAS CELLE DU PARENT SEUL. Le problème à deux corps se
// résout sur µ = G(M + m). Pour presque toutes ces lunes m/M ~ 1e-4 et l'écart est
// invisible — mais PLUTON-CHARON est une quasi-binaire (Charon pèse 12 % du
// système) et `MU_PLUTO` est explicitement « hors Charon » : sans le terme m, la
// période de Charon sortait 5,9 % trop longue. L'oracle de période l'a dénoncé.
// NUANCE DÉCLARÉE, dans l'autre sens : pour Mars et les géantes, `body_mu` est le
// GM du SYSTÈME (planète + lunes), donc la masse du satellite y est déjà comptée
// une fois et on la compte deux. L'erreur qui en résulte vaut m/M, soit ~2e-4 au
// pire (Titan) — trois ordres de grandeur sous l'erreur du modèle circulaire.
// Corriger l'un en gardant l'autre est le bon compromis : on ne troque pas 6 %
// d'erreur réelle contre 0,02 % de scrupule.
inline double satellite_mean_motion(const SatelliteDef& s) {
  const double mu = body_mu(s.parent) + s.mu;
  return std::sqrt(mu / (s.sma_m * s.sma_m * s.sma_m));
}
inline double satellite_period_days(const SatelliteDef& s) {
  return cst::TWO_PI / satellite_mean_motion(s) / cst::DAY;
}

// Position du satellite RAPPORTÉE À SON PARENT, écliptique J2000 (m).
// Base du plan orbital : u = ligne des nœuds (équateur du parent ∩ écliptique),
// w = direction à 90° dans le plan orbital, inclinée de i au-dessus de l'équateur.
// Pour i > 90°, cos i < 0 : le moment cinétique bascule côté sud du pôle, donc le
// mouvement est RÉTROGRADE — Triton sort juste sans aucun cas particulier.
inline Vec3 satellite_parentcentric(const SatelliteDef& s, Epoch t) {
  const double theta = satellite_mean_motion(s) * t.tdb;   // phase depuis J2000
  const double i = s.incl_eq_deg * cst::DEG;
  const Vec3 pole = spin_axis_ecliptic(s.parent);
  // Repli sur x si le pôle est ~normal à l'écliptique (ligne des nœuds indéfinie).
  Vec3 u = cross(Vec3{0.0, 0.0, 1.0}, pole);
  u = (norm2(u) > 1e-12) ? unit(u) : Vec3{1.0, 0.0, 0.0};
  const Vec3 v = unit(cross(pole, u));
  const Vec3 w = v * std::cos(i) + pole * std::sin(i);
  return (u * std::cos(theta) + w * std::sin(theta)) * s.sma_m;
}

// Normale du plan orbital (unitaire, sens du moment cinétique). FORME FERMÉE :
// h ∝ r × v ∝ u × w = pole·cos i − v·sin i, constante dans ce modèle. Pour i > 90°
// elle bascule côté sud du pôle du parent : Triton en sort rétrograde tout seul.
inline Vec3 satellite_orbit_normal(const SatelliteDef& s) {
  const double i = s.incl_eq_deg * cst::DEG;
  const Vec3 pole = spin_axis_ecliptic(s.parent);
  Vec3 u = cross(Vec3{0.0, 0.0, 1.0}, pole);
  u = (norm2(u) > 1e-12) ? unit(u) : Vec3{1.0, 0.0, 0.0};
  const Vec3 v = unit(cross(pole, u));
  return unit(pole * std::cos(i) - v * std::sin(i));
}

// ═══ ROTATION SYNCHRONE : UN FAIT, PAS UN RÉGLAGE ═══ (2026-07-27)
//
// Les dix-neuf lunes de cette table sont TOUTES verrouillées par la marée : elles
// présentent en permanence la même face à leur primaire. C'était déjà écrit dans
// le rendu (« c'est un FAIT physique, pas un réglage ») mais ce n'était pas FAIT :
// `has_orientation` est faux pour dix-huit d'entre elles, si bien que le rendu
// retombait sur un axe = normale écliptique et une phase arbitraire. Elles
// tournaient donc sur elles-mêmes en montrant n'importe quelle face à leur
// planète — le même défaut que celui qui a été vu sur la Lune, en pire.
//
// Le verrou se construit à partir de la GÉOMÉTRIE, ce qui supprime toute phase à
// deviner : la longitude 0 EST le point sous-parent, et le pôle EST la normale
// orbitale. La sortie ne peut donc pas dériver de sa planète, quelle que soit
// l'époque.
//
// POURQUOI PAS LES ÉLÉMENTS IAU POUR TITAN (le seul de la table qui en a) : sa
// POSITION vient de ce fichier, dont la phase orbitale est explicitement
// arbitraire (voir l'en-tête). Appliquer son vrai W à une position dont la phase
// est fausse donnerait un point sous-Saturne QUI SE PROMÈNE — un verrou qui ne
// verrouille rien. Entre « le bon méridien à la mauvaise place » et « la même face
// à son primaire », c'est la seconde qui est vraie à l'écran. Elle redeviendra le
// vrai W le jour où une éphéméride satellitaire (SAT441/JUP365) donnera la phase.
//
// APPROXIMATION DÉCLARÉE [GDD 6.8] : obliquité propre nulle (le pôle est pris
// normal à l'orbite). C'est vrai à moins d'un degré pour ces lunes ; la Lune, qui
// s'en écarte de 6,7°, n'est pas ici — elle a son modèle IAU, et sa position
// réelle pour l'honorer.
inline BodyFrame satellite_frame_ecliptic(const SatelliteDef& s, Epoch t) {
  BodyFrame f;
  const Vec3 r = satellite_parentcentric(s, t);          // parent -> lune
  f.z = satellite_orbit_normal(s);
  f.x = unit(-r);                                        // lune -> parent = longitude 0
  f.y = unit(cross(f.z, f.x));
  f.x = unit(cross(f.y, f.z));                           // re-orthogonalisation
  return f;
}

} // namespace fen::ephem
