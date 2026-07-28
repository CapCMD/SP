// fen/mission/FlightTimeline.hpp — LA CHRONOLOGIE DE VOL [GDD 4.1, 9, 14.3]
//
// Jusqu'ici, « lancer » et « débriefer » étaient deux clics consécutifs : un vol
// vers Mars ne consommait PAS UNE SECONDE de temps de jeu. Le moteur savait
// pourtant qu'une insertion est une phase critique (Events.hpp majore ses taux
// d'anomalie, MissionTempo.hpp en déduit un plafond de cadence) — il ne savait
// simplement pas QUAND elle a lieu. Une phase qui n'a pas de date n'arrive
// jamais : le plafond ne mordait qu'à l'ascension, et le temps qui coule
// [GDD 14.2] n'avait rien à quoi s'appliquer.
//
// Ce fichier DATE le vol. À partir du feu vert, une mission a une suite de
// segments — ascension, parking, injection, croisière, insertion ou EDL — dont
// chacun porte un début et une fin. Trois conséquences immédiates :
//   . `flight_phase_of` LIT cette chronologie au lieu de deviner ;
//   . le plafond de cadence [GDD 14.3] mord à l'insertion et à l'EDL ;
//   . le vol DURE : on ne débriefe pas une mission qui n'est pas arrivée
//     (`flight_arrival`, opposé par le gate de MissionLoop.hpp).
//
// ═══ AUCUNE DURÉE N'EST UN RÉGLAGE ═══ [GDD 6.8, doctrine du projet]
// Elles sont SOURCÉES (ascension, EDL, manœuvre critique : voir plus bas) ou
// DÉRIVÉES par Kepler de l'orbite concernée (attente en parking, transit GTO,
// phasage de rendez-vous). La croisière interplanétaire, elle, n'est pas
// inventée du tout : c'est la DURÉE DE TRANSIT de la fenêtre réellement visée
// (`astro::WindowResult::local_tof_days`), capturée au feu vert. Une famille
// dont le contrat ne nomme pas de cible n'a donc PAS de date d'arrivée, et le
// modèle le DIT (`dated == false`) au lieu de la fabriquer.
//
// C++ pur, aucune dépendance UE.
#pragma once
#include <cmath>
#include <string>

#include "fen/core/Constants.hpp"
#include "fen/mission/MissionFsm.hpp"

namespace fen::mission {

// ═══ DURÉES CARACTÉRISTIQUES DES PHASES ═══ (secondes de temps de JEU)
// Ordres de grandeur RÉELS et sourcés [GDD 6.8], jamais des valeurs de confort :
//   . ascension sol -> orbite ~9 min   (Falcon 9 : SECO à T+8 min 40 ;
//                                       Navette : MECO à T+8 min 30) ;
//   . EDL ~7 min                       (MSL, « seven minutes of terror » :
//                                       entrée atmosphérique -> toucher) ;
//   . manœuvre critique ~10 min        (insertion orbitale : Apollo LOI 6 min 2 s,
//                                       ordre de grandeur de la dizaine de min).
// Les phases NON critiques n'ont pas de durée OPPOSABLE : elles durent ce que la
// trajectoire dure (voir les durées dérivées ci-dessous), et rien n'y exige la
// présence du joueur.
inline double phase_duration_s(FlightPhase p) {
  switch (p) {
    case FlightPhase::Launch:           return 9.0 * 60.0;
    case FlightPhase::Edl:              return 7.0 * 60.0;
    case FlightPhase::CriticalManeuver: return 10.0 * 60.0;
    default:                            return 0.0;   // pas de durée opposable
  }
}

// ═══ LES DURÉES DÉRIVÉES ═══ — Kepler, pas une table de constantes.
// L'ORBITE DE PARKING est la MÊME que celle dont `trajectory_dv_for_mission`
// paie l'injection (Oberth depuis 200 km) : un chiffre, une source. Le tout se
// déduit ensuite d'elle, sans qu'aucune durée ne soit saisie.
inline constexpr double PARKING_ALT_M = 200.0e3;

inline double orbital_period_s(double a_m, double mu) {
  return cst::TWO_PI * std::sqrt(a_m * a_m * a_m / mu);
}

inline double parking_radius_m() { return cst::R_EARTH + PARKING_ALT_M; }

// Une révolution en orbite de parking : ~88,5 min.
inline double parking_period_s() {
  return orbital_period_s(parking_radius_m(), cst::MU_EARTH);
}

// L'ORBITE GÉOSTATIONNAIRE N'EST PAS UN CHIFFRE : c'est la solution de
// « période orbitale == période de rotation de la Terre », donc a = (µ/ω²)^⅓ sur
// la vitesse de rotation SIDÉRALE (cst::OMEGA_EARTH). On retrouve 42 164 km sans
// jamais l'avoir écrit.
inline double geo_radius_m() {
  return std::cbrt(cst::MU_EARTH / (cst::OMEGA_EARTH * cst::OMEGA_EARTH));
}

// Transit sur l'orbite de transfert géostationnaire : une DEMI-période de
// l'ellipse parking -> GEO, soit ~5 h 15 — la valeur réellement volée.
inline double gto_coast_s() {
  return 0.5 * orbital_period_s(0.5 * (parking_radius_m() + geo_radius_m()),
                                cst::MU_EARTH);
}

// PHASAGE DE RENDEZ-VOUS EN ORBITE BASSE : le profil « 4 orbites » (Soyuz MS,
// Dragon) — quatre révolutions de l'orbite de poursuite pour rattraper la cible,
// soit ~5 h 54. APPROXIMATION DÉCLARÉE [GDD 6.8] : la vraie durée dépend de
// l'angle de phase à l'allumage, donc de l'instant du lancement ; le profil à
// quatre orbites est celui qu'on VISE en dimensionnant la fenêtre, et c'est lui
// qu'on retient tant que la mission vécue ne pilote pas la poursuite elle-même.
inline double rendezvous_phasing_s() { return 4.0 * parking_period_s(); }

// ═══ LE PROFIL DE VOL ═══ — ce que la famille du contrat IMPLIQUE comme suite
// de manœuvres. Ce n'est pas un genre littéraire : chaque profil est une
// succession physique différente (une charge GEO fait un transit d'ellipse, un
// cargo NEP spirale, un rover entre dans une atmosphère).
enum class FlightProfile {
  LeoRendezvous,    // poursuite et amarrage en orbite basse
  GeoTransfer,      // injection GTO, transit, circularisation
  Interplanetary,   // injection, croisière, insertion en orbite
  Surface,          // ... et EDL au lieu de l'insertion
  Continuous,       // poussée continue (NEP) : ni injection ni insertion brèves
};

inline FlightProfile flight_profile_of(const std::string& family) {
  if (family == "logistique" || family == "service" || family == "habite")
    return FlightProfile::LeoRendezvous;
  if (family == "sat")      return FlightProfile::GeoTransfer;
  if (family == "surface")  return FlightProfile::Surface;
  if (family == "nep")      return FlightProfile::Continuous;
  return FlightProfile::Interplanetary;   // mars, mars_habite, science, relativiste
}

// Familles dont l'exploitation se passe au voisinage de la Terre. Conservée
// parce que d'autres lois la lisent ; DÉRIVÉE du profil, pour qu'il n'y ait
// qu'une seule table de familles dans le moteur.
inline bool near_earth_family(const std::string& family) {
  const FlightProfile p = flight_profile_of(family);
  return p == FlightProfile::LeoRendezvous || p == FlightProfile::GeoTransfer;
}

// ═══ LA CHRONOLOGIE ═══
// Segments jointifs, datés en jours DEPUIS LE FEU VERT. Le DERNIER segment est
// ouvert (l'exploitation dure jusqu'au débrief) ; `duree_jours` est donc la date
// d'ARRIVÉE — fin de la dernière manœuvre d'arrivée — et non celle du dernier
// segment. `dated == false` signifie que l'arrivée n'est pas calculable : le
// modèle le déclare au lieu d'inventer une durée.
struct FlightSegment {
  FlightPhase phase{FlightPhase::Ground};
  double t0_days{0.0};
  double t1_days{0.0};
};

// ═══ LES CORRECTIONS DE MI-PARCOURS SONT DES RENDEZ-VOUS DATÉS ═══
// [GDD 8.4, 14.3]. Une TCM est une manœuvre fine : elle DOIT donc ramener le
// temps à un rythme lent, comme l'insertion et l'EDL. Ce n'est pas un ajout
// cosmétique à la chronologie — c'est ce qui garantit que le joueur EST LÀ au
// moment où il doit agir, au lieu de franchir la date à « mois/s » sans la voir.
// Dates SOURCÉES sur la pratique martienne (MSL : TCM-1 L+15 j, TCM-3 A−45 j) ;
// une correction tardive coûte une fortune, la leviée ayant disparu.
inline constexpr double TCM1_APRES_INJECTION_J = 14.0;
inline constexpr double TCM2_AVANT_ARRIVEE_J   = 45.0;

struct FlightTimeline {
  // 12 et non 8 : la croisière se coupe en trois autour de ses deux corrections.
  static constexpr int MAX = 12;
  FlightSegment seg[MAX]{};
  int    n{0};
  bool   dated{false};        // la date d'arrivée est calculable
  double t_go_days{0.0};      // date absolue du feu vert
  double duree_jours{0.0};    // feu vert -> fin de la manœuvre d'ARRIVÉE

  void pousser(FlightPhase p, double duree_s) {
    if (n >= MAX) return;
    const double t0 = (n == 0) ? 0.0 : seg[n - 1].t1_days;
    seg[n] = {p, t0, t0 + duree_s / cst::DAY};
    ++n;
  }
  // Segment terminal, sans fin : l'exploitation court jusqu'au débrief.
  void pousser_ouvert(FlightPhase p) {
    if (n >= MAX) return;
    const double t0 = (n == 0) ? 0.0 : seg[n - 1].t1_days;
    seg[n] = {p, t0, t0};       // t1 == t0 marque l'absence de fin
    ++n;
  }

  // Phase à une date ABSOLUE. Avant le feu vert : au sol. Après le dernier
  // segment fermé : la phase du segment ouvert.
  FlightPhase phase_at(double now_days) const {
    if (n == 0) return FlightPhase::Ground;
    const double t = now_days - t_go_days;
    if (t < 0.0) return FlightPhase::Ground;
    for (int i = 0; i < n; ++i)
      if (t < seg[i].t1_days) return seg[i].phase;
    return seg[n - 1].phase;
  }
};

// Construit la chronologie d'une mission. `tof_days` = durée de transit de la
// fenêtre visée, capturée au feu vert (0 = inconnue, cf. en-tête).
inline FlightTimeline build_flight_timeline(const Mission& m, double tof_days) {
  FlightTimeline tl;
  if (m.state != MissionState::Launched) return tl;
  tl.t_go_days = m.state_entered_days;

  const FlightProfile prof = flight_profile_of(m.contract.family);
  const double t_asc  = phase_duration_s(FlightPhase::Launch);
  const double t_burn = phase_duration_s(FlightPhase::CriticalManeuver);
  const double t_edl  = phase_duration_s(FlightPhase::Edl);
  const bool   croisiere_datee = tof_days > 0.0;

  // L'ascension est commune à tous les profils : on part toujours du sol.
  tl.pousser(FlightPhase::Launch, t_asc);

  switch (prof) {
    case FlightProfile::LeoRendezvous:
      // Poursuite puis amarrage. « LeoOps » nomme ici l'orbite d'attente : une
      // phase orbitale où rien n'est critique.
      tl.pousser(FlightPhase::LeoOps, rendezvous_phasing_s());
      tl.pousser(FlightPhase::CriticalManeuver, t_burn);   // rendez-vous/amarrage
      tl.duree_jours = tl.seg[tl.n - 1].t1_days;
      tl.dated = true;
      tl.pousser_ouvert(FlightPhase::LeoOps);
      break;

    case FlightProfile::GeoTransfer:
      tl.pousser(FlightPhase::LeoOps, parking_period_s());        // un tour d'attente
      tl.pousser(FlightPhase::CriticalManeuver, t_burn);          // injection GTO
      tl.pousser(FlightPhase::TransferCruise, gto_coast_s());     // transit d'ellipse
      tl.pousser(FlightPhase::CriticalManeuver, t_burn);          // circularisation
      tl.duree_jours = tl.seg[tl.n - 1].t1_days;
      tl.dated = true;
      tl.pousser_ouvert(FlightPhase::LeoOps);
      break;

    case FlightProfile::Interplanetary:
    case FlightProfile::Surface:
      tl.pousser(FlightPhase::LeoOps, parking_period_s());        // orbite de parking
      tl.pousser(FlightPhase::CriticalManeuver, t_burn);          // injection
      if (!croisiere_datee) {
        // Cible non nommée par le contrat : la croisière n'a pas de fin
        // calculable. On le DÉCLARE plutôt que de la fabriquer.
        tl.pousser_ouvert(FlightPhase::TransferCruise);
        break;
      }
      // LA CROISIÈRE SE COUPE AUTOUR DE SES CORRECTIONS. Chaque TCM est une
      // manœuvre critique datée : le plafond de cadence l'imposera au monde, et
      // le joueur sera présent pour l'exécuter [GDD 8.4, 14.3].
      {
        const double tof_s = tof_days * cst::DAY;
        const double t1 = TCM1_APRES_INJECTION_J * cst::DAY;
        const double t2 = tof_s - TCM2_AVANT_ARRIVEE_J * cst::DAY;
        // Une croisière trop courte pour porter ses deux corrections les perd
        // plutôt que de les empiler : on ne date pas une manœuvre qui n'aurait
        // pas le temps d'avoir lieu.
        if (t1 + t_burn < t2 && t2 + t_burn < tof_s) {
          tl.pousser(FlightPhase::TransferCruise, t1);
          tl.pousser(FlightPhase::CriticalManeuver, t_burn);      // TCM-1
          tl.pousser(FlightPhase::TransferCruise, t2 - t1 - t_burn);
          tl.pousser(FlightPhase::CriticalManeuver, t_burn);      // TCM-2
          tl.pousser(FlightPhase::TransferCruise, tof_s - t2 - t_burn);
        } else {
          tl.pousser(FlightPhase::TransferCruise, tof_s);
        }
      }
      if (prof == FlightProfile::Surface) {
        tl.pousser(FlightPhase::Edl, t_edl);
        tl.duree_jours = tl.seg[tl.n - 1].t1_days;
        tl.dated = true;
        tl.pousser_ouvert(FlightPhase::SurfaceOps);
      } else {
        tl.pousser(FlightPhase::CriticalManeuver, t_burn);        // insertion
        tl.duree_jours = tl.seg[tl.n - 1].t1_days;
        tl.dated = true;
        tl.pousser_ouvert(FlightPhase::LeoOps);                   // orbite de la cible
      }
      break;

    case FlightProfile::Continuous:
      // Poussée continue : la spirale n'a ni injection ni insertion BRÈVES, et sa
      // durée demande une intégration de la poussée, pas une formule fermée.
      // Non datée, et déclaré comme tel [GDD 6.8].
      tl.pousser(FlightPhase::LeoOps, parking_period_s());
      tl.pousser_ouvert(FlightPhase::TransferCruise);
      break;
  }
  return tl;
}

inline FlightTimeline build_flight_timeline(const Mission& m) {
  return build_flight_timeline(m, m.tof_days);
}

// ═══ LA DATE D'INJECTION ═══ — la fin de la première manœuvre critique, donc le
// début de l'arc héliocentrique. `build_flight_trace` la calculait déjà en
// interne ; elle est extraite ici parce qu'un second appelant en a besoin
// (l'arc de poursuite disponible, [GDD 8.6]) et que la LIRE coûte une
// chronologie quand la reconstruire coûterait un Lambert et 512 propagations.
// Rend false si le vol n'a pas d'injection datée.
inline bool flight_injection_days(const Mission& m, double& t_inj_days) {
  if (m.state != MissionState::Launched) return false;
  const FlightTimeline tl = build_flight_timeline(m);
  if (!tl.dated || tl.n < 3) return false;
  for (int i = 0; i < tl.n; ++i)
    if (tl.seg[i].phase == FlightPhase::CriticalManeuver) {
      t_inj_days = tl.t_go_days + tl.seg[i].t1_days;
      return true;
    }
  return false;
}

// ═══ LA PHASE DE VOL EST DÉRIVÉE, PAS SAISIE ═══
// `Mission::phase` existait sans que rien ne la renseigne : un drapeau qu'on ne
// pouvait que cocher à la main, donc un « malus abstrait » en puissance. Elle est
// une LECTURE de la chronologie ci-dessus — déterministe, rejouable, et rien de
// plus à sauvegarder que la date du feu vert et la durée de transit visée.
inline FlightPhase flight_phase_of(const Mission& m, double now_days) {
  if (m.state != MissionState::Launched) return FlightPhase::Ground;
  return build_flight_timeline(m).phase_at(now_days);
}

// ═══ EST-ON ARRIVÉ ? ═══
// La réponse qui manquait pour que « lancer » cesse d'être « avoir réussi ».
// `dated == false` (cible non nommée, poussée continue) laisse le vol libre : on
// ne bloque JAMAIS sur une date qu'on ne sait pas calculer.
struct ArrivalStatus {
  bool   dated{false};
  bool   arrived{true};
  double reste_jours{0.0};
};

inline ArrivalStatus flight_arrival(const Mission& m, double now_days) {
  ArrivalStatus a;
  if (m.state != MissionState::Launched) return a;
  const FlightTimeline tl = build_flight_timeline(m);
  if (!tl.dated) return a;                       // non datée : rien à opposer
  a.dated = true;
  const double reste = (tl.t_go_days + tl.duree_jours) - now_days;
  a.arrived = reste <= 0.0;
  a.reste_jours = reste > 0.0 ? reste : 0.0;
  return a;
}

} // namespace fen::mission
