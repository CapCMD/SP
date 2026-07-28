// fen/ephem/BodyOrientation.hpp — ORIENTATION DES CORPS [IAU WGCCRE]
//
// L'éphéméride donne OÙ est un corps ; ce fichier donne COMMENT il est orienté :
// l'axe de rotation (pôle), l'obliquité, l'angle du méridien origine à l'époque,
// et le point SUB-SOLAIRE (la latitude du Soleil vu du corps — ce qui commande
// les saisons et l'hémisphère éclairé). C'est la donnée que le rendu doit
// consommer pour l'inclinaison d'axe et le terminateur jour/nuit : sans elle,
// Mars n'a pas ses 25°, la Terre n'a pas ses saisons — un tell d'arcade.
//
// SOURCE : rapport du groupe de travail IAU sur les coordonnées cartographiques
// (WGCCRE 2015). Les pôles α0/δ0 sont publiés dans le repère ÉQUATORIAL ICRF ;
// l'éphéméride du projet travaille en ÉCLIPTIQUE J2000. On convertit donc par
// l'obliquité (Constants::OBLIQUITY_J2000). Les termes de précession du pôle
// (en T) et les termes périodiques (Lune, satellites) sont NÉGLIGÉS en V1 —
// DÉCLARÉ : l'obliquité et la saison en sortent au dixième de degré, la phase de
// rotation à la minute d'angle sur quelques années.
#pragma once
#include <algorithm>
#include <cmath>

#include "fen/core/Constants.hpp"
#include "fen/core/Epoch.hpp"
#include "fen/core/Vec3.hpp"
#include "fen/ephem/Ephemeris.hpp"

namespace fen::ephem {

// Passage ÉQUATORIAL ICRF -> ÉCLIPTIQUE J2000 : rotation d'obliquité autour de
// l'axe x commun (l'équinoxe vernal). Vérifié par oracle : le pôle équatorial
// (0,0,1) part sur (0, sinε, cosε) ; le pôle écliptique (0,−sinε,cosε) revient
// sur (0,0,1).
inline Vec3 equatorial_to_ecliptic(const Vec3& v) {
  const double e = cst::OBLIQUITY_J2000;
  const double c = std::cos(e), s = std::sin(e);
  return { v.x, c * v.y + s * v.z, -s * v.y + c * v.z };
}

// Éléments de rotation IAU : pôle (α0, δ0) en degrés dans l'ICRF, angle du
// méridien origine W0 (deg) à J2000 et son taux (deg/jour). Un taux NÉGATIF =
// rotation rétrograde (Vénus).
struct RotationElements {
  double ra0_deg{};
  double dec0_deg{};
  double w0_deg{};
  double w_rate_deg_per_day{};
};

// Table WGCCRE 2015 (valeurs à J2000). Les corps hors table renvoient un modèle
// « terre-like » neutre plutôt qu'une valeur fausse (et `has_orientation` est
// faux pour eux).
inline bool has_orientation(Body b) {
  switch (b) {
    case Body::Sun: case Body::Mercury: case Body::Venus: case Body::EarthBary:
    case Body::Moon: case Body::Mars: case Body::Jupiter: case Body::Saturn:
    case Body::Titan: case Body::Uranus: case Body::Neptune: case Body::Pluto:
      return true;
    default:
      return false;
  }
}

inline RotationElements rotation_elements(Body b) {
  switch (b) {
    case Body::Sun:      return {286.13,     63.87,     84.176,   14.1844000};
    case Body::Mercury:  return {281.0103,   61.4155,   329.5988,  6.1385108};
    case Body::Venus:    return {272.76,     67.16,     160.20,   -1.4813688}; // rétrograde
    case Body::EarthBary:return {  0.00,     90.00,     190.147, 360.9856235};
    case Body::Moon:     return {269.9949,   66.5392,    38.3213,  13.17635815};
    case Body::Mars:     return {317.68143,  52.88650,  176.630,  350.89198226};
    case Body::Jupiter:  return {268.056595, 64.495303, 284.95,   870.5360000}; // Système III
    case Body::Saturn:   return { 40.589,    83.537,     38.90,   810.7939024};
    case Body::Titan:    return { 39.4827,   83.4279,   186.5855,  22.5769768};
    case Body::Uranus:   return {257.311,   -15.175,    203.81,  -501.1600928}; // "couché", rétrograde
    case Body::Neptune:  return {299.36,     43.46,     249.978,  541.1397757}; // terme périodique négligé
    case Body::Pluto:    return {132.993,    -6.163,    302.695,   56.3625225};
    default:             return {  0.00,     90.00,     190.147, 360.9856235}; // terre-like
  }
}

// Axe de rotation (pôle nord) dans le repère ÉCLIPTIQUE J2000, unitaire.
inline Vec3 spin_axis_ecliptic(Body b) {
  const RotationElements e = rotation_elements(b);
  const double a = e.ra0_deg * cst::DEG, d = e.dec0_deg * cst::DEG;
  const Vec3 pole_eq{ std::cos(d) * std::cos(a), std::cos(d) * std::sin(a), std::sin(d) };
  return unit(equatorial_to_ecliptic(pole_eq));
}

// Obliquité par rapport à l'écliptique [rad] : angle entre le PÔLE IAU (côté
// nord du plan invariable) et le pôle écliptique. ATTENTION à la convention : ce
// n'est PAS le "tilt axial" populaire pour les corps rétrogrades. Vénus rend
// ~1.2° (son pôle est quasi normal à l'écliptique) alors qu'on cite souvent
// 177° ; Uranus rend ~82° quand on cite 98°. Le caractère rétrograde vit dans le
// SIGNE de `w_rate_deg_per_day`, pas ici. Pour le rendu, la direction du pôle
// (`spin_axis_ecliptic`) + le signe de W suffisent et sont sans ambiguïté.
inline double obliquity_to_ecliptic_rad(Body b) {
  const Vec3 axis = spin_axis_ecliptic(b);
  const double c = std::clamp(axis.z, -1.0, 1.0);   // axis . (0,0,1)
  return std::acos(c);
}

// Angle du méridien origine à l'époque [deg, dans [0,360)]. Sa dérivée est le
// taux de rotation sidéral : c'est ce qui fait tourner le corps sous le rendu.
inline double prime_meridian_deg(Body b, Epoch t) {
  const RotationElements e = rotation_elements(b);
  const double d = t.tdb / cst::DAY;                 // jours depuis J2000
  double w = e.w0_deg + e.w_rate_deg_per_day * d;
  w = std::fmod(w, 360.0);
  if (w < 0.0) w += 360.0;
  return w;
}

// ═══ LE REPÈRE LIÉ AU CORPS — CE QUI MANQUAIT ═══ (2026-07-27)
//
// `spin_axis_ecliptic` + `prime_meridian_deg` donnent l'AXE et l'ANGLE, mais
// l'angle se mesure DEPUIS QUELQUE PART, et ce quelque part n'était nulle part.
// Le rendu composait « inclinaison » (rotation minimale de +Z vers le pôle, via
// `FQuat::FindBetweenNormals`) puis « rotation propre de W autour du pôle ». Or la
// rotation minimale a une composante azimutale ARBITRAIRE : W était donc compté
// depuis une origine quelconque. Conséquence : axe juste, VITESSE juste, PHASE
// FAUSSE. Invisible sur Jupiter (des bandes zonales n'ont pas de « face »),
// FLAGRANT sur la Lune — elle montrait la mauvaise face, ce qui a révélé le
// défaut. C'est le même défaut pour la Terre (mauvais méridien face au Soleil) et
// pour Mars (mauvais hémisphère), simplement moins reconnaissable.
//
// L'IAU définit l'origine sans ambiguïté : W est compté depuis le NŒUD Q de
// l'équateur du corps sur l'équateur ICRF (ascension droite α0 + 90°), dans le
// sens de la rotation. Le repère se construit donc entièrement :
//   Z = pôle,  Q = nœud,  X = Q tourné de W autour de Z,  Y = Z × X.
// VÉRIFIÉ à la main sur la Terre : AD(méridien de Greenwich) = 90° + W, soit
// 280,15° à J2000 — le temps sidéral de Greenwich y valait 280,46°, l'écart
// étant la précession que la table WGCCRE néglige (déclaré en tête de fichier).
//
// CONVENTION DE LONGITUDE : (X, Y, Z) est droitier, donc la longitude croît
// d'X vers Y, c'est-à-dire vers l'EST. C'est le repère des cartes modernes
// (planétocentriques est-positives, nord en haut), donc celui que les
// équirectangulaires du projet attendent — et ce que le rendu consomme.
struct BodyFrame {
  Vec3 x;   // longitude 0, latitude 0 : le MÉRIDIEN ORIGINE
  Vec3 y;   // longitude +90° est, sur l'équateur
  Vec3 z;   // pôle nord (= spin_axis_ecliptic)
};

inline BodyFrame body_frame_ecliptic(Body b, Epoch t) {
  const RotationElements e = rotation_elements(b);
  const double a = e.ra0_deg * cst::DEG, d = e.dec0_deg * cst::DEG;
  const double w = prime_meridian_deg(b, t) * cst::DEG;
  // Équatorial ICRF : le pôle, le nœud (à α0 + 90°), et le quadrant suivant.
  const Vec3 pole{ std::cos(d) * std::cos(a), std::cos(d) * std::sin(a), std::sin(d) };
  const Vec3 node{ -std::sin(a), std::cos(a), 0.0 };
  const Vec3 quad = cross(pole, node);          // 90° après le nœud, sens de rotation
  const Vec3 prime = node * std::cos(w) + quad * std::sin(w);
  // -> écliptique. La conversion est une rotation : elle conserve le produit
  // vectoriel, donc y se déduit après coup (un cross de moins, et strictement
  // orthonormal quoi que fasse l'arrondi).
  BodyFrame f;
  f.z = unit(equatorial_to_ecliptic(pole));
  f.x = unit(equatorial_to_ecliptic(prime));
  f.y = unit(cross(f.z, f.x));
  return f;
}

// ═══ LE VOILE NUAGEUX NE TOURNE PAS AVEC LE SOL ═══ (2026-07-27)
//
// Tout ce qui précède oriente le CORPS SOLIDE. Or deux des corps rendus portent
// une couche visible qui a sa propre rotation, et l'ignorer était un tell : le
// voile était peint sur la surface, donc solidaire d'elle au mètre près.
//
// Vent zonal MOYEN du voile visible, RELATIF À LA SURFACE [m/s], positif vers
// l'EST (le sens des longitudes croissantes). Ce n'est pas un réglage
// d'apparence : c'est la grandeur mesurée, et la période apparente s'en déduit
// (`cloud_deck_period_s` : 2πR/|v|), au lieu d'être saisie.
//   . TERRE : +10 m/s. Moyenne d'un écoulement zonal qui va de ~−5 m/s (alizés
//     d'est, tropiques) à ~+30 m/s (jet des moyennes latitudes) — un seul
//     nombre pour un défilement rigide est forcément un compromis, DÉCLARÉ
//     [GDD 6.8]. Il donne un tour en ~46 jours : invisible au temps réel (c'est
//     JUSTE : un nuage ne fait pas le tour du globe en une seconde), net aux
//     cadences jour/s et au-delà.
//   . VÉNUS : −100 m/s. La SUPER-ROTATION, l'un des faits les plus marquants du
//     système solaire : le sommet des nuages boucle en ~4,4 jours (le sol met
//     243 jours, dans le même sens rétrograde). Le signe négatif EST cette
//     rétrogradation. Le sol de Vénus tournant 55 fois moins vite, le vent
//     relatif et le vent absolu se confondent à 2 % près.
// Les autres corps rendent 0 : soit ils n'ont pas d'atmosphère, soit leur
// structure visible EST la surface (les bandes de Jupiter et Saturne tournent
// avec le corps ; leur rotation différentielle — le Système I équatorial devance
// le Système III de 7,4°/jour — demanderait un cisaillement en latitude, pas un
// défilement rigide, et n'est PAS faite ici).
inline double cloud_zonal_wind_ms(Body b) {
  switch (b) {
    case Body::EarthBary: return  10.0;
    case Body::Venus:     return -100.0;
    default:              return 0.0;
  }
}

// Période apparente du voile relativement à la surface [s], signée comme le vent
// (positive = vers l'est). 0 si le corps n'a pas de voile animé.
inline double cloud_deck_period_s(Body b) {
  const double v = cloud_zonal_wind_ms(b);
  if (v == 0.0) return 0.0;
  return cst::TWO_PI * body_radius(b) / v;
}

// LATITUDE SUB-SOLAIRE [rad] : la « déclinaison du Soleil » vue du corps, à
// partir de sa position héliocentrique écliptique. = arcsin( û_soleil . axe ),
// où û_soleil = direction corps->Soleil = -r/|r|. C'est le moteur des saisons
// et de l'hémisphère éclairé (Terre : +23.4° au solstice de juin).
inline double subsolar_latitude_rad(Body b, const Vec3& r_helio_ecliptic) {
  const double n = norm(r_helio_ecliptic);
  if (n <= 0.0) return 0.0;
  const Vec3 sun_dir = r_helio_ecliptic / (-n);      // corps -> Soleil
  const Vec3 axis = spin_axis_ecliptic(b);
  return std::asin(std::clamp(dot(sun_dir, axis), -1.0, 1.0));
}

} // namespace fen::ephem
