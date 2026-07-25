// fen/astro/LaunchWindow.hpp — LA FENÊTRE DE LANCEMENT [GDD 7.3]
//
// « On ne cherche une fenêtre que si la conception est viable ; on ne lance que
// si le ciel est là. » La géométrie des corps commande : une fenêtre vers Mars
// revient toutes les ~779.9 j (période synodique), et la rater coûte 25.6 mois
// d'attente. Ce module répond à UNE question, sans magie : « à cette date, la
// fenêtre est-elle ouverte, et sinon dans combien de temps ? »
//
// La réponse est AUTO-CALIBRÉE, pas jugée contre une constante inventée : on
// balaie un horizon d'au moins une période synodique (la vraie carte porkchop,
// positions réelles) pour trouver l'OPTIMUM de la fenêtre, puis on déclare la
// fenêtre ouverte si, MAINTENANT (à `slop` jours près), le meilleur transfert
// disponible est à moins de `factor` fois cet optimum. Rien n'est arcade : le
// coût vient de Lambert sur l'éphéméride, le critère est un rapport géométrique.
//
// Réutilise `porkchop` (déjà sous oracle). Outil 2-corps : son erreur est
// FACTURÉE en Δv de correction plus tard, comme le porkchop lui-même.
#pragma once
#include <algorithm>
#include <limits>
#include <vector>

#include "fen/astro/Porkchop.hpp"
#include "fen/core/Constants.hpp"
#include "fen/core/Epoch.hpp"
#include "fen/ephem/Ephemeris.hpp"

namespace fen::astro {

struct WindowParams {
  double slop_days{60.0};       // largeur opérationnelle de « maintenant »
  double horizon_days{800.0};   // >= 1 période synodique Terre-Mars (779.9 j)
  double tof_min_days{150.0};   // durées de transit explorées (famille Hohmann)
  double tof_max_days{400.0};
  int    n_dep{80};             // résolution en date de départ sur l'horizon
  int    n_tof{40};             // résolution en durée de transit
  double factor{1.30};          // ouvert si local <= factor * optimum synodique
};

struct WindowResult {
  bool   ok{false};             // au moins une solution de Lambert sur l'horizon
  bool   open{false};           // la fenêtre est-elle ouverte MAINTENANT ?
  double local_best{0.0};       // min (vinf_dep + vinf_arr) sur [now, now+slop], m/s
  double global_best{0.0};      // min sur [now, now+horizon] = optimum synodique, m/s
  double best_dep_tdb{0.0};     // date de départ de cet optimum (s TDB)
  double next_open_days{-1.0};  // délai jusqu'à la prochaine ouverture (j) ; -1 si aucune
  // v_inf de l'optimum, séparés : de quoi calculer le Δv RÉEL (Oberth) d'une
  // injection depuis une orbite de parking et d'une insertion à l'arrivée
  // (Transfers.hpp : injection_dv_from_circular / capture_dv_to_*).
  double vinf_dep{0.0};         // excès hyperbolique de départ, m/s (C3 = vinf_dep²)
  double vinf_arr{0.0};         // excès hyperbolique d'arrivée, m/s
};

// Métrique de coût géométrique d'un point : la somme des excès hyperboliques.
// C'est le proxy standard du Δv total d'une mission balistique (injection +
// insertion), celui que `porkchop::best_total` minimise.
inline WindowResult launch_window(const ephem::IEphemeris& eph,
                                  ephem::Body dep, ephem::Body arr,
                                  Epoch now, const WindowParams& p = {}) {
  WindowResult w;
  const double t0 = now.tdb;
  const Porkchop pc = porkchop(
      eph, dep, arr, t0, t0 + p.horizon_days * cst::DAY, p.n_dep,
      p.tof_min_days * cst::DAY, p.tof_max_days * cst::DAY, p.n_tof);

  const double INF = std::numeric_limits<double>::infinity();
  const double slop_end = t0 + p.slop_days * cst::DAY;

  // Meilleur coût PAR COLONNE de départ (min sur les durées de transit).
  std::vector<double> col_best(static_cast<std::size_t>(p.n_dep), INF);
  std::vector<double> col_t(static_cast<std::size_t>(p.n_dep), 0.0);
  double global_best = INF, local_best = INF, best_dep = t0;
  double best_vinf_dep = 0.0, best_vinf_arr = 0.0;

  for (int i = 0; i < p.n_dep; ++i) {
    double bi = INF, bi_dep = 0.0, bi_arr = 0.0;
    for (int j = 0; j < p.n_tof; ++j) {
      const PorkchopPoint& q = pc.at(i, j);
      if (!q.ok) continue;
      const double s = q.vinf_dep + q.vinf_arr;
      if (s < bi) { bi = s; bi_dep = q.vinf_dep; bi_arr = q.vinf_arr; }
    }
    const double td = pc.at(i, 0).t_dep;   // identique pour tout j d'une colonne
    col_best[static_cast<std::size_t>(i)] = bi;
    col_t[static_cast<std::size_t>(i)] = td;
    if (bi < global_best) {
      global_best = bi; best_dep = td;
      best_vinf_dep = bi_dep; best_vinf_arr = bi_arr;
    }
    if (td <= slop_end && bi < local_best) local_best = bi;
  }

  if (!std::isfinite(global_best)) return w;   // aucune solution : ok reste false

  w.ok = true;
  w.global_best = global_best;
  w.local_best = std::isfinite(local_best) ? local_best : global_best;
  w.best_dep_tdb = best_dep;
  w.vinf_dep = best_vinf_dep;
  w.vinf_arr = best_vinf_arr;

  const double threshold = p.factor * global_best;
  w.open = std::isfinite(local_best) && local_best <= threshold;

  // Prochaine ouverture : première colonne (dans l'ordre du temps) sous le seuil.
  w.next_open_days = -1.0;
  for (int i = 0; i < p.n_dep; ++i) {
    if (col_t[static_cast<std::size_t>(i)] >= t0 &&
        col_best[static_cast<std::size_t>(i)] <= threshold) {
      w.next_open_days = std::max(0.0, (col_t[static_cast<std::size_t>(i)] - t0) / cst::DAY);
      break;
    }
  }
  return w;
}

} // namespace fen::astro
