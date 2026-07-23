// fen/astro/Porkchop.hpp
// PORKCHOP — la carte des fenêtres de lancement.
//
// Grille de solutions de Lambert sur (date de départ, durée de transit). Chaque
// point donne le C3 exigé du lanceur et le v_inf d'arrivée. C'est LE document de
// travail de tout concepteur de mission interplanétaire, et c'est un OUTIL DE
// CONCEPTION 2 CORPS : il ignore la gravité de la Terre au départ, celle de Mars
// à l'arrivée, Jupiter, et la poussée finie.
//
// Son erreur n'est pas un défaut : elle est FACTURÉE au joueur en Delta-v de
// correction. C'est exactement l'objet de l'économie de fidélité de modèle.
#pragma once
#include <vector>
#include <cmath>
#include <limits>
#include "fen/astro/Lambert.hpp"
#include "fen/ephem/Ephemeris.hpp"
#include "fen/core/Epoch.hpp"

namespace fen::astro {

struct PorkchopPoint {
  double t_dep{};     // s TDB
  double tof{};       // s
  double c3{};        // m^2/s^2  (= |v_inf depart|^2)
  double vinf_dep{};  // m/s
  double vinf_arr{};  // m/s
  Vec3   v1{}, v2{};  // vitesses héliocentriques de la solution de Lambert
  bool   ok{false};
};

struct Porkchop {
  std::vector<PorkchopPoint> pts;
  int n_dep{}, n_tof{};
  PorkchopPoint best_c3;          // C3 minimal
  PorkchopPoint best_total;       // min (v_inf_dep + v_inf_arr) — proxy du Delta-v total
  const PorkchopPoint& at(int i, int j) const { return pts[i * n_tof + j]; }
};

inline Porkchop porkchop(const ephem::IEphemeris& eph, ephem::Body dep, ephem::Body arr,
                         double t_dep0, double t_dep1, int n_dep,
                         double tof_min, double tof_max, int n_tof) {
  Porkchop pc;
  pc.n_dep = n_dep;
  pc.n_tof = n_tof;
  pc.pts.resize(static_cast<std::size_t>(n_dep) * n_tof);
  pc.best_c3.c3 = std::numeric_limits<double>::infinity();
  double best_sum = std::numeric_limits<double>::infinity();

  for (int i = 0; i < n_dep; ++i) {
    const double td = t_dep0 + (t_dep1 - t_dep0) * i / std::max(1, n_dep - 1);
    const auto E = eph.state(dep, ephem::Body::Sun, Epoch{td});
    for (int j = 0; j < n_tof; ++j) {
      const double tof = tof_min + (tof_max - tof_min) * j / std::max(1, n_tof - 1);
      const auto M = eph.state(arr, ephem::Body::Sun, Epoch{td + tof});

      PorkchopPoint& p = pc.pts[static_cast<std::size_t>(i) * n_tof + j];
      p.t_dep = td;
      p.tof = tof;

      auto L = lambert(E.r, M.r, tof, cst::MU_SUN, true, 0);
      if (!L.ok) continue;
      p.v1 = L.solutions[0].v1;
      p.v2 = L.solutions[0].v2;
      p.vinf_dep = norm(p.v1 - E.v);
      p.vinf_arr = norm(p.v2 - M.v);
      p.c3 = p.vinf_dep * p.vinf_dep;
      p.ok = true;

      if (p.c3 < pc.best_c3.c3) pc.best_c3 = p;
      const double s = p.vinf_dep + p.vinf_arr;
      if (s < best_sum) { best_sum = s; pc.best_total = p; }
    }
  }
  return pc;
}

} // namespace fen::astro
