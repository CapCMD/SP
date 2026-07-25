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
#include "fen/flight/Reentry.hpp"
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

  std::printf("\nRENTREE + PERTURBATIONS : %d oracles OK, %d en echec.\n", g_ok, g_ko);
  return g_ko == 0 ? 0 : 1;
}

#endif // SP_STANDALONE_TESTS
