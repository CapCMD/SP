// fen/astro/Mga.hpp
//
// TOUR À ASSISTANCES MULTIPLES (Multiple Gravity Assist).
//
// Une seule assistance jovienne ne casse pas le mur du C3 : atteindre Jupiter
// coûte déjà 77 km²/s². Pour partir avec une charge utile, il faut GAGNER de
// l'énergie AVANT — sur Vénus et sur la Terre. C'est la découverte de Minovitch
// (1961), et c'est ce qui a rendu possible Voyager, Galileo et Cassini.
//
// Le problème n'est plus une équation : c'est une OPTIMISATION GLOBALE.
//   inconnues : la date de lancement, la durée de chaque jambe, et le nombre de
//               révolutions de chaque arc de Lambert.
//   contraintes : à chaque survol, |v_inf| conservé et déviation réalisable ;
//               C3 sous ce que le lanceur vend ; durée totale sous ce que le
//               RTG supporte.
//   objectif : minimiser le Delta-v EMBARQUÉ (survols propulsés + insertion).
//
// Le paysage est MULTIMODAL : des milliers de minima locaux. C'est pour ça que
// le jeu facture le TEMPS DE CALCUL — chercher un bon tour COÛTE, et le joueur
// doit décider combien il y met. C'est le même axiome que l'économie de fidélité
// de modèle, appliqué au calendrier.
#pragma once
#include <algorithm>
#include <functional>
#include <limits>
#include <vector>
#include "fen/astro/Flyby.hpp"
#include "fen/astro/Lambert.hpp"
#include "fen/astro/Transfers.hpp"
#include "fen/core/Rng.hpp"
#include "fen/ephem/Ephemeris.hpp"

namespace fen::astro {

struct MgaProblem {
  std::vector<ephem::Body> seq;      // [depart, survol..., arrivee]
  std::vector<double> rp_min;        // m, un par survol intermediaire
  double t0_lo{}, t0_hi{};           // s TDB
  std::vector<double> tof_lo, tof_hi;// s, un par jambe
  int max_revs{2};
  double c3_max{20e6};               // m^2/s^2 : ce que le lanceur VEND
  double tof_total_max{};            // s : ce que le RTG supporte
  double rp_insert{}, a_insert{};    // orbite de capture visee au corps final
};

struct MgaResult {
  std::vector<double> t;             // epoques a chaque corps
  std::vector<double> rp;            // periastres de survol (m)
  std::vector<double> dv_fb;         // Delta-v propulsif par survol (m/s)
  std::vector<double> gain;          // ce que la PLANETE donne, net du moteur
  std::vector<double> vinf_in, vinf_out;  // le DESACCORD est le diagnostic
  std::vector<int> revs;
  double c3{}, vinf_arr{}, dv_insert{}, dv_onboard{}, tof_total{};
  bool feasible{false};
  double cost{std::numeric_limits<double>::max()};
};

inline int n_legs(const MgaProblem& p) { return static_cast<int>(p.seq.size()) - 1; }
inline int n_flybys(const MgaProblem& p) { return static_cast<int>(p.seq.size()) - 2; }
inline int n_vars(const MgaProblem& p) { return 1 + 2 * n_legs(p); }  // t0 + tof[] + rev[]

// x = [t0, tof_1..tof_L, rev_1..rev_L]   (rev continu, tronque)
inline MgaResult mga_evaluate(const MgaProblem& p, const ephem::IEphemeris& eph,
                              const std::vector<double>& x) {
  MgaResult r;
  const int L = n_legs(p), F = n_flybys(p);

  double t = x[0];
  r.t.push_back(t);
  for (int i = 0; i < L; ++i) {
    t += x[1 + i];
    r.t.push_back(t);
  }
  r.tof_total = r.t.back() - r.t.front();
  if (r.tof_total > p.tof_total_max) return r;

  // --- resolution des L arcs de Lambert -------------------------------------
  std::vector<Vec3> v_dep(L), v_arr(L);
  for (int i = 0; i < L; ++i) {
    const auto A = eph.state(p.seq[i], ephem::Body::Sun, Epoch{r.t[i]});
    const auto B = eph.state(p.seq[i + 1], ephem::Body::Sun, Epoch{r.t[i + 1]});
    const int rev = std::min(p.max_revs,
                             std::max(0, static_cast<int>(x[1 + L + i])));
    auto Lm = lambert(A.r, B.r, x[1 + i], cst::MU_SUN, true, rev);
    if (!Lm.ok) return r;
    // On retient la solution du nombre de revolutions demande, si elle existe.
    const LambertSolution* sol = &Lm.solutions[0];
    for (const auto& s : Lm.solutions)
      if (s.revolutions == rev) { sol = &s; break; }
    v_dep[i] = sol->v1;
    v_arr[i] = sol->v2;
    r.revs.push_back(sol->revolutions);
  }

  // --- lancement -------------------------------------------------------------
  const auto E0 = eph.state(p.seq[0], ephem::Body::Sun, Epoch{r.t[0]});
  r.c3 = norm2(v_dep[0] - E0.v);

  // --- survols ---------------------------------------------------------------
  for (int k = 0; k < F; ++k) {
    const auto Bk = eph.state(p.seq[k + 1], ephem::Body::Sun, Epoch{r.t[k + 1]});
    const Vec3 vin  = v_arr[k]     - Bk.v;
    const Vec3 vout = v_dep[k + 1] - Bk.v;
    auto fb = solve_flyby(vin, vout, ephem::body_mu(p.seq[k + 1]), p.rp_min[k]);
    if (!fb.feasible) return r;      // la planete ne peut pas tourner autant. BORNE PHYSIQUE.
    r.rp.push_back(fb.rp);
    r.dv_fb.push_back(fb.dv);
    r.gain.push_back(fb.gravity_gain);
    r.vinf_in.push_back(fb.vinf_in);
    r.vinf_out.push_back(fb.vinf_out);
  }

  // --- insertion -------------------------------------------------------------
  const auto S = eph.state(p.seq.back(), ephem::Body::Sun, Epoch{r.t.back()});
  const double mu_t = ephem::body_mu(p.seq.back());
  r.vinf_arr = norm(v_arr[L - 1] - S.v);
  r.dv_insert = std::sqrt(r.vinf_arr * r.vinf_arr + 2.0 * mu_t / p.rp_insert)
              - std::sqrt(mu_t * (2.0 / p.rp_insert - 1.0 / p.a_insert));

  r.dv_onboard = r.dv_insert;
  for (double d : r.dv_fb) r.dv_onboard += d;
  r.feasible = true;

  // Le C3 n'est pas du Delta-v embarque : c'est ce que le LANCEUR vend. On le
  // traite donc comme une CONTRAINTE, pas comme un terme du cout. Le depasser
  // est une penalite raide — parce qu'en vrai, il n'existe simplement pas de
  // lanceur pour ca.
  const double over = std::sqrt(std::fmax(0.0, r.c3)) - std::sqrt(p.c3_max);
  r.cost = r.dv_onboard + (over > 0 ? 50.0 * over : 0.0);
  return r;
}

// --- ÉVOLUTION DIFFÉRENTIELLE (Storn & Price, DE/rand/1/bin) ------------------
// Un paysage à des milliers de minima locaux ne se descend pas au gradient.
// DE est le standard de fait pour les problemes MGA. Deterministe ici : meme
// graine, meme tour trouve.
struct DeResult { std::vector<double> x; double f{}; long long evals{}; };

inline DeResult differential_evolution(
    const std::function<double(const std::vector<double>&)>& f,
    const std::vector<double>& lo, const std::vector<double>& hi,
    int pop, int gens, std::uint64_t seed, double F = 0.7, double CR = 0.9,
    const std::vector<double>* x_seed = nullptr) {
  const int D = static_cast<int>(lo.size());
  Rng rng(seed);
  std::vector<std::vector<double>> X(pop, std::vector<double>(D));
  std::vector<double> fx(pop);
  long long evals = 0;

  for (int i = 0; i < pop; ++i) {
    for (int j = 0; j < D; ++j) X[i][j] = rng.uniform(lo[j], hi[j]);
    // AMORCAGE : un tiers de la population part du voisinage de la solution
    // grossiere. C'est l'economie de fidelite de modele appliquee a la RECHERCHE
    // elle-meme : le modele pas cher fournit le bassin, le modele cher l'affine.
    if (x_seed && i < pop / 3 && static_cast<int>(x_seed->size()) == D) {
      for (int j = 0; j < D; ++j) {
        const double w = (i == 0) ? 0.0 : 0.15 * (hi[j] - lo[j]) * (2.0 * rng.uniform01() - 1.0);
        X[i][j] = std::clamp((*x_seed)[j] + w, lo[j], hi[j]);
      }
    }
    fx[i] = f(X[i]);
    ++evals;
  }
  int best = 0;
  for (int i = 1; i < pop; ++i) if (fx[i] < fx[best]) best = i;

  std::vector<double> trial(D);
  for (int g = 0; g < gens; ++g) {
    for (int i = 0; i < pop; ++i) {
      int a, b, c;
      do { a = static_cast<int>(rng.uniform01() * pop); } while (a == i);
      do { b = static_cast<int>(rng.uniform01() * pop); } while (b == i || b == a);
      do { c = static_cast<int>(rng.uniform01() * pop); } while (c == i || c == a || c == b);
      const int jr = static_cast<int>(rng.uniform01() * D);
      for (int j = 0; j < D; ++j) {
        if (rng.uniform01() < CR || j == jr) {
          double v = X[a][j] + F * (X[b][j] - X[c][j]);
          if (v < lo[j] || v > hi[j]) v = rng.uniform(lo[j], hi[j]);  // re-tirage aux bornes
          trial[j] = v;
        } else {
          trial[j] = X[i][j];
        }
      }
      const double ft = f(trial);
      ++evals;
      if (ft < fx[i]) {
        X[i] = trial;
        fx[i] = ft;
        if (ft < fx[best]) best = i;
      }
    }
  }
  return DeResult{X[best], fx[best], evals};
}

} // namespace fen::astro
