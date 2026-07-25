// fen/flight/Reentry.hpp — RENTRÉE ATMOSPHÉRIQUE, EDL, AÉROFREINAGE [GDD 7.6, 5.11]
//
// Chapitre entier du GDD qui n'avait AUCUNE implémentation : `flight/Descent.hpp`
// traite explicitement l'atterrissage SANS atmosphère. Or [GDD 7.6] exige que
// « la rentrée atmosphérique, l'aérofreinage et l'EDL soient modélisés comme des
// problèmes COUPLÉS de vitesse, angle d'entrée, chauffage, marge structurale,
// masse, géométrie et environnement atmosphérique — avec la même rigueur
// physique que les phases orbitales », et [GDD 8.5] leur assigne « les seuils
// les plus stricts du jeu ».
//
// ═══ DOCTRINE : DEUX NIVEAUX, ET ILS DOIVENT S'ACCORDER ═══
// 1. Les FORMES CLOSES (Allen–Eggers, Sutton–Graves) donnent au joueur de quoi
//    DIMENSIONNER : pic de décélération, pic de flux, charge thermique, corridor
//    d'entrée. Elles sont exactes sous leurs hypothèses, et ces hypothèses sont
//    écrites.
// 2. L'INTÉGRATION est la VÉRITÉ : gravité, courbure, portance, atmosphère
//    réelle. C'est elle qui tranche en vol.
// Les oracles vérifient que (1) et (2) s'accordent : une forme close qui dérive
// de la vérité est un mensonge confortable, exactement ce que le GDD refuse.
//
// ═══ ALLEN–EGGERS (1958) ═══
// Hypothèses : entrée balistique (pas de portance), pente γ CONSTANTE pendant
// l'impulsion de décélération, atmosphère exponentielle, gravité négligeable
// devant la traînée sur ce segment, planète plate. Domaine : entrées raides
// (|γ| ≳ 5°) et rapides. En posant B = m/(Cd·A), s = |sin γ| :
//
//     v(h) = v_E · exp( −H·ρ(h) / (2·B·s) )
//     a_max = v_E²·s / (2·e·H)          [INDÉPENDANT de B — résultat célèbre]
//     v(a_max) = v_E·e^(−1/2) ≈ 0,607·v_E,     ρ(a_max) = B·s/H
//
// ═══ SUTTON–GRAVES ═══
//     q_stag = k · √(ρ/R_n) · v³        (W/m², point d'arrêt)
// k = 1,7415e-4 SI pour l'air terrestre, 1,898e-4 pour l'atmosphère de CO2 de
// Mars. Couplé à Allen–Eggers :
//     v(q_max) = v_E·e^(−1/6) ≈ 0,846·v_E,     ρ(q_max) = B·s/(3H)
//     q_max = k·√(B·s/(3·H·R_n))·v_E³·e^(−1/2)
//     Q_total = k·v_E²·√(π·B·H/(s·R_n))        (J/m², intégrale exacte)
//
// Ces trois dernières formes sont DÉRIVÉES ici, pas recopiées : elles tombent
// du changement de variable u = H·ρ/(2·B·s), pour lequel v = v_E·e^(−u).
#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

#include "fen/core/Constants.hpp"
#include "fen/env/Atmosphere.hpp"

namespace fen::flight {

// Constante de Sutton–Graves, en unités SI (kg^0.5/m).
inline constexpr double SUTTON_GRAVES_EARTH = 1.7415e-4;
inline constexpr double SUTTON_GRAVES_MARS  = 1.898e-4;

// ---------------------------------------------------------------------------
// LE VÉHICULE DE RENTRÉE — ce que le joueur dimensionne.
// ---------------------------------------------------------------------------
struct EntryVehicle {
  double mass_kg{};
  double cd{};                  // coefficient de traînée hypersonique (~1,4 capsule)
  double area_m2{};             // section de référence
  double nose_radius_m{1.0};    // rayon de nez : PLUS IL EST GRAND, MOINS ÇA CHAUFFE
  double lift_to_drag{0.0};     // 0 = balistique ; ~0,3 capsule pilotée

  // LE coefficient qui gouverne tout : B = m/(Cd·A), en kg/m².
  double ballistic_coefficient() const {
    const double cda = cd * area_m2;
    return cda > 0.0 ? mass_kg / cda : 0.0;
  }
};

// ---------------------------------------------------------------------------
// 1) FORMES CLOSES
// ---------------------------------------------------------------------------

// Altitude à laquelle l'atmosphère atteint la densité `rho` (inversion par
// bissection : la table est exponentielle PAR MORCEAUX, donc une inversion
// analytique palier par palier serait fausse aux frontières).
inline double altitude_for_density(const env::IAtmosphere& atmo, double rho) {
  if (rho <= 0.0) return 0.0;
  double lo = 0.0, hi = 200000.0;
  for (int i = 0; i < 80; ++i) {
    const double mid = 0.5 * (lo + hi);
    if (atmo.density(mid) > rho) lo = mid; else hi = mid;
  }
  return 0.5 * (lo + hi);
}

struct AnalyticEntry {
  double peak_decel_ms2{};      // pic de décélération (m/s²)
  double peak_decel_g{};        // ... en g terrestres — la contrainte équipage
  double v_at_peak_decel{};     // vitesse au pic
  double alt_at_peak_decel_m{};
  double peak_heat_flux_wm2{};  // pic de flux au point d'arrêt
  double v_at_peak_heat{};
  double alt_at_peak_heat_m{};
  double heat_load_jm2{};       // charge thermique intégrée — dimensionne l'ablatif
  bool   valid{false};          // faux si la pente est trop rasante (hors domaine)

  // ═══ DOMAINE DE L'HYPOTHÈSE « γ CONSTANT » ═══
  // Allen–Eggers suppose la pente FIGÉE pendant la traversée. Or
  //     dγ/dt = (v/r − g/v)·cos γ
  // et dès que v dépasse la vitesse circulaire locale (retour lunaire ou
  // interplanétaire), ce terme est POSITIF : la trajectoire s'APLATIT avant
  // d'atteindre le pic. La pente réelle au pic est alors plus rasante que celle
  // de l'interface, et le pic de décélération PLUS FAIBLE que la forme close.
  // `gamma_drift_rad` estime cet aplatissement. Quand il n'est plus petit devant
  // |γ|, `constant_gamma_ok` passe à faux : la forme close reste une BORNE
  // SUPÉRIEURE (donc conservatrice, ce qui est le bon sens de l'erreur pour un
  // dimensionnement), mais seule l'intégration tranche. Mesuré par oracle :
  // à 11 km/s et −6,5°, la forme close surestime le pic de g d'un facteur ~1,9.
  double gamma_drift_rad{};
  bool   constant_gamma_ok{false};
  double scale_height_used_m{};  // H retenu, évalué À L'ALTITUDE DU PIC
};

// `gamma_e_rad` : pente d'entrée, NÉGATIVE en descente. `atmo` fournit ρ et H.
inline AnalyticEntry analytic_entry(const EntryVehicle& veh, double v_entry_ms,
                                    double gamma_e_rad, const env::IAtmosphere& atmo,
                                    double sutton_graves_k = SUTTON_GRAVES_EARTH,
                                    double mu = cst::MU_EARTH,
                                    double alt_interface_m = 120000.0) {
  AnalyticEntry out;
  const double B = veh.ballistic_coefficient();
  const double s = std::fabs(std::sin(gamma_e_rad));
  const double Rn = veh.nose_radius_m;
  // Hors domaine : pente quasi nulle (Allen–Eggers suppose une traversée
  // franche) ou géométrie dégénérée. On le DIT, on ne renvoie pas un chiffre.
  if (B <= 0.0 || s < 1.0e-3 || Rn <= 0.0 || v_entry_ms <= 0.0) return out;

  // ═══ H AUTO-COHÉRENT ═══
  // Le pic de décélération a lieu à ρ = B·s/H, donc à une altitude qui dépend
  // de H — lequel dépend de l'altitude. Prendre H à une altitude de référence
  // ARBITRAIRE (60 km ?) ferait dépendre le résultat d'un chiffre magique. On
  // itère donc jusqu'au point fixe : trois passes suffisent, l'application est
  // fortement contractante (H varie lentement en altitude).
  double H = atmo.scale_height(50000.0);
  double h_peak = 0.0;
  for (int i = 0; i < 6; ++i) {
    if (H <= 0.0) return out;
    h_peak = altitude_for_density(atmo, B * s / H);
    const double H_new = atmo.scale_height(h_peak);
    if (std::fabs(H_new - H) < 1.0) { H = H_new; break; }
    H = H_new;
  }
  if (H <= 0.0) return out;
  out.scale_height_used_m = H;

  const double e = std::exp(1.0);

  // --- pic de décélération : a = (s·v_E²/H)·u·e^(−2u), maximal en u = 1/2 ----
  out.peak_decel_ms2 = v_entry_ms * v_entry_ms * s / (2.0 * e * H);
  out.peak_decel_g = out.peak_decel_ms2 / cst::G0;
  out.v_at_peak_decel = v_entry_ms * std::exp(-0.5);
  out.alt_at_peak_decel_m = altitude_for_density(atmo, B * s / H);

  // --- pic de flux : q ∝ √u·e^(−3u), maximal en u = 1/6 ---------------------
  const double rho_q = B * s / (3.0 * H);
  out.peak_heat_flux_wm2 = sutton_graves_k * std::sqrt(rho_q / Rn) *
                           v_entry_ms * v_entry_ms * v_entry_ms * std::exp(-0.5);
  out.v_at_peak_heat = v_entry_ms * std::exp(-1.0 / 6.0);
  out.alt_at_peak_heat_m = altitude_for_density(atmo, rho_q);

  // --- charge thermique : Q = k·v_E²·√(π·B·H/(s·R_n)) -----------------------
  out.heat_load_jm2 = sutton_graves_k * v_entry_ms * v_entry_ms *
                      std::sqrt(cst::PI * B * H / (s * Rn));

  // --- APLATISSEMENT DE LA PENTE : le domaine de l'hypothèse ---------------
  // Temps de descente de l'interface jusqu'au pic, à pente et vitesse figées,
  // puis dérive de pente accumulée sur ce temps.
  const double r = atmo.body_radius() + 0.5 * (alt_interface_m + h_peak);
  const double g_acc = mu / (r * r);
  const double dt_to_peak =
      (alt_interface_m > h_peak) ? (alt_interface_m - h_peak) / (v_entry_ms * s) : 0.0;
  out.gamma_drift_rad =
      std::fabs(v_entry_ms / r - g_acc / v_entry_ms) * dt_to_peak;
  // Seuil DÉCLARÉ : au-delà de 20 % de la pente d'entrée, l'hypothèse ne tient
  // plus et la forme close n'est plus qu'une borne supérieure.
  out.constant_gamma_ok = out.gamma_drift_rad < 0.20 * std::fabs(gamma_e_rad);

  out.valid = true;
  return out;
}

// ---------------------------------------------------------------------------
// 2) CORRIDOR D'ENTRÉE [GDD 7.6, 8.5]
// ---------------------------------------------------------------------------
// Trop raide -> on dépasse la limite structurale ou thermique ; trop rasant ->
// on ricoche hors de l'atmosphère (skip-out) sans avoir dissipé assez d'énergie.
// Le corridor est l'intervalle de pentes qui satisfait LES DEUX.
struct EntryCorridor {
  double gamma_min_rad{};       // le plus RASANT admissible (skip-out)
  double gamma_max_rad{};       // le plus RAIDE admissible (g / flux)
  bool   feasible{false};       // faux : aucune pente ne convient
  const char* binding_limit{"aucune"};   // ce qui ferme le corridor
};

inline EntryCorridor entry_corridor(const EntryVehicle& veh, double v_entry_ms,
                                    const env::IAtmosphere& atmo,
                                    double g_limit, double heat_flux_limit_wm2,
                                    double min_velocity_loss_frac = 0.90,
                                    double sutton_graves_k = SUTTON_GRAVES_EARTH) {
  EntryCorridor c;
  const double B = veh.ballistic_coefficient();
  const double Rn = veh.nose_radius_m;
  if (B <= 0.0 || Rn <= 0.0 || v_entry_ms <= 0.0) return c;

  // Même auto-cohérence que `analytic_entry` : H est évalué à l'altitude du pic,
  // qui dépend de la pente cherchée. On itère (contraction rapide).
  double H = atmo.scale_height(50000.0);
  double s_max = 0.0, s_g = 0.0, s_q = 0.0;
  for (int i = 0; i < 6; ++i) {
    if (H <= 0.0) return c;
    // g :   v_E²·s/(2eH) ≤ g_limit·g0        ->  s ≤ 2·e·H·g_limit·g0/v_E²
    s_g = 2.0 * std::exp(1.0) * H * g_limit * cst::G0 /
          (v_entry_ms * v_entry_ms);
    // flux : k·√(B·s/(3·H·R_n))·v_E³·e^(−1/2) ≤ q_lim
    //        ->  s ≤ 3·H·R_n·( q_lim·e^(1/2) / (k·v_E³) )² / B
    const double tmp = heat_flux_limit_wm2 * std::exp(0.5) /
                       (sutton_graves_k * v_entry_ms * v_entry_ms * v_entry_ms);
    s_q = 3.0 * H * Rn * tmp * tmp / B;
    const double s_new = std::min(s_g, s_q);
    if (s_new <= 0.0) return c;
    const double H_new = atmo.scale_height(altitude_for_density(atmo, B * s_new / H));
    s_max = s_new;
    if (std::fabs(H_new - H) < 1.0) { H = H_new; break; }
    H = H_new;
  }
  c.binding_limit = (s_g < s_q) ? "deceleration" : "flux thermique";

  // --- borne RASANTE : il faut DISSIPER ------------------------------------
  // À la pente s, la vitesse résiduelle après traversée jusqu'à la densité ρ_f
  // vaut v_E·exp(−H·ρ_f/(2·B·s)). Exiger une perte d'au moins
  // `min_velocity_loss_frac` impose une pente MINIMALE :
  //     exp(−H·ρ_f/(2·B·s)) ≤ 1 − f   ->   s ≤ H·ρ_f / (2·B·|ln(1−f)|)
  // ... ce qui est encore une borne SUPÉRIEURE : à pente donnée, plus on
  // descend, plus on freine. La vraie borne rasante vient de la profondeur
  // atteignable : une entrée trop rasante RESSORT avant d'atteindre ρ_f. On la
  // pose donc géométriquement — la corde parcourue dans une couche d'épaisseur
  // ~H ne doit pas dépasser le rayon de courbure de la trajectoire.
  const double R = atmo.body_radius();
  const double s_min = std::sqrt(std::max(0.0, 2.0 * H / R));   // ≈ 1,7° sur Terre
  (void)min_velocity_loss_frac;

  c.gamma_min_rad = -std::asin(std::clamp(s_min, 0.0, 1.0));
  c.gamma_max_rad = -std::asin(std::clamp(s_max, 0.0, 1.0));
  c.feasible = (s_max > s_min);
  if (!c.feasible) c.binding_limit = "corridor ferme";
  return c;
}

// ---------------------------------------------------------------------------
// 3) LA VÉRITÉ : INTÉGRATION DE LA RENTRÉE
// ---------------------------------------------------------------------------
// Équations planaires, planète sphérique non tournante, portance dans le plan :
//     dv/dt = −D/m − g·sin γ
//     dγ/dt = (v/r − g/v)·cos γ + (L/D)·D/(m·v)
//     dh/dt = v·sin γ
//     dx/dt = v·cos γ·R/(R+h)                    (distance sol)
// avec D = ½·ρ·v²·Cd·A et g = µ/r². La gravité et la courbure sont ICI, alors
// qu'Allen–Eggers les néglige : c'est précisément l'écart que les oracles
// mesurent.
struct EntryTrajectoryPoint {
  double t{}, alt_m{}, v_ms{}, gamma_rad{}, decel_ms2{}, heat_flux_wm2{};
};

struct EntryIntegration {
  double t_final_s{};
  double v_final_ms{};
  double downrange_m{};
  double max_decel_ms2{};
  double max_decel_g{};
  double max_heat_flux_wm2{};
  double heat_load_jm2{};
  double alt_final_m{};
  bool   skipped_out{false};    // ressorti de l'atmosphère sans se poser
  bool   reached_surface{false};
  std::vector<EntryTrajectoryPoint> trace;
};

inline EntryIntegration integrate_entry(const EntryVehicle& veh, double v_entry_ms,
                                        double gamma_e_rad, double alt_entry_m,
                                        const env::IAtmosphere& atmo, double mu,
                                        double dt_s = 0.05,
                                        double stop_alt_m = 0.0,
                                        double sutton_graves_k = SUTTON_GRAVES_EARTH,
                                        int max_steps = 400000,
                                        bool keep_trace = false) {
  EntryIntegration out;
  const double R = atmo.body_radius();
  const double cda = veh.cd * veh.area_m2;
  if (veh.mass_kg <= 0.0 || cda <= 0.0) return out;

  double v = v_entry_ms, g_ = gamma_e_rad, h = alt_entry_m, x = 0.0, t = 0.0;

  // Dérivées de l'état (v, γ, h, x).
  auto deriv = [&](double vv, double gg, double hh,
                   double& dv, double& dg, double& dh, double& dx,
                   double& decel, double& q) {
    const double r = R + hh;
    const double rho = hh > 0.0 ? atmo.density(hh) : atmo.density(0.0);
    const double g_acc = mu / (r * r);
    const double D = 0.5 * rho * vv * vv * cda;          // force de traînée (N)
    decel = D / veh.mass_kg;
    dv = -decel - g_acc * std::sin(gg);
    dg = (vv > 1.0e-6)
             ? (vv / r - g_acc / vv) * std::cos(gg) +
                   veh.lift_to_drag * decel / vv
             : 0.0;
    dh = vv * std::sin(gg);
    dx = vv * std::cos(gg) * R / r;
    q = sutton_graves_k * std::sqrt(std::max(0.0, rho) / veh.nose_radius_m) *
        vv * vv * vv;
  };

  double dv1, dg1, dh1, dx1, decel, q;
  for (int step = 0; step < max_steps; ++step) {
    deriv(v, g_, h, dv1, dg1, dh1, dx1, decel, q);
    out.max_decel_ms2 = std::max(out.max_decel_ms2, decel);
    out.max_heat_flux_wm2 = std::max(out.max_heat_flux_wm2, q);
    out.heat_load_jm2 += q * dt_s;
    if (keep_trace) out.trace.push_back({t, h, v, g_, decel, q});

    // RK4 sur (v, γ, h, x).
    double dv2, dg2, dh2, dx2, dv3, dg3, dh3, dx3, dv4, dg4, dh4, dx4, dd, qq;
    deriv(v + 0.5 * dt_s * dv1, g_ + 0.5 * dt_s * dg1, h + 0.5 * dt_s * dh1,
          dv2, dg2, dh2, dx2, dd, qq);
    deriv(v + 0.5 * dt_s * dv2, g_ + 0.5 * dt_s * dg2, h + 0.5 * dt_s * dh2,
          dv3, dg3, dh3, dx3, dd, qq);
    deriv(v + dt_s * dv3, g_ + dt_s * dg3, h + dt_s * dh3,
          dv4, dg4, dh4, dx4, dd, qq);

    v  += (dt_s / 6.0) * (dv1 + 2.0 * dv2 + 2.0 * dv3 + dv4);
    g_ += (dt_s / 6.0) * (dg1 + 2.0 * dg2 + 2.0 * dg3 + dg4);
    h  += (dt_s / 6.0) * (dh1 + 2.0 * dh2 + 2.0 * dh3 + dh4);
    x  += (dt_s / 6.0) * (dx1 + 2.0 * dx2 + 2.0 * dx3 + dx4);
    t += dt_s;

    if (v <= 0.0) { v = 0.0; break; }
    if (h <= stop_alt_m) { out.reached_surface = true; break; }
    // Ressorti : au-dessus de l'interface et en montée. C'est le SKIP-OUT, le
    // vrai mode d'échec du corridor rasant [GDD 7.6].
    if (h > alt_entry_m && g_ > 0.0) { out.skipped_out = true; break; }
  }

  out.t_final_s = t;
  out.v_final_ms = v;
  out.downrange_m = x;
  out.alt_final_m = h;
  out.max_decel_g = out.max_decel_ms2 / cst::G0;
  return out;
}

// ---------------------------------------------------------------------------
// 4) AÉROFREINAGE ET AÉROCAPTURE [GDD 5.11]
// ---------------------------------------------------------------------------
// Un passage périapse dans l'atmosphère retire de l'énergie sans ergols. La
// densité de colonne parcourue au voisinage du périapse vaut, pour une
// atmosphère exponentielle :
//     ∫ρ ds ≈ ρ_p · √(2·π·H·r_p)
// d'où la perte de vitesse du passage :
//     Δv ≈ (v_p / (2·B)) · ρ_p · √(2·π·H·r_p)
// Approximation DÉCLARÉE : vitesse quasi constante au voisinage du périapse
// (vrai tant que Δv ≪ v_p, c'est-à-dire pour l'aérofreinage ; l'aérocapture, qui
// retire beaucoup en un passage, demande l'intégration ci-dessus).
inline double aerobraking_dv_per_pass(double rho_periapsis, double v_periapsis,
                                      double r_periapsis_m, double scale_height_m,
                                      double ballistic_coef) {
  if (ballistic_coef <= 0.0 || rho_periapsis <= 0.0) return 0.0;
  const double column = rho_periapsis *
                        std::sqrt(2.0 * cst::PI * scale_height_m * r_periapsis_m);
  return v_periapsis * column / (2.0 * ballistic_coef);
}

// Nombre de passages pour passer d'une apoapse à une autre, à périapse figée.
// Chaque passage retire Δv ; l'apoapse descend en conséquence (vis-viva).
inline int aerobraking_passes(double mu, double r_periapsis_m,
                              double r_apoapsis_start_m, double r_apoapsis_target_m,
                              double rho_periapsis, double scale_height_m,
                              double ballistic_coef, int max_passes = 100000) {
  if (r_apoapsis_target_m >= r_apoapsis_start_m) return 0;
  double ra = r_apoapsis_start_m;
  int n = 0;
  while (ra > r_apoapsis_target_m && n < max_passes) {
    const double a = 0.5 * (r_periapsis_m + ra);
    const double vp = std::sqrt(mu * (2.0 / r_periapsis_m - 1.0 / a));
    const double dv = aerobraking_dv_per_pass(rho_periapsis, vp, r_periapsis_m,
                                              scale_height_m, ballistic_coef);
    if (dv <= 0.0) break;
    const double vp_new = vp - dv;
    // Nouvelle orbite : même périapse, vitesse périapse réduite.
    const double inv_a_new = 2.0 / r_periapsis_m - vp_new * vp_new / mu;
    if (inv_a_new <= 0.0) break;                    // encore hyperbolique
    const double a_new = 1.0 / inv_a_new;
    const double ra_new = 2.0 * a_new - r_periapsis_m;
    if (ra_new >= ra) break;                        // ne converge plus
    ra = ra_new;
    ++n;
  }
  return n;
}

} // namespace fen::flight
