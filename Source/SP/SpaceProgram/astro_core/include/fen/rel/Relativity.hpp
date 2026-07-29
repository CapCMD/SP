// fen/rel/Relativity.hpp — relativité restreinte [GDD 6.7, 19.4]
//
// INVARIANT : la dilatation est CALCULÉE depuis le profil de vitesse réel,
// jamais posée. En dessous du seuil (β < 0.1), γ ≈ 1 et le temps propre égale
// le temps de jeu : AUCUN effet n'est appliqué [GDD 6.7.2]. Seule une
// architecture antimatière de fin d'arbre franchit ce seuil [GDD 19.3].
//
// Isolé du chemin critique : rien ici n'est appelé par le propagateur.
// Golden tests analytiques : γ(β), τ = ∫dt/γ, m0/mf vs forme close [carte P3].
#pragma once
#include <algorithm>
#include <cmath>
#include <vector>
#include "fen/core/Constants.hpp"

namespace fen::rel {

// Seuil d'activation du régime relativiste [GDD 6.7.2] : en dessous,
// γ−1 < 0.5 % — imperceptible à l'échelle d'une vie de personnage.
inline constexpr double BETA_THRESHOLD = 0.10;

// SEUIL NARRATIF [GDD 6.7.2, v1.2] : distinct de l'effet MESURABLE. Un écart de
// 4,8 % (β=0,3) est mesurable par un instrument mais représente ~5 mois sur dix
// ans — invisible à l'échelle d'une vie. L'écart ne devient perceptible dans la
// narration et la carrière que vers β ≳ 0,7. Le design ne VISE jamais ce seuil ;
// il le CONSTATE si le joueur l'atteint.
inline constexpr double BETA_NARRATIVE = 0.70;

inline double beta(double v_mps) { return v_mps / cst::C_LIGHT; }

inline double lorentz_gamma(double b) {
  if (b <= 0.0) return 1.0;
  if (b >= 1.0) return 1e300;             // jamais atteint : garde numérique
  return 1.0 / std::sqrt(1.0 - b * b);
}

// γ − 1 sans cancellation aux petits β (β ~ 1e-4 → γ−1 ~ 5e-9, illisible en
// double via lorentz_gamma() - 1). Forme exacte : β² / (√(1−β²)·(1+√(1−β²))).
inline double gamma_minus_one(double b) {
  if (b <= 0.0) return 0.0;
  if (b >= 1.0) return 1e300;
  const double s = std::sqrt(1.0 - b * b);
  return b * b / (s * (1.0 + s));
}

inline bool is_relativistic(double v_mps) { return beta(v_mps) >= BETA_THRESHOLD; }

// --- ProperTimeIntegrator ----------------------------------------------------
// τ_bord = ∫ dt / γ(t) le long du profil de vitesse [GDD 6.7.1].
// Trapèze sur échantillons ordonnés en t. Suffisant : le profil est lisse
// (accélération continue) et le résultat n'entre dans AUCUNE boucle de retour
// physique — il ne pilote que les horloges (carrière, narration).
struct VelocitySample {
  double t{};      // s (TDB, temps terrestre)
  double v{};      // m/s (norme de la vitesse héliocentrique)
};

inline double proper_time(const std::vector<VelocitySample>& profile) {
  double tau = 0.0;
  for (std::size_t i = 1; i < profile.size(); ++i) {
    const double dt = profile[i].t - profile[i - 1].t;
    const double g0 = lorentz_gamma(beta(profile[i - 1].v));
    const double g1 = lorentz_gamma(beta(profile[i].v));
    tau += 0.5 * dt * (1.0 / g0 + 1.0 / g1);
  }
  return tau;
}

// --- RelativisticRocketModel [GDD 6.7.3] -------------------------------------
// Pour une vitesse d'éjection propre ve constante :
//   m0/mf = ((1+β)/(1−β))^(c/(2·ve))
// Cœur antimatière : ve effectif ≈ c/3 (photons γ et pions neutres perdus,
// aucune contribution à la poussée dirigée). Le verrou n'est PAS le ratio de
// masse (β=0.3 → ratio ~2.5) : c'est la PRODUCTION d'antimatière [GDD 19.3].
inline constexpr double VE_ANTIMATTER_EFF = cst::C_LIGHT / 3.0;

inline double mass_ratio(double beta_final, double ve) {
  if (beta_final <= 0.0) return 1.0;
  if (beta_final >= 1.0 || ve <= 0.0) return 1e300;
  return std::pow((1.0 + beta_final) / (1.0 - beta_final),
                  cst::C_LIGHT / (2.0 * ve));
}

// Inverse exact via la rapidité φ = (ve/c)·ln(m0/mf), β = tanh(φ).
inline double beta_from_mass_ratio(double m0_over_mf, double ve) {
  if (m0_over_mf <= 1.0) return 0.0;
  return std::tanh((ve / cst::C_LIGHT) * std::log(m0_over_mf));
}

// ═══ LE VERROU DE L'ALLER-RETOUR [GDD 6.7.4, v1.2] ═══
// Un aller-retour HABITÉ exige QUATRE poussées (accélération, décélération à
// l'arrivée, accélération au retour, décélération finale). Le ratio de masse
// total est donc le ratio unitaire ÉLEVÉ À LA PUISSANCE QUATRE. À β=0,5 :
// unitaire ~5,2, aller-retour ~730 ; à β=0,9 : ~83 → ~4,7×10⁷. « Un aller-retour
// à haute vitesse sans ravitaillement est physiquement hors de portée. »
inline double round_trip_mass_ratio(double beta_cruise, double ve) {
  const double single = mass_ratio(beta_cruise, ve);
  return single * single * single * single;   // quatre poussées
}
// Nombre de poussées d'une architecture : 1 (sonde en aller simple sans
// insertion), 2 (aller simple avec insertion/freinage), 4 (aller-retour habité).
inline double mass_ratio_for_burns(double beta_per_burn, double ve, int n_burns) {
  double r = 1.0;
  const double single = mass_ratio(beta_per_burn, ve);
  for (int i = 0; i < n_burns; ++i) r *= single;
  return r;
}

// ═══════════════════════════════════════════════════════════════════════════
// LA DESTINATION D'UNE MISSION RELATIVISTE [GDD 3.4, 9.3, 10.1]
// ═══════════════════════════════════════════════════════════════════════════
// LE GDD NE LA NOMMAIT PAS, ET SES CHIFFRES LA DÉSIGNAIENT POURTANT. [9.3] : une
// mission relativiste « peut couvrir plusieurs décennies terrestres » ; [3.4] :
// « β ≈ 0,9 → ~5 ans d'écart », ce qui à γ(0,9) = 2,294 suppose ~8,8 ans de vol.
// Des décennies à une fraction notable de c font des ANNÉES-LUMIÈRE : la cible
// est stellaire. Arbitrage tranché avec l'utilisateur (2026-07-29), parce que
// [17.3] bornait la scène au système solaire et que le document ne départageait
// pas — c'était donc un choix de périmètre, pas une déduction.
//
// FAIT MESURÉ, pas un réglage : Proxima Centauri, parallaxe Gaia DR3
// 768,0665 mas → 1,30197 pc → 4,2465 al. C'est l'étoile la plus proche du
// Soleil, donc le MINORANT absolu de toute distance interstellaire — aucune
// architecture ne peut faire mieux, et c'est ce qui en fait le bon repère.
inline constexpr double LIGHT_YEAR_M = 9.4607304725808e15;      // exact (an julien)
inline constexpr double PROXIMA_DISTANCE_LY = 4.2465;
inline constexpr double PROXIMA_DISTANCE_M = PROXIMA_DISTANCE_LY * LIGHT_YEAR_M;

// LE TRANSIT RELATIVISTE. Deux durées, et c'est tout l'intérêt : celle que la
// Terre compte, et celle que l'équipage vit.
struct RelativisticTransit {
  double t_earth_s{0.0};     // durée dans le référentiel du départ
  double tau_ship_s{0.0};    // temps propre à bord [GDD 6.7.1]
  double age_gap_s() const { return t_earth_s - tau_ship_s; }   // [GDD 3.4]
};

// APPROXIMATION DÉCLARÉE [GDD 6.8] — et elle a un SENS, elle borne :
// on modélise la traversée comme un TRAJET RECTILIGNE à β constant, poussées
// traitées comme impulsionnelles. C'est la même approximation que le régime
// impulsionnel de [GDD 6.3], mais ici elle est OPTIMISTE et il faut le dire : un
// cœur annihilant est en régime CONTINU [GDD 6.4, très faible densité de
// poussée], donc l'accélération n'est pas brève et le vol réel dure PLUS
// longtemps. La durée rendue est un MINORANT du temps de vol, et l'écart d'âge
// qu'elle donne un minorant de l'écart réel.
//
// LE TEMPS PROPRE N'EST PAS DIVISÉ, IL EST INTÉGRÉ : τ = ∫dt/γ [GDD 6.7.1], par
// `proper_time` sur le profil de vitesse — le seul point d'entrée, pour que le
// jour où le profil aura une rampe d'accélération, rien d'autre ne bouge.
inline RelativisticTransit relativistic_transit(double distance_m, double beta) {
  RelativisticTransit t;
  if (!(distance_m > 0.0) || !(beta > 0.0) || beta >= 1.0) return t;
  t.t_earth_s = distance_m / (beta * cst::C_LIGHT);
  const std::vector<VelocitySample> profil = {
      {0.0,         beta * cst::C_LIGHT},
      {t.t_earth_s, beta * cst::C_LIGHT}};
  t.tau_ship_s = proper_time(profil);
  return t;
}

// ═══ CHAÎNE ANTIMATIÈRE : masse ↔ β [GDD 5.12.12, 19.3] ═══
// Le VERROU n'est pas le ratio de masse (modeste) mais la QUANTITÉ d'antimatière
// à produire et confiner. Pour un cœur annihilant (ve ≈ c/3), le propergol est
// pour moitié de l'antimatière, pour moitié de la matière. La masse
// d'antimatière requise pour qu'un véhicule de masse sèche `m_dry_kg` atteigne
// `beta` (une poussée) : m_a = ½ · m_dry · (ratio − 1).
inline double antimatter_needed_g(double m_dry_kg, double beta_final,
                                  int n_burns = 1) {
  if (m_dry_kg <= 0.0 || beta_final <= 0.0) return 0.0;
  const double ratio = mass_ratio_for_burns(beta_final, VE_ANTIMATTER_EFF, n_burns);
  const double propellant_g = m_dry_kg * 1000.0 * (ratio - 1.0);
  return 0.5 * propellant_g;                  // moitié antimatière
}
// L'inverse : quel β un véhicule atteint avec `antimatter_g` disponibles.
// ═══ `n_burns` N'ÉTAIT PAS LÀ, ET C'ÉTAIT UN DÉFAUT ═══ [GDD 6.7.4]
// La fonction ALLER (`antimatter_needed_g`) connaissait le nombre de poussées ;
// la fonction RETOUR, non — elle rendait toujours le β d'un aller simple. Une
// asymétrie entre deux fonctions inverses l'une de l'autre, dans le même
// fichier : tout ce qui LISAIT un β le lisait donc surestimé, et d'autant plus
// que l'architecture devait freiner ou revenir. Or c'est précisément ce que le
// GDD appelle « le verrou de l'aller-retour » et qu'il chiffre au ratio à la
// puissance quatre. Le ratio total étant R^n, la rapidité — qui est ADDITIVE
// [GDD 6.7.3, Annexe A] — se divise simplement par n :
//     φ_par_poussée = (1/n) · (ve/c) · ln(R_total)
// et c'est l'inverse EXACT de `antimatter_needed_g` pour tout n.
inline double beta_from_antimatter(double m_dry_kg, double antimatter_g,
                                   int n_burns = 1) {
  if (m_dry_kg <= 0.0 || antimatter_g <= 0.0 || n_burns < 1) return 0.0;
  const double ratio_total = 1.0 + 2.0 * antimatter_g / (m_dry_kg * 1000.0);
  return beta_from_mass_ratio(std::pow(ratio_total, 1.0 / n_burns),
                              VE_ANTIMATTER_EFF);
}

// ═══ COMBIEN DE POUSSÉES UNE ARCHITECTURE DEMANDE-T-ELLE ═══ [GDD 6.7.4]
// « Quatre poussées (accélération, décélération, retour, freinage final) sans
// ravitaillement. » Ce n'est pas un réglage : cela se LIT sur la mission. Un
// équipage doit freiner à l'arrivée ET revenir — sinon ce n'est pas une mission,
// c'est un aller sans retour. Une sonde robotique en survol ne freine pas.
inline constexpr int BURNS_FLYBY       = 1;   // survol : on passe, on ne s'arrête pas
inline constexpr int BURNS_ONE_WAY     = 2;   // aller + insertion (on s'arrête là-bas)
inline constexpr int BURNS_ROUND_TRIP  = 4;   // + retour + freinage final [GDD 6.7.4]
inline int burns_for_architecture(bool crewed, bool stops_at_target = true) {
  if (crewed) return BURNS_ROUND_TRIP;              // un équipage revient
  return stops_at_target ? BURNS_ONE_WAY : BURNS_FLYBY;
}

// ═══════════════════════════════════════════════════════════════════════════
// LE PLANCHER DE PRODUCTION EST UNE LOI, PAS UN RÉGLAGE [GDD 5.12.12, 19.6]
// ═══════════════════════════════════════════════════════════════════════════
// On ne fabrique pas un antiproton sans fabriquer son proton : la création se
// fait par PAIRES. L'énergie minimale par gramme d'ANTImatière est donc celle
// de la paire, 2·m·c² = 1,7975e14 J/g, et aucun progrès technologique ne passe
// dessous — c'est de la conservation de l'énergie, pas une hypothèse.
//
// Tout ce que l'arbre peut déplacer est le RENDEMENT η ∈ (0,1] :
//     énergie par gramme = E_paire / η
//
// ANCRAGE RÉEL, ET IL CORRIGE UNE HYPOTHÈSE MUETTE : les usines à antiprotons
// d'aujourd'hui (CERN AD, ex-Tevatron) tournent à η ~ 1e-9. Or l'ancien défaut
// tabulé de 1e17 J/g SUPPOSAIT η = 1,8e-3 — six ordres de grandeur au-dessus du
// réel, c'est-à-dire une hypothèse de fin d'arbre posée au lieu d'être déduite,
// et surtout jamais DÉCLARÉE comme telle [GDD 6.8, 12.5]. Elle l'est maintenant.
inline constexpr double ANTIMATTER_PAIR_ENERGY_J_PER_G =
    2.0e-3 * cst::C_LIGHT * cst::C_LIGHT;               // 1,7975e14 J/g
inline constexpr double ANTIMATTER_EFFICIENCY_TODAY = 1.0e-9;   // CERN, ordre de grandeur

// LES PALIERS DE LA FILIÈRE — le levier d'équilibrage annoncé [GDD 5.12.12].
// « Rendement énergétique : couple la production à LA BRANCHE ÉNERGIE et au
// budget. » C'est une instruction de câblage, et elle n'était pas suivie : le
// débit se nourrissait de la MARGE DE PUISSANCE DE NOVELLUS (38 kW au départ,
// 5 MW au mieux), si bien qu'aucune recherche de branche 6 ne pouvait le
// changer. Une usine à antimatière n'est pas un module de station.
enum class AntimatterTier {
  None = 0,       // pas d'usine
  Fission,        // parc de fission spatial [GDD 5.12.8, palier 4]
  Fusion,         // fusion pilotée [GDD 5.12.11, palier 7]
  Mature          // filière antimatière aboutie [GDD 5.12.12, palier 8]
};

// LE MODÈLE DE PRODUCTION — le vrai paramètre d'équilibrage de la fin de jeu
// [GDD 5.12.12, v1.2]. Ce sont ces paramètres, et non un β cible, qui décident
// de la vitesse maximale réellement atteignable ; l'invariant est que produire
// est PLURIANNUEL.
struct AntimatterProduction {
  double production_efficiency{ANTIMATTER_EFFICIENCY_TODAY};  // η ∈ (0,1]
  double plant_power_w{0.0};               // puissance de l'USINE (branche 6)
  double cost_me_per_g{1.0e6};             // coût (M€/g) — hors échelle
  double confinement_capacity_g{1.0e-3};   // masse stockable en sécurité (plafond)
  double loss_rate_per_day{1.0e-2};        // perte de confinement (risque permanent)

  // L'énergie par gramme n'est plus un champ libre : c'est le plancher divisé
  // par le rendement. Un η ≥ 1 serait une machine à mouvement perpétuel.
  double energy_j_per_g() const {
    const double eta = std::clamp(production_efficiency, 1e-30, 1.0);
    return ANTIMATTER_PAIR_ENERGY_J_PER_G / eta;
  }

  // ═══ LE DÉBIT N'EST PAS UN PARAMÈTRE LIBRE : C'EST DE LA PUISSANCE ═══
  //     ṁ = P / (énergie par gramme)
  // et c'est ce qui fait de l'INFRASTRUCTURE le vrai levier, exactement ce que
  // le GDD annonce. Ce qui a changé, c'est OÙ la puissance est prise.
  double rate_from_power_g_yr(double power_w) const {
    if (power_w <= 0.0) return 0.0;
    return power_w * (365.25 * cst::DAY) / energy_j_per_g();
  }
  double rate_g_yr() const { return rate_from_power_g_yr(plant_power_w); }

  // « Le débit FIXE LA DURÉE D'ACCUMULATION avant qu'une mission soit possible »
  // [GDD 5.12.12]. Durée BRUTE, hors fuite : `AntimatterStock::years_to_reach`
  // est la vraie, celle qui sait dire « jamais ».
  double years_to_accumulate(double grams) const {
    const double r = rate_g_yr();
    return r > 0.0 ? grams / r : 1e300;
  }

  double energy_to_produce(double grams) const { return grams * energy_j_per_g(); }
  double cost_to_produce_me(double grams) const { return grams * cost_me_per_g; }
  // Le stock UTILE ne peut pas dépasser la capacité de confinement.
  double max_usable_stock_g() const { return confinement_capacity_g; }
  // Survie du stock sur `days` (processus de perte poissonien).
  double stock_survival(double days) const {
    return std::exp(-std::max(0.0, loss_rate_per_day) * std::max(0.0, days));
  }

  // ═══════════════════════════════════════════════════════════════════════
  // LA CALIBRATION DE FIN DE JEU [GDD Annexe E] — DÉDUITE, PAS CHOISIE
  // ═══════════════════════════════════════════════════════════════════════
  // L'Annexe E diffère ces nombres en nommant leur dépendance : « vitesse
  // maximale SOUHAITÉE en fin d'arbre ». Or le corps du GDD la dit, et à trois
  // endroits — l'annexe renvoie donc à une question à laquelle le document
  // répond déjà. On INVERSE ces énoncés au lieu d'inventer des paliers :
  //
  //   (i)   [5.12.11] la fusion est « la vraie transition vers le
  //         pré-relativiste : de très grandes vitesses SANS ENCORE rendre la
  //         dilatation significative » ⇒ au palier fusion, β doit rester SOUS
  //         le seuil de mesurabilité (β = 0,1 [6.7.2]) ;
  //   (ii)  [6.7.2] « seule l'antimatière FRANCHIT β ≳ 0,3 » ⇒ au palier
  //         abouti, β = 0,3 doit être ATTEIGNABLE ;
  //   (iii) [3.5] « atteindre la fin de la branche 6 demande souvent PLUSIEURS
  //         VIES » et [3.4] la mort naturelle vient vers 85 ans ⇒ la durée
  //         d'accumulation visée est de l'ordre de 120 à 150 ans ;
  //   (iv)  [5.12.12] le confinement « PLAFONNE le stock utile » — un plafond,
  //         pas une interdiction : il doit se tenir AU-DESSUS du besoin de (ii),
  //         faute de quoi il n'est pas un plafond mais un mur.
  //
  // C'est (iv) qui tranche la question restée ouverte : `confinement_capacity_g`
  // valait 1 g quand le besoin le plus modeste en réclame des tonnes. Ce n'était
  // donc pas « le régime relativiste est hors d'échelle » (thèse défendable)
  // mais « le réservoir a été dimensionné sans rapport avec ce qu'on y met ».
  //
  // ═══ L'ARCHITECTURE DE RÉFÉRENCE EST UN VOL HABITÉ ═══ [GDD 3.4, 14.4]
  // DÉCISION DE L'UTILISATEUR (2026-07-29) : « le relativisme n'a d'intérêt que
  // pour les vols habités ». Elle est juste, et elle est structurante — une
  // dilatation que PERSONNE NE VIT n'a aucune conséquence de jeu : [3.4] fait
  // peser l'écart d'âge sur la carrière et la passation, [14.4] sur le
  // vieillissement, et une sonde ne vieillit pas. Calibrer sur une sonde de 5 t
  // en survol (l'ancre précédente) revenait donc à calibrer sur le seul cas qui
  // ne sert à rien.
  //
  // On calibre désormais sur ce que le GDD veut voir arriver : SIX PERSONNES,
  // ALLER-RETOUR vers l'étoile la plus proche, donc `BURNS_ROUND_TRIP` [6.7.4].
  //
  // LA MASSE N'EST PAS CHOISIE, ELLE EST MESURÉE : c'est le point fixe de
  // `mission::bilan_relativiste` pour cette architecture (coque 20,6 t + blindage
  // 32,8 t à 20 g/cm² + 2 t de charge client, puis les vivres que la durée
  // impose) — 213 t. Elle est reproduite ici parce que `astro_core` ne peut pas
  // appeler `mission/` (couche supérieure) ; l'oracle vérifie qu'elles coïncident,
  // donc elle ne peut pas dériver en silence.
  static constexpr double CALIB_DRY_MASS_KG = 183000.0;   // point fixe mesuré
  static constexpr int    CALIB_BURNS = 4;                // aller-retour [6.7.4]
  static constexpr double CALIB_TARGET_BETA = 0.3;        // [GDD 6.7.2, 3.4]
  static constexpr double CALIB_HORIZON_YEARS = 160.0;    // [GDD 3.4, 3.5]

  static AntimatterProduction for_tier(AntimatterTier t) {
    AntimatterProduction p;
    switch (t) {
      case AntimatterTier::None:
        p.production_efficiency = ANTIMATTER_EFFICIENCY_TODAY;
        p.plant_power_w = 0.0;
        break;
      // PALIER FISSION — l'usine d'aujourd'hui, alimentée par un parc spatial de
      // 1 GW. Rendement RÉEL (CERN) : rien ne dit qu'on sache déjà mieux faire.
      case AntimatterTier::Fission:
        p.production_efficiency = ANTIMATTER_EFFICIENCY_TODAY;   // 1e-9
        p.plant_power_w = 1.0e9;
        p.confinement_capacity_g = 1.0e-3;
        p.loss_rate_per_day = 1.0e-2;
        break;
      // PALIER FUSION — la branche énergie fait son saut ; le rendement gagne
      // trois ordres par une usine dédiée. VÉRIFIÉ contre (i) : le stock
      // d'équilibre y achète β ~ 6e-7, très loin sous le seuil. La fusion reste
      // donc « pré-relativiste », comme 5.12.11 l'exige.
      case AntimatterTier::Fusion:
        p.production_efficiency = 1.0e-6;
        p.plant_power_w = 1.0e13;
        p.confinement_capacity_g = 1.0e3;
        p.loss_rate_per_day = 1.0e-3;
        break;
      // PALIER ABOUTI — fin d'arbre, et RECALIBRÉ pour l'ARCHITECTURE HABITÉE.
      // Le vol habité relativiste a un SEUIL D'EXISTENCE mesuré : sous 3,0e8 g,
      // le point fixe vivres ↔ vitesse diverge et aucun équipage ne part. Il en
      // faut 4,26e9 pour que six personnes atteignent β = 0,3 — l'ancienne
      // calibration (1e7 g de confinement) était 426 fois trop basse, et elle
      // l'était parce qu'elle visait une SONDE.
      // η = 1e-2 : « extrêmement spéculative » est le mot du GDD pour ce palier
      // [5.12.3], et 1 % reste sept ordres au-dessus du CERN — déclaré [12.5].
      // P = 2e16 W ≈ 11 % de l'ensoleillement reçu par la Terre : ce n'est pas un
      // module de station mais une industrie à l'échelle du système solaire, ce
      // que 5.12.12 appelle « un changement de régime physique, INDUSTRIEL et
      // narratif ». Confinement et fuite restent posés par (iv) : au-dessus du
      // besoin, mais assez serrés pour que ce soit encore la FUITE qui borne
      // (équilibre 9,6e9 contre 1e10 de réservoir).
      case AntimatterTier::Mature:
        p.production_efficiency = 1.0e-2;
        p.plant_power_w = 2.0e16;
        p.confinement_capacity_g = 1.0e10;
        p.loss_rate_per_day = 1.0e-5;
        break;
    }
    return p;
  }
};

// --- LE STOCK RÉEL [GDD 5.12.12, 19.3] ---------------------------------------
// `AntimatterProduction` décrivait un processus que RIEN n'exécutait : quatre
// paramètres, aucun gramme nulle part. Un stock d'antimatière est pourtant
// l'exemple type de la ressource qui ne se « possède » pas — elle se produit en
// continu et elle FUIT en continu, si bien que ce qu'on en a à un instant donné
// est un ÉQUILIBRE, pas un cumul.
//
//     dS/dt = ṁ − λ·S        (production constante, perte proportionnelle)
//
// INTÉGRÉE EXACTEMENT, jamais par un pas d'Euler :
//     S(t+dt) = S∞ + (S − S∞)·e^(−λ·dt)    avec S∞ = ṁ/λ
// Conséquence directe, et c'est la même exigence que les sous-pas du temps
// [GDD 14.2] : avancer d'un bloc ou par tranches donne EXACTEMENT le même stock.
struct AntimatterStock {
  AntimatterProduction prod{};
  double grams{0.0};

  // Le stock d'ÉQUILIBRE : là où la fuite mange toute la production. C'est le
  // plafond RÉEL — et il n'a aucune raison d'égaler la capacité de confinement.
  // LA PUISSANCE N'EST PLUS UN ARGUMENT : elle appartient à l'usine, donc au
  // palier de branche 6, et non à l'appelant. Tant qu'elle venait du dehors,
  // chaque site d'appel pouvait passer la marge de la station et personne ne
  // voyait que le levier annoncé n'était pas branché sur son levier.
  double equilibrium_g() const {
    const double lambda = std::max(0.0, prod.loss_rate_per_day);
    if (lambda <= 0.0) return prod.confinement_capacity_g;   // sans fuite : le bidon
    const double m_dot_day = prod.rate_g_yr() / 365.25;
    return std::min(m_dot_day / lambda, prod.confinement_capacity_g);
  }

  void tick(double dt_days, bool production_qualifiee) {
    if (dt_days <= 0.0) return;
    const double lambda = std::max(0.0, prod.loss_rate_per_day);
    // La production s'arrête si la filière n'est pas qualifiée — mais PAS la
    // fuite : un stock mal confiné se perd que l'on sache le refaire ou non.
    const double m_dot_day =
        production_qualifiee ? prod.rate_g_yr() / 365.25 : 0.0;
    if (lambda <= 0.0) {
      grams += m_dot_day * dt_days;
    } else {
      const double s_inf = m_dot_day / lambda;
      grams = s_inf + (grams - s_inf) * std::exp(-lambda * dt_days);
    }
    grams = std::clamp(grams, 0.0, prod.confinement_capacity_g);
  }

  // Années pour atteindre `cible_g`. Rend l'infini si l'équilibre est en dessous :
  // ce n'est pas « long », c'est IMPOSSIBLE, et le joueur doit lire la différence.
  double years_to_reach(double cible_g) const {
    if (cible_g <= grams) return 0.0;
    const double s_inf = equilibrium_g();
    if (cible_g >= s_inf) return 1e300;                 // hors d'atteinte à ce débit
    const double lambda = std::max(0.0, prod.loss_rate_per_day);
    if (lambda <= 0.0) {
      const double m_dot_day = prod.rate_g_yr() / 365.25;
      return m_dot_day > 0.0 ? (cible_g - grams) / m_dot_day / 365.25 : 1e300;
    }
    return std::log((s_inf - grams) / (s_inf - cible_g)) / lambda / 365.25;
  }
  bool hors_atteinte(double cible_g) const {
    return years_to_reach(cible_g) > 1e299;
  }

  // POURQUOI ça bloque, et pas seulement QUE ça bloque. Le joueur qui agrandit
  // son réservoir alors que c'est la fuite qui borne doit pouvoir le lire.
  bool borne_par_la_fuite() const {
    return equilibrium_g() < prod.confinement_capacity_g - 1e-12;
  }
};

// Énergie d'annihilation disponible pour `grams` d'antimatière (avec autant de
// matière) : E = 2·m·c². ~1.8e14 J/g de PAIRE ; le GDD retient ~9e13 J par
// gramme d'ANTIMATIÈRE annihilé avec sa contrepartie [GDD Annexe B].
inline double annihilation_energy_j(double grams_antimatter) {
  return grams_antimatter * 1e-3 * cst::C_LIGHT * cst::C_LIGHT;
}

// ═══════════════════════════════════════════════════════════════════════════
// LE TAUX D'UNE HORLOGE — CE QUI FAIT VRAIMENT DIVERGER DEUX ÂGES [GDD 6.7]
// ═══════════════════════════════════════════════════════════════════════════
// La dilatation cinématique n'est que LA MOITIÉ DU TIERS de l'histoire : une
// horloge bat aussi selon le POTENTIEL où elle se trouve. Les deux termes sont du
// même ordre dans le système solaire (~1e-9), et les ignorer ferait mentir le
// modèle d'un facteur 3 — pas d'un epsilon. On garde donc :
//
//     dτ/dt = (1 + Φ/c²) / γ(v)
//
// EXACT en v (relativité restreinte, valable jusqu'à β→1, ce que le régime
// antimatière exige), PREMIER ORDRE en Φ/c². L'approximation de champ faible est
// DÉCLARÉE [GDD 6.8] et bornée : |Φ|/c² ≤ 1e-8 partout où un vaisseau peut aller
// dans ce jeu (surface solaire exclue), donc le terme négligé est en 1e-16 —
// quatre ordres sous la seconde sur une vie entière.

// Le POTENTIEL DU GÉOÏDE, W₀/c². Constante DÉFINISSANTE (UAI/UGGI 2015), pas un
// réglage. Le géoïde est l'équipotentielle du potentiel EFFECTIF (gravitation +
// centrifuge) : une seule valeur suffit donc quelle que soit la LATITUDE du site
// de lancement, et c'est très exactement la définition du TAI. Utiliser −μ⊕/R⊕ à
// la place aurait demandé de traquer la latitude pour un résultat moins juste.
inline constexpr double W0_SUR_C2 = 6.969290134e-10;

inline double taux_horloge(double v_mps, double phi_j_per_kg) {
  const double c2 = cst::C_LIGHT * cst::C_LIGHT;
  return (1.0 + phi_j_per_kg / c2) / lorentz_gamma(beta(v_mps));
}

// CE QUI EST OBSERVABLE N'EST PAS UN TAUX, C'EST UN RAPPORT. Un taux d'horloge
// seul dépend du système de coordonnées choisi et ne se mesure pas ; deux
// horloges comparées, si. Rend dτ_A / dτ_B.
inline double rapport_horloges(double v_a, double phi_a, double v_b, double phi_b) {
  const double tb = taux_horloge(v_b, phi_b);
  return tb > 0.0 ? taux_horloge(v_a, phi_a) / tb : 1.0;
}

// --- MOYENNES TEMPORELLES SUR UNE ORBITE KÉPLÉRIENNE : DEUX IDENTITÉS EXACTES -
// Sur une période, la moyenne TEMPORELLE de 1/r vaut EXACTEMENT 1/a. (En anomalie
// excentrique, dt = √(a³/μ)·(1 − e·cos E)·dE et r = a(1 − e·cos E) : les deux
// facteurs se simplifient, il reste ∫dE/a sur 2π.) Il en découle, sans la moindre
// intégration numérique et sans hypothèse sur l'excentricité :
//
//     ⟨v²⟩ = μ(2⟨1/r⟩ − 1/a) = μ/a        (vis-viva moyennée)
//     ⟨Φ⟩  = −μ⟨1/r⟩          = −μ/a
//
// La vitesse quadratique moyenne d'une orbite est donc celle d'un CERCLE de rayon
// a. Ce n'est pas une approximation de trajectoire : c'est la valeur exacte de la
// moyenne qui entre dans le taux d'horloge. Corollaire remarquable, et vérifié
// par oracle : |⟨Φ⟩|/c² = 2 × ⟨v²⟩/(2c²) — le terme de potentiel vaut exactement
// le DOUBLE du terme cinétique, et de signe opposé. Une orbite haute bat donc
// plus vite qu'une orbite basse, et l'effet net est l'inverse de l'intuition
// « qui va vite vieillit moins ».
inline double v_rms_kepler(double a_m, double mu) {
  return (a_m > 0.0 && mu > 0.0) ? std::sqrt(mu / a_m) : 0.0;
}
inline double phi_moyen_kepler(double a_m, double mu) {
  return (a_m > 0.0 && mu > 0.0) ? -mu / a_m : 0.0;
}

// RAPPORT DES HORLOGES POUR DEUX ORBITES DU MÊME CORPS CENTRAL. Le mouvement que
// les deux partagent (la Terre autour du Soleil, pour deux orbites terrestres)
// se SIMPLIFIE dans le rapport : c'est pourquoi on peut traiter le géocentrique
// et l'héliocentrique séparément sans jamais additionner deux repères.
inline double rapport_horloges_kepler(double a_bord_m, double a_ref_m, double mu) {
  return rapport_horloges(v_rms_kepler(a_bord_m, mu), phi_moyen_kepler(a_bord_m, mu),
                          v_rms_kepler(a_ref_m, mu),  phi_moyen_kepler(a_ref_m, mu));
}

// UNE ORBITE TERRESTRE CONTRE UNE HORLOGE AU SOL. La référence n'est pas une
// orbite mais le géoïde, d'où sa constante propre. C'est le calcul qui donne les
// deux nombres que tout le monde connaît — ISS ≈ −24,6 µs/j (le sol gagne),
// GPS ≈ +38,6 µs/j (l'orbite gagne) — et les oracles les vérifient : un modèle
// d'horloge qui ne retrouve pas la correction GPS n'est pas un modèle d'horloge.
inline double rapport_horloge_orbite_terrestre(double r_orbite_m) {
  if (!(r_orbite_m > 0.0)) return 1.0;
  return taux_horloge(v_rms_kepler(r_orbite_m, cst::MU_EARTH),
                      phi_moyen_kepler(r_orbite_m, cst::MU_EARTH))
         / (1.0 - W0_SUR_C2);
}

// --- DualClock [GDD 14.4, 3.4] -----------------------------------------------
// Deux horloges : Terre (temps de jeu) et bord (temps propre). Elles COÏNCIDENT
// sous le seuil ; l'écart cumulé est le vieillissement différentiel, qui pèse
// sur la carrière et la passation. Traitement multijoueur spécifique [GDD 16.3].
struct DualClock {
  double t_earth{0.0};   // s écoulées côté Terre
  double tau_board{0.0}; // s vécues à bord

  // Avance les deux horloges pendant dt (temps Terre) à vitesse bord v.
  // Cinématique PURE : la référence est supposée immobile et hors potentiel.
  // Correct pour un vaisseau relativiste, insuffisant pour une croisière
  // interplanétaire — voir `advance_ratio`, qui est le chemin vivant.
  void advance(double dt, double v_mps) {
    t_earth += dt;
    tau_board += dt / lorentz_gamma(beta(v_mps));
  }
  // LE CHEMIN GÉNÉRAL : `ratio` = dτ_bord / dτ_Terre, tel que le rendent les
  // fonctions ci-dessus. `dt` est du temps propre TERRESTRE, c'est-à-dire le
  // calendrier du jeu — ce que le joueur lit sur son bandeau.
  void advance_ratio(double dt, double ratio) {
    t_earth += dt;
    tau_board += dt * ratio;
  }
  // Écart d'âge accumulé. POSITIF = le personnage revient plus JEUNE que le
  // monde (régime relativiste) ; NÉGATIF = il revient plus VIEUX, ce qui est le
  // cas réel d'une croisière interplanétaire, où le potentiel l'emporte sur la
  // vitesse. Le signe est un RÉSULTAT, pas une hypothèse.
  double aging_gap() const { return t_earth - tau_board; }
  bool   diverged() const { return std::fabs(aging_gap()) > 1.0; } // > 1 s : affichable
};

} // namespace fen::rel
