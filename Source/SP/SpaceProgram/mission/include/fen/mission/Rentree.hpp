// fen/mission/Rentree.hpp — LA RENTRÉE EST UN VERROU DE MISSION [GDD 9.2, 7.6, 8.5]
//
// `flight/Reentry.hpp` portait 120 oracles, trois modèles (formes closes
// d'Allen–Eggers, corridor d'entrée, intégration planaire complète) et **aucun
// appelant hors des tests**. Pendant ce temps :
//   . `CapsulePart` porte CINQ champs — Cd hypersonique, section, rayon de nez,
//     finesse, limite en g — qui n'existent que pour ce module ;
//   . l'arbre technologique VEND trois nœuds de rentrée (`rentree_capsule`,
//     `reutilisation`, `rentree_lourde`) ;
//   . et l'équipage revenait de Mars sans que rien ne vérifie qu'il survive.
// Dixième cas de la famille « modèle sans consommateur », et le plus gros.
//
// ═══ CE QUI EST DÉRIVÉ, ET DE QUOI ═══
//
// 1) LA VITESSE D'INTERFACE NE SE DÉCLARE PAS. Elle sort de l'excès hyperbolique
//    d'arrivée que la fenêtre de tir CALCULE DÉJÀ (`astro::WindowResult::vinf_arr`,
//    trajet retour = même appel, corps échangés) :
//        v_interface = √(v∞² + 2µ/r_interface)
//    Contrôle immédiat : un retour lunaire est quasi parabolique (v∞ ≈ 0), donc
//    v = √(2µ/r) = 11,0 km/s à 122 km — EXACTEMENT la vitesse d'interface
//    d'Apollo 11 (36 194 ft/s). Le modèle retrouve le chiffre publié sans qu'on
//    le lui donne.
//
// 2) LA TENUE DU BOUCLIER NE SE DÉCLARE PAS NON PLUS. Le flux admissible d'un
//    ablatif n'est presque jamais public ; ce qui l'est toujours, c'est L'ENTRÉE
//    QU'IL A SURVÉCUE. On calcule donc la capacité comme **le pic de flux de sa
//    propre entrée de qualification, à la pente la plus raide que sa limite en g
//    autorise** — le cas le plus sévère auquel il a été certifié. Aucun chiffre
//    inventé : seulement la masse, la géométrie, le g admissible et la vitesse
//    d'interface qualifiée, toutes publiées. Même principe que les courbes de
//    fiabilité dérivées de `QualStatus`.
//
// 3) LE VERDICT EST LE CORRIDOR, PAS UN SEUIL. `flight::entry_corridor` croise
//    les deux contraintes — trop raide on dépasse le g ou le flux, trop rasant on
//    ricoche — et rend la limite QUI FERME (`binding_limit`). Un refus nomme donc
//    ce qui manque, comme partout ailleurs ici.
//
// ═══ CE QUE ÇA NE FAIT PAS, ET IL FAUT LE DIRE [GDD 6.8, 12.5] ═══
// . LE FLUX EST CONVECTIF SEUL. Sutton–Graves ne donne que le flux convectif au
//   point d'arrêt. Aux vitesses de retour lunaire, le flux RADIATIF du gaz choqué
//   est du même ordre : le pic total d'Apollo était de ~4-5 MW/m², le modèle en
//   annonce ~2,1. **Le chiffre affiché est donc environ la MOITIÉ du flux réel à
//   11 km/s**, et il ne faut pas le présenter comme un total.
//   CE QUI SAUVE LE VERDICT : la capacité du bouclier est dérivée avec LA MÊME
//   formule, donc le jugement est un RAPPORT entre deux flux convectifs. Il est
//   juste là où le chiffre absolu ne l'est pas. La conséquence à connaître est
//   ailleurs : le radiatif croît en v^8 environ, bien plus vite que le convectif
//   en v³, donc le modèle est OPTIMISTE aux vitesses très élevées (retour
//   interplanétaire rapide) — c'est le seul endroit où l'erreur ne va pas dans le
//   bon sens, et il est déclaré ici.
// . Pas de guidage actif : la finesse `lift_to_drag` est portée par la pièce mais
//   le corridor est calculé en BALISTIQUE. Un pilotage en portance ÉLARGIT le
//   corridor réel (Apollo l'utilisait) : le modèle est donc conservateur, et
//   l'écart va dans le sens qui refuse plutôt que dans celui qui tue.
// . Pas de rentrée à sauts (skip entry), qu'Orion utilise pour étaler la charge
//   thermique. Même sens d'erreur.
// . La charge thermique INTÉGRÉE (qui dimensionne l'épaisseur d'ablatif) est
//   calculée mais n'est pas opposée : on n'a pas de masse d'ablatif par pièce.
#pragma once
#include <algorithm>
#include <cmath>
#include <string>

#include "fen/core/Constants.hpp"
#include "fen/env/Atmosphere.hpp"
#include "fen/flight/Reentry.hpp"
#include "fen/vehicle/PartsCatalog.hpp"

namespace fen::mission {

// Altitude d'interface atmosphérique — convention NASA de l'entry interface.
// 122 km (400 000 ft) sur Terre, 125 km sur Mars. Valeurs DÉCLARÉES, ce sont les
// conventions du domaine et non des réglages.
inline constexpr double ENTRY_INTERFACE_EARTH_M = 122000.0;
inline constexpr double ENTRY_INTERFACE_MARS_M  = 125000.0;

// v_interface = √(v∞² + 2µ/r) — conservation de l'énergie sur l'hyperbole
// d'arrivée. v∞ = 0 rend exactement la vitesse de libération locale.
inline double vitesse_interface(double v_inf_ms, double mu, double body_radius_m,
                                double alt_interface_m) {
  const double r = body_radius_m + alt_interface_m;
  if (r <= 0.0) return 0.0;
  return std::sqrt(std::max(0.0, v_inf_ms * v_inf_ms) + 2.0 * mu / r);
}

// Le véhicule de rentrée que DÉCRIT une pièce du catalogue, chargé de `masse_kg`
// (masse sèche de la capsule + ce qu'elle ramène).
inline flight::EntryVehicle vehicule_entree(const vehicle::CapsulePart& c,
                                            double masse_kg) {
  flight::EntryVehicle v;
  v.mass_kg = masse_kg > 0.0 ? masse_kg : c.dry_mass_kg;
  v.cd = c.cd_hypersonic;
  v.area_m2 = c.area_m2;
  v.nose_radius_m = c.nose_radius_m;
  v.lift_to_drag = c.lift_to_drag;
  return v;
}

// ═══ LE CORRIDOR SE TRANCHE PAR L'INTÉGRATION, PAS PAR LA FORME CLOSE ═══
//
// TROUVÉ EN BRANCHANT, et c'est la raison d'être de l'exercice. Le premier jet
// appelait `flight::entry_corridor`, la forme close d'Allen–Eggers. Résultat
// mesuré : **corridor fermé pour les cinq capsules, à toutes les vitesses**, y
// compris Apollo sur le retour lunaire qu'il a réellement volé. Ce n'était pas un
// bug : Allen–Eggers est **balistique**, et à −6,5° et 11 km/s il annonce ~35 g
// là où Apollo en a mesuré 6,5. L'écart n'est pas une erreur du modèle, c'est la
// PORTANCE — Apollo entrait avec L/D = 0,3 et tenait sa trajectoire. Aucune
// capsule ne revient de la Lune en balistique, et la table le dit : le corridor
// balistique à 11 km/s est vide.
//
// `flight::integrate_entry` porte la portance dans ses équations
// (dγ/dt inclut (L/D)·D/(m·v)), détecte le SKIP-OUT et l'atteinte du sol. C'est
// donc lui qui tranche — et c'était le troisième modèle inutilisé du fichier.
// On bissecte les deux bornes ; elles sont monotones en |γ| (plus raide = plus de
// g et de flux ; plus rasant = ricochet).
//
// PAS D'INTÉGRATION PAR FRAME : `dt = 0,25 s` et 24 itérations de bissection,
// soit ~50 intégrations de ~2 400 pas. C'est une évaluation de CONCEPTION, pas
// une boucle de rendu. Un oracle vérifie que le résultat ne bouge pas à
// `dt = 0,05 s`.
inline constexpr double CORRIDOR_DT_S = 0.25;
inline constexpr int    CORRIDOR_ITER = 24;
inline constexpr double CORRIDOR_GAMMA_MAX_RAD = 0.45;   // ~26°, au-delà rien ne tient

struct PassageEntree {
  bool   survit{false};
  double pic_g{};
  double pic_flux_wm2{};
  double charge_thermique_jm2{};
  bool   ricoche{false};
};

inline PassageEntree simuler_entree(const flight::EntryVehicle& veh, double v_ms,
                                    double gamma_rad, double alt_interface_m,
                                    const env::IAtmosphere& atmo, double mu,
                                    double g_limit, double flux_limit_wm2,
                                    double sutton_graves_k) {
  const flight::EntryIntegration r = flight::integrate_entry(
      veh, v_ms, gamma_rad, alt_interface_m, atmo, mu, CORRIDOR_DT_S, 0.0,
      sutton_graves_k);
  PassageEntree p;
  p.pic_g = r.max_decel_g;
  p.pic_flux_wm2 = r.max_heat_flux_wm2;
  p.charge_thermique_jm2 = r.heat_load_jm2;
  p.ricoche = r.skipped_out;
  p.survit = !r.skipped_out && r.max_decel_g <= g_limit
             && (flux_limit_wm2 <= 0.0 || r.max_heat_flux_wm2 <= flux_limit_wm2);
  return p;
}

// Corridor intégré. `flux_limit_wm2 <= 0` : on ne contraint QUE le g — c'est le
// mode qui sert à DÉRIVER la capacité d'un bouclier depuis sa qualification.
inline flight::EntryCorridor corridor_integre(const flight::EntryVehicle& veh,
                                              double v_ms, double alt_interface_m,
                                              const env::IAtmosphere& atmo,
                                              double mu, double g_limit,
                                              double flux_limit_wm2,
                                              double sutton_graves_k) {
  flight::EntryCorridor c;
  if (veh.ballistic_coefficient() <= 0.0 || v_ms <= 0.0) return c;
  auto passe = [&](double s) {
    return simuler_entree(veh, v_ms, -s, alt_interface_m, atmo, mu, g_limit,
                          flux_limit_wm2, sutton_graves_k);
  };
  // ═══ LES DEUX BORNES NE MESURENT PAS LA MÊME CHOSE ═══
  // Une première rédaction bissectait la borne RAIDE sur « survit », qui incluait
  // l'absence de ricochet. Mesuré : corridor fermé pour Apollo sur le retour
  // lunaire qu'il a volé. La cause n'était pas la physique — c'est que la
  // trajectoire la plus rasante RICOCHE par définition, donc le test d'amorçage
  // échouait toujours, et le prédicat n'était pas monotone (un ricochet décélère
  // PEU, donc « ne dépasse pas le g » y est vrai). On sépare :
  //   . borne RAIDE  : la plus grande pente qui tienne les LIMITES (g, flux) ;
  //   . borne RASANTE : la plus petite pente qui ne RICOCHE PAS.
  // Chacune est monotone dans son propre sens, et c'est leur croisement qui est
  // le corridor [GDD 7.6].
  auto respecte_limites = [&](double s) {
    const PassageEntree p = passe(s);
    return p.pic_g <= g_limit
           && (flux_limit_wm2 <= 0.0 || p.pic_flux_wm2 <= flux_limit_wm2);
  };
  // ON CALCULE LES DEUX BORNES DANS TOUS LES CAS, MÊME QUAND ÇA NE PASSE PAS.
  // Une première rédaction sortait dès que la borne raide échouait, laissant la
  // borne rasante à zéro — et le refus se chiffrait alors à une pente quasi
  // horizontale, où l'engin RICOCHE sans chauffer. Résultat affiché : « flux à
  // 1 % du tenable » sur une rentrée refusée. Le refus doit se mesurer là où l'on
  // volerait VRAIMENT, c'est-à-dire à la pente la plus rasante qui dissipe.
  //
  // BORNE RASANTE d'abord : elle ne dépend QUE du ricochet, pas des limites.
  double s_min = 1.0e-4;
  if (passe(s_min).ricoche) {
    double slo = s_min, shi = CORRIDOR_GAMMA_MAX_RAD;
    if (passe(shi).ricoche) {
      c.binding_limit = "ricochet : aucune pente ne dissipe";
      return c;                                     // rien ne freine, à aucune pente
    }
    for (int i = 0; i < CORRIDOR_ITER; ++i) {
      const double mid = 0.5 * (slo + shi);
      if (passe(mid).ricoche) slo = mid; else shi = mid;
    }
    s_min = shi;
  }
  c.gamma_min_rad = -s_min;

  // BORNE RAIDE : la plus grande pente qui tienne les limites, cherchée AU-DESSUS
  // de la borne rasante — en dessous, la question ne se pose pas.
  double lo = s_min, hi = CORRIDOR_GAMMA_MAX_RAD;
  if (!respecte_limites(lo)) {
    // Même la pente la plus rasante qui dissipe dépasse une limite. C'est le vrai
    // refus, et on le chiffre ICI.
    const PassageEntree p = passe(lo);
    c.binding_limit = p.pic_g > g_limit ? "deceleration" : "flux thermique";
    c.gamma_max_rad = -s_min;
    return c;                                       // feasible reste faux
  }
  for (int i = 0; i < CORRIDOR_ITER; ++i) {
    const double mid = 0.5 * (lo + hi);
    if (respecte_limites(mid)) lo = mid; else hi = mid;
  }
  const double s_max = lo;
  const PassageEntree au_max = passe(s_max);
  c.binding_limit = (s_max >= 0.99 * CORRIDOR_GAMMA_MAX_RAD) ? "aucune limite atteinte"
                    : (au_max.pic_g >= 0.98 * g_limit) ? "deceleration"
                                                       : "flux thermique";
  c.gamma_max_rad = -s_max;
  c.feasible = (s_max > s_min);
  return c;
}

// ═══ LA CAPACITÉ DU BOUCLIER, DÉRIVÉE DE SA QUALIFICATION ═══
// Pic de flux de l'entrée de qualification, à la pente la plus raide que sa
// limite en g autorise — le point le plus chaud du domaine certifié. Aucun chiffre
// de tenue thermique n'est déclaré nulle part : il sort de la géométrie, de la
// masse, du g admissible et de la vitesse d'interface qualifiée, toutes publiées.
// CONSÉQUENCE STRUCTURELLE : une capsule qui refait EXACTEMENT son entrée de
// qualification passe toujours, par construction. Le modèle ne peut pas déclarer
// Apollo incapable du retour lunaire.
inline double flux_admissible_wm2(const vehicle::CapsulePart& c,
                                  const env::IAtmosphere& atmo, double mu) {
  if (c.qual_entry_speed_ms <= 0.0 || c.max_entry_g <= 0.0) return 0.0;
  const flight::EntryVehicle veh = vehicule_entree(c, c.dry_mass_kg);
  const double alt_if = (c.sutton_graves_k == flight::SUTTON_GRAVES_MARS)
                            ? ENTRY_INTERFACE_MARS_M : ENTRY_INTERFACE_EARTH_M;
  const flight::EntryCorridor q =
      corridor_integre(veh, c.qual_entry_speed_ms, alt_if, atmo, mu,
                       c.max_entry_g, 0.0, c.sutton_graves_k);
  if (!q.feasible) return 0.0;
  return simuler_entree(veh, c.qual_entry_speed_ms, q.gamma_max_rad, alt_if, atmo,
                        mu, c.max_entry_g, 0.0, c.sutton_graves_k).pic_flux_wm2;
}

// ═══ LE BILAN DE RENTRÉE ═══
struct BilanRentree {
  bool   evalue{false};          // faux = la mission ne rentre pas
  bool   ok{false};
  double v_interface_ms{};
  double flux_admissible_wm2{};
  double masse_rentree_kg{};
  flight::EntryCorridor corridor{};
  // Ce qu'on subit si l'on vise le MILIEU du corridor — le point de conception.
  double gamma_nominal_rad{};
  double pic_g{};
  double pic_flux_wm2{};
  double charge_thermique_jm2{};
  // MARGE VISIBLE PLUTÔT QUE MARGE INVENTÉE. Mesuré en branchant : à la masse de
  // qualification exacte, un kilo de plus ferme le corridor — le retour lunaire
  // est une lame de couteau. La tentation était d'appliquer un facteur de marge
  // sur le flux admissible ; il n'en existe pas de publié (le margining réel de
  // TPS est une somme quadratique sur l'ÉPAISSEUR, pas un multiplicateur de flux).
  // On ne l'invente donc pas : on AFFICHE le rapport, et le joueur voit qu'il est
  // à 100 %. Le corridor lunaire mesuré fait 0,22° — c'est la physique, pas une
  // dureté de jeu, et Apollo le décrivait déjà ainsi.
  double marge_flux{};           // flux admissible / pic subi ; < 1 = refusé
  double largeur_corridor_rad{};
  std::string cause;             // vide si ok
};

// `v_interface_ms` EST LA VITESSE À L'INTERFACE, pas un excès hyperbolique — une
// première rédaction ne prenait que v∞, ce qui rendait un retour d'orbite BASSE
// inexprimable (une orbite liée n'a pas d'excès hyperbolique, et le clamp à zéro
// faisait rentrer tout le monde à la vitesse de libération). Pour un retour
// interplanétaire, `vitesse_interface(v∞, ...)` fournit l'argument ; pour un
// retour d'orbite, c'est la vitesse orbitale à l'interface.
inline BilanRentree evaluer_rentree(const vehicle::CapsulePart& c,
                                    double masse_rentree_kg, double v_interface_ms,
                                    const env::IAtmosphere& atmo, double mu) {
  BilanRentree b;
  b.evalue = true;
  b.masse_rentree_kg = masse_rentree_kg;
  const double alt_if = (c.sutton_graves_k == flight::SUTTON_GRAVES_MARS)
                            ? ENTRY_INTERFACE_MARS_M : ENTRY_INTERFACE_EARTH_M;
  b.v_interface_ms = v_interface_ms;
  b.flux_admissible_wm2 = flux_admissible_wm2(c, atmo, mu);
  if (b.flux_admissible_wm2 <= 0.0) {
    b.cause = "capsule sans qualification de rentree";
    return b;
  }
  const flight::EntryVehicle veh = vehicule_entree(c, masse_rentree_kg);
  b.corridor = corridor_integre(veh, b.v_interface_ms, alt_if, atmo, mu,
                                c.max_entry_g, b.flux_admissible_wm2,
                                c.sutton_graves_k);
  if (!b.corridor.feasible) {
    // UN REFUS NOMME CE QUI FERME, pas « rentrée impossible ».
    b.cause = std::string("corridor de rentree ferme : ") + b.corridor.binding_limit;
    // DE COMBIEN EST-ON PASSÉ À CÔTÉ ? On mesure à la pente la plus RASANTE qui
    // dissipe encore — la trajectoire la plus froide qu'on pourrait réellement
    // voler. Un refus sans distance à parcourir n'est pas actionnable [piège n°42].
    if (b.corridor.gamma_min_rad < 0.0) {
      const PassageEntree au_plus_froid = simuler_entree(
          veh, b.v_interface_ms, b.corridor.gamma_min_rad, alt_if, atmo, mu,
          c.max_entry_g, b.flux_admissible_wm2, c.sutton_graves_k);
      b.marge_flux = au_plus_froid.pic_flux_wm2 > 0.0
                         ? b.flux_admissible_wm2 / au_plus_froid.pic_flux_wm2 : 0.0;
      b.pic_g = au_plus_froid.pic_g;
      b.pic_flux_wm2 = au_plus_froid.pic_flux_wm2;
    }
    return b;
  }
  b.largeur_corridor_rad =
      std::fabs(b.corridor.gamma_max_rad) - std::fabs(b.corridor.gamma_min_rad);
  // Point de conception : le MILIEU du corridor, la pente qu'un profil nominal
  // vise. Ce n'est pas une marge cachée — le corridor entier reste admissible,
  // et c'est ce point qu'on rapporte au joueur.
  b.gamma_nominal_rad = 0.5 * (b.corridor.gamma_min_rad + b.corridor.gamma_max_rad);
  const PassageEntree p = simuler_entree(veh, b.v_interface_ms, b.gamma_nominal_rad,
                                         alt_if, atmo, mu, c.max_entry_g,
                                         b.flux_admissible_wm2, c.sutton_graves_k);
  b.pic_g = p.pic_g;
  b.pic_flux_wm2 = p.pic_flux_wm2;
  b.charge_thermique_jm2 = p.charge_thermique_jm2;
  b.marge_flux = p.pic_flux_wm2 > 0.0 ? b.flux_admissible_wm2 / p.pic_flux_wm2 : 0.0;
  b.ok = true;
  return b;
}

// Vitesse à l'interface pour un retour d'ORBITE (liée) de rayon d'apoastre donné.
// Vis-viva sur l'ellipse de rentrée, périgée sous l'interface.
inline double vitesse_interface_orbite(double r_apo_m, double mu,
                                       double body_radius_m,
                                       double alt_interface_m) {
  const double r = body_radius_m + alt_interface_m;
  // Ellipse dont l'apoastre est l'orbite quittée et le périastre bien sous
  // l'interface (freinage de désorbitation) : on retient a ≈ (r_apo + r)/2, ce
  // qui MINORE légèrement la vitesse — sens conservateur inverse, donc déclaré.
  const double a = 0.5 * (std::max(r_apo_m, r) + r);
  if (a <= 0.0 || r <= 0.0) return 0.0;
  return std::sqrt(std::max(0.0, mu * (2.0 / r - 1.0 / a)));
}

// La capsule du catalogue qui SAIT rentrer à cette vitesse, la plus légère
// d'abord — de quoi nommer une solution quand on refuse [piège n°42].
inline const vehicle::CapsulePart* capsule_capable(double v_interface_ms,
                                                   double masse_rentree_kg,
                                                   const env::IAtmosphere& atmo,
                                                   double mu, int equipage = 0) {
  const vehicle::CapsulePart* best = nullptr;
  for (const auto& c : vehicle::capsule_catalog()) {
    if (c.crew < equipage) continue;
    if (c.sutton_graves_k != flight::SUTTON_GRAVES_EARTH) continue;
    const BilanRentree b =
        evaluer_rentree(c, masse_rentree_kg, v_interface_ms, atmo, mu);
    if (!b.ok) continue;
    if (!best || c.dry_mass_kg < best->dry_mass_kg) best = &c;
  }
  return best;
}

} // namespace fen::mission
