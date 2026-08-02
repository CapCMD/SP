// fen/vehicle/Geometry.hpp — LA GÉOMÉTRIE DU VÉHICULE [GDD 12.2, 17.2, 17.4]
//
// « Éditeur en coupe fournissant LA GÉOMÉTRIE DU VÉHICULE, réutilisée
//   directement au rendu » [12.2]
// « Le catalogue de pièces et l'éditeur en coupe fournissent DÉJÀ la géométrie :
//   un véhicule assemblé par le joueur doit être RENDU, pas modélisé » [17.2]
// « de la vue système au PLAN VAISSEAU (mètres) par simple zoom, car le vaisseau
//   EST DÉJÀ dans la scène » [17.4]
//
// Ces trois lignes demandaient une chose qui n'existait nulle part : aucune pièce
// du catalogue ne portait de DIMENSION. Le véhicule du joueur était un point
// émissif de taille écran constante — à dix mètres comme à dix unités
// astronomiques. Ce fichier lui donne un corps.
//
// ═══ LA RÈGLE DE CE FICHIER : ON NE POSE AUCUNE DIMENSION ═══
// Tout est DÉRIVÉ de ce que le catalogue porte déjà (poussée, Isp au vide, Isp au
// sol, densité d'ergols, section de capsule) et de la géométrie de tuyère de
// manuel. Trois recoupements le vérifient sur des cotes publiques (RS-25, F-1,
// capsules), et l'oracle les tient.
//
// ═══ ET CETTE GÉOMÉTRIE NE NOURRIT AUCUNE PHYSIQUE ═══
// C'est un PRODUIT D'AFFICHAGE : le rendu 3D [17.2] et la coupe de l'atelier
// [12.2]. Rien ici n'entre dans Tsiolkovsky, dans la traînée ni dans la rentrée
// (la capsule porte déjà sa propre section pour `flight/Reentry.hpp`). La
// frontière est volontaire : elle borne ce que coûterait une cote fausse, et
// c'est ce qui autorise les approximations déclarées ci-dessous [GDD 6.8, 12.5].
#pragma once
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "fen/core/Constants.hpp"
#include "fen/vehicle/PartsCatalog.hpp"
#include "fen/vehicle/Propulsion.hpp"

namespace fen::vehicle {

namespace cst = fen::cst;

// Pression atmosphérique de référence au niveau de la mer : c'est ELLE qui
// convertit l'écart Isp vide/sol en surface de sortie d'ajutage (voir plus bas).
inline constexpr double P0_SEA_LEVEL = 101325.0;   // Pa (atmosphère normale)

// ═══════════════════════════════════════════════════════════════════════════
// L'AJUTAGE — SA SECTION DE SORTIE
// ═══════════════════════════════════════════════════════════════════════════
//
// ROUTE EXACTE, SANS AUCUN PARAMÈTRE — quand le moteur s'allume au sol.
// La poussée au sol et au vide ne diffèrent que du terme de pression :
//     F_sol = F_vide − p0 · A_sortie
// et comme le débit est le même, Isp_sol / Isp_vide = F_sol / F_vide. D'où
//     A_sortie = F_vide · (1 − Isp_sol / Isp_vide) / p0
// Le catalogue porte les trois grandeurs : la section de sortie de tout moteur
// allumable au sol est donc une DONNÉE, pas une estimation.
//
// RECOUPEMENT (tenu par l'oracle) : RS-25 → 2,34 m de diamètre de sortie contre
// 2,30 m publiés ; F-1 → 3,63 m contre 3,72 m publiés. Moins de 2,5 % d'écart,
// sur deux moteurs que tout sépare.
inline double exit_area_exact_m2(const EnginePart& e) {
  if (e.isp_sl_s <= 0.0 || e.isp_vac_s <= 0.0) return 0.0;
  const double ratio = e.isp_sl_s / e.isp_vac_s;
  if (ratio >= 1.0) return 0.0;
  return e.thrust_vac_n * (1.0 - ratio) / P0_SEA_LEVEL;
}

// ROUTE DE CLASSE — pour les moteurs qui ne s'allument QU'AU VIDE.
// Il n'y a alors plus d'Isp au sol, donc plus d'identité exacte, et le
// catalogue ne porte ni pression de chambre ni rapport de détente. On repasse
// par la relation de manuel F = C_F · p_c · A_col, avec A_sortie = ε · A_col :
//     A_sortie = F_vide · ε / (C_F · p_c)
// Les trois facteurs se lument en UNE constante par classe de moteur,
// K = ε / (C_F · p_c), en m²/N.
//
// APPROXIMATION DÉCLARÉE [GDD 6.8] : ε et p_c sont pris aux ordres de grandeur
// de la littérature de propulsion (Sutton, *Rocket Propulsion Elements*), par
// CLASSE et non par moteur. Ce sont les seules valeurs POSÉES du fichier.
// Leur erreur est MESURÉE, pas espérée : sur les cinq moteurs allumables au sol,
// où la route exacte donne la réponse, l'oracle compare les deux et publie
// l'écart (voir `test_contenu_gdd`). On ne l'utilise donc jamais quand la route
// exacte s'applique.
inline constexpr double CF_VACUUM = 1.9;     // coefficient de poussée au vide

struct NozzleClass {
  double chamber_pressure_pa;  // p_c
  double expansion_ratio;      // ε
  double k() const { return expansion_ratio / (CF_VACUUM * chamber_pressure_pa); }
};

// Une classe par régime de moteur. Le PROPERGOL vient du réservoir par défaut de
// la pièce (`default_tank`) : un moteur cryogénique et un moteur à ergols
// stockables ne travaillent pas du tout à la même pression de chambre.
inline NozzleClass nozzle_class_sea_level()   { return {100.0e5,  25.0}; }
// LES DEUX CLASSES « À VIDE » NE SE DISTINGUENT PAS PAR LE HASARD MAIS PAR LE
// CYCLE, et le propergol le trahit : un moteur à vide LH2 est un expandeur, à
// pression de chambre modeste (RL10 44 bar, Vinci 60) ; un moteur à vide
// hydrocarbure dérive d'un étage principal et garde sa haute pression (Merlin
// Vacuum ~97 bar). Les confondre élargissait le Merlin Vacuum de 37 %, mesuré.
inline NozzleClass nozzle_class_vac_lh2()     { return { 45.0e5, 140.0}; }
inline NozzleClass nozzle_class_vac_hydrocarb(){ return { 95.0e5, 165.0}; }
inline NozzleClass nozzle_class_vac_storable(){ return { 12.0e5,  85.0}; }
inline NozzleClass nozzle_class_solid()       { return { 60.0e5,  16.0}; }
inline NozzleClass nozzle_class_ntp()         { return { 45.0e5, 100.0}; }

// DENSITÉ DE POUSSÉE DES PROPULSEURS ÉLECTRIQUES — mesurée sur les unités volées.
// Un propulseur à grilles est limité par la CHARGE D'ESPACE, un Hall ne l'est
// pas : leurs densités de poussée diffèrent d'un ordre de grandeur, et c'est
// cette limite physique qui fixe le diamètre de grille ou de canal.
//   . grilles : NSTAR 92 mN sur 30 cm (1,30 N/m²), NEXT-C 236 mN sur 40 cm (1,88)
//   . Hall    : SPT-100 83 mN sur 10 cm (10,6 N/m²), BHT-6000 400 mN sur 20 (12,7)
inline constexpr double THRUST_DENSITY_GRIDDED_N_M2 = 1.6;
inline constexpr double THRUST_DENSITY_HALL_N_M2    = 11.5;

// Le propergol du moteur, tel que son réservoir par défaut le nomme.
inline const char* engine_propellant(const EnginePart& e) {
  if (e.tank_id && *e.tank_id) {
    if (const TankPart* t = find_tank(e.tank_id)) return t->propellant;
  }
  return "";
}

// L'hydrogène liquide, et lui seul, impose le cycle à expandeur.
inline bool is_hydrogen(const char* prop) {
  const std::string p = prop ? prop : "";
  return p == "LOX/LH2";
}
inline bool is_hydrocarbon(const char* prop) {
  const std::string p = prop ? prop : "";
  return p == "LOX/RP-1" || p == "LOX/CH4";
}

// La classe d'ajutage d'une pièce — un seul endroit où ce choix se fait.
inline NozzleClass nozzle_class_of(const EnginePart& e) {
  if (e.family == PropFamily::ChemicalSolid) return nozzle_class_solid();
  if (e.family == PropFamily::Ntp)           return nozzle_class_ntp();
  if (e.isp_sl_s > 0.0)                      return nozzle_class_sea_level();
  const char* prop = engine_propellant(e);
  if (is_hydrogen(prop))    return nozzle_class_vac_lh2();
  if (is_hydrocarbon(prop)) return nozzle_class_vac_hydrocarb();
  return nozzle_class_vac_storable();
}

// Section de sortie, toutes filières. Exacte quand elle peut l'être.
inline double exit_area_m2(const EnginePart& e) {
  const double exact = exit_area_exact_m2(e);
  if (exact > 0.0) return exact;

  switch (e.family) {
    case PropFamily::ElectricGridded:
      return e.thrust_vac_n / THRUST_DENSITY_GRIDDED_N_M2;
    case PropFamily::ElectricHall:
    case PropFamily::Nep:
      return e.thrust_vac_n / THRUST_DENSITY_HALL_N_M2;
    case PropFamily::Fusion:
    case PropFamily::Antimatter:
      // Tuyère MAGNÉTIQUE : il n'y a pas de paroi à dimensionner, et rien de
      // publié à quoi se raccrocher. On rend la boucle de champ à la densité de
      // poussée d'un Hall — hypothèse de simulation assumée, confiance D
      // [GDD 12.5], sans conséquence hors du dessin.
      return e.thrust_vac_n / THRUST_DENSITY_HALL_N_M2;
    case PropFamily::ChemicalSolid:
    case PropFamily::Ntp:
    case PropFamily::ChemicalLiquid:
    default:
      return e.thrust_vac_n * nozzle_class_of(e).k();
  }
}

inline double exit_diameter_m(const EnginePart& e) {
  return 2.0 * std::sqrt(std::max(0.0, exit_area_m2(e)) / cst::PI);
}

// ═══════════════════════════════════════════════════════════════════════════
// L'AJUTAGE — SA LONGUEUR
// ═══════════════════════════════════════════════════════════════════════════
// Une cloche à 80 % (la norme depuis Rao) tient dans un cône de demi-angle 15° :
//     L_cloche = 0,8 · (R_sortie − R_col) / tan 15°
// et le moteur ajoute devant sa chambre, son injecteur et sa turbomachinerie.
// CETTE TÊTE S'ÉCHELONNE SUR LE COL, PAS SUR LA CLOCHE — mesuré : un facteur
// multiplicatif calé sur les moteurs de premier étage allongeait de 26 % les
// moteurs à vide, dont la cloche est immense et la chambre minuscule. Cinq
// rayons de col reproduisent les trois cotes publiques d'un coup.
//
// RECOUPEMENT (tenu par l'oracle) : RS-25 → 3,96 m contre 4,24 m publiés
// (−6,6 %) ; F-1 → 6,15 contre 5,79 (+6,2 %) ; RL10C-1 → 2,30 contre 2,22
// (+3,6 %). Trois moteurs que tout sépare, moins de 7 % chacun.
inline constexpr double ENGINE_HEAD_THROATS = 5.0;  // chambre + turbopompes
inline constexpr double BELL_FRACTION      = 0.8;   // cloche à 80 %
inline constexpr double CONE_HALF_ANGLE_TAN = 0.2679491924311227;  // tan 15°

inline double engine_length_m(const EnginePart& e) {
  const double ae = exit_area_m2(e);
  if (ae <= 0.0) return 0.0;
  const double re = std::sqrt(ae / cst::PI);

  // Les propulseurs électriques n'ont pas de cloche : leur longueur est celle du
  // corps de décharge, de l'ordre du diamètre (NSTAR 30 cm × ~35 cm ;
  // SPT-100 10 cm × ~10 cm). Déclaré, sans conséquence hors du dessin.
  if (e.family == PropFamily::ElectricGridded) return 2.0 * re * 1.0;
  if (e.family == PropFamily::ElectricHall || e.family == PropFamily::Nep ||
      e.family == PropFamily::Fusion || e.family == PropFamily::Antimatter)
    return 2.0 * re * 0.7;

  // Rayon au col : le rapport de détente de la classe le donne à partir de la
  // sortie.
  const NozzleClass nc = nozzle_class_of(e);
  const double rt = re / std::sqrt(std::max(1.0, nc.expansion_ratio));
  const double bell = BELL_FRACTION * (re - rt) / CONE_HALF_ANGLE_TAN;
  return std::max(0.0, bell) + ENGINE_HEAD_THROATS * rt;
}

// ═══════════════════════════════════════════════════════════════════════════
// LE RÉSERVOIR ET LA CAPSULE — RIEN À DÉRIVER, LE CATALOGUE LES PORTE
// ═══════════════════════════════════════════════════════════════════════════
// Le volume d'ergols est la masse divisée par la densité du couple, que
// `TankPart` déclare déjà (`density_kg_m3`, pondérée par le rapport de mélange).
// Le ciel gazeux et les imbrûlés (`residual_fraction`) occupent du volume qu'il
// faut bien enfermer : le réservoir est donc plus grand que ses ergols.
inline double tank_volume_m3(const TankPart& t, double propellant_kg) {
  if (t.density_kg_m3 <= 0.0) return 0.0;
  return std::max(0.0, propellant_kg) / t.density_kg_m3 *
         (1.0 + std::max(0.0, t.residual_fraction));
}

// UN SOLIDE N'A PAS DE RÉSERVOIR — le catalogue le dit déjà (`tank_id` vide,
// « l'enveloppe est déjà dans mass_kg »). Sa poudre occupe pourtant un volume,
// et c'est le CORPS du propulseur qui le tient. Densité d'un APCP coulé
// (perchlorate d'ammonium / HTPB / aluminium) : 1 770 kg/m³, valeur publiée des
// propergols composites de propulseurs de grande taille.
inline constexpr double SOLID_PROPELLANT_DENSITY_KG_M3 = 1770.0;

// Le volume à enfermer pour un étage, quelle que soit sa filière. C'est le seul
// endroit qui sait que le solide se passe de réservoir.
inline double stage_propellant_volume_m3(const StageChoice& st, double propellant_kg) {
  const EnginePart& e = st.engine_part();
  if (!e.tank_id || !e.tank_id[0]) {
    return std::max(0.0, propellant_kg) / SOLID_PROPELLANT_DENSITY_KG_M3 *
           (1.0 + st.residual_fraction());
  }
  const auto& v = tank_catalog();
  const int i = st.tank < 0 ? 0
              : (st.tank >= static_cast<int>(v.size()) ? static_cast<int>(v.size()) - 1
                                                       : st.tank);
  return tank_volume_m3(v[static_cast<std::size_t>(i)], propellant_kg);
}

// La capsule porte sa SECTION DE RÉFÉRENCE parce que la rentrée en a besoin
// [GDD 7.6] : le diamètre du bouclier en découle exactement. Recoupement direct
// sur les quatre lignées du catalogue, qui citent toutes leur cote — Apollo
// 3,91 m, Orion 5,0 m, Dragon 3,7 m, aérocoque martienne 4,5 m.
inline double capsule_diameter_m(const CapsulePart& c) {
  return 2.0 * std::sqrt(std::max(0.0, c.area_m2) / cst::PI);
}

// Hauteur d'une capsule conique : proportion des corps de rentrée réels
// (Apollo 3,47 m pour 3,91 de fond ; Orion 3,3 pour 5,0). Déclaré.
inline constexpr double CAPSULE_HEIGHT_RATIO = 0.8;

// MASSE VOLUMIQUE APPARENTE D'UNE CHARGE UTILE — ce qu'occupe un engin
// spatial une fois replié. Dawn : 1 240 kg dans ~5,3 m³ = 234 kg/m³ ; la bande
// courante des sondes et satellites est 100-300. Déclaré, sans conséquence hors
// du dessin.
inline constexpr double PAYLOAD_BULK_DENSITY_KG_M3 = 200.0;

// ═══════════════════════════════════════════════════════════════════════════
// LA PILE — LA COUPE DU VÉHICULE
// ═══════════════════════════════════════════════════════════════════════════
// Le véhicule est une pile de solides de révolution COAXIAUX, décrits par leurs
// deux stations et leurs deux rayons : c'est exactement ce qu'un dessin en coupe
// [GDD 12.2] montre, et exactement ce qu'un rendu 3D [17.2] a besoin de savoir.
// z = 0 à la base (sortie de l'ajutage du bas), z croissant vers l'avant.
enum class HullRole {
  Nozzle = 0,     // ajutage
  Tank,           // réservoir d'ergols
  Interstage,     // jupe couvrant le moteur de l'étage supérieur
  Payload,        // charge utile nue
  Capsule,        // capsule ou aérocoque
};

struct HullSegment {
  HullRole role{HullRole::Tank};
  double z0_m{}, z1_m{};     // stations le long de l'axe (z1 > z0)
  double r0_m{}, r1_m{};     // rayons aux deux stations (tronc de cône)
  int    stage{-1};          // étage d'appartenance ; -1 = charge utile
  double length_m() const { return z1_m - z0_m; }
};

struct VehicleHull {
  std::vector<HullSegment> segments;
  double length_m{0.0};
  double max_diameter_m{0.0};
  bool   valid{false};
};

// ÉLANCEMENT MAXIMAL D'UN VAISSEAU. Un réservoir n'est pas une aiguille : le
// diamètre de la pile monte tant que l'élancement dépasse cette borne. Saturn V
// vaut 11, Falcon 9 vaut 19, un étage Centaur seul vaut 4 — la borne encadre les
// architectures réelles et n'agit que sur les piles très petites, où le diamètre
// serait sinon dicté par le seul ajutage.
inline constexpr double MAX_SLENDERNESS = 12.0;

// Épaisseur de la jupe d'interétage : ce qui couvre le moteur de l'étage du
// dessus. Elle vaut la longueur de ce moteur — c'est ce qu'elle enferme.

// Construit la coupe. `propellant_kg` est donné PAR ÉTAGE, du bas vers le haut,
// dans le même ordre que `stages` — c'est la sortie de `size_multistage_for_dv`,
// donc la masse d'ergols que Tsiolkovsky a réellement exigée [GDD 6.1]. Aucune
// dimension n'est posée ici : la géométrie est une CONSÉQUENCE du
// dimensionnement, comme le veut [GDD 12.2].
inline VehicleHull build_hull(const std::vector<StageChoice>& stages,
                              const std::vector<double>& propellant_kg,
                              const CapsulePart* capsule,
                              double payload_kg) {
  VehicleHull h;
  if (stages.empty()) return h;

  const std::size_t ns = stages.size();

  // --- 1) ce que chaque pièce impose, indépendamment de la pile -------------
  std::vector<double> v_tank(ns, 0.0), l_engine(ns, 0.0), d_engine(ns, 0.0);
  double d_min = 0.0;
  double v_total = 0.0, l_engines = 0.0;
  for (std::size_t k = 0; k < ns; ++k) {
    const EnginePart& e = stages[k].engine_part();
    const double prop = (k < propellant_kg.size()) ? propellant_kg[k] : 0.0;
    v_tank[k]   = stage_propellant_volume_m3(stages[k], prop);
    l_engine[k] = engine_length_m(e);
    d_engine[k] = exit_diameter_m(e);
    d_min = std::max(d_min, d_engine[k]);
    v_total += v_tank[k];
    l_engines += l_engine[k];
  }
  const double d_capsule = capsule ? capsule_diameter_m(*capsule) : 0.0;
  d_min = std::max(d_min, d_capsule);

  const double v_payload =
      std::max(0.0, payload_kg) / PAYLOAD_BULK_DENSITY_KG_M3;

  // --- 2) le diamètre de la pile -------------------------------------------
  // Un lanceur a UN diamètre courant : c'est le plus contraignant de ses
  // pièces, élargi si l'élancement le demande. Point fixe monotone (la longueur
  // des réservoirs décroît en 1/D²), dix passes suffisent très largement.
  double d = std::max(d_min, 0.5);
  for (int it = 0; it < 40; ++it) {
    const double area = cst::PI * d * d / 4.0;
    // longueur totale = ajutages + réservoirs (fûts + fonds) + charge utile
    double len = l_engines;
    for (std::size_t k = 0; k < ns; ++k)
      len += (area > 0.0 ? v_tank[k] / area : 0.0) + d / 2.0;
    len += (area > 0.0 ? v_payload / area : 0.0);
    if (capsule) len += CAPSULE_HEIGHT_RATIO * d_capsule;
    if (len <= MAX_SLENDERNESS * d) break;
    d *= 1.05;
  }
  const double r = d / 2.0;
  const double area = cst::PI * r * r;

  // --- 3) l'empilement, du bas vers le haut --------------------------------
  double z = 0.0;
  for (std::size_t k = 0; k < ns; ++k) {
    const int st = static_cast<int>(k);
    // l'ajutage, cône ouvert vers le bas
    if (l_engine[k] > 0.0) {
      HullSegment s;
      s.role = HullRole::Nozzle; s.stage = st;
      s.z0_m = z; s.z1_m = z + l_engine[k];
      s.r0_m = d_engine[k] / 2.0;
      s.r1_m = std::max(0.05, d_engine[k] / 8.0);
      h.segments.push_back(s);
      z += l_engine[k];
    }
    // le réservoir : fût cylindrique + fonds bombés (hauteur r/2 chacun,
    // volume π r³/3 par fond ellipsoïdal de demi-axe r/2)
    const double v_domes = 2.0 * cst::PI * r * r * r / 3.0;
    const double v_barrel = std::max(0.0, v_tank[k] - v_domes);
    const double l_barrel = area > 0.0 ? v_barrel / area : 0.0;
    {
      HullSegment s;
      s.role = HullRole::Tank; s.stage = st;
      s.z0_m = z; s.z1_m = z + l_barrel + r;   // fût + les deux demi-fonds
      s.r0_m = r; s.r1_m = r;
      h.segments.push_back(s);
      z = s.z1_m;
    }
    // LA JUPE D'INTERÉTAGE ENFERME LE MOTEUR DE L'ÉTAGE SUPÉRIEUR — elle ne
    // s'ajoute donc PAS à la longueur : elle l'entoure. `z` ne bouge pas, et
    // l'itération suivante pose l'ajutage du dessus à l'intérieur.
    if (k + 1 < ns && l_engine[k + 1] > 0.0) {
      HullSegment s;
      s.role = HullRole::Interstage; s.stage = st;
      s.z0_m = z; s.z1_m = z + l_engine[k + 1];
      s.r0_m = r; s.r1_m = r;
      h.segments.push_back(s);
    }
  }

  // --- 4) ce que la pile emporte -------------------------------------------
  if (v_payload > 0.0) {
    HullSegment s;
    s.role = HullRole::Payload; s.stage = -1;
    s.z0_m = z; s.z1_m = z + (area > 0.0 ? v_payload / area : 0.0);
    s.r0_m = r; s.r1_m = r;
    h.segments.push_back(s);
    z = s.z1_m;
  }
  if (capsule) {
    HullSegment s;
    s.role = HullRole::Capsule; s.stage = -1;
    s.z0_m = z; s.z1_m = z + CAPSULE_HEIGHT_RATIO * d_capsule;
    s.r0_m = d_capsule / 2.0;
    s.r1_m = d_capsule / 6.0;    // culot tronqué, comme tout corps d'entrée
    h.segments.push_back(s);
    z = s.z1_m;
  }

  h.length_m = z;
  for (const auto& s : h.segments)
    h.max_diameter_m = std::max(h.max_diameter_m,
                                2.0 * std::max(s.r0_m, s.r1_m));
  h.valid = !h.segments.empty() && h.length_m > 0.0;
  return h;
}

} // namespace fen::vehicle
