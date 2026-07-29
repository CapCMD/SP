// fen/mission/Avaries.hpp — LA VIE À BORD TOMBE EN PANNE [GDD 9.1, 9.5]
//
// `mission/Events.hpp` tirait des événements calibrés depuis le premier jour, et
// PERSONNE ne les consommait : la « bibliothèque d'anomalies » de [GDD 9.5]
// existait sans qu'aucune ne se produise jamais. Ce fichier est ce qui manquait —
// ce qu'un événement FAIT, combien de temps il le fait, et ce que le joueur peut
// y opposer [GDD 9.1 : « diagnostics/réparations »].
//
// DEUX NATURES D'ÉVÉNEMENT, et la distinction est physique, pas de confort :
//   . l'ÉRUPTION SOLAIRE est un INSTANT — une dose reçue, rien à réparer ; c'est
//     le blindage embarqué qui décide, et lui seul [GDD 6.6] ;
//   . les PANNES sont des ÉTATS — elles durent tant qu'on ne les répare pas, et
//     leur effet se paie CHAQUE JOUR sur les consommables.
// C'est cette seconde nature qui donne un sens à la maintenance : réparer une
// avarie de support-vie, c'est arrêter une hémorragie de vivres, pas effacer une
// icône.
//
// C++ pur, sous oracle. Ne connaît ni GameState ni le rendu.
#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "fen/env/Radiation.hpp"
#include "fen/mission/Crew.hpp"
#include "fen/mission/Events.hpp"

namespace fen::mission {

// ═══ UNE AVARIE ═══ — un événement qui DURE.
struct Avarie {
  EventKind kind{};
  double    debut_days{0.0};      // date de jeu de l'occurrence
  double    gravite01{0.0};       // magnitude tirée par EventSampler
  // RÉPARATION EN COURS : une réparation prend du temps, et pendant ce temps
  // l'avarie continue de coûter. `fin_reparation_days` <= debut => pas engagée.
  double    fin_reparation_days{0.0};
  bool      reparee{false};

  bool en_reparation(double now_days) const {
    return !reparee && fin_reparation_days > debut_days && now_days < fin_reparation_days;
  }
  bool active(double now_days) const { return !reparee && now_days >= debut_days; }
};

// ═══ CE QU'UNE AVARIE COÛTE, PAR JOUR ═══
// Tous les effets portent sur des grandeurs qui EXISTENT déjà et qui sont déjà
// vivantes (les boucles de recyclage, les soutes, l'épuration du CO2, la boucle
// sol). Aucun « malus » abstrait : une panne de support-vie dégrade la boucle,
// point — et c'est `VitalState::consume` qui en tire les conséquences.
struct EffetsAvaries {
  double recup_eau_restante{1.0};   // fraction de la récupération d'eau encore obtenue
  double recup_o2_restante{1.0};    // idem pour l'oxygène
  double fuite_o2_kg_j{0.0};        // perte directe (brèche)
  double fuite_eau_kg_j{0.0};
  double facteur_co2{1.0};          // >1 : l'épuration consomme plus vite sa capacité
  double surconso_vivres{1.0};      // >1 : un malade consomme davantage
  bool   sol_injoignable{false};    // plus de commande depuis le sol [GDD 9.6]
  int    n_actives{0};
};

// Débits de fuite d'une brèche, DÉCLARÉS [GDD 6.8] : une micrométéorite qui
// perce fait un trou de l'ordre du millimètre, et l'ordre de grandeur d'une fuite
// d'atmosphère à travers un tel trou se compte en kilogrammes par jour. On prend
// 0,5 kg/j d'air au plus fort de l'échelle, et un dixième en eau (circuit percé).
inline constexpr double FUITE_O2_MAX_KG_J  = 0.5;
inline constexpr double FUITE_EAU_MAX_KG_J = 0.05;

inline EffetsAvaries effets_avaries(const std::vector<Avarie>& av, double now_days) {
  EffetsAvaries e;
  for (const auto& a : av) {
    if (!a.active(now_days)) continue;
    ++e.n_actives;
    switch (a.kind) {
      case EventKind::LifeSupportFault:
        // La boucle ne s'arrête pas : elle DÉGRADE. Une panne franche coûte la
        // moitié de la récupération, une panne mineure quelques pour cent.
        e.recup_eau_restante *= (1.0 - 0.5 * a.gravite01);
        e.recup_o2_restante  *= (1.0 - 0.5 * a.gravite01);
        break;
      case EventKind::PowerFault:
        // L'épuration du CO2 est un poste ÉLECTRIQUE : moins de puissance, plus
        // de cartouche consommée pour le même CO2 rejeté.
        e.facteur_co2 *= (1.0 + 1.5 * a.gravite01);
        // Et l'ECLSS est alimenté lui aussi : la récupération en pâtit.
        e.recup_eau_restante *= (1.0 - 0.25 * a.gravite01);
        e.recup_o2_restante  *= (1.0 - 0.25 * a.gravite01);
        break;
      case EventKind::Micrometeorite:
        e.fuite_o2_kg_j  += FUITE_O2_MAX_KG_J  * a.gravite01;
        e.fuite_eau_kg_j += FUITE_EAU_MAX_KG_J * a.gravite01;
        break;
      case EventKind::MedicalEmergency:
        // Un malade consomme plus (soins, hydratation) et ne travaille pas.
        e.surconso_vivres *= (1.0 + 0.20 * a.gravite01);
        break;
      case EventKind::CommLoss:
        // [GDD 9.6] : le sol ne peut plus commander. Ce n'est pas une perte de
        // ressource — c'est une perte d'ASSISTANCE, et elle se paie ailleurs.
        e.sol_injoignable = true;
        break;
      default:
        break;   // l'éruption solaire n'est pas une avarie : voir plus bas
    }
  }
  e.recup_eau_restante = std::clamp(e.recup_eau_restante, 0.0, 1.0);
  e.recup_o2_restante  = std::clamp(e.recup_o2_restante,  0.0, 1.0);
  return e;
}

// LES BOUCLES RÉELLEMENT OBTENUES, avaries comprises. C'est ce qu'il faut passer
// à `VitalState::consume` : une panne de support-vie doit se lire dans la
// consommation, sinon elle n'existe pas.
inline RecyclingLoops boucles_degradees(const RecyclingLoops& nominal,
                                        const EffetsAvaries& e) {
  RecyclingLoops l;
  l.water_recovery = nominal.water_recovery * e.recup_eau_restante;
  l.o2_recovery    = nominal.o2_recovery    * e.recup_o2_restante;
  return l;
}

// ═══ RÉPARER [GDD 9.1, 5.10] ═══
// « Diagnostics / réparations. » La capacité de réparer n'est PAS un tirage : le
// joueur répare ce que son architecture lui permet de réparer. C'est la branche 4
// (« Réparabilité et maintenance locale », « Diagnostics autonomes », « Médecine
// embarquée assistée ») qui achète cette capacité — et c'est ce qui lui donne
// enfin un effet mesurable en vol, comme le recyclage en a un sur la masse.
//
// UN DÉ DE PLUS SERAIT UN DÉ NU, et la doctrine du moteur l'interdit : ce qu'on
// modélise, c'est de la COMPÉTENCE et du TEMPS, pas de la chance.
struct CapaciteBord {
  bool maintenance_locale{false};    // réparer la structure et les fluides
  bool diagnostics_autonomes{false}; // trouver la panne sans le sol -> plus vite
  bool medecine_embarquee{false};    // traiter une urgence médicale
  bool redondance_base{false};       // basculer sur un secours -> plus vite
};

// Peut-on seulement s'y attaquer ? Une urgence médicale ne se répare pas avec une
// clé, et une brèche ne se soigne pas : à chaque nature d'avarie sa capacité.
inline bool reparable(EventKind k, const CapaciteBord& c) {
  switch (k) {
    case EventKind::MedicalEmergency:   return c.medecine_embarquee;
    case EventKind::SolarParticleEvent: return false;   // un instant, pas un état
    case EventKind::CommLoss:           return c.diagnostics_autonomes || c.maintenance_locale;
    default:                            return c.maintenance_locale;
  }
}

// DURÉE d'une réparation (jours). Elle croît avec la gravité et se raccourcit
// avec ce qu'on a embarqué. Les deux facilitateurs ne font PAS la même chose :
// le diagnostic autonome trouve la panne (il coupe le temps de recherche), la
// redondance permet de basculer pendant qu'on répare (elle coupe l'urgence).
inline constexpr double REPARATION_JOURS_BASE = 3.0;

inline double duree_reparation_jours(const Avarie& a, const CapaciteBord& c) {
  double j = REPARATION_JOURS_BASE * (0.5 + 1.5 * a.gravite01);
  if (c.diagnostics_autonomes) j *= 0.6;
  if (c.redondance_base)       j *= 0.8;
  return j;
}

// ═══ L'ÉRUPTION SOLAIRE : UN INSTANT, ET LE BLINDAGE DÉCIDE ═══ [GDD 6.6]
// Et voici la SYMÉTRIE que le GCR seul cachait : contre les GCR, blinder ne sert
// presque à rien (plancher de spallation, ~11 % gagnés en doublant la masse au
// décollage) ; contre un SPE, l'atténuation est EXPONENTIELLE — λ ≈ 15 g/cm² pour
// un matériau riche en hydrogène. 20 g/cm² divisent la dose aiguë par ~3,8.
// C'est exactement pourquoi les vrais projets blindent un ABRI ANTI-TEMPÊTE et
// non tout l'habitat : la masse ne sert pas contre le fond permanent, elle sauve
// la vie le jour où le Soleil s'emporte.
inline double dose_aigue_spe_gy(double magnitude01, const env::Shielding& s) {
  env::SpeEvent ev;
  ev.unshielded_dose_gy = spe_unshielded_gy(magnitude01);
  return env::spe_dose_gy(ev, s);
}

} // namespace fen::mission
