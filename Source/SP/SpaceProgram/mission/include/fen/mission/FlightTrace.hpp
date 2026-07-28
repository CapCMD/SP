// fen/mission/FlightTrace.hpp — LA TRACE DU VOL DANS LE MONDE [GDD 8.1, 8.3, 17.3]
//
// La chronologie (FlightTimeline.hpp) dit QUAND ; ce fichier dit OÙ. Le monde
// d'ARES est une scène unique à l'échelle 1:1 où « tous les corps, stations et
// véhicules actifs coexistent, à leur position réelle » [GDD 17.3] : un vaisseau
// en croisière doit donc ÊTRE quelque part, et sa trajectoire doit se voir « dans
// le monde, à sa position réelle » [GDD 8.3].
//
// ═══ L'ARC EST CALCULÉ, PAS DESSINÉ ═══
// La trajectoire nominale est la solution de LAMBERT entre la position réelle du
// corps de départ à l'instant de l'injection et celle du corps d'arrivée à
// l'instant de l'insertion — les deux dates venant de la chronologie, les deux
// positions de l'éphéméride. La polyligne est ensuite obtenue en PROPAGEANT cet
// état par Kepler, pas en interpolant entre deux points : ce qui est tracé est
// donc la trajectoire, et la position du vaisseau à l'instant t est un point DE
// cette trajectoire, par construction.
//
// ═══ CE QUI N'EST PAS ENCORE LÀ, ET POURQUOI ═══ [GDD 6.8]
// [GDD 7.5] veut que le joueur ne voie jamais une position vraie mais une
// SOLUTION DE NAVIGATION avec son incertitude. À ce stade, le vaisseau suit son
// arc nominal PAR CONSTRUCTION : l'issue du vol est encore un tirage à
// l'arrivée (`fly_mission`), aucun écart d'exécution n'est simulé en route. La
// position publiée est donc la nominale, et le corridor est NUL — ce n'est pas
// une fuite de vérité, c'est une absence d'écart. Corridor et estimé divergeront
// quand le vol manuel introduira l'erreur d'exécution et la poursuite (nav/).
//
// APPROXIMATION DÉCLARÉE [GDD 6.8] : coniques raccordées. L'arc est képlérien
// autour du Soleil, comme le porkchop qui a choisi la fenêtre — la même
// approximation, donc la même erreur, déjà FACTURÉE en marge de correction de
// mi-parcours dans le budget Δv. Les phases proches de la Terre (ascension,
// parking, rendez-vous LEO, mise à poste GEO) n'ont PAS de trace ici : à
// l'échelle du système elles tiennent dans le pixel de la Terre, et ce qui n'est
// pas séparable ne doit pas être désignable (piège n°41).
//
// C++ pur, aucune dépendance UE.
#pragma once
#include <cmath>

#include "fen/astro/Kepler.hpp"
#include "fen/astro/Lambert.hpp"
#include "fen/core/Constants.hpp"
#include "fen/core/Epoch.hpp"
#include "fen/core/Vec3.hpp"
#include "fen/ephem/Ephemeris.hpp"
#include "fen/mission/FlightTimeline.hpp"
#include "fen/mission/MissionLoop.hpp"

namespace fen::mission {

// Nombre de points de la polyligne. Même ordre de grandeur que les orbites des
// corps (512) : vu de près, un polygone se voit.
inline constexpr int TRACE_MAX_PTS = 512;

// Un nœud de manœuvre : un instant de la chronologie où quelque chose se fait.
struct TraceNode {
  Vec3   pos{};              // position héliocentrique (m)
  double t_days{0.0};        // date absolue (jours de jeu)
  bool   done{false};        // déjà franchi
};

struct FlightTrace {
  bool   ok{false};          // un arc héliocentrique existe et a été résolu
  int    n{0};
  Vec3   traj[TRACE_MAX_PTS];   // trajectoire NOMINALE, héliocentrique écliptique
  Vec3   pos{};                 // position à l'époque demandée
  bool   sur_arc{false};        // le vaisseau est entre l'injection et l'arrivée
  double corridor_3s_m{0.0};    // nul tant que rien ne fait diverger le vol
  int    n_nodes{0};
  TraceNode nodes[2];           // injection (départ de l'arc), arrivée
  // L'ÉTAT DE DÉPART, gardé : c'est lui qui rend la position réévaluable à
  // n'importe quel instant pour UNE propagation. Sans lui il faudrait relire le
  // point d'échantillon le plus proche — et à 512 points sur 329 jours, un
  // échantillon fait 0,64 jour, soit plus d'un million de km d'écart. Un arc
  // tracé finement mais parcouru par sauts serait pire que pas d'arc du tout.
  Vec3   r_dep{}, v_dep{};
};

// LE VOL A-T-IL UN ARC HÉLIOCENTRIQUE À TRACER ? Prédicat SÉPARÉ de la
// signature ci-dessous, et ce n'est pas de la coquetterie : la signature est
// bâtie sur la date du feu vert, qui peut parfaitement être NÉGATIVE (un vol
// lancé avant l'origine du calendrier, ce que fait toute capture épinglant une
// croisière). Un sentinelle pris DANS le domaine de la valeur aurait alors
// rejeté un vol parfaitement valide — et c'est exactement ce qui est arrivé
// (piège n°61).
inline bool flight_has_arc(const Mission& m) {
  if (m.state != MissionState::Launched) return false;
  if (m.tof_days <= 0.0) return false;
  return window_target_for_family(m.contract.family).impose;
}

// SIGNATURE DE L'ARC : ce dont il dépend ENTIÈREMENT. L'arc est figé au feu vert
// (date + durée de transit + destination) ; tant que ces trois-là ne bougent
// pas, la polyligne n'a aucune raison d'être recalculée. Une résolution de
// Lambert plus 512 propagations par frame serait un gaspillage pur — et le pont
// prévoit déjà `last_arc_sig` pour ça. Aucune valeur n'est réservée : c'est
// `flight_has_arc` qui dit s'il y a un arc.
inline double flight_trace_signature(const Mission& m) {
  const WindowTarget wt = window_target_for_family(m.contract.family);
  return m.state_entered_days * 1000.0 + m.tof_days +
         static_cast<double>(wt.arr) * 1.0e-3;
}

// AVANCE la trace à une date, SANS refaire l'arc : une propagation de Kepler
// depuis l'état de départ, pas une relecture du point d'échantillon le plus
// proche. C'est ce qu'on appelle chaque frame ; la reconstruction complète
// (Lambert + 512 propagations) n'a lieu que si la signature change.
// Avant l'injection le vaisseau est encore à la Terre, après l'arrivée il est à
// destination : dans les deux cas il est DANS le pixel de son corps.
inline void trace_avancer(FlightTrace& tr, double now_days) {
  if (!tr.ok || tr.n_nodes < 2 || tr.n < 2) return;
  const double t_dep = tr.nodes[0].t_days, t_arr = tr.nodes[1].t_days;
  tr.nodes[0].done = now_days >= t_dep;
  tr.nodes[1].done = now_days >= t_arr;
  if (now_days <= t_dep)      { tr.pos = tr.traj[0];          tr.sur_arc = false; return; }
  if (now_days >= t_arr)      { tr.pos = tr.traj[tr.n - 1];   tr.sur_arc = false; return; }
  const astro::KeplerResult K = astro::kepler_propagate(
      tr.r_dep, tr.v_dep, (now_days - t_dep) * cst::DAY, cst::MU_SUN);
  tr.pos = K.r;
  tr.sur_arc = true;
}

// Construit la trace. `now_days` / `now_tdb` donnent la CORRESPONDANCE entre le
// calendrier de jeu et l'échelle des éphémérides : le calendrier et le TDB
// avancent du même pas, une date de jeu se convertit donc linéairement.
inline FlightTrace build_flight_trace(const Mission& m, double now_days,
                                      double now_tdb, const ephem::IEphemeris& eph) {
  FlightTrace tr;
  if (!flight_has_arc(m)) return tr;

  const WindowTarget wt = window_target_for_family(m.contract.family);
  const FlightTimeline tl = build_flight_timeline(m);

  // Les bornes de l'arc se LISENT dans la chronologie : on ne les redérive pas
  // ici, sinon deux endroits du moteur dateraient le même vol. L'arc va de la
  // fin de l'INJECTION au début de la manœuvre d'ARRIVÉE — la croisière est
  // coupée entre les deux par ses corrections de mi-parcours, et c'est
  // précisément pourquoi on ne peut pas se contenter d'un « segment de
  // croisière » (il y en a trois).
  if (!tl.dated || tl.n < 3) return tr;
  int i_inj = -1;
  for (int i = 0; i < tl.n; ++i)
    if (tl.seg[i].phase == FlightPhase::CriticalManeuver) { i_inj = i; break; }
  if (i_inj < 0) return tr;
  const int i_arr = tl.n - 2;    // dernier segment FERMÉ : la manœuvre d'arrivée
  if (i_arr <= i_inj) return tr;

  const double t_dep_days = tl.t_go_days + tl.seg[i_inj].t1_days;
  const double t_arr_days = tl.t_go_days + tl.seg[i_arr].t0_days;
  const double tof_s = (t_arr_days - t_dep_days) * cst::DAY;
  if (tof_s <= 0.0) return tr;

  auto tdb_de = [&](double jours) { return now_tdb + (jours - now_days) * cst::DAY; };

  const Vec3 r1 = eph.state(ephem::Body::EarthBary, ephem::Body::Sun,
                            Epoch{tdb_de(t_dep_days)}).r;
  const Vec3 r2 = eph.state(wt.arr, ephem::Body::Sun, Epoch{tdb_de(t_arr_days)}).r;

  const astro::LambertResult L = astro::lambert(r1, r2, tof_s, cst::MU_SUN);
  if (!L.ok || L.solutions.empty()) return tr;   // pas d'arc : rien à tracer
  const Vec3 v1 = L.solutions[0].v1;

  // POLYLIGNE PAR PROPAGATION, jamais par interpolation : chaque point est un
  // état réel de la trajectoire, donc la position du vaisseau à l'instant t est
  // un point DE la courbe tracée, et non un objet posé à côté d'elle.
  tr.n = TRACE_MAX_PTS;
  for (int k = 0; k < tr.n; ++k) {
    const double dt = tof_s * static_cast<double>(k) / static_cast<double>(tr.n - 1);
    const astro::KeplerResult K = astro::kepler_propagate(r1, v1, dt, cst::MU_SUN);
    tr.traj[k] = K.r;
  }

  tr.r_dep = r1;
  tr.v_dep = v1;

  // LES NŒUDS SONT LES MANŒUVRES DE LA CHRONOLOGIE, pas des repères décoratifs :
  // l'injection (début de l'arc) et l'arrivée (insertion ou EDL). « Fait » se
  // LIT de la date, il ne se coche pas.
  tr.nodes[0] = {r1, t_dep_days, now_days >= t_dep_days};
  tr.nodes[1] = {tr.traj[tr.n - 1], t_arr_days, now_days >= t_arr_days};
  tr.n_nodes = 2;

  tr.ok = true;
  trace_avancer(tr, now_days);   // pose `pos` et `sur_arc`
  return tr;
}

} // namespace fen::mission
