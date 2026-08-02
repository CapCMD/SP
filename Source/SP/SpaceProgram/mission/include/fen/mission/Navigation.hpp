// fen/mission/Navigation.hpp — LA DISPERSION DE NAVIGATION [GDD 7.5, 8.1-8.5]
//
// `MissionPlan::p_physics` valait **0,985**, avec en commentaire « issue du MC,
// simplifiée ». C'était le dernier chiffre magique de la boucle de mission : un
// quart de la probabilité de succès d'un vol ne dépendait de RIEN — ni du moteur
// choisi, ni de la marge de correction provisionnée, ni de la géométrie du
// transfert. Ce fichier le REMPLACE par un calcul.
//
// ═══ LA CHAÎNE, ET ELLE EST ENTIÈREMENT DÉRIVÉE ═══
//   1. L'injection n'est jamais exécutée exactement : `nav/Gates.hpp` (Gates,
//      1963) donne son écart-type à partir du Δv commandé — déjà dans le moteur,
//      déjà sous oracle.
//   2. Une erreur au périgée est AMPLIFIÉE en héliocentrique : v∞² = v_p² − v_esc²
//      donne v∞ δv∞ = v_p δv_p, donc δv∞ = (v_p/v∞) δv_p. C'est l'effet Oberth
//      lu à l'envers, et c'est pour cela qu'une injection interplanétaire se
//      mesure au dixième de m/s.
//   3. Cette erreur de vitesse se propage en erreur de POSITION à l'arrivée, par
//      la matrice de transition de l'arc : δr_arr = Φ_rv(arr←dep) δv.
//   4. La CORRECTION de mi-parcours qui annule ce manque au but est, elle aussi,
//      linéaire en δv. Son Δv est donc une variable aléatoire dont on connaît la
//      loi — et la question de jeu devient exacte : **la marge provisionnée
//      couvre-t-elle la correction qu'il faudra faire ?**
//   5. P(succès de navigation) = P(|Δv_corr| ≤ marge). Loi de Maxwell (norme
//      d'un vecteur gaussien 3D), forme close.
//
// Conséquence : provisionner de la marge de correction (`Program::dv_margin`) et
// choisir un moteur précis ACHÈTENT enfin quelque chose de calculé, au lieu d'un
// 0,985 immuable. Et le couplage annoncé par Program.hpp — « plus de marge ->
// étage plus lourd -> lanceur plus cher » — se ferme : les deux bouts existent.
//
// ═══ LA MATRICE DE TRANSITION ═══
// Obtenue par DIFFÉRENCES FINIES CENTRÉES sur le propagateur képlérien de l'arc,
// exactement comme `nav/OrbitDetermination.hpp` la prend sur le propagateur de
// vérité : aucun modèle dupliqué à maintenir, et c'est ce que le joueur peut
// faire lui-même avec l'API publique. APPROXIMATION DÉCLARÉE [GDD 6.8] : Kepler
// et non n-corps, cohérent avec l'arc lui-même (coniques raccordées) et avec le
// porkchop qui a choisi la fenêtre — la même approximation, donc la même erreur.
//
// C++ pur, aucune dépendance UE.
#pragma once
#include <cmath>

#include <cstdint>

#include "fen/astro/BPlane.hpp"
#include "fen/astro/Kepler.hpp"
#include "fen/astro/Stm.hpp"
#include "fen/core/Constants.hpp"
#include "fen/core/Rng.hpp"
#include "fen/core/Vec3.hpp"
#include "fen/mission/FlightTrace.hpp"
#include "fen/nav/Gates.hpp"

namespace fen::mission {

// === L'ALGEBRE DE LA CORRECTION VIT DANS astro_core ===
// `M3`, `inverse3`, `StmBlocks` et `kepler_stm` etaient definis ici. Ils ne
// connaissent pourtant que Kepler et deux corps : c'est de l'astrodynamique, et
// les laisser dans `mission/` les mettait hors de portee de `ares/vol.hpp`,
// l'API avec laquelle le JOUEUR ecrit son logiciel de vol [GDD 15.3]. On les
// re-exporte : tous les appels de `fen::mission` restent ecrits pareil, et il
// n'y a toujours qu'UNE matrice de transition dans le moteur.
using astro::M3;
using astro::inverse3;
using astro::StmBlocks;
using astro::kepler_stm;
using astro::dv_correction;

// ═══ QUAND CORRIGE-T-ON ? ═══
// Pas « quand on veut » : il faut d'abord une DÉTERMINATION D'ORBITE, donc un
// arc de poursuite. Les missions martiennes réelles placent leur TCM-1 autour de
// deux semaines après le lancement (MSL L+15 j, Mars 2020 L+14 j, MAVEN L+13 j).
// Valeur SOURCÉE, pas un réglage. APPROXIMATION DÉCLARÉE [GDD 6.8] : le budget
// de poursuite acheté (`Program::tracking_days`) ne modifie pas encore cette
// date ni la qualité de la solution — il le fera quand la détermination d'orbite
// (`nav/OrbitDetermination.hpp`) sera branchée sur la trace.
inline constexpr double TCM_ARC_DAYS = TCM1_APRES_INJECTION_J;

// Loi de MAXWELL — la norme d'un vecteur gaussien 3D isotrope d'écart-type `s`
// par axe. C'est exactement la loi du Δv de correction, puisque celui-ci est une
// application linéaire d'une erreur d'injection gaussienne.
inline double maxwell_cdf(double x, double s) {
  if (!(s > 0.0)) return x >= 0.0 ? 1.0 : 0.0;
  const double a = x / s;
  if (a <= 0.0) return 0.0;
  return std::erf(a / std::sqrt(2.0)) -
         std::sqrt(2.0 / cst::PI) * a * std::exp(-0.5 * a * a);
}
// 99e centile de la même loi : a tel que CDF(a) = 0,99 (constante numérique de
// la loi, pas un réglage — comme 1,96 l'est pour la gaussienne).
inline constexpr double MAXWELL_P99 = 3.3682;

struct NavDispersion {
  bool   ok{false};
  double dv_injection{0.0};    // Δv de l'injection depuis l'orbite de parking (m/s)
  double sigma_dv_inj{0.0};    // 1σ de l'erreur d'exécution (Gates), m/s
  double oberth_gain{0.0};     // v_p/v∞ : amplification de l'erreur en héliocentrique
  double sigma_vinf{0.0};      // 1σ de l'erreur de vitesse héliocentrique, m/s
  double sigma_r_arr_km{0.0};  // 1σ du manque au but SANS correction
  double sigma_corr{0.0};      // 1σ PAR AXE du Δv de correction requis (m/s)
  double dv_corr_p99{0.0};     // 99e centile du Δv de correction (m/s)
  double p_marge{0.0};         // P(marge provisionnée >= Δv requis)
  double t_tcm_days{0.0};      // date de la correction, depuis l'injection
};

// `dv_margin_ms` : la marge de correction PROVISIONNÉE par le programme
// (`Program::dv_margin`), déjà payée en ergols dans le bilan de masse.
inline NavDispersion nav_dispersion(const FlightTrace& tr, const Vec3& v_depart_corps,
                                    double dv_margin_ms,
                                    const nav::GatesParams& gates = {}) {
  NavDispersion d;
  if (!tr.ok || tr.n_nodes < 2) return d;

  // 1) L'INJECTION. v∞ est l'écart entre la vitesse de l'arc au départ et celle
  //    du corps quitté : c'est ce que le véhicule doit gagner en héliocentrique.
  const double vinf = norm(tr.v_dep - v_depart_corps);
  if (!(vinf > 0.0)) return d;
  const double rp = parking_radius_m();
  const double v_circ = std::sqrt(cst::MU_EARTH / rp);
  const double v_p = std::sqrt(vinf * vinf + 2.0 * cst::MU_EARTH / rp);
  d.dv_injection = v_p - v_circ;
  d.oberth_gain = v_p / vinf;

  // 2) L'ERREUR D'EXÉCUTION, puis son amplification. `gates_sigma_total` rend la
  //    norme 1σ du vecteur d'erreur : par axe, elle vaut donc /√3.
  d.sigma_dv_inj = nav::gates_sigma_total(d.dv_injection, gates);
  // APPROXIMATION DÉCLARÉE, et CONSERVATRICE [GDD 12.5] : le gain d'Oberth
  // s'applique rigoureusement à la composante ALIGNÉE ; on l'applique aux trois,
  // ce qui MAJORE la dispersion. Un modèle hors de son domaine doit se tromper
  // dans le sens sévère (piège n°25).
  d.sigma_vinf = d.oberth_gain * d.sigma_dv_inj;
  const double s_axe = d.sigma_vinf / std::sqrt(3.0);

  // 3) PROPAGATION jusqu'à LA PREMIÈRE VISÉE, et 4) CORRECTION à TCM-1.
  // ⚠ « LA PREMIÈRE VISÉE » N'EST PAS TOUJOURS L'ARRIVÉE. Pour un transfert
  // direct, si — et `t_nav_fin_days` vaut alors exactement la date d'arrivée,
  // donc rien ne change. Pour un TOUR d'assistance, la première chose que
  // l'erreur d'injection déforme est la manœuvre profonde de la première jambe,
  // à quelques mois : corriger « pour Jupiter dans cinq ans » d'un seul coup
  // n'aurait aucun sens opérationnel, et la matrice de transition sur cinq ans
  // n'aurait aucune validité.
  const double t_dep = tr.depart().t_days, t_arr = tr.t_nav_fin_days;
  const double tof_s = (t_arr - t_dep) * cst::DAY;
  d.t_tcm_days = TCM_ARC_DAYS < (t_arr - t_dep) ? TCM_ARC_DAYS : 0.5 * (t_arr - t_dep);
  const double t_c_s = d.t_tcm_days * cst::DAY;

  const StmBlocks Sdep_arr = kepler_stm(tr.r_dep, tr.v_dep, tof_s, cst::MU_SUN);
  const StmBlocks Sdep_c   = kepler_stm(tr.r_dep, tr.v_dep, t_c_s, cst::MU_SUN);
  if (!Sdep_arr.ok || !Sdep_c.ok) return d;
  const auto Kc = astro::kepler_propagate(tr.r_dep, tr.v_dep, t_c_s, cst::MU_SUN);
  if (!Kc.converged) return d;
  const StmBlocks Sc_arr = kepler_stm(Kc.r, Kc.v, tof_s - t_c_s, cst::MU_SUN);
  if (!Sc_arr.ok) return d;

  // Manque au but SANS correction : δr_arr = Φ_rv(arr←dep) δv.
  d.sigma_r_arr_km = s_axe * std::sqrt(Sdep_arr.rv.frob2() / 3.0) / 1000.0;

  // La correction qui ANNULE le manque au but :
  //   Δv_c = −Φ_rv(arr←c)⁻¹ [ Φ_rr(arr←c) Φ_rv(c←dep) + Φ_rv(arr←c) Φ_vv(c←dep) ] δv
  M3 inv_rv;
  if (!inverse3(Sc_arr.rv, inv_rv)) return d;
  M3 A;
  {
    const M3 t1 = inv_rv * (Sc_arr.rr * Sdep_c.rv);
    for (int i = 0; i < 3; ++i)
      for (int j = 0; j < 3; ++j)
        A.m[i][j] = -(t1.m[i][j] + Sdep_c.vv.m[i][j]);
  }
  // Covariance isotrope en entrée : Σ_corr = s_axe² A Aᵀ, dont on ne retient que
  // l'écart-type moyen par axe — le corridor est de toute façon dessiné rond.
  d.sigma_corr = s_axe * std::sqrt(A.frob2() / 3.0);
  d.dv_corr_p99 = MAXWELL_P99 * d.sigma_corr;

  // 5) LA QUESTION DE JEU, exacte : la marge couvre-t-elle la correction ?
  d.p_marge = maxwell_cdf(dv_margin_ms, d.sigma_corr);
  d.ok = true;
  return d;
}

// ═══ L'ERREUR RÉELLEMENT COMMISE ═══ [GDD 8.1, 8.2]
// Jusqu'ici la dispersion était STATISTIQUE : on savait ce qui POUVAIT arriver,
// rien n'arrivait. Ici l'injection est exécutée pour de bon — `nav::apply_gates`
// tire l'écart sur un SOUS-FLUX dédié de la graine de mission, donc rejouable, et
// ajouter d'autres sources d'aléa ailleurs ne décalera pas ce tirage.
//
// Ce que ça change : le manque au but n'est plus une loi mais un NOMBRE, et la
// question « la marge suffit-elle ? » cesse d'être une probabilité pour devenir
// un fait. Le joueur, lui, ne voit toujours pas cet écart [GDD 7.5] — il ne verra
// que ce que la poursuite lui en dira.
struct NavRealisation {
  bool   ok{false};
  double dv_inj_erreur{0.0};    // norme de l'écart d'exécution réellement commis
  double miss_arr_km{0.0};      // manque au but SI on ne corrige pas
  double dv_correction{0.0};    // Δv de la correction qu'il faut réellement faire
  Vec3   v_dep_vraie{};         // vitesse héliocentrique VRAIE au départ de l'arc
};

inline NavRealisation nav_realisation(const FlightTrace& tr, const NavDispersion& d,
                                      std::uint64_t mission_seed,
                                      const nav::GatesParams& gates = {}) {
  NavRealisation r;
  if (!tr.ok || !d.ok || tr.n_nodes < 2) return r;

  // Le tirage porte sur le Δv COMMANDÉ de l'injection, dans la direction de la
  // vitesse héliocentrique visée : c'est là que la manœuvre a lieu.
  Rng rng = Rng(mission_seed).substream(0x494E4A45'00000001ull);   // « INJE »
  const Vec3 dir = unit(tr.v_dep);
  const Vec3 dv_cmd = dir * d.dv_injection;
  const Vec3 dv_obtenu = nav::apply_gates(dv_cmd, gates, rng);
  const Vec3 err = dv_obtenu - dv_cmd;
  r.dv_inj_erreur = norm(err);
  // Même amplification d'Oberth que pour la statistique — une seule loi pour la
  // dispersion et pour sa réalisation, sinon les deux divergeraient en silence.
  r.v_dep_vraie = tr.v_dep + err * d.oberth_gain;

  const double t_dep = tr.depart().t_days, t_arr = tr.t_nav_fin_days;
  const double tof_s = (t_arr - t_dep) * cst::DAY;
  const double t_c_s = d.t_tcm_days * cst::DAY;

  // Manque au but SANS correction : on propage les DEUX états et on compare.
  // Pas de linéarisation ici — l'écart est trop grand pour qu'un premier ordre
  // soit honnête (1σ vaut déjà plus d'un million de km).
  const auto Knom = astro::kepler_propagate(tr.r_dep, tr.v_dep, tof_s, cst::MU_SUN);
  const auto Kvra = astro::kepler_propagate(tr.r_dep, r.v_dep_vraie, tof_s, cst::MU_SUN);
  if (!Knom.converged || !Kvra.converged) return r;
  r.miss_arr_km = norm(Kvra.r - Knom.r) / 1000.0;

  // LA CORRECTION RÉELLEMENT REQUISE à TCM-1 : celle qui ramène l'arrivée sur la
  // cible. Δv_c = −Φ_rv(arr←c)⁻¹ · [manque au but propagé depuis l'état vrai à c].
  const auto Kc = astro::kepler_propagate(tr.r_dep, r.v_dep_vraie, t_c_s, cst::MU_SUN);
  if (!Kc.converged) return r;
  const StmBlocks Sc_arr = kepler_stm(Kc.r, Kc.v, tof_s - t_c_s, cst::MU_SUN);
  if (!Sc_arr.ok) return r;
  const auto Kfin = astro::kepler_propagate(Kc.r, Kc.v, tof_s - t_c_s, cst::MU_SUN);
  if (!Kfin.converged) return r;
  M3 inv_rv;
  if (!inverse3(Sc_arr.rv, inv_rv)) return r;
  r.dv_correction = norm(inv_rv * (Kfin.r - Knom.r));
  r.ok = true;
  return r;
}

// ═══════════════════════════════════════════════════════════════════════════
// VISER UN SURVOL : LE PLAN-B [GDD 8.4, 8.5, 5.11]
// ═══════════════════════════════════════════════════════════════════════════
// `astro/BPlane.hpp` était le QUATRIÈME en-tête mort de la série des assistances,
// et son propre commentaire disait à quoi il devait servir : « on ne cible pas
// une orbite, on cible un point dans un plan perpendiculaire à l'asymptote
// d'arrivée ; l'ellipse de dispersion superposée au corridor admissible EST
// l'interface de la sanction ». Il n'avait aucun appelant parce que le jeu
// n'avait aucun survol. Il en a un.
//
// ═══ CE QU'UN SURVOL EXIGE, ET QUE PERSONNE N'EXIGEAIT ═══
// Un transfert direct arrive « à Jupiter » : rater de 100 000 km se corrige. Un
// SURVOL est un rendez-vous de précision — la déviation qu'il fournit dépend du
// périastre, et 100 km d'écart en paramètre d'impact valent 91 km d'altitude.
// En dessous de l'interface atmosphérique, le véhicule ne survole plus : il rentre
// à 9 km/s. Le corridor est donc BORNÉ PAR LA PHYSIQUE, pas par une règle.
//
// ═══ LA CASCADE, ET SON RÉSULTAT INATTENDU ═══
//   1. TCM-1 annule le manque au but prédit ; son coût est `d.sigma_corr`.
//   2. Son EXÉCUTION est imparfaite (Gates), et cette erreur-là se propage
//      jusqu'au survol : c'est le manque résiduel.
//   3. Une DERNIÈRE correction, quelques jours avant le survol, l'annule.
//   4. C'est SON erreur d'exécution qui décide — et elle, plus rien ne la corrige.
// MESURÉ : le résidu final est **insensible à la date de cette dernière
// correction** (111 km à E−3 j, 113 km à E−10 j, 123 km à E−30 j). La raison est
// structurelle : corriger plus tard coûte plus cher (bras de levier plus court),
// donc l'erreur d'exécution grandit dans la même proportion que le levier
// diminue. Le délai déclaré n'est donc PAS un réglage caché — c'est ce qui rend
// ce modèle publiable.
struct SurvolNav {
  bool   ok{false};
  double b_vise_m{0.0};          // paramètre d'impact visé
  double b_limite_m{0.0};        // celui qui amène à l'interface atmosphérique
  double demi_corridor_m{0.0};   // ce qu'on peut rater vers le bas
  double dv_derniere_corr{0.0};  // p99 de la dernière correction (m/s)
  double sigma_b_m{0.0};         // dispersion résiduelle dans le plan-B
  double p_survol{1.0};          // P(le survol reste dans le corridor)
};

// Délai de la DERNIÈRE correction avant un survol. Dix jours : c'est la pratique
// réelle (Galileo, Cassini et Juno ont tous placé leur dernier TCM entre E−10 j et
// E−3 j). Et la mesure ci-dessus montre que le résultat n'en dépend pas.
inline constexpr double TCM_FINALE_AVANT_SURVOL_J = 10.0;

inline SurvolNav nav_survol(const FlightTrace& tr, const NavDispersion& d,
                            int arc_du_survol, double rp_vise_m, double rp_limite_m,
                            double vinf_fb_ms, double mu_corps,
                            double lead_days = TCM_FINALE_AVANT_SURVOL_J,
                            const nav::GatesParams& gates = {}) {
  SurvolNav s;
  if (!tr.ok || !d.ok || arc_du_survol <= 0 || arc_du_survol >= tr.n_arcs) return s;
  if (!(vinf_fb_ms > 0.0) || !(rp_vise_m > rp_limite_m)) return s;

  // 1) LE CORRIDOR, en paramètre d'impact — la conversion exacte de `BPlane.hpp`.
  s.b_vise_m   = astro::b_from_rp(rp_vise_m, vinf_fb_ms, mu_corps);
  s.b_limite_m = astro::b_from_rp(rp_limite_m, vinf_fb_ms, mu_corps);
  s.demi_corridor_m = s.b_vise_m - s.b_limite_m;
  if (!(s.demi_corridor_m > 0.0)) return s;

  // 2) CE QUE TCM-1 LAISSE AU SURVOL : son erreur d'exécution, propagée.
  const double sig_e1 =
      nav::gates_sigma_total(MAXWELL_P99 * d.sigma_corr, gates) / std::sqrt(3.0);
  double t_vers_survol = 0.0;
  for (int a = 0; a <= arc_du_survol; ++a) t_vers_survol += tr.arcs[a].dt_days;
  const double dt1 = (t_vers_survol - d.t_tcm_days) * cst::DAY;
  if (dt1 <= 0.0) return s;
  const StmBlocks S1 = kepler_stm(tr.arcs[0].r0, tr.arcs[0].v0, dt1, cst::MU_SUN);
  if (!S1.ok) return s;
  const double miss1 = sig_e1 * std::sqrt(S1.rv.frob2() / 3.0);

  // 3) LA DERNIÈRE CORRECTION, et 4) ce qu'ELLE laisse.
  const StmBlocks S2 = kepler_stm(tr.arcs[arc_du_survol].r0, tr.arcs[arc_du_survol].v0,
                                  lead_days * cst::DAY, cst::MU_SUN);
  if (!S2.ok) return s;
  const double levier = std::sqrt(S2.rv.frob2() / 3.0);   // m de manque par m/s
  if (!(levier > 0.0)) return s;
  const double sig_c2 = miss1 / levier;
  s.dv_derniere_corr = MAXWELL_P99 * sig_c2;
  const double sig_e2 = nav::gates_sigma_total(s.dv_derniere_corr, gates) / std::sqrt(3.0);
  s.sigma_b_m = sig_e2 * levier;
  if (!(s.sigma_b_m > 0.0)) return s;

  // 5) LA LOI EST CELLE DU PLAN-B, et c'est pour ça qu'on y travaille : le manque
  // au but y est un vecteur à DEUX composantes (B·T, B·R), donc sa norme suit une
  // loi de RAYLEIGH — pas Maxwell (3D), qui vaut pour un Δv.
  const double z = s.demi_corridor_m / s.sigma_b_m;
  s.p_survol = 1.0 - std::exp(-0.5 * z * z);
  s.ok = true;
  return s;
}

// Le corridor 3σ à un instant donné : la dispersion de POSITION autour de l'arc
// nominal, propagée depuis l'erreur d'injection. C'est ce que le joueur ne sait
// PAS de sa position [GDD 7.5, 8.3]. Il CROÎT tant que rien ne le rétrécit — et
// rien ne le rétrécit encore : la poursuite (achat de passes, détermination
// d'orbite) est la brique suivante, et c'est DÉCLARÉ plutôt que simulé.
inline double corridor_3sigma_m(const FlightTrace& tr, const NavDispersion& d,
                                double now_days) {
  if (!tr.ok || !d.ok || tr.n_nodes < 2) return 0.0;
  const double t_dep = tr.depart().t_days;
  // LE CORRIDOR NE CROÎT QUE JUSQU'À LA PREMIÈRE VISÉE. Au-delà, la manœuvre qui
  // l'attend le remet à zéro par définition (c'est ce qu'une correction FAIT), et
  // propager la matrice de transition sur cinq ans de tour ne voudrait rien dire.
  const double dt_s = (std::min(now_days, tr.t_nav_fin_days) - t_dep) * cst::DAY;
  if (dt_s <= 0.0) return 0.0;
  const StmBlocks S = kepler_stm(tr.r_dep, tr.v_dep, dt_s, cst::MU_SUN);
  if (!S.ok) return 0.0;
  const double s_axe = d.sigma_vinf / std::sqrt(3.0);
  return 3.0 * s_axe * std::sqrt(S.rv.frob2() / 3.0);
}

} // namespace fen::mission
