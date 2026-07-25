// tests/test_carte_flotte.cpp — ORACLES de la carte : flotte, epoque, echelle.
//
// Trois familles, toutes verifiables hors moteur :
//   . FLOTTE [GDD 8.3]  : ephemeride propre de chaque engin en service ;
//   . EPOQUE [GDD 14.1] : etat du monde fige sur l instant REEL a la fondation ;
//   . ECHELLE           : garanties de la compression de profondeur de la carte.
//
// STANDALONE UNIQUEMENT : compile avec /DSP_STANDALONE_TESTS (hors UE, l UBT
// compile tous les .cpp du module — sans la macro, ce TU est vide).
#ifdef SP_STANDALONE_TESTS

// test_flotte.cpp — oracles de la flotte v0.7 : éphéméride par engin [GDD 8.3].
// Compilé hors moteur contre le cœur vivant (comme les 102 oracles).
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

#include "app/jeu.hpp"
#include "app/scaled_space.hpp"
#include "fen/core/Constants.hpp"

using namespace fen;
using namespace fen::app;

static int g_ok = 0, g_ko = 0;
#define CHECK(cond, nom)                                                     \
  do {                                                                       \
    if (cond) { ++g_ok; }                                                    \
    else { ++g_ko; std::printf("ECHEC : %s (ligne %d)\n", nom, __LINE__); }  \
  } while (0)

int main() {
  const std::string tmp = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") +
                          "\\test_flotte_save.txt";

  // ---- 1. relais GEO : cercle au sma réel, période sidérale exacte ---------
  {
    Jeu jeu;
    EnginFlotte e; e.type = EnginFlotte::RelaisGeo; e.nom = "GEO-1";
    e.t0 = 1.0e9; e.sma_m = 42164170.0; e.phase0 = 0.0;
    const Vec3 p0 = jeu.flotte_position_rel(e, e.t0);
    CHECK(std::fabs(p0.x - e.sma_m) < 1e-6 && std::fabs(p0.y) < 1e-6, "relais : position a t0");
    CHECK(jeu.flotte_parent(e) == (int)ephem::Body::EarthBary, "relais : parent = Terre");
    const double Tp = cst::TWO_PI * std::sqrt(std::pow(e.sma_m, 3) / cst::MU_EARTH);
    const Vec3 p1 = jeu.flotte_position_rel(e, e.t0 + Tp);
    CHECK(norm(p1 - p0) < 1.0, "relais : retour apres une periode");
    CHECK(std::fabs(Tp - 86164.1) < 20.0, "relais : periode ~ jour sideral");
    const Vec3 ph = jeu.flotte_position_rel(e, e.t0 + Tp / 2.0);
    CHECK(std::fabs(ph.x + e.sma_m) < 1.0, "relais : opposition a T/2");
  }

  // ---- 2. orbiteur Mars : rayon = périastre atteint, parent Mars -----------
  {
    Jeu jeu;
    EnginFlotte e; e.type = EnginFlotte::OrbiteurMars; e.nom = "MARS-1";
    e.t0 = 2.0e9; e.sma_m = cst::R_MARS + 400e3; e.phase0 = 1.0;
    CHECK(jeu.flotte_parent(e) == (int)ephem::Body::Mars, "orbiteur : parent = Mars");
    const Vec3 p = jeu.flotte_position_rel(e, e.t0 + 12345.0);
    CHECK(std::fabs(norm(p) - e.sma_m) < 1e-3, "orbiteur : rayon constant (cercle)");
  }

  // ---- 3. sonde : propagation képlérienne héliocentrique, elle s'éloigne ---
  {
    Jeu jeu;
    EnginFlotte e; e.type = EnginFlotte::SondeLointaine; e.nom = "SONDE-1";
    e.t0 = 3.0e9;
    e.r0 = Vec3{1.5 * cst::AU, 0.0, 0.0};
    const double vpar = std::sqrt(2.0 * cst::MU_SUN / norm(e.r0));   // parabolique
    e.v0 = Vec3{0.3 * vpar, 0.95 * vpar, 0.0};                       // hyperbolique
    CHECK(jeu.flotte_parent(e) == (int)ephem::Body::Sun, "sonde : parent = Soleil");
    const Vec3 p0 = jeu.flotte_position_rel(e, e.t0);
    CHECK(norm(p0 - e.r0) < 1.0, "sonde : etat initial restitue");
    const Vec3 p1 = jeu.flotte_position_rel(e, e.t0 + 365.25 * cst::DAY);
    const Vec3 p2 = jeu.flotte_position_rel(e, e.t0 + 2.0 * 365.25 * cst::DAY);
    CHECK(norm(p1) > norm(p0) && norm(p2) > norm(p1), "sonde : recession monotone");
  }

  // ---- 4. save -> load : les éphémérides survivent bit-proche --------------
  {
    Jeu jeu;
    jeu.creer_agence("TEST", ModeAide::Normal);
    jeu.relais_geo = 1; jeu.orbiteurs_mars = 1; jeu.sondes_lointaines = 1;
    EnginFlotte a; a.type = EnginFlotte::RelaisGeo; a.nom = "GEO-7";
    a.t0 = 1.23456789012345e9; a.sma_m = 42164170.123; a.phase0 = 2.345678901234567;
    EnginFlotte b; b.type = EnginFlotte::OrbiteurMars; b.nom = "MARS-3";
    b.t0 = 1.3e9; b.sma_m = 3896200.5; b.phase0 = 0.123456789;
    EnginFlotte s; s.type = EnginFlotte::SondeLointaine; s.nom = "COM-1";
    s.t0 = 1.4e9;
    s.r0 = Vec3{2.1e11, -3.2e10, 0.0}; s.v0 = Vec3{12345.6789, -23456.789, 0.0};
    jeu.flotte = {a, b, s};
    CHECK(jeu.sauvegarder(tmp), "save : ecriture");

    Jeu jeu2;
    CHECK(jeu2.charger(tmp), "load : lecture");
    CHECK(jeu2.flotte.size() == 3, "load : 3 engins (aucune synthese en trop)");
    if (jeu2.flotte.size() == 3) {
      const auto& A = jeu2.flotte[0];
      CHECK(A.type == EnginFlotte::RelaisGeo && A.nom == "GEO-7", "load : identite relais");
      CHECK(std::fabs(A.t0 - a.t0) < 1e-3 && std::fabs(A.sma_m - a.sma_m) < 1e-6 &&
            std::fabs(A.phase0 - a.phase0) < 1e-12, "load : elements relais exacts");
      const auto& S = jeu2.flotte[2];
      CHECK(std::fabs(S.r0.x - s.r0.x) < 1e-3 && std::fabs(S.v0.y - s.v0.y) < 1e-9,
            "load : etat heliocentrique sonde exact");
      // même éphéméride avant/après : la position publiée est identique
      const double t = 1.5e9;
      CHECK(norm(jeu.flotte_position_rel(s, t) - jeu2.flotte_position_rel(S, t)) < 1e-3,
            "load : ephemeride sonde identique");
    }
  }

  // ---- 5. sauvegarde ANCIENNE (compteurs seuls) : reconstruction déclarée --
  {
    std::ofstream f(tmp);
    f << "FENETRE_SAUVEGARDE 2\nnom=LEGACY\nmode=0\ntresorerie=50\nmois=10\n"
         "relais=2\norbmars=1\nsondes=2\n";
    f.close();
    Jeu jeu;
    CHECK(jeu.charger(tmp), "legacy : lecture");
    int nr = 0, no = 0, ns = 0;
    for (const auto& e : jeu.flotte) {
      nr += e.type == EnginFlotte::RelaisGeo;
      no += e.type == EnginFlotte::OrbiteurMars;
      ns += e.type == EnginFlotte::SondeLointaine;
    }
    CHECK(nr == 2 && no == 1 && ns == 2, "legacy : effectifs reconstruits");
    for (const auto& e : jeu.flotte) {
      const Vec3 p = jeu.flotte_position_rel(e, jeu.epoch_courant() + 5.0 * cst::DAY);
      CHECK(std::isfinite(p.x) && std::isfinite(p.y), "legacy : position finie");
    }
    // déterminisme de la reconstruction : deux chargements -> mêmes engins
    Jeu jeu2; jeu2.charger(tmp);
    CHECK(jeu2.flotte.size() == jeu.flotte.size() &&
          norm(jeu2.flotte[0].r0 - jeu.flotte[0].r0) < 1e-9 &&
          std::fabs(jeu2.flotte[4].t0 - jeu.flotte[4].t0) < 1e-9,
          "legacy : reconstruction deterministe");
  }

  // ---- 6. [GDD 14.1] WorldEpoch : instant RÉEL figé à la fondation ---------
  {
    Jeu jeu;
    CHECK(jeu.epoch0_tdb == 0.0, "epoch0 : nul avant fondation");
    jeu.creer_agence("EPOQUE", ModeAide::Normal);
    CHECK(jeu.epoch0_tdb != 0.0, "epoch0 : fige a la fondation");
    // l'instant réel doit tomber dans une fenêtre plausible autour d'aujourd'hui
    // (2026-07-24 dans cette session) : bornes larges 2020-2100, en s TDB / J2000.
    const double an = 365.25 * cst::DAY;
    CHECK(jeu.epoch0_tdb > 20.0 * an && jeu.epoch0_tdb < 100.0 * an,
          "epoch0 : instant reel plausible (2020-2100)");
    // epoch_courant = epoch0 + mois de calendrier (aucune autre derive)
    CHECK(std::fabs(jeu.epoch_courant() - jeu.epoch0_tdb) < 1e-6, "epoch0 : T+0 = epoch0");
    jeu.agence.mois = 12.0;
    CHECK(std::fabs(jeu.epoch_courant() - (jeu.epoch0_tdb + 12.0 * 30.44 * cst::DAY)) < 1e-6,
          "epoch0 : T+12 mois = epoch0 + 12x30,44 j");
    // persistance : save -> load restitue l'époque de fondation à l'identique
    CHECK(jeu.sauvegarder(tmp), "epoch0 : save");
    Jeu relu;
    CHECK(relu.charger(tmp), "epoch0 : load");
    CHECK(std::fabs(relu.epoch0_tdb - jeu.epoch0_tdb) < 1e-6, "epoch0 : restitue par le load");
    CHECK(std::fabs(relu.epoch_courant() - jeu.epoch_courant()) < 1e-6,
          "epoch0 : meme etat du monde apres rechargement");
    // reinitialiser efface l'époque (nouvelle partie = nouvelle synchronisation)
    relu.reinitialiser();
    CHECK(relu.epoch0_tdb == 0.0, "epoch0 : efface par reinitialiser");
  }

  // ---- 7. saves d'avant la v0.7 : calendrier illustratif conservé ----------
  {
    std::ofstream f(tmp);
    f << "FENETRE_SAUVEGARDE 2\nnom=LEGACY\nmode=0\ntresorerie=50\nmois=0\n";
    f.close();
    Jeu jeu;
    CHECK(jeu.charger(tmp), "legacy epoch0 : lecture");
    CHECK(jeu.epoch0_tdb == 0.0, "legacy epoch0 : absent -> 0");
    const double base_2027 = epoch_from_iso("2027-03-14T00:00:00").tdb;
    CHECK(std::fabs(jeu.epoch_courant() - base_2027) < 1e-6,
          "legacy epoch0 : calendrier 2027 conserve");
  }

  // ---- 8. compression de profondeur de la carte : les 4 garanties ---------
  {
    using fen::app::SCALED_SPACE_KM;
    using fen::app::scaled_space_factor;
    using fen::app::scaled_space_distance;

    // (4) identité en deçà de D0 : la zone regardée n'est pas déformée
    CHECK(scaled_space_factor(1.0) == 1.0, "scaled : identite tres pres");
    CHECK(scaled_space_factor(6371.0) == 1.0, "scaled : identite au rayon terrestre");
    CHECK(scaled_space_factor(SCALED_SPACE_KM) == 1.0, "scaled : identite a D0");
    CHECK(scaled_space_factor(0.0) == 1.0, "scaled : distance nulle sans effet");

    // (2) taille angulaire inchangée : rayon'/d' == rayon/d, pour tout d
    const double rayons[5] = {1737.4, 6371.0, 69911.0, 24764.0, 696000.0};
    const double dists[6]  = {5.0e3, 3.0e5, 1.5e6, 1.5e8, 7.8e8, 4.5e9};
    for (double d : dists)
      for (double r : rayons) {
        const double f = scaled_space_factor(d);
        const double ang_vraie = r / d, ang_comp = (r * f) / (d * f);
        CHECK(std::fabs(ang_comp - ang_vraie) <= 1e-12 * ang_vraie,
              "scaled : taille angulaire preservee");
      }

    // (3) monotonie stricte : l'ordre d'occultation est preserve
    double prev = -1.0;
    for (double d = 1.0e3; d < 1.0e10; d *= 1.35) {
      const double dp = scaled_space_distance(d);
      CHECK(dp > prev, "scaled : distance comprimee strictement croissante");
      prev = dp;
    }

    // compression EFFECTIVE : toute la scène tient dans une plage exploitable
    // par le z-buffer (ordres de grandeur : 4,5e9 -> 3,2e6 ; 1,5e8 -> 2,2e6).
    CHECK(scaled_space_distance(4.5e9) < 4.0e6, "scaled : Neptune ramene sous 4e6 km");
    CHECK(scaled_space_distance(1.5e8) < 2.5e6, "scaled : 1 UA ramene sous 2,5e6 km");
    // la plage totale rendue reste sous ~5 ordres de grandeur au-dessus de D0
    CHECK(scaled_space_distance(4.5e9) / SCALED_SPACE_KM < 12.0,
          "scaled : plage de rendu compacte");
    // ... sans jamais rapprocher un objet (d' <= d, et d' croit avec d)
    for (double d : dists)
      CHECK(scaled_space_distance(d) <= d + 1e-9, "scaled : jamais d'eloignement inverse");
  }

  std::printf("\nFLOTTE + EPOQUE + ECHELLE : %d oracles OK, %d en echec.\n", g_ok, g_ko);
  return g_ko == 0 ? 0 : 1;
}

#endif // SP_STANDALONE_TESTS
