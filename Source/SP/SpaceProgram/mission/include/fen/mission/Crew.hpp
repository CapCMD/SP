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
#include <string>
#include "fen/core/Constants.hpp"
#include "fen/env/Radiation.hpp"     // le second consommable d'un équipage : la dose
#include "fen/mission/Events.hpp"    // FlightPhase : où l'on est décide ce qu'on prend
#include "fen/rel/Relativity.hpp"    // ... y compris le rythme de sa propre horloge

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
  // « Recyclage quasi fermé » — le palier haut de la branche 4. Les deux chiffres
  // étaient DÉJÀ déclarés dans les commentaires ci-dessus depuis le premier jour ;
  // ils deviennent ici un état atteignable au lieu d'une note de bas de page.
  static RecyclingLoops advanced() { return {0.93, 0.85}; }
};

// ═══ LE RECYCLAGE EST UN RÉSULTAT DE L'ARBRE, PAS UN RÉGLAGE ═══ [GDD 5.10]
// « Indispensable pour la Lune durable, le cislunaire, Mars et toute mission sans
// évacuation immédiate — AUSSI IMPORTANTE QUE LE MOTEUR pour le vol habité
// lointain » [GDD 5.10, 19.1]. Cette phrase n'était vraie nulle part tant que les
// boucles ne pesaient rien : elle le devient parce que la masse de consommables
// entre dans Tsiolkovsky (voir `crew_consumables` et `MissionPlan::evaluate`).
// Les deux nœuds sont ceux de `app/ares.hpp` : `recyclage_partiel` (TRL 7,
// acquis au départ) et `recyclage_ferme` (TRL 3, 900 j / 200 M€, rang Principal).
inline RecyclingLoops loops_from_tech(bool recyclage_partiel, bool recyclage_ferme) {
  if (recyclage_ferme)   return RecyclingLoops::advanced();
  if (recyclage_partiel) return RecyclingLoops::iss();
  return RecyclingLoops::none();
}

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

// ═══ QUI EST À BORD, ET COMBIEN DE TEMPS ═══ [GDD 9.4, 12.1]
// Un effectif d'équipage s'emprunte à une mission de référence RÉELLE, exactement
// comme une poussée s'emprunte à un moteur réel [GDD 12.1] : « à partir de pièces
// réelles ou extrapolées de lignées réelles, JAMAIS génériques ». Sources en
// clair, une par ligne, pour qu'un chiffre faux soit réfutable.
inline int crew_size_for_family(const std::string& family) {
  if (family == "habite")      return 7;   // équipage permanent d'un incrément ISS
  if (family == "service")     return 7;   // STS-125, dernière mission de service Hubble
  if (family == "mars_habite") return 6;   // NASA Design Reference Architecture 5.0
  if (family == "lunaire_habite") return 3; // Apollo : trois a bord, deux au sol
  // Aucune mission relativiste n'a jamais volé : pas de source. On prend l'analogue
  // sourcé le plus proche (croisière habitée longue durée) et on le DÉCLARE, plutôt
  // que d'inventer un chiffre qui aurait l'air d'en être un [GDD 12.5].
  if (family == "relativiste") return 6;
  return 0;                                // robotique : personne à bord
}

// DURÉE D'OCCUPATION des familles dont la chronologie ne date PAS le vol : ce
// n'est alors pas un transit mais un SÉJOUR, et un séjour a lui aussi sa
// référence réelle.
// L'INCRÉMENT ISS — la référence de ce qu'est un tour de service NORMAL en vol
// habité. Sert deux fois : la durée d'un séjour en orbite basse, et la frontière
// entre une mission ordinaire et une mission LONGUE (voir `mission_longue`).
inline constexpr double INCREMENT_ISS_JOURS = 180.0;

inline double crew_stay_days_for_family(const std::string& family) {
  if (family == "habite")  return INCREMENT_ISS_JOURS;  // incrément ISS (~6 mois)
  if (family == "service") return 12.9;                 // durée de vol de STS-125
  return 30.0;                                          // séjour court, DÉCLARÉ
}

// ═══ COMBIEN DE TEMPS L'ÉQUIPAGE EST-IL LOIN DE CHEZ LUI ═══ [GDD 9.4]
// `round_trip_days` > 0 ⇒ la mission a une cible datée et l'équipage REVIENT :
// la durée est celle de l'aller-retour, que l'appelant tire de la géométrie
// (période synodique — voir `crew_round_trip_days` dans MissionLoop.hpp). Sinon,
// c'est un séjour, et la référence ci-dessus s'applique.
//
// POURQUOI L'ALLER SEUL NE SUFFIT PAS, et c'est le point : dimensionner les
// vivres sur le transit aller donnerait un chiffre CALCULÉ mais FAUX — l'équipage
// mourrait au retour. Une approximation identifiée est autorisée, une
// approximation déguisée en certitude ne l'est pas [GDD 12.5, 19.6].
inline double crew_occupation_days(const std::string& family, double round_trip_days) {
  if (round_trip_days > 0.0) return round_trip_days;
  return crew_stay_days_for_family(family);
}

// ═══ QU'EST-CE QU'UNE MISSION « LONGUE » ═══ [GDD 9.2, décision 12]
// « Les missions LONGUES sont réservées à la fin de carrière. » Le mot appelle
// une frontière, et elle n'est pas libre : au-delà d'un tour de service normal —
// l'incrément ISS, le seul étalon réel de « une rotation d'équipage ordinaire » —
// on ne part plus en mission, on quitte son poste. C'est la lecture littérale de
// la suite de la phrase : « le personnage ne quitte ARES que lorsqu'il n'a plus
// de carrière à construire ».
inline bool mission_longue(const std::string& family, double round_trip_days) {
  return crew_occupation_days(family, round_trip_days) > INCREMENT_ISS_JOURS;
}

// ═══ CE QU'IL FAUT BLINDER : LA GÉOMÉTRIE DE L'HABITAT ═══ [GDD 6.6, 19.1]
// « L'arbitrage masse / protection / mission » de [GDD 6.6] n'existe que si le
// blindage PÈSE, et il ne pèse qu'au prorata d'une SURFACE. Celle-ci n'est pas un
// chiffre de design : elle découle de l'équipage.
//
// Un seul nombre sourcé — le VOLUME HABITABLE PAR PERSONNE. Référence NASA pour
// les missions de longue durée : ~25 m³/personne est la limite basse acceptable
// (l'ISS en offre ~60, mais ce n'est pas un véhicule de transit). Le reste est de
// la géométrie : un module pressurisé est un CYLINDRE, et son élancement est
// déclaré à L = 2·D — celui d'un Destiny (8,5 m pour 4,2 m) à 1 % près.
inline constexpr double VOLUME_HABITABLE_M3_PAR_PERSONNE = 25.0;

// Surface à blinder (m²) pour n personnes. V = π r² L avec L = 4r ⇒ r = (V/4π)^⅓.
// Le volume par personne est une DÉCISION D'ARCHITECTE [GDD 3.1] : « l'Architecte
// décide COMMENT concevoir ». Serrer l'habitat allège la structure ET la surface
// à blinder — et se paie en tenue d'équipage sur les longues durées.
inline double surface_habitat_m2(int n_crew,
                                 double m3_par_personne = VOLUME_HABITABLE_M3_PAR_PERSONNE) {
  if (n_crew <= 0 || m3_par_personne <= 0.0) return 0.0;
  const double v = n_crew * m3_par_personne;
  const double r = std::cbrt(v / (4.0 * cst::PI));
  const double l = 4.0 * r;
  return 2.0 * cst::PI * r * r + 2.0 * cst::PI * r * l;   // 2 fonds + latéral
}

// ═══ CE QUE PÈSE UN HABITAT PRESSURISÉ ═══ [GDD 3.1, 12.1]
// ARES dit « il faut aller là pour faire ça » ; c'est l'ARCHITECTE qui en déduit
// le véhicule. La masse de l'habitat n'a donc rien à faire dans les termes du
// contrat — elle est une CONSÉQUENCE de deux décisions (combien de personnes,
// combien de volume chacune) et d'un fait d'ingénierie.
//
// LE FAIT, ET IL EST REMARQUABLEMENT STABLE — modules pressurisés de l'ISS :
//     Destiny  14 515 kg / 106 m³ = 137 kg/m³
//     Columbus 10 275 kg /  75 m³ = 137 kg/m³
//     Kibo JPM 15 900 kg / 116 m³ = 137 kg/m³
// Trois modules, trois agences, trois décennies de conception : 137 kg/m³ à 1 %
// près. Ce n'est pas une coïncidence, c'est ce que coûte une coque pressurisée
// qualifiée pour du vol habité. Le blindage AJOUTÉ se compte à part [GDD 6.6].
inline constexpr double MASSE_HABITAT_KG_PAR_M3 = 137.0;

inline double masse_habitat_kg(int n_crew,
                               double m3_par_personne = VOLUME_HABITABLE_M3_PAR_PERSONNE) {
  if (n_crew <= 0 || m3_par_personne <= 0.0) return 0.0;
  return n_crew * m3_par_personne * MASSE_HABITAT_KG_PAR_M3;
}

// ═══ LA COQUE BLINDE DÉJÀ ═══ [GDD 6.6, 12.1]
// Un équipage n'est JAMAIS nu dans le vide : la structure, les équipements et les
// consommables qui l'entourent constituent un blindage de fait. Le modèle partait
// de zéro, ce qui décrivait un astronaute exposé sans véhicule — et rendait toute
// éruption létale quel que soit le soin apporté à l'architecture.
// Ordre de grandeur RÉEL d'une capsule habitée (module de commande Apollo,
// ~7-8 g/cm² en moyenne sur l'angle solide). Aluminium : pauvre en hydrogène.
inline constexpr double COQUE_STRUCTURE_GCM2 = 7.5;
inline constexpr double COQUE_HYDROGENE01    = 0.3;

// LE BLINDAGE RÉELLEMENT VU PAR L'ÉQUIPAGE : la coque, plus ce que le joueur a
// PAYÉ. La distinction compte pour la masse — la structure est déjà dans la masse
// sèche du véhicule, seul l'ajout se facture (voir `masse_blindage_kg`).
inline env::Shielding blindage_effectif(const env::Shielding& ajoute) {
  env::Shielding s;
  s.areal_density_gcm2 = COQUE_STRUCTURE_GCM2 + ajoute.areal_density_gcm2;
  // Richesse en hydrogène moyennée au prorata des densités surfaciques : ajouter
  // du polyéthylène améliore vraiment la qualité du blindage, pas seulement sa
  // quantité — et c'est la raison pour laquelle on choisit ce matériau.
  const double total = s.areal_density_gcm2;
  s.hydrogen_richness =
      total > 0.0 ? (COQUE_STRUCTURE_GCM2 * COQUE_HYDROGENE01
                     + ajoute.areal_density_gcm2 * ajoute.hydrogen_richness) / total
                  : COQUE_HYDROGENE01;
  return s;
}

// MASSE DU BLINDAGE (kg). La densité surfacique est en g/cm², l'unité qui compte
// en radioprotection : 1 g/cm² = 10 kg/m². Rien d'autre à convertir.
//
// ET C'EST BRUTAL, comme dans la réalité : 20 g/cm² autour de six personnes
// pèsent une trentaine de tonnes. C'est précisément pourquoi [GDD 6.6] parle d'un
// VERROU et non d'une option, et pourquoi les vrais projets préfèrent blinder un
// abri anti-tempête plutôt que tout l'habitat.
inline double masse_blindage_kg(int n_crew, double areal_density_gcm2,
                                double m3_par_personne = VOLUME_HABITABLE_M3_PAR_PERSONNE) {
  if (n_crew <= 0 || areal_density_gcm2 <= 0.0) return 0.0;
  return surface_habitat_m2(n_crew, m3_par_personne) * areal_density_gcm2 * 10.0;
}

// LE BUDGET VITAL D'UNE MISSION, prêt à peser dans Tsiolkovsky. Rend un budget
// nul pour une mission robotique : personne à nourrir, donc pas un gramme.
// L'EFFECTIF EST DONNÉ PAR L'OBJECTIF (`Contract::crew_required`), pas relu dans
// une table : c'est ARES qui dit combien de personnes doivent être là-bas
// [GDD 3.1]. La DURÉE, elle, reste une conséquence de la famille — un séjour et
// un transit ne se comptent pas pareil.
inline VitalBudget crew_consumables(int n_crew, const std::string& family,
                                    double round_trip_days, const RecyclingLoops& loops,
                                    double margin_frac = 0.15) {
  if (n_crew <= 0) return {};
  return vital_budget(n_crew, crew_occupation_days(family, round_trip_days), loops,
                      margin_frac);
}
// Surcharge de commodité pour les oracles de modèle pur, qui n'ont pas de
// contrat sous la main : l'effectif de RÉFÉRENCE de la famille.
inline VitalBudget crew_consumables(const std::string& family, double round_trip_days,
                                    const RecyclingLoops& loops,
                                    double margin_frac = 0.15) {
  return crew_consumables(crew_size_for_family(family), family, round_trip_days, loops,
                          margin_frac);
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

// ARMER LES SOUTES depuis le budget provisionné à la conception. La capacité
// d'épuration du CO2 se dimensionne sur la MÊME durée que les vivres : un vol
// qui a de quoi manger mais plus de quoi respirer n'est pas un vol provisionné.
// Le CO2 ne se « recycle » pas au sens des boucles ci-dessus — il s'épure, et la
// cartouche s'épuise ; c'est pourquoi il n'entre pas dans `RecyclingLoops`.
inline VitalState arm_vitals(const VitalBudget& b, int n_crew, double duration_days,
                             double margin_frac = 0.15,
                             const MetabolicRates& met = {}) {
  VitalState s;
  s.o2_kg = b.o2_kg; s.water_kg = b.water_kg; s.food_kg = b.food_kg;
  // MÊME marge que `vital_budget` : trois soutes provisionnées à 1,15 et une
  // quatrième à 1,00 feraient du CO2 le facteur limitant de TOUS les vols, ce qui
  // serait un artefact de code et non un fait d'ingénierie.
  s.co2_scrub_capacity_kg = met.co2_out_kg * n_crew * duration_days * (1.0 + margin_frac);
  return s;
}

// ═══ CE QUE L'ÉQUIPAGE PREND, ET OÙ ═══ [GDD 6.6, 7.7]
// Le débit de dose GCR dépend de la FRACTION DE CIEL OUVERTE : `Radiation.hpp`
// l'appelle `geometry_factor` et donne les deux chiffres déclarés — 0,4 en orbite
// basse (la magnétosphère et la Terre masquent la moitié du ciel), 0,5 sur un sol
// planétaire (2π masqués), 1,0 en espace libre. On les LIT sur la phase de vol,
// qui est déjà dérivée de la chronologie : rien de nouveau à renseigner.
inline double facteur_geometrie_ciel(FlightPhase p) {
  switch (p) {
    case FlightPhase::Ground:      return 0.0;   // au sol : l'atmosphère protège
    case FlightPhase::Launch:
    case FlightPhase::LeoOps:      return 0.4;   // LEO, sous la magnétosphère
    case FlightPhase::Edl:
    case FlightPhase::SurfaceOps:  return 0.5;   // le sol masque un hémisphère
    default:                       return 1.0;   // croisière, manœuvre : plein ciel
  }
}

// LA DOSE CHRONIQUE PRISE PENDANT `dt_days`, blindage compris. `activity01` vient
// du cycle solaire : les GCR sont ANTI-corrélés à l'activité (au maximum solaire,
// l'héliosphère les repousse) — c'est `env::gcr_modulation` qui porte le signe,
// et c'est pourquoi on ne le recalcule pas ici.
inline double dose_chronique_sv(double dt_days, FlightPhase p,
                                const env::Shielding& s, double activity01) {
  const double geo = facteur_geometrie_ciel(p);
  if (geo <= 0.0 || dt_days <= 0.0) return 0.0;
  return env::gcr_dose_rate_sv_day(env::gcr_modulation(activity01), s, geo) * dt_days;
}

// ═══ ET LE TEMPS QU'IL Y VIT N'EST PAS CELUI DU CALENDRIER ═══ [GDD 6.7, 14.4]
// Même structure que `facteur_geometrie_ciel` juste au-dessus, et pour la même
// raison : OÙ L'ON EST décide de ce qu'on prend — de la dose là-haut, du rythme
// de l'horloge ici. `rel::DualClock` existait, était sauvegardé, et n'était
// AVANCÉ NULLE PART : le vieillissement différentiel que [GDD 3.4] fait peser sur
// la passation valait rigoureusement zéro, quelle que soit la mission.
//
// LA GÉOMÉTRIE EST GELÉE AU DÉPART, comme le blindage et la fiabilité : ce sont
// les demi-grands axes réellement lus sur les éphémérides à l'embarquement. Les
// figer rend le vol REJOUABLE (une sauvegarde rechargée bat au même rythme) et
// évite de traîner une éphéméride dans le tick.
struct GeometrieHorloge {
  double a_terre_m{0.0};       // demi-grand axe héliocentrique de la Terre
  double a_croisiere_m{0.0};   // ... du véhicule en croisière (ellipse de transfert)
  double a_sejour_m{0.0};      // ... du corps visité, pendant le séjour
  double r_parking_m{0.0};     // rayon de l'orbite terrestre de parking (phases LEO)
  // ═══ ET SI LE VAISSEAU N'EST PLUS SUR UNE ORBITE ═══ [GDD 6.7.2, 19.3]
  // β de croisière, tiré de l'antimatière RÉELLEMENT embarquée. Au-delà du seuil
  // du GDD, la croisière n'est plus képlérienne — elle s'échappe — et les
  // moyennes ⟨1/r⟩ = 1/a n'ont plus de sens. Vaut 0 pour toute mission chimique,
  // nucléaire ou électrique, c'est-à-dire pour toutes celles du catalogue actuel.
  double beta_croisiere{0.0};
  bool valide() const { return a_terre_m > 0.0; }
};

// dτ_bord / dτ_Terre selon la phase. DEUX RÉGIMES, jamais additionnés :
//   . près de la Terre, l'horloge de bord se compare au GÉOÏDE, et le mouvement
//     héliocentrique — que les deux partagent — se simplifie exactement ;
//   . en croisière ou en séjour, elle se compare à une horloge PORTÉE PAR LA
//     TERRE sur son orbite, et c'est le Soleil qui fait le potentiel.
// APPROXIMATIONS DÉCLARÉES [GDD 6.8], toutes deux mesurées plutôt qu'affirmées :
//   . le potentiel de surface du corps VISITÉ est négligé (Mars : 1,4e-10, soit
//     4,4 ms par année de séjour — trois ordres sous le seuil d'affichage) ;
//   . l'excentricité n'intervient pas, non par simplification mais parce que les
//     moyennes ⟨1/r⟩ = 1/a et ⟨v²⟩ = μ/a sont EXACTES pour toute excentricité.
inline double rapport_horloge_bord(FlightPhase p, const GeometrieHorloge& g) {
  if (!g.valide()) return 1.0;
  switch (p) {
    case FlightPhase::Ground:
      return 1.0;                       // même horloge que le monde : rien à dire
    case FlightPhase::Launch:
    case FlightPhase::LeoOps:
      return rel::rapport_horloge_orbite_terrestre(g.r_parking_m);
    case FlightPhase::Edl:
    case FlightPhase::SurfaceOps:
      return rel::rapport_horloges_kepler(g.a_sejour_m, g.a_terre_m, cst::MU_SUN);
    default:
      // ═══ DEUX RÉGIMES DE CROISIÈRE, ET LE SEUIL DU GDD LES SÉPARE ═══
      // Au-delà de β = 0,1, le vaisseau ne suit plus une ellipse héliocentrique :
      // il s'échappe, et parler de son demi-grand axe n'a plus de sens. Le
      // potentiel (1e-8) est alors écrasé par la cinématique (γ−1 ≥ 0,5 %, soit
      // cinq cents mille fois plus) — on bascule donc sur la relativité
      // restreinte PURE, exacte à tout β. En dessous du seuil, c'est l'inverse
      // qui est vrai, et c'est l'orbite qui commande.
      if (g.beta_croisiere >= rel::BETA_THRESHOLD)
        return 1.0 / rel::lorentz_gamma(g.beta_croisiere);
      return rel::rapport_horloges_kepler(g.a_croisiere_m, g.a_terre_m, cst::MU_SUN);
  }
}

// --- CommsDelayModel [GDD 9.5] -----------------------------------------------
// Délai lumière UN SENS. L'aller-retour (question -> réponse sol) double.
inline double comms_delay_s(double distance_m) { return distance_m / cst::C_LIGHT; }
inline double comms_roundtrip_s(double distance_m) { return 2.0 * comms_delay_s(distance_m); }

// Au-delà de ~60 s aller-retour, le sol ne peut plus "piloter" une anomalie :
// seuil d'autonomie de décision affiché au joueur (déclaré).
inline bool ground_loop_realtime(double distance_m) {
  return comms_roundtrip_s(distance_m) < 60.0;
}

// ═══ LA BOUCLE SOL SE FERME-T-ELLE SUR CETTE PHASE ? ═══ [GDD 9.6, 15.3]
// « Le logiciel de vol embarqué prépare l'autonomie QUAND LE SOL EST HORS DE
// PORTÉE. » Ce prédicat dit quand. Pour agir sur une phase, le sol doit voir,
// décider et commander DANS la fenêtre : il lui faut donc un aller-retour court
// devant la durée propre de la phase. Aucun seuil libre ici — on compare deux
// grandeurs physiques, le temps de la lumière et la durée de la manœuvre.
//
// Ce que ça donne, et ce sont les faits réels :
//   . amarrage en orbite basse — 0,03 s d'aller-retour contre des heures : le
//     sol est dans la boucle, et c'est ainsi que se conduisent les opérations
//     LEO ;
//   . EDL martienne — ~26 min contre 7 min de descente : la boucle NE SE FERME
//     PAS. C'est la raison, non négociable, pour laquelle tout atterrisseur
//     martien descend sous le contrôle de son propre logiciel, et pourquoi le
//     sol de MSL a regardé « seven minutes of terror » sans pouvoir rien faire.
// Une phase sans durée opposable (croisière) rend `false` : on ne « conduit »
// pas une croisière, on y tient des rendez-vous datés, préparés à l'avance —
// c'est justement pourquoi une TCM se commande très bien depuis le sol.
inline bool ground_loop_closes(double distance_m, double phase_duration_s) {
  if (phase_duration_s <= 0.0) return false;
  return comms_roundtrip_s(distance_m) < phase_duration_s;
}

// ═══ LA MISSION VÉCUE [GDD 9, décision 18] ═══
// « Vol habité vécu INCLUS ; commande médiée par le terminal. » Le joueur monte
// à bord, et à partir de là il est responsable scientifique de la mission
// [GDD 9.1] pendant qu'ARES tourne sans lui [GDD 9.3].
//
// REMPLACE `CrewMissionSlot`, qui portait la seule règle de simultanéité et un
// `mission_index` — un INDICE de tableau, fragile dès qu'une mission se termine
// et que le vecteur se réordonne. Tout le reste du modèle désigne une mission par
// `contract.id` ; celui-ci le fait aussi. L'ancien slot n'était de toute façon
// écrit par personne : il n'y a rien à migrer.
//
// ÉTAT, pas dérivation : une absence est un FAIT daté (on est parti tel jour) que
// rien d'autre ne porte. Il se sauvegarde donc, contrairement à la phase de vol
// qui, elle, se relit de la chronologie.
struct LivedMission {
  bool        active{false};
  std::string mission_id;            // laquelle est VÉCUE — une seule à la fois
  double      depart_days{0.0};      // date de jeu de l'embarquement
  int         n_crew{0};
  RecyclingLoops loops{};            // les boucles réellement embarquées
  VitalState  vitals{};              // ce qui reste à bord, MAINTENANT

  // ═══ CE QUI EST GELÉ PENDANT L'ABSENCE ═══ [GDD 9.3]
  // « La chaîne de fin de partie financière est SUSPENDUE et la confiance GELÉE
  // à sa valeur de départ : aucune faillite ni perte de crédibilité ne peut
  // survenir en l'absence du joueur. » On retient donc la valeur du départ ; le
  // gel est REPOSÉ à chaque tick plutôt que gardé par une porte, parce qu'une
  // porte s'oublie et qu'un état qu'on restaure ne s'oublie pas.
  double      confidence_frozen{0.0};
  int         missions_at_departure{0};   // pour le journal de reconstitution

  // ═══ CE QUI EST TOMBÉ EN PANNE ═══ [GDD 9.5, 9.1]
  // Les avaries en cours, et jusqu'où les événements ont été tirés. Le tirage se
  // fait par FENÊTRES D'UN JOUR indexées sur le calendrier : le même vol rejoué
  // donne les mêmes pannes aux mêmes dates, quelle que soit la cadence ou la
  // fréquence d'image — même exigence que les sous-pas du temps [GDD 14.2].
  // (Le type `Avarie` vit dans `Avaries.hpp`, qui inclut ce fichier : on garde
  // ici l'INDEX de tirage, et le vecteur d'avaries voyage à côté, sur GameState.)
  double     jour_evenements_tire{-1.0};
  // La fiabilité du VÉHICULE, figée à l'embarquement depuis le plan : elle module
  // les taux de panne interne [GDD 12.3]. Un système qualifié tombe moins.
  double     fiabilite_systeme{0.98};

  // ═══ LE BLINDAGE EMBARQUÉ ═══ [GDD 6.6]
  // Celui de la conception : on ne le change pas en vol. La DOSE, elle, ne vit
  // PAS ici — elle appartient au personnage (`GameState::dose_architecte`) et
  // doit survivre au débarquement : « un personnage consommé ne revole pas »
  // [GDD 6.6] n'aurait aucun sens si le compteur repartait de zéro à chaque
  // retour. C'est la distinction que `DoseAccumulator` porte déjà lui-même entre
  // `mission_sv` et `career_sv` ; il suffisait de le ranger au bon endroit.
  env::Shielding blindage{};

  // ═══ LA PRÉPARATION DE L'ÉQUIPAGE, ELLE AUSSI FIGÉE ═══ [GDD 11.6, 9.4]
  // `station::effects` calculait depuis toujours l'effet du module médical de
  // Novellus (0,6 : « réduit les urgences médicales en vol »), un oracle le
  // vérifiait — et `EventContext::medical_risk_factor` était écrit en dur à 1,0
  // dans le tick. Le module coûtait 110 M€ pour rigoureusement rien.
  // GELÉ AU DÉPART, et pas lu en vol, pour la même raison que la fiabilité : ce
  // que le facteur représente est un ENTRAÎNEMENT reçu AVANT le décollage. Le lire
  // en direct laisserait l'adjoint changer les taux de panne d'un vol en cours en
  // démontant un module — et casserait la rejouabilité, puisqu'une sauvegarde
  // rechargée dans un autre état de station ne tirerait plus les mêmes urgences.
  double facteur_risque_medical{1.0};

  // ═══ ET LE RYTHME DES HORLOGES ═══ [GDD 6.7, 14.4]
  // Les demi-grands axes lus sur les éphémérides à l'embarquement (voir
  // `GeometrieHorloge`). Gelés pour que le vol soit rejouable au battement près.
  GeometrieHorloge horloge{};

  bool try_embark(const std::string& id) {
    if (active) return false;             // [GDD 9.2] une seule à la fois
    active = true; mission_id = id; return true;
  }
  void disembark() { active = false; mission_id.clear(); }

  // Jours de vivres restants au rythme courant — LA télémétrie de [GDD 9.1].
  double days_left() const { return vitals.days_left(n_crew, loops); }
  // L'équipage est-il encore en vie ? Une réserve épuisée est une urgence, pas
  // une jauge qui clignote [GDD 9.4].
  bool consumables_exhausted() const { return active && days_left() <= 0.0; }
};

} // namespace fen::mission
