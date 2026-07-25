// fen/astro/Elements.hpp
// Éléments classiques osculateurs. Traitement EXPLICITE des cas dégénérés
// (e -> 0, i -> 0) : le jeu vise précisément l'orbite circulaire équatoriale
// (GEO), donc les singularités de Omega et omega ne sont pas un cas d'école,
// c'est le cas nominal. Les critères de mission sont donc formulés sur des
// quantités NON singulières (a, e, i, r_p, r_a), jamais sur omega ou Omega seuls.
#pragma once
#include "fen/core/Vec3.hpp"

namespace fen::astro {

struct Elements {
  double a{};      // demi-grand axe [m]  (négatif si hyperbolique)
  double e{};      // excentricité
  double i{};      // inclinaison [rad]
  double raan{};   // ascension droite du noeud ascendant [rad]
  double argp{};   // argument du périastre [rad]
  double nu{};     // anomalie vraie [rad]
  double p{};      // semi-latus rectum [m]  (seul valide si e == 1)
  double rp{};     // rayon périastre [m]
  double ra{};     // rayon apoastre [m]  (négatif/NaN si hyperbolique)
  bool circular{false};
  bool equatorial{false};
};

Elements rv_to_elements(const Vec3& r, const Vec3& v, double mu);
void elements_to_rv(const Elements& el, double mu, Vec3& r, Vec3& v);

// Période orbitale (elliptique uniquement). NaN sinon.
double orbital_period(double a, double mu);

// Anomalie vraie <-> excentrique <-> moyenne
double nu_to_M(double nu, double e);
double M_to_nu(double M, double e);

} // namespace fen::astro
