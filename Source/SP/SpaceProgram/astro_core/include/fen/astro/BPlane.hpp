// fen/astro/BPlane.hpp
// Coordonnées du PLAN-B (B-plane). C'est le langage de la navigation
// interplanétaire réelle : on ne cible pas "une orbite", on cible un point
// (B.R, B.T) dans un plan perpendiculaire à l'asymptote d'arrivée.
//
// Pourquoi ça compte pour le jeu : la dispersion d'exécution (Gates) se traduit
// en une ELLIPSE dans le plan-B. L'ellipse de dispersion, superposée au corridor
// admissible, EST l'interface graphique de la sanction. Le joueur voit, en un
// coup d'oeil, s'il a assez de marge — sans qu'aucun chiffre "de jeu" ne soit
// inventé.
#pragma once
#include <cmath>
#include "fen/core/Vec3.hpp"

namespace fen::astro {

struct BPlane {
  Vec3 S;        // unitaire : asymptote INCIDENTE (direction de v_inf entrante)
  Vec3 T;        // unitaire : S x k / |S x k|
  Vec3 R;        // unitaire : S x T
  Vec3 B;        // vecteur B (m), du centre du corps vers le point de percée
  double BdotR{};
  double BdotT{};
  double b{};        // |B| = paramètre d'impact (m)
  double vinf{};     // m/s
  double rp{};       // rayon périastre atteint (m)
  bool hyperbolic{false};
};

// r,v : état RELATIF AU CORPS CIBLE, dans le repère inertiel du corps.
// k_ref : normale du plan de référence (pôle écliptique {0,0,1} par défaut).
inline BPlane b_plane(const Vec3& r, const Vec3& v, double mu,
                      const Vec3& k_ref = Vec3{0, 0, 1}) {
  BPlane bp;
  const double rn = norm(r);
  const double v2 = norm2(v);
  const Vec3 h = cross(r, v);
  const Vec3 evec = (r * (v2 - mu / rn) - v * dot(r, v)) / mu;
  const double e = norm(evec);
  if (e <= 1.0) { bp.hyperbolic = false; return bp; }  // capturé : pas de plan-B

  bp.hyperbolic = true;
  const double energy = 0.5 * v2 - mu / rn;
  const double a = -mu / (2.0 * energy);        // < 0
  bp.vinf = std::sqrt(2.0 * energy);
  bp.rp   = a * (1.0 - e);                      // = -|a|(e-1) > 0
  bp.b    = std::fabs(a) * std::sqrt(e * e - 1.0);

  const Vec3 ih = unit(h);
  const Vec3 ie = unit(evec);
  const Vec3 iw = cross(ih, ie);                // dans le plan, perp. à e, sens du mouvement

  // Asymptote incidente : v_hat(-inf) = (1/e) e_hat + sqrt(1 - 1/e^2) w_hat
  const double inv_e = 1.0 / e;
  bp.S = unit(ie * inv_e + iw * std::sqrt(1.0 - inv_e * inv_e));

  // B = b * (S x h_hat) : perpendiculaire à S, dans le plan orbital, du côté périastre.
  bp.B = cross(bp.S, ih) * bp.b;

  bp.T = unit(cross(bp.S, k_ref));
  bp.R = cross(bp.S, bp.T);
  bp.BdotT = dot(bp.B, bp.T);
  bp.BdotR = dot(bp.B, bp.R);
  return bp;
}

// Conversion paramètre d'impact <-> rayon périastre pour un v_inf donné.
// Relation exacte (conservation de h et de l'énergie) :  b^2 = rp^2 + 2 mu rp / vinf^2
inline double b_from_rp(double rp, double vinf, double mu) {
  return rp * std::sqrt(1.0 + 2.0 * mu / (rp * vinf * vinf));
}
inline double rp_from_b(double b, double vinf, double mu) {
  const double k = mu / (vinf * vinf);
  return std::sqrt(k * k + b * b) - k;
}

} // namespace fen::astro
