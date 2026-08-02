// fen/env/Radiation.hpp — environnement radiatif [GDD 6.6, 7.7, 19.1]
//
// TROIS SOURCES DISTINCTES, trois traitements :
//   GCR        dose CHRONIQUE, haut TEL, presque impossible à blinder ;
//   SPE        bouffées AIGUËS (plusieurs Gy/h non blindé -> risque létal),
//              corrélées au cycle solaire réel [SpaceWeather.hpp] ;
//   Van Allen  radiation piégée : interne (protons) / externe (électrons),
//              à traverser VITE ou à éviter.
// INVARIANTS : le blindage est MASSIQUE et jamais gratuit ; l'hydrogène est
// efficace ; le Z élevé AGGRAVE (secondaires de spallation) ; un réacteur
// ajoute sa propre source (shadow shield + distance) [GDD 6.6].
// Ordres de grandeur ancrés sur l'Annexe B : AR Mars ~0.3-0.7 Sv de GCR.
// Toutes les valeurs sont des hypothèses de modèle DÉCLARÉES [GDD 6.8].
#pragma once
#include <cmath>
#include "fen/core/Constants.hpp"

namespace fen::env {

// --- ShieldingModel ----------------------------------------------------------
// Épaisseur exprimée en g/cm² (densité surfacique) : la seule unité qui compte.
// 20 g/cm² d'eau ~ 20 cm ; 20 g/cm² d'alu ~ 7.4 cm.
struct Shielding {
  double areal_density_gcm2{0.0};  // g/cm² entre l'équipage et l'espace
  double hydrogen_richness{0.5};   // 0 = Z élevé (alu/acier), 1 = polyéthylène/eau
};

// Atténuation SPE (protons, spectre mou) : exponentielle efficace,
// λ ≈ 15 g/cm² pour matériaux H-riches, 25 g/cm² pour Z élevés (déclaré).
inline double spe_transmission(const Shielding& s) {
  const double lambda = 25.0 - 10.0 * s.hydrogen_richness;
  return std::exp(-s.areal_density_gcm2 / lambda);
}

// Atténuation GCR : très dure. Plancher physique : au-delà de ~30 g/cm², les
// SECONDAIRES (spallation) compensent l'absorption — un blindage Z élevé peut
// même AUGMENTER la dose. Modèle : décroissance douce, plancher à 0.60/0.75.
inline double gcr_transmission(const Shielding& s) {
  const double floor_t = 0.75 - 0.15 * s.hydrogen_richness;  // H-riche : meilleur plancher
  const double lambda  = 60.0;                               // g/cm² (déclaré)
  const double t = floor_t + (1.0 - floor_t) * std::exp(-s.areal_density_gcm2 / lambda);
  // Z élevé + fort blindage : léger rebond de secondaires.
  const double rebound = (1.0 - s.hydrogen_richness) * 0.05
                         * (1.0 - std::exp(-s.areal_density_gcm2 / 30.0));
  return t + rebound;
}

// --- GcrFlux -----------------------------------------------------------------
// Débit de dose GCR interplanétaire NON blindé. Ancrage Annexe B : un aller-
// retour martien (~ 500 j de croisière) fait ~0.3-0.7 Sv -> ~0.6-1.4 mSv/j
// selon la phase du cycle. Déclaré : 1.2 mSv/j au minimum solaire (gcr_mod=1).
inline constexpr double GCR_FREE_SPACE_SV_PER_DAY = 1.2e-3;

// En orbite basse terrestre, la magnétosphère + la Terre masquent ~1/2 du ciel :
// facteur déclaré 0.4. En surface planétaire (Mars), le sol masque 2π : 0.5.
inline double gcr_dose_rate_sv_day(double gcr_modulation, const Shielding& s,
                                   double geometry_factor = 1.0) {
  return GCR_FREE_SPACE_SV_PER_DAY * gcr_modulation * gcr_transmission(s)
         * geometry_factor;
}

// --- SpeEventModel -----------------------------------------------------------
// Une éruption majeure : dose aiguë en heures. Non blindé : plusieurs Gy
// possibles [GDD 6.6] -> létal. L'abri anti-tempête (ergols, eau, cargaison)
// est une DÉCISION D'ARCHITECTURE, pas un bonus.
struct SpeEvent {
  double t_onset{};                // s TDB
  double duration_s{18.0 * 3600};  // quelques heures à ~1 jour
  double unshielded_dose_gy{};     // tirée par EventSampler (0.1 .. 5 Gy)
};

inline double spe_dose_gy(const SpeEvent& ev, const Shielding& s) {
  return ev.unshielded_dose_gy * spe_transmission(s);
}

// Facteur de qualité moyen des protons SPE (Gy -> Sv) : déclaré 1.5.
inline constexpr double SPE_QUALITY_FACTOR = 1.5;

// --- VanAllenBeltModel -------------------------------------------------------
// Dose par TRAVERSÉE (montée GTO/translunaire) selon le périgée/apogée : on ne
// modélise pas la carte L-shell en V1, mais un péage par passage, déclaré.
struct VanAllenCrossing {
  double dose_sv_unshielded{0.010};  // ~10 mSv/traversée rapide (déclaré)
};
inline double van_allen_dose_sv(const VanAllenCrossing& c, const Shielding& s) {
  return c.dose_sv_unshielded * spe_transmission(s);  // spectre proton : même λ
}

// Séjour PROLONGÉ en orbite moyenne (au cœur des ceintures) : interdit de fait.
inline constexpr double MEO_DOSE_RATE_SV_PER_DAY = 5.0e-3;  // blindage léger

// --- DoseAccumulator [GDD 6.6] -----------------------------------------------
// Cumul de mission ET de carrière. La limite de carrière est un VERROU : un
// personnage "consommé" ne revole pas — arbitrage réel des programmes habités.
inline constexpr double CAREER_DOSE_LIMIT_SV = 1.0;   // référence institutionnelle
inline constexpr double ACUTE_SICKNESS_GY    = 1.0;   // syndrome aigu probable
inline constexpr double ACUTE_LETHAL_GY      = 4.5;   // DL50 sans soins

// ═══════════════════════════════════════════════════════════════════════════
// CHRONIQUE ET AIGU NE TUENT PAS DE LA MÊME FAÇON [GDD 6.6, Annexe B]
// ═══════════════════════════════════════════════════════════════════════════
// Le modèle savait tuer par dose AIGUË (déterministe : au-delà de la DL50, on
// meurt) et **ne savait rien faire de la dose CHRONIQUE** : 10 Sv accumulés sur
// vingt ans ne produisaient aucun effet de santé, ils se contentaient de
// verrouiller les vols suivants — c'est-à-dire rien du tout sur un vol terminal
// [GDD 9.2], qui est justement le seul où de telles doses arrivent.
//
// La différence est RÉELLE et se chiffre. Un effet chronique est STOCHASTIQUE :
// il ne fixe pas un seuil de mort, il fixe une PROBABILITÉ de mort par cancer
// radio-induit — le REID (Risk of Exposure-Induced Death), l'instrument dont
// les agences se servent réellement.
//
//   . coefficient de risque ICRP pour une population de TRAVAILLEURS ADULTES :
//     ~4,1 % par Sv (la population générale est à ~5,5 %) ;
//   . DDREF (Dose and Dose Rate Effectiveness Factor) : à FAIBLE DÉBIT, le même
//     Sv fait environ deux fois moins de dégâts qu'en une fois — l'ADN a le
//     temps de se réparer. ICRP retient 2. C'est EXACTEMENT la distinction qui
//     manquait, et c'est elle qui rend un aller-retour interstellaire pensable.
inline constexpr double REID_PER_SV_ADULT_WORKER = 0.041;   // ICRP, par Sv
inline constexpr double DDREF_CHRONIC            = 2.0;     // ICRP, faible débit

// Risque de décès radio-induit pour une dose CHRONIQUE cumulée. Borné à 1 :
// au-delà, la linéarité n'a plus de sens et le modèle ne doit pas rendre 3.
inline double reid_from_chronic_sv(double sv) {
  if (sv <= 0.0) return 0.0;
  return std::min(1.0, sv * REID_PER_SV_ADULT_WORKER / DDREF_CHRONIC);
}

// ET LA LIMITE INSTITUTIONNELLE CESSE D'ÊTRE UN NOMBRE NU. 1 Sv, c'est
// `reid_from_chronic_sv(1.0)` ≈ **2,1 %** — à comparer aux **3 % de REID** qui
// sont la norme de la NASA. La constante d'Annexe B tombe donc dans la bonne
// bande pour une RAISON, et non parce qu'elle est ronde.
inline double reid_at_career_limit() { return reid_from_chronic_sv(CAREER_DOSE_LIMIT_SV); }

struct DoseAccumulator {
  double mission_sv{0.0};
  double mission_acute_gy{0.0};   // aigu (SPE) : compte séparément pour l'effet santé
  double career_sv{0.0};

  void add_chronic(double sv)  { mission_sv += sv; career_sv += sv; }
  void add_acute_gy(double gy, double quality = SPE_QUALITY_FACTOR) {
    mission_acute_gy += gy;
    mission_sv += gy * quality;
    career_sv  += gy * quality;
  }
  bool career_exceeded() const { return career_sv >= CAREER_DOSE_LIMIT_SV; }
  bool acute_sickness() const  { return mission_acute_gy >= ACUTE_SICKNESS_GY; }
  bool acute_lethal() const    { return mission_acute_gy >= ACUTE_LETHAL_GY; }

  // LE RISQUE QUE LA MISSION A FAIT COURIR, et celui de toute la carrière. La
  // part AIGUË est retirée du cumul chronique avant conversion : elle a déjà été
  // jugée par son propre barème (DL50), la compter deux fois surestimerait.
  double reid_mission() const {
    return reid_from_chronic_sv(mission_sv - mission_acute_gy * SPE_QUALITY_FACTOR);
  }
  double reid_career() const { return reid_from_chronic_sv(career_sv); }
};

// --- Source réacteur [GDD 5.12.8] --------------------------------------------
// Un cœur nucléaire irradie son propre équipage. Shadow shield + distance :
// la dose décroît en 1/d² derrière le cône d'ombre. Modèle déclaré.
struct ReactorSource {
  double p_thermal_w{};
  double shadow_shield_gcm2{50.0};
  double separation_m{20.0};       // longueur de mât : PÈSE sur la structure
};
inline double reactor_crew_dose_sv_day(const ReactorSource& r) {
  // 1 MWth non blindé à 10 m ~ 1 Sv/j (ordre déclaré), atténué exponentiellement
  // par le shadow shield (λ=16 g/cm², gamma+neutrons) et en 1/d².
  const double base = (r.p_thermal_w / 1.0e6) * 1.0 * (100.0 / (r.separation_m * r.separation_m));
  return base * std::exp(-r.shadow_shield_gcm2 / 16.0);
}

} // namespace fen::env
