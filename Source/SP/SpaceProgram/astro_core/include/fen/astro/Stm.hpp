// fen/astro/Stm.hpp — MATRICE DE TRANSITION D'UN ARC KÉPLÉRIEN
//
// « De combien bouge l'arrivée si je pousse d'un mètre par seconde
// maintenant ? » — Φ_rv répond, et c'est la question de TOUTE correction de
// mi-parcours. Les quatre blocs (∂r/∂r₀, ∂r/∂v₀, ∂v/∂r₀, ∂v/∂v₀) sont obtenus
// par DIFFÉRENCES FINIES CENTRÉES sur le propagateur : le modèle linéarisé est
// donc exactement celui que le jeu propage, jamais une formule parallèle qui
// pourrait en diverger.
//
// ═══ POURQUOI DANS astro_core ET PLUS DANS mission/ ═══
// Ce code ne connaît que Kepler et deux corps : c'est de l'astrodynamique, pas
// de la logique de mission. Il vivait dans `fen/mission/Navigation.hpp`, ce qui
// le mettait hors de portée de `ares/vol.hpp` — l'API que le JOUEUR utilise pour
// écrire son logiciel de vol [GDD 15.3]. Le solveur offert au joueur devait donc
// se rabattre sur une correction proportionnelle Δv = −Δr/τ, valable en champ
// nul et FAUSSE dès qu'un arc courbe : sur une croisière de Mars elle commande
// dans une direction qui AGGRAVE le manque au but. Un exemple du GDD qui rate
// est pire qu'une absence d'exemple.
//
// Il est ici, et `fen::mission` le ré-exporte : un chiffre, une source.
#pragma once
#include <cmath>

#include "fen/astro/Kepler.hpp"
#include "fen/core/Vec3.hpp"

namespace fen::astro {

// Une 3x3 dense, juste ce qu'il faut : produit matrice-vecteur, produit
// matriciel, déterminant, norme de Frobenius. Importer une algèbre linéaire
// générale pour ça serait disproportionné — c'est l'argument déjà retenu par
// `fen/core/Matrix.hpp` pour son 6x6.
struct M3 {
  double m[3][3]{};
  Vec3 operator*(const Vec3& v) const {
    return {m[0][0]*v.x + m[0][1]*v.y + m[0][2]*v.z,
            m[1][0]*v.x + m[1][1]*v.y + m[1][2]*v.z,
            m[2][0]*v.x + m[2][1]*v.y + m[2][2]*v.z};
  }
  M3 operator*(const M3& b) const {
    M3 r;
    for (int i = 0; i < 3; ++i)
      for (int j = 0; j < 3; ++j) {
        double s = 0.0;
        for (int k = 0; k < 3; ++k) s += m[i][k] * b.m[k][j];
        r.m[i][j] = s;
      }
    return r;
  }
  double det() const {
    return m[0][0]*(m[1][1]*m[2][2] - m[1][2]*m[2][1])
         - m[0][1]*(m[1][0]*m[2][2] - m[1][2]*m[2][0])
         + m[0][2]*(m[1][0]*m[2][1] - m[1][1]*m[2][0]);
  }
  // Norme de Frobenius au carré : trace(A Aᵀ). C'est tout ce dont la
  // propagation d'une covariance ISOTROPE a besoin.
  double frob2() const {
    double s = 0.0;
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) s += m[i][j] * m[i][j];
    return s;
  }
};

inline bool inverse3(const M3& a, M3& out) {
  const double d = a.det();
  if (!(std::fabs(d) > 0.0) || !std::isfinite(d)) return false;
  const double id = 1.0 / d;
  out.m[0][0] =  (a.m[1][1]*a.m[2][2] - a.m[1][2]*a.m[2][1]) * id;
  out.m[0][1] = -(a.m[0][1]*a.m[2][2] - a.m[0][2]*a.m[2][1]) * id;
  out.m[0][2] =  (a.m[0][1]*a.m[1][2] - a.m[0][2]*a.m[1][1]) * id;
  out.m[1][0] = -(a.m[1][0]*a.m[2][2] - a.m[1][2]*a.m[2][0]) * id;
  out.m[1][1] =  (a.m[0][0]*a.m[2][2] - a.m[0][2]*a.m[2][0]) * id;
  out.m[1][2] = -(a.m[0][0]*a.m[1][2] - a.m[0][2]*a.m[1][0]) * id;
  out.m[2][0] =  (a.m[1][0]*a.m[2][1] - a.m[1][1]*a.m[2][0]) * id;
  out.m[2][1] = -(a.m[0][0]*a.m[2][1] - a.m[0][1]*a.m[2][0]) * id;
  out.m[2][2] =  (a.m[0][0]*a.m[1][1] - a.m[0][1]*a.m[1][0]) * id;
  return true;
}

// Blocs de la matrice de transition d'un arc képlérien, par différences finies
// CENTRÉES sur le propagateur. `h` en échelle relative, même doctrine que
// `nav::stm` : le fond de la courbe en V entre troncature et bruit d'arrondi.
struct StmBlocks { M3 rr, rv, vr, vv; bool ok{false}; };

inline StmBlocks kepler_stm(const Vec3& r0, const Vec3& v0, double dt, double mu) {
  StmBlocks S;
  const double hr = 5.0e-7 * norm(r0);
  const double hv = 5.0e-7 * norm(v0);
  if (!(hr > 0.0) || !(hv > 0.0)) return S;
  for (int j = 0; j < 3; ++j) {
    Vec3 dr{}, dv{};
    dr[j] = hr;
    const auto Rp = kepler_propagate(r0 + dr, v0, dt, mu);
    const auto Rm = kepler_propagate(r0 - dr, v0, dt, mu);
    dv[j] = hv;
    const auto Vp = kepler_propagate(r0, v0 + dv, dt, mu);
    const auto Vm = kepler_propagate(r0, v0 - dv, dt, mu);
    if (!Rp.converged || !Rm.converged || !Vp.converged || !Vm.converged) return S;
    for (int i = 0; i < 3; ++i) {
      S.rr.m[i][j] = (Rp.r[i] - Rm.r[i]) / (2.0 * hr);
      S.vr.m[i][j] = (Rp.v[i] - Rm.v[i]) / (2.0 * hr);
      S.rv.m[i][j] = (Vp.r[i] - Vm.r[i]) / (2.0 * hv);
      S.vv.m[i][j] = (Vp.v[i] - Vm.v[i]) / (2.0 * hv);
    }
  }
  S.ok = true;
  return S;
}

// ═══ LE Δv QUI ANNULE UN MANQUE AU BUT ═══
// Δv = −Φ_rv(arrivée ← maintenant)⁻¹ · Δr. C'est LA correction de mi-parcours au
// premier ordre, et la seule source de cette formule dans le moteur : le mode
// Normal l'appelle par son nœud de graphe, le mode Pro par `ares::vol::Solveur`,
// et le bureau d'études par la dispersion de navigation.
// Rend faux si l'arc est dégénéré (Φ_rv non inversible) — l'appelant décide
// alors quoi faire, plutôt que de recevoir un vecteur silencieusement faux.
inline bool dv_correction(const Vec3& r, const Vec3& v, const Vec3& manque,
                          double horizon_s, double mu, Vec3& dv_out) {
  const StmBlocks Phi = kepler_stm(r, v, horizon_s, mu);
  M3 inv;
  if (!Phi.ok || !inverse3(Phi.rv, inv)) return false;
  dv_out = inv * (Vec3{} - manque);
  return true;
}

} // namespace fen::astro
