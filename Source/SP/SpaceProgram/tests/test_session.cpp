// tests/test_session.cpp — ORACLES DE LA SESSION (app/session.hpp).
//
// La session est ce qui reste du jeu quand on enlève le rendu : routage de
// scène, sauvegardes, modales, publication du pont. Elle est née du passage en
// rendu total UE5 (elle remplace `fen::ui::Interface`, qui n'était pas testable
// puisque mêlée à ImGui) — elle doit donc être sous oracle comme le reste.
//
// Ce qui est vérifié ici est du CONTRAT, pas de la mise en page :
//   . [GDD 14]   l'époque publiée est celle du jeu, jamais autre chose ;
//   . [GDD 8.3]  la flotte en service est publiée intégralement ;
//   . économie stricte : la FAILLITE s'impose depuis le MODÈLE, l'UI ne peut
//     ni la déclencher ni la refuser ;
//   . aucune scène de jeu sans agence créée.
//
// STANDALONE UNIQUEMENT : compile avec /DSP_STANDALONE_TESTS (hors UE, l UBT
// compile tous les .cpp du module — sans la macro, ce TU est vide).
#ifdef SP_STANDALONE_TESTS

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#include "app/session.hpp"

using namespace fen;
using namespace fen::app;

static int g_ok = 0, g_ko = 0;
#define CHECK(cond, nom)                                                     \
  do {                                                                       \
    if (cond) { ++g_ok; }                                                    \
    else { ++g_ko; std::printf("ECHEC : %s (ligne %d)\n", nom, __LINE__); }  \
  } while (0)

int main() {
  const std::string tmp = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".");

  // ---- 1. slug d'agence : un nom -> un fichier, toujours utilisable --------
  {
    CHECK(Session::slug_agence("ARES") == "ares", "slug : minuscules");
    CHECK(Session::slug_agence("Agence Spatiale") == "agence_spatiale", "slug : espaces");
    CHECK(Session::slug_agence("A!!!B") == "a_b", "slug : ponctuation groupee");
    CHECK(Session::slug_agence("  ") == "agence", "slug : vide -> repli");
    CHECK(Session::slug_agence("Zéphyr 9") == "z_phyr_9", "slug : non-ASCII remplace");
  }

  // ---- 2. pas de Monde sans agence ----------------------------------------
  // Le Monde est UNIQUE (pas de scènes séparées) : quel que soit le cadrage de
  // la caméra, on ne peut y être sans agence créée.
  {
    Session s;
    s.scene = SceneJeu::Monde; s.cadrage = Cadrage::Systeme;
    s.tick(0.016);
    CHECK(s.scene == SceneJeu::Titre, "routage : monde (cadrage systeme) sans agence -> titre");
    s.scene = SceneJeu::Monde; s.cadrage = Cadrage::Bord;
    s.tick(0.016);
    CHECK(s.scene == SceneJeu::Titre, "routage : monde (cadrage bord) sans agence -> titre");
  }

  // ---- 3. le pont reflète EXACTEMENT scène + cadrage -----------------------
  // Le Monde est unique ; le CADRAGE dit quel plan la caméra occupe. Le menu
  // utilise ce même monde (plan système) comme décor : `carte3d_active` et
  // `menu_backdrop` ne disent pas la même chose et ne doivent jamais être confondus.
  {
    Session s;
    s.nouvelle_partie("Oracle", ModeAide::Normal);
    CHECK(s.scene == SceneJeu::Monde && s.cadrage == Cadrage::Bord,
          "nouvelle partie : on entre dans le Monde, A BORD");

    s.tick(0.016);
    CHECK(g_render_bridge.scene.load() == (int)SceneJeu::Monde, "pont : scene = Monde");
    CHECK(!g_render_bridge.carte3d_active.load(), "pont : cadrage systeme inactif a bord");
    CHECK(!g_render_bridge.menu_backdrop.load(), "pont : pas de decor de menu a bord");
    CHECK(g_render_bridge.posts.n.load() == 8, "pont : les 8 postes publies a bord");

    // [M] = signet de caméra : on tire la vue au plan système SANS changer de scène.
    s.cadrage = Cadrage::Systeme;
    s.tick(0.016);
    CHECK(g_render_bridge.scene.load() == (int)SceneJeu::Monde, "pont : toujours le meme Monde");
    CHECK(g_render_bridge.carte3d_active.load(), "pont : cadrage systeme actif");
    CHECK(!g_render_bridge.menu_backdrop.load(), "pont : cadrage systeme != decor de menu");

    s.scene = SceneJeu::Titre;
    s.tick(0.016);
    CHECK(!g_render_bridge.carte3d_active.load(), "pont : cadrage systeme inactif au titre");
    CHECK(g_render_bridge.menu_backdrop.load(), "pont : decor de menu au titre");
  }

  // ---- 4. l'époque publiée est CELLE DU JEU [GDD 14] -----------------------
  {
    Session s;
    s.nouvelle_partie("Oracle", ModeAide::Normal);
    s.tick(0.016);
    const double attendue = s.jeu.epoch_courant();
    CHECK(std::fabs(g_render_bridge.epoch_tdb.load() - attendue) < 1e-6,
          "epoque : publiee = epoque de jeu");
    // Elle est publiée dans TOUTES les scènes (le menu a besoin d'un décor daté).
    s.scene = SceneJeu::Titre;
    s.tick(0.016);
    CHECK(std::fabs(g_render_bridge.epoch_tdb.load() - s.jeu.epoch_courant()) < 1e-6,
          "epoque : publiee aussi au titre");
    // Avancer d'un mois avance l'époque publiée : le temps est UNIQUE.
    const double avant = g_render_bridge.epoch_tdb.load();
    s.jeu.passer_mois();
    s.tick(0.016);
    CHECK(g_render_bridge.epoch_tdb.load() > avant, "epoque : suit le calendrier agence");
  }

  // ---- 5. la flotte en service est publiée INTEGRALEMENT [GDD 8.3] --------
  {
    Session s;
    s.nouvelle_partie("Oracle", ModeAide::Normal);
    const int n0 = (int)s.jeu.flotte.size();
    for (int i = 0; i < 3; ++i) {
      EnginFlotte e;
      e.type = EnginFlotte::RelaisGeo;
      e.nom = "GEO-" + std::to_string(i);
      e.t0 = s.jeu.epoch_courant();
      e.sma_m = 42164170.0;
      e.phase0 = 0.7 * i;
      s.jeu.flotte.push_back(e);
    }
    s.tick(0.016);
    CHECK(g_render_bridge.fleet.n.load() == n0 + 3, "flotte : tous les engins publies");
    const auto& c = g_render_bridge.fleet.craft[n0];
    CHECK(c.parent == (int)ephem::Body::EarthBary, "flotte : parent publie");
    const double r = std::sqrt(c.rel_m[0] * c.rel_m[0] + c.rel_m[1] * c.rel_m[1] +
                               c.rel_m[2] * c.rel_m[2]);
    CHECK(std::fabs(r - 42164170.0) < 1.0, "flotte : position relative a l echelle vraie");
    // Sans vol commis, rien n'est publié comme vol : pas de trace inventée.
    CHECK(!g_render_bridge.vehicle.valid.load(), "flotte : aucun vol interplanetaire fantome");
    CHECK(!g_render_bridge.geo.valid.load(), "flotte : aucun vol GEO fantome");
  }

  // ---- 6. LA FAILLITE VIENT DU MODELE, pas de l'UI ------------------------
  {
    Session s;
    s.nouvelle_partie("Oracle", ModeAide::Normal);
    s.tick(0.016);
    CHECK(s.modal == Modal::Aucun, "faillite : rien tant que l agence vit");

    s.jeu.game_over = true;
    s.jeu.raison_faillite = "tresorerie negative deux mois de suite";
    s.tick(0.016);
    CHECK(s.modal == Modal::GameOver, "faillite : imposee des que le modele bascule");

    // L'UI ne peut pas la balayer : tant que le modèle est en faillite, elle revient.
    s.modal = Modal::Aucun;
    s.tick(0.016);
    CHECK(s.modal == Modal::GameOver, "faillite : l UI ne peut pas la refuser");

    // Au titre, en revanche, il n'y a plus de partie à condamner.
    s.scene = SceneJeu::Titre;
    s.tick(0.016);
    CHECK(s.modal != Modal::GameOver, "faillite : pas de game over au menu");

    // La seule sortie propre remet le modèle à zéro.
    s.nouvelle_apres_faillite();
    CHECK(s.modal == Modal::Aucun && s.scene == SceneJeu::Titre && !s.jeu.game_over,
          "faillite : nouvelle partie repart du menu, modele remis a zero");
  }

  // ---- 7. sauvegarde : un fichier par agence, relu par le scan ------------
  {
    const std::filesystem::path dossier =
        std::filesystem::path(tmp) / "sp_oracle_saves";
    std::error_code ec;
    std::filesystem::remove_all(dossier, ec);
    std::filesystem::create_directories(dossier, ec);

    Session s;
    s.chemin_sauvegarde = (dossier / "agence.sauvegarde.txt").string();
    s.nouvelle_partie("Agence Test", ModeAide::Pro);
    CHECK(s.chemin_sauvegarde == (dossier / "agence_test.sav").string(),
          "sauvegarde : un fichier .sav par agence");
    CHECK(s.jeu.agence.mode == ModeAide::Pro, "sauvegarde : le mode d aide est retenu");
    s.jeu.agence.reussites = 3;
    s.sauvegarder_partie();
    CHECK(std::filesystem::exists(s.chemin_sauvegarde), "sauvegarde : fichier ecrit");
    CHECK(!s.saves_scannees, "sauvegarde : le scan est invalide apres ecriture");

    Session s2;
    s2.chemin_sauvegarde = (dossier / "agence.sauvegarde.txt").string();
    s2.scanner_sauvegardes();
    CHECK(s2.saves_listees.size() == 1, "scan : la partie ecrite est listee");
    CHECK(s2.saves_listees[0].label.find("Agence Test") != std::string::npos,
          "scan : le libelle porte le nom de l agence");
    CHECK(s2.saves_listees[0].label.find("3 reussite") != std::string::npos,
          "scan : le libelle porte le bilan");
    CHECK(s2.charger_partie(s2.saves_listees[0].chemin), "chargement : la partie se relit");
    CHECK(s2.jeu.agence.nom == "Agence Test", "chargement : nom restitue");
    CHECK(s2.jeu.agence.reussites == 3, "chargement : bilan restitue");
    CHECK(s2.chemin_sauvegarde == s2.saves_listees[0].chemin,
          "chargement : la partie chargee devient l active");

    // Un fichier qui n'est pas une sauvegarde n'est jamais proposé.
    { std::ofstream f(dossier / "intrus.sav"); f << "ceci n est pas une sauvegarde\n"; }
    s2.scanner_sauvegardes();
    CHECK(s2.saves_listees.size() == 1, "scan : un .sav invalide est ignore");
    CHECK(!s2.charger_partie((dossier / "intrus.sav").string()),
          "chargement : un fichier invalide est refuse");

    std::filesystem::remove_all(dossier, ec);
  }

  // ---- 8. cadrage d'arrivee : borne au corps vise --------------------------
  {
    CHECK(distance_cadrage(-1) > 1.0e8, "cadrage : vue systeme large");
    const double dTerre = distance_cadrage((int)ephem::Body::EarthBary);
    const double rTerre = ephem::body_radius(ephem::Body::EarthBary) / 1000.0;
    CHECK(dTerre > rTerre && dTerre < 100.0 * rTerre, "cadrage : Terre entiere a l ecran");
    CHECK(distance_cadrage((int)ephem::Body::Jupiter) >
          distance_cadrage((int)ephem::Body::Mercury),
          "cadrage : un corps plus gros se regarde de plus loin");
    CHECK(distance_cadrage((int)ephem::Body::Moon) >= 3000.0, "cadrage : plancher respecte");
  }

  // ---- 9. accepter un contrat : cree une Mission, repond au mail [GDD 4.1] --
  {
    Session s;
    s.nouvelle_partie("Oracle", ModeAide::Normal);
    s.tick(0.016);   // la couche ARES notifie les contrats deja jouables
    auto& G = *s.jeu.ares.etat;

    // Un contrat NON notifie ne peut pas etre accepte [GDD 10.2].
    CHECK(!s.accepter_contrat("CAT-99-inexistant"), "accept : un contrat inconnu est refuse");

    // On prend le premier contrat effectivement notifie.
    const auto pending = G.inbox.pending_contracts();
    CHECK(!pending.empty(), "accept : au moins un contrat notifie au depart");
    if (!pending.empty()) {
      const std::string cid = pending[0]->contract_id;
      const std::size_t nav = G.missions.size();
      CHECK(s.accepter_contrat(cid), "accept : le contrat notifie est accepte");
      CHECK(G.missions.size() == nav + 1, "accept : une Mission est creee");
      CHECK(G.missions.back().contract.id == cid, "accept : la Mission porte le bon contrat");
      // La FSM est entree en phase PREREQUIS (transition legale depuis RECU).
      CHECK(G.missions.back().state == mission::MissionState::Prerequisites,
            "accept : la mission passe en phase PREREQUIS [GDD 4.1]");
      // Le mail est repondu : il sort de l attente.
      CHECK(s.jeu.ares.etat->inbox.pending_contracts().size() == pending.size() - 1,
            "accept : le mail sort de l attente");
      // Accepter DEUX FOIS le meme contrat est refuse.
      CHECK(!s.accepter_contrat(cid), "accept : pas d acceptation en double");
      CHECK(G.missions.size() == nav + 1, "accept : aucune mission dupliquee");
    }
  }

  // ---- 10. VOL DE CAMERA [M] [GDD v1.2 ch.8.3, 17.4] ----------------------
  {
    // (a) le modèle de vol : bornes exactes, monotone, interpolation LOG.
    VolCamera v;
    v.actif = true; v.duree_s = 1.0;
    v.dist_depart_km = 7000.0; v.dist_arrivee_km = 9.0e8;
    v.progres = 0.0;
    CHECK(std::fabs(v.dist_courante_km() - 7000.0) < 1e-6, "vol : depart exact a p=0");
    v.progres = 1.0;
    CHECK(std::fabs(v.dist_courante_km() - 9.0e8) < 1.0, "vol : arrivee exacte a p=1");
    double prec = 0.0; bool mono = true;
    for (int i = 0; i <= 10; ++i) {
      v.progres = i / 10.0;
      const double d = v.dist_courante_km();
      if (i > 0 && d < prec) mono = false;
      prec = d;
    }
    CHECK(mono, "vol : distance monotone le long du vol");
    v.progres = 0.5;
    const double geo = std::sqrt(7000.0 * 9.0e8);        // moyenne GEOMETRIQUE
    const double ari = 0.5 * (7000.0 + 9.0e8);           // moyenne ARITHMETIQUE
    CHECK(std::fabs(v.dist_courante_km() - geo) < std::fabs(v.dist_courante_km() - ari),
          "vol : interpolation logarithmique (mi-course ~ moyenne geometrique)");

    // (b) intégration Session : [M] à bord lance un vol VERS le système.
    Session s;
    s.nouvelle_partie("Oracle", ModeAide::Normal);
    s.demarrer_vol_cadrage();
    CHECK(s.vol_cam.actif && s.vol_cam.sens == SensVol::VersSysteme,
          "vol : [M] a bord -> vol vers le systeme");
    CHECK(s.cadrage == Cadrage::Systeme, "vol : le plan systeme rend des le depart du vol");
    CHECK(g_render_bridge.focus_body.load() == (int)ephem::Body::EarthBary,
          "vol : la camera est ancree sur la Terre");
    CHECK(std::fabs(g_render_bridge.cam.dist_km.load() - s.dist_bord_km()) < 1.0,
          "vol : depart au ras de la Terre");
    const double arrivee_avant = s.vol_cam.dist_arrivee_km;
    s.demarrer_vol_cadrage();          // un vol à la fois : sans effet
    CHECK(s.vol_cam.dist_arrivee_km == arrivee_avant, "vol : un seul vol a la fois");
    s.tick(1.0);                       // le vol se termine
    CHECK(!s.vol_cam.actif, "vol : le vol se termine");
    CHECK(s.cadrage == Cadrage::Systeme, "vol vers systeme : on reste au plan systeme");
    CHECK(std::fabs(g_render_bridge.cam.dist_km.load() - Session::DIST_SYSTEME_KM) < 1.0,
          "vol : arrivee a la vue systeme");

    // (c) [M] depuis le système : vol de RETOUR ; à l'arrivée, main à la 1re personne.
    s.demarrer_vol_cadrage();
    CHECK(s.vol_cam.actif && s.vol_cam.sens == SensVol::VersBord,
          "vol : [M] au systeme -> vol de retour a bord");
    CHECK(s.cadrage == Cadrage::Systeme, "vol de retour : le systeme rend encore pendant le vol");
    s.tick(1.0);
    CHECK(!s.vol_cam.actif && s.cadrage == Cadrage::Bord,
          "vol de retour : a l arrivee, la main passe a la 1re personne");

    // (d) Échap depuis le système : retour IMMÉDIAT (coupe un vol en cours).
    s.demarrer_vol_cadrage();  s.tick(1.0);    // Bord -> Systeme
    s.demarrer_vol_cadrage();                  // vol de retour en cours
    CHECK(s.vol_cam.actif, "vol : retour en cours avant Echap");
    s.retour_bord_immediat();
    CHECK(!s.vol_cam.actif && s.cadrage == Cadrage::Bord,
          "vol : Echap coupe le vol et rentre a bord immediatement");
  }

  // ---- 11. NOVELLUS dans le monde : orbite LEO publiee [GDD v1.2 11.1, 17.3] -
  {
    Session s;
    s.nouvelle_partie("Oracle", ModeAide::Normal);
    s.tick(0.016);
    const auto& st = g_render_bridge.station;
    CHECK(st.valid.load(), "novellus : publie dans le monde");
    CHECK(st.altitude_km == 418.0, "novellus : altitude LEO 418 km");
    CHECK(st.envergure_m == 109.0, "novellus : envergure reelle de l ISS");
    const double r = std::sqrt(st.rel_m[0] * st.rel_m[0] + st.rel_m[1] * st.rel_m[1] +
                               st.rel_m[2] * st.rel_m[2]);
    const double rterre = ephem::body_radius(ephem::Body::EarthBary);
    CHECK(std::fabs(r - (rterre + 418000.0)) < 1.0, "novellus : rayon = R_Terre + 418 km");
    CHECK(std::fabs((r - rterre) / 1000.0 - 418.0) < 1e-3, "novellus : altitude coherente");
    CHECK(std::fabs(st.rel_m[2]) < 1.0, "novellus : cercle dans le plan ecliptique (declare)");
    // la position AVANCE avec le temps de jeu (etat au temps courant [GDD 14]).
    const double x0 = st.rel_m[0], y0 = st.rel_m[1];
    s.jeu.passer_mois();
    s.tick(0.016);
    const double dx = st.rel_m[0] - x0, dy = st.rel_m[1] - y0;
    CHECK(std::sqrt(dx * dx + dy * dy) > 1.0, "novellus : sa position suit le temps de jeu");
    // focus spécial Novellus : cadré de TRES pres (55 m), pas comme un corps.
    CHECK(distance_cadrage(FOCUS_STATION) > 0.0 && distance_cadrage(FOCUS_STATION) < 100.0,
          "novellus : le focus le cadre de pres (km)");
    CHECK(distance_cadrage(FOCUS_STATION) < distance_cadrage((int)ephem::Body::Moon),
          "novellus : cadre plus pres que le plus petit corps");
  }

  std::printf("\nSESSION : %d oracles OK, %d en echec.\n", g_ok, g_ko);
  return g_ko == 0 ? 0 : 1;
}

#endif // SP_STANDALONE_TESTS
