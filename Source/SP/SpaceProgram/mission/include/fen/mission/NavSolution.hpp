// fen/mission/NavSolution.hpp — LA POURSUITE [GDD 7.5, 8.1, 8.6]
//
// « Le joueur ne voit jamais une position vraie absolue, mais une SOLUTION DE
// NAVIGATION avec incertitude en position et vitesse » [GDD 7.5]. Jusqu'ici il ne
// voyait rien du tout : l'erreur d'injection était commise (Navigation.hpp) et
// restait invisible jusqu'au débrief. Ce fichier lui donne le moyen de la
// MESURER — et ce moyen se paie.
//
// `nav/Tracking.hpp` l'annonce depuis toujours dans son en-tête : « sans
// poursuite, le joueur ne SAIT PAS que son erreur d'exécution a eu lieu. Il croit
// que sa manœuvre est passée au nominal. Sa correction est donc calculée sur un
// état faux — donc inutile. » C'est cette phrase qu'on rend vraie ici, et
// `Program::tracking_days`, acheté et facturé depuis le premier jour, cesse
// enfin d'être décoratif.
//
// ═══ CE QUI EST CALCULÉ, ET RIEN N'EST INVENTÉ ═══
// Un filtre par lots BAYÉSIEN, la forme standard (Tapley, Schutz & Born ch. 4) :
//
//     Λ = P0⁻¹ + Σ_k (H_k Φ_k)ᵀ W_k (H_k Φ_k)          (matrice d'information)
//     δx̂ = Λ⁻¹ Σ_k (H_k Φ_k)ᵀ W_k (y_k − h_k(x_nom))    (estimé)
//     P  = Λ⁻¹                                          (covariance)
//
// avec, sans exception, des pièces DÉJÀ dans le moteur et déjà sous oracle :
//   . H_k        — `nav::predict`, partielles exactes de (distance, vitesse
//                  radiale) par rapport à l'état, pour les trois complexes RÉELS
//                  du Deep Space Network (`nav::dsn_complexes`) ;
//   . visibilité — `nav::station_visible` (masque d'élévation réel) ;
//   . Φ_k        — la matrice de transition képlérienne de `Navigation.hpp` ;
//   . P0         — l'a priori EST la dispersion d'injection : avant de mesurer,
//                  le joueur ne connaît que la loi de son erreur. C'est ce qui
//                  rend le problème bien posé, et c'est vrai.
//
// ═══ POURQUOI PAS `nav::batch_least_squares` ═══ [GDD 6.8]
// La machinerie complète existe et est plus fidèle (propagateur n-corps, STM par
// différences finies sur la VÉRITÉ). Elle demande une propagation numérique par
// mesure, toutes les 60 s sur un arc de deux semaines : hors de portée d'un
// écran qui se rafraîchit. On garde la MÊME algèbre et les MÊMES partielles, sur
// des états KÉPLÉRIENS — la même approximation de coniques raccordées que l'arc
// qu'on estime, donc pas de modèle plus grossier que ce qu'il mesure.
//
// C++ pur, aucune dépendance UE.
#pragma once
#include <cmath>
#include <cstdint>
#include <vector>

#include "fen/astro/Kepler.hpp"
#include "fen/core/Constants.hpp"
#include "fen/core/Matrix.hpp"
#include "fen/core/Rng.hpp"
#include "fen/core/Vec3.hpp"
#include "fen/ephem/BodyOrientation.hpp"
#include "fen/ephem/Ephemeris.hpp"
#include "fen/mission/Navigation.hpp"
#include "fen/nav/Tracking.hpp"

namespace fen::mission {

// Le DSN mesure dans le repère ÉQUATORIAL géocentrique (c'est là que vivent les
// antennes) ; l'arc vit en ÉCLIPTIQUE héliocentrique. `equatorial_to_ecliptic`
// existe ; voici sa réciproque — rotation d'angle −ε autour de x.
inline Vec3 ecliptic_to_equatorial(const Vec3& v) {
  const double e = cst::OBLIQUITY_J2000;
  const double c = std::cos(e), s = std::sin(e);
  return {v.x, c * v.y - s * v.z, s * v.y + c * v.z};
}

// CADENCE DE POURSUITE — une mesure toutes les 8 h par station visible, soit
// l'ordre d'une passe quotidienne par complexe. APPROXIMATION DÉCLARÉE
// [GDD 6.8] : une passe réelle est un train de points corrélés ; on en retient
// un échantillon indépendant, ce qui SOUS-ESTIME l'information acquise — donc
// se trompe dans le sens sévère (piège n°25).
inline constexpr double TRACKING_SAMPLE_S = 8.0 * 3600.0;

// ═══ CE QUE COÛTE D'ÉCOUTER ═══ [GDD 8.6, 13.3]
// « Trop fréquent coûte des ressources et du temps. » Le temps était déjà payé
// (`Assessment::schedule_months` intègre `tracking_days`) ; l'argent ne l'était
// pas : `Program::tracking_musd` et `Program::tracking_days` étaient deux nombres
// LIBRES et INDÉPENDANTS — on pouvait acheter cent jours de poursuite pour zéro.
// Il manquait un tarif.
//
// LA PASSE EST DÉJÀ DANS LE MODÈLE : c'est `TRACKING_SAMPLE_S` ci-dessus, les 8 h
// d'une passe quotidienne par complexe. On ne la redéclare donc pas ici — le coût
// s'en déduit, et le jour où la cadence change, le tarif suit tout seul.
//
// TARIF HORAIRE D'ANTENNE : ordre de grandeur du fee d'ouverture d'une antenne
// 34 m du Deep Space Network, ~1 100 $/h. APPROXIMATION DÉCLARÉE [GDD 6.8,
// Annexe E — à calibrer] : le coût complet d'une campagne de navigation réelle
// dépasse largement l'ouverture d'antenne (équipe de nav, opérations), et c'est
// le TEMPS, pas cette facture, qui porte l'arbitrage de [GDD 8.6].
inline constexpr double DSN_COUT_ME_PAR_HEURE_ANTENNE = 0.0011;   // M€/h

// Coût d'un arc de `jours` d'écoute. Les trois complexes du DSN ne sont pas tous
// visibles à la fois : `nav::dsn_complexes()` en compte trois et la géométrie en
// laisse voir un à deux — on facture les heures d'antenne RÉELLEMENT ouvertes,
// soit une passe par complexe et par échantillon.
inline double cout_poursuite_me(double jours, int n_complexes_ouverts = 2) {
  if (jours <= 0.0 || n_complexes_ouverts <= 0) return 0.0;
  const double heures_par_complexe = jours * 24.0;
  return DSN_COUT_ME_PAR_HEURE_ANTENNE * heures_par_complexe *
         static_cast<double>(n_complexes_ouverts);
}

// A priori de POSITION à l'injection : le lanceur sait où il a lâché sa charge.
// Un kilomètre est généreux ; il sert surtout à rendre l'information inversible
// dans les directions que la poursuite n'observe pas.
inline constexpr double SIGMA_R0_INJECTION_M = 1000.0;

struct NavSolution {
  bool   ok{false};
  int    n_mesures{0};
  double arc_jours{0.0};
  Vec3   dr_estime{};        // écart ESTIMÉ au nominal, à l'injection (m)
  Vec3   dv_estime{};        // ... en vitesse (m/s)
  double sigma_r{0.0};       // 1σ de la solution en position (m)
  double sigma_v{0.0};       // 1σ en vitesse (m/s)
  double dv_vrai{0.0};       // ORACLE SEULEMENT : |erreur vraie|, jamais publié
  double erreur_estime{0.0}; // ORACLE SEULEMENT : |estimé − vrai| en vitesse
};

// `arc_days` : la durée de poursuite ACHETÉE (`Program::tracking_days`). Zéro =
// aucune mesure : la solution se réduit à l'a priori, l'écart estimé est NUL et
// l'incertitude vaut la dispersion d'injection. Autrement dit, le joueur croit
// que sa manœuvre est passée au nominal — ce qui est faux, et lui coûtera.
inline NavSolution nav_solution(const FlightTrace& tr, const NavDispersion& disp,
                                const NavRealisation& reel, double arc_days,
                                double epoch_dep_tdb, const ephem::IEphemeris& eph,
                                std::uint64_t seed) {
  NavSolution s;
  if (!tr.ok || !disp.ok || !reel.ok || tr.n_nodes < 2) return s;
  s.dv_vrai = norm(reel.v_dep_vraie - tr.v_dep);

  const double s_axe = disp.sigma_vinf / std::sqrt(3.0);
  const auto& stations = nav::dsn_complexes();
  const double arc_s = arc_days * cst::DAY;
  s.arc_jours = arc_days;

  // ---- 1) LES OBSERVATIONS, tirées UNE FOIS -------------------------------
  // Une mesure est un fait : on ne la retire pas à chaque itération du fit,
  // sinon le bruit changerait sous le solveur et il ne convergerait vers rien.
  struct Obs { double t_tdb, dt, y_range, y_rr, w_range, w_rr; std::size_t station; };
  std::vector<Obs> obs;
  {
    Rng rng = Rng(seed).substream(0x4D455355'00000001ull);   // « MESU »
    for (double dt = TRACKING_SAMPLE_S; dt <= arc_s; dt += TRACKING_SAMPLE_S) {
      const auto Kv = astro::kepler_propagate(tr.r_dep, reel.v_dep_vraie, dt, cst::MU_SUN);
      if (!Kv.converged) continue;
      const double t_tdb = epoch_dep_tdb + dt;
      const ephem::PosVel terre =
          eph.state(ephem::Body::EarthBary, ephem::Body::Sun, Epoch{t_tdb});
      // Géocentrique ÉQUATORIAL : le repère des antennes.
      const Vec3 rv = ecliptic_to_equatorial(Kv.r - terre.r);
      const Vec3 vv = ecliptic_to_equatorial(Kv.v - terre.v);
      for (std::size_t k = 0; k < stations.size(); ++k) {
        if (!nav::station_visible(stations[k], t_tdb, rv)) continue;
        const nav::MeasPredict pv = nav::predict(stations[k], t_tdb, rv, vv);
        const double sr = stations[k].sigma_range, srr = stations[k].sigma_rangerate;
        // La mesure porte sur la VÉRITÉ, bruitée. C'est le seul endroit du
        // modèle où la vérité entre dans ce que le joueur peut connaître — et
        // elle n'y entre que parce qu'il a payé pour l'observer.
        obs.push_back({t_tdb, dt, pv.range + rng.normal(0.0, sr),
                       pv.range_rate + rng.normal(0.0, srr),
                       1.0 / (sr * sr), 1.0 / (srr * srr), k});
      }
    }
  }
  s.n_mesures = static_cast<int>(obs.size());

  // ---- 2) LE FIT, ITÉRÉ (Gauss-Newton) ------------------------------------
  // La mesure n'est PAS linéaire en l'état : linéariser une seule fois autour du
  // nominal, à des milliers de km de la vérité, donne un estimé correct mais une
  // covariance MENSONGÈRE — le filtre s'annonçait 300 fois plus précis qu'il ne
  // l'était (piège n°66). On re-linéarise donc autour de l'estimé courant,
  // exactement comme `nav::batch_least_squares` le fait sur le propagateur de
  // vérité. Trois passes suffisent : la quatrième ne déplace plus rien.
  Vec3 r_ref = tr.r_dep, v_ref = tr.v_dep;
  Mat6 P;
  bool inverse_ok = false;
  for (int iter = 0; iter < 3; ++iter) {
    Mat6 Lambda = Mat6::zero();
    double b[6] = {0, 0, 0, 0, 0, 0};
    // A PRIORI : ce qu'on sait AVANT toute mesure — la loi de l'erreur
    // d'injection. Son résidu se compte depuis la référence courante.
    const Vec3 dr_ap = tr.r_dep - r_ref, dv_ap = tr.v_dep - v_ref;
    for (int i = 0; i < 3; ++i) {
      const double wr = 1.0 / (SIGMA_R0_INJECTION_M * SIGMA_R0_INJECTION_M);
      const double wv = 1.0 / (s_axe * s_axe);
      Lambda.m[i][i] += wr;
      Lambda.m[3 + i][3 + i] += wv;
      b[i] += wr * dr_ap[i];
      b[3 + i] += wv * dv_ap[i];
    }

    for (const Obs& o : obs) {
      const auto Kn = astro::kepler_propagate(r_ref, v_ref, o.dt, cst::MU_SUN);
      if (!Kn.converged) continue;
      const StmBlocks Phi = kepler_stm(r_ref, v_ref, o.dt, cst::MU_SUN);
      if (!Phi.ok) continue;
      const ephem::PosVel terre =
          eph.state(ephem::Body::EarthBary, ephem::Body::Sun, Epoch{o.t_tdb});
      const Vec3 rn = ecliptic_to_equatorial(Kn.r - terre.r);
      const Vec3 vn = ecliptic_to_equatorial(Kn.v - terre.v);
      const nav::MeasPredict pn = nav::predict(stations[o.station], o.t_tdb, rn, vn);
      const double res[2] = {o.y_range - pn.range, o.y_rr - pn.range_rate};
      const double w[2] = {o.w_range, o.w_rr};

      // H (2x6, repère équatorial géocentrique) -> repère de l'ARC :
      //   . rotation équatorial <- écliptique sur chaque bloc ;
      //   . puis matrice de transition jusqu'à l'époque d'injection.
      for (int row = 0; row < 2; ++row) {
        // dh/d(r_ecl), dh/d(v_ecl) : on applique la rotation par sa TRANSPOSÉE
        // (les partielles se transforment en covecteurs).
        const Vec3 hr_eq{pn.H[row][0], pn.H[row][1], pn.H[row][2]};
        const Vec3 hv_eq{pn.H[row][3], pn.H[row][4], pn.H[row][5]};
        const Vec3 hr = ephem::equatorial_to_ecliptic(hr_eq);
        const Vec3 hv = ephem::equatorial_to_ecliptic(hv_eq);
        // H_eff[j] = Σ_i ( hr_i Φ_rr[i][j] + hv_i Φ_vr[i][j] ) pour j en position,
        //            Σ_i ( hr_i Φ_rv[i][j] + hv_i Φ_vv[i][j] ) pour j en vitesse.
        double He[6];
        for (int j = 0; j < 3; ++j) {
          double a = 0.0, c = 0.0;
          for (int i = 0; i < 3; ++i) {
            a += hr[i] * Phi.rr.m[i][j] + hv[i] * Phi.vr.m[i][j];
            c += hr[i] * Phi.rv.m[i][j] + hv[i] * Phi.vv.m[i][j];
          }
          He[j] = a;
          He[3 + j] = c;
        }
        for (int i = 0; i < 6; ++i) {
          b[i] += He[i] * w[row] * res[row];
          for (int j = 0; j < 6; ++j) Lambda.m[i][j] += He[i] * w[row] * He[j];
        }
      }
    }

    if (!invert6(Lambda, P)) return s;
    inverse_ok = true;
    double d[6] = {0, 0, 0, 0, 0, 0};
    for (int i = 0; i < 6; ++i)
      for (int j = 0; j < 6; ++j) d[i] += P.m[i][j] * b[j];
    r_ref = r_ref + Vec3{d[0], d[1], d[2]};
    v_ref = v_ref + Vec3{d[3], d[4], d[5]};
  }
  if (!inverse_ok) return s;

  s.dr_estime = r_ref - tr.r_dep;
  s.dv_estime = v_ref - tr.v_dep;
  s.sigma_r = sigma_position(P);
  s.sigma_v = sigma_velocity(P);
  s.erreur_estime = norm(s.dv_estime - (reel.v_dep_vraie - tr.v_dep));
  s.ok = true;
  return s;
}

// ═══ LA CAMPAGNE DE CORRECTION [GDD 8.4, 8.6] ═══
// « Mesurer l'état réel → recalculer une solution depuis la position actuelle →
// déterminer le Δv et son orientation → exécuter → vérifier. »
//
// LE POINT DE TOUT CE QUI PRÉCÈDE : la correction se calcule sur ce que le joueur
// CROIT, pas sur ce qui EST. Sans poursuite il croit son vol nominal, ne corrige
// donc rien, et rate sa cible de millions de km. Avec poursuite il corrige presque
// juste — et ce qui reste, c'est l'erreur d'exécution de la correction elle-même,
// qu'une seconde manœuvre rattrape. C'est exactement pourquoi les missions réelles
// enchaînent plusieurs TCM.
//
// APPROXIMATION DÉCLARÉE [GDD 6.8] : DEUX manœuvres. Une campagne martienne
// réelle en compte quatre à six ; deux suffisent à faire apparaître la loi (la
// première encaisse l'erreur d'injection, la seconde son propre résidu) et rien
// n'empêchera d'en ajouter. Entre les deux, la poursuite CONTINUE : la
// connaissance résiduelle vaut alors le σ atteint par la solution.
//
// LES DATES des deux corrections viennent de la CHRONOLOGIE (FlightTimeline.hpp,
// TCM1_APRES_INJECTION_J et TCM2_AVANT_ARRIVEE_J) : ce sont les mêmes instants
// que ceux où le plafond de cadence ramène le monde au temps réel. Un chiffre,
// une source — sinon le modèle corrigerait à une date et le jeu à une autre.

struct NavCampagne {
  bool   ok{false};
  double dv_total{0.0};      // Δv réellement dépensé par la campagne (m/s)
  double miss_final_km{0.0}; // manque au but après correction
  double dv_tcm1{0.0}, dv_tcm2{0.0};
};

// ═══ CONDUIRE LES RENDEZ-VOUS QUI RESTENT ═══
//
// Une correction n'est pas un traitement de fin de vol : c'est un RENDEZ-VOUS
// DATÉ, et il n'a lieu que si quelqu'un est là pour le commander. La campagne
// commence donc là où le vol EN EST — pas forcément à l'injection. Quand le
// joueur a déjà corrigé de sa main, l'état vrai est celui que SA manœuvre a
// laissé, et il ne reste à tenir que les rendez-vous postérieurs.
//
// UNE SEULE LOI DE CAMPAGNE, DEUX POINTS D'ENTRÉE : sans cela l'adjoint absent
// [GDD 9.3] et le logiciel de bord [GDD 15.3] corrigeraient différemment pour la
// même physique. `nav_campagne` ci-dessous n'est que le cas « depuis l'injection,
// personne n'a rien fait ».
//
// `r`/`v`/`t_days` sont l'état VRAI, en entrée ET en sortie : la campagne le fait
// avancer. `t_days` est une date de jeu ABSOLUE, comme celles des nœuds de la
// trace — un rendez-vous antérieur à cette date a déjà été franchi, il n'est pas
// à prendre.
// `sol1`/`sol2` : la connaissance disponible AU MOMENT de chaque rendez-vous.
// Deux solutions et non une, parce que l'arc de poursuite GRANDIT entre les deux
// [GDD 8.6] : corriger TCM-1 avec ce qu'on saura à TCM-2 serait se donner des
// mesures qui n'ont pas encore été faites. Quand la poursuite ne change pas, les
// deux sont la même et le résultat est celui d'avant.
inline NavCampagne nav_campagne_depuis(const FlightTrace& tr, const NavDispersion& disp,
                                       const NavSolution& sol1, const NavSolution& sol2,
                                       Vec3& r, Vec3& v, double& t_days,
                                       std::uint64_t seed,
                                       const nav::GatesParams& gates = {}) {
  const NavSolution& sol = sol1;
  NavCampagne c;
  if (!tr.ok || !disp.ok || !sol1.ok || !sol2.ok || tr.n_nodes < 2) return c;
  const double t_dep = tr.nodes[0].t_days, t_arr = tr.nodes[1].t_days;
  const double tof_s = (t_arr - t_dep) * cst::DAY;
  if (tof_s <= 0.0) return c;

  // Cible visée : le point d'arrivée NOMINAL.
  const auto Knom = astro::kepler_propagate(tr.r_dep, tr.v_dep, tof_s, cst::MU_SUN);
  if (!Knom.converged) return c;
  const Vec3 cible = Knom.r;

  Rng rng = Rng(seed).substream(0x54434D00'00000001ull);   // « TCM »

  // Une manœuvre : on vise la cible depuis l'état CRU (vrai + erreur de
  // connaissance), on calcule le Δv par la matrice de transition, et on
  // l'exécute avec l'erreur d'exécution de Gates.
  auto manoeuvre = [&](double t_rdv_days, double sigma_connaissance) -> double {
    if (t_rdv_days <= t_days) return 0.0;   // rendez-vous déjà franchi : pas le sien
    const auto K = astro::kepler_propagate(r, v, (t_rdv_days - t_days) * cst::DAY,
                                           cst::MU_SUN);
    if (!K.converged) return 0.0;
    r = K.r; v = K.v; t_days = t_rdv_days;
    const double reste = (t_arr - t_days) * cst::DAY;
    if (reste <= 0.0) return 0.0;
    const StmBlocks Phi = kepler_stm(r, v, reste, cst::MU_SUN);
    M3 inv_rv;
    if (!Phi.ok || !inverse3(Phi.rv, inv_rv)) return 0.0;
    // CE QUE L'AGENT CROIT : l'état vrai vu à travers son incertitude.
    const Vec3 v_cru = v + Vec3{rng.normal(0.0, sigma_connaissance),
                                rng.normal(0.0, sigma_connaissance),
                                rng.normal(0.0, sigma_connaissance)};
    const auto Kfin = astro::kepler_propagate(r, v_cru, reste, cst::MU_SUN);
    if (!Kfin.converged) return 0.0;
    const Vec3 dv_cmd = inv_rv * (cible - Kfin.r);
    const Vec3 dv_obtenu = nav::apply_gates(dv_cmd, gates, rng);
    v = v + dv_obtenu;
    return norm(dv_obtenu);
  };

  // TCM-1 : la connaissance est celle que la poursuite a achetée. SANS
  // poursuite, `dv_estime` est nul et l'incertitude vaut la dispersion entière —
  // l'agent vise donc en croyant le vol nominal, et ne corrige rien d'utile.
  const double sigma1 = std::sqrt(sol1.erreur_estime * sol1.erreur_estime +
                                  sol1.sigma_v * sol1.sigma_v);
  c.dv_tcm1 = manoeuvre(t_dep + disp.t_tcm_days, sigma1);
  // TCM-2 : la poursuite a continué — d'où `sol2`, et pas `sol1` : la
  // connaissance vaut le σ que l'arc ATTEINT À CETTE DATE a produit.
  c.dv_tcm2 = manoeuvre(t_arr - TCM2_AVANT_ARRIVEE_J,
                        std::fmax(sol2.sigma_v, 1.0e-4));

  const auto Kf = astro::kepler_propagate(r, v, (t_arr - t_days) * cst::DAY, cst::MU_SUN);
  if (!Kf.converged) return c;
  c.miss_final_km = norm(Kf.r - cible) / 1000.0;
  c.dv_total = c.dv_tcm1 + c.dv_tcm2;
  c.ok = true;
  return c;
}

// LA CAMPAGNE COMPLÈTE, depuis l'injection : personne n'a encore rien fait, les
// deux rendez-vous sont à prendre. C'est le comportement de l'ADJOINT pendant
// une absence du joueur [GDD 9.3] — jamais le repli silencieux d'un joueur
// présent qui n'a rien commandé.
inline NavCampagne nav_campagne(const FlightTrace& tr, const NavDispersion& disp,
                                const NavRealisation& reel, const NavSolution& sol,
                                std::uint64_t seed,
                                const nav::GatesParams& gates = {}) {
  if (!tr.ok || !reel.ok || tr.n_nodes < 2) return {};
  Vec3 r = tr.r_dep, v = reel.v_dep_vraie;   // l'état VRAI, que le joueur ne voit pas
  double t = tr.nodes[0].t_days;
  // Une seule solution pour les deux rendez-vous : la poursuite ne bouge pas
  // entre eux dans ce cas d'entrée.
  return nav_campagne_depuis(tr, disp, sol, sol, r, v, t, seed, gates);
}

} // namespace fen::mission
