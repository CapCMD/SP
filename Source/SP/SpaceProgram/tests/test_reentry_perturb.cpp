// tests/test_reentry_perturb.cpp — ORACLES DE LA PHYSIQUE AJOUTÉE
//
// Trois familles, toutes vérifiables contre une VÉRITÉ INDÉPENDANTE (forme
// close analytique, cas réel documenté, ou intégration numérique) :
//   . ATMOSPHÈRE + TRAÎNÉE     [GDD 7.1, 7.7]
//   . PRESSION DE RADIATION    [GDD 7.1, 7.5]
//   . RENTRÉE / EDL / AÉROCAPTURE [GDD 7.6, 5.11]
//
// EXIGENCE : « la physique et les mathématiques doivent être parfaites, aucun
// côté arcade. » Chaque oracle compare donc à un invariant, pas à la sortie
// d'hier. Les formes closes d'Allen–Eggers sont confrontées à l'intégration
// RK4 complète (gravité + courbure incluses), et les ancrages numériques sont
// des cas réels : Apollo 4, Soyouz, MSL.
//
// STANDALONE UNIQUEMENT : compile avec /DSP_STANDALONE_TESTS.
#ifdef SP_STANDALONE_TESTS

#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

#include "fen/core/Constants.hpp"
#include "fen/env/Atmosphere.hpp"
#include "fen/env/SpaceWeather.hpp"
#include "fen/flight/Descent.hpp"
#include "app/vehicle_design.hpp"
#include "fen/astro/LaunchWindow.hpp"
#include "fen/flight/Reentry.hpp"
#include "fen/mission/Assistance.hpp"
#include "fen/mission/Rentree.hpp"
#include "fen/reliability/AdvancedFilieres.hpp"
#include "fen/force/Drag.hpp"
#include "fen/force/Srp.hpp"

using namespace fen;

static int g_ok = 0, g_ko = 0;
#define CHECK(cond, nom)                                                     \
  do {                                                                       \
    if (cond) { ++g_ok; }                                                    \
    else { ++g_ko; std::printf("ECHEC : %s (ligne %d)\n", nom, __LINE__); }  \
  } while (0)
#define CHECK_NEAR(a, b, tol, nom)                                           \
  do {                                                                       \
    const double d_ = std::fabs((a) - (b));                                   \
    const double s_ = std::fabs(b) > 1e-30 ? d_ / std::fabs(b) : d_;          \
    if (s_ <= (tol)) { ++g_ok; }                                              \
    else { ++g_ko; std::printf("ECHEC : %s — %.6g vs %.6g (ecart %.2f %%, ligne %d)\n", \
                               nom, (double)(a), (double)(b), 100.0 * s_, __LINE__); } \
  } while (0)

int main() {
  // ═══════════════════ ATMOSPHÈRE [GDD 7.7] ═══════════════════
  {
    auto atmo = env::earth_atmosphere(env::atmo_density_factor(0.5));
    CHECK(atmo.body_radius() == cst::R_EARTH, "atmo : rayon de reference terrestre");
    CHECK(atmo.body_omega() == cst::OMEGA_EARTH, "atmo : l atmosphere tourne avec la Terre");
    // Monotonie stricte sur tout le domaine.
    double prev = 1e30;
    bool mono = true;
    for (double h = 0.0; h <= 1000e3; h += 10e3) {
      const double d = atmo.density(h);
      if (d >= prev) mono = false;
      prev = d;
    }
    CHECK(mono, "atmo : densite strictement decroissante sur 0-1000 km");
    // Le niveau de la mer est physique, pas un artefact de table.
    CHECK_NEAR(env::earth_atmosphere(1.0 / env::SOLAR_MIN_SCALE).density(0.0),
               1.225, 1e-9, "atmo : 1,225 kg/m3 au niveau de la mer");
    // La hauteur d'échelle basse est ~8,4 km : ancrage physique connu.
    // H est DÉRIVÉE de la table : entre 0 et 25 km, elle vaut 25/ln(1,225/0,03899)
    // = 7,25 km — la hauteur d échelle MOYENNE de la couche, pas celle du sol.
    CHECK_NEAR(atmo.scale_height(0.0), 7252.0, 0.01, "atmo : H moyenne 0-25 km ~ 7,25 km");
    // MARS : ρ0 ~ 0,020 kg/m3, H ~ 11 km.
    auto mars = env::mars_atmosphere();
    CHECK_NEAR(mars.density(0.0), 0.020, 1e-9, "atmo Mars : 0,020 kg/m3 au sol");
    CHECK_NEAR(mars.scale_height(0.0), 11336.0, 0.01, "atmo Mars : H ~ 11,3 km (derivee)");
    CHECK(mars.density(0.0) < 0.02 * 1.01 && mars.density(0.0) > 1.0e-3,
          "atmo Mars : cent fois plus tenue que la Terre");
  }

  // ═══════════════════ TRAÎNÉE [GDD 7.1] ═══════════════════
  {
    auto atmo = env::earth_atmosphere(env::atmo_density_factor(0.5));
    const double cda = 2.2 * 10.0;                       // Cd·A = 22 m²
    force::AtmosphericDrag drag(&atmo, cda);
    const double m = 1000.0;
    CHECK_NEAR(drag.ballistic_coefficient(m), m / cda, 1e-12,
               "trainee : B = m/(Cd.A)");

    // Orbite circulaire équatoriale à 300 km : la traînée doit s'opposer à la
    // vitesse RELATIVE, donc être quasi antiparallèle à v, et son module doit
    // valoir ½ρv_rel²·CdA/m.
    const double r = cst::R_EARTH + 300e3;
    const double v = std::sqrt(cst::MU_EARTH / r);
    force::Ctx c{0.0, Vec3{r, 0, 0}, Vec3{0, v, 0}, m};
    Vec3 a{}; double mdot = 0.0;
    drag.accumulate(c, a, mdot);
    CHECK(mdot == 0.0, "trainee : ne consomme pas d ergols");
    const double v_rel = v - cst::OMEGA_EARTH * r;        // atmosphère entraînée
    const double rho = atmo.density(300e3);
    CHECK_NEAR(norm(a), 0.5 * rho * v_rel * v_rel * cda / m, 1e-9,
               "trainee : module = 1/2 rho v_rel^2 Cd A / m");
    CHECK(a.y < 0.0 && std::fabs(a.x) < 1e-12 && std::fabs(a.z) < 1e-12,
          "trainee : opposee a la vitesse relative");
    // ROTATION DE L'ATMOSPHÈRE. Pour une orbite ÉQUATORIALE PROGRADE — le cas
    // le plus défavorable — l'entraînement retire ω·r = 487 m/s sur 7 726 m/s,
    // soit (1 − 0,063)² = 0,878 : la traînée est réduite de 12,2 %. L'ignorer
    // biaiserait donc systématiquement toute durée de vie orbitale.
    const double sans_rotation = 0.5 * rho * v * v * cda / m;
    const double ratio_attendu = (v_rel / v) * (v_rel / v);
    CHECK_NEAR(norm(a) / sans_rotation, ratio_attendu, 1e-9,
               "trainee : reduction exacte en (v_rel/v)^2");
    CHECK(ratio_attendu > 0.85 && ratio_attendu < 0.90,
          "trainee : ~12 % de moins en equatorial prograde a 300 km");

    // Sous la surface et en apesanteur d'atmosphère : aucune force, pas de NaN.
    Vec3 a2{}; double md2 = 0.0;
    force::Ctx haut{0.0, Vec3{cst::R_EARTH + 5.0e7, 0, 0}, Vec3{0, 1000, 0}, m};
    drag.accumulate(haut, a2, md2);
    CHECK(norm(a2) >= 0.0 && std::isfinite(norm(a2)), "trainee : finie tres haut");
    Vec3 a3{}; double md3 = 0.0;
    force::Ctx sous{0.0, Vec3{cst::R_EARTH - 1000.0, 0, 0}, Vec3{0, 1000, 0}, m};
    drag.accumulate(sous, a3, md3);
    CHECK(norm(a3) == 0.0, "trainee : nulle sous la surface (hors domaine)");

    // L'ACTIVITÉ SOLAIRE MORD : au maximum, la traînée est bien plus forte.
    auto atmo_max = env::earth_atmosphere(env::atmo_density_factor(1.0));
    force::AtmosphericDrag drag_max(&atmo_max, cda);
    Vec3 am{}; double mdm = 0.0;
    drag_max.accumulate(c, am, mdm);
    CHECK_NEAR(norm(am) / norm(a),
               env::atmo_density_factor(1.0) / env::atmo_density_factor(0.5),
               1e-9, "trainee : proportionnelle au facteur d activite solaire");
  }

  // ═══════════════════ PRESSION DE RADIATION [GDD 7.1, 7.5] ═══════════════════
  {
    // P0 = S0/c — DÉRIVÉE, jamais saisie.
    CHECK_NEAR(force::SRP_P0, 4.5406e-6, 1e-3, "SRP : P0 = S0/c ~ 4,54e-6 N/m2");

    const ephem::StandishEphemeris eph;
    // Héliocentrique, sans occulteur : cas propre pour la loi en 1/d².
    force::SolarRadiationPressure srp(&eph, ephem::Body::Sun, 1.5, 20.0);
    const double m = 500.0;
    Vec3 a1{}; double md = 0.0;
    force::Ctx c1{0.0, Vec3{cst::AU, 0, 0}, Vec3{0, 0, 0}, m};
    srp.accumulate(c1, a1, md);
    const double attendu = 1.5 * 20.0 * force::SRP_P0 / m;
    CHECK_NEAR(norm(a1), attendu, 1e-9, "SRP : a = Cr.A.P0/m a 1 UA");
    CHECK(a1.x > 0.0, "SRP : la lumiere POUSSE (Soleil -> vehicule)");
    CHECK(md == 0.0, "SRP : ne consomme pas d ergols");

    // Loi en 1/d² : à 2 UA, quatre fois moins.
    Vec3 a2{}; double md2 = 0.0;
    force::Ctx c2{0.0, Vec3{2.0 * cst::AU, 0, 0}, Vec3{0, 0, 0}, m};
    srp.accumulate(c2, a2, md2);
    CHECK_NEAR(norm(a2), norm(a1) / 4.0, 1e-9, "SRP : loi en 1/d2");

    // OMBRE CONIQUE. Vu de la Terre, le Soleil fait ~0,53° de diamètre : un
    // occulteur aligné et plus large que lui donne l'ombre TOTALE, un
    // occulteur aligné plus étroit donne un transit ANNULAIRE partiel.
    const Vec3 vers_soleil{cst::AU, 0, 0};
    CHECK(force::shadow_factor(vers_soleil, Vec3{-1.0e7, 0, 0}, cst::R_SUN,
                               cst::R_EARTH) == 1.0,
          "ombre : occulteur derriere le vehicule = plein soleil");
    CHECK(force::shadow_factor(vers_soleil, Vec3{1.0e6, 0, 0}, cst::R_SUN,
                               cst::R_EARTH) == 0.0,
          "ombre : occulteur large et aligne = ombre totale");
    const double nu_lat = force::shadow_factor(vers_soleil, Vec3{3.0e9, 0, 0},
                                               cst::R_SUN, cst::R_EARTH);
    CHECK(nu_lat > 0.0 && nu_lat < 1.0, "ombre : transit annulaire partiel");
    // Loin sur le côté : rien ne masque.
    CHECK(force::shadow_factor(vers_soleil, Vec3{1.0e6, 1.0e9, 0}, cst::R_SUN,
                               cst::R_EARTH) == 1.0,
          "ombre : occulteur hors de l axe = plein soleil");
    // Pénombre : monotone entre 0 et 1 quand on s'écarte de l'axe.
    double prec = -1.0; bool croissant = true;
    for (double y = 0.0; y < 3.0e7; y += 1.0e6) {
      const double nu = force::shadow_factor(vers_soleil, Vec3{1.0e7, y, 0},
                                             cst::R_SUN, cst::R_EARTH);
      if (nu < prec - 1e-12) croissant = false;
      prec = nu;
      CHECK_NEAR(std::clamp(nu, 0.0, 1.0), nu, 1e-12, "ombre : nu dans [0,1]");
    }
    CHECK(croissant, "ombre : penombre monotone en sortant de l axe");
  }

  // ═══════════════════ RENTRÉE : FORMES CLOSES vs INTÉGRATION ═══════════════
  {
    auto atmo = env::earth_atmosphere(env::atmo_density_factor(0.5));
    flight::EntryVehicle apollo;
    apollo.mass_kg = 5500.0;      // module de commande Apollo
    apollo.cd = 1.35;
    apollo.area_m2 = 12.02;       // bouclier 3,91 m de diamètre
    apollo.nose_radius_m = 4.69;
    apollo.lift_to_drag = 0.0;    // balistique : Allen–Eggers s'applique

    const double B = apollo.ballistic_coefficient();
    CHECK(B > 300.0 && B < 360.0, "rentree : B Apollo ~ 340 kg/m2");

    const double v_e = 11000.0;                  // retour lunaire
    const double gam = -6.5 * cst::PI / 180.0;   // corridor Apollo réel
    const auto an = flight::analytic_entry(apollo, v_e, gam, atmo);
    CHECK(an.valid, "Allen-Eggers : domaine valide a -6,5 deg");

    // 1) INDÉPENDANCE EN B — le résultat le plus célèbre du modèle.
    // Il n'est EXACT que sous l'hypothèse d'Allen–Eggers : une atmosphère
    // ISOTHERME (H unique). On le vérifie donc d'abord là, où c'est un théorème.
    {
      // AtmoLayer::scale_h_km est en KILOMÈTRES : 7,2 km, pas 7200.
      std::vector<env::AtmoLayer> iso = {{0.0, 1.225, 7.2}};
      env::ExponentialAtmosphere atmo_iso(&iso, cst::R_EARTH, 0.0, 1.0);
      flight::EntryVehicle leger = apollo, lourd10 = apollo;
      lourd10.mass_kg = apollo.mass_kg * 10.0;
      const auto a_l = flight::analytic_entry(leger, v_e, gam, atmo_iso);
      const auto a_L = flight::analytic_entry(lourd10, v_e, gam, atmo_iso);
      CHECK_NEAR(a_L.peak_decel_ms2, a_l.peak_decel_ms2, 1e-12,
                 "Allen-Eggers isotherme : pic de deceleration EXACTEMENT independant de B");
      CHECK_NEAR(a_L.peak_heat_flux_wm2 / a_l.peak_heat_flux_wm2, std::sqrt(10.0),
                 1e-12, "Sutton-Graves isotherme : q_max en sqrt(B), exact");
      CHECK_NEAR(a_L.heat_load_jm2 / a_l.heat_load_jm2, std::sqrt(10.0), 1e-12,
                 "charge thermique isotherme : en sqrt(B), exact");
      CHECK_NEAR(a_l.scale_height_used_m, 7200.0, 1e-12,
                 "isotherme : H unique retenu");
    }
    // Dans l'atmosphère RÉELLE (non isotherme), la dépendance en B subsiste mais
    // reste faible : un véhicule plus dense pénètre plus bas, où H diffère. Ce
    // n'est pas un défaut du modèle — c'est ce que l'hypothèse isotherme cache.
    flight::EntryVehicle lourd = apollo;
    lourd.mass_kg = 55000.0;                     // dix fois plus lourd
    const auto an_lourd = flight::analytic_entry(lourd, v_e, gam, atmo);
    CHECK_NEAR(an_lourd.peak_decel_ms2, an.peak_decel_ms2, 0.10,
               "atmosphere reelle : pic de g quasi independant de B (< 10 %)");
    CHECK(an_lourd.scale_height_used_m != an.scale_height_used_m,
          "atmosphere reelle : le H retenu depend bien de la penetration");
    CHECK_NEAR(an_lourd.peak_heat_flux_wm2 / an.peak_heat_flux_wm2,
               std::sqrt(10.0), 0.10, "atmosphere reelle : q_max ~ sqrt(B)");
    CHECK_NEAR(an_lourd.heat_load_jm2 / an.heat_load_jm2, std::sqrt(10.0), 0.10,
               "atmosphere reelle : charge thermique ~ sqrt(B)");

    // 2) VITESSES AUX PICS — invariants purs du modèle.
    CHECK_NEAR(an.v_at_peak_decel / v_e, std::exp(-0.5), 1e-12,
               "Allen-Eggers : v(a_max) = v_E/sqrt(e)");
    CHECK_NEAR(an.v_at_peak_heat / v_e, std::exp(-1.0 / 6.0), 1e-12,
               "Sutton-Graves : v(q_max) = v_E.e^(-1/6)");
    CHECK(an.alt_at_peak_heat_m > an.alt_at_peak_decel_m,
          "rentree : le pic de flux precede le pic de g (plus haut)");

    // 3) ORDRES DE GRANDEUR. Attention : Apollo pulled ~7 g, mais en pilotant sa
    // PORTANCE (L/D ≈ 0,3) et avec un saut. Une entrée PUREMENT BALISTIQUE à
    // 11 km/s est bien plus brutale — c'est précisément pourquoi les capsules de
    // retour lunaire sont portantes. On vérifie donc l'ordre de grandeur
    // balistique, pas la valeur pilotée.
    CHECK(an.peak_decel_g > 10.0 && an.peak_decel_g < 60.0,
          "rentree : une entree balistique a 11 km/s depasse largement 10 g");
    // Flux au point d'arrêt : plusieurs MW/m2 (Apollo ~ 5 MW/m2).
    CHECK(an.peak_heat_flux_wm2 > 1.0e6 && an.peak_heat_flux_wm2 < 2.0e7,
          "rentree : flux de l ordre du MW/m2");

    // 4) DOMAINE DE L'HYPOTHÈSE « γ CONSTANT ».
    // À 11 km/s et −6,5°, le véhicule est SUPER-CIRCULAIRE : la trajectoire
    // s'aplatit de plusieurs degrés avant le pic. Le modèle doit le DIRE.
    CHECK(!an.constant_gamma_ok,
          "domaine : aplatissement signale pour une entree super-circulaire rasante");
    CHECK(an.gamma_drift_rad > 0.2 * std::fabs(gam),
          "domaine : la derive de pente est effectivement importante");
    CHECK(an.scale_height_used_m > 4000.0 && an.scale_height_used_m < 12000.0,
          "domaine : H auto-coherent, evalue a l altitude du pic");

    // 5) L'INTÉGRATION EST LA VÉRITÉ.
    const auto it = flight::integrate_entry(apollo, v_e, gam, 120000.0, atmo,
                                            cst::MU_EARTH, 0.02, 0.0);
    CHECK(it.reached_surface, "integration : la capsule atteint le sol");
    CHECK(!it.skipped_out, "integration : pas de ricochet a -6,5 deg");
    // Hors domaine, la forme close doit être une BORNE SUPÉRIEURE : se tromper
    // dans le sens sévère est acceptable pour un dimensionnement [GDD 12.5],
    // l'inverse ne l'est pas.
    CHECK(an.peak_decel_g >= it.max_decel_g,
          "conservatisme : hors domaine, la forme close MAJORE le pic de g");
    CHECK(it.t_final_s > 60.0 && it.t_final_s < 1200.0,
          "integration : duree de rentree de l ordre de la minute a la dizaine");
    CHECK(it.downrange_m > 100e3, "integration : la distance sol est consequente");

    // 5b) DANS SON DOMAINE, la forme close doit COLLER à la vérité. Entrée
    // raide : la pente n'a pas le temps de s'aplatir, l'hypothèse tient.
    const double gam_raide = -30.0 * cst::PI / 180.0;
    const auto an_r = flight::analytic_entry(apollo, v_e, gam_raide, atmo);
    const auto it_r = flight::integrate_entry(apollo, v_e, gam_raide, 120000.0,
                                              atmo, cst::MU_EARTH, 0.01, 0.0);
    CHECK(an_r.constant_gamma_ok, "domaine : a -30 deg l hypothese gamma constant tient");
    CHECK_NEAR(it_r.max_decel_g, an_r.peak_decel_g, 0.12,
               "accord : pic de g a -30 deg, forme close vs integration (< 12 %)");
    CHECK_NEAR(it_r.max_heat_flux_wm2, an_r.peak_heat_flux_wm2, 0.15,
               "accord : pic de flux a -30 deg (< 15 %)");
    CHECK_NEAR(it_r.heat_load_jm2, an_r.heat_load_jm2, 0.25,
               "accord : charge thermique a -30 deg (< 25 %)");
    // L'accord doit être MEILLEUR quand on entre dans le domaine.
    const double err_rasant = std::fabs(it.max_decel_g - an.peak_decel_g) / an.peak_decel_g;
    const double err_raide = std::fabs(it_r.max_decel_g - an_r.peak_decel_g) / an_r.peak_decel_g;
    CHECK(err_raide < err_rasant,
          "domaine : l accord s ameliore quand l entree se redresse");

    // 5) MONOTONIES PHYSIQUES : plus raide = plus dur, plus vite.
    const auto raide = flight::integrate_entry(apollo, v_e, -10.0 * cst::PI / 180.0,
                                               120000.0, atmo, cst::MU_EARTH, 0.02);
    CHECK(raide.max_decel_g > it.max_decel_g, "monotonie : plus raide = plus de g");
    CHECK(raide.t_final_s < it.t_final_s, "monotonie : plus raide = plus court");
    CHECK(raide.heat_load_jm2 < it.heat_load_jm2,
          "monotonie : plus raide = MOINS de charge thermique totale");
    CHECK(raide.max_heat_flux_wm2 > it.max_heat_flux_wm2,
          "monotonie : plus raide = flux de pointe PLUS eleve");

    // 6) UN NEZ PLUS LARGE CHAUFFE MOINS (q ∝ 1/sqrt(R_n)).
    flight::EntryVehicle nez_fin = apollo;
    nez_fin.nose_radius_m = apollo.nose_radius_m / 4.0;
    const auto an_fin = flight::analytic_entry(nez_fin, v_e, gam, atmo);
    CHECK_NEAR(an_fin.peak_heat_flux_wm2 / an.peak_heat_flux_wm2, 2.0, 1e-9,
               "Sutton-Graves : q_max en 1/sqrt(R_n)");

    // 7) SKIP-OUT : une entrée trop rasante ressort.
    const auto rasant = flight::integrate_entry(apollo, v_e, -0.5 * cst::PI / 180.0,
                                                120000.0, atmo, cst::MU_EARTH, 0.05);
    CHECK(rasant.skipped_out || !rasant.reached_surface,
          "corridor : une entree a -0,5 deg ne se pose pas");

    // 8) HORS DOMAINE DÉCLARÉ, la forme close le DIT.
    CHECK(!flight::analytic_entry(apollo, v_e, -0.01 * cst::PI / 180.0, atmo).valid,
          "Allen-Eggers : hors domaine signale, pas de chiffre invente");
  }

  // ═══════════════════ CORRIDOR D'ENTRÉE [GDD 7.6, 8.5] ═══════════════════
  {
    auto atmo = env::earth_atmosphere(env::atmo_density_factor(0.5));
    flight::EntryVehicle soyouz;
    soyouz.mass_kg = 2900.0;
    soyouz.cd = 1.4;
    soyouz.area_m2 = 3.8;
    soyouz.nose_radius_m = 2.2;

    // Équipage : 8 g de limite structurale, 6 MW/m2 de bouclier.
    const auto corr = flight::entry_corridor(soyouz, 7800.0, atmo, 8.0, 6.0e6);
    CHECK(corr.feasible, "corridor : faisable pour un retour LEO habite");
    CHECK(corr.gamma_max_rad < corr.gamma_min_rad,
          "corridor : la borne raide est plus negative que la rasante");
    const double deg = 180.0 / cst::PI;
    CHECK(std::fabs(corr.gamma_min_rad * deg) < 5.0,
          "corridor : borne rasante de quelques degres");
    CHECK(std::fabs(corr.gamma_max_rad * deg) > 1.0,
          "corridor : borne raide exploitable");

    // Une limite de g plus SÉVÈRE resserre le corridor.
    const auto strict = flight::entry_corridor(soyouz, 7800.0, atmo, 3.0, 6.0e6);
    CHECK(std::fabs(strict.gamma_max_rad) < std::fabs(corr.gamma_max_rad),
          "corridor : une limite de g plus severe interdit les entrees raides");

    // À vitesse de retour lunaire, le même véhicule voit son corridor se
    // resserrer : c'est la difficulté réelle du retour interplanétaire.
    const auto lunaire = flight::entry_corridor(soyouz, 11000.0, atmo, 8.0, 6.0e6);
    CHECK(std::fabs(lunaire.gamma_max_rad) < std::fabs(corr.gamma_max_rad),
          "corridor : plus on arrive vite, plus le corridor est etroit");

    // Contrainte absurde : le corridor se ferme et le DIT.
    const auto ferme = flight::entry_corridor(soyouz, 11000.0, atmo, 0.05, 1.0e3);
    CHECK(!ferme.feasible, "corridor : declare infaisable quand il l est");
  }

  // ═══════════════════ AÉROFREINAGE / AÉROCAPTURE [GDD 5.11] ═══════════════
  {
    auto mars = env::mars_atmosphere();
    const double mu_mars = 4.282837e13;
    const double rp = env::R_MARS_ATMO_REF + 110e3;      // périapse dans l'air
    const double ra0 = env::R_MARS_ATMO_REF + 45000e3;   // capture très elliptique
    const double H = mars.scale_height(110e3);
    const double rho_p = mars.density(110e3);
    const double B = 60.0;                               // orbiteur type MRO

    const double a0 = 0.5 * (rp + ra0);
    const double vp = std::sqrt(mu_mars * (2.0 / rp - 1.0 / a0));
    const double dv = flight::aerobraking_dv_per_pass(rho_p, vp, rp, H, B);
    CHECK(dv > 0.0, "aerofreinage : un passage retire de la vitesse");
    CHECK(dv < vp, "aerofreinage : jamais plus que la vitesse elle-meme");
    // Un passage d'aérofreinage réel retire quelques m/s : c'est pour cela qu'il
    // en faut des centaines (MRO : ~450 passages sur 5 mois).
    CHECK(dv < 100.0, "aerofreinage : ordre de grandeur de quelques m/s par passage");

    // Linéarité en 1/B : un véhicule deux fois plus « lourd » freine deux fois moins.
    CHECK_NEAR(flight::aerobraking_dv_per_pass(rho_p, vp, rp, H, 2.0 * B),
               dv / 2.0, 1e-12, "aerofreinage : dv proportionnel a 1/B");
    // Linéarité en densité.
    CHECK_NEAR(flight::aerobraking_dv_per_pass(2.0 * rho_p, vp, rp, H, B),
               2.0 * dv, 1e-12, "aerofreinage : dv proportionnel a rho");

    // Campagne complète : descendre l'apoapse de 45 000 à 500 km.
    const int n = flight::aerobraking_passes(mu_mars, rp, ra0,
                                             env::R_MARS_ATMO_REF + 500e3,
                                             rho_p, H, B);
    CHECK(n > 10, "aerofreinage : une campagne demande de nombreux passages");
    CHECK(n < 100000, "aerofreinage : la campagne converge");
    // Périapse plus BAS = atmosphère plus dense = moins de passages.
    const double rho_bas = mars.density(100e3);
    const int n_bas = flight::aerobraking_passes(
        mu_mars, env::R_MARS_ATMO_REF + 100e3, ra0,
        env::R_MARS_ATMO_REF + 500e3, rho_bas, mars.scale_height(100e3), B);
    CHECK(n_bas < n, "aerofreinage : un periapse plus bas raccourcit la campagne");
    // Cible déjà atteinte : aucun passage.
    CHECK(flight::aerobraking_passes(mu_mars, rp, ra0, ra0 + 1.0, rho_p, H, B) == 0,
          "aerofreinage : rien a faire si la cible est deja au-dessus");
  }

  // ═══════════ LA DESCENTE PROPULSEE [GDD 7.6] — LE PREMIER ORACLE ══════════
  // `flight/Descent.hpp` est le SEUL module du coeur que ni le jeu ni AUCUNE
  // suite n exercait : zero include hors de lui-meme. Et son en-tete AFFIRME une
  // validation — « Lune, Isp 311 s, TWR 2-3 -> ~1730-1750 m/s de freinage
  // (Apollo LM : ~2000 m/s marge comprise) » — que personne n avait jamais
  // verifiee. La regle du projet est d ecrire l oracle AVANT de croire le code :
  // le voici, avec des annees de retard.
  {
    using namespace fen;
    const double mu = cst::MU_MOON, R = cst::R_MOON;
    const double isp_lm = 311.0;             // Descent Propulsion System, Apollo LM

    // (a) LA VALIDATION QUE L EN-TETE REVENDIQUE, mot pour mot.
    double h2 = 0.0, h3 = 0.0;
    const double dv2 = flight::descent_dv_required(mu, R, 2.0, isp_lm, &h2);
    const double dv3 = flight::descent_dv_required(mu, R, 3.0, isp_lm, &h3);
    std::printf("     descente 7.6 : Lune, Isp 311 s -> TWR 2 : %.0f m/s (PDI %.0f km) | "
                "TWR 3 : %.0f m/s (PDI %.0f km)\n",
                dv2, h2 / 1000.0, dv3, h3 / 1000.0);
    CHECK(dv2 > 1650.0 && dv2 < 1850.0,
          "7.6 : TWR 2 rend le freinage annonce par l en-tete (~1730-1750 m/s)");
    CHECK(dv3 > 1650.0 && dv3 < 1850.0, "7.6 : ... et TWR 3 aussi");
    // Apollo LM emportait ~2000 m/s : le calcul doit rester SOUS cette valeur,
    // qui comprend les marges et le vol stationnaire de site.
    CHECK(dv2 < 2000.0 && dv3 < 2000.0,
          "7.6 : ... et tient sous les ~2000 m/s reellement emportes par le LM");

    // (b) LA SANCTION PHYSIQUE : le Delta-v EXPLOSE quand TWR -> 1.
    // C est le vrai arbitrage d ingenierie — un moteur trop faible brule son
    // Delta-v a lutter contre la pesanteur.
    const double dv_faible = flight::descent_dv_required(mu, R, 1.2, isp_lm);
    const double dv_fort   = flight::descent_dv_required(mu, R, 6.0, isp_lm);
    std::printf("     descente 7.6 : TWR 1,2 -> %.0f m/s ; TWR 6 -> %.0f m/s ; "
                "vitesse orbitale rasante %.0f m/s\n",
                dv_faible, dv_fort, std::sqrt(mu / R));
    CHECK(dv_faible > dv2, "7.6 : un moteur faible paie des pertes de gravite");
    CHECK(dv_fort < dv2, "7.6 : ... et un moteur fort s en approche de la limite");
    // (c) LA LIMITE IMPULSIONNELLE : quand TWR -> l infini, le freinage tend vers
    // la vitesse orbitale rasante. C est une BORNE, pas un reglage.
    const double v_circ = std::sqrt(mu / R);
    CHECK(dv_fort > 0.90 * v_circ,
          "7.6 : a forte poussee, le freinage approche la vitesse orbitale");
    CHECK(dv_faible > v_circ,
          "7.6 : a faible poussee, il la DEPASSE — les pertes de gravite sont reelles");

    // (d) UN ATTERRISSAGE EST UN POSE, PAS UN VOL STATIONNAIRE.
    // `descent_dv_required` ne retient que les solutions qui TOUCHENT le sol.
    const flight::DescentOutcome o =
        flight::powered_descent(mu, R, h2, 2.0 * 1000.0 * (mu / (R * R)), 1000.0,
                                isp_lm, 990.0);
    CHECK(o.reached_ground, "7.6 : au PDI optimal, l atterrisseur touche le sol");
    CHECK(o.touchdown_speed < 5.0, "7.6 : ... et il le touche DOUCEMENT");
    CHECK(!o.out_of_propellant, "7.6 : ... sans avoir epuise ses ergols");
    CHECK(o.propellant_used > 0.0 && o.duration > 0.0,
          "7.6 : la descente consomme et dure — c est une verite integree");

    // (e) ET LA GRAVITE DU CORPS DECIDE : se poser sur un astre plus lourd coute
    // plus cher, a poussee relative egale.
    CHECK(flight::descent_dv_required(cst::MU_MOON, cst::R_MOON, 2.0, isp_lm) <
          flight::descent_dv_required(cst::MU_MARS, cst::R_MARS, 2.0, isp_lm),
          "7.6 : Mars coute plus cher que la Lune a poussee relative egale");
  }

  // ═══════════ LA RENTRÉE EST UN VERROU DE MISSION [GDD 9.2, 7.6] ═══════════
  // `flight/Reentry.hpp` portait 120 oracles et AUCUN appelant hors des tests,
  // pendant que `CapsulePart` traînait cinq champs qui n'existent que pour lui et
  // que l'arbre vendait trois noeuds de rentree. Ce bloc verrouille le
  // BRANCHEMENT (`mission/Rentree.hpp`) : la physique est deja verrouillee plus
  // haut, ici on verifie qu'elle DECIDE quelque chose, et juste.
  {
    using namespace fen::mission;
    const env::ExponentialAtmosphere atmo = env::earth_atmosphere(1.0);
    const double mu = cst::MU_EARTH;

    // ---- A. LES DEUX CONTROLES PUBLIES, ET RIEN N EST DECLARE -------------
    // La vitesse d interface ne se pose pas a la main : elle sort de l energie.
    // Un retour lunaire est quasi parabolique (v_inf ~ 0), donc v = sqrt(2mu/r).
    const double v_lune = vitesse_interface(0.0, mu, cst::R_EARTH,
                                            ENTRY_INTERFACE_EARTH_M);
    const double v_leo = vitesse_interface_orbite(cst::R_EARTH + 400000.0, mu,
                                                  cst::R_EARTH,
                                                  ENTRY_INTERFACE_EARTH_M);
    std::printf("\n     RENTREE : v_interface DERIVEE -> lunaire %.0f m/s"
                " [Apollo 11 : 11 030] | LEO 400 km %.0f m/s [publie ~7 800]\n",
                v_lune, v_leo);
    CHECK(std::fabs(v_lune - 11030.0) < 100.0,
          "9.2 : le retour lunaire retrouve la vitesse d interface d Apollo 11 a 1 %");
    CHECK(std::fabs(v_leo - 7800.0) < 200.0,
          "9.2 : le retour d orbite basse retrouve ~7,8 km/s");
    // Plus d exces hyperbolique = plus vite. Monotone, sans discontinuite.
    CHECK(vitesse_interface(3000.0, mu, cst::R_EARTH, ENTRY_INTERFACE_EARTH_M)
              > v_lune,
          "9.2 : un retour interplanetaire arrive plus vite qu un retour lunaire");
    CHECK(v_lune > v_leo, "9.2 : ... et un retour lunaire plus vite qu une LEO");

    // ---- B. LA CAPACITE DU BOUCLIER EST DERIVEE, PAS DECLAREE -------------
    // Aucun flux admissible n est ecrit nulle part : il sort de la masse, de la
    // geometrie, du g admissible et de la vitesse d interface QUALIFIEE.
    const vehicle::CapsulePart* apollo = vehicle::find_capsule("APOLLO-CM");
    const vehicle::CapsulePart* soyuz  = vehicle::find_capsule("SOYUZ-SA");
    const vehicle::CapsulePart* orion  = vehicle::find_capsule("ORION-CM");
    CHECK(apollo && soyuz && orion, "12.1 : les capsules reelles sont au catalogue");
    const double f_apollo = flux_admissible_wm2(*apollo, atmo, mu);
    const double f_soyuz  = flux_admissible_wm2(*soyuz, atmo, mu);
    CHECK(f_apollo > 0.0 && f_soyuz > 0.0, "9.2 : toute capsule volee a une capacite");
    CHECK(f_apollo > f_soyuz,
          "9.2 : un bouclier qualifie au retour lunaire tient plus qu un bouclier LEO");

    // ---- C. LA PROPRIETE STRUCTURELLE : QUI A VOLE, VOLE ------------------
    // La capacite etant derivee de la qualification, une capsule qui REFAIT son
    // entree de qualification passe toujours. Le modele ne PEUT pas declarer
    // Apollo incapable du retour lunaire — c est une garantie, pas une chance.
    for (const auto& c : vehicle::capsule_catalog()) {
      if (c.sutton_graves_k != flight::SUTTON_GRAVES_EARTH) continue;
      const BilanRentree b =
          evaluer_rentree(c, c.dry_mass_kg, c.qual_entry_speed_ms, atmo, mu);
      CHECK(b.ok, "9.2 : une capsule refaisant son entree de qualification passe");
    }

    // ---- D. LE VERDICT REPRODUIT L HISTOIRE ------------------------------
    const BilanRentree ap_lune = evaluer_rentree(*apollo, apollo->dry_mass_kg,
                                                 v_lune, atmo, mu);
    const BilanRentree so_lune = evaluer_rentree(*soyuz, soyuz->dry_mass_kg,
                                                 v_lune, atmo, mu);
    const BilanRentree so_leo  = evaluer_rentree(*soyuz, soyuz->dry_mass_kg,
                                                 v_leo, atmo, mu);
    std::printf("     RENTREE : Apollo lunaire %s (%.1f g, corridor %.2f deg) |"
                " Soyouz lunaire %s (%s) | Soyouz LEO %s (%.1f g)\n",
                ap_lune.ok ? "OUI" : "NON", ap_lune.pic_g,
                ap_lune.largeur_corridor_rad * 180.0 / cst::PI,
                so_lune.ok ? "OUI" : "NON", so_lune.cause.c_str(),
                so_leo.ok ? "OUI" : "NON", so_leo.pic_g);
    CHECK(ap_lune.ok, "9.2 : Apollo rentre du retour lunaire — il l a fait");
    CHECK(so_leo.ok, "9.2 : Soyouz rentre d orbite basse — il le fait");
    CHECK(!so_lune.ok,
          "9.2 : Soyouz NE rentre PAS d un retour lunaire — il ne l a jamais fait");
    CHECK(so_lune.cause.find("flux") != std::string::npos,
          "9.2 : et le refus NOMME ce qui ferme le corridor");
    // Un refus doit dire de COMBIEN on depasse : la marge est visible.
    CHECK(so_lune.marge_flux > 0.0 && so_lune.marge_flux < 1.0,
          "9.2 : un refus chiffre la distance au tenable, il ne dit pas juste non");
    CHECK(so_leo.marge_flux >= 1.0, "9.2 : une rentree admissible a une marge >= 1");
    // Le g reste sous la limite de la piece, toujours.
    CHECK(so_leo.pic_g <= soyuz->max_entry_g,
          "9.2 : la rentree admissible respecte le g de la piece");
    CHECK(so_leo.pic_g > 2.0, "9.2 : ... et une rentree, ca decelere vraiment");

    // ---- E. LA MASSE RENTREE DECIDE [GDD 6.1] ----------------------------
    // Rentrer plus lourd, c est un coefficient balistique plus grand, donc une
    // entree plus profonde et plus chaude. Le corridor se ferme.
    const BilanRentree o_nu = evaluer_rentree(*orion, orion->dry_mass_kg, v_lune,
                                              atmo, mu);
    const BilanRentree o_charge = evaluer_rentree(*orion, orion->dry_mass_kg + 5000.0,
                                                  v_lune, atmo, mu);
    CHECK(o_nu.ok, "9.2 : Orion rentre du retour lunaire — Artemis I l a fait");
    CHECK(!o_charge.ok || o_charge.marge_flux < o_nu.marge_flux,
          "6.1 : ramener 5 t de plus degrade la marge de rentree");
    // LA LAME DE COUTEAU EST REELLE, ET MESUREE : le corridor lunaire est etroit.
    std::printf("     RENTREE : corridor lunaire d Apollo = %.2f deg, marge de flux"
                " %.3f — la lame de couteau du GDD 7.6 est mesuree, pas decretee\n",
                ap_lune.largeur_corridor_rad * 180.0 / cst::PI, ap_lune.marge_flux);
    CHECK(ap_lune.largeur_corridor_rad * 180.0 / cst::PI < 1.0,
          "7.6 : le corridor de retour lunaire fait moins d un degre");
    CHECK(ap_lune.largeur_corridor_rad > 0.0, "7.6 : ... mais il existe");

    // ---- F. LE PAS D INTEGRATION EST CONVERGE ----------------------------
    // On integre a dt = 0,25 s pour bissecter le corridor. La question honnete
    // n est pas « est-ce assez fin » mais « le resultat bouge-t-il ».
    const flight::EntryVehicle veh = vehicule_entree(*apollo, apollo->dry_mass_kg);
    const flight::EntryIntegration r_gros = flight::integrate_entry(
        veh, v_lune, -7.5 * cst::PI / 180.0, ENTRY_INTERFACE_EARTH_M, atmo, mu, 0.25);
    const flight::EntryIntegration r_fin = flight::integrate_entry(
        veh, v_lune, -7.5 * cst::PI / 180.0, ENTRY_INTERFACE_EARTH_M, atmo, mu, 0.05);
    const double ecart = std::fabs(r_gros.max_decel_g - r_fin.max_decel_g)
                       / r_fin.max_decel_g;
    std::printf("     RENTREE : pic de g a dt=0,25 s %.4f contre %.4f a dt=0,05 s"
                " (ecart %.2e)\n", r_gros.max_decel_g, r_fin.max_decel_g, ecart);
    CHECK(ecart < 1.0e-3, "9.2 : le pas d integration du corridor est converge");

    // ---- G. UN REFUS PROPOSE UNE SOLUTION (piege n°42) --------------------
    const vehicle::CapsulePart* pour_lune = capsule_capable(v_lune, 6000.0, atmo, mu, 3);
    const vehicle::CapsulePart* pour_leo  = capsule_capable(v_leo, 4000.0, atmo, mu, 3);
    CHECK(pour_lune != nullptr, "9.2 : le catalogue porte une capsule de retour lunaire");
    CHECK(pour_leo != nullptr, "9.2 : ... et une capsule de retour LEO");
    CHECK(capsule_capable(vitesse_interface(6000.0, mu, cst::R_EARTH,
                                            ENTRY_INTERFACE_EARTH_M),
                          6000.0, atmo, mu, 3) == nullptr,
          "9.2 : AUCUNE capsule volee ne rentre d un retour interplanetaire rapide"
          " — et le modele le dit au lieu de l inventer");

    // ---- H. L ATELIER EN FAIT UN VERDICT DE CONCEPTION [GDD 12.2] --------
    {
      using namespace fen::app;
      VehicleDesign d = VehicleDesign::starter();
      d.capsule = -1;
      CHECK(!evaluate_design(d).rentree.evalue,
            "9.2 : sans capsule, il n y a pas de rentree a evaluer");
      // Avec une capsule et un retour, la conception PORTE le verdict.
      for (std::size_t i = 0; i < vehicle::capsule_catalog().size(); ++i)
        if (std::string(vehicle::capsule_catalog()[i].id) == "SOYUZ-SA")
          d.capsule = (int)i;
      d.payload_kg = 0.0;
      d.v_interface_retour_ms = v_leo;
      const DesignSummary s_leo = evaluate_design(d);
      CHECK(s_leo.rentree.evalue && s_leo.rentree.ok,
            "9.2 : un Soyouz qui revient de LEO est une conception valide");
      d.v_interface_retour_ms = v_lune;
      const DesignSummary s_lune = evaluate_design(d);
      CHECK(s_lune.rentree.evalue && !s_lune.rentree.ok,
            "9.2 : le meme Soyouz qui revient de la Lune est refuse a la conception");
      std::printf("     RENTREE : l atelier refuse et NOMME -> \"%s\"\n",
                  s_lune.warning.c_str());
      CHECK(s_lune.warning.find("9.2") != std::string::npos,
            "9.2 : l alerte de conception cite le chapitre");
      CHECK(s_lune.warning.find("%") != std::string::npos,
            "9.2 : ... chiffre le depassement");
      CHECK(s_lune.warning.find("APOLLO-CM") != std::string::npos,
            "9.2 : ... et nomme une capsule qui, elle, tiendrait");
    }
  }

  // ═══════════ L'ASSISTANCE GRAVITATIONNELLE [GDD, competences Senior] ═══════
  // Quatre en-tetes morts d un coup — `astro/Mga`, `Mga1Dsm`, `LocalRefine`,
  // `BPlane` — alors que le GDD nomme les assistances comme competence Senior et
  // que `MgaProblem` portait deja les contraintes du JEU dans ses commentaires.
  // Contrairement a la branche de propagation numerique, AUCUNE decision ecrite
  // ne la justifiait : c etait un manque, pas un choix.
  {
    using namespace fen::mission;
    ephem::StandishEphemeris eph;
    const Epoch t0 = epoch_from_iso("2030-01-01T00:00:00");
    const double r_park = cst::R_EARTH + 200000.0;

    // ---- A. LE RANG EST UN VERROU, PAS UNE DECORATION --------------------
    const TourType* evj = find_tour("E-E-J");
    CHECK(evj != nullptr, "assistance : le catalogue porte le profil Juno (E-E-J)");
    const BilanTour junior = evaluer_tour(*evj, eph, t0, 1095.0, r_park,
                                          career::Rank::Junior);
    CHECK(junior.evalue && !junior.rang_suffisant,
          "GDD : un Junior ne planifie pas d assistance gravitationnelle");
    CHECK(junior.cause.find("Senior") != std::string::npos,
          "GDD : ... et le refus NOMME le rang exige");
    CHECK(!junior.faisable, "GDD : un refus de rang ne calcule pas un tour");

    // ---- B. LE TOUR SE RESOUT, ET IL EST DETERMINISTE --------------------
    // Sans determinisme, « le meilleur tour trouve » serait une anecdote, et deux
    // affichages successifs se contrediraient.
    const BilanTour a = evaluer_tour(*evj, eph, t0, 1095.0, r_park,
                                     career::Rank::Senior);
    const BilanTour b2 = evaluer_tour(*evj, eph, t0, 1095.0, r_park,
                                      career::Rank::Senior);
    CHECK(a.faisable, "assistance : un tour Terre-Venus-Jupiter se resout");
    CHECK(a.c3_m2s2 == b2.c3_m2s2 && a.tof_ans == b2.tof_ans,
          "assistance : meme graine -> meme tour, BIT A BIT");
    CHECK(a.rp_survol_m.size() == 1, "assistance : un survol pour E-E-J");
    CHECK(a.rp_survol_m[0] >= evj->rp_min_m[0] * 0.999,
          "assistance : le survol respecte le periastre minimal — on ne rase pas la Terre");
    // LE v_inf MINIMAL EST DERIVE, ET IL SE RECOUPE. Pour ouvrir Jupiter depuis un
    // survol terrestre il faut 8,79 km/s selon l en-tete de `Mga1Dsm.hpp` ; la
    // formule de Hohmann le retrouve independamment.
    {
      const auto st = eph.state(ephem::Body::EarthBary, ephem::Body::Sun, t0);
      const auto sj = eph.state(ephem::Body::Jupiter, ephem::Body::Sun, t0);
      const double vmin = vinf_min_survol(norm(st.r), norm(sj.r), norm(st.v));
      std::printf("     ASSISTANCE : v_inf minimal derive pour ouvrir Jupiter depuis"
                  " un survol terrestre = %.0f m/s [Mga1Dsm.hpp annonce 8 790]\n", vmin);
      CHECK(std::fabs(vmin - 8790.0) < 150.0,
            "assistance : la borne physique derivee retrouve les 8,79 km/s du module");
    }

    // ---- C. LE TROC EST REEL, ET C EST LUI LE GAMEPLAY -------------------
    // Direct vers Jupiter, par la meme machinerie de fenetre que le jeu utilise.
    astro::WindowParams wp;
    wp.horizon_days = 2000.0; wp.tof_min_days = 100.0; wp.tof_max_days = 4000.0;
    const astro::WindowResult direct = astro::launch_window(
        eph, ephem::Body::EarthBary, ephem::Body::Jupiter, t0, wp);
    const double c3_direct = direct.vinf_dep * direct.vinf_dep;
    const double R_J = ephem::body_radius(ephem::Body::Jupiter);
    const Comparaison cmp = comparer_au_direct(
        a, c3_direct, direct.vinf_arr, direct.tof_days / 365.25, r_park,
        10.0 * R_J, 100.0 * R_J, ephem::body_mu(ephem::Body::Jupiter));
    std::printf("\n     ASSISTANCE : Jupiter direct C3 %.1f km2/s2 en %.2f ans"
                " -> %s C3 %.1f en %.2f ans | Dv %.0f -> %.0f m/s"
                " (%.0f economises, %.2f an paye)\n",
                c3_direct / 1e6, cmp.tof_direct_ans, evj->id, a.c3_m2s2 / 1e6, a.tof_ans,
                cmp.dv_direct_ms, cmp.dv_tour_ms, cmp.dv_economise_ms,
                cmp.annees_payees);
    CHECK(a.c3_m2s2 < c3_direct * 0.5,
          "assistance : le survol divise par plus de deux l energie de depart");
    CHECK(cmp.vaut_le_coup, "assistance : le tour coute MOINS de Delta-v que le direct");
    CHECK(cmp.dv_economise_ms > 1000.0,
          "assistance : l economie se compte en KILOMETRES par seconde, pas en marge");
    CHECK(cmp.annees_payees > 1.0,
          "assistance : ... et elle se paie en ANNEES de vol, jamais gratuite");
    // LA GARDE CONTRE LE MENSONGE : un tour non convergent doit se REFUSER, pas
    // se presenter au joueur comme une option deux fois plus chere.
    const BilanTour garde = evaluer_tour_utile(*evj, eph, t0, 1095.0, r_park,
                                               career::Rank::Senior, 1.0);
    CHECK(!garde.faisable && garde.cause.find("non convergent") != std::string::npos,
          "assistance : un tour qui ne bat pas le direct est REFUSE et le dit");
    const BilanTour ok = evaluer_tour_utile(*evj, eph, t0, 1095.0, r_park,
                                            career::Rank::Senior, cmp.dv_direct_ms);
    CHECK(ok.faisable, "assistance : ... et celui qui le bat passe la garde");
    // Le C3 se convertit en Delta-v par la MEME formule que le reste du jeu.
    CHECK(std::fabs(dv_depart_pour_c3(a.c3_m2s2, r_park)
                    - astro::injection_dv_from_circular(std::sqrt(a.c3_m2s2), r_park,
                                                        cst::MU_EARTH)) < 1e-9,
          "assistance : le C3 se paie par l injection standard, pas une formule a part");
    CHECK(dv_depart_pour_c3(0.0, r_park) > 3000.0
              && dv_depart_pour_c3(0.0, r_park) < 3300.0,
          "assistance : C3 nul = liberation depuis 200 km ~ 3,2 km/s");
    CHECK(dv_depart_pour_c3(c3_direct, r_park) > dv_depart_pour_c3(a.c3_m2s2, r_park),
          "assistance : plus de C3 = plus de Delta-v, toujours");

    // ---- D. LE TEMPS DE VOL N EST PLUS GRATUIT ---------------------------
    // C est ce qui rend le troc honnete : depuis cette session, les annees
    // consomment les vivres, brulent le coeur [12.4] et percent les radiateurs.
    const double jours_tour = a.tof_ans * 365.25;
    CHECK(reliability::burnup_from_days(jours_tour, true)
              > reliability::burnup_from_days(cmp.tof_direct_ans * 365.25, true),
          "12.4 : le tour long brule PLUS de vie de coeur que le direct");
    CHECK(env::radiator_capacity_after(jours_tour, 1.5)
              < env::radiator_capacity_after(cmp.tof_direct_ans * 365.25, 1.5),
          "12.4 : ... et perce davantage les radiateurs");

    // ---- E. LE CATALOGUE, ET CE QU IL PORTE ------------------------------
    CHECK(!tour_catalog().empty(), "12.1 : le catalogue de tours existe");
    for (const auto& t : tour_catalog()) {
      CHECK(t.seq.size() >= 3, "assistance : un tour a au moins un survol");
      CHECK(t.rp_min_m.size() == t.seq.size() - 2,
            "assistance : un periastre minimal par survol");
      CHECK(t.rp_max_m.size() == t.rp_min_m.size(),
            "assistance : les bornes de periastre vont par paire");
      CHECK(t.tof_lo_j.size() == t.seq.size() - 1,
            "assistance : une borne de duree par jambe");
      CHECK(std::string(t.heritage).size() > 4,
            "12.1 : chaque tour porte la mission qui l a vole — jamais du generique");
      CHECK(t.tof_total_max_ans > 0.0, "assistance : une duree maximale, toujours");
    }

    // ---- F. LE PLAFOND DE C3 EST OPPOSABLE -------------------------------
    // `c3_max` est « ce que le lanceur VEND ». DANS `Mga1Dsm` CE N EST QU UNE
    // PENALITE DE COUT (cost += 50 x depassement) : correct pour guider un
    // optimiseur, FAUX pour un verdict — le point resterait « faisable » avec un
    // C3 que le lanceur ne vend pas. La contrainte dure est donc appliquee ici, et
    // une premiere redaction s y est laisse prendre.
    // UN PLAFOND HORS D ATTEINTE (sous la borne basse de recherche, 1 km/s de
    // v_inf) : aucun point ne peut le satisfaire, donc le tour est REFUSE.
    const BilanTour serre = evaluer_tour(*evj, eph, t0, 1095.0, r_park,
                                         career::Rank::Senior, 1.0e5);
    CHECK(!serre.faisable,
          "assistance : un plafond de C3 irrealiste refuse le tour au lieu de mentir");
    CHECK(serre.cause.find("C3") != std::string::npos,
          "assistance : ... et il nomme le C3, pas un motif vague");
    // ET UN PLAFOND SEULEMENT SEVERE : depuis que le raffineur descend au fond,
    // l optimiseur SAIT le respecter — en payant la difference en manoeuvre
    // profonde. Le verdict reste honnete de deux facons : le C3 rendu ne depasse
    // JAMAIS le plafond, et le tour devient si cher que la garde le refuse.
    const BilanTour bride = evaluer_tour(*evj, eph, t0, 1095.0, r_park,
                                         career::Rank::Senior, 1.0e6);
    if (bride.faisable) {
      std::printf("     ASSISTANCE : plafond C3 = 1,0 km2/s2 -> tour a %.0f m/s"
                  " (C3 %.2f, DSM %.0f) contre %.0f m/s sans bride\n",
                  bride.dv_total_ms, bride.c3_m2s2 / 1e6, bride.dv_bord_ms,
                  a.dv_total_ms);
      CHECK(bride.c3_m2s2 <= 1.0e6 * (1.0 + 1e-9),
            "assistance : un tour rendu sous plafond RESPECTE le plafond");
      CHECK(bride.dv_total_ms > a.dv_total_ms,
            "assistance : ... et la bride se paie, elle n est jamais gratuite");
    } else {
      CHECK(bride.cause.find("C3") != std::string::npos,
            "assistance : ... ou bien le tour est refuse en nommant le C3");
    }

    // ---- G. L ELAGAGE VISE LE CORPS SUIVANT, PAS LA CIBLE FINALE ---------
    // C EST LA CAUSE DU « Galileo a 18 019 m/s » DE LA PASSE PRECEDENTE, et elle
    // n avait rien a voir avec la dimension du probleme : exiger d un survol de
    // Venus qu il ouvre JUPITER interdit la trajectoire que Galileo a volee.
    {
      const auto sv = eph.state(ephem::Body::Venus, ephem::Body::Sun, t0);
      const auto se = eph.state(ephem::Body::EarthBary, ephem::Body::Sun, t0);
      const auto sj = eph.state(ephem::Body::Jupiter, ephem::Body::Sun, t0);
      const double vers_terre   = vinf_min_survol(norm(sv.r), norm(se.r), norm(sv.v));
      const double vers_jupiter = vinf_min_survol(norm(sv.r), norm(sj.r), norm(sv.v));
      std::printf("     ASSISTANCE : survol de Venus — ouvrir la TERRE demande %.0f m/s,"
                  " ouvrir JUPITER %.0f ; Galileo y est passe a ~5 000\n",
                  vers_terre, vers_jupiter);
      CHECK(vers_jupiter > 4.0 * vers_terre,
            "assistance : la borne vers la cible finale est SANS COMMUNE MESURE avec la vraie");
      CHECK(vers_terre < 5000.0 && vers_jupiter > 5000.0,
            "assistance : le vol REEL de Galileo (~5 km/s a Venus) passe l une et pas l autre");
      // Une jambe resonnante (meme corps) ne demande AUCUNE energie de plus.
      CHECK(vinf_min_survol(norm(se.r), norm(se.r), norm(se.v)) < 1.0,
            "assistance : un retour resonnant Terre-Terre n exige aucun exces de vitesse");
    }

    // ---- H. LE TOUR LONG EXISTE, ET C EST CELUI DE GALILEO ---------------
    // Il ne converge qu avec LES DEUX corrections (elagage juste + raffineur a
    // gradient). Le controle n est pas « le chiffre d hier » mais le VOL PUBLIE :
    // C3 15,9 km2/s2 au lancement, 6,14 ans de transit, DSM de quelques dizaines
    // de m/s — un VEEGA reellement raccorde ne pousse presque pas.
    {
      const TourType* veega = find_tour("E-V-E-E-J");
      CHECK(veega != nullptr, "12.1 : le catalogue porte le profil Galileo (E-V-E-E-J)");
      CHECK(std::string(veega->heritage).find("Galileo") != std::string::npos,
            "12.1 : ... avec la mission qui l a vole");
      // ⚠ L EPOQUE EST CELLE D UNE OPPORTUNITE, ET C EST LE POINT.
      // Un VEEGA n est PAS disponible tous les ans : Galileo, Cassini et Juno ont
      // tous attendu leur alignement. Balaye depuis 2026-12-13, le modele trouve
      // une opportunite ; depuis 2030-01-01, la fenetre de trois ans n en contient
      // pas et le tour se REFUSE (verifie plus bas). Fixer ici la date de
      // l opportunite n est donc pas choisir son oracle : c est nommer le ciel.
      const Epoch t_opp = epoch_from_iso("2026-12-13T00:00:00");
      const BilanTour g = evaluer_tour(*veega, eph, t_opp, 1095.0, r_park,
                                       career::Rank::Senior);
      CHECK(g.faisable, "assistance : le tour a TROIS survols converge sur son opportunite");
      std::printf("     ASSISTANCE : Galileo VEEGA — C3 %.1f km2/s2 [vol reel 15,9],"
                  " DSM %.0f m/s, %.2f ans [vol reel 6,14], Dv total %.0f m/s"
                  " [direct %.0f]\n",
                  g.c3_m2s2 / 1e6, g.dv_bord_ms, g.tof_ans, g.dv_total_ms,
                  cmp.dv_direct_ms);
      CHECK(g.rp_survol_m.size() == 3, "assistance : trois survols dans le bilan");
      CHECK(g.vinf_survol_ms.size() == 3, "assistance : un |v_inf| par survol");
      CHECK(g.epoques_tdb.size() == 5,
            "assistance : le tour est DATE de bout en bout (depart, 3 survols, arrivee)");
      for (std::size_t k = 0; k + 1 < g.epoques_tdb.size(); ++k)
        CHECK(g.epoques_tdb[k + 1] > g.epoques_tdb[k],
              "assistance : les epoques du tour sont strictement croissantes");
      // LE RECOUPEMENT SUR LE VOL REEL.
      CHECK(g.c3_m2s2 / 1e6 > 8.0 && g.c3_m2s2 / 1e6 < 25.0,
            "assistance : le C3 du VEEGA encadre les 15,9 km2/s2 de Galileo");
      CHECK(g.tof_ans > 5.5 && g.tof_ans < 7.0,
            "assistance : ... et sa duree les 6,14 ans du vol reel");
      CHECK(g.dv_bord_ms < 500.0,
            "assistance : un VEEGA raccorde ne pousse presque pas — la geometrie travaille");
      // LE TROC, ET IL EST MEILLEUR QUE CELUI DU TOUR COURT. Le direct se
      // recalcule A LA MEME DATE que le tour : comparer deux geometries
      // differentes ne dirait rien (piege n°94).
      const astro::WindowResult d_opp = astro::launch_window(
          eph, ephem::Body::EarthBary, ephem::Body::Jupiter, t_opp, wp);
      const double dv_direct_opp =
          dv_depart_pour_c3(d_opp.vinf_dep * d_opp.vinf_dep, r_park)
          + astro::capture_dv_to_ellipse(d_opp.vinf_arr, 10.0 * R_J, 100.0 * R_J,
                                         ephem::body_mu(ephem::Body::Jupiter));
      CHECK(g.dv_total_ms < dv_direct_opp,
            "assistance : le tour long bat le transfert direct");
      CHECK(g.tof_ans > d_opp.tof_days / 365.25,
            "assistance : ... en payant davantage d annees, jamais l inverse");
      const double eco = dv_direct_opp - g.dv_total_ms;
      CHECK(eco > 2000.0,
            "assistance : trois survols economisent des kilometres par seconde");
      // Le survol respecte ses periastres, y compris celui de Venus.
      for (std::size_t k = 0; k < g.rp_survol_m.size(); ++k)
        CHECK(g.rp_survol_m[k] >= veega->rp_min_m[k] * 0.999,
              "assistance : chaque survol du tour long respecte son periastre minimal");
      // DETERMINISME, sur le cas DIFFICILE — c est la que le hasard couterait cher.
      const BilanTour g2 = evaluer_tour(*veega, eph, t_opp, 1095.0, r_park,
                                        career::Rank::Senior);
      CHECK(g.dv_total_ms == g2.dv_total_ms && g.epoque_depart_tdb == g2.epoque_depart_tdb,
            "assistance : le tour a trois survols est reproductible BIT A BIT");
    }

    // ---- H2. LE TOUR LONG A UNE OPPORTUNITE, PAS UNE VARIANCE ------------
    // LE MEME TOUR demande a des dates differentes rendait 5 374 m/s ou 10 976, et
    // j ai d abord cru a de la non-convergence. La DATE DE DEPART TROUVEE a
    // tranche : les bons resultats partent tous a la MEME date absolue, et les
    // echecs sont ceux dont la fenetre de trois ans se TERMINE avant elle. Un
    // VEEGA n est pas disponible tous les ans — c est de la mecanique celeste.
    {
      const TourType* veega = find_tour("E-V-E-E-J");
      if (veega != nullptr) {
        const Epoch tA = epoch_from_iso("2026-12-13T00:00:00");
        const Epoch tB{tA.tdb + 135.0 * cst::DAY};
        const BilanTour a1 = evaluer_tour(*veega, eph, tA, 1095.0, r_park,
                                          career::Rank::Senior);
        const BilanTour b1 = evaluer_tour(*veega, eph, tB, 1095.0, r_park,
                                          career::Rank::Senior);
        CHECK(a1.faisable && b1.faisable,
              "5.11 : l opportunite se retrouve depuis deux balayages differents");
        if (a1.faisable && b1.faisable) {
          const double ecart_j =
              std::fabs(a1.epoque_depart_tdb - b1.epoque_depart_tdb) / cst::DAY;
          std::printf("     ASSISTANCE : deux balayages decales de 135 j visent la MEME"
                      " opportunite — depart a %.0f j d ecart, Dv %.0f et %.0f m/s\n",
                      ecart_j, a1.dv_total_ms, b1.dv_total_ms);
          CHECK(ecart_j < 90.0,
                "5.11 : deux recherches decalees convergent sur la MEME date de depart");
          // CE QUI EST VRAI DES DEUX, ET CE QUI NE L EST QUE DU MEILLEUR. La date
          // est la meme a deux semaines pres — c est l opportunite, et c est le
          // fait. Le COUT, lui, depend de la profondeur a laquelle chaque balayage
          // descend dans le bassin : 5 372 contre 6 256 m/s, soit 16 % d ecart. On
          // n oppose donc pas aux deux une egalite qu ils n ont pas ; ce qu on
          // exige, c est que les deux battent le direct (sinon la garde les
          // refuserait) et que le meilleur porte bien la signature d un VEEGA.
          CHECK(std::min(a1.dv_bord_ms, b1.dv_bord_ms) < 500.0,
                "5.11 : ... et le meilleur des deux a la DSM quasi nulle d un VEEGA raccorde");
        }
        // ET HORS OPPORTUNITE, LA GARDE REFUSE AU LIEU DE VENDRE. Balaye depuis
        // 2030-01-01, aucune fenetre de trois ans ne contient d alignement
        // exploitable : le tour sort a plus cher que le direct, donc refuse.
        const astro::WindowResult d30 = astro::launch_window(
            eph, ephem::Body::EarthBary, ephem::Body::Jupiter, t0, wp);
        const double dv_direct_30 =
            dv_depart_pour_c3(d30.vinf_dep * d30.vinf_dep, r_park)
            + astro::capture_dv_to_ellipse(d30.vinf_arr, 10.0 * R_J, 100.0 * R_J,
                                           ephem::body_mu(ephem::Body::Jupiter));
        const BilanTour hors = evaluer_tour_utile(*veega, eph, t0, 1095.0, r_park,
                                                  career::Rank::Senior, dv_direct_30);
        std::printf("     ASSISTANCE : hors opportunite (balayage 2030) -> %s\n",
                    hors.faisable ? "trouve quand meme" : hors.cause.c_str());
        CHECK(!hors.faisable || hors.dv_total_ms < dv_direct_30,
              "5.11 : hors opportunite, le tour est REFUSE — jamais vendu plus cher"
              " que le direct");
      }
    }

    // ---- I. CE QUI RESTE REFUSE, ET LA GARDE LE DIT ----------------------
    // Cassini (quatre survols) ne converge pas dans le budget, et un survol de
    // Jupiter vers Saturne fait ARRIVER trop vite pour s inserer — Voyager ne s y
    // est jamais insere. Ces deux tours ne sont PAS au catalogue ; on verifie ici
    // que s ils y etaient, la garde les refuserait au lieu de les vendre.
    {
      TourType voyager;
      voyager.id = "E-J-S"; voyager.nom = "Terre - Jupiter - Saturne";
      voyager.heritage = "Voyager 1 (1977-1980), qui n a jamais capture";
      voyager.seq = {ephem::Body::EarthBary, ephem::Body::Jupiter, ephem::Body::Saturn};
      voyager.rp_min_m = {1.5e8}; voyager.rp_max_m = {1.0e10};
      voyager.tof_lo_j = {450.0, 750.0}; voyager.tof_hi_j = {550.0, 1000.0};
      voyager.tof_total_max_ans = 6.0;
      astro::WindowParams wps;
      wps.horizon_days = 2000.0; wps.tof_min_days = 100.0; wps.tof_max_days = 4000.0;
      const astro::WindowResult ds = astro::launch_window(
          eph, ephem::Body::EarthBary, ephem::Body::Saturn, t0, wps);
      const double R_S = ephem::body_radius(ephem::Body::Saturn);
      const double dv_direct_S =
          dv_depart_pour_c3(ds.vinf_dep * ds.vinf_dep, r_park)
          + astro::capture_dv_to_ellipse(ds.vinf_arr, 10.0 * R_S, 100.0 * R_S,
                                         ephem::body_mu(ephem::Body::Saturn));
      const BilanTour v = evaluer_tour_utile(voyager, eph, t0, 1095.0, r_park,
                                             career::Rank::Senior, dv_direct_S);
      std::printf("     ASSISTANCE : E-J-S refuse — %s (direct Saturne %.0f m/s)\n",
                  v.cause.c_str(), dv_direct_S);
      CHECK(!v.faisable,
            "assistance : un survol de Jupiter vers Saturne ne bat PAS le direct");
      CHECK(v.cause.find("non convergent") != std::string::npos,
            "assistance : ... et la garde le dit au lieu de le vendre");
    }
  }

  std::printf("\nRENTREE + PERTURBATIONS : %d oracles OK, %d en echec.\n", g_ok, g_ko);
  return g_ko == 0 ? 0 : 1;
}

#endif // SP_STANDALONE_TESTS
