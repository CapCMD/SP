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
#include <vector>

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

// UN MORCEAU DE TRAJECTOIRE PROPAGEABLE. Un transfert direct en a UN ; un tour
// d'assistance en a deux par jambe (dérive vers la manœuvre profonde, puis arc
// vers le corps suivant). La trace ne connaît que ça : elle propage.
struct TraceArc {
  Vec3   r0{}, v0{};
  double t0_days{0.0}, dt_days{0.0};
};

struct FlightTrace {
  bool   ok{false};          // un arc héliocentrique existe et a été résolu
  int    n{0};
  // ⚠ LA POLYLIGNE EST SUR LE TAS, ET CE N'EST PAS UN DÉTAIL (piège n°95).
  // C'était un tableau fixe de 512 Vec3, soit **12 Ko dans la structure**. Or
  // `FlightTrace` traverse toute la chaîne de navigation PAR VALEUR, et
  // `ContexteVol` en contient un : la campagne de correction en tient TROIS
  // vivants dans une même frame (courant, TCM-1, TCM-2), chacun avec le sien.
  // La pile de 1 Mo y passait déjà de justesse — ajouter les morceaux d'un tour
  // (un kilo-octet) l'a fait DÉBORDER, et le symptôme était une suite d'oracles
  // qui s'arrêtait net au milieu, code de sortie 0xC00000FD. La polyligne est un
  // artefact de RENDU : elle n'a rien à faire dans le chemin qui calcule une
  // correction, et encore moins sur la pile.
  std::vector<Vec3> traj;       // trajectoire NOMINALE, héliocentrique écliptique
  Vec3   pos{};                 // position à l'époque demandée
  // LA VITESSE À CETTE MÊME DATE — publiée pour la MÊME raison que celle de
  // Novellus : elle donne l'axe du vaisseau au rendu [GDD 17.2], et le rendu ne
  // dérive rien. La deviner en différenciant deux frames serait faux dès la
  // cadence « mois/s », où une frame avance de douze heures. Kepler la rend en
  // même temps que la position : elle ne coûte rien de plus.
  Vec3   vel{};                 // vitesse héliocentrique à l'époque demandée
  bool   sur_arc{false};        // le vaisseau est entre l'injection et l'arrivée
  double corridor_3s_m{0.0};    // nul tant que rien ne fait diverger le vol
  // LES NŒUDS SONT LES MANŒUVRES : injection, survols et manœuvres profondes d'un
  // tour, arrivée. Le PREMIER est toujours le départ, le DERNIER toujours
  // l'arrivée — `depart()` et `arrivee()` le disent au lieu de le supposer, parce
  // qu'un tour en met quatre entre les deux.
  static constexpr int MAX_NODES = 8;
  int    n_nodes{0};
  TraceNode nodes[MAX_NODES];
  const TraceNode& depart()  const { return nodes[0]; }
  const TraceNode& arrivee() const { return nodes[n_nodes > 0 ? n_nodes - 1 : 0]; }
  // L'ÉTAT DE DÉPART, gardé : c'est lui qui rend la position réévaluable à
  // n'importe quel instant pour UNE propagation. Sans lui il faudrait relire le
  // point d'échantillon le plus proche — et à 512 points sur 329 jours, un
  // échantillon fait 0,64 jour, soit plus d'un million de km d'écart. Un arc
  // tracé finement mais parcouru par sauts serait pire que pas d'arc du tout.
  Vec3   r_dep{}, v_dep{};
  // LES MORCEAUX, dans l'ordre. Un transfert direct en a un seul, et il vaut
  // exactement (r_dep, v_dep) — donc rien ne change pour lui.
  static constexpr int MAX_ARCS = 12;
  int      n_arcs{0};
  TraceArc arcs[MAX_ARCS];
  // ═══ L'ARC SUR LEQUEL SE JUGE LA NAVIGATION ═══ [GDD 8.4]
  // C'est le PREMIER morceau : celui que l'erreur d'injection déforme, et dont
  // l'écart se paie à la première visée. Pour un transfert direct, ce premier
  // morceau EST tout le vol, donc rien ne bouge. Pour un tour, la première visée
  // est la manœuvre profonde de la première jambe — pas Jupiter dans cinq ans, ce
  // qui n'aurait aucun sens à corriger d'un coup.
  double t_nav_fin_days{0.0};
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
  const double t_dep = tr.depart().t_days, t_arr = tr.arrivee().t_days;
  for (int k = 0; k < tr.n_nodes; ++k) tr.nodes[k].done = now_days >= tr.nodes[k].t_days;
  if (now_days <= t_dep)      { tr.pos = tr.traj[0];          tr.sur_arc = false; return; }
  if (now_days >= t_arr)      { tr.pos = tr.traj[tr.n - 1];   tr.sur_arc = false; return; }
  // ON PROPAGE DEPUIS LE MORCEAU COURANT, jamais depuis le premier : après une
  // manœuvre profonde ou un survol, l'état a changé — propager le départ sur cinq
  // ans donnerait un vaisseau qui n'est plus sur sa propre trajectoire.
  for (int a = tr.n_arcs - 1; a >= 0; --a) {
    if (now_days + 1e-12 < tr.arcs[a].t0_days && a > 0) continue;
    const astro::KeplerResult K = astro::kepler_propagate(
        tr.arcs[a].r0, tr.arcs[a].v0, (now_days - tr.arcs[a].t0_days) * cst::DAY,
        cst::MU_SUN);
    tr.pos = K.r;
    tr.vel = K.v;
    tr.sur_arc = true;
    return;
  }
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

  // ═══ CAS 1 : LE VOL SUIT UN TOUR D'ASSISTANCE ═══ [GDD 5.11, 8.3]
  // Les morceaux sont ceux que l'optimiseur a RÉELLEMENT parcourus, figés sur la
  // mission au feu vert. On ne re-résout rien ici — un tour se recalculerait
  // différemment à chaque date, et un vol parti ne change pas de trajectoire.
  if (!m.tour_arcs.empty()) {
    const int na = static_cast<int>(m.tour_arcs.size());
    tr.n_arcs = na < FlightTrace::MAX_ARCS ? na : FlightTrace::MAX_ARCS;
    double duree_totale_s = 0.0;
    for (int a = 0; a < tr.n_arcs; ++a) {
      const Mission::TourArc& s = m.tour_arcs[static_cast<std::size_t>(a)];
      tr.arcs[a].r0 = Vec3{s.r0[0], s.r0[1], s.r0[2]};
      tr.arcs[a].v0 = Vec3{s.v0[0], s.v0[1], s.v0[2]};
      // Les dates du tour sont ABSOLUES (s TDB) ; le calendrier de jeu avance du
      // même pas, la conversion est donc linéaire — la même que partout ailleurs.
      tr.arcs[a].t0_days  = now_days + (s.t0_tdb - now_tdb) / cst::DAY;
      tr.arcs[a].dt_days  = s.dt_s / cst::DAY;
      duree_totale_s += s.dt_s;
    }
    if (duree_totale_s <= 0.0) return tr;
    // POLYLIGNE : chaque morceau reçoit sa part de points, au prorata de sa durée.
    // Un morceau court (une dérive de trois semaines) ne mérite pas autant de
    // points qu'une croisière de trois ans, et une répartition égale ferait un
    // tracé grossier là où la courbure est forte.
    int k = 0;
    tr.traj.resize(TRACE_MAX_PTS);
    for (int a = 0; a < tr.n_arcs && k < TRACE_MAX_PTS; ++a) {
      const double part = tr.arcs[a].dt_days * cst::DAY / duree_totale_s;
      int npts = static_cast<int>(part * (TRACE_MAX_PTS - tr.n_arcs)) + 1;
      if (a == tr.n_arcs - 1) npts = TRACE_MAX_PTS - k;   // le dernier ferme le tracé
      for (int j = 0; j < npts && k < TRACE_MAX_PTS; ++j, ++k) {
        const double f = (npts > 1) ? static_cast<double>(j) / (npts - 1) : 0.0;
        const astro::KeplerResult K = astro::kepler_propagate(
            tr.arcs[a].r0, tr.arcs[a].v0, f * tr.arcs[a].dt_days * cst::DAY,
            cst::MU_SUN);
        tr.traj[k] = K.r;
      }
    }
    tr.n = k;
    tr.traj.resize(static_cast<std::size_t>(tr.n));
    tr.r_dep = tr.arcs[0].r0;
    tr.v_dep = tr.arcs[0].v0;
    // LES NŒUDS : le départ, puis la FIN de chaque morceau. Un tour en a donc
    // autant que de manœuvres profondes et de survols, plus l'arrivée — ce sont
    // exactement les instants où quelque chose se passe [GDD 8.3].
    tr.nodes[0] = {tr.arcs[0].r0, tr.arcs[0].t0_days, false};
    tr.n_nodes = 1;
    for (int a = 0; a < tr.n_arcs && tr.n_nodes < FlightTrace::MAX_NODES; ++a) {
      const astro::KeplerResult K = astro::kepler_propagate(
          tr.arcs[a].r0, tr.arcs[a].v0, tr.arcs[a].dt_days * cst::DAY, cst::MU_SUN);
      tr.nodes[tr.n_nodes] = {K.r, tr.arcs[a].t0_days + tr.arcs[a].dt_days, false};
      ++tr.n_nodes;
    }
    tr.t_nav_fin_days = tr.arcs[0].t0_days + tr.arcs[0].dt_days;
    tr.ok = true;
    trace_avancer(tr, now_days);
    return tr;
  }

  // ═══ CAS 2 : LE TRANSFERT DIRECT ═══ un seul arc, résolu par Lambert.
  const astro::LambertResult L = astro::lambert(r1, r2, tof_s, cst::MU_SUN);
  if (!L.ok || L.solutions.empty()) return tr;   // pas d'arc : rien à tracer
  const Vec3 v1 = L.solutions[0].v1;

  // POLYLIGNE PAR PROPAGATION, jamais par interpolation : chaque point est un
  // état réel de la trajectoire, donc la position du vaisseau à l'instant t est
  // un point DE la courbe tracée, et non un objet posé à côté d'elle.
  tr.n = TRACE_MAX_PTS;
  tr.traj.resize(static_cast<std::size_t>(tr.n));
  for (int k = 0; k < tr.n; ++k) {
    const double dt = tof_s * static_cast<double>(k) / static_cast<double>(tr.n - 1);
    const astro::KeplerResult K = astro::kepler_propagate(r1, v1, dt, cst::MU_SUN);
    tr.traj[k] = K.r;
  }

  tr.r_dep = r1;
  tr.v_dep = v1;
  tr.n_arcs = 1;
  tr.arcs[0] = {r1, v1, t_dep_days, t_arr_days - t_dep_days};

  // LES NŒUDS SONT LES MANŒUVRES DE LA CHRONOLOGIE, pas des repères décoratifs :
  // l'injection (début de l'arc) et l'arrivée (insertion ou EDL). « Fait » se
  // LIT de la date, il ne se coche pas.
  tr.nodes[0] = {r1, t_dep_days, now_days >= t_dep_days};
  tr.nodes[1] = {tr.traj[tr.n - 1], t_arr_days, now_days >= t_arr_days};
  tr.n_nodes = 2;
  tr.t_nav_fin_days = t_arr_days;   // un vol direct vise sa cible dès l'injection

  tr.ok = true;
  trace_avancer(tr, now_days);   // pose `pos` et `sur_arc`
  return tr;
}

} // namespace fen::mission
