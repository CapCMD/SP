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

  // ---- 2. aucune scène de jeu sans agence ---------------------------------
  {
    Session s;
    s.scene = SceneJeu::Carte;
    s.tick(0.016);
    CHECK(s.scene == SceneJeu::Titre, "routage : carte sans agence -> titre");
    s.scene = SceneJeu::Station;
    s.tick(0.016);
    CHECK(s.scene == SceneJeu::Titre, "routage : station sans agence -> titre");
  }

  // ---- 3. le pont reflète EXACTEMENT la scène ------------------------------
  // Le menu utilise la carte comme décor : les deux drapeaux ne disent pas la
  // même chose et ne doivent jamais être confondus.
  {
    Session s;
    s.nouvelle_partie("Oracle", ModeAide::Normal);
    CHECK(s.scene == SceneJeu::Station, "nouvelle partie : on arrive A BORD");

    s.tick(0.016);
    CHECK(g_render_bridge.scene.load() == (int)SceneJeu::Station, "pont : scene station");
    CHECK(!g_render_bridge.carte3d_active.load(), "pont : carte inactive en station");
    CHECK(!g_render_bridge.menu_backdrop.load(), "pont : pas de decor de menu en station");
    CHECK(g_render_bridge.posts.n.load() == 8, "pont : les 8 postes publies en station");

    s.scene = SceneJeu::Carte;
    s.tick(0.016);
    CHECK(g_render_bridge.carte3d_active.load(), "pont : carte active");
    CHECK(!g_render_bridge.menu_backdrop.load(), "pont : carte != decor de menu");

    s.scene = SceneJeu::Titre;
    s.tick(0.016);
    CHECK(!g_render_bridge.carte3d_active.load(), "pont : carte inactive au titre");
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

  std::printf("\nSESSION : %d oracles OK, %d en echec.\n", g_ok, g_ko);
  return g_ko == 0 ? 0 : 1;
}

#endif // SP_STANDALONE_TESTS
