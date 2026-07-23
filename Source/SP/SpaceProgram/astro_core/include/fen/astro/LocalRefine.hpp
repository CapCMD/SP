// fen/astro/LocalRefine.hpp
//
// RAFFINEUR LOCAL À GRADIENT — descente projetée quasi-Newton dans une boîte.
//
// POURQUOI CETTE BRIQUE EXISTE. Le MBH saute de bassin en bassin ; il ne descend
// pas au fond. Sur le tour V-V-E-J-S, son affinage interne était une DE dans une
// boîte resserrée : robuste, mais AVEUGLE — 13 200 évaluations pour faire ce
// qu'un gradient fait en 50.
//
// TROIS DIFFICULTÉS, ET LEUR RÉPONSE.
//
// 1. LES ÉCHELLES. t0 ~ 1e9 s, eta ~ 0,4, rp ~ 1e7 m, beta ~ 1 rad, T ~ 1e7 s.
//    Un pas de différences finies UNIQUE n'a aucun sens dans ces unités, et la
//    métrique implicite de BFGS (l'identité) y est absurde.
//    => on travaille dans le CUBE UNITÉ : z = (x - lo)/(hi - lo). Toutes les
//    variables deviennent O(1), un seul h les gouverne, et l'identité redevient
//    une métrique honnête. Sans cette normalisation, la méthode ne converge pas :
//    ce n'est pas un détail de confort, c'est la condition d'existence.
//
// 2. LES DISCONTINUITÉS. L'objectif MGA-1DSM n'est PAS continu : échec de
//    Lambert, durée totale dépassée => coût = +inf. Une différence centrée naïve
//    y calcule (1e300 - f)/(2h) et fabrique une direction de descente absurde.
//    => LA DIFFÉRENCE FINIE DÉTECTE LA FALAISE ET RECULE :
//         - un côté dans la falaise  -> différence UNILATÉRALE du côté faisable ;
//         - les deux côtés dedans    -> on RÉTRÉCIT h et on recommence ;
//         - rien de faisable même à h minuscule -> la composante est GELÉE.
//    Le nombre de fois où ça arrive est COMPTÉ et RENDU (`cliff_hits`). C'est un
//    diagnostic sur le problème, pas un détail d'implémentation : une falaise
//    fréquente signifie qu'une CONTRAINTE DURE mord, et il faut le savoir.
//
// 3. LA BOÎTE. Le pas est projeté : z(a) = clamp(z + a·d, 0, 1). Le test
//    d'Armijo porte sur le déplacement RÉELLEMENT EFFECTUÉ (z(a) - z), et non
//    sur a·d : sinon il accepterait un pas que la projection a tronqué à rien.
//
// GARANTIE — ET CE QU'ELLE NE DOIT PAS À BFGS.
// La décroissance est monotone PAR CONSTRUCTION : un pas n'est accepté que s'il
// fait STRICTEMENT baisser f et satisfait Armijo. BFGS n'est qu'une ACCÉLÉRATION.
// Si sa direction n'est pas de descente, on retombe sur la plus forte pente
// projetée, qui l'est toujours. Aucune propriété du résultat ne repose sur la
// justesse de la Hessienne approchée — seulement sa vitesse.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>
#include "fen/core/Rng.hpp"

namespace fen::astro {

struct RefineOptions {
  int    max_iter{400};
  double fd_step{1e-6};      // pas de DF, en coordonnées NORMALISÉES (fraction de la boîte)
  double fd_shrink{0.1};     // repli quand les DEUX côtés tombent dans la falaise
  int    fd_max_shrink{4};   // -> h descend jusqu'à 1e-10 avant de geler la composante
  double grad_tol{1e-9};     // ||gradient projeté||_inf  (résidu KKT, unités normalisées)
  double step_tol{1e-14};
  double armijo_c1{1e-4};
  double backtrack{0.5};
  int    max_backtracks{60};
  double cliff{1e298};       // f >= cliff  <=>  point INFAISABLE
  long long max_evals{500000};
};

struct RefineResult {
  std::vector<double> x;
  double f0{1e300};          // coût au départ
  double f{1e300};           // coût à l'arrivée
  int    iters{0};
  long long evals{0};
  double gnorm{0.0};         // ||gradient projeté||_inf à la sortie = RÉSIDU KKT
  int    cliff_hits{0};      // combien de fois la DF a heurté la discontinuité
  int    hess_resets{0};
  bool   ok{false};          // le point de départ était-il faisable ?
  const char* stop{"?"};
};

// --- GRADIENT PAR DIFFÉRENCES FINIES, CONSCIENT DE LA FALAISE -----------------
// Rend le nombre de composantes où la discontinuité a été détectée.
inline int fd_gradient(const std::function<double(const std::vector<double>&)>& F,
                       const std::vector<double>& z, double f0,
                       const RefineOptions& o,
                       std::vector<double>& g, long long& evals,
                       std::vector<double>& zt,
                       std::vector<char>* blk = nullptr) {
  const int D = static_cast<int>(z.size());
  int cliffs = 0;
  for (int j = 0; j < D; ++j) {
    double h = o.fd_step;
    g[j] = 0.0;
    if (blk) (*blk)[j] = 0;
    bool done = false;
    for (int s = 0; s <= o.fd_max_shrink && !done; ++s) {
      const double zp = std::min(1.0, z[j] + h);
      const double zm = std::max(0.0, z[j] - h);
      if (zp <= zm) break;                       // variable dégénérée (lo == hi)

      zt = z; zt[j] = zp; const double fp = F(zt); ++evals;
      zt = z; zt[j] = zm; const double fm = F(zt); ++evals;

      const bool uf = (zp > z[j]) && (fp < o.cliff);   // avant utilisable ?
      const bool ub = (zm < z[j]) && (fm < o.cliff);   // arrière utilisable ?

      if (uf && ub) {                       // centrée (ou chordale si la boîte a tronqué)
        g[j] = (fp - fm) / (zp - zm); done = true;
      } else if (uf) {                      // falaise DERRIÈRE -> on recule vers l'avant
        g[j] = (fp - f0) / (zp - z[j]); ++cliffs; done = true;
        if (blk && fm >= o.cliff) (*blk)[j] = -1;      // interdit de DESCENDRE z_j
      } else if (ub) {                      // falaise DEVANT -> on recule vers l'arrière
        g[j] = (f0 - fm) / (z[j] - zm); ++cliffs; done = true;
        if (blk && fp >= o.cliff) (*blk)[j] = +1;      // interdit de MONTER z_j
      } else {
        h *= o.fd_shrink;                   // les DEUX côtés : sliver faisable, on rétrécit
        if (s == o.fd_max_shrink) {                    // composante GELÉE dans les DEUX sens
          g[j] = 0.0; ++cliffs;
          if (blk) (*blk)[j] = 2;
        }
      }
    }
  }
  return cliffs;
}

inline RefineResult refine_local(
    const std::function<double(const std::vector<double>&)>& f,
    const std::vector<double>& lo, const std::vector<double>& hi,
    const std::vector<double>& x0, const RefineOptions& o = RefineOptions{}) {
  const int D = static_cast<int>(lo.size());
  RefineResult R;
  R.x = x0;

  // --- normalisation : la boîte devient le cube unité -------------------------
  std::vector<double> range(D), z(D), zt(D), zn(D), xbuf(D);
  for (int j = 0; j < D; ++j) {
    range[j] = hi[j] - lo[j];
    z[j] = (range[j] > 0.0) ? std::clamp((x0[j] - lo[j]) / range[j], 0.0, 1.0) : 0.0;
  }
  auto F = [&](const std::vector<double>& zz) {
    for (int j = 0; j < D; ++j) xbuf[j] = (range[j] > 0.0) ? lo[j] + zz[j] * range[j] : lo[j];
    return f(xbuf);
  };
  auto emit = [&](const std::vector<double>& zz) {
    for (int j = 0; j < D; ++j) R.x[j] = (range[j] > 0.0) ? lo[j] + zz[j] * range[j] : lo[j];
  };

  R.f0 = R.f = F(z);
  R.evals = 1;
  emit(z);
  if (!(R.f0 < o.cliff)) { R.stop = "depart infaisable"; return R; }
  R.ok = true;

  std::vector<double> g(D, 0.0), gprev(D, 0.0), d(D, 0.0), s(D, 0.0), y(D, 0.0), Hy(D, 0.0);
  std::vector<double> H(static_cast<std::size_t>(D) * D, 0.0);
  auto reset_H = [&]() {
    std::fill(H.begin(), H.end(), 0.0);
    for (int j = 0; j < D; ++j) H[static_cast<std::size_t>(j) * D + j] = 1.0;
  };
  reset_H();
  std::vector<char> act(D, 0), act_prev(D, 0), blk(D, 0);
  bool have_s = false;
  R.stop = "budget d'iterations epuise";

  for (R.iters = 0; R.iters < o.max_iter; ++R.iters) {
    if (R.evals >= o.max_evals) { R.stop = "budget d'evaluations epuise"; break; }
    R.cliff_hits += fd_gradient(F, z, R.f, o, g, R.evals, zt, &blk);

    // --- ENSEMBLE ACTIF + GRADIENT PROJETÉ ------------------------------------
    // Une variable collée à une borne dont le gradient pousse VERS L'EXTÉRIEUR
    // est bloquée : elle ne compte ni dans la direction, ni dans le test d'arrêt.
    //
    // ET LA MÊME CHOSE POUR LA FALAISE — c'est la leçon que l'oracle m'a apprise.
    // Une falaise n'est PAS une borne de la boîte : la projection ne la voit pas.
    // Résultat, sans ce qui suit : la composante qui bute contre le mur fait
    // s'effondrer le pas alpha, et elle entraîne dans sa chute TOUTES les autres
    // composantes de la direction — y compris celles qui étaient LIBRES et qui
    // avaient encore du chemin à faire. Mesuré : f = 0,1125 au lieu de 0,0900.
    // => une composante murée DANS LE SENS DE LA DESCENTE est active, exactement
    //    comme une borne. Le sous-espace libre survit, et il converge.
    double gn = 0.0;
    for (int j = 0; j < D; ++j) {
      const bool box  = (z[j] <= 0.0 && g[j] > 0.0) || (z[j] >= 1.0 && g[j] < 0.0);
      const bool wall = (g[j] < 0.0 && (blk[j] == +1 || blk[j] == 2))   // descente veut monter
                     || (g[j] > 0.0 && (blk[j] == -1 || blk[j] == 2));  // descente veut descendre
      act[j] = (box || wall) ? 1 : 0;
      if (!act[j]) gn = std::max(gn, std::fabs(g[j]));
    }
    R.gnorm = gn;
    if (gn <= o.grad_tol) { R.stop = "point stationnaire (KKT)"; break; }

    // --- BFGS : mise à jour de l'inverse de la Hessienne -----------------------
    bool changed = false;
    for (int j = 0; j < D; ++j) if (act[j] != act_prev[j]) { changed = true; break; }
    if (changed) {
      // L'ensemble actif a bougé : la Hessienne accumulée décrit un autre
      // sous-espace libre. On la jette. C'est ce qui rend la méthode SÛRE.
      if (R.iters > 0) { reset_H(); ++R.hess_resets; }
      have_s = false;
    } else if (have_s) {
      double sy = 0.0, ss = 0.0, yy = 0.0;
      for (int j = 0; j < D; ++j) {
        y[j] = g[j] - gprev[j];
        sy += s[j] * y[j];
        ss += s[j] * s[j];
        yy += y[j] * y[j];
      }
      // condition de courbure : sans elle, BFGS peut produire une H non définie
      // positive, donc une direction de MONTÉE. On saute la mise à jour.
      if (sy > 1e-12 * std::sqrt(ss * yy) && sy > 0.0) {
        const double rho = 1.0 / sy;
        double yHy = 0.0;
        for (int i = 0; i < D; ++i) {
          double acc = 0.0;
          for (int j = 0; j < D; ++j) acc += H[static_cast<std::size_t>(i) * D + j] * y[j];
          Hy[i] = acc;
        }
        for (int j = 0; j < D; ++j) yHy += y[j] * Hy[j];
        const double c = rho * (1.0 + rho * yHy);
        for (int i = 0; i < D; ++i)
          for (int j = 0; j < D; ++j)
            H[static_cast<std::size_t>(i) * D + j] +=
                c * s[i] * s[j] - rho * (s[i] * Hy[j] + Hy[i] * s[j]);
      }
    }
    act_prev = act;

    // --- DIRECTION : d = -H·g, restreinte au sous-espace LIBRE -----------------
    for (int i = 0; i < D; ++i) {
      if (act[i]) { d[i] = 0.0; continue; }
      double acc = 0.0;
      for (int j = 0; j < D; ++j) if (!act[j]) acc += H[static_cast<std::size_t>(i) * D + j] * g[j];
      d[i] = -acc;
    }
    // BFGS peut faire TOURNER la direction : une composante libre peut se
    // retrouver poussée dans un mur que son propre gradient ne demandait pas.
    // On la rabote. (Le repli plus bas, lui, est mural par construction.)
    for (int j = 0; j < D; ++j) {
      if (d[j] > 0.0 && (blk[j] == +1 || blk[j] == 2)) d[j] = 0.0;
      if (d[j] < 0.0 && (blk[j] == -1 || blk[j] == 2)) d[j] = 0.0;
    }
    double gd = 0.0;
    for (int j = 0; j < D; ++j) gd += g[j] * d[j];
    if (!(gd < 0.0)) {                       // BFGS a menti : repli sur la plus forte pente
      reset_H(); ++R.hess_resets; have_s = false;
      gd = 0.0;
      for (int j = 0; j < D; ++j) { d[j] = act[j] ? 0.0 : -g[j]; gd += g[j] * d[j]; }
      if (!(gd < 0.0)) { R.stop = "aucune direction de descente"; break; }
    }

    // --- RECHERCHE LINÉAIRE D'ARMIJO SUR L'ARC PROJETÉ -------------------------
    // Une falaise est un échec d'Armijo comme un autre : on recule. C'est là que
    // la contrainte dure (durée totale, échec de Lambert) est réellement traitée.
    double alpha = 1.0, fnew = R.f;
    bool stepped = false;
    for (int b = 0; b < o.max_backtracks; ++b) {
      double dot = 0.0, mx = 0.0;
      for (int j = 0; j < D; ++j) {
        zn[j] = std::clamp(z[j] + alpha * d[j], 0.0, 1.0);
        const double dz = zn[j] - z[j];
        dot += g[j] * dz;                    // pente le long du DÉPLACEMENT RÉEL
        mx = std::max(mx, std::fabs(dz));
      }
      if (mx <= o.step_tol) break;           // le pas est indistinguable de zéro
      if (dot < 0.0) {
        fnew = F(zn); ++R.evals;
        // décroissance STRICTE **et** Armijo : l'invariant de monotonie est ici.
        if (fnew < o.cliff && fnew < R.f && fnew <= R.f + o.armijo_c1 * dot) { stepped = true; break; }
      }
      alpha *= o.backtrack;
    }
    if (!stepped) { R.stop = "recherche lineaire epuisee"; break; }

    for (int j = 0; j < D; ++j) { s[j] = zn[j] - z[j]; y[j] = 0.0; }
    gprev = g;
    have_s = true;
    z = zn;
    R.f = fnew;
  }

  emit(z);
  return R;
}

// --- MBH À RAFFINEUR LOCAL ---------------------------------------------------
// Le MBH d'origine affinait par une DE dans une boîte réduite : il ne pouvait
// pas suivre une vallée qui sortait de cette boîte. Ici l'affinage est LOCAL et
// LIBRE dans toute la boîte du problème : il suit la vallée jusqu'au bout.
struct MbhLocalResult {
  std::vector<double> x;
  double f{1e300};
  long long evals{0};
  int hops{0}, improvements{0}, restarts{0};
  int cliff_hits{0};
};

inline std::vector<double> mbh_random_feasible(
    const std::function<double(const std::vector<double>&)>& f,
    const std::vector<double>& lo, const std::vector<double>& hi,
    Rng& rng, double cliff, int tries, long long& evals) {
  const int D = static_cast<int>(lo.size());
  std::vector<double> x(D);
  for (int t = 0; t < tries; ++t) {
    for (int j = 0; j < D; ++j) x[j] = rng.uniform(lo[j], hi[j]);
    ++evals;
    if (f(x) < cliff) return x;
  }
  return x;   // faute de mieux : le raffineur rendra "depart infaisable", c'est correct
}

inline MbhLocalResult mbh_refine(
    const std::function<double(const std::vector<double>&)>& f,
    const std::vector<double>& lo, const std::vector<double>& hi,
    const std::vector<double>& x0,          // vide => départ aléatoire
    int hops, double radius, std::uint64_t seed,
    const RefineOptions& opt = RefineOptions{},
    int stall_restart = 0) {                // 0 => jamais de redémarrage
  const int D = static_cast<int>(lo.size());
  Rng rng(seed);
  MbhLocalResult B;
  long long ev = 0;

  std::vector<double> start = x0.empty()
      ? mbh_random_feasible(f, lo, hi, rng, opt.cliff, 5000, ev)
      : x0;
  auto r0 = refine_local(f, lo, hi, start, opt);
  ev += r0.evals;
  B.x = r0.x; B.f = r0.f; B.cliff_hits += r0.cliff_hits;

  std::vector<double> xp(D);
  int stall = 0;
  for (int h = 0; h < hops; ++h) {
    for (int j = 0; j < D; ++j) {
      const double w = radius * (hi[j] - lo[j]);
      xp[j] = std::clamp(B.x[j] + w * (2.0 * rng.uniform01() - 1.0), lo[j], hi[j]);
    }
    auto r = refine_local(f, lo, hi, xp, opt);
    ev += r.evals;
    ++B.hops;
    B.cliff_hits += r.cliff_hits;
    if (r.ok && r.f < B.f) { B.f = r.f; B.x = r.x; ++B.improvements; stall = 0; }
    else                   { ++stall; }

    if (stall_restart > 0 && stall >= stall_restart) {
      auto xr = mbh_random_feasible(f, lo, hi, rng, opt.cliff, 5000, ev);
      auto rr = refine_local(f, lo, hi, xr, opt);
      ev += rr.evals;
      ++B.restarts;
      stall = 0;
      if (rr.ok && rr.f < B.f) { B.f = rr.f; B.x = rr.x; ++B.improvements; }
    }
  }
  B.evals = ev;
  return B;
}

} // namespace fen::astro
