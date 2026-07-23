// fen/mission/Crew.hpp — missions habitées [GDD 9, 13.4]
//
// UNE SEULE mission habitée vécue à la fois [GDD 9.2]. Le joueur calcule TOUTES
// les ressources vitales (O2, eau, nourriture) — métier réel d'architecte
// mission [GDD 9.3]. Le recyclage RÉDUIT les besoins bruts sans JAMAIS les
// annuler. Le délai de communication est le délai lumière VRAI : distance
// instantanée / c [GDD 9.5] — l'autonomie de décision croît avec l'éloignement.
#pragma once
#include <algorithm>
#include <cmath>
#include "fen/core/Constants.hpp"

namespace fen::mission {

// --- Besoins métaboliques par personne et par jour ---------------------------
// Valeurs type NASA BVAD, déclarées comme référence de modèle.
struct MetabolicRates {
  double o2_kg{0.84};
  double co2_out_kg{1.04};
  double water_kg{3.6};        // boisson + hygiène + nourriture réhydratée
  double food_dry_kg{1.83};    // aliments + emballage
};

// --- Recyclage [GDD 9.3] -----------------------------------------------------
// Efficacités de BOUCLE, jamais 1.0 : le recyclage quasi fermé reste < 1
// (branche 4, Autonomie longue durée). La nourriture ne se recycle pas en V1.
struct RecyclingLoops {
  double water_recovery{0.0};   // 0 = tout consommable ; ISS ~0.87 ; avancé ~0.93
  double o2_recovery{0.0};      // Sabatier + électrolyse : ~0.5 ; avancé ~0.85
  static RecyclingLoops none() { return {0.0, 0.0}; }
  static RecyclingLoops iss()  { return {0.87, 0.50}; }
};

// --- VitalResourceModel ------------------------------------------------------
// Bilan de masse consommables pour n_crew pendant duration_days, marge comprise.
// La marge est une RESSOURCE DE MISSION [GDD 4.4] : elle pèse au décollage.
struct VitalBudget {
  double o2_kg{}, water_kg{}, food_kg{};
  double total_kg() const { return o2_kg + water_kg + food_kg; }
};

inline VitalBudget vital_budget(int n_crew, double duration_days,
                                const RecyclingLoops& loops,
                                double margin_frac = 0.15,
                                const MetabolicRates& met = {}) {
  const double pd = n_crew * duration_days * (1.0 + margin_frac);
  VitalBudget b;
  b.o2_kg    = met.o2_kg    * pd * (1.0 - std::clamp(loops.o2_recovery,    0.0, 0.95));
  b.water_kg = met.water_kg * pd * (1.0 - std::clamp(loops.water_recovery, 0.0, 0.95));
  b.food_kg  = met.food_dry_kg * pd;   // pas de boucle nourriture en V1
  return b;
}

// --- État vivant des consommables (suivi temps réel à bord [GDD 9.1]) --------
struct VitalState {
  double o2_kg{}, water_kg{}, food_kg{};
  double co2_scrub_capacity_kg{};   // capacité restante d'épuration CO2

  // Avance de dt jours ; renvoie faux si une ressource est épuisée (urgence).
  bool consume(int n_crew, double dt_days, const RecyclingLoops& loops,
               const MetabolicRates& met = {}) {
    const double pd = n_crew * dt_days;
    o2_kg    -= met.o2_kg    * pd * (1.0 - loops.o2_recovery);
    water_kg -= met.water_kg * pd * (1.0 - loops.water_recovery);
    food_kg  -= met.food_dry_kg * pd;
    co2_scrub_capacity_kg -= met.co2_out_kg * pd;
    return o2_kg > 0.0 && water_kg > 0.0 && food_kg > 0.0
        && co2_scrub_capacity_kg > 0.0;
  }
  // Jours restants au rythme courant — LA donnée de télémétrie habitée.
  double days_left(int n_crew, const RecyclingLoops& loops,
                   const MetabolicRates& met = {}) const {
    if (n_crew <= 0) return 1e18;
    double d = 1e18;
    const double o2r = met.o2_kg * (1.0 - loops.o2_recovery);
    const double h2or = met.water_kg * (1.0 - loops.water_recovery);
    if (o2r > 0)  d = std::min(d, o2_kg / (o2r * n_crew));
    if (h2or > 0) d = std::min(d, water_kg / (h2or * n_crew));
    d = std::min(d, food_kg / (met.food_dry_kg * n_crew));
    d = std::min(d, co2_scrub_capacity_kg / (met.co2_out_kg * n_crew));
    return d;
  }
};

// --- CommsDelayModel [GDD 9.5] -----------------------------------------------
// Délai lumière UN SENS. L'aller-retour (question -> réponse sol) double.
inline double comms_delay_s(double distance_m) { return distance_m / cst::C_LIGHT; }
inline double comms_roundtrip_s(double distance_m) { return 2.0 * comms_delay_s(distance_m); }

// Au-delà de ~60 s aller-retour, le sol ne peut plus "piloter" une anomalie :
// seuil d'autonomie de décision affiché au joueur (déclaré).
inline bool ground_loop_realtime(double distance_m) {
  return comms_roundtrip_s(distance_m) < 60.0;
}

// --- Simultanéité [GDD 9.2] --------------------------------------------------
struct CrewMissionSlot {
  bool active{false};
  std::size_t mission_index{};    // laquelle est VÉCUE (une seule à la fois)
  bool try_embark(std::size_t idx) {
    if (active) return false;
    active = true; mission_index = idx; return true;
  }
  void disembark() { active = false; }
};

} // namespace fen::mission
