// fen/mission/MissionLoop.hpp — LA BOUCLE DE MISSION [GDD 4.1]
//
// Le cycle qui RELIE les systèmes : réception → prérequis → conception →
// fenêtre → qualification → lancement → exploitation → débrief. La FSM
// (MissionFsm.hpp) porte les états ; ce fichier porte les GATES (chaque
// transition a une condition physique/programmatique réelle) et l'ISSUE du vol.
//
// DOCTRINE : aucune transition n'est gratuite. On ne passe en conception que si
// les prérequis sont là ; on ne cherche une fenêtre que si la conception est
// VIABLE (masse/budget/calendrier/risque, via mission::assess) ; on ne lance
// que qualifié ; et l'issue du vol est DÉTERMINISTE, tirée contre la P(succès)
// évaluée — « les pannes suivent des probabilités calibrées » [GDD 7.3, 9.4].
// Rien n'est un coup de dé nu : la graine vient de l'agence, la probabilité de
// la physique et de l'argent.
//
// C++ pur. Ne connaît PAS GameState (qui l'inclut) : l'issue produit un
// AnomalyEvent que l'appelant passe à GameState::apply_anomaly [GDD 10.4].
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>

#include "fen/astro/LaunchWindow.hpp"
#include "fen/astro/Transfers.hpp"
#include "fen/core/Constants.hpp"
#include "fen/core/Rng.hpp"
#include "fen/mission/FlightTimeline.hpp"
#include "fen/mission/MissionFsm.hpp"
#include "fen/mission/Program.hpp"
#include "fen/mission/Severity.hpp"

namespace fen::mission {

// ═══ LE Δv DE TRAJECTOIRE PAR TYPE DE MISSION ═══
// Budgets de trajectoire (m/s) depuis une orbite de parking, ordres de grandeur
// RÉELS et DÉCLARÉS [GDD 6.8]. Ce n'est pas le Δv de mise en orbite (le lanceur
// s'en charge) mais celui que le VÉHICULE doit fournir ensuite.
inline double trajectory_dv_for_family(const std::string& family) {
  if (family == "sat")          return 1800.0;   // LEO -> GEO (GTO + circularisation)
  if (family == "science")      return 3600.0;   // echappee + corrections
  if (family == "surface")      return 4300.0;   // interplanetaire + insertion/EDL
  if (family == "logistique")   return 200.0;    // rendez-vous LEO
  if (family == "service")      return 400.0;    // inspection/maintenance orbitale
  if (family == "habite")       return 300.0;    // LEO habite (rejoindre la station)
  if (family == "mars")         return 4800.0;   // orbiteur martien
  if (family == "mars_habite")  return 6000.0;   // aller habite + insertion
  if (family == "nep")          return 9000.0;   // cargo lointain (poussee continue)
  if (family == "relativiste")  return 30000.0;  // fin de jeu
  return 3000.0;                                  // defaut prudent
}

// Nombre d'allumages typiques (ignitions) — module la fiabilité moteur.
inline int burns_for_family(const std::string& family) {
  if (family == "sat") return 2;                  // injection + circularisation
  if (family == "logistique" || family == "service" || family == "habite") return 3;
  if (family == "surface" || family == "mars" || family == "mars_habite") return 4;
  return 2;
}

// ═══ LE PLAN DE MISSION ═══ — les décisions de conception + leur évaluation.
struct MissionPlan {
  Program    program;            // moteur, lanceur, essais, poursuite, revue, marge
  int        n_stages{2};
  double     finite_loss{150.0}; // pertes de poussée finie (m/s), provisionnées
  double     p_physics{0.985};   // fidélité de navigation (issue du MC, simplifiée)
  double     dv_traj_override{0.0}; // >0 : Δv de trajectoire imposé (fenêtre réelle)
  Assessment assessment;
  bool       evaluated{false};

  // Évalue le plan contre le contrat de la mission : c'est le GATE de conception.
  // `dv_traj_override` (posé par le driver depuis la géométrie de la fenêtre)
  // prime sur le forfait par famille — c'est ce qui rend le budget Mars réel.
  void evaluate(const Mission& m) {
    const double dv = dv_traj_override > 0.0 ? dv_traj_override
                                             : trajectory_dv_for_family(m.contract.family);
    const int burns = burns_for_family(m.contract.family);
    assessment = assess_multistage(m.contract.terms, program, burns, dv, finite_loss, n_stages);
    finalize(assessment, m.contract.terms, p_physics);
    evaluated = true;
  }
};

// ═══ LE GATE D'UNE TRANSITION ═══ — légalité FSM + condition réelle.
struct GateResult {
  bool        allowed{false};
  std::string reason;           // le POURQUOI d'un refus, affichable
};

inline GateResult mission_gate(const Mission& m, const MissionPlan& plan,
                               MissionState target) {
  if (!m.can_advance_to(target))
    return {false, "transition illegale a cette phase"};
  switch (target) {
    case MissionState::Prerequisites:
    case MissionState::Design:
      return {true, ""};                          // toujours autorisé de concevoir
    case MissionState::WindowSearch:
      if (!plan.evaluated)       return {false, "conception non evaluee"};
      if (!plan.assessment.ok)   return {false, std::string("conception non viable : ") + plan.assessment.why};
      return {true, ""};
    case MissionState::Qualification:
      // Fenêtre de lancement. La condition PROGRAMMATIQUE est toujours ouverte
      // ici ; la condition GÉOMÉTRIQUE réelle (positions des corps) est portée
      // par `launch_window_gate` ci-dessous, que le driver applique EN PLUS à
      // cette transition — car elle exige l'éphéméride, absente de cette
      // signature pure.
      return {true, ""};
    case MissionState::Launched:
      // Qualification : une revue indépendante OU des essais à feu. Sans l'un
      // des deux, on ne signe pas le feu vert [GDD 12.3].
      if (!(plan.program.review || plan.program.test_hours > 0.0))
        return {false, "qualification requise : revue ou essais a feu"};
      return {true, ""};
    case MissionState::Debrief:
      return {true, ""};                          // le vol s'exécute
    default:
      return {true, ""};
  }
}

// ═══ LE GATE DE FENÊTRE DE LANCEMENT ═══ [GDD 7.3]
// Séparé de `mission_gate` : il exige l'éphéméride (positions réelles des
// corps), que tout appelant du cœur n'a pas sous la main. Le driver l'applique
// à la transition WindowSearch -> Qualification, EN PLUS du gate programmatique.
//
// Familles à fenêtre synodique RÉELLE : transferts impulsifs vers Mars
// (mars / mars_habite / surface = rover, retour d'échantillons). Les autres
// gardent une fenêtre permanente en V1, pour des raisons PHYSIQUES, pas par
// paresse : near-Earth (sat/logistique/service/habité) le sont vraiment ; NEP
// est à poussée continue (pas de fenêtre impulsive étroite) ; « science » n'a
// pas de destination nommée par le contrat ; « relativiste » est un régime de
// fin de jeu. DÉCLARÉ, et extensible dès qu'un contrat nomme sa cible.
struct WindowTarget { bool impose{false}; ephem::Body dep{}; ephem::Body arr{}; };

inline WindowTarget window_target_for_family(const std::string& family) {
  if (family == "mars" || family == "mars_habite" || family == "surface")
    return {true, ephem::Body::EarthBary, ephem::Body::Mars};
  return {false, {}, {}};
}

// `now` : l'époque courante (s TDB, via Epoch). L'issue est un GateResult : si
// la fenêtre est fermée, `reason` chiffre l'attente — « rater = 25.6 mois ».
inline GateResult launch_window_gate(const Mission& m, Epoch now,
                                     const ephem::IEphemeris& eph,
                                     const astro::WindowParams& params = {}) {
  const WindowTarget wt = window_target_for_family(m.contract.family);
  if (!wt.impose) return {true, ""};   // fenêtre permanente (voir supra)

  const astro::WindowResult w = astro::launch_window(eph, wt.dep, wt.arr, now, params);
  if (!w.ok) return {false, "fenetre : aucune solution de transfert calculable"};
  if (w.open) return {true, ""};

  GateResult r;
  r.allowed = false;
  char buf[112];
  std::snprintf(buf, sizeof buf,
                "fenetre de lancement fermee : prochaine dans %.0f jours",
                w.next_open_days);
  r.reason = buf;
  return r;
}

// Δv DE TRAJECTOIRE RÉEL d'une mission [GDD 6.8, 7.3] — plus d'arcade pour Mars.
// Pour une famille à fenêtre synodique, on le tire de la GÉOMÉTRIE de la fenêtre
// courante : injection hyperbolique (Oberth) depuis une orbite de parking LEO +
// insertion elliptique à Mars + une marge de mi-parcours DÉCLARÉE. Le coût
// devient donc SENSIBLE à la qualité de la fenêtre. Familles sans fenêtre
// imposée : on garde le forfait par famille (identique à avant).
inline double trajectory_dv_for_mission(const Mission& m, Epoch now,
                                        const ephem::IEphemeris& eph) {
  const WindowTarget wt = window_target_for_family(m.contract.family);
  if (!wt.impose) return trajectory_dv_for_family(m.contract.family);

  const astro::WindowResult w = astro::launch_window(eph, wt.dep, wt.arr, now);
  if (!w.ok) return trajectory_dv_for_family(m.contract.family);   // repli prudent

  // Le VÉHICULE part d'une orbite de parking (le lanceur l'y a mis) : il paie
  // l'injection réduite par Oberth, pas le v_inf nu. MÊME orbite de parking que
  // celle dont la chronologie de vol tire la durée d'attente avant injection
  // (FlightTimeline.hpp) : un chiffre, une source.
  const double injection = astro::injection_dv_from_circular(
      w.vinf_dep, parking_radius_m(), cst::MU_EARTH);
  // Insertion à Mars : capture elliptique (rp bas, ra très haut) — le choix réel,
  // bien moins cher qu'une circularisation basse.
  const double insertion = astro::capture_dv_to_ellipse(
      w.vinf_arr, cst::R_MARS + 400.0e3, cst::R_MARS + 30000.0e3, cst::MU_MARS);
  const double midcourse = 150.0;   // corrections de mi-parcours [7.5], DÉCLARÉ
  return injection + insertion + midcourse;
}

// LA DURÉE DE TRANSIT VISÉE, à capturer AU FEU VERT [GDD 7.3, 9]. C'est ce qui
// DATE l'arrivée, donc l'insertion et l'EDL — les phases critiques que le moteur
// savait modéliser sans savoir quand elles ont lieu. On prend la durée du
// meilleur transfert de la fenêtre COURANTE (`local_tof_days`), pas celle de
// l'optimum synodique : le vol part maintenant, il vole le transfert de
// maintenant. 0 = famille sans cible nommée (croisière non datée, déclaré).
// ═══ LA DURÉE DOIT ALLER AVEC LA DATE DE DÉPART ═══
// `slop_days` par défaut vaut 60 j : c'est la largeur opérationnelle de
// « maintenant » pour décider si la fenêtre est OUVERTE — la bonne question là,
// la mauvaise ici. Le meilleur transfert de ces 60 jours peut partir 6 semaines
// plus tard ; appliquer SA durée à un départ aujourd'hui donne un couple
// (départ, arrivée) que rien ne relie, et Lambert répond quand même : par un arc
// valide mais absurde, qui plonge à 0,26 UA du Soleil pour rejoindre Mars.
// Trouvé en RENDU, l'arc partant hors du champ (piège n°63). On resserre donc le
// slop à un pas de balayage de la carte porkchop : la durée rendue est celle
// d'un transfert qui part BIEN au moment du feu vert.
inline double transfer_tof_days(const Mission& m, Epoch now,
                                const ephem::IEphemeris& eph) {
  const WindowTarget wt = window_target_for_family(m.contract.family);
  if (!wt.impose) return 0.0;
  astro::WindowParams p;
  p.slop_days = p.horizon_days / static_cast<double>(p.n_dep);   // un pas de balayage
  const astro::WindowResult w = astro::launch_window(eph, wt.dep, wt.arr, now, p);
  return w.ok ? w.local_tof_days : 0.0;
}

// ═══ LE GATE D'ARRIVÉE ═══ [GDD 4.1, 9]
// « Lancer » n'a jamais voulu dire « avoir réussi » : entre le feu vert et le
// débrief il y a un VOL, et il dure. Tant que la chronologie n'est pas arrivée à
// sa manœuvre d'arrivée, le débrief est refusé — et comme le gate de fenêtre, le
// refus CHIFFRE l'attente au lieu de la subir. Une croisière non datée (cible
// non nommée, poussée continue) ne bloque rien : on n'oppose pas une date qu'on
// ne sait pas calculer.
inline GateResult arrival_gate(const Mission& m, double now_days) {
  const ArrivalStatus a = flight_arrival(m, now_days);
  if (!a.dated || a.arrived) return {true, ""};
  GateResult r;
  r.allowed = false;
  char buf[112];
  std::snprintf(buf, sizeof buf, "vol en cours : arrivee dans %.0f jours",
                a.reste_jours);
  r.reason = buf;
  return r;
}

// ═══ L'ISSUE DU VOL ═══ — déterministe, tirée contre la P(succès) évaluée.
struct FlightOutcome {
  bool         success{false};
  Severity     severity{Severity::Minor};
  std::string  cause;
  AnomalyEvent anomaly;         // rempli si échec, à passer à apply_anomaly
  bool         has_anomaly{false};
};

// TOLÉRANCE D'ARRIVÉE : le point de visée doit être atteint pour que la capture
// soit possible. 1 000 km est un ordre de grandeur de couloir de capture ; à
// CALIBRER [GDD Annexe E]. Repère : une campagne bien poursuivie arrive à
// ~100 km, une campagne aveugle à ~90 000 km — le seuil sépare deux régimes, il
// n'arbitre pas un cas limite.
// AU NIVEAU DE L'ESPACE DE NOMS, et non plus dans le corps de `fly_mission` : la
// tolérance que le code de vol du joueur reçoit dans ses entrées [GDD 15.3] doit
// être CELLE-CI, pas une copie. Un chiffre, une source.
inline constexpr double ARRIVEE_TOLERANCE_KM = 1000.0;

// `seed` : combine la graine d'agence et l'identité de la mission -> rejouable.
inline FlightOutcome fly_mission(const Mission& m, const MissionPlan& plan,
                                 std::uint64_t seed) {
  FlightOutcome out;
  const Assessment& a = plan.assessment;
  Rng rng(seed);

  // ═══ 0 bis) EXÉCUTER HORS DU DOMAINE DE VALIDITÉ ═══ [GDD 15.5, ch.10]
  // « Un code qualifié en orbite basse n'est PAS qualifié pour Mars ; exécuter
  // hors du domaine = comportement NON COUVERT = cause d'anomalie légitime. »
  // C'est ce qui donne son prix au banc d'essai : sans cette porte, acheter des
  // heures d'essai ne changeait rien à l'issue et la qualification n'était qu'un
  // décor payant.
  //
  // AVANT le verdict de navigation, et pas après : quand les deux tombent, la
  // cause PROXIMALE est le logiciel. Un code dont on ne sait rien a pu commander
  // n'importe quoi — le manque au but qu'on mesurerait ensuite serait sa
  // conséquence, pas une seconde faute.
  //
  // CE N'EST PAS UNE PÉNALITÉ POUR AVOIR ÉCRIT DU CODE. Rester dans son domaine
  // est gratuit, visible au poste avant le feu vert, et le joueur peut toujours
  // élargir la plage exercée (en payant des heures) ou ne rien embarquer. Ce qui
  // se paie ici, c'est d'avoir embarqué un logiciel sur un vol qu'il n'a jamais
  // vu au banc.
  if (m.code_embarque && m.code_non_couvert) {
    AnomalyEvent ev;
    ev.mission_id = m.contract.id;
    ev.date_days = m.state_entered_days;
    ev.what = "logiciel de vol execute hors de son domaine de validite";
    ev.severity = Severity::Major;
    ev.modifiers.primary_objective_lost = true;
    // Cause racine documentée [GDD 10.3] : la fiche disait ce qu'elle couvrait.
    ev.modifiers.player_error_causal = true;
    if (m.contract.crewed) ev.modifiers.human_lethal_exposure = true;
    out.cause = "comportement non couvert du logiciel de vol";
    out.severity = ev.severity;
    out.anomaly = ev;
    out.has_anomaly = true;
    out.success = false;
    return out;
  }

  // ═══ 0) LA NAVIGATION N'EST PLUS UN DÉ ═══ [GDD 8.1, 8.4]
  // Quand le vol a une navigation calculée, l'erreur d'injection a été TIRÉE au
  // feu vert et le Δv de correction qu'elle exige est un NOMBRE. La question
  // devient donc factuelle : la marge provisionnée le couvre-t-elle ? C'est la
  // différence entre estimer un risque (à la conception) et subir un résultat
  // (en vol) — et c'est ce qui fait qu'une marge trop courte se paie, au lieu
  // de se diluer dans une probabilité.
  if (m.nav_evaluee && (m.nav_dv_required > plan.program.dv_margin ||
                        m.nav_miss_km > ARRIVEE_TOLERANCE_KM)) {
    AnomalyEvent ev;
    ev.mission_id = m.contract.id;
    ev.date_days = m.state_entered_days;
    ev.what = (m.nav_dv_required > plan.program.dv_margin)
                  ? "campagne de correction au-dela de la marge provisionnee"
                  : "point de visee manque : capture impossible";
    ev.severity = Severity::Major;
    ev.modifiers.primary_objective_lost = true;
    // Le joueur a sous-provisionné : c'est une décision de conception, donc une
    // cause racine documentée [GDD 10.3].
    ev.modifiers.player_error_causal = true;
    if (m.contract.crewed) ev.modifiers.human_lethal_exposure = true;
    out.cause = "derive de navigation hors corridor";
    out.severity = ev.severity;
    out.anomaly = ev;
    out.has_anomaly = true;
    out.success = false;
    return out;
  }

  // 1) LA MISSION RÉUSSIT-ELLE ? Tirage contre la P(succès) évaluée. Quand la
  // navigation est résolue, elle ne doit plus peser une SECONDE fois dans le
  // tirage : on retire son facteur, sinon le même risque serait compté deux
  // fois — une estimation ET un fait.
  const double p_tirage =
      (m.nav_evaluee && a.p_physics > 0.0) ? a.p_success / a.p_physics : a.p_success;
  if (rng.uniform01() <= p_tirage) {
    out.success = true;
    out.severity = Severity::Minor;
    out.cause = "mission nominale";
    return out;
  }

  // 2) ÉCHEC : à QUOI est-il dû ? On attribue la cause proportionnellement aux
  // probabilités de défaillance de chaque poste (1-p). Le poste le plus fragile
  // est le plus probable — c'est la lecture d'ingénieur, pas un dé.
  const double f_launcher = 1.0 - a.p_launcher;
  const double f_engine   = 1.0 - a.p_engine;
  const double f_blunder  = a.p_blunder;
  const double f_physics  = 1.0 - a.p_physics;
  const double ftot = f_launcher + f_engine + f_blunder + f_physics;
  double pick = rng.uniform(0.0, ftot > 0.0 ? ftot : 1.0);

  AnomalyEvent ev;
  ev.mission_id = m.contract.id;
  ev.date_days = m.state_entered_days;

  if (pick < f_launcher) {
    // Défaillance lanceur : perte au décollage ou en ascension. Véhicule perdu,
    // débris possibles en LEO si la fragmentation a lieu en altitude.
    out.cause = "defaillance du lanceur";
    ev.what = "echec du lanceur en ascension";
    ev.severity = Severity::Critical;
    ev.modifiers.unique_vehicle_lost = true;
    ev.breakup_mass_kg = a.m0_kg * 0.3;   // etage superieur + charge utile
    ev.breakup_alt_km = 200.0;
    ev.breakup_is_collision = false;
  } else if ((pick -= f_launcher) < f_engine) {
    out.cause = "defaillance moteur en vol";
    ev.what = "extinction ou explosion moteur";
    ev.severity = Severity::Major;
    ev.modifiers.unique_vehicle_lost = true;
  } else if ((pick -= f_engine) < f_blunder) {
    // Erreur grossière de conception/calcul non rattrapée : le joueur en est la
    // cause documentée [GDD 10.3 modificateur].
    out.cause = "erreur de conception non rattrapee";
    ev.what = "faute de calcul : trajectoire ou budget errone";
    ev.severity = Severity::Major;
    ev.modifiers.player_error_causal = true;
    ev.modifiers.primary_objective_lost = true;
  } else {
    // Écart de navigation au-delà du corridor : objectif manqué, mais souvent
    // récupérable — gravité moindre.
    out.cause = "derive de navigation hors corridor";
    ev.what = "insertion hors tolerance : objectif degrade";
    ev.severity = Severity::Moderate;
  }

  // Une mission HABITÉE expose un équipage : le palier monte [GDD 10.3].
  if (m.contract.crewed) ev.modifiers.human_lethal_exposure = true;

  out.severity = ev.severity;
  out.anomaly = ev;
  out.has_anomaly = true;
  out.success = false;
  return out;
}

// Graine rejouable pour une mission : agence + identité de la mission.
inline std::uint64_t mission_seed(std::uint64_t agency_seed, const std::string& mission_id) {
  std::uint64_t h = 1469598103934665603ull;      // FNV-1a offset
  for (char ch : mission_id) { h ^= static_cast<unsigned char>(ch); h *= 1099511628211ull; }
  return agency_seed ^ h;
}

} // namespace fen::mission
