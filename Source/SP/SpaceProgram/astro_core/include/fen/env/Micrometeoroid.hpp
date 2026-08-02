// fen/env/Micrometeoroid.hpp — FLUX MICROMÉTÉORITIQUE ET PERFORATION [GDD 12.4, 6.5, 7.8]
//
// [GDD 12.4] nomme trois mécanismes de dégradation des filières avancées. Deux
// étaient branchés (vieillissement du cœur, confinement antimatière) ; le
// troisième — « dégradation des radiateurs : érosion, MICROMÉTÉORITES, cycles
// thermiques, critique pour NEP/fusion » — ne l'était qu'à MOITIÉ : on savait
// calculer le risque d'une COLLISION avec un objet catalogué (env/Debris.hpp),
// pas la PERFORATION par la population sub-millimétrique. Or ce n'est pas la même
// population, ni la même physique, ni la même conséquence.
//
// Le fichier précédent le déclarait manquant « faute d'un modèle de flux que rien
// dans le dépôt ne porte ». C'était vrai du dépôt, faux du monde : les quatre
// pièces sont publiées depuis des décennies. On les prend telles quelles.
//
// ═══ LES QUATRE PIÈCES, ET LEUR SOURCE ═══
//
// 1) FLUX CUMULÉ — modèle de GRÜN et al. (1985), la référence du flux
//    interplanétaire à 1 UA, calé sur les détecteurs Pioneer/HEOS, les
//    microcratères lunaires et la photométrie de la lumière zodiacale :
//      F(m) = 3,15576e7 · [F1(m) + F2(m) + F3(m)]      particules · m⁻² · an⁻¹
//      F1 = (2,2e3·m^0,306 + 15,0)^−4,38                (m > 1e−9 g)
//      F2 = 1,3e−9·(m + 1e11·m² + 1e27·m⁴)^−0,36        (1e−14 < m < 1e−9 g)
//      F3 = 1,3e−16·(m + 1e6·m²)^−0,85                  (m < 1e−14 g)
//    m en GRAMMES ; flux cumulé (masse ≥ m) sur une plaque plane d'orientation
//    aléatoire vue sous 2π stéradians ; domaine 1e−18 à 1 g.
//    Le facteur 3,15576e7 EST le nombre de secondes d'une année julienne : les
//    trois F sont par m²·s, la somme est par m²·an.
//
// 2) DENSITÉ DES MÉTÉOROÏDES — distribution par palier de SSP-30425B (environnement
//    naturel de dimensionnement de la Station) : 2,0 g/cm³ sous 1e−6 g,
//    1,0 g/cm³ de 1e−6 à 1e−2 g, 0,5 g/cm³ au-delà. Ce n'est pas un détail : la
//    même particule de 100 µm pèse quatre fois moins à 0,5 qu'à 2,0, et le flux
//    varie comme une puissance de la masse.
//
// 3) LIMITE BALISTIQUE — équation de COUR-PALAIS pour cible semi-infinie :
//      p = 5,24 · d^(19/18) · BH^(−1/4) · (ρ_p/ρ_t)^(1/2) · (v⊥/c_t)^(2/3)
//    p profondeur de cratère et d diamètre du projectile en cm, BH dureté Brinell
//    de la cible, ρ en g/cm³, v et c_t en km/s. Une plaque MINCE est perforée
//    quand son épaisseur descend sous k·p, avec k = 1,8 pour la perforation,
//    2,2 pour l'écaillage détaché, 3,0 pour l'écaillage naissant (coefficients de
//    Cour-Palais/Christiansen, établis sur de l'aluminium 7075-T6).
//
// 4) QUI DOMINE — LDEF (5,7 ans en orbite, 130 m² de surface récupérée et
//    analysée cratère par cratère) tranche la question qui décide de tout ici :
//    les DÉBRIS orbitaux dominent le flux mesuré sous 30 µm de profondeur de
//    pénétration dans l'aluminium, les MÉTÉOROÏDES au-dessus. Une paroi de
//    caloduc fait 500 à 2 000 µm : dans ce régime, la population naturelle EST
//    la population qui perce, et modéliser Grün seul n'est pas une approximation
//    commode, c'est le bon choix de population.
//
// ═══ RECOUPEMENT MESURÉ CONTRE SSP-30425B, ET IL N'EST PAS FLATTEUR ═══
//
// SSP-30425B publie le flux de météoroïdes de son environnement de
// dimensionnement à l'orbite de la Station. Comparé au Grün implémenté ici, avec
// les MÊMES densités de particules :
//     ≥ 10 µm  : SSP 7,91e2  /  Grün 9,36e1   → SSP est 8,45× plus haut
//     ≥ 100 µm : SSP 8,78e0  /  Grün 1,43e0   → SSP est 6,16× plus haut
//     ≥ 1 mm   : SSP 5,22e-3 /  Grün 3,20e-3  → SSP est 1,63× plus haut
// L'écart se resserre exactement là où ce fichier travaille (les parois réelles
// coupent vers 0,1–0,7 mm de diamètre critique) mais il ne s'annule pas, et il va
// dans le MAUVAIS sens : Grün est le plus optimiste des deux. Trois raisons
// connues et non ajustables : SSP est un environnement de DIMENSIONNEMENT (donc
// délibérément majorant), il s'applique en orbite terrestre où la focalisation
// gravitationnelle concentre le flux, et il descend d'un modèle antérieur à Grün.
// On garde Grün SANS facteur correctif, parce que la mission passe l'essentiel de
// son temps en croisière interplanétaire — le régime pour lequel Grün EST le
// modèle juste — et parce qu'ajouter un facteur pour rejoindre un standard
// terrestre serait calibrer sur le mauvais milieu.
// L'écart est déclaré ici pour que personne ne le redécouvre en le prenant pour
// un bug : en orbite basse, ce fichier sous-estime d'un facteur ~6.
//
// ═══ CE QUE CE MODÈLE NE FAIT PAS, ET IL FAUT LE DIRE [GDD 6.8, 12.5] ═══
//
// . PAROI SIMPLE UNIQUEMENT. Un radiateur réel est BLINDÉ en configuration
//   Whipple : un bouclier mince devant, un vide, le tube derrière. À masse
//   égale, cette configuration relève la limite balistique de près d'un ordre de
//   grandeur en diamètre — c'est pourquoi les radiateurs de la Station survivent
//   à un flux qui percerait leur tube nu. Les épaisseurs que ce fichier réclame
//   sont donc une BORNE SUPÉRIEURE, jamais un optimum. Le sens de l'erreur est le
//   sens conservateur [GDD 12.5], et l'équation double paroi de
//   Cour-Palais/Christiansen est le prochain incrément si le besoin s'en fait
//   sentir.
//   LES DEUX ÉCARTS SE COMPENSENT, ET PAS À ÉGALITÉ. Un ordre de grandeur en
//   diamètre critique vaut ~2 000 en flux (Φ ∝ d^−3,3), là où le déficit contre
//   SSP-30425B ne vaut que ~6. Le modèle est donc NET PESSIMISTE, largement, et
//   c'est la seule direction acceptable pour un verdict de survie.
// . DÉBRIS SUB-MILLIMÉTRIQUES NON CATALOGUÉS exclus (il faudrait ORDEM). Justifié
//   par LDEF ci-dessus au-delà de 30 µm, donc pour toute paroi réelle ; l'omission
//   ne mordrait que sur une phase d'assemblage en orbite basse.
// . INCIDENCE NORMALE. v⊥ = v : on ne moyenne pas sur les angles d'impact. Une
//   incidence oblique perfore MOINS, l'hypothèse est donc conservatrice.
#pragma once
#include <algorithm>
#include <cmath>

#include "fen/core/Constants.hpp"

namespace fen::env {

// ---------------------------------------------------------------------------
// 1) FLUX CUMULÉ DE GRÜN (1985)
// ---------------------------------------------------------------------------

inline constexpr double GRUN_SECONDS_PER_YEAR = 3.15576e7;   // année julienne
inline constexpr double GRUN_MASS_MIN_G = 1.0e-18;
inline constexpr double GRUN_MASS_MAX_G = 1.0;

// Flux cumulé de particules de masse ≥ `mass_g`, par m² et par an, sur une
// plaque plane d'orientation aléatoire (2π sr), au voisinage de 1 UA hors
// influence de la Terre.
// HORS DOMAINE : on BORNE au lieu d'extrapoler. Sous 1e−18 g le modèle sature ;
// au-dessus de 1 g on renvoie le flux de 1 g, ce qui SURESTIME le flux des gros
// projectiles — direction conservatrice [GDD 12.5], et l'énoncer vaut mieux que
// de laisser une extrapolation muette décider d'une survie.
inline double grun_flux_per_m2_year(double mass_g) {
  if (mass_g <= 0.0) return 0.0;
  const double m = std::clamp(mass_g, GRUN_MASS_MIN_G, GRUN_MASS_MAX_G);
  const double m2 = m * m;
  const double f1 = std::pow(2.2e3 * std::pow(m, 0.306) + 15.0, -4.38);
  const double f2 = 1.3e-9 * std::pow(m + 1.0e11 * m2 + 1.0e27 * m2 * m2, -0.36);
  const double f3 = 1.3e-16 * std::pow(m + 1.0e6 * m2, -0.85);
  return GRUN_SECONDS_PER_YEAR * (f1 + f2 + f3);
}

inline double grun_flux_per_m2_s(double mass_g) {
  return grun_flux_per_m2_year(mass_g) / GRUN_SECONDS_PER_YEAR;
}

// ---------------------------------------------------------------------------
// 2) GÉOMÉTRIE ET DENSITÉ DES MÉTÉOROÏDES [SSP-30425B]
// ---------------------------------------------------------------------------

// Vitesse d'impact moyenne des météoroïdes à 1 UA. 20 km/s est la valeur de
// référence de la population de Grün ; SSP-30425B retient 19 km/s pour l'orbite
// de la Station. Valeur DÉCLARÉE [GDD 6.8].
inline constexpr double METEOROID_MEAN_SPEED_KMS = 20.0;

// Paliers de densité de SSP-30425B, du plus dense au moins dense.
inline constexpr int METEOROID_DENSITY_BINS = 3;
inline constexpr double METEOROID_DENSITY_G_CM3[3] = {2.0, 1.0, 0.5};
inline constexpr double METEOROID_BIN_MASS_LO_G[3] = {0.0, 1.0e-6, 1.0e-2};
inline constexpr double METEOROID_BIN_MASS_HI_G[3] = {1.0e-6, 1.0e-2, 1.0e30};

inline double meteoroid_density_g_cm3(double mass_g) {
  if (mass_g < 1.0e-6) return 2.0;
  if (mass_g < 1.0e-2) return 1.0;
  return 0.5;
}

// Sphère : m = (π/6)·ρ·d³, en grammes / cm.
inline double meteoroid_mass_g(double diameter_cm, double density_g_cm3) {
  if (diameter_cm <= 0.0 || density_g_cm3 <= 0.0) return 0.0;
  return (cst::PI / 6.0) * density_g_cm3 * diameter_cm * diameter_cm * diameter_cm;
}

inline double meteoroid_diameter_cm(double mass_g, double density_g_cm3) {
  if (mass_g <= 0.0 || density_g_cm3 <= 0.0) return 0.0;
  return std::cbrt(6.0 * mass_g / (cst::PI * density_g_cm3));
}

// ---------------------------------------------------------------------------
// 3) LIMITE BALISTIQUE DE COUR-PALAIS
// ---------------------------------------------------------------------------

// Matériau de paroi. Données de manuel, pas des réglages : dureté Brinell,
// densité, VITESSE DU SON DE VOLUME (et non la vitesse longitudinale, plus
// élevée — Cour-Palais est calé sur la première).
struct WallMaterial {
  const char* name{"Al 6061-T6"};
  double brinell{95.0};
  double density_g_cm3{2.70};
  double sound_speed_kms{5.10};
};

// k de Cour-Palais/Christiansen : le mode de défaillance qu'on refuse.
inline constexpr double PERFORATION_K       = 1.8;   // trou franc
inline constexpr double SPALL_DETACHED_K    = 2.2;   // écaillage détaché
inline constexpr double SPALL_INCIPIENT_K   = 3.0;   // écaillage naissant

inline const WallMaterial& wall_al_6061() {
  static const WallMaterial w{"Al 6061-T6", 95.0, 2.70, 5.10};
  return w;
}
inline const WallMaterial& wall_al_7075() {
  // Le matériau sur lequel k = 1,8 a été établi.
  static const WallMaterial w{"Al 7075-T6", 150.0, 2.81, 5.10};
  return w;
}
inline const WallMaterial& wall_ti_6al4v() {
  static const WallMaterial w{"Ti-6Al-4V", 334.0, 4.43, 4.99};
  return w;
}
inline const WallMaterial& wall_inox_304() {
  // 304 RECUIT : 123 HB. La plage publiée va de 123 (recuit) à plus de 200
  // (écroui) ; on retient la BORNE MOLLE, celle qui perce le plus facilement
  // [GDD 12.5].
  static const WallMaterial w{"Inox 304", 123.0, 8.00, 5.00};
  return w;
}

// Diamètre de projectile qui perfore tout juste une paroi de `wall_cm`.
// Inversion directe de p = 5,24·d^(19/18)·BH^(−1/4)·(ρp/ρt)^(1/2)·(v/c)^(2/3)
// avec la condition de perforation wall = k·p.
inline double critical_diameter_cm(double wall_cm, const WallMaterial& w,
                                   double projectile_density_g_cm3,
                                   double v_kms = METEOROID_MEAN_SPEED_KMS,
                                   double k = PERFORATION_K) {
  if (wall_cm <= 0.0 || v_kms <= 0.0 || projectile_density_g_cm3 <= 0.0) return 0.0;
  const double coef = k * 5.24
                    * std::pow(w.brinell, -0.25)
                    * std::sqrt(projectile_density_g_cm3 / w.density_g_cm3)
                    * std::pow(v_kms / w.sound_speed_kms, 2.0 / 3.0);
  if (coef <= 0.0) return 0.0;
  return std::pow(wall_cm / coef, 18.0 / 19.0);
}

// Flux de PERFORATIONS d'une paroi de `wall_cm`, par m² et par an.
//
// LA DENSITÉ DU PROJECTILE APPARAÎT DES DEUX CÔTÉS — c'est la seule subtilité du
// fichier. Elle fixe le diamètre critique (un grain dense perce mieux) ET la
// masse de ce diamètre (un grain dense est plus lourd). On cherche donc le palier
// de SSP-30425B qui est COHÉRENT avec lui-même : la masse critique qu'il produit
// doit retomber dans sa propre plage. Les paliers laissent deux petits
// intervalles de diamètre sans solution cohérente (98–124 µm et 2,67–3,36 mm) ;
// on y prend le palier le plus PESSIMISTE, qui est toujours le plus dense
// puisque m_c ∝ ρ^(−0,42) et que le flux décroît avec la masse.
inline double perforation_flux_per_m2_year(double wall_cm, const WallMaterial& w,
                                           double v_kms = METEOROID_MEAN_SPEED_KMS,
                                           double k = PERFORATION_K) {
  // Aucune paroi : toute la population passe. On renvoie le flux au bas du
  // domaine de Grün plutôt que zéro — « rien ne protège » n'est pas « rien ne
  // frappe » (c'est exactement le piège du modèle sans conséquence).
  if (wall_cm <= 0.0) return grun_flux_per_m2_year(GRUN_MASS_MIN_G);

  double flux_pessimiste = 0.0;
  double flux_coherent = -1.0;
  for (int i = 0; i < METEOROID_DENSITY_BINS; ++i) {
    const double rho = METEOROID_DENSITY_G_CM3[i];
    const double d_c = critical_diameter_cm(wall_cm, w, rho, v_kms, k);
    const double m_c = meteoroid_mass_g(d_c, rho);
    const double f = grun_flux_per_m2_year(m_c);
    if (f > flux_pessimiste) flux_pessimiste = f;
    if (m_c >= METEOROID_BIN_MASS_LO_G[i] && m_c < METEOROID_BIN_MASS_HI_G[i])
      flux_coherent = f;
  }
  return flux_coherent >= 0.0 ? flux_coherent : flux_pessimiste;
}

// ---------------------------------------------------------------------------
// 4) CE QU'UNE AILE DE RADIATEUR EN SUBIT
// ---------------------------------------------------------------------------
//
// UNE AILE DE RADIATEUR N'EST PAS UNE SURFACE, C'EST N CIRCUITS. Chaque caloduc
// (ou boucle de fluide) est indépendant : la première perforation le vide et
// retire SA part de la capacité de rejet, pas celle des autres. La fraction de
// capacité qui survit est donc la probabilité qu'un segment donné n'ait pas été
// touché :
//     capacité(t) = exp( − Φ_perforation · a_segment · t )
// CONSÉQUENCE CONTRE-INTUITIVE ET CORRECTE : la capacité résiduelle ne dépend
// PAS de la surface totale, seulement de la surface d'UN segment. Mille mètres
// carrés découpés en mille circuits vieillissent comme un seul mètre carré. La
// surface totale coûte de la masse et une section de collision (déjà modélisées
// ailleurs) — elle ne coûte pas de fragilité par perforation. Le levier, c'est
// la SEGMENTATION.
//
// Surface d'un circuit indépendant. CE N'EST PAS UNE VALEUR DÉCLARÉE, C'EST DE
// LA GÉOMÉTRIE DE VOL : un panneau du Heat Rejection Subsystem de la Station
// mesure 3,33 × 2,64 m (8,79 m²) et porte VINGT-DEUX tubes en parallèle, soit
// 0,40 m² par circuit. Le pas de tubes correspondant (~150 mm) est du même ordre
// que les 90 mm des radiateurs à boucle pompée monophasée publiés.
//
// UNE PREMIÈRE RÉDACTION AVAIT POSÉ 1,0 m² « ordre de grandeur, à calibrer », en
// signalant que le paramètre avait un levier direct. Il en avait un, et il l'a
// prouvé tout de suite : sur un petit radiateur, il faisait exploser la marge de
// surface de 60 % au lieu de 20 %, parce qu'une aile de 6 m² n'aurait eu que six
// circuits pour moyenner ses pertes. L'oracle a refusé le chiffre, et la bonne
// réponse n'était pas de déplacer le seuil — c'était d'aller chercher le pas de
// tube réel.
inline constexpr double RADIATOR_SEGMENT_AREA_M2 = 0.40;   // ISS HRS : 8,79 m² / 22 tubes

// Fraction de la capacité de rejet encore vivante après `exposure_days`.
inline double radiator_capacity_after(double exposure_days, double wall_mm,
                                      double segment_area_m2 = RADIATOR_SEGMENT_AREA_M2,
                                      const WallMaterial* material = nullptr,
                                      double v_kms = METEOROID_MEAN_SPEED_KMS) {
  if (exposure_days <= 0.0 || segment_area_m2 <= 0.0) return 1.0;
  const WallMaterial& w = material ? *material : wall_al_6061();
  const double flux = perforation_flux_per_m2_year(wall_mm * 0.1, w, v_kms);
  const double annees = exposure_days / 365.25;
  return std::exp(-flux * segment_area_m2 * annees);
}

// Nombre de circuits indépendants d'une aile.
inline double radiator_segment_count(double area_m2,
                                     double segment_area_m2 = RADIATOR_SEGMENT_AREA_M2) {
  if (area_m2 <= 0.0 || segment_area_m2 <= 0.0) return 1.0;
  const double n = area_m2 / segment_area_m2;
  return n < 1.0 ? 1.0 : n;
}

// PERCENTILE DE DIMENSIONNEMENT. On ne dimensionne pas sur la MOYENNE des
// perforations : un radiateur taillé pour la moyenne a une chance sur deux de
// manquer sa charge à la fin. On tolère moyenne + 3σ de circuits morts, σ étant
// l'écart-type poissonien √moyenne. Le 3 est un choix de dimensionnement DÉCLARÉ
// [GDD 6.8] — c'est le percentile, pas la physique.
inline constexpr double RADIATOR_DESIGN_SIGMA = 3.0;

// Nombre de circuits morts que le dimensionnement TOLÈRE : moyenne + 3σ sur la
// durée pour laquelle on construit.
inline double radiator_tolerated_dead(double area_m2, double exposure_days,
                                      double wall_mm,
                                      double segment_area_m2 = RADIATOR_SEGMENT_AREA_M2,
                                      const WallMaterial* material = nullptr,
                                      double v_kms = METEOROID_MEAN_SPEED_KMS) {
  const double n = radiator_segment_count(area_m2, segment_area_m2);
  const double q = 1.0 - radiator_capacity_after(exposure_days, wall_mm,
                                                 segment_area_m2, material, v_kms);
  const double moyenne = n * q;
  return moyenne + RADIATOR_DESIGN_SIGMA * std::sqrt(moyenne);
}

// SURDIMENSIONNEMENT REQUIS pour tenir la charge thermique jusqu'au bout. C'est
// ce facteur qui remplace le forfait `redundancy_margin` : « 15 % de surface en
// trop pour les perforations tolérées » était une affirmation sans calcul
// derrière ; ceci en est un.
//
// LA SURFACE ENTRE DES DEUX CÔTÉS — et c'est physique, pas un artefact. La
// fraction TOLÉRÉE vaut (moyenne + kσ)/N, et σ/N décroît en 1/√N : une grande
// aile moyenne ses pertes et exige RELATIVEMENT moins de marge qu'une petite.
//
// LA MARGE DIMENSIONNE LA SURFACE, QUI RENTRE DANS N : c'est un point fixe. Une
// première rédaction l'itérait quatre fois « ça converge en deux tours » — c'était
// faux, l'itération OSCILLE (la pente est négative) et ne converge pas sur une
// petite aile. Elle n'a pas besoin d'itérer du tout, elle se résout :
//   perte(A) = q + k·√(q·a/A) ,  A = A0·M ,  M = 1/(1 − perte)
//   en posant x = 1/M et u = √x :   u² + c·u − (1 − q) = 0 ,  c = k·√(q·a/A0)
//   u = (−c + √(c² + 4(1 − q))) / 2 ,  M = 1/u²
// Racine positive de l'unique trinôme, exacte, sans boucle.
inline double radiator_redundancy_margin(double exposure_days, double wall_mm,
                                         double base_area_m2,
                                         double segment_area_m2 = RADIATOR_SEGMENT_AREA_M2,
                                         const WallMaterial* material = nullptr,
                                         double v_kms = METEOROID_MEAN_SPEED_KMS) {
  if (exposure_days <= 0.0 || base_area_m2 <= 0.0 || segment_area_m2 <= 0.0) return 1.0;
  const double q = 1.0 - radiator_capacity_after(exposure_days, wall_mm,
                                                 segment_area_m2, material, v_kms);
  // Une capacité effondrée demanderait une surface infinie : on borne à 4×, et
  // au-delà c'est à l'ÉPAISSEUR de répondre, pas à la surface (le blindage croît
  // en T^(1/3) là où la surface croîtrait exponentiellement).
  if (q >= 0.75) return 4.0;
  const double c = RADIATOR_DESIGN_SIGMA * std::sqrt(q * segment_area_m2 / base_area_m2);
  const double u = 0.5 * (-c + std::sqrt(c * c + 4.0 * (1.0 - q)));
  if (u <= 0.5) return 4.0;                       // 1/u² > 4
  const double marge = 1.0 / (u * u);
  return marge > 4.0 ? 4.0 : marge;
}

// ---------------------------------------------------------------------------
// 5) CE QUE LE BLINDAGE COÛTE
// ---------------------------------------------------------------------------
//
// Une paroi plus épaisse ne se paie pas en rien : elle se paie au kilo, et
// Tsiolkovsky la paie deux fois (masse sèche de l'étage). On ne charge que
// l'EXCÈS sur une paroi de référence, parce que la densité surfacique du panneau
// (`RadiatorSpec::areal_density`) inclut déjà des caloducs — charger la paroi
// entière compterait le tube deux fois.
inline constexpr double RADIATOR_WALL_BASELINE_MM = 0.5;   // paroi nue de référence
// Fraction de la surface du panneau réellement occupée par du tube sous pression :
// c'est elle seule qu'il faut blinder. 30 % correspond à des caloducs au pas de
// quelques centimètres. Valeur DÉCLARÉE [GDD 6.8, Annexe E].
inline constexpr double RADIATOR_TUBE_COVERAGE = 0.30;

inline double radiator_armour_kg_per_m2(double wall_mm,
                                        double tube_coverage = RADIATOR_TUBE_COVERAGE,
                                        const WallMaterial* material = nullptr) {
  const WallMaterial& w = material ? *material : wall_al_6061();
  const double exces_mm = wall_mm - RADIATOR_WALL_BASELINE_MM;
  if (exces_mm <= 0.0 || tube_coverage <= 0.0) return 0.0;
  // g/cm³ → kg/m³ = ×1000 ; mm → m = ×1e-3.
  return w.density_g_cm3 * 1000.0 * exces_mm * 1.0e-3 * tube_coverage;
}

// ÉPAISSEUR DE PAROI nécessaire pour ne pas perdre plus de `max_loss_fraction`
// de capacité sur la durée. Un refus doit nommer la direction : c'est cette
// fonction qui la donne au joueur. Bissection — la relation flux/épaisseur est
// monotone mais pas inversible en forme close (Φ ∝ t^−3,1 environ).
inline double required_wall_mm(double exposure_days, double max_loss_fraction,
                               double segment_area_m2 = RADIATOR_SEGMENT_AREA_M2,
                               const WallMaterial* material = nullptr,
                               double v_kms = METEOROID_MEAN_SPEED_KMS) {
  if (exposure_days <= 0.0) return 0.0;
  const double perte = std::clamp(max_loss_fraction, 1.0e-6, 0.99);
  const WallMaterial& w = material ? *material : wall_al_6061();
  const double annees = exposure_days / 365.25;
  // Φ admissible : capacité = exp(−Φ·a·T) ≥ 1 − perte.
  const double flux_max = -std::log(1.0 - perte) / (segment_area_m2 * annees);
  double lo = 0.001, hi = 50.0;                   // mm
  if (perforation_flux_per_m2_year(hi * 0.1, w, v_kms) > flux_max) return hi;
  for (int i = 0; i < 80; ++i) {
    const double mid = 0.5 * (lo + hi);
    if (perforation_flux_per_m2_year(mid * 0.1, w, v_kms) > flux_max) lo = mid;
    else hi = mid;
  }
  return hi;
}

} // namespace fen::env
