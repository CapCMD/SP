// fen/astro/Mga1Dsm.hpp
//
// MGA-1DSM — UNE MANŒUVRE EN ESPACE PROFOND PAR JAMBE.
//
// POURQUOI CETTE BRIQUE EXISTE. Elle n'a pas été choisie : elle a été IMPOSÉE
// par un chiffre. Le tour V-V-E-J-S en MGA pur (Mga.hpp) a livré ceci :
//
//     survol      v_inf entrant -> sortant     desaccord      dv paye
//     Venus 2       5,98 -> 5,98 km/s            0,00           0
//     TERRE         6,96 -> 14,16 km/s         +7,21 km/s    4 935 m/s
//     Jupiter       7,83 -> 7,83 km/s            0,00           0
//
// 87 % du Delta-v embarqué venait d'UN SEUL survol mal raccordé. Deux arcs de
// Lambert consécutifs n'ont AUCUNE raison de se rejoindre avec le même |v_inf| :
// c'est la limite structurelle du MGA pur, pas un défaut d'optimiseur.
//
// CE QUE CHANGE LA DSM. On renverse le problème :
//   - le survol devient NON PROPULSÉ **par construction** : on ne CHOISIT plus
//     v_inf_sortant, on le CALCULE en tournant v_inf_entrant de l'angle que la
//     planète peut fournir, autour d'un axe paramétré par l'angle du plan-B.
//     |v_inf| est alors conservé exactement, à la précision machine.
//   - et c'est la MANŒUVRE EN ESPACE PROFOND, au milieu de la jambe, qui déforme
//     l'arc jusqu'à ce que l'arrivée soit compatible avec le survol suivant.
//
// Le Delta-v ne disparaît pas : il se DÉPLACE, du périastre (où l'on payait un
// désaccord brutal) vers l'espace profond (où l'on ne paie qu'une mise en forme).
// Cassini : 450 m/s à l'aphélie entre les deux survols de Vénus. Sans elle, la
// résonance Vénus-Vénus ne se referme pas.
//
// ÉLÉGANCE SUPPLÉMENTAIRE : plus aucun survol n'est "infaisable". La déviation
// est ce qu'elle est, on ne la demande plus. La contrainte r_p >= r_p_min est
// portée par les BORNES de l'optimiseur, pas par un rejet.
#pragma once
#include <cmath>
#include <vector>
#include "fen/astro/Flyby.hpp"
#include "fen/astro/Kepler.hpp"
#include "fen/astro/Lambert.hpp"
#include "fen/astro/Mga.hpp"
#include <algorithm>
#include <functional>
#include "fen/ephem/Ephemeris.hpp"

namespace fen::astro {

// Rotation de v_inf par un survol. |v_inf| est conservé A LA PRECISION MACHINE :
//   |v_out| = |v_in| * sqrt(cos^2 d + cos^2 b sin^2 d + sin^2 b sin^2 d) = |v_in|
// Ce n'est pas une approximation, c'est une identité trigonométrique.
inline Vec3 flyby_rotate(const Vec3& vinf_in, const Vec3& v_planet,
                         double rp, double mu, double beta) {
  const double vn = norm(vinf_in);
  if (vn <= 0.0) return vinf_in;
  const double delta = flyby_turn(vn, rp, mu);
  const Vec3 ih = vinf_in / vn;
  Vec3 jh = cross(ih, v_planet);
  const double jn = norm(jh);
  if (jn < 1e-12) return vinf_in;
  jh = jh / jn;
  const Vec3 kh = cross(ih, jh);
  return (ih * std::cos(delta)
        + jh * (std::cos(beta) * std::sin(delta))
        + kh * (std::sin(beta) * std::sin(delta))) * vn;
}

struct Mga1DsmProblem {
  std::vector<ephem::Body> seq;
  std::vector<double> rp_min, rp_max;   // m, un par survol

  // CONTRAINTE DE PUISSANCE PAR SURVOL — et ce n'est PAS une regle de jeu.
  //
  // Apres un survol, v_helio = v_planete + v_inf, donc  |v_helio| <= |v_p| + |v_inf|.
  // Pour ouvrir Jupiter depuis la Terre il faut |v_helio| = 38,57 km/s (Hohmann),
  // et v_Terre = 29,78. DONC :
  //
  //     |v_inf| au survol terrestre  >=  38,57 - 29,78  =  8,79 km/s
  //
  // Deux lignes. Un optimiseur qui l'ignore explore des tours PHYSIQUEMENT
  // INCAPABLES d'atteindre Jupiter, et paie l'impossibilite en DSM geante
  // (mesure : 7 890 m/s). Le joueur qui SAIT ca elague son espace de recherche
  // d'un facteur enorme.
  //
  // >>> LA CONNAISSANCE PHYSIQUE ACHETE DU TEMPS DE CALCUL. <<<
  // C'est le meme axiome que l'economie de fidelite de modele, et c'est la
  // raison d'etre de la couche de calcul du joueur.
  std::vector<double> vinf_min;         // m/s, un par survol (0 = pas de contrainte)
  double t0_lo{}, t0_hi{};
  double vinf_lo{3000.0}, vinf_hi{5000.0};
  std::vector<double> tof_lo, tof_hi;   // s, un par jambe
  double tof_total_max{};

  // LA DUREE : FALAISE OU PENALITE ? Ce n'est pas un detail d'implementation.
  //
  // Le C3 est deja traite en PENALITE (cost += 50 * depassement) : depasser ce
  // que le lanceur vend coute cher, mais le point EXISTE. La duree, elle, etait
  // traitee en FALAISE : cout = +inf, le point N'EXISTE PAS.
  //
  // Ces deux contraintes sont pourtant de MEME NATURE — une ressource bornee
  // (ce que le lanceur vend / ce que le RTG supporte). L'incoherence a un prix
  // MESURE : un raffineur a gradient peut GLISSER le long d'une penalite ; il ne
  // peut PAS glisser le long d'une falaise. L'optimum est colle au mur des 9 ans
  // dans 100 % des essais, et le raffineur y sort avec un residu KKT de 9e4 :
  // il ne descend pas, il RABOTE.
  //
  // 0 = falaise (comportement d'origine, bit-a-bit). > 0 = penalite en m/s par
  // seconde de depassement. La contrainte reste DURE : on verifie tof_total a la
  // fin. Mais le chemin pour y arriver devient derivable.
  double tof_penalty{0.0};

  double c3_max{20e6};
  double rp_insert{}, a_insert{};
};

struct Mga1DsmResult {
  std::vector<double> t;          // époques aux corps
  std::vector<double> dsm;        // Delta-v de chaque DSM (m/s)
  std::vector<double> t_dsm;
  std::vector<double> rp;         // périastres de survol (m)
  std::vector<double> vinf_fb;    // |v_inf| à chaque survol (conservé)
  std::vector<double> turn;       // déviation obtenue (rad)
  std::vector<double> gain;       // Delta-v héliocentrique GRATUIT (ici, il l'est vraiment)
  double c3{}, vinf_arr{}, dv_insert{}, dv_dsm_total{}, dv_onboard{}, tof_total{};
  double vinf_shortfall{};   // m/s manquants pour que le tour soit PHYSIQUEMENT possible
  double tof_over{};         // s au-dela de ce que le RTG supporte (0 = admissible)
  bool feasible{false};
  double cost{1e300};
};

inline int d1_flybys(const Mga1DsmProblem& p) { return static_cast<int>(p.seq.size()) - 2; }
inline int d1_legs(const Mga1DsmProblem& p)   { return static_cast<int>(p.seq.size()) - 1; }
inline int d1_nvars(const Mga1DsmProblem& p)  { return 6 + 4 * d1_flybys(p); }

// x = [t0, vinf, u, v, eta_0, T_0,  (beta_k, rp_k, eta_k, T_k) pour k = 1..F]
inline Mga1DsmResult mga1dsm_evaluate(const Mga1DsmProblem& p, const ephem::IEphemeris& eph,
                                      const std::vector<double>& x) {
  Mga1DsmResult r;
  const int F = d1_flybys(p), L = d1_legs(p);

  // --- durée totale : contrainte dure, mais franchissable EN PAYANT ----------
  double Ttot = x[5];
  for (int k = 1; k <= F; ++k) Ttot += x[6 + 4 * (k - 1) + 3];
  if (p.tof_penalty <= 0.0 && Ttot > p.tof_total_max) return r;   // falaise (d'origine)
  r.tof_total = Ttot;
  r.tof_over = std::fmax(0.0, Ttot - p.tof_total_max);

  // --- injection : v_inf sur la sphère (Vinf, theta, phi) --------------------
  const double Vinf = x[1];
  const double theta = cst::TWO_PI * x[2];
  const double phi = std::acos(2.0 * x[3] - 1.0) - 0.5 * cst::PI;
  const Vec3 vinf0{Vinf * std::cos(phi) * std::cos(theta),
                   Vinf * std::cos(phi) * std::sin(theta),
                   Vinf * std::sin(phi)};
  r.c3 = Vinf * Vinf;

  double t = x[0];
  const auto P0 = eph.state(p.seq[0], ephem::Body::Sun, Epoch{t});
  Vec3 r_sc = P0.r, v_sc = P0.v + vinf0;
  r.t.push_back(t);

  for (int i = 0; i < L; ++i) {
    const double T   = (i == 0) ? x[5] : x[6 + 4 * (i - 1) + 3];
    const double eta = (i == 0) ? x[4] : x[6 + 4 * (i - 1) + 2];
    if (T <= 0.0 || eta <= 0.0 || eta >= 1.0) return r;

    // --- 1) dérive képlérienne jusqu'à la DSM ---
    // Elle peut faire PLUSIEURS TOURS : c'est elle qui absorbe la géométrie.
    // C'est pourquoi l'arc de Lambert qui suit n'a besoin que de 0 révolution.
    auto k = kepler_propagate(r_sc, v_sc, eta * T, cst::MU_SUN);
    if (!k.converged) return r;

    // --- 2) arc de Lambert de la DSM jusqu'au corps suivant ---
    const double t_next = t + T;
    const auto Pn = eph.state(p.seq[i + 1], ephem::Body::Sun, Epoch{t_next});
    auto Lm = lambert(k.r, Pn.r, (1.0 - eta) * T, cst::MU_SUN, true, 0);
    if (!Lm.ok) return r;

    const double dv = norm(Lm.solutions[0].v1 - k.v);
    r.dsm.push_back(dv);
    r.t_dsm.push_back(t + eta * T);
    r.dv_dsm_total += dv;

    const Vec3 v_arr = Lm.solutions[0].v2;
    t = t_next;
    r.t.push_back(t);

    if (i < L - 1) {
      // --- 3) SURVOL. Non propulsé PAR CONSTRUCTION. ---
      const double beta = x[6 + 4 * i + 0];
      const double rp   = x[6 + 4 * i + 1];
      const double mu_b = ephem::body_mu(p.seq[i + 1]);
      const Vec3 vin  = v_arr - Pn.v;
      const Vec3 vout = flyby_rotate(vin, Pn.v, rp, mu_b, beta);
      r.rp.push_back(rp);
      const double vn_fb = norm(vin);
      r.vinf_fb.push_back(vn_fb);
      if (!p.vinf_min.empty() && p.vinf_min[i] > 0.0 && vn_fb < p.vinf_min[i])
        r.vinf_shortfall += (p.vinf_min[i] - vn_fb);
      r.turn.push_back(flyby_turn(norm(vin), rp, mu_b));
      r.gain.push_back(norm(vout - vin));     // ici il est VRAIMENT gratuit
      r_sc = Pn.r;
      v_sc = Pn.v + vout;
    } else {
      r.vinf_arr = norm(v_arr - Pn.v);
    }
  }

  const double mu_t = ephem::body_mu(p.seq.back());
  r.dv_insert = std::sqrt(r.vinf_arr * r.vinf_arr + 2.0 * mu_t / p.rp_insert)
              - std::sqrt(mu_t * (2.0 / p.rp_insert - 1.0 / p.a_insert));
  r.dv_onboard = r.dv_dsm_total + r.dv_insert;
  r.feasible = true;

  const double over = std::sqrt(r.c3) - std::sqrt(p.c3_max);
  r.cost = r.dv_onboard + (over > 0 ? 50.0 * over : 0.0)
         + 5.0 * r.vinf_shortfall            // elagage : ces tours ne peuvent PAS marcher
         + p.tof_penalty * r.tof_over;       // le RTG : meme traitement que le lanceur
  return r;
}

// --- MONOTONIC BASIN HOPPING -------------------------------------------------
// DE trouve un bassin. MBH saute de bassin en bassin, en n'acceptant QUE les
// ameliorations (monotone). C'est l'outil standard des problemes MGA (ESA/GTOP).
// Ici, l'affinage local est une DE courte dans une boite RESSERREE autour du
// point courant : robuste aux discontinuites (echecs de Lambert), sans gradient.
struct MbhResult { std::vector<double> x; double f{1e300}; long long evals{}; int hops{}; };

inline MbhResult basin_hopping(
    const std::function<double(const std::vector<double>&)>& f,
    const std::vector<double>& lo, const std::vector<double>& hi,
    const std::vector<double>& x0, int hops, double radius,
    std::uint64_t seed,
    const std::function<DeResult(const std::vector<double>&, const std::vector<double>&,
                                 const std::vector<double>&, std::uint64_t)>& refine) {
  const int D = static_cast<int>(lo.size());
  Rng rng(seed);
  MbhResult best;
  best.x = x0;
  best.f = f(x0);

  for (int h = 0; h < hops; ++h) {
    std::vector<double> xp(D), l2(D), h2(D);
    for (int j = 0; j < D; ++j) {
      const double w = radius * (hi[j] - lo[j]);
      xp[j] = std::clamp(best.x[j] + w * (2.0 * rng.uniform01() - 1.0), lo[j], hi[j]);
      l2[j] = std::clamp(xp[j] - 0.5 * w, lo[j], hi[j]);
      h2[j] = std::clamp(xp[j] + 0.5 * w, lo[j], hi[j]);
      if (h2[j] - l2[j] < 1e-12) h2[j] = l2[j] + 1e-9;
    }
    auto r = refine(l2, h2, xp, rng.next_u64());
    best.evals += r.evals;
    ++best.hops;
    if (r.f < best.f) { best.f = r.f; best.x = r.x; }   // MONOTONE : que des gains
  }
  return best;
}

} // namespace fen::astro
