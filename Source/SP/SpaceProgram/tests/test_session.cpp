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

#include "app/impesanteur.hpp"
#include "app/session.hpp"
#include "fen/mission/Graphe.hpp"
#include "fen/mission/Manoeuvre.hpp"
#include "fen/mission/NavSolution.hpp"

using namespace fen;
using namespace fen::app;

static int g_ok = 0, g_ko = 0;
#define CHECK(cond, nom)                                                     \
  do {                                                                       \
    if (cond) { ++g_ok; }                                                    \
    else { ++g_ko; std::printf("ECHEC : %s (ligne %d)\n", nom, __LINE__); }  \
  } while (0)

int main(int argc, char** argv) {
  // SORTIE NON TAMPONNÉE : un oracle qui plante emporte sinon tout ce qu'il
  // venait d'imprimer, et on cherche le défaut à l'aveugle. Deux minutes payées.
  std::setvbuf(stdout, nullptr, _IONBF, 0);
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
    // ---- LES POSTES SONT DANS LEUR MODULE, PAS ALIGNES DANS UN COULOIR -------
    // Ils etaient poses a 1,7 m d'intervalle a partir du point d'apparition : la
    // station etait un decor. Le sous-titre de chaque poste NOMME son module
    // [GDD 11], et les positions le suivent maintenant (mesurees sur le modele par
    // Tools/diag_iss_modules.py). Ce qui est verifie ici est le CONTRAT, pas les
    // chiffres : la table est la seule source, et l'oracle mordrait si on
    // revenait a un alignement.
    {
      int np = 0;
      const PosteDef* P = postes_def(np);
      CHECK(np == 8, "postes : la table en porte 8");
      // 1. LA PUBLICATION EST FIDELE A LA TABLE (aucun calcul entre les deux).
      bool fidele = true;
      for (int i = 0; i < np; ++i) {
        const auto& it = g_render_bridge.posts.items[i];
        if (std::fabs(it.x - P[i].x) > 1e-4 || std::fabs(it.y - P[i].y) > 1e-4 ||
            std::fabs(it.z - P[i].z) > 1e-4) fidele = false;
        if (std::fabs(it.radius_m - POSTE_PORTEE_M) > 1e-6) fidele = false;
      }
      CHECK(fidele, "postes : publies EXACTEMENT a la position de la table");
      // 2. ILS SONT DISPERSES DANS LA STATION. Un couloir alignerait tout sur un
      // seul axe ; ici les trois axes doivent porter de l'ecart, et l'ecart total
      // doit etre de l'ordre de la station (55 m), pas de 12 m de couloir.
      double lo[3] = {1e30, 1e30, 1e30}, hi[3] = {-1e30, -1e30, -1e30};
      for (int i = 0; i < np; ++i) {
        const double p[3] = {P[i].x, P[i].y, P[i].z};
        for (int k = 0; k < 3; ++k) {
          lo[k] = std::min(lo[k], p[k]); hi[k] = std::max(hi[k], p[k]);
        }
      }
      CHECK(hi[0] - lo[0] > 30.0, "postes : etales sur l axe des modules (> 30 m)");
      CHECK(hi[1] - lo[1] > 15.0, "postes : ... et de babord a tribord (> 15 m)");
      CHECK(hi[2] - lo[2] > 3.0,  "postes : ... et du zenith au nadir (la cupola)");
      // 3. AUCUN RECOUVREMENT. `near_post` prend le PLUS PROCHE, donc rien n'est
      // ambigu au sens strict — mais deux postes qui se disputent le meme metre
      // cube sont un defaut de PLACEMENT, pas d'affichage : on ne peut plus se
      // tenir « au poste » sans etre aussi au voisin. C'est cet oracle qui a
      // attrape les trois postes entasses dans NOVELLUS.
      bool disjoints = true;
      for (int i = 0; i < np; ++i)
        for (int j = i + 1; j < np; ++j) {
          const double dx = P[i].x - P[j].x, dy = P[i].y - P[j].y, dz = P[i].z - P[j].z;
          if (std::sqrt(dx * dx + dy * dy + dz * dz) <= 2.0 * POSTE_PORTEE_M)
            disjoints = false;
        }
      CHECK(disjoints, "postes : leurs portees ne se recouvrent pas");
      // 4. LA COUPOLE EST SOUS LE NADIR DE NODE 3 — c'est ce qui en fait le seul
      // poste d'ou l'on voit la Terre (la cupola regarde le nadir en permanence).
      // -Z = nadir dans ce modele (mesure : Tools/diag_iss_modules.py).
      const PosteDef& coupole = P[6];
      CHECK(std::string(coupole.id) == "observation", "postes : le 7e est la COUPOLE");
      CHECK(coupole.z < -4.0, "postes : la COUPOLE est sous le hub, du cote NADIR");
      bool plus_bas = true;
      for (int i = 0; i < np; ++i)
        if (i != 6 && P[i].z <= coupole.z) plus_bas = false;
      CHECK(plus_bas, "postes : ... et c est le poste le plus bas de la station");
      // 5. VIGIE est AU point d'apparition : une partie qui commence doit avoir un
      // poste a portee, sinon le joueur arrive devant rien.
      const PosteDef& vigie = P[7];
      const double dvx = vigie.x - NOVELLUS_OEIL_M[0], dvy = vigie.y - NOVELLUS_OEIL_M[1],
                   dvz = vigie.z - NOVELLUS_OEIL_M[2];
      CHECK(std::sqrt(dvx * dvx + dvy * dvy + dvz * dvz) < POSTE_PORTEE_M,
            "postes : VIGIE est a portee du point d apparition");
    }

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

    // (a2) l'ORBITE aussi est interpolée, et le yaw par le PLUS COURT chemin :
    // sans repli dans ±pi, entrer a bord pouvait faire presque un tour complet.
    v.yaw_depart = 3.0;  v.yaw_arrivee = -3.0;      // ecart court = +0.283 (par pi)
    v.pitch_depart = 1.0; v.pitch_arrivee = 0.0;
    v.progres = 0.5;
    CHECK(v.yaw_courant() > 3.0 && v.yaw_courant() < 3.3,
          "vol : yaw par le plus court chemin (passe par pi, pas par 0)");
    CHECK(std::fabs(v.pitch_courant() - 0.5) < 1e-9, "vol : pitch interpole");
    v.progres = 1.0;
    CHECK(std::fabs(v.pitch_courant() - 0.0) < 1e-9, "vol : pitch exact a l arrivee");

    // (b) intégration Session : [M] à bord lance un vol VERS le système.
    Session s;
    s.nouvelle_partie("Oracle", ModeAide::Normal);
    s.demarrer_vol_cadrage();
    CHECK(s.vol_cam.actif && s.vol_cam.sens == SensVol::VersSysteme,
          "vol : [M] a bord -> vol vers le systeme");
    CHECK(s.cadrage == Cadrage::Systeme, "vol : le plan systeme rend des le depart du vol");
    CHECK(g_render_bridge.focus_body.load() == FOCUS_STATION,
          "vol : la camera est ancree sur NOVELLUS, pas sur la Terre");
    CHECK(std::fabs(g_render_bridge.cam.dist_km.load() - s.dist_bord_km()) < 1e-9,
          "vol : depart DEPUIS l oeil du joueur (pas au ras de la Terre)");
    const double arrivee_avant = s.vol_cam.dist_arrivee_km;
    s.demarrer_vol_cadrage();          // un vol à la fois : sans effet
    CHECK(s.vol_cam.dist_arrivee_km == arrivee_avant, "vol : un seul vol a la fois");
    s.tick(1.0);                       // le vol se termine
    CHECK(!s.vol_cam.actif, "vol : le vol se termine");
    CHECK(s.cadrage == Cadrage::Systeme, "vol vers systeme : on reste au plan systeme");
    CHECK(std::fabs(g_render_bridge.cam.dist_km.load() - Session::DIST_SYSTEME_KM) < 1.0,
          "vol : arrivee a la vue systeme");
    CHECK(g_render_bridge.focus_body.load() == (int)ephem::Body::EarthBary,
          "vol : au plan systeme l ancre redevient la Terre (Novellus sous-pixel)");
    CHECK(std::fabs(g_render_bridge.cam.yaw.load() - Session::YAW_SYSTEME) < 1e-9 &&
          std::fabs(g_render_bridge.cam.pitch.load() - Session::PITCH_SYSTEME) < 1e-9,
          "vol : arrivee sur la pose de repos de la vue systeme");
    CHECK(!g_render_bridge.interieur_coexiste.load() &&
          g_render_bridge.cam.look_to_bord.load() == 0.0,
          "vol : au plan systeme, ni coexistence ni melange de regard");

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
    CHECK(!g_render_bridge.interieur_coexiste.load() &&
          g_render_bridge.cam.look_to_bord.load() == 0.0,
          "vol : Echap remet le rendu dans son etat canonique");
  }

  // ---- 10b. LE HANDOFF VERS L'AMBULATION (incr. 3c-3) [GDD v1.2 17.4, ch.18] -
  // Ce qui est vérifié ici est la GÉOMÉTRIE de la reprise : la caméra doit finir
  // pile sur l'œil du pawn (sinon la coupure reste, simplement déplacée).
  {
    Session s;
    s.nouvelle_partie("Oracle", ModeAide::Normal);

    // (a) la pose d'amarrage RECONSTRUIT l'offset de l'œil. C'est l'invariant
    // central : orbite (dist, yaw, pitch) -> offset cartesien == offset de l'oeil
    // en axes de rendu (miroir en y sur le repere station), TOURNE PAR L'ATTITUDE
    // de la station (elle vole cupola au nadir : son flanc change au fil de
    // l'orbite, et la camera doit en sortir du bon cote). La rotation est
    // verifiee a part, en 11b (f) — ici on la compose telle qu'elle est publiee.
    const Session::PoseBord pb = s.pose_bord();
    const double ox = pb.dist_km * std::cos(pb.pitch) * std::cos(pb.yaw);
    const double oy = pb.dist_km * std::cos(pb.pitch) * std::sin(pb.yaw);
    const double oz = pb.dist_km * std::sin(pb.pitch);
    const Vec3 oeil_attendu = appliquer_attitude(
        Session::attitude_publiee(), Vec3{ NOVELLUS_OEIL_M[0] / 1000.0,
                                          -NOVELLUS_OEIL_M[1] / 1000.0,
                                           NOVELLUS_OEIL_M[2] / 1000.0});
    CHECK(std::fabs(ox - oeil_attendu.x) < 1e-9 &&
          std::fabs(oy - oeil_attendu.y) < 1e-9 &&
          std::fabs(oz - oeil_attendu.z) < 1e-9,
          "handoff : (dist,yaw,pitch) reconstruit exactement l offset de l oeil");
    const double r_attendu = std::sqrt(NOVELLUS_OEIL_M[0] * NOVELLUS_OEIL_M[0] +
                                       NOVELLUS_OEIL_M[1] * NOVELLUS_OEIL_M[1] +
                                       NOVELLUS_OEIL_M[2] * NOVELLUS_OEIL_M[2]) / 1000.0;
    CHECK(std::fabs(pb.dist_km - r_attendu) < 1e-12, "handoff : distance = norme de l oeil");
    CHECK(pb.dist_km > 0.0 && pb.dist_km < 0.03, "handoff : l amarrage est a ~20 m, pas a 7000 km");

    // (b) DEUX SEUILS distincts : l'enveloppe (la geometrie interieure prend le
    // relais) est STRICTEMENT plus large que l'amarrage (la main passe). Sans
    // cela le franchissement de la coque et la reprise tomberaient au meme
    // instant, et la bascule de LOD redeviendrait une coupure.
    CHECK(s.rayon_enveloppe_km() > pb.dist_km,
          "handoff : l enveloppe englobe le point d amarrage");
    CHECK(std::fabs(s.rayon_enveloppe_km() - STATION_ENVERGURE_M * 0.5 / 1000.0) < 1e-12,
          "handoff : enveloppe = demi-envergure du modele (55 m -> 27,5 m)");

    // (c) mélange du regard : 0 dehors, 1 a l amarrage, monotone entre les deux.
    CHECK(s.melange_regard(s.rayon_enveloppe_km() * 2.0) == 0.0,
          "handoff : hors enveloppe, la camera regarde la station");
    CHECK(s.melange_regard(pb.dist_km) == 1.0,
          "handoff : a l amarrage, la camera regarde CE QUE regarde le pawn");
    CHECK(s.melange_regard(pb.dist_km * 0.5) == 1.0,
          "handoff : en deca de l amarrage, le regard reste celui du pawn");
    {
      const double e = s.rayon_enveloppe_km();
      double prec = -1.0; bool mono = true;
      for (int i = 0; i <= 20; ++i) {
        const double d = e - (e - pb.dist_km) * (i / 20.0);
        const double m = s.melange_regard(d);
        if (m < prec) mono = false;
        prec = m;
      }
      CHECK(mono, "handoff : le melange du regard croit en approchant");
    }

    // (d) le vol de RETOUR franchit l'enveloppe AVANT de rendre la main : la
    // coexistence s'allume pendant le vol, la 1re personne ne reprend qu'a la fin.
    s.demarrer_vol_cadrage(); s.tick(1.0);          // Bord -> Systeme
    s.demarrer_vol_cadrage();                       // vol de retour
    bool coexiste_vue = false, main_avant_coque = false;
    for (int i = 0; i < 200 && s.vol_cam.actif; ++i) {
      s.tick(0.01);
      if (g_render_bridge.interieur_coexiste.load()) {
        coexiste_vue = true;
        if (s.vol_cam.actif && s.cadrage == Cadrage::Bord) main_avant_coque = true;
      }
    }
    CHECK(coexiste_vue, "handoff : l interieur coexiste pendant la fin du vol de retour");
    CHECK(!main_avant_coque, "handoff : la main ne passe pas avant la fin du vol");
    CHECK(s.cadrage == Cadrage::Bord, "handoff : a la fin du vol, on est a bord");
    // ... et la camera est EXACTEMENT sur la pose d'amarrage : c'est ce qui rend
    // la reprise invisible. La pose est relue MAINTENANT, pas celle d'avant le
    // vol : la station tourne (cupola au nadir), donc l'amarrage suit son flanc.
    // C'est precisement ce que `publier_camera_vol` resynchronise a chaque frame.
    const Session::PoseBord pb_arrivee = s.pose_bord();
    CHECK(std::fabs(g_render_bridge.cam.dist_km.load() - pb_arrivee.dist_km) < 1e-9 &&
          std::fabs(g_render_bridge.cam.yaw.load() - pb_arrivee.yaw) < 1e-9 &&
          std::fabs(g_render_bridge.cam.pitch.load() - pb_arrivee.pitch) < 1e-9,
          "handoff : le vol finit pile sur la pose de l oeil du pawn");

    // (e) SYMÉTRIE : le vol de sortie part de la coexistence (on est encore dans
    // la coque a l instant du depart), sinon le depart claquerait aussi.
    s.demarrer_vol_cadrage();
    CHECK(g_render_bridge.interieur_coexiste.load(),
          "handoff : au depart du vol de sortie, l interieur coexiste encore");
    CHECK(g_render_bridge.cam.look_to_bord.load() == 1.0,
          "handoff : au depart du vol de sortie, le regard est encore celui du pawn");
    s.tick(1.0);
    CHECK(!g_render_bridge.interieur_coexiste.load(),
          "handoff : arrive au systeme, l interieur ne rend plus");

    // (f) l'ŒIL VIVANT du pawn fait foi quand UE l'a publie : on ressort la ou
    // l'on est entre, pas au point d'apparition.
    g_render_bridge.station_out.eye_m[0] = -12.0f;
    g_render_bridge.station_out.eye_m[1] = 2.0f;
    g_render_bridge.station_out.eye_m[2] = 0.5f;
    g_render_bridge.station_out.ready = true;
    const Session::PoseBord pv = s.pose_bord();
    // La DISTANCE est invariante par rotation : elle contraint l'oeil vivant seul.
    CHECK(std::fabs(pv.dist_km - std::sqrt(12.0 * 12.0 + 4.0 + 0.25) / 1000.0) < 1e-12,
          "handoff : la pose suit l oeil VIVANT du pawn");
    // La DIRECTION, elle, est celle de l'oeil vivant MIROITE EN Y puis tourne par
    // l'attitude — les deux conventions composees, sur la valeur vivante.
    {
      const Vec3 attendu = appliquer_attitude(Session::attitude_publiee(),
                                              Vec3{-0.012, -0.002, 0.0005});
      const Vec3 obtenu{pv.dist_km * std::cos(pv.pitch) * std::cos(pv.yaw),
                        pv.dist_km * std::cos(pv.pitch) * std::sin(pv.yaw),
                        pv.dist_km * std::sin(pv.pitch)};
      CHECK(norm(obtenu - attendu) < 1e-12,
            "handoff : miroir en y ET attitude appliques a l oeil vivant");
    }
    g_render_bridge.station_out.ready = false;
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
    // ---- LA VITESSE PUBLIEE : ce qui donne son ATTITUDE a la station ----------
    // Le rendu s'en sert pour poser l'axe de vol (cupola au nadir, vol « XVV »
    // reel). Elle est publiee ICI et pas derivee la-bas, parce qu'a mois/s une
    // frame avance de ~8 orbites LEO : une difference de positions cote rendu ne
    // dirait plus rien de la tangente. Deux oracles, qui la contraignent
    // completement sans jamais recopier le calcul :
    {
      const double v = std::sqrt(st.vel_ms[0] * st.vel_ms[0] + st.vel_ms[1] * st.vel_ms[1] +
                                 st.vel_ms[2] * st.vel_ms[2]);
      // 1. MODULE : la vitesse circulaire sqrt(mu/r) — ~7,66 km/s a 418 km. C'est
      // la 3e loi de Kepler qui relit le modele, pas une valeur recopiee.
      const double v_circ = std::sqrt(ephem::body_mu(ephem::Body::EarthBary) / r);
      CHECK(std::fabs(v / v_circ - 1.0) < 1e-9, "novellus : |v| = vitesse circulaire a 418 km");
      CHECK(v > 7500.0 && v < 7800.0, "novellus : ...soit ~7,66 km/s (ancre chiffree LEO)");
      // 2. DIRECTION : perpendiculaire au rayon (orbite circulaire). C'est ce qui
      // rend l'attitude bien definie — une composante radiale ferait tanguer la
      // station.
      const double rdotv = (st.rel_m[0] * st.vel_ms[0] + st.rel_m[1] * st.vel_ms[1] +
                            st.rel_m[2] * st.vel_ms[2]) / (r * v);
      CHECK(std::fabs(rdotv) < 1e-12, "novellus : v perpendiculaire a r (cercle)");
    }
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

  // ---- 11b. L'ORBITE REELLE DE NOVELLUS : LE PLAN ET LA PERIODE -------------
  // La station passait par le helper de la FLOTTE : un cercle dans le plan
  // ECLIPTIQUE. Le rayon etait bon (donc la periode aussi), mais le PLAN etait
  // faux de 51,6° — et un plan faux, c'est une Terre qui defile n'importe ou sous
  // la cupola. Ce qui est verifie ici : l'inclinaison se mesure bien sur
  // l'EQUATEUR TERRESTRE (pas sur l'ecliptique), la periode sort de Kepler, et le
  // noeud regresse comme J2 l'impose.
  {
    using namespace fen::app;
    const double a = novellus_sma_m();

    // (a) LA PERIODE — 92,9 min. Elle n'est pas saisie : elle SORT de la 3e loi.
    const double T = novellus_periode_s();
    CHECK(std::fabs(T - 5576.0) < 5.0, "orbite : periode = 92,9 min (5576 s)");
    CHECK(std::fabs(T / 60.0 - 92.9) < 0.1, "orbite : ...soit 92,9 min, la valeur reelle de l ISS");
    // relecture independante : T^2 = 4 pi^2 a^3 / mu.
    const double T_kepler = cst::TWO_PI * std::sqrt(a * a * a / ephem::body_mu(ephem::Body::EarthBary));
    CHECK(std::fabs(T - T_kepler) < 1e-6, "orbite : la periode EST celle de Kepler");

    // (b) L'INCLINAISON, mesuree sur l'EQUATEUR TERRESTRE. Le moment cinetique
    // r x v est normal au plan orbital ; l'angle qu'il fait avec le POLE de la
    // Terre EST l'inclinaison. On la mesure sur l'etat publie, sans jamais relire
    // la constante saisie autrement que pour la comparaison finale.
    const NovellusEtat s0 = novellus_etat(0.0);
    const Vec3 pole = ephem::spin_axis_ecliptic(ephem::Body::EarthBary);
    const Vec3 h = unit(cross(s0.r, s0.v));
    const double inc_deg = std::acos(std::clamp(dot(h, pole), -1.0, 1.0)) / cst::DEG;
    CHECK(std::fabs(inc_deg - 51.64) < 1e-9, "orbite : inclinaison 51,64° sur l EQUATEUR terrestre");
    // ... et ce n'est PAS l'inclinaison sur l'ecliptique : le plan de l'equateur
    // est lui-meme a 23,44°, donc les deux ne peuvent pas coincider. C'est
    // exactement le defaut que ce modele corrige.
    const double inc_ecl_deg = std::acos(std::clamp(h.z, -1.0, 1.0)) / cst::DEG;
    CHECK(std::fabs(inc_ecl_deg - 51.64) > 1.0,
          "orbite : l inclinaison sur l ECLIPTIQUE en differe (l equateur est a 23,44°)");
    // ... et la station QUITTE le plan de l'ecliptique, ou l'ancien modele la
    // clouait. Mesure au quart d'orbite : c'est la que l'ecart au plan est maximal
    // (a l'epoque de reference elle est AU NŒUD, donc dans les deux plans a la fois
    // — voir la convention de phase declaree dans novellus_orbite.hpp).
    const NovellusEtat sq = novellus_etat(T * 0.25);
    CHECK(std::fabs(sq.r.z) > 1.0e6,
          "orbite : la station QUITTE le plan de l ecliptique (l ancien modele l y clouait)");

    // (c) UNE PERIODE PLUS TARD, ON EST REVENU AU MEME POINT — a la regression du
    // noeud pres. Et cet ecart residuel n'est pas « du bruit tolere » : il VAUT le
    // deplacement que J2 impose sur une orbite, a·|dOmega/dt|·T = 38 km. L'oracle
    // lie donc la periode, le demi-grand axe et J2 d'un seul trait.
    const double derive_j2 = a * std::fabs(novellus_raan_rate_rad_s()) * T;
    const NovellusEtat s1 = novellus_etat(T);
    CHECK(std::fabs(norm(s1.r - s0.r) / derive_j2 - 1.0) < 0.02,
          "orbite : apres une periode, la station est revenue (a la regression du noeud pres)");
    CHECK(norm(s1.r - s0.r) < 0.01 * a,
          "orbite : ... soit 38 km sur 6 796, un demi-millieme de tour");
    const NovellusEtat sd = novellus_etat(T * 0.5);
    CHECK(norm(sd.r + s0.r) < 0.01 * a, "orbite : a la demi-periode, elle est a l oppose");

    // (d) LA REGRESSION DU NŒUD (J2) : ~ -4,95°/jour, un tour en 73 jours. C'est
    // ce qui commande le cycle beta et l'heure locale des survols ; sans elle le
    // plan serait fige dans l'inertiel, faux des la premiere journee de jeu.
    const double raan_deg_jour = novellus_raan_rate_rad_s() * cst::DAY / cst::DEG;
    CHECK(raan_deg_jour < 0.0, "J2 : le noeud RECULE (orbite prograde)");
    CHECK(std::fabs(raan_deg_jour + 4.95) < 0.05, "J2 : -4,95°/jour, la valeur reelle de l ISS");
    // mesuree sur l'etat publie : l'angle entre les deux normales au plan, a un
    // jour d'intervalle, vaut la rotation du noeud (le plan pivote autour du pole).
    const NovellusEtat sj = novellus_etat(cst::DAY);
    const Vec3 hj = unit(cross(sj.r, sj.v));
    // la composante du basculement le long du pole est nulle : c'est un pivotement
    // AUTOUR du pole, donc l'inclinaison est CONSERVEE.
    const double inc_j = std::acos(std::clamp(dot(hj, pole), -1.0, 1.0)) / cst::DEG;
    CHECK(std::fabs(inc_j - inc_deg) < 1e-9, "J2 : le noeud regresse SANS changer l inclinaison");
    CHECK(norm(hj - h) > 1.0e-3, "J2 : ... mais le plan a bel et bien tourne en un jour");

    // (e) L'ATTITUDE : cupola au nadir, axe de vol dans la vitesse.
    const AttitudeRendu att = novellus_attitude_rendu(s0.r, s0.v);
    // repere ORTHONORME et DIRECT (det = +1) : sans quoi le modele serait rendu
    // en miroir — une station dont la poutre part du mauvais bord.
    CHECK(std::fabs(norm(att.avant) - 1.0) < 1e-12 &&
          std::fabs(norm(att.tribord) - 1.0) < 1e-12 &&
          std::fabs(norm(att.zenith) - 1.0) < 1e-12, "attitude : axes unitaires");
    CHECK(std::fabs(dot(att.avant, att.tribord)) < 1e-12 &&
          std::fabs(dot(att.avant, att.zenith)) < 1e-12 &&
          std::fabs(dot(att.tribord, att.zenith)) < 1e-12, "attitude : axes orthogonaux");
    CHECK(std::fabs(dot(att.avant, cross(att.tribord, att.zenith)) - 1.0) < 1e-12,
          "attitude : repere DIRECT (det = +1), pas un miroir");
    // LE NADIR REGARDE LA TERRE : -Z du modele (ou est la cupola) doit pointer
    // vers le centre de la Terre, c'est-a-dire a l'oppose du radial.
    const Vec3 vers_terre = unit(ecl_vers_rendu(-s0.r));
    CHECK(dot(-att.zenith, vers_terre) > 1.0 - 1e-12,
          "attitude : la CUPOLA (-Z) regarde le centre de la Terre");
    CHECK(dot(att.avant, unit(ecl_vers_rendu(s0.v))) > 1.0 - 1e-12,
          "attitude : l axe de vol (+X) est dans la vitesse");

    // ... ET ELLE Y RESTE TOUT LE TOUR. C'est la propriete demandee (« cupola face
    // Terre tout le temps ») : on echantillonne une orbite entiere, le nadir ne
    // doit jamais s'ecarter d'un iota. Au passage, la station fait bien UN TOUR sur
    // elle-meme par orbite dans l'inertiel — l'avant a change de sens a mi-course.
    {
      bool nadir_tenu = true;
      for (int i = 0; i <= 64; ++i) {
        const NovellusEtat sk = novellus_etat(T * (i / 64.0));
        const AttitudeRendu ak = novellus_attitude_rendu(sk.r, sk.v);
        if (dot(-ak.zenith, unit(ecl_vers_rendu(-sk.r))) < 1.0 - 1e-12) nadir_tenu = false;
      }
      CHECK(nadir_tenu, "attitude : la cupola regarde la Terre a TOUT instant de l orbite");
      const AttitudeRendu a_demi = novellus_attitude_rendu(sd.r, sd.v);
      CHECK(dot(att.avant, a_demi.avant) < -0.99,
            "attitude : un tour complet par orbite (l avant s est inverse a mi-course)");
    }

    // (f) LA POSE DE BORD PORTE L'ATTITUDE. C'est le troisieme consommateur, et
    // celui qui rendrait le handoff visible s'il divergeait : la camera doit
    // sortir de la station DU BON COTE, donc suivre sa rotation.
    {
      Session s2;
      s2.nouvelle_partie("Oracle", ModeAide::Normal);
      s2.tick(0.016);                      // publie Novellus, donc l attitude
      const Session::PoseBord pb2 = s2.pose_bord();
      const Vec3 local{ NOVELLUS_OEIL_M[0] / 1000.0,
                       -NOVELLUS_OEIL_M[1] / 1000.0,
                        NOVELLUS_OEIL_M[2] / 1000.0};
      const Vec3 attendu = appliquer_attitude(Session::attitude_publiee(), local);
      const Vec3 obtenu{pb2.dist_km * std::cos(pb2.pitch) * std::cos(pb2.yaw),
                        pb2.dist_km * std::cos(pb2.pitch) * std::sin(pb2.yaw),
                        pb2.dist_km * std::sin(pb2.pitch)};
      CHECK(norm(obtenu - attendu) < 1e-12,
            "handoff : la pose de bord est l oeil TOURNE PAR L ATTITUDE");
      // une rotation conserve la norme : la DISTANCE d amarrage, elle, ne bouge pas
      // — donc les deux seuils du handoff (enveloppe, amarrage) sont intacts.
      CHECK(std::fabs(pb2.dist_km - norm(local)) < 1e-12,
            "handoff : ... et la distance d amarrage est inchangee (rotation)");
      // et l attitude publiee n est PAS l identite : le test ci-dessus mordrait.
      CHECK(norm(Session::attitude_publiee().zenith - Vec3{0, 0, 1}) > 1e-3,
            "handoff : l attitude publiee est bien vivante (pas l identite)");
    }
  }

  // ---- 11c. SE DEPLACER EN IMPESANTEUR (app/impesanteur.hpp) ---------------
  // Le pawn utilisait `UFloatingPawnMovement`, qui (1) ne bouge pas sans
  // possession — et le pawn n'est jamais possede, l'entree vient du HUD par le
  // pont — et (2) ANNULE la vitesse des qu'on lache la touche. On ne pouvait donc
  // ni se deplacer, ni deriver. La loi vit maintenant ici, et ce qu'elle promet
  // est verifie : on derive, on ne pousse qu'en appui, on ne s'arrete qu'en
  // s'agrippant.
  {
    using namespace fen::app;
    const Vec3 nulle{0, 0, 0};
    const Vec3 av{1, 0, 0};                       // une direction quelconque
    const Vec3 v0{0.5, -0.2, 0.1};                // ~0,55 m/s, en pleine derive
    const double dt = 1.0 / 60.0;

    // (a) L'INVARIANT CENTRAL : sans entree, la vitesse est RENDUE TELLE QUELLE.
    // Aucun amortissement, dans AUCUNE des quatre combinaisons appui/agrippe —
    // sauf celle qui veut dire « je tiens la main courante ».
    CHECK(norm(avancer_vitesse(v0, nulle, false, false, dt) - v0) == 0.0,
          "impesanteur : sans entree, on DERIVE (volume libre)");
    CHECK(norm(avancer_vitesse(v0, nulle, true, false, dt) - v0) == 0.0,
          "impesanteur : ... et on derive AUSSI le long d une paroi qu on ne tient pas");
    CHECK(norm(avancer_vitesse(v0, nulle, false, true, dt) - v0) == 0.0,
          "impesanteur : s agripper au VIDE ne freine pas");
    CHECK(norm(avancer_vitesse(v0, nulle, true, true, dt)) < norm(v0),
          "impesanteur : s agripper EN APPUI, si");
    // ... et sur une seconde entiere de derive, pas un iota perdu (un
    // amortissement, meme faible, se verrait ici).
    {
      Vec3 v = v0;
      for (int i = 0; i < 60; ++i) v = avancer_vitesse(v, nulle, true, false, dt);
      CHECK(norm(v - v0) == 0.0, "impesanteur : une seconde de derive, vitesse INTACTE");
    }

    // (b) ON N'ACCELERE QU'EN POUSSANT SUR QUELQUE CHOSE. La brasse dans l'air
    // existe (jamais bloque au milieu d'un module) mais elle est derisoire — c'est
    // le RAPPORT qui porte la sensation, et il est de vingt.
    const double d_appui = norm(avancer_vitesse(nulle, av, true, false, dt));
    const double d_libre = norm(avancer_vitesse(nulle, av, false, false, dt));
    CHECK(std::fabs(d_appui - IMPESANTEUR.poussee_ms2 * dt) < 1e-12,
          "impesanteur : en appui, dv = poussee x dt");
    CHECK(d_libre > 0.0, "impesanteur : en volume libre on avance encore (pas de blocage)");
    CHECK(d_appui / d_libre > 15.0, "impesanteur : ... mais quinze fois moins vite au moins");

    // (c) LA DIRECTION EST NORMALISEE : une diagonale ne doit pas pousser plus
    // fort qu'un axe (le defaut classique des deplacements en croix).
    const Vec3 diag{1, 1, 1};
    CHECK(std::fabs(norm(avancer_vitesse(nulle, diag, true, false, dt)) - d_appui) < 1e-12,
          "impesanteur : la diagonale ne pousse pas plus fort qu un axe");

    // (d) LE FREINAGE S ARRETE A ZERO — jamais d inversion, meme sur un grand pas.
    const Vec3 lent{0.01, 0, 0};
    CHECK(norm(avancer_vitesse(lent, nulle, true, true, 1.0)) == 0.0,
          "impesanteur : la prise amene a l arret, sans repartir en arriere");

    // (e) LE PLAFOND. Il est atteint, et il n est jamais depasse.
    {
      Vec3 v = nulle;
      for (int i = 0; i < 600; ++i) v = avancer_vitesse(v, av, true, false, dt);
      CHECK(std::fabs(norm(v) - IMPESANTEUR.v_max_ms) < 1e-12,
            "impesanteur : le plafond de vitesse est atteint");
      CHECK(IMPESANTEUR.v_max_ms < 1.5,
            "impesanteur : ... et il reste celui d un equipage, pas d un vol (< 1,5 m/s)");
    }

    // (f) LE CHOC n'est PAS teste ici, et ce n'est pas un oubli : il n'est pas
    // dans la loi. L'absorption se lit sur le DEPLACEMENT OBTENU du balayage
    // moteur, pas sur une normale rapportee (voir impesanteur.hpp et
    // `AvancerEnImpesanteur`) — c'est une mesure, pas une equation, et elle se
    // verifie en jeu, pas ici.
  }

  // ---- 12. LE TEMPS QUI COULE [GDD 14.2] ----------------------------------
  // Ce qui est vérifié ici : la cadence, la QUANTIFICATION en sous-pas fixes
  // (donc l'indépendance à la cadence de rendu), et le fait que la comptabilité
  // mensuelle tombe une fois par frontière franchie — ni zéro (l'agence vivrait
  // gratuitement) ni deux fois.
  {
    // (a) PAUSE par défaut : rien ne bouge, sinon fonder une agence lancerait
    // aussitôt l'horloge et ses charges.
    Session s;
    s.nouvelle_partie("Oracle", ModeAide::Normal);
    CHECK(s.jeu.cadence == game::TimeRate::Paused, "temps : la cadence par defaut est la PAUSE");
    const double mois0 = s.jeu.agence.mois;
    for (int i = 0; i < 50; ++i) s.tick(0.016);
    CHECK(s.jeu.agence.mois == mois0, "temps : en pause, le calendrier ne bouge pas");

    // (b) la cadence convertit du temps REEL en jours de jeu : 1 j / s reel.
    // (dt borne a 0,25 s par appel, cf. (e) : on avance donc par quarts de seconde)
    s.jeu.cadence = game::TimeRate::Day;
    double cumul = 0.0;
    for (int i = 0; i < 4; ++i) cumul += s.jeu.faire_couler_le_temps(0.25);
    CHECK(std::fabs(cumul - 1.0) < 1e-12, "temps : cadence JOUR = 1 jour par seconde reelle");
    CHECK(std::fabs(s.jeu.agence.mois - (mois0 + 1.0 / 30.44)) < 1e-12,
          "temps : le calendrier avance de 1 jour, en mois");

    // (c) SOUS-PAS FIXES : sous le pas, rien ne sort — mais rien ne se perd.
    Session q; q.nouvelle_partie("Pas", ModeAide::Normal);
    q.jeu.cadence = game::TimeRate::Day;
    CHECK(q.jeu.faire_couler_le_temps(0.01) == 0.0,
          "temps : une avance sous le sous-pas ne sort rien");
    CHECK(q.jeu.faire_couler_le_temps(0.01) > 0.0,
          "temps : le reste s accumule et sort au sous-pas suivant");
    CHECK(std::fabs(q.jeu.faire_couler_le_temps(0.0)) == 0.0,
          "temps : dt nul n avance rien");

    // (d) INDÉPENDANCE À LA CADENCE DE RENDU : la même durée réelle donne le même
    // calendrier, qu'on la découpe en 4 frames ou en 100. C'est l'objet des
    // sous-pas fixes [GDD 14].
    Session lent, rapide;
    lent.nouvelle_partie("Lent", ModeAide::Normal);
    rapide.nouvelle_partie("Rapide", ModeAide::Normal);
    lent.jeu.cadence = game::TimeRate::Day;
    rapide.jeu.cadence = game::TimeRate::Day;
    for (int i = 0; i < 4; ++i)   lent.jeu.faire_couler_le_temps(0.25);
    for (int i = 0; i < 100; ++i) rapide.jeu.faire_couler_le_temps(0.01);
    CHECK(std::fabs(lent.jeu.agence.mois - rapide.jeu.agence.mois) < 1e-12,
          "temps : 4 frames ou 100 frames -> MEME calendrier (sous-pas fixes)");

    // (e) une frame anormalement longue ne TELEPORTE pas le calendrier : le temps
    // reel au-dela de 0,25 s est PERDU, pas differe — un gel de fenetre ou une
    // compilation de shaders ne doit pas couter des mois de charges au joueur.
    Session gel; gel.nouvelle_partie("Gel", ModeAide::Normal);
    gel.jeu.cadence = game::TimeRate::Month;
    gel.jeu.faire_couler_le_temps(30.0);          // 30 s de gel : 30 mois demandes
    CHECK(gel.jeu.agence.mois < 1.0,
          "temps : une frame de 30 s est bornee (pas de saut de plusieurs mois)");
    // 0,25 s a la cadence MOIS = 0,25 mois, au sous-pas pres (le reste attend).
    CHECK(std::fabs(gel.jeu.agence.mois - 0.25) < Jeu::PAS_JOURS / 30.44 + 1e-12,
          "temps : la borne rend 0,25 s de jeu, pas 30 (le surplus est perdu)");

    // (f) COMPTABILITE MENSUELLE : une fois par frontiere franchie. Les charges
    // fixes de l'agence valent 0,6 M$/mois, sans flotte ni contrat.
    Session c; c.nouvelle_partie("Compta", ModeAide::Normal);
    const double tres0 = c.jeu.agence.tresorerie;
    c.jeu.avancer_temps(0.9 * 30.44);             // 0,9 mois : aucune frontiere
    CHECK(c.jeu.agence.tresorerie == tres0, "temps : sous un mois, aucune charge");
    CHECK(std::fabs(c.jeu.agence.mois - 0.9) < 1e-9, "temps : le calendrier est fractionnaire");
    c.jeu.avancer_temps(0.2 * 30.44);             // franchit 1.0
    CHECK(std::fabs(c.jeu.agence.tresorerie - (tres0 - 0.6)) < 1e-9,
          "temps : la frontiere de mois solde EXACTEMENT une fois");
    CHECK(std::fabs(c.jeu.agence.mois - 1.1) < 1e-9, "temps : le calendrier reprend apres le solde");
    c.jeu.avancer_temps(2.5 * 30.44);             // franchit 2.0 et 3.0
    CHECK(std::fabs(c.jeu.agence.tresorerie - (tres0 - 1.8)) < 1e-9,
          "temps : deux frontieres franchies = deux soldes, jamais un arrondi");
    CHECK(std::fabs(c.jeu.agence.mois - 3.6) < 1e-9, "temps : calendrier exact apres soldes");

    // (g) LA PRESSION D'INACTIVITE MORD [GDD 13.2, 14.2] : accelerer sans
    // programme ni commercial erode la tresorerie puis la reserve. C'est la
    // CONTRAINTE TEMPORELLE du GDD : sans elle, accelerer serait gratuit.
    Session p; p.nouvelle_partie("Pression", ModeAide::Normal);
    p.tick(0.016);                                 // la couche ARES s initialise
    CHECK(p.jeu.ares.initialisee(), "temps : couche ARES prete");
    const double fonds0 = p.jeu.ares.etat->finance.treasury_me +
                          p.jeu.ares.etat->finance.reserve_me;
    p.jeu.avancer_temps(12.0 * 30.44);              // un an
    p.tick(0.016);                                  // ARES rattrape les 12 mois
    const double fonds1 = p.jeu.ares.etat->finance.treasury_me +
                          p.jeu.ares.etat->finance.reserve_me;
    CHECK(fonds1 < fonds0, "temps : un an d inactivite erode les fonds [GDD 13.2]");
    // Ordre de grandeur du GDD : environ -9 Md EUR par an d'inactivite.
    const double erosion_mde = (fonds0 - fonds1) / 1000.0;
    CHECK(erosion_mde > 3.0 && erosion_mde < 25.0,
          "temps : l erosion annuelle est de l ordre du GDD (quelques Md EUR)");

    // (h) L'EPOQUE SUIT LE CALENDRIER [GDD 14.1] : les corps bougent vraiment.
    const double ep_avant = p.jeu.epoch_courant();
    p.jeu.avancer_temps(10.0);
    CHECK(std::fabs((p.jeu.epoch_courant() - ep_avant) - 10.0 * cst::DAY) < 1e-6,
          "temps : l epoque avance exactement des jours ecoules");

    // (i) le temps ne coule PAS au menu ni sous une modale (une modale porte une
    // decision : le monde l attend). Il coule en revanche poste OUVERT, sans quoi
    // regler la cadence au poste AGENCE n aurait aucun effet visible.
    Session m; m.nouvelle_partie("Modale", ModeAide::Normal);
    m.jeu.cadence = game::TimeRate::Month;
    m.modal = Modal::Reglages;
    const double mm0 = m.jeu.agence.mois;
    for (int i = 0; i < 10; ++i) m.tick(0.05);
    CHECK(m.jeu.agence.mois == mm0, "temps : une modale suspend le temps");
    m.modal = Modal::Aucun;
    m.poste_ouvert = 0;                             // AGENCE
    for (int i = 0; i < 10; ++i) m.tick(0.05);
    CHECK(m.jeu.agence.mois > mm0, "temps : un poste ouvert ne suspend PAS le temps");
    m.scene = SceneJeu::Titre;
    const double mm1 = m.jeu.agence.mois;
    for (int i = 0; i < 10; ++i) m.tick(0.05);
    CHECK(m.jeu.agence.mois == mm1, "temps : au menu, le temps ne coule pas");
    // ... et la cadence est PUBLIEE pour la barre de temps (indicateur [GDD 14]).
    m.scene = SceneJeu::Monde;
    m.tick(0.016);
    CHECK(g_render_bridge.cadence.load() == (int)game::TimeRate::Month,
          "temps : la cadence est publiee sur le pont");

    // (j) TOUTE PARTIE DEMARRE EN PAUSE : le temps est une depense, il ne doit
    // jamais se mettre a couler par heritage de la partie precedente.
    Session h; h.nouvelle_partie("Herite", ModeAide::Normal);
    h.jeu.cadence = game::TimeRate::Month;
    h.nouvelle_partie("Suivante", ModeAide::Normal);
    CHECK(h.jeu.cadence == game::TimeRate::Paused,
          "temps : fonder une agence remet l horloge en pause");
    h.jeu.cadence = game::TimeRate::Month;
    h.jeu.avancer_temps(30.44);
    const std::string chemin_h = tmp + "/oracle_horloge.sav";
    h.chemin_sauvegarde = chemin_h;
    h.sauvegarder_partie();
    h.jeu.cadence = game::TimeRate::Month;
    CHECK(h.charger_partie(chemin_h), "temps : la partie se recharge");
    CHECK(h.jeu.cadence == game::TimeRate::Paused,
          "temps : on ne charge JAMAIS dans une partie qui defile");
    h.jeu.reinitialiser();
    CHECK(h.jeu.cadence == game::TimeRate::Paused, "temps : le reset remet en pause");

    // (k) la FAILLITE arrete le calendrier : plus de charges apres la dissolution.
    Session f; f.nouvelle_partie("Faillite", ModeAide::Normal);
    f.jeu.avancer_temps(400.0 * 30.44);             // bien au-dela de la caisse
    CHECK(f.jeu.game_over, "temps : l inactivite prolongee finit par ruiner l agence");
    const double mois_faillite = f.jeu.agence.mois;
    f.jeu.avancer_temps(10.0 * 30.44);
    CHECK(f.jeu.agence.mois == mois_faillite, "temps : apres la faillite, le calendrier s arrete");
  }

  // ---- 13. LE RYTHME DU TEMPS EN MISSION [GDD 14.3] -----------------------
  // « Toute manoeuvre fine RAMENE le temps a un rythme lent. » Ce qui est verifie
  // ici : que le plafond est DEDUIT (pas saisi), qu'il MORD (le temps ne coule
  // pas au-dessus), qu'il RAMENE la cadence de lui-meme, qu'il se LEVE quand la
  // phase critique est passee, et qu'il n'enleve jamais au joueur le droit de
  // ralentir ou de mettre en pause.
  {
    using fen::mission::FlightPhase;
    using game::TimeRate;

    // (a) LA LOI EST UNE DEDUCTION : le plafond sort de la duree de la phase et
    // du temps d'observation exige, jamais d'une table de crans ecrite a la main.
    // Une phase de 9 min ne peut pas defiler a 1 jour/s ; une phase d'un an, si.
    CHECK(fen::mission::tempo_ceiling_for_duration(9.0 * 60.0) == TimeRate::Realtime,
          "tempo : une phase de 9 min impose le temps reel");
    // Une phase d'UN AN ne libere PAS encore la cadence maximale, et c'est la
    // loi qui le dit, pas une preference : a « mois/s » une annee defile en 12 s
    // reelles, sous les 20 s exigees. Il faut ~1,7 an pour meriter le cran plein.
    CHECK(fen::mission::tempo_ceiling_for_duration(365.0 * cst::DAY) == TimeRate::Week,
          "tempo : une phase d un an plafonne encore a la semaine/s");
    CHECK(fen::mission::tempo_ceiling_for_duration(5.0 * 365.0 * cst::DAY) == TimeRate::Month,
          "tempo : une phase de plusieurs annees n impose rien");
    CHECK(fen::mission::tempo_ceiling_for_duration(0.0) == TimeRate::Realtime,
          "tempo : le plancher est le TEMPS REEL, jamais la pause [GDD 14]");
    // Le plafond est monotone en duree : plus la phase est longue, plus on peut
    // accelerer. Une loi qui ne l'est pas serait un tableau deguise.
    bool monotone = true;
    double d_prec = 1.0;
    for (double d = 60.0; d <= 400.0 * cst::DAY; d *= 3.0) {
      if (static_cast<int>(fen::mission::tempo_ceiling_for_duration(d)) <
          static_cast<int>(fen::mission::tempo_ceiling_for_duration(d_prec)))
        monotone = false;
      d_prec = d;
    }
    CHECK(monotone, "tempo : le plafond croit avec la duree de la phase");

    // (b) SEULE une phase CRITIQUE contraint — et c'est le MEME predicat que
    // celui qui majore les taux d'anomalie (Events.hpp), pas un second jugement.
    CHECK(fen::mission::tempo_ceiling_for_phase(FlightPhase::TransferCruise) == TimeRate::Month,
          "tempo : la croisiere n impose aucun plafond");
    CHECK(fen::mission::tempo_ceiling_for_phase(FlightPhase::LeoOps) == TimeRate::Month,
          "tempo : les operations LEO n imposent aucun plafond");
    CHECK(fen::mission::tempo_ceiling_for_phase(FlightPhase::Launch) == TimeRate::Realtime,
          "tempo : l ascension impose le temps reel");
    CHECK(fen::mission::tempo_ceiling_for_phase(FlightPhase::Edl) == TimeRate::Realtime,
          "tempo : l EDL impose le temps reel");
    CHECK(fen::mission::tempo_ceiling_for_phase(FlightPhase::CriticalManeuver) == TimeRate::Realtime,
          "tempo : la manoeuvre critique impose le temps reel");
    CHECK(fen::mission::is_critical_phase(FlightPhase::Edl) &&
          !fen::mission::is_critical_phase(FlightPhase::Ground),
          "tempo : le predicat de phase critique est partage avec Events.hpp");

    // (c) LA PHASE EST DERIVEE, pas saisie : elle sort de l'etat FSM, du temps
    // passe dedans et de la famille. Une mission au sol n'est jamais en vol.
    fen::mission::Mission m;
    m.contract.id = "T-TEMPO";
    m.contract.family = "mars";
    m.state = fen::mission::MissionState::Design;
    m.state_entered_days = 100.0;
    CHECK(fen::mission::flight_phase_of(m, 100.0) == FlightPhase::Ground,
          "tempo : hors vol, la phase est AU SOL");
    m.state = fen::mission::MissionState::Launched;
    CHECK(fen::mission::flight_phase_of(m, 100.0) == FlightPhase::Launch,
          "tempo : au feu vert, la mission est en ASCENSION");
    // 9 min = 0,00625 j : juste avant, encore l'ascension ; juste apres, la croisiere.
    const double asc_j = fen::mission::phase_duration_s(FlightPhase::Launch) / cst::DAY;
    CHECK(fen::mission::flight_phase_of(m, 100.0 + asc_j * 0.99) == FlightPhase::Launch,
          "tempo : l ascension dure sa duree propre");
    // Passee l'ascension on ne croise pas : on est en ORBITE DE PARKING, et on y
    // reste une revolution avant l'injection. L'oracle disait « croisiere » du
    // temps ou la phase etait devinee ; la chronologie, elle, suit le vol reel.
    CHECK(fen::mission::flight_phase_of(m, 100.0 + asc_j * 1.01) == FlightPhase::LeoOps,
          "tempo : passee l ascension, on attend en orbite de parking");
    m.contract.family = "sat";
    CHECK(fen::mission::flight_phase_of(m, 100.0 + asc_j * 1.01) == FlightPhase::LeoOps,
          "tempo : passee l ascension, une mission proche opere en LEO");
    m.contract.family = "mars";

    // (d) LA MISSION LA PLUS CONTRAIGNANTE COMMANDE : deux vols ne s'annulent pas.
    std::vector<fen::mission::Mission> vols;
    CHECK(!fen::mission::tempo_limit(vols, 100.0).constrained,
          "tempo : sans mission en vol, aucune contrainte");
    fen::mission::Mission croisiere = m;
    croisiere.contract.id = "T-CROISIERE";
    croisiere.contract.family = "mars";
    croisiere.state_entered_days = 0.0;              // lancee depuis longtemps
    vols.push_back(croisiere);
    CHECK(!fen::mission::tempo_limit(vols, 100.0).constrained,
          "tempo : une croisiere seule ne contraint rien");
    fen::mission::Mission ascension = m;
    ascension.contract.id = "T-ASCENSION";
    ascension.state_entered_days = 100.0;
    vols.push_back(ascension);
    const fen::mission::TempoLimit lim = fen::mission::tempo_limit(vols, 100.0);
    CHECK(lim.constrained && lim.max_rate == TimeRate::Realtime,
          "tempo : une ascension en cours contraint toute la partie");
    CHECK(lim.mission_id == "T-ASCENSION",
          "tempo : le plafond NOMME la mission qui l impose");

    // (e) LE PLAFOND MORD SUR LA PARTIE : regler_cadence borne et le DIT, pour
    // que l'interface montre un refus au lieu d'un cran sans effet.
    Session t; t.nouvelle_partie("Tempo", ModeAide::Normal);
    t.tick(0.016);
    CHECK(t.jeu.regler_cadence(TimeRate::Month), "tempo : hors mission, la cadence est libre");
    CHECK(t.jeu.cadence == TimeRate::Month, "tempo : le cran demande est pose");
    CHECK(!t.jeu.plafond_temps().constrained, "tempo : aucune contrainte sans vol");

    // On met une mission EN VOL a l'instant courant (l'ascension commence).
    auto& G = *t.jeu.ares.etat;
    fen::mission::Mission vol;
    vol.contract.id = "VOL-01";
    vol.contract.family = "sat";
    vol.state = fen::mission::MissionState::Launched;
    vol.state_entered_days = G.clock.now_days();
    G.missions.push_back(vol);

    CHECK(t.jeu.plafond_temps().constrained, "tempo : l ascension contraint la partie");
    CHECK(t.jeu.plafond_temps().max_rate == TimeRate::Realtime,
          "tempo : le plafond de l ascension est le temps reel");
    CHECK(!t.jeu.regler_cadence(TimeRate::Month),
          "tempo : demander MOIS/S en ascension est REFUSE, et le dit");
    CHECK(t.jeu.cadence == TimeRate::Realtime, "tempo : la demande est bornee au plafond");
    // Le joueur garde le droit de RALENTIR et de mettre en PAUSE : le plafond
    // n'est qu'un maximum, jamais une cadence imposee.
    CHECK(t.jeu.regler_cadence(TimeRate::Paused), "tempo : la pause reste toujours permise");
    CHECK(t.jeu.cadence == TimeRate::Paused, "tempo : la pause est posee");

    // (f) LA MANOEUVRE RAMENE LE TEMPS : ecrire la cadence directement ne suffit
    // pas a passer outre — pas une seconde de jeu ne se convertit au-dessus du
    // plafond. C'est ce qui rend la faute impossible plutot que corrigee apres.
    t.jeu.cadence = TimeRate::Month;                 // contournement volontaire
    const double mois_avant = t.jeu.agence.mois;
    t.jeu.faire_couler_le_temps(0.25);
    CHECK(t.jeu.cadence == TimeRate::Realtime,
          "tempo : l ecoulement RAMENE la cadence sous le plafond");
    CHECK(t.jeu.agence.mois - mois_avant < 1.0 / 30.44,
          "tempo : le temps ecoule est celui du plafond, pas celui demande");

    // (g) LE PLAFOND SE LEVE tout seul quand la phase critique est passee : une
    // contrainte qui ne se leve pas serait une punition, pas un rythme.
    G.missions.back().state_entered_days = G.clock.now_days() - 2.0 * asc_j;
    CHECK(!t.jeu.plafond_temps().constrained,
          "tempo : l ascension terminee, la contrainte se leve");
    CHECK(t.jeu.regler_cadence(TimeRate::Month), "tempo : on peut re-accelerer apres l ascension");
    CHECK(t.jeu.cadence == TimeRate::Month, "tempo : la cadence rapide revient");

    // (h) LE PLAFOND EST PUBLIE sur le pont : sans lui a l'ecran, un cran refuse
    // serait incomprehensible (un mecanisme correct et invisible est absent).
    G.missions.back().state_entered_days = G.clock.now_days();
    t.tick(0.016);
    CHECK(g_render_bridge.tempo_contraint.load(), "tempo : la contrainte est publiee");
    CHECK(g_render_bridge.cadence_max.load() == (int)TimeRate::Realtime,
          "tempo : le plafond est publie pour le bandeau du temps");
    CHECK(g_render_bridge.tempo_phase.load() == (int)FlightPhase::Launch,
          "tempo : la phase qui impose le rythme est nommee sur le pont");
    // ... et la phase de vol derivee est RECOPIEE sur la mission : le champ que
    // personne ne renseignait porte enfin une valeur vivante (Events.hpp la lit).
    CHECK(G.missions.back().phase == FlightPhase::Launch,
          "tempo : Mission::phase est derivee, plus jamais un drapeau mort");
  }

  // ---- 14. LA CHRONOLOGIE DE VOL [GDD 4.1, 9, 14.3] ----------------------
  // Ce qui manquait au rythme en mission : une DATE. L'insertion et l'EDL
  // etaient des phases critiques que rien ne declenchait, donc un plafond qui ne
  // mordait qu'a l'ascension. On verifie ici que les durees sont DERIVEES (pas
  // saisies), que la chronologie est jointive, que le vol DURE, et que les
  // phases critiques d'arrivee contraignent enfin le temps.
  {
    using fen::mission::FlightPhase;
    using fen::mission::MissionState;
    using game::TimeRate;

    // (a) LES DUREES SONT DERIVEES DE KEPLER, jamais ecrites a la main. Chaque
    // valeur se verifie contre une grandeur connue du metier.
    const double t_park = fen::mission::parking_period_s();
    CHECK(std::fabs(t_park - 5310.0) < 60.0,
          "chrono : une revolution en parking a 200 km dure ~88,5 min");
    // L'ORBITE GEOSTATIONNAIRE N'EST PAS UN CHIFFRE : c'est la solution de
    // « periode orbitale == jour sideral ». On retrouve 42 164 km sans l'ecrire.
    CHECK(std::fabs(fen::mission::geo_radius_m() - 42164.0e3) < 20.0e3,
          "chrono : le rayon geostationnaire se DEDUIT de la rotation sidérale");
    // Transit sur l'ellipse parking -> GEO : une demi-periode, ~5 h 15.
    const double t_gto = fen::mission::gto_coast_s();
    CHECK(std::fabs(t_gto - 5.26 * 3600.0) < 0.15 * 3600.0,
          "chrono : le transit GTO dure une demi-periode d ellipse (~5 h 15)");
    CHECK(std::fabs(fen::mission::rendezvous_phasing_s() - 4.0 * t_park) < 1.0,
          "chrono : le phasage de rendez-vous est le profil a 4 orbites");
    // L'orbite de parking de la chronologie est CELLE dont l'injection est payee
    // (MissionLoop) : un chiffre, une source.
    CHECK(std::fabs(fen::mission::parking_radius_m() - (cst::R_EARTH + 200.0e3)) < 1.0,
          "chrono : le parking de la chronologie est celui du bilan de dv");

    // (b) LA CHRONOLOGIE EST JOINTIVE ET ORDONNEE : pas de trou entre deux
    // phases, pas de retour en arriere. Un vol dont les segments se chevauchent
    // rendrait la phase ambigue, donc le plafond arbitraire.
    fen::mission::Mission mm;
    mm.contract.id = "T-CHRONO";
    mm.contract.family = "mars";
    mm.state = MissionState::Launched;
    mm.state_entered_days = 500.0;
    mm.tof_days = 259.0;                       // duree de transit d une vraie fenetre
    const fen::mission::FlightTimeline tl = fen::mission::build_flight_timeline(mm);
    bool jointive = tl.n > 0;
    for (int i = 1; i < tl.n; ++i)
      if (std::fabs(tl.seg[i].t0_days - tl.seg[i - 1].t1_days) > 1e-12) jointive = false;
    CHECK(jointive, "chrono : les segments sont jointifs, sans trou ni chevauchement");
    CHECK(tl.seg[0].phase == FlightPhase::Launch,
          "chrono : tout vol commence par l ascension");

    // (c) L'INSERTION EST DATEE — le manque que ce chantier comble. Elle tombe a
    // la FIN de la croisiere, et elle est critique.
    const double t_go = mm.state_entered_days;
    const double t_arr = t_go + tl.duree_jours;
    CHECK(tl.dated, "chrono : une cible nommee donne une date d arrivee");
    CHECK(std::fabs(tl.duree_jours - 259.0) < 1.0,
          "chrono : la croisiere dure la duree de transit de la fenetre visee");
    CHECK(fen::mission::flight_phase_of(mm, t_go + 100.0) == FlightPhase::TransferCruise,
          "chrono : a mi-parcours, la mission croise");
    CHECK(fen::mission::flight_phase_of(mm, t_arr - 1e-4) == FlightPhase::CriticalManeuver,
          "chrono : a l arrivee, la mission INSERE — et c est date");
    CHECK(fen::mission::flight_phase_of(mm, t_arr + 1.0) == FlightPhase::LeoOps,
          "chrono : passee l insertion, la mission opere en orbite");

    // (d) LE PLAFOND MORD ENFIN AILLEURS QU A L ASCENSION. C'est la raison
    // d'etre de la chronologie : la loi de MissionTempo ne changeait pas, il lui
    // manquait un evenement date auquel s'appliquer.
    std::vector<fen::mission::Mission> vols_arr{mm};
    CHECK(!fen::mission::tempo_limit(vols_arr, t_go + 100.0).constrained,
          "chrono : en croisiere, le temps reste libre");
    const fen::mission::TempoLimit lim_ins =
        fen::mission::tempo_limit(vols_arr, t_arr - 1e-4);
    CHECK(lim_ins.constrained && lim_ins.max_rate == TimeRate::Realtime,
          "chrono : l INSERTION ramene le temps au rythme reel [GDD 14.3]");
    CHECK(lim_ins.phase == FlightPhase::CriticalManeuver,
          "chrono : le plafond NOMME l insertion");

    // ... et l'EDL pour une mission de surface, par le meme chemin.
    fen::mission::Mission ms = mm;
    ms.contract.id = "T-EDL";
    ms.contract.family = "surface";
    const fen::mission::FlightTimeline tls = fen::mission::build_flight_timeline(ms);
    const double t_edl = ms.state_entered_days + tls.duree_jours;
    CHECK(fen::mission::flight_phase_of(ms, t_edl - 1e-4) == FlightPhase::Edl,
          "chrono : une mission de surface finit par un EDL, date");
    CHECK(fen::mission::flight_phase_of(ms, t_edl + 1.0) == FlightPhase::SurfaceOps,
          "chrono : apres l EDL, on opere en surface");
    const fen::mission::TempoLimit lim_edl =
        fen::mission::tempo_limit(std::vector<fen::mission::Mission>{ms}, t_edl - 1e-4);
    CHECK(lim_edl.constrained && lim_edl.phase == FlightPhase::Edl,
          "chrono : l EDL contraint le temps, et se nomme");

    // (e) LE PROFIL SUIT LA PHYSIQUE DE LA FAMILLE, pas un genre litteraire.
    fen::mission::Mission mg = mm;
    mg.contract.family = "sat";                 // charge geostationnaire
    const fen::mission::FlightTimeline tlg = fen::mission::build_flight_timeline(mg);
    CHECK(tlg.dated && std::fabs(tlg.duree_jours * cst::DAY -
              (fen::mission::phase_duration_s(FlightPhase::Launch) + t_park +
               2.0 * fen::mission::phase_duration_s(FlightPhase::CriticalManeuver) +
               t_gto)) < 1.0,
          "chrono : une mise a poste GEO = ascension + parking + injection + transit + circularisation");
    fen::mission::Mission mr = mm;
    mr.contract.family = "logistique";
    const fen::mission::FlightTimeline tlr = fen::mission::build_flight_timeline(mr);
    CHECK(tlr.dated && tlr.duree_jours * cst::DAY < 8.0 * 3600.0,
          "chrono : un rendez-vous LEO se conclut en quelques heures, pas en mois");

    // (f) CE QU ON NE SAIT PAS CALCULER, ON LE DECLARE. Une famille dont le
    // contrat ne nomme pas de cible n'a pas de date d'arrivee — et le modele le
    // DIT au lieu d'inventer une duree. Un vol non date ne bloque donc rien.
    fen::mission::Mission mn = mm;
    mn.contract.family = "science";
    mn.tof_days = 0.0;
    CHECK(!fen::mission::build_flight_timeline(mn).dated,
          "chrono : sans cible nommee, l arrivee n est pas datee [GDD 6.8]");
    CHECK(fen::mission::arrival_gate(mn, mn.state_entered_days + 1.0).allowed,
          "chrono : on n oppose jamais une date qu on ne sait pas calculer");
    fen::mission::Mission mnep = mm;
    mnep.contract.family = "nep";
    CHECK(!fen::mission::build_flight_timeline(mnep).dated,
          "chrono : une spirale a poussee continue n a pas d insertion breve");

    // (g) LE VOL DURE : on ne debriefe pas une sonde encore en croisiere, et le
    // refus CHIFFRE l'attente (comme le gate de fenetre).
    const fen::mission::GateResult g_tot = fen::mission::arrival_gate(mm, t_go + 10.0);
    CHECK(!g_tot.allowed, "chrono : en croisiere, le debrief est refuse");
    CHECK(g_tot.reason.find("249") != std::string::npos,
          "chrono : le refus CHIFFRE les jours restants");
    CHECK(fen::mission::arrival_gate(mm, t_arr + 0.5).allowed,
          "chrono : arrivee, la mission peut etre debriefee");

    // (h) LE MODELE POSSEDE L'ISSUE DU VOL. Elle vivait sur la session d'UI, qui
    // ne se sauvegarde pas : un vol reussi puis recharge se concluait en echec.
    // Et une mission en cours ne se sauvegardait PAS DU TOUT — invisible tant
    // qu'un vol durait zero seconde, fatal des qu'il dure 259 jours.
    Session sv; sv.nouvelle_partie("Chrono", ModeAide::Normal);
    sv.tick(0.016);
    auto& Gs = *sv.jeu.ares.etat;
    // On vise une mission A FENETRE SYNODIQUE : ce sont les seules dont la
    // croisiere est datee par une vraie geometrie, donc les seules qui mettent
    // la restitution a l'epreuve sur des centaines de jours. Le critere est le
    // MEME predicat que celui du modele (`window_target_for_family`), pas une
    // liste de familles recopiee ici. Le contrat est REAPPARIE par son id au
    // chargement : sa famille vient du catalogue, pas de la sauvegarde.
    std::size_t idx_vol = 0;
    bool trouve_fenetre = false;
    for (std::size_t i = 0; i < Gs.catalog.entries().size(); ++i)
      if (fen::mission::window_target_for_family(
              Gs.catalog.entries()[i].contract.family).impose) {
        idx_vol = i; trouve_fenetre = true; break;
      }
    const std::string id_vol = Gs.catalog.entries().empty()
                                 ? std::string()
                                 : Gs.catalog.entries()[idx_vol].contract.id;
    CHECK(!id_vol.empty(), "chrono : le catalogue porte au moins un contrat");
    CHECK(trouve_fenetre,
          "chrono : le catalogue porte une mission a fenetre synodique [GDD 10.1]");
    fen::mission::Mission mv;
    mv.contract = Gs.catalog.entries()[idx_vol].contract;
    mv.state = MissionState::Launched;
    mv.state_entered_days = Gs.clock.now_days();
    mv.tof_days = 259.0;
    mv.flight_flown = true;
    mv.flight_success = true;
    // LE LOGICIEL EMBARQUE EST UN FAIT DU VOL, donc il se sauvegarde : un vol
    // recharge doit voler avec le code qu'il transporte, et avec la meme
    // couverture [GDD 15.5]. Sans cela, quitter au menu absoudrait un logiciel
    // embarque hors de son domaine — l'issue changerait au rechargement.
    mv.code_embarque = true;
    mv.code_non_couvert = true;
    // ═══ ET LE VAISSEAU PARTI EN EST UN AUSSI ═══ [GDD 12.2, 17.2] (V8)
    // La coupe du véhicule se reconstruit depuis la pile, ses ergols, sa capsule
    // et sa charge utile. Les recalculer au chargement les prendrait à la
    // conception COURANTE — que le joueur a pu retoucher pendant le vol.
    mv.vaisseau_etages.push_back({0, 0, 4200.0});
    mv.vaisseau_etages.push_back({0, 0, 1900.0});
    mv.vaisseau_capsule = -1;
    mv.vaisseau_payload_kg = 1500.0;
    Gs.missions.push_back(mv);
    const std::string chemin_v = tmp + "/oracle_chrono.sav";
    sv.chemin_sauvegarde = chemin_v;
    sv.sauvegarder_partie();
    Session sv2;
    CHECK(sv2.charger_partie(chemin_v), "chrono : la partie se recharge");
    auto& G2 = *sv2.jeu.ares.etat;
    CHECK(G2.missions.size() == 1, "chrono : la mission EN VOL survit a la sauvegarde");
    if (G2.missions.size() == 1) {
      const fen::mission::Mission& r = G2.missions[0];
      CHECK(r.contract.id == id_vol,
            "chrono : le contrat est reapparie depuis le catalogue, par son id");
      CHECK(r.state == MissionState::Launched, "chrono : l etat FSM est restitue");
      CHECK(std::fabs(r.state_entered_days - mv.state_entered_days) < 1e-9,
            "chrono : la date du feu vert est restituee, donc la chronologie aussi");
      CHECK(std::fabs(r.tof_days - 259.0) < 1e-9,
            "chrono : la duree de transit figee au feu vert survit");
      CHECK(r.flight_flown && r.flight_success,
            "chrono : l issue du vol appartient au modele, pas a l ecran");
      CHECK(r.code_embarque && r.code_non_couvert,
            "chrono : le logiciel embarque et son domaine survivent a la sauvegarde");
      CHECK(r.contract.terms.budget_musd == mv.contract.terms.budget_musd,
            "chrono : le contrat restitue porte ses termes physiques");
      // L'INVARIANT QUI COMPTE : la chronologie RECONSTRUITE est la meme. C'est
      // pour cela qu'on sauvegarde la date du feu vert et la duree de transit,
      // et rien d'autre — la chronologie n'est pas un etat, c'est un calcul.
      const double t_ref = Gs.clock.now_days();
      const fen::mission::ArrivalStatus a0 = fen::mission::flight_arrival(mv, t_ref);
      const fen::mission::ArrivalStatus a1 = fen::mission::flight_arrival(r, t_ref);
      CHECK(a0.dated && a1.dated && std::fabs(a0.reste_jours - a1.reste_jours) < 1e-9,
            "chrono : la date d arrivee est identique avant et apres rechargement");
      CHECK(a1.reste_jours > 250.0,
            "chrono : une croisiere martienne rechargee dure encore des mois");

      // ═══ LE VAISSEAU PARTI SURVIT, ET SA COUPE EST LA MÊME ═══ [GDD 12.2, 17.2]
      CHECK(r.vaisseau_etages.size() == 2,
            "vaisseau : la pile figee au feu vert survit a la sauvegarde (V8)");
      if (r.vaisseau_etages.size() == 2) {
        CHECK(std::fabs(r.vaisseau_etages[0].propellant_kg - 4200.0) < 1e-9 &&
              std::fabs(r.vaisseau_etages[1].propellant_kg - 1900.0) < 1e-9,
              "vaisseau : les ergols de CHAQUE etage survivent — la coupe en depend");
        CHECK(r.vaisseau_capsule == -1 &&
              std::fabs(r.vaisseau_payload_kg - 1500.0) < 1e-9,
              "vaisseau : capsule et charge utile survivent");
      }
      const fen::vehicle::VehicleHull h0 = Session::coupe_du_vol(mv);
      const fen::vehicle::VehicleHull h1 = Session::coupe_du_vol(r);
      CHECK(h0.valid && h1.valid, "vaisseau : le vol recharge a une coupe");
      CHECK(std::fabs(h0.length_m - h1.length_m) < 1e-12 &&
            std::fabs(h0.max_diameter_m - h1.max_diameter_m) < 1e-12,
            "vaisseau : la coupe est identique avant et apres rechargement");
      std::printf("     VAISSEAU : coque rechargee %.2f m x %.2f m (%d segments)\n",
                  h1.length_m, h1.max_diameter_m, (int)h1.segments.size());

      // ET L'INVARIANT QUI JUSTIFIE LE GEL : retoucher la conception ne touche
      // PAS un vaisseau déjà parti. C'est le défaut que `tof_days` et
      // `tour_arcs` évitent déjà, appliqué à la géométrie.
      sv2.vehicule_design.payload_kg *= 4.0;
      sv2.vehicule_design.stages[0].dv_target_ms *= 2.0;
      const fen::vehicle::VehicleHull h2 = Session::coupe_du_vol(G2.missions[0]);
      CHECK(std::fabs(h2.length_m - h1.length_m) < 1e-12,
            "vaisseau : retoucher l atelier ne deforme pas un vol deja parti");
    }

    // (i) LE VOL EST PUBLIE SUR LE PONT : phase et jours restants. Sans eux, une
    // mission lancee parait figee pendant des mois de temps de jeu. On compare
    // au MODELE, jamais a un nombre suppose : le pont ne calcule rien.
    sv.tick(0.016);
    CHECK(g_render_bridge.vol_actif.load(), "chrono : le vol en cours est publie");
    CHECK(g_render_bridge.vol_arrivee_datee.load(),
          "chrono : le pont dit si l arrivee est datee");
    CHECK(g_render_bridge.vol_phase.load() ==
              (int)fen::mission::flight_phase_of(Gs.missions[0], Gs.clock.now_days()),
          "chrono : la phase publiee est celle du modele");
    CHECK(std::fabs(g_render_bridge.vol_reste_jours.load() -
                    fen::mission::flight_arrival(Gs.missions[0], Gs.clock.now_days()).reste_jours) < 1e-9,
          "chrono : les jours restants publies sont ceux du modele");
  }

  // ---- 15. LA TRACE DU VOL DANS LE MONDE [GDD 8.1, 8.3, 17.3] ------------
  // La chronologie dit QUAND, la trace dit OU. Le tracé du pont existait et
  // DORMAIT : le rendu savait dessiner trajectoire, corridor et nœuds, plus
  // personne ne les publiait. On vérifie que l'arc est un VRAI arc (Lambert sur
  // l'éphéméride, propagé par Kepler), qu'il RELIE les deux corps aux dates de
  // la chronologie, et que la position du vaisseau est un point DE cet arc.
  {
    using fen::mission::MissionState;
    Session tv; tv.nouvelle_partie("Trace", ModeAide::Normal);
    tv.tick(0.016);
    auto& Gt = *tv.jeu.ares.etat;
    const double now_days = Gt.clock.now_days();
    const double now_tdb = tv.jeu.epoch_courant();

    // ON PART DANS LA FENETRE, comme le gate l'impose au joueur. Ce n'est pas un
    // detail de confort : hors fenetre, Lambert repond quand meme, par un arc
    // valide mais qu'aucune mission ne volerait (piege n°63). L'oracle doit donc
    // tester le vol que le JEU autorise.
    fen::mission::Mission mt;
    mt.contract.id = "T-TRACE";
    mt.contract.family = "mars_habite";
    mt.state = MissionState::Launched;
    const auto W0 = fen::astro::launch_window(
        tv.jeu.eph, fen::ephem::Body::EarthBary, fen::ephem::Body::Mars,
        fen::Epoch{now_tdb});
    // ═══ L'ATTENTE VIENT DU MÊME TRANSFERT QUE LA DURÉE ═══ [défaut du 2026-08-01]
    // Cette ligne calculait l'attente sur `open`/`next_open_days` puis prenait la
    // durée de transit AILLEURS. Or `open` peut être vrai GRÂCE À un départ dans
    // 50 jours : le vol partait alors le jour même en emportant la durée d'un
    // départ qu'il ne faisait pas, et l'arc plongeait à 0,862 UA pour une injection
    // de 5 827 m/s. Les deux grandeurs sortent maintenant du MÊME appel de fenêtre.
    (void)W0;
    const double attente = fen::mission::transfer_wait_days(mt, fen::Epoch{now_tdb},
                                                            tv.jeu.eph);
    mt.state_entered_days = now_days + attente;
    mt.tof_days = fen::mission::transfer_tof_days(
        mt, fen::Epoch{now_tdb + attente * cst::DAY}, tv.jeu.eph);
    CHECK(mt.tof_days > 100.0 && mt.tof_days < 500.0,
          "trace : la duree de transit vient de la vraie fenetre");

    const fen::mission::FlightTrace T =
        fen::mission::build_flight_trace(mt, now_days, now_tdb, tv.jeu.eph);
    CHECK(T.ok && T.n >= 2, "trace : l arc heliocentrique est resolu");

    // ═══ LE GATE ET LA TRAJECTOIRE DOIVENT PARLER DU MÊME VOL ═══
    // L INVARIANT QUI MANQUAIT, et son absence a coute trois oracles rouges le
    // 2026-08-01. Le gate utilisait `slop_days` = 60 et la duree de transit un pas
    // de balayage (10 j) : le gate ouvrait grace a un transfert partant dans
    // 50 jours pendant que le vol decollait le jour meme avec la duree de CE
    // transfert-la. Arc a 0,862 UA, injection a 5 827 m/s. Un seul reglage
    // maintenant (`mission_window_params`), et l invariant se verifie :
    //   attendre `transfer_wait_days` DOIT rendre la fenetre ouverte.
    // Il ne depend d aucune date : il est vrai tous les jours de l annee.
    {
      const double t_apres = now_tdb + attente * cst::DAY;
      const fen::mission::GateResult g_apres =
          fen::mission::launch_window_gate(mt, fen::Epoch{t_apres}, tv.jeu.eph);
      CHECK(g_apres.allowed,
            "fenetre : apres l attente annoncee, le gate est OUVERT (un seul reglage)");
      if (attente == 0.0)
        CHECK(fen::mission::launch_window_gate(mt, fen::Epoch{now_tdb},
                                               tv.jeu.eph).allowed,
              "fenetre : attente nulle signifie gate deja ouvert, jamais l inverse");
      // Et le couple (depart, duree) est celui du MEME instant.
      CHECK(std::fabs(mt.tof_days
                      - fen::mission::transfer_tof_days(mt, fen::Epoch{t_apres},
                                                        tv.jeu.eph)) < 1e-9,
            "fenetre : la duree de transit est celle de la date de DEPART");
      std::printf("     fenetre : attente %.1f j -> depart a +%.1f j, transit %.1f j"
                  " (un seul reglage de fenetre pour le gate et la trajectoire)\n",
                  attente, T.nodes[0].t_days - now_days, mt.tof_days);
    }

    // (a) L'ARC RELIE LES DEUX CORPS, AUX DATES DE LA CHRONOLOGIE. C'est
    // l'oracle qui compte : si le depart ne tombe pas sur la Terre a la date
    // d'injection, l'arc est un dessin, pas une trajectoire.
    const double t_dep = T.nodes[0].t_days, t_arr = T.nodes[1].t_days;
    const auto pos_de = [&](fen::ephem::Body b, double jours) {
      return tv.jeu.eph.state(b, fen::ephem::Body::Sun,
                              fen::Epoch{now_tdb + (jours - now_days) * cst::DAY}).r;
    };
    const fen::Vec3 rTerre = pos_de(fen::ephem::Body::EarthBary, t_dep);
    const fen::Vec3 rMars  = pos_de(fen::ephem::Body::Mars, t_arr);
    // UN SEUIL ABSOLU SUR UNE GRANDEUR RELATIVE FINIT PAR MENTIR. Ces deux
    // residus valaient « < 1 m » — un chiffre choisi sur les dates du jour ou
    // l'oracle a ete ecrit. Or la geometrie de l'arc BOUGE avec la date reelle
    // (la fenetre synodique est cherchee depuis l'epoque courante), et 512
    // propagations de Kepler sur 2,3e11 m accumulent quelques 1e-12 relatifs :
    // l'arrivee est passee a 1,359 m un beau matin, sans que rien n'ait change
    // dans le modele. Meme famille que le piege n°67.
    //
    // Ce que la propriete affirme, c'est que l'arc TOUCHE les deux corps. On le
    // mesure donc RELATIVEMENT a l'arc, avec une borne qui reste des ordres de
    // grandeur sous le rayon d'une planete (1e-10 x 2,3e11 m ~ 23 m, contre
    // 3,4e6 m de rayon martien) : la preuve est intacte, la fragilite est partie.
    const double taille_arc = fen::norm(rMars - rTerre);
    const double borne_extremite = 1.0e-10 * taille_arc;
    std::printf("     trace : residu aux extremites — depart %.3f m, arrivee %.3f m "
                "(borne %.1f m sur un arc de %.3f UA)\n",
                fen::norm(T.traj[0] - rTerre), fen::norm(T.traj[T.n - 1] - rMars),
                borne_extremite, taille_arc / cst::AU);
    CHECK(fen::norm(T.traj[0] - rTerre) < borne_extremite,
          "trace : l arc PART de la Terre a la date d injection");
    CHECK(fen::norm(T.traj[T.n - 1] - rMars) < borne_extremite,
          "trace : l arc ARRIVE sur Mars a la date d insertion");
    CHECK(t_arr - t_dep > 100.0,
          "trace : l arc dure la croisiere, pas un instant");

    // (b) C'EST UNE TRAJECTOIRE, PAS UNE INTERPOLATION. Un segment droit entre
    // les deux corps passerait bien plus pres du Soleil que l'arc reel : le
    // point median de l'arc doit s'en ecarter nettement.
    const fen::Vec3 milieu_arc = T.traj[T.n / 2];
    const fen::Vec3 milieu_corde = (rTerre + rMars) * 0.5;
    CHECK(fen::norm(milieu_arc - milieu_corde) > 1.0e10,
          "trace : le milieu de l arc n est PAS le milieu de la corde (>0,07 UA)");
    // ... et surtout : L'ARC EST UNE CONIQUE. C'est l'invariant qui distingue
    // une trajectoire d'un dessin — l'energie specifique et le moment cinetique
    // sont CONSTANTS le long d'une propagation keplerienne, et ne le seraient
    // pour aucune courbe interpolee. On les mesure sur la polyligne elle-meme,
    // via la vitesse reconstruite par propagation.
    // ON BALAIE TOUS LES POINTS, pas un sur seize : un echantillonnage lache
    // laisse passer exactement ce qu'on cherche — une poignee de points aberrants
    // au milieu d'une courbe saine (piege n°62).
    int n_aberrants = 0;
    double r_poly_max = 0.0;
    for (int k = 0; k < T.n; ++k) {
      const double r = fen::norm(T.traj[k]);
      if (!std::isfinite(r) || r <= 0.0) { ++n_aberrants; continue; }
      r_poly_max = std::max(r_poly_max, r / 1.495978707e11);
    }
    std::printf("     polyligne : %d points, %d aberrants, rayon max %.3f UA\n",
                T.n, n_aberrants, r_poly_max);
    CHECK(n_aberrants == 0, "trace : aucun point de la polyligne n est aberrant");
    CHECK(r_poly_max < 3.0, "trace : AUCUN point ne part a l infini");

    double h_min = 1e300, h_max = -1e300, e_min = 1e300, e_max = -1e300;
    double r_min_ua = 1e300, r_max_ua = -1e300;
    const double TOF_S = (t_arr - t_dep) * cst::DAY;
    for (int k = 0; k < T.n; k += 16) {
      const double dt = TOF_S * (double)k / (double)(T.n - 1);
      const auto K = fen::astro::kepler_propagate(T.r_dep, T.v_dep, dt, cst::MU_SUN);
      const double h = fen::norm(fen::cross(K.r, K.v));
      const double r = fen::norm(K.r);
      const double en = 0.5 * fen::norm2(K.v) - cst::MU_SUN / r;
      h_min = std::min(h_min, h); h_max = std::max(h_max, h);
      e_min = std::min(e_min, en); e_max = std::max(e_max, en);
      r_min_ua = std::min(r_min_ua, r / 1.495978707e11);
      r_max_ua = std::max(r_max_ua, r / 1.495978707e11);
    }
    std::printf("     arc : perihelie %.3f UA, aphelie %.3f UA, transit %.0f j\n",
                r_min_ua, r_max_ua, t_arr - t_dep);
    CHECK(std::fabs(h_max - h_min) / h_max < 1e-9,
          "trace : le moment cinetique est CONSTANT le long de l arc (c est une conique)");
    CHECK(std::fabs(e_max - e_min) / std::fabs(e_max) < 1e-9,
          "trace : l energie specifique est CONSTANTE le long de l arc");
    // ═══ CE QUE LA FENETRE ACHETE, VU SUR L'ARC ═══
    // Un transfert PRIS DANS SA FENETRE reste ENTRE les deux orbites : il ne
    // plonge pas vers le Soleil pour aller chercher Mars. C'est l'oracle qui
    // aurait attrape le piege n°63 tout seul — et il vaut mieux que « l arc
    // reste dans le systeme interne », qui laissait passer un plongeon a
    // 0,26 UA. Marge de 5 % sous le perihelie terrestre (0,983 UA).
    CHECK(r_min_ua > 0.93, "trace : un transfert EN FENETRE ne plonge pas vers le Soleil");
    CHECK(r_max_ua < 1.75, "trace : ... et ne depasse pas l aphelie martien (1,666 UA)");

    // (c) LA POSITION EST UN POINT DE L'ARC, et elle AVANCE avec le temps. On
    // propage a mi-croisiere : le vaisseau doit s'y trouver, et pres du point
    // d'echantillon correspondant.
    fen::mission::FlightTrace T2 = T;
    const double t_mid = 0.5 * (t_dep + t_arr);
    fen::mission::trace_avancer(T2, t_mid);
    CHECK(T2.sur_arc, "trace : a mi-croisiere, le vaisseau est SUR l arc");
    // LA POSITION EST UN POINT DE LA COURBE TRACEE, exactement : on avance a la
    // date de l'echantillon k et on doit retomber sur traj[k] au metre pres.
    // (Comparer au « milieu » de l'index serait faux d'un demi-echantillon —
    // 0,3 jour, soit 700 000 km : la discretisation aurait masque le test.)
    const int k_test = 137;
    fen::mission::FlightTrace T3 = T;
    fen::mission::trace_avancer(
        T3, t_dep + (t_arr - t_dep) * (double)k_test / (double)(T.n - 1));
    CHECK(fen::norm(T3.pos - T.traj[k_test]) < 1.0,
          "trace : a la date d un echantillon, la position EST cet echantillon");
    CHECK(fen::norm(T2.pos - T.pos) > 1.0e10,
          "trace : la position AVANCE avec le temps de jeu");
    // Aux bornes, le vaisseau est dans le pixel de son corps — declare.
    fen::mission::trace_avancer(T2, t_dep - 1.0);
    CHECK(!T2.sur_arc && fen::norm(T2.pos - T.traj[0]) < 1.0,
          "trace : avant l injection, le vaisseau est encore a la Terre");
    fen::mission::trace_avancer(T2, t_arr + 1.0);
    CHECK(!T2.sur_arc && fen::norm(T2.pos - T.traj[T.n - 1]) < 1.0,
          "trace : apres l arrivee, le vaisseau est a destination");

    // (d) LES NŒUDS SONT LES MANŒUVRES DE LA CHRONOLOGIE, et « fait » se LIT de
    // la date au lieu de se cocher (piege n°20b).
    fen::mission::trace_avancer(T2, t_dep + 1.0);
    CHECK(T2.nodes[0].done && !T2.nodes[1].done,
          "trace : en croisiere, l injection est faite et l arrivee ne l est pas");

    // (e) L'ARC EST FIGE AU FEU VERT : sa signature ne depend que de la date, de
    // la duree de transit et de la destination. Sans cela on resoudrait Lambert
    // et 512 propagations a chaque frame.
    const double sig = fen::mission::flight_trace_signature(mt);
    CHECK(fen::mission::flight_has_arc(mt), "trace : un vol datable a un arc");
    fen::mission::Mission mt2 = mt;
    mt2.state_entered_days += 1.0;
    CHECK(fen::mission::flight_trace_signature(mt2) != sig,
          "trace : changer la date du feu vert change l arc");
    fen::mission::Mission mt3 = mt;
    mt3.contract.family = "sat";
    CHECK(!fen::mission::flight_has_arc(mt3),
          "trace : une mise a poste GEO n a pas d arc heliocentrique [piege n°41]");
    fen::mission::Mission mt4 = mt;
    mt4.tof_days = 0.0;
    CHECK(!fen::mission::flight_has_arc(mt4),
          "trace : sans duree de transit, pas d arc — et rien n est invente");
    // LE SENTINELLE NE VIT PAS DANS LE DOMAINE DE LA VALEUR (piege n°61) : un
    // feu vert ANTERIEUR a l'origine du calendrier donne une signature negative,
    // et c'est un vol parfaitement valide — toute capture epinglant une
    // croisiere en est un.
    fen::mission::Mission mt5 = mt;
    mt5.state_entered_days = -165.0;
    CHECK(fen::mission::flight_trace_signature(mt5) < 0.0 &&
          fen::mission::flight_has_arc(mt5),
          "trace : une signature negative n est pas une absence d arc");

    // (f) LA TRACE EST PUBLIEE SUR LE PONT, et le rendu n y recalcule rien.
    Gt.missions.push_back(mt);
    tv.tick(0.016);
    CHECK(g_render_bridge.vehicle.valid.load(), "trace : le pont porte la trace du vol");
    CHECK(g_render_bridge.vehicle.n >= 2, "trace : la polyligne est publiee");
    CHECK(std::fabs(g_render_bridge.vehicle.traj_m[0][0] - T.traj[0].x) < 1.0,
          "trace : la polyligne publiee est celle du modele");
    CHECK(g_render_bridge.vehicle.n_nodes == 2, "trace : les deux nœuds sont publies");
    CHECK(g_render_bridge.vehicle.corridor_3s_m == 0.0,
          "trace : le corridor est NUL tant que rien ne fait diverger le vol [GDD 6.8]");
    // L'ARC NE SE RECALCULE PAS SANS RAISON : deux frames de suite, la generation
    // ne bouge plus.
    const int gen1 = g_render_bridge.vehicle.gen.load();
    tv.tick(0.016);
    tv.tick(0.016);
    CHECK(g_render_bridge.vehicle.gen.load() == gen1,
          "trace : l arc est fige — aucune reconstruction par frame");

    // ---- 16. LA DISPERSION DE NAVIGATION [GDD 7.5, 8.1-8.5] --------------
    // `p_physics` valait 0,985 : un quart de la probabilite de succes ne
    // dependait de RIEN. On verifie ici que la chaine entiere est calculee —
    // Gates, Oberth, matrice de transition, Δv de correction, loi de Maxwell —
    // et que chaque maillon repond a une variation comme la physique l'exige.
    {
      const fen::Vec3 v_terre = tv.jeu.eph.state(
          fen::ephem::Body::EarthBary, fen::ephem::Body::Sun,
          fen::Epoch{now_tdb + (t_dep - now_days) * cst::DAY}).v;
      const double MARGE = 50.0;   // m/s provisionnes
      const fen::mission::NavDispersion D =
          fen::mission::nav_dispersion(T, v_terre, MARGE);
      CHECK(D.ok, "nav : la dispersion se calcule sur l arc reel");
      std::printf("     nav : dv_inj %.0f m/s, sigma %.2f m/s, Oberth x%.2f,\n"
                  "           manque au but 1s %.0f km, correction 1s %.2f m/s,\n"
                  "           p99 %.1f m/s, P(marge %.0f m/s) = %.3f\n",
                  D.dv_injection, D.sigma_dv_inj, D.oberth_gain, D.sigma_r_arr_km,
                  D.sigma_corr, D.dv_corr_p99, MARGE, D.p_marge);

      // (a) L'INJECTION EST CELLE DU BILAN Δv. Une injection martienne depuis une
      // orbite de parking a 200 km coute ~3,6 km/s (Oberth), pas le v∞ nu.
      CHECK(D.dv_injection > 3000.0 && D.dv_injection < 4500.0,
            "nav : l injection martienne depuis LEO coute 3-4,5 km/s");
      // (b) L'ERREUR EST AMPLIFIEE, et le facteur est celui de la physique :
      // v∞ δv∞ = v_p δv_p, donc le gain est v_p/v∞ — entre 3 et 4 pour Mars.
      CHECK(D.oberth_gain > 2.5 && D.oberth_gain < 5.0,
            "nav : l erreur au perigee est amplifiee par v_p/v_inf (effet Oberth)");
      CHECK(D.sigma_vinf > D.sigma_dv_inj,
            "nav : l erreur heliocentrique est PLUS GRANDE que l erreur du moteur");
      // (c) SANS CORRECTION, ON RATE LA PLANETE. C'est le fait qui justifie tout
      // le chapitre 8 du GDD : le manque au but se compte en millions de km.
      CHECK(D.sigma_r_arr_km > 1.0e5,
            "nav : sans correction, le manque au but depasse 100 000 km");
      // (d) LA CORRECTION EST BIEN PLUS PETITE QUE L ERREUR QU ELLE ANNULE —
      // c'est tout l'interet de corriger TOT, et ca sort du calcul, pas d'un
      // reglage : corriger a J+14 sur un transit de 329 j coute quelques m/s.
      CHECK(D.sigma_corr > 0.0 && D.sigma_corr < D.sigma_vinf,
            "nav : corriger tot coute MOINS que l erreur d injection elle-meme");
      CHECK(std::fabs(D.dv_corr_p99 - 3.3682 * D.sigma_corr) < 1e-6,
            "nav : le 99e centile est celui de la loi de Maxwell");
      CHECK(std::fabs(D.t_tcm_days - 14.0) < 1e-9,
            "nav : la correction a lieu a TCM-1, valeur SOURCEE (MSL L+15 j)");

      // (e) LA LOI DE MAXWELL EST UNE VRAIE LOI : croissante, bornee, et ses
      // valeurs remarquables tombent juste.
      CHECK(fen::mission::maxwell_cdf(0.0, 1.0) == 0.0, "nav : Maxwell(0) = 0");
      CHECK(fen::mission::maxwell_cdf(1e6, 1.0) > 0.999999, "nav : Maxwell(inf) = 1");
      CHECK(std::fabs(fen::mission::maxwell_cdf(3.3682, 1.0) - 0.99) < 1e-3,
            "nav : le 99e centile de Maxwell vaut bien 3,3682 sigma");
      bool croissante = true;
      double p_prec = -1.0;
      for (double x = 0.0; x <= 6.0; x += 0.25) {
        const double p = fen::mission::maxwell_cdf(x, 1.0);
        if (p < p_prec) croissante = false;
        p_prec = p;
      }
      CHECK(croissante, "nav : P(succes) croit avec la marge provisionnee");

      // (f) LA MARGE ACHETE VRAIMENT QUELQUE CHOSE — c'est le point du chantier.
      // Un plan sans marge echoue en navigation ; un plan genereux passe. Avant,
      // les deux valaient 0,985.
      const fen::mission::NavDispersion D0 = fen::mission::nav_dispersion(T, v_terre, 0.0);
      const fen::mission::NavDispersion D9 = fen::mission::nav_dispersion(T, v_terre, 500.0);
      CHECK(D0.p_marge < 0.01, "nav : sans marge de correction, la navigation echoue");
      CHECK(D9.p_marge > 0.99, "nav : une marge genereuse rend la navigation sure");
      CHECK(D0.sigma_corr == D.sigma_corr && D9.sigma_corr == D.sigma_corr,
            "nav : la marge ne change pas la physique, seulement la couverture");

      // (g) UN MOTEUR PLUS PRECIS ACHETE DE LA PROBABILITE. Gates est le seul
      // parametre de qualite d'execution : le diviser par deux doit se voir.
      fen::nav::GatesParams fin;
      fin.sigma_mag_prop = 0.001; fin.sigma_point_prop = 0.00075;
      const fen::mission::NavDispersion Df = fen::mission::nav_dispersion(T, v_terre, MARGE, fin);
      CHECK(Df.sigma_corr < D.sigma_corr && Df.p_marge > D.p_marge,
            "nav : un moteur deux fois plus precis double la couverture de marge");

      // (h) LE CORRIDOR CROIT AVEC LE TEMPS DE VOL, et il est NUL avant le
      // depart : on ne disperse pas ce qui n'est pas encore parti.
      const double c0 = fen::mission::corridor_3sigma_m(T, D, t_dep - 1.0);
      const double c1 = fen::mission::corridor_3sigma_m(T, D, t_dep + 10.0);
      const double c2 = fen::mission::corridor_3sigma_m(T, D, t_dep + 100.0);
      CHECK(c0 == 0.0, "nav : avant l injection, aucun corridor");
      CHECK(c1 > 0.0 && c2 > c1, "nav : le corridor CROIT tant que rien ne le retrecit");
      const double c_mid = fen::mission::corridor_3sigma_m(
          T, D, 0.5 * (t_dep + t_arr));
      std::printf("     nav : corridor 3s a J+10 %.0f km, J+100 %.0f km, "
                  "mi-croisiere %.0f km\n",
                  c1 / 1000.0, c2 / 1000.0, c_mid / 1000.0);
      // POURQUOI LE CORRIDOR SE LIT AU TERMINAL ET PAS SEULEMENT SUR LA CARTE
      // [GDD 8.3] : rapporte a la distance Terre-Soleil, il vaut quelques
      // millieme — donc quelques PIXELS au plan systeme. Ce qui n'est pas
      // separable a l'ecran doit etre CHIFFRE ailleurs (meme doctrine que
      // Novellus, piege n°41). L'oracle fixe l'ordre de grandeur pour que la
      // regle ne se perde pas.
      CHECK(c_mid / 1.495978707e11 < 0.05,
            "nav : a mi-croisiere le corridor fait moins de 5 % d une UA — sous-pixel en vue systeme");

      // (i) LE PLAN LE LIT : `p_physics` n'est plus une constante.
      Session sp; sp.nouvelle_partie("Nav", ModeAide::Normal);
      sp.tick(0.016);
      auto& Gp = *sp.jeu.ares.etat;
      // Une mission a fenetre synodique : c'est la seule dont la navigation se
      // calcule (meme predicat que le modele, jamais une liste recopiee).
      std::size_t ip = 0;
      for (std::size_t i = 0; i < Gp.catalog.entries().size(); ++i)
        if (fen::mission::window_target_for_family(
                Gp.catalog.entries()[i].contract.family).impose) { ip = i; break; }
      fen::mission::Mission mp;
      mp.contract = Gp.catalog.entries()[ip].contract;
      mp.state = MissionState::Design;
      mp.state_entered_days = Gp.clock.now_days();
      Gp.missions.push_back(mp);
      sp.piloter_premiere_mission();
      sp.mission_plan.program.dv_margin = 0.0;
      sp.evaluer_plan();
      const double p_sans = sp.mission_plan.p_physics;
      sp.mission_plan.program.dv_margin = 400.0;
      sp.evaluer_plan();
      const double p_avec = sp.mission_plan.p_physics;
      std::printf("     nav : P(navigation) sans marge %.3f, avec 400 m/s %.3f\n",
                  p_sans, p_avec);
      CHECK(p_avec > p_sans,
            "nav : provisionner de la marge AUGMENTE le P(succes) du plan");
      CHECK(p_sans != 0.985 && p_avec != 0.985,
            "nav : p_physics n est plus la constante 0,985");

      // (j) L'ERREUR EST RÉELLEMENT COMMISE, et elle est REJOUABLE. Jusqu'ici la
      // dispersion etait statistique : on savait ce qui POUVAIT arriver, rien
      // n'arrivait. `apply_gates` tire l'ecart sur un sous-flux dedie.
      const fen::mission::NavRealisation R1 =
          fen::mission::nav_realisation(T, D, 4242);
      const fen::mission::NavRealisation R2 =
          fen::mission::nav_realisation(T, D, 4242);
      const fen::mission::NavRealisation R3 =
          fen::mission::nav_realisation(T, D, 9999);
      CHECK(R1.ok, "nav : l injection s execute pour de bon");
      CHECK(R1.dv_inj_erreur == R2.dv_inj_erreur &&
            R1.dv_correction == R2.dv_correction,
            "nav : la meme graine rejoue le MEME vol");
      CHECK(R1.dv_correction != R3.dv_correction,
            "nav : une autre mission tire un autre ecart");
      std::printf("     nav : erreur commise %.2f m/s -> manque au but %.0f km "
                  "-> correction %.1f m/s\n",
                  R1.dv_inj_erreur, R1.miss_arr_km, R1.dv_correction);
      // L'ecart tire doit VIVRE dans la loi qui le decrit : quelques sigmas au
      // plus, jamais dix. C'est l'oracle qui attraperait un sous-flux mal cable.
      CHECK(R1.dv_inj_erreur > 0.0 && R1.dv_inj_erreur < 6.0 * D.sigma_dv_inj,
            "nav : l ecart commis tient dans la loi qui le decrit");
      CHECK(R1.miss_arr_km > 1000.0,
            "nav : un ecart de quelques m/s se paie en milliers de km au but");
      // ... et la correction reelle est du meme ordre que sa statistique : c'est
      // le controle croise entre la loi et son tirage.
      CHECK(R1.dv_correction < 6.0 * D.sigma_corr,
            "nav : la correction requise tient dans la dispersion calculee");

      // (k) L'ISSUE DE NAVIGATION EST UN FAIT, PLUS UN DE. Deux plans
      // identiques, marges differentes : celui qui a sous-provisionne echoue, et
      // pour un motif NOMME — le meme vol, deux issues, sans aucun alea.
      fen::mission::Mission mv2;
      mv2.contract.id = "T-NAV";
      mv2.contract.family = "mars_habite";
      mv2.state = MissionState::Launched;
      mv2.nav_evaluee = true;
      mv2.nav_dv_required = 120.0;
      fen::mission::MissionPlan pl;
      pl.assessment.p_success = 0.95; pl.assessment.p_physics = 0.985;
      pl.assessment.p_launcher = 0.98; pl.assessment.p_engine = 0.99;
      pl.program.dv_margin = 50.0;              // insuffisante
      const auto o_court = fen::mission::fly_mission(mv2, pl, 7);
      CHECK(!o_court.success, "nav : une marge trop courte fait ECHOUER le vol");
      CHECK(o_court.cause.find("navigation") != std::string::npos,
            "nav : et le motif d echec NOMME la navigation");
      CHECK(o_court.anomaly.modifiers.player_error_causal,
            "nav : sous-provisionner est une cause racine du joueur [GDD 10.3]");
      pl.program.dv_margin = 200.0;             // suffisante
      const auto o_large = fen::mission::fly_mission(mv2, pl, 7);
      CHECK(o_large.success || o_large.cause.find("navigation") == std::string::npos,
            "nav : marge suffisante, la navigation n est plus le probleme");
      // Le RISQUE N EST PAS COMPTE DEUX FOIS : navigation resolue, son facteur
      // sort du tirage.
      fen::mission::Mission mv3 = mv2;
      mv3.nav_evaluee = false;
      int n_ok_avec = 0, n_ok_sans = 0;
      for (std::uint64_t s = 0; s < 400; ++s) {
        if (fen::mission::fly_mission(mv2, pl, s).success) ++n_ok_avec;
        if (fen::mission::fly_mission(mv3, pl, s).success) ++n_ok_sans;
      }
      CHECK(n_ok_avec > n_ok_sans,
            "nav : une navigation RESOLUE ne pese plus dans le tirage");

      // (l) LE FAIT SE SAUVEGARDE : un vol rechargé garde l'erreur qu'il a faite.
      Session sn; sn.nouvelle_partie("NavSave", ModeAide::Normal);
      sn.tick(0.016);
      auto& Gn = *sn.jeu.ares.etat;
      fen::mission::Mission mn;
      mn.contract = Gn.catalog.entries()[ip].contract;
      mn.state = MissionState::Launched;
      mn.state_entered_days = Gn.clock.now_days();
      mn.tof_days = 300.0;
      mn.nav_evaluee = true;
      mn.nav_dv_required = 87.5;
      mn.nav_miss_km = 1234567.0;
      Gn.missions.push_back(mn);
      const std::string chemin_n = tmp + "/oracle_nav.sav";
      sn.chemin_sauvegarde = chemin_n;
      sn.sauvegarder_partie();
      Session sn2;
      CHECK(sn2.charger_partie(chemin_n), "nav : la partie se recharge");
      auto& Gn2 = *sn2.jeu.ares.etat;
      CHECK(Gn2.missions.size() == 1 && Gn2.missions[0].nav_evaluee &&
            std::fabs(Gn2.missions[0].nav_dv_required - 87.5) < 1e-9 &&
            std::fabs(Gn2.missions[0].nav_miss_km - 1234567.0) < 1e-6,
            "nav : l erreur commise survit a la sauvegarde — on ne la retire pas");

      // ---- 17. LA POURSUITE [GDD 7.5, 8.6] ------------------------------
      // « Sans poursuite, le joueur ne SAIT PAS que son erreur d'execution a eu
      // lieu » (en-tete de nav/Tracking.hpp). On verifie que cette phrase est
      // devenue VRAIE : sans mesures il croit son vol nominal, et chaque jour
      // d'arc achete rapproche son estime de la verite.
      const double t_dep_tdb = now_tdb + (t_dep - now_days) * cst::DAY;
      const auto sol = [&](double jours) {
        return fen::mission::nav_solution(T, D, R1, jours, t_dep_tdb, tv.jeu.eph, 4242);
      };
      const fen::mission::NavSolution S0 = sol(0.0);
      const fen::mission::NavSolution S3 = sol(3.0);
      const fen::mission::NavSolution S14 = sol(14.0);
      std::printf("     poursuite : 0 j -> %d mesures, sigma_v %.3f m/s, erreur estime %.3f m/s\n"
                  "                 3 j -> %d mesures, sigma_v %.3f m/s, erreur estime %.3f m/s\n"
                  "                14 j -> %d mesures, sigma_v %.3f m/s, erreur estime %.3f m/s\n"
                  "                (erreur VRAIE a estimer : %.3f m/s)\n",
                  S0.n_mesures, S0.sigma_v, S0.erreur_estime,
                  S3.n_mesures, S3.sigma_v, S3.erreur_estime,
                  S14.n_mesures, S14.sigma_v, S14.erreur_estime, S14.dv_vrai);

      CHECK(S0.ok && S14.ok, "poursuite : la solution se calcule");
      // (a) SANS POURSUITE, IL CROIT SON VOL NOMINAL — l'estime ne bouge pas de
      // l'a priori, et l'incertitude vaut la dispersion d'injection.
      CHECK(S0.n_mesures == 0, "poursuite : sans arc achete, aucune mesure");
      CHECK(fen::norm(S0.dv_estime) < 1e-9,
            "poursuite : sans mesure, l ecart estime est NUL — il croit son vol nominal");
      CHECK(std::fabs(S0.sigma_v - D.sigma_vinf) / D.sigma_vinf < 0.05,
            "poursuite : sans mesure, l incertitude EST la dispersion d injection");
      // (b) ACHETER DE LA POURSUITE ACHETE DE LA CONNAISSANCE. C'est le point :
      // `Program::tracking_days` etait facture depuis toujours sans rien faire.
      CHECK(S14.n_mesures > S3.n_mesures && S3.n_mesures > 0,
            "poursuite : un arc plus long fournit plus de mesures");
      CHECK(S14.sigma_v < S3.sigma_v && S3.sigma_v < S0.sigma_v,
            "poursuite : l incertitude DECROIT avec l arc achete");
      CHECK(S14.erreur_estime < S0.erreur_estime,
            "poursuite : l estime se rapproche de la VERITE quand on mesure");
      // (c) L ESTIME EST COHERENT AVEC SA PROPRE COVARIANCE : l'erreur restante
      // doit tenir dans quelques sigmas. Un filtre qui se croit plus precis
      // qu'il ne l'est serait pire qu'un filtre imprecis.
      CHECK(S14.erreur_estime < 5.0 * S14.sigma_v,
            "poursuite : l erreur restante tient dans l incertitude annoncee");
      // (d) LA VERITE N ENTRE QUE PAR LES MESURES : deux graines de bruit
      // differentes donnent deux solutions differentes, mais toutes deux
      // proches de la meme verite.
      const fen::mission::NavSolution Sb = fen::mission::nav_solution(
          T, D, R1, 14.0, t_dep_tdb, tv.jeu.eph, 777);
      CHECK(fen::norm(Sb.dv_estime - S14.dv_estime) > 0.0,
            "poursuite : un autre bruit de mesure donne une autre solution");
      CHECK(Sb.erreur_estime < S0.erreur_estime,
            "poursuite : ... mais elle vise la meme verite");
      // (e) LA SOLUTION EST REJOUABLE : meme graine, meme resultat.
      const fen::mission::NavSolution Sc = sol(14.0);
      CHECK(fen::norm(Sc.dv_estime - S14.dv_estime) == 0.0,
            "poursuite : meme graine, meme solution de navigation");

      // ---- 18. LA CAMPAGNE DE CORRECTION [GDD 8.4] ---------------------
      // LE POINT DE TOUT CE QUI PRECEDE : on corrige sur ce qu'on CROIT, pas sur
      // ce qui EST. C'est ici que l'achat de poursuite devient DECISIF.
      const auto camp = [&](const fen::mission::NavSolution& S) {
        return fen::mission::nav_campagne(T, D, R1, S, 4242);
      };
      const fen::mission::NavCampagne C0 = camp(S0);    // aveugle
      const fen::mission::NavCampagne C14 = camp(S14);  // 14 j de poursuite
      std::printf("     campagne : sans poursuite -> TCM1 %.1f + TCM2 %.1f = %.1f m/s, "
                  "manque final %.0f km\n"
                  "                avec 14 j     -> TCM1 %.1f + TCM2 %.1f = %.1f m/s, "
                  "manque final %.0f km\n",
                  C0.dv_tcm1, C0.dv_tcm2, C0.dv_total, C0.miss_final_km,
                  C14.dv_tcm1, C14.dv_tcm2, C14.dv_total, C14.miss_final_km);
      CHECK(C0.ok && C14.ok, "campagne : les deux campagnes se calculent");
      // (a) LA POURSUITE EST DECISIVE — c'est la phrase de nav/Tracking.hpp
      // devenue vraie : sans mesures, la correction est calculee sur un etat
      // faux, donc elle rate.
      CHECK(C14.miss_final_km < C0.miss_final_km,
            "campagne : poursuivre AMELIORE le manque au but final");
      CHECK(C14.miss_final_km < 0.05 * C0.miss_final_km,
            "campagne : ... et l ameliore d au moins un ordre de grandeur");
      // (b) CORRIGER COUTE : le Δv depense est du meme ordre que la statistique
      // qui l'avait annonce (dv_corr_p99), jamais gratuit ni delirant.
      CHECK(C14.dv_total > 0.0 && C14.dv_total < 3.0 * D.dv_corr_p99,
            "campagne : le Δv depense tient dans la marge que le bureau d etudes a chiffree");
      // (c) LA SECONDE MANŒUVRE RATTRAPE LA PREMIERE : elle est bien plus
      // petite, et c'est pour cela qu'une campagne reelle en enchaine plusieurs.
      CHECK(C14.dv_tcm2 < C14.dv_tcm1,
            "campagne : TCM-2 ne rattrape que le residu de TCM-1");
      // (d) REJOUABLE, comme tout le reste de la chaine.
      CHECK(camp(S14).dv_total == C14.dv_total,
            "campagne : meme graine, meme campagne");

      // ---- 19. LA MANŒUVRE EST UN ACTE DU JOUEUR [GDD 7.4, 2.2] --------
      // Tout ce qui precede calculait la correction A SA PLACE. Ici il commande
      // trois composantes en repere RSW, et le modele applique LITTERALEMENT ce
      // qu'il demande — Gates compris, aucun rattrapage silencieux.
      {
        const auto Kcible = fen::astro::kepler_propagate(
            T.r_dep, T.v_dep, (t_arr - t_dep) * cst::DAY, cst::MU_SUN);
        const fen::Vec3 cible = Kcible.r;
        const double t_tcm = t_dep + fen::mission::TCM1_APRES_INJECTION_J;

        // L'etat VRAI du vol, avance jusqu'a la date de correction.
        auto etat_a_tcm = [&]() {
          fen::mission::EtatVol e;
          e.valide = true; e.t_days = t_dep; e.r = T.r_dep; e.v = R1.v_dep_vraie;
          fen::mission::avancer_etat_vol(e, t_tcm);
          return e;
        };

        // (a) CE QU IL VOIT DEPEND DE CE QU IL A ACHETE. Sans poursuite, son
        // estime EST le nominal : le manque au but projete est NUL et il croit
        // tout aller bien. C'est la phrase de nav/Tracking.hpp, vue de l'ecran.
        const auto vue_aveugle = fen::mission::vue_navigation(
            T.r_dep, T.v_dep, S0.dv_estime, cible, t_dep, t_tcm, t_arr,
            S0.sigma_r, S0.sigma_v);
        const auto vue_poursuivie = fen::mission::vue_navigation(
            T.r_dep, T.v_dep, S14.dv_estime, cible, t_dep, t_tcm, t_arr,
            S14.sigma_r, S14.sigma_v);
        CHECK(vue_aveugle.ok && vue_poursuivie.ok, "manoeuvre : la vue se calcule");
        CHECK(vue_aveugle.manque_km < 1.0,
              "manoeuvre : SANS poursuite, le joueur ne voit AUCUN ecart a corriger");
        CHECK(vue_poursuivie.manque_km > 1.0e5,
              "manoeuvre : AVEC poursuite, il voit un manque au but de centaines de milliers de km");

        // (b) LE SOLVEUR EST UNE ASSISTANCE DE MODE, pas une automatisation : il
        // repond a la question posee, il ne decide ni du moment ni du geste.
        fen::Vec3 dv_rsw{};
        CHECK(fen::mission::solveur_correction(vue_poursuivie, dv_rsw),
              "manoeuvre : le solveur rend un Δv en repere RSW");
        const double dv_solveur = fen::norm(dv_rsw);
        std::printf("     manoeuvre : solveur -> R %.2f  S %.2f  W %.2f  (|dv| %.1f m/s)\n",
                    dv_rsw.x, dv_rsw.y, dv_rsw.z, dv_solveur);
        CHECK(dv_solveur > 1.0 && dv_solveur < 500.0,
              "manoeuvre : le Δv propose est du bon ordre de grandeur");

        // (c) EXECUTER LE Δv DU SOLVEUR AMENE AU BUT. C'est le bouclage complet :
        // mesurer -> calculer -> executer -> verifier [GDD 8.4].
        fen::mission::EtatVol e_bon = etat_a_tcm();
        const double manque_avant = fen::mission::manque_reel_km(e_bon, cible, t_arr);
        const auto r_bon = fen::mission::executer_correction(e_bon, vue_poursuivie, dv_rsw, 11);
        const double manque_apres = fen::mission::manque_reel_km(e_bon, cible, t_arr);
        std::printf("     manoeuvre : manque au but %.0f km -> %.0f km apres correction "
                    "(%.1f m/s depenses)\n",
                    manque_avant, manque_apres, r_bon.dv_depense);
        CHECK(r_bon.ok && r_bon.dv_depense > 0.0, "manoeuvre : le Δv est reellement depense");
        CHECK(manque_apres < 0.01 * manque_avant,
              "manoeuvre : une correction JUSTE divise le manque au but par cent");

        // (d) NE RIEN FAIRE EST UN CHOIX, ET IL COUTE. Le modele ne corrige
        // jamais en douce a la place du joueur.
        fen::mission::EtatVol e_rien = etat_a_tcm();
        const auto r_rien = fen::mission::executer_correction(e_rien, vue_poursuivie, {}, 11);
        CHECK(r_rien.ok && r_rien.dv_depense == 0.0, "manoeuvre : ne rien commander ne coute rien");
        CHECK(std::fabs(fen::mission::manque_reel_km(e_rien, cible, t_arr) - manque_avant) < 1.0,
              "manoeuvre : ... et ne corrige rien — le vol continue tel quel");

        // (e) UN Δv MAL ORIENTE EMPIRE LA TRAJECTOIRE. Le modele applique ce
        // qu'on lui donne : c'est la sanction du calcul faux [GDD 7.4].
        fen::mission::EtatVol e_faux = etat_a_tcm();
        fen::mission::executer_correction(e_faux, vue_poursuivie,
                                          {dv_rsw.x, -dv_rsw.y, dv_rsw.z}, 11);
        CHECK(fen::mission::manque_reel_km(e_faux, cible, t_arr) > manque_avant,
              "manoeuvre : un Δv mal oriente AGGRAVE le manque au but");

        // (f) CORRIGER SUR UN ETAT FAUX NE CORRIGE RIEN — le coeur du chapitre 8.
        // Le joueur aveugle ne voit aucun ecart, son solveur lui rend donc un Δv
        // quasi nul : il ne depense rien et rate tout.
        fen::Vec3 dv_aveugle{};
        fen::mission::solveur_correction(vue_aveugle, dv_aveugle);
        fen::mission::EtatVol e_aveugle = etat_a_tcm();
        fen::mission::executer_correction(e_aveugle, vue_aveugle, dv_aveugle, 11);
        CHECK(fen::mission::manque_reel_km(e_aveugle, cible, t_arr) > 0.5 * manque_avant,
              "manoeuvre : sans poursuite, corriger ne sert a rien [nav/Tracking.hpp]");

        // (g) LA CHRONOLOGIE DATE LES CORRECTIONS, donc le plafond de cadence
        // les IMPOSE : le joueur SERA la au moment d'agir [GDD 14.3].
        fen::mission::Mission mtcm = mt;
        const auto tl = fen::mission::build_flight_timeline(mtcm);
        int n_crit = 0;
        for (int i = 0; i < tl.n; ++i)
          if (tl.seg[i].phase == fen::mission::FlightPhase::CriticalManeuver) ++n_crit;
        CHECK(n_crit == 4,
              "manoeuvre : la chronologie date injection + DEUX corrections + insertion");
        const double t_tcm_abs = mtcm.state_entered_days +
            (tl.seg[0].t1_days + 0.0);   // repere : apres l'ascension
        (void)t_tcm_abs;
        CHECK(fen::mission::flight_phase_of(mtcm, t_dep + fen::mission::TCM1_APRES_INJECTION_J
                                            + 0.003) ==
              fen::mission::FlightPhase::CriticalManeuver,
              "manoeuvre : a la date de TCM-1, la phase est CRITIQUE");
        CHECK(fen::mission::tempo_limit(
                  std::vector<fen::mission::Mission>{mtcm},
                  t_dep + fen::mission::TCM1_APRES_INJECTION_J + 0.003).max_rate ==
              game::TimeRate::Realtime,
              "manoeuvre : ... et le monde retombe au TEMPS REEL pour qu il agisse");

        // ---- 20. LE GRAPHE DE NŒUDS [GDD 2.2, 15.1] -------------------
        // Le joueur ne COMMANDE plus seulement son Δv : il le CALCULE, en
        // assemblant des primitives typees qui SONT les fonctions de l'API.
        using fen::mission::TypeNoeud;
        const auto G = fen::mission::graphe_correction_reference();
        const auto r_graphe = fen::mission::evaluer_graphe(G, vue_poursuivie);
        CHECK(r_graphe.valide && r_graphe.evalue, "graphe : le graphe de reference s evalue");
        std::printf("     graphe : %zu noeuds -> R %.2f  S %.2f  W %.2f\n",
                    G.size(), r_graphe.dv_rsw.x, r_graphe.dv_rsw.y, r_graphe.dv_rsw.z);
        // (a) L EQUIVALENCE EST STRICTE [GDD 2.2] : le graphe assemble a la main
        // rend EXACTEMENT ce que le solveur du mode Normal rend. Si les deux
        // divergeaient, l'un des deux mentirait sur ce que fait l'autre.
        CHECK(fen::norm(r_graphe.dv_rsw - dv_rsw) < 1e-6,
              "graphe : le graphe assemble rend le MEME Dv que le solveur");
        // (b) LE TYPAGE REFUSE, ET IL DIT POURQUOI — c'est l'assistance que le
        // mode Normal accorde, l'exact equivalent du compilateur en mode Pro.
        const auto faux = fen::mission::evaluer_graphe(
            {TypeNoeud::SolutionNav, TypeNoeud::EcartCible, TypeNoeud::Propager,
             TypeNoeud::Commande}, vue_poursuivie);
        CHECK(!faux.valide, "graphe : un branchement mal type est REFUSE");
        CHECK(faux.noeud_fautif == 2 && faux.motif.find("DUREE") != std::string::npos,
              "graphe : ... et le refus NOMME le noeud et le type attendu");
        std::printf("     graphe : refus type -> noeud %d : %s\n",
                    faux.noeud_fautif, faux.motif.c_str());
        // (c) UN NŒUD EXIGE CE DONT IL A BESOIN : resoudre sans transition, ou
        // propager sans etat, sont des fautes de raisonnement, pas des plantages.
        const auto sans_stm = fen::mission::evaluer_graphe(
            {TypeNoeud::SolutionNav, TypeNoeud::EcartCible, TypeNoeud::ResoudreDv,
             TypeNoeud::VersRsw, TypeNoeud::Commande}, vue_poursuivie);
        CHECK(!sans_stm.valide && sans_stm.motif.find("TRANSITION") != std::string::npos,
              "graphe : resoudre sans TRANSITION est refuse, et le motif l explique");
        const auto sans_etat = fen::mission::evaluer_graphe(
            {TypeNoeud::TempsRestant, TypeNoeud::Propager, TypeNoeud::Commande},
            vue_poursuivie);
        CHECK(!sans_etat.valide && sans_etat.motif.find("ETAT") != std::string::npos,
              "graphe : propager sans ETAT en amont est refuse");
        // (d) LE GRAPHE DOIT ABOUTIR A UNE COMMANDE : un raisonnement qui ne
        // commande rien n'est pas une manoeuvre.
        const auto sans_fin = fen::mission::evaluer_graphe(
            {TypeNoeud::SolutionNav, TypeNoeud::TempsRestant}, vue_poursuivie);
        CHECK(!sans_fin.valide && sans_fin.motif.find("COMMANDE") != std::string::npos,
              "graphe : un graphe qui ne commande rien est refuse");
        // (e) CHAQUE NŒUD EST UNE FONCTION D API, et le declare — c'est ce qui
        // rend l'equivalence Normal/Pro verifiable et non promise.
        bool tous_nommes = true;
        for (const auto& d : fen::mission::noeuds_disponibles())
          if (!d.nom || !d.appel || d.appel[0] == '\0') tous_nommes = false;
        CHECK(tous_nommes, "graphe : chaque noeud NOMME la fonction d API qu il est");
        // (f) EXECUTER CE QUE LE GRAPHE COMMANDE FAIT LE MEME VOL que le solveur.
        fen::mission::EtatVol e_graphe = etat_a_tcm();
        fen::mission::executer_correction(e_graphe, vue_poursuivie, r_graphe.dv_rsw, 11);
        CHECK(std::fabs(fen::mission::manque_reel_km(e_graphe, cible, t_arr) - manque_apres) < 1.0,
              "graphe : le vol obtenu est celui du calcul assemble, au metre pres");

        // ---- 21. LE LOGICIEL DE VOL DU MODE PRO [GDD 15.1, 15.5, 18] ---
        // En PRO il n'y a plus de graphe : le joueur ECRIT le code qui decidera
        // a sa place. Ce bloc prouve le CABLAGE COMPLET — ce qu'aucune capture
        // ne peut montrer : la chaine compiler -> banc -> televerser -> executer,
        // et le fait que le Dv rendu par SON code fait vraiment voler le vaisseau.
        {
          // On pose l'etat de vol sur la session, comme `tirer_navigation` le
          // fait au feu vert, puis on avance l'horloge jusqu'a TCM-1.
          fen::mission::Mission mv = mt;
          mv.vol_vrai_valide = true;
          mv.vol_vrai_t_days = t_dep;
          for (int k = 0; k < 3; ++k) {
            mv.vol_vrai_r[k] = T.r_dep[k];
            mv.vol_vrai_v[k] = R1.v_dep_vraie[k];
            mv.nav_connu_dv[k] = S14.dv_estime[k];
          }
          mv.nav_sigma_r = S14.sigma_r;
          mv.nav_sigma_v = S14.sigma_v;
          tv.trace_vol = T;
          tv.mission_plan.program.dv_margin = 600.0;   // mission bien provisionnee
          // ON AVANCE PAR LE CALENDRIER DE L'AGENCE, pas en forcant l'horloge :
          // `agence.mois` est autoritaire et `ares.avancer` en DERIVE l'horloge
          // de mission (app/ares.hpp). Poser `clock` a la main marcherait une
          // frame, puis la synchronisation l'effacerait — et l'oracle aurait
          // teste un etat que le jeu n'atteint jamais.
          tv.jeu.avancer_temps(t_tcm - Gt.clock.now_days());
          tv.tick(0.016);
          CHECK(std::fabs(Gt.clock.now_days() - t_tcm) < 0.5,
                "code de vol : le monde est bien a la date de la correction");

          const fen::mission::VueNavigation VS = tv.vue_vol(mv);
          CHECK(VS.ok, "code de vol : la session rend la vue de navigation du moment");
          CHECK(std::fabs(VS.manque_km - vue_poursuivie.manque_km) < 1.0,
                "code de vol : ... et c est bien celle que le joueur lit a l ecran");

          // (a) CE QUE LE CODE RECOIT : rien de plus que ce que le joueur voit.
          const fen::code::EntreesVol E = tv.entrees_vol(mv);
          CHECK(E.sigma3_m == 3.0 * S14.sigma_r,
                "code de vol : le code recoit l incertitude 3s de la SOLUTION, jamais la verite");
          CHECK(std::fabs(E.tau_s - (t_arr - Gt.clock.now_days()) * cst::DAY) < 1.0,
                "code de vol : l horizon de manoeuvre est le temps qui RESTE");
          CHECK(fen::norm((E.pos - E.cible) - VS.manque_projete) < 1.0,
                "code de vol : la cible ramenee a maintenant rend l ecart projete exact");
          CHECK(E.tolerance_m == fen::mission::ARRIVEE_TOLERANCE_KM * 1000.0,
                "code de vol : la tolerance est CELLE de la boucle de mission, pas une copie");
          std::printf("     code de vol : 3s = %.0f km, horizon %.0f j, ecart projete %.0f km\n",
                      E.sigma3_m / 1000.0, E.tau_s / cst::DAY, VS.manque_km);

          // (b) L ORDRE DES ETAPES EST TENU [GDD 15.5] : on ne qualifie pas un
          // texte qui n'a pas compile, on ne televerse pas un texte non qualifie,
          // et rien ne s'execute a bord qui n'y soit monte.
          CHECK(!tv.banc_essai_vol(mv), "code de vol : pas de banc avant compilation [15.5]");
          CHECK(!tv.televerser_vol(), "code de vol : pas de televersement sans qualification");
          CHECK(!tv.executer_code_vol(mv), "code de vol : rien ne s execute qui ne soit a bord");

          // LES CHEMINS SONT FOURNIS, jamais devines : c'est la couche
          // plateforme qui sait ou vit le projet (ici, l'argument de l'oracle ;
          // dans le jeu, `FPaths` cote UEBridge).
          {
            const std::string racine = argc > 1 ? argv[1] : ".";
            const std::string tmpc =
                (std::filesystem::temp_directory_path() / "sp_session_code").string();
            std::error_code ec2;
            std::filesystem::create_directories(tmpc, ec2);
            tv.toolchain.dossier_travail = tmpc;
            tv.toolchain.includes = {racine + "/Source/SP/SpaceProgram/astro_core/include",
                                     racine + "/Source/SP/SpaceProgram/mission/include",
                                     racine + "/Source/SP/SpaceProgram"};
            tv.toolchain.sources = {racine +
                "/Source/SP/SpaceProgram/astro_core/src/Kepler.cpp"};
            tv.toolchain.vcvars = "C:\\Program Files\\Microsoft Visual Studio\\2022"
                                  "\\Community\\VC\\Auxiliary\\Build\\vcvars64.bat";
          }

          // (c) LA COMPILATION. Sans compilateur sur la machine, la chaine le DIT
          // au lieu de faire passer le code du joueur pour faux (piege n°69).
          const bool bChaine = tv.compiler_vol(&mv);
          if (tv.resultat_vol.issue == fen::code::IssueCode::Indisponible) {
            std::printf("     code de vol : compilateur absent — reste des oracles ignores\n");
          } else {
            CHECK(bChaine && tv.resultat_vol.ok(),
                  "code de vol : le squelette de [GDD 15.3] compile et s execute");
            CHECK(tv.source_vol_compilee(), "code de vol : ce TEXTE-ci est celui qui a compile");

            // (d) LE BANC COUTE, ET SA FICHE APPARTIENT AU TEXTE. Editer une
            // ligne apres coup PERIME la qualification : sinon la fiche
            // porterait sur un code que personne n'a jamais exerce.
            const double tresor_avant = Gt.finance.treasury_me;
            const double date_avant = Gt.clock.now_days();
            tv.banc_heures = 400.0;
            CHECK(tv.banc_essai_vol(mv), "code de vol : le banc qualifie un code compile");
            CHECK(tv.cert_vol.certified && tv.source_vol_certifiee(),
                  "code de vol : ... et rend une fiche VALIDE pour ce texte");
            CHECK(Gt.finance.treasury_me < tresor_avant,
                  "code de vol : le banc CONSOMME du budget [15.5]");
            tv.tick(0.016);   // la meme synchronisation qu'une frame de jeu
            CHECK(Gt.clock.now_days() > date_avant,
                  "code de vol : ... et RETARDE la fenetre");
            CHECK(tv.cert_vol.coverage > 0.0 && tv.cert_vol.coverage < 1.0,
                  "code de vol : le banc rassure sans garantir");
            std::printf("     code de vol : banc %.0f h -> couverture %.0f %%, %.1f M EUR, +%.1f j\n",
                        tv.banc_heures, tv.cert_vol.coverage * 100.0,
                        tv.cert_vol.budget_spent_me,
                        fen::code::bench_delay_days(tv.cert_vol));

            const std::string garde = tv.source_vol;
            tv.source_vol += "\n// une ligne de plus\n";
            CHECK(!tv.source_vol_certifiee(),
                  "code de vol : editer une ligne PERIME la fiche de qualification");
            CHECK(!tv.televerser_vol(), "code de vol : ... et interdit le televersement");
            tv.source_vol = garde;
            CHECK(tv.source_vol_certifiee(), "code de vol : le texte qualifie redevient valide tel quel");

            // (e) TELEVERSER, puis LAISSER LE CODE DECIDER. C'est [GDD 9.6] :
            // « le joueur n'est pas aux commandes, il a ecrit a l'avance la
            // logique qui decidera a sa place. »
            CHECK(tv.televerser_vol(), "code de vol : un code qualifie monte a bord");
            CHECK(tv.source_vol_a_bord(), "code de vol : ... et c est bien CE texte qui y est");
            CHECK(tv.executer_code_vol(mv), "code de vol : le logiciel embarque s execute en vol");

            const fen::code::DecisionsVol& Dec = tv.resultat_vol.decisions;
            const fen::code::EntreesVol E2 = tv.entrees_vol(mv);
            const double dv_cmd = std::sqrt(tv.tcm_commande[0] * tv.tcm_commande[0] +
                                            tv.tcm_commande[1] * tv.tcm_commande[1] +
                                            tv.tcm_commande[2] * tv.tcm_commande[2]);
            std::printf("     code de vol : decision = %s, |Dv| = %.2f m/s "
                        "(R %.1f  S %.1f  W %.1f)\n",
                        Dec.execute ? "EXECUTER" : "ne rien executer", dv_cmd,
                        tv.tcm_commande[0], tv.tcm_commande[1], tv.tcm_commande[2]);

            // LE GARDE-FOU DU JOUEUR GOUVERNE VRAIMENT. Le squelette refuse
            // d'agir au-dela de 12 km d'incertitude 3s : sa decision doit suivre
            // SA regle, pas une regle du moteur.
            if (E2.sigma3_m > 12000.0) {
              CHECK(!Dec.execute && Dec.replan_s > 0.0,
                    "code de vol : sur solution degradee, SON garde-fou reporte la manoeuvre");
              CHECK(!Dec.alertes.empty(), "code de vol : ... et il alerte");
            } else {
              CHECK(Dec.execute && dv_cmd > 0.0,
                    "code de vol : sur solution saine, SON code commande une correction");
              // (f) ET CE Dv FAIT VOLER LE VAISSEAU. Le bouclage complet du
              // mode Pro : son C++ -> un processus -> un Dv en RSW -> l'etat
              // VRAI du vol, erreur de Gates comprise.
              fen::mission::EtatVol e_code;
              e_code.valide = true; e_code.t_days = mv.vol_vrai_t_days;
              e_code.r = {mv.vol_vrai_r[0], mv.vol_vrai_r[1], mv.vol_vrai_r[2]};
              e_code.v = {mv.vol_vrai_v[0], mv.vol_vrai_v[1], mv.vol_vrai_v[2]};
              fen::mission::avancer_etat_vol(e_code, Gt.clock.now_days());
              const double avant = fen::mission::manque_reel_km(e_code, cible, t_arr);
              const double depense = tv.executer_tcm(
                  mv, fen::Vec3{tv.tcm_commande[0], tv.tcm_commande[1], tv.tcm_commande[2]});
              const double apres = mv.nav_miss_km;
              std::printf("     code de vol : manque %.0f km -> %.0f km "
                          "(%.1f m/s reellement depenses)\n", avant, apres, depense);
              CHECK(depense > 0.0, "code de vol : la manoeuvre de SON code est reellement depensee");
              CHECK(apres < 0.01 * avant,
                    "code de vol : le Dv calcule par SON code divise le manque au but par cent");
              CHECK(mv.tcm_faits == 1 && mv.tcm_dv_depense > 0.0,
                    "code de vol : ... et la mission garde trace de la correction");
              // L EQUIVALENCE DES DEUX MODES [GDD 2.2] : le code du mode PRO et
              // le graphe du mode NORMAL resolvent le MEME probleme. Ils ne
              // rendent pas le meme chiffre — le banc a fait passer huit jours,
              // et la correction se calcule depuis l'endroit ou l'on est — mais
              // ils sont du meme ordre. Si l'un valait le double de l'autre,
              // l'un des deux mentirait sur ce que fait l'autre.
              CHECK(depense > 0.5 * dv_solveur && depense < 2.0 * dv_solveur,
                    "code de vol : PRO et NORMAL commandent la meme manoeuvre, au meme ordre");
            }

            // (f bis) LE GARDE-FOU DU JOUEUR DECIDE, SUR LE MEME CODE. On rejoue
            // le logiciel televerse sur une solution DEGRADEE : il doit refuser
            // d'agir et replanifier, parce que SA regle le dit — pas parce que le
            // moteur l'en empeche. C'est [GDD 9.6] : la logique qu'il a ecrite
            // decide a sa place, y compris quand elle decide de ne rien faire.
            {
              fen::mission::Mission m_degrade = mv;
              m_degrade.nav_sigma_r = 20000.0;      // 3s = 60 km >> 12 km
              CHECK(tv.executer_code_vol(m_degrade),
                    "code de vol : le meme logiciel se rejoue sur une solution degradee");
              const fen::code::DecisionsVol& Dg = tv.resultat_vol.decisions;
              CHECK(!Dg.execute,
                    "code de vol : sur solution degradee, SON garde-fou refuse d agir");
              CHECK(Dg.replan_s == 48.0 * 3600.0,
                    "code de vol : ... il replanifie a 48 h, la valeur de SON code");
              CHECK(!Dg.alertes.empty(), "code de vol : ... et il alerte");
              CHECK(tv.tcm_commande[0] == 0.0 && tv.tcm_commande[1] == 0.0 &&
                    tv.tcm_commande[2] == 0.0,
                    "code de vol : ne rien executer EST une decision, et elle vide la commande");
              std::printf("     code de vol : solution degradee -> \"%s\", replan %.0f h\n",
                          Dg.alertes.empty() ? "-" : Dg.alertes[0].c_str(),
                          Dg.replan_s / 3600.0);
            }

            // (g) LE DOMAINE DE VALIDITE MORD [GDD 15.5]. Un code qualifie
            // jusqu'a 12 km d'incertitude n'est PAS qualifie a 40 : executer
            // hors domaine est un comportement non couvert, donc une cause
            // d'anomalie legitime — et le poste le dit au lieu de l'ignorer.
            CHECK(!tv.code_hors_domaine(mv) || E2.sigma3_m > tv.banc_borne_sigma3_m,
                  "code de vol : dans sa plage, le code est couvert");
            fen::mission::Mission m_flou = mv;
            m_flou.nav_sigma_r = tv.banc_borne_sigma3_m;   // 3s = 3x la borne
            CHECK(tv.code_hors_domaine(m_flou),
                  "code de vol : au-dela de la plage exercee, le comportement n est PAS couvert");
            // ... et un code qualifie pour la croisiere ne l'est pas pour une
            // rentree : « un code qualifie en orbite basse n'est pas qualifie
            // pour Mars » [GDD 15.5], vu depuis le profil de la mission.
            fen::mission::Mission m_autre = mv;
            m_autre.contract.family = "sat";
            CHECK(std::string(fen::app::Session::env_vol(m_autre)) !=
                  std::string(fen::app::Session::env_vol(mv)),
                  "code de vol : un autre profil de vol est un autre environnement");
            CHECK(tv.code_hors_domaine(m_autre),
                  "code de vol : ... et la fiche d un environnement ne couvre pas l autre");

            // (h) LE FEU VERT EMBARQUE CE QUI EST A BORD [GDD 15.5]. Un drapeau
            // que rien ne remplit est un piege (n°20b) : on verifie que la PORTE
            // du feu vert consigne le logiciel SUR LA MISSION — c'est de la que
            // `fly_mission` le lira, longtemps apres, y compris apres une
            // sauvegarde.
            {
              fen::mission::Mission m_go = mt;
              tv.tirer_navigation(m_go);
              CHECK(m_go.nav_evaluee, "feu vert : la navigation est tiree");
              CHECK(m_go.code_embarque,
                    "feu vert : le logiciel televerse part AVEC le vehicule");
              CHECK(!m_go.code_non_couvert,
                    "feu vert : ... et il est COUVERT — la poursuite tient le 3s dans la plage exercee");

              // Et sans rien a bord, rien ne part : le drapeau n'est pas un decor.
              const bool garde_bord = tv.code_a_bord;
              tv.code_a_bord = false;
              fen::mission::Mission m_nu = mt;
              tv.tirer_navigation(m_nu);
              CHECK(!m_nu.code_embarque,
                    "feu vert : sans code a bord, la mission part sans logiciel");
              CHECK(!m_nu.code_non_couvert,
                    "feu vert : ... et un vol sans code ne peut pas etre hors du domaine d un code absent");
              tv.code_a_bord = garde_bord;
            }

            // ---- 22. CE QUE COUTE DE NE RIEN EMBARQUER --------------------
            //                                  [GDD 7.4, 8.4, 9.3, 15.3, 15.5]
            // LE FILET EST RETIRE. La campagne de correction n'est plus
            // conduite automatiquement au feu vert : une correction est un
            // RENDEZ-VOUS DATE, et il n'a lieu que si quelqu'un est la pour le
            // commander. Avant, ne rien embarquer et ne rien piloter ne coutait
            // RIEN — le vol arrivait corrige tout seul, et « toutes les
            // manoeuvres sont calculees PAR LE JOUEUR » [GDD 7.4] restait un
            // voeu. Ce bloc prouve les issues d'un MEME vol selon qui a tenu
            // ses rendez-vous, toutes calculees par la meme loi de campagne et
            // sans qu'aucun malus soit applique nulle part.
            {
              const double garde_track = tv.mission_plan.program.tracking_days;
              const bool   garde_susp  = Gt.finance.suspended;
              tv.mission_plan.program.tracking_days = 14.0;  // il SAIT ou il est
              Gt.finance.suspended = false;                  // ... et il est a son poste

              // (a) AU FEU VERT, RIEN N EST ENCORE JOUE. Ces deux chiffres
              // portaient l'issue d'une campagne deja conduite ; ils portent
              // maintenant l'etat du vol tel qu'il vient d'etre injecte.
              fen::mission::Mission m0 = mt;
              tv.tirer_navigation(m0);
              m0.code_embarque = false;      // reference : rien a bord
              m0.code_non_couvert = false;
              m0.code_couverture = 0.0;
              CHECK(m0.nav_evaluee, "prix : la navigation est tiree au feu vert");
              CHECK(m0.nav_dv_required == 0.0,
                    "prix : au feu vert, PAS UN m/s n a encore ete depense en correction");
              CHECK(m0.tcm_faits == 0,
                    "prix : ... et aucun rendez-vous n a encore ete tenu");
              CHECK(m0.nav_miss_km > 1.0e6,
                    "prix : le manque d un vol que PERSONNE ne corrige se compte en millions de km");

              // (b) PERSONNE N Y VA. Le vol garde l'ecart qu'il traine. Ce
              // n'est pas une penalite : c'est le manque au but MESURE sur la
              // trajectoire reellement volee.
              fen::mission::Mission m_rien = m0;
              tv.resoudre_vol(m_rien);
              CHECK(m_rien.nav_dv_required == 0.0,
                    "prix : personne n a rien commande, donc rien n a ete depense");
              CHECK(m_rien.nav_miss_km > fen::mission::ARRIVEE_TOLERANCE_KM,
                    "prix : ... et un vol que personne ne conduit MANQUE sa cible [GDD 8.4]");
              CHECK(m_rien.vol_conduit_par == 0,
                    "prix : le debrief pourra DIRE que personne n a tenu les rendez-vous");

              // (c) LE LOGICIEL EMBARQUE TIENT LES RENDEZ-VOUS — sa raison
              // d'etre [GDD 9.6] : il agit a bord, a la date, sans le sol.
              fen::mission::Mission m_code = m0;
              m_code.code_embarque = true;
              m_code.code_non_couvert = false;
              m_code.code_couverture = 1.0;      // un banc qui a tout exerce
              tv.resoudre_vol(m_code);
              CHECK(m_code.nav_dv_required > 0.0,
                    "prix : le logiciel de bord a depense du Dv pour corriger");
              CHECK(m_code.nav_miss_km < 0.01 * m_rien.nav_miss_km,
                    "prix : ... et il divise le manque au but par cent [GDD 15.3]");
              CHECK(m_code.nav_miss_km < fen::mission::ARRIVEE_TOLERANCE_KM,
                    "prix : embarquer un logiciel COUVERT amene la mission au but");
              CHECK(m_code.vol_conduit_par == 2,
                    "prix : ... et le debrief credite LE LOGICIEL DE BORD");

              // (d) « LE BANC RASSURE SANS GARANTIR » [GDD 15.5]. A couverture
              // nulle le tirage tombe et le code ne tient RIEN : acheter des
              // heures d'essai cesse d'etre decoratif, et c'est le premier
              // endroit du moteur ou `code_success_prob` mord vraiment.
              fen::mission::Mission m_nul = m0;
              m_nul.code_embarque = true;
              m_nul.code_non_couvert = false;
              m_nul.code_couverture = 0.0;
              tv.resoudre_vol(m_nul);
              CHECK(m_nul.nav_miss_km == m_rien.nav_miss_km,
                    "prix : couverture nulle -> le code ne tient AUCUN rendez-vous");

              // (e) L ADJOINT, ET SEULEMENT EN L ABSENCE DU JOUEUR [GDD 9.3].
              // « ARES fonctionne normalement sous un adjoint, ni penalite ni
              // degradation punitive » : l'agence ne laisse pas tomber un vol
              // pendant que l'architecte est en route. Present, elle ne pilote
              // pas a sa place.
              fen::mission::Mission m_abs = m0;
              Gt.finance.suspended = true;
              tv.resoudre_vol(m_abs);
              Gt.finance.suspended = false;
              CHECK(m_abs.nav_miss_km < fen::mission::ARRIVEE_TOLERANCE_KM,
                    "prix : en ABSENCE, l adjoint conduit la campagne et amene le vol au but [9.3]");
              CHECK(m_rien.nav_miss_km > m_abs.nav_miss_km,
                    "prix : ... alors que le meme vol, joueur PRESENT et inactif, manque sa cible");
              CHECK(m_abs.vol_conduit_par == 3,
                    "prix : ... et le debrief nomme L ADJOINT, pas le joueur");

              // ... et un joueur qui a TOUT tenu de sa main est credite, LUI :
              // l'agent ne prend que ce qui reste, et il ne reste rien.
              fen::mission::Mission m_lui = m0;
              m_lui.code_embarque = true;      // il a POURTANT du code a bord
              m_lui.code_non_couvert = false;
              m_lui.code_couverture = 1.0;
              m_lui.tcm_faits = 2;
              {   // son etat vrai est deja au-dela du second rendez-vous
                fen::mission::EtatVol e_lui;
                e_lui.valide = true; e_lui.t_days = m0.vol_vrai_t_days;
                e_lui.r = {m0.vol_vrai_r[0], m0.vol_vrai_r[1], m0.vol_vrai_r[2]};
                e_lui.v = {m0.vol_vrai_v[0], m0.vol_vrai_v[1], m0.vol_vrai_v[2]};
                fen::mission::avancer_etat_vol(
                    e_lui, t_arr - fen::mission::TCM2_AVANT_ARRIVEE_J + 1.0);
                m_lui.vol_vrai_t_days = e_lui.t_days;
                for (int k = 0; k < 3; ++k) {
                  m_lui.vol_vrai_r[k] = e_lui.r[k];
                  m_lui.vol_vrai_v[k] = e_lui.v[k];
                }
              }
              tv.resoudre_vol(m_lui);
              CHECK(m_lui.vol_conduit_par == 1,
                    "prix : un joueur qui a tout tenu est credite, MEME avec du code a bord");
              CHECK(m_lui.nav_dv_required == 0.0,
                    "prix : ... et le logiciel de bord ne redepense rien par-dessus lui");

              // (f) UNE CAMPAGNE SE REPREND LA OU LE VOL EN EST : un
              // rendez-vous deja franchi n'est pas a prendre. C'est ce qui fait
              // que l'agent COMPLETE le joueur au lieu de le doubler.
              {
                fen::mission::EtatVol e_rep;
                e_rep.valide = true; e_rep.t_days = t_dep;
                e_rep.r = T.r_dep; e_rep.v = R1.v_dep_vraie;
                CHECK(fen::mission::avancer_etat_vol(
                          e_rep, t_dep + fen::mission::TCM1_APRES_INJECTION_J + 1.0),
                      "prix : l etat vrai s avance au-dela de TCM-1");
                fen::Vec3 rr = e_rep.r, vv = e_rep.v;
                double tt = e_rep.t_days;
                const auto C_rep =
                    fen::mission::nav_campagne_depuis(T, D, S14, S14, rr, vv, tt, 4242);
                CHECK(C_rep.ok && C_rep.dv_tcm1 == 0.0,
                      "prix : un rendez-vous deja franchi n est PAS repris");
                CHECK(C_rep.dv_tcm2 > 0.0,
                      "prix : ... mais celui qui reste est bien tenu");
                CHECK(tt > t_dep + fen::mission::TCM1_APRES_INJECTION_J,
                      "prix : la campagne rend l etat vrai la ou elle l a laisse");
              }

              // (g) L ETAT VRAI D UN VOL EN COURS SURVIT A LA SAUVEGARDE.
              // Il ne s'ecrivait pas : tant que l'issue etait decidee au feu
              // vert ca ne se voyait pas, mais depuis que les rendez-vous se
              // tiennent EN VOL, c'est cet etat qui la porte. Quitter au menu
              // (qui sauvegarde) effacerait sinon les corrections commandees a
              // la main — meme piege que les missions en vol non serialisees.
              {
                fen::game::GameState& Gs = *tv.jeu.ares.etat;
                const auto garde_missions = Gs.missions;
                fen::mission::Mission m_sav = m0;
                // Le contrat doit exister DANS LE CATALOGUE : une mission est
                // reappariee par son id au chargement, et le catalogue est
                // reconstruit par la graine. `mt` porte un id d'oracle.
                CHECK(!Gs.catalog.entries().empty(), "sauvegarde : le catalogue existe");
                m_sav.contract = Gs.catalog.entries().front().contract;
                m_sav.tcm_dv_depense = 37.5;    // il a corrige de sa main
                m_sav.tcm_faits = 1;
                Gs.missions.clear();
                Gs.missions.push_back(m_sav);
                fen::save::Writer w;
                Gs.save(w);
                fen::save::Reader rd(w.bytes().data(), w.bytes().size());
                // « Le catalogue est reconstruit par la graine AVANT le
                // chargement » : la copie modele exactement cette precondition.
                fen::game::GameState G2 = Gs;
                G2.missions.clear();
                const bool relu = G2.load(rd);
                CHECK(relu && G2.missions.size() == 1,
                      "sauvegarde : la mission en vol se relit");
                if (relu && G2.missions.size() == 1) {
                  const fen::mission::Mission& mr = G2.missions[0];
                  CHECK(mr.vol_vrai_valide, "sauvegarde : l etat VRAI du vol survit");
                  CHECK(mr.vol_vrai_t_days == m_sav.vol_vrai_t_days &&
                            mr.vol_vrai_r[0] == m_sav.vol_vrai_r[0] &&
                            mr.vol_vrai_v[2] == m_sav.vol_vrai_v[2],
                        "sauvegarde : ... au bit pres, position ET vitesse");
                  CHECK(mr.tcm_dv_depense == 37.5 && mr.tcm_faits == 1,
                        "sauvegarde : les corrections commandees a la main ne s effacent pas");
                  CHECK(mr.nav_sigma_r == m_sav.nav_sigma_r &&
                            mr.nav_connu_dv[1] == m_sav.nav_connu_dv[1],
                        "sauvegarde : ce que la poursuite a revele survit aussi");
                  CHECK(mr.code_couverture == m_sav.code_couverture,
                        "sauvegarde : la couverture figee au feu vert part avec le vol");
                }
                Gs.missions = garde_missions;
              }

              std::printf("     prix de l inaction : personne %.0f km | logiciel couvert %.0f km"
                          " (%.1f m/s) | adjoint absent %.0f km | banc a vide %.0f km\n",
                          m_rien.nav_miss_km, m_code.nav_miss_km, m_code.nav_dv_required,
                          m_abs.nav_miss_km, m_nul.nav_miss_km);

              // ---- 23. LE DELAI DE COMMUNICATION [GDD 8.3, 9.6] ----------
              // `comms_delay_s` existait depuis le premier jour et PERSONNE ne
              // l'appelait ; [GDD 8.3] liste pourtant « delai de communication »
              // parmi ce que le plan terminal doit afficher. Ce bloc le branche
              // et MESURE ce qu'il coute — sans le gonfler : sur une croisiere
              // il est petit devant le bras de levier, et c'est un resultat.
              {
                tv.trace_vol = T;   // la vue lit la trace publiee
                const fen::mission::VueNavigation VD = tv.vue_vol(m0);
                CHECK(VD.ok, "delai : la vue de navigation se calcule");
                // (a) IL EST REEL ET IL EST GRAND. Terre-vaisseau se compte en
                // dizaines de millions de km des les premieres semaines.
                CHECK(VD.delai_com_s > 1.0,
                      "delai : le plan terminal porte enfin un delai de communication");
                CHECK(VD.delai_com_s < 25.0 * 60.0,
                      "delai : ... et il reste sous les 25 min-lumiere du systeme interne");
                // (b) IL EST CELUI DE LA DISTANCE, pas un forfait : c'est
                // `comms_delay_s` de la distance Terre-vaisseau, et rien d'autre.
                const fen::Vec3 r_terre = tv.jeu.eph.state(
                    fen::ephem::Body::EarthBary, fen::ephem::Body::Sun,
                    fen::Epoch{tv.jeu.epoch_courant()}).r;
                CHECK(std::fabs(VD.delai_com_s -
                                fen::mission::comms_delay_s(
                                    fen::norm(VD.r_estime - r_terre))) < 1e-9,
                      "delai : c est comms_delay_s de la distance, une seule source");
                // (c) CE QU IL COMMANDE ARRIVE PLUS TARD. La manoeuvre s'applique
                // a `now + d/c` : l'etat vrai avance donc AU-DELA de l'instant de
                // la commande. C'est la que le vol autonome prend son sens.
                fen::mission::Mission m_d = m0;
                const double t_avant = m_d.vol_vrai_t_days;
                fen::Vec3 dv_d{};
                CHECK(fen::mission::solveur_correction(VD, dv_d),
                      "delai : le solveur rend un Dv a commander");
                const double depense = tv.executer_tcm(m_d, dv_d);
                CHECK(depense > 0.0, "delai : la correction est bien executee");
                const double now_j = Gt.clock.now_days();
                CHECK(m_d.vol_vrai_t_days > now_j,
                      "delai : la manoeuvre s applique APRES l instant de la commande");
                CHECK(std::fabs((m_d.vol_vrai_t_days - now_j) * cst::DAY - VD.delai_com_s) < 1e-6,
                      "delai : ... exactement d/c plus tard, pas un forfait");
                CHECK(m_d.vol_vrai_t_days > t_avant,
                      "delai : l etat vrai a bien avance");
                // (d) CE QUE LE DELAI COUTE, MESURE et non suppose. Meme Dv,
                // meme graine, meme etat de depart : applique a l'instant de la
                // commande, puis d/c plus tard. L'ecart est l'effet PUR du delai.
                auto miss_si_retard = [&](double retard_s) {
                  fen::mission::EtatVol e;
                  e.valide = true; e.t_days = m0.vol_vrai_t_days;
                  e.r = {m0.vol_vrai_r[0], m0.vol_vrai_r[1], m0.vol_vrai_r[2]};
                  e.v = {m0.vol_vrai_v[0], m0.vol_vrai_v[1], m0.vol_vrai_v[2]};
                  fen::mission::avancer_etat_vol(e, now_j + retard_s / cst::DAY);
                  fen::mission::executer_correction(e, VD, dv_d, 4242);
                  return fen::mission::manque_reel_km(e, cible, t_arr);
                };
                const double miss_immediat = miss_si_retard(0.0);
                const double miss_retarde  = miss_si_retard(VD.delai_com_s);
                CHECK(miss_retarde != miss_immediat,
                      "delai : le retard CHANGE le vol — ce n est pas un affichage");
                // ET ON DECLARE SA TAILLE [GDD 6.8] : en croisiere, d/c est
                // minuscule devant le bras de levier restant, donc son cout en
                // manque au but l'est aussi. Le delai n'est PAS ce qui rend le
                // vol autonome necessaire en croisiere — il le devient aux
                // phases courtes, ou d/c depasse la duree de la manoeuvre.
                CHECK(std::fabs(miss_retarde - miss_immediat) <
                          fen::mission::ARRIVEE_TOLERANCE_KM,
                      "delai : ... mais en croisiere son cout reste sous la tolerance d arrivee");
                // (e) LE VAISSEAU S ELOIGNE, DONC LE DELAI CROIT. A TCM-2 il
                // vaut des ordres de grandeur de plus qu a TCM-1 : le meme
                // modele, deux moments, deux realites operationnelles.
                const double t_tcm2 = t_arr - fen::mission::TCM2_AVANT_ARRIVEE_J;
                const auto K2 = fen::astro::kepler_propagate(
                    T.r_dep, T.v_dep, (t_tcm2 - t_dep) * cst::DAY, cst::MU_SUN);
                const fen::Vec3 r_terre2 = tv.jeu.eph.state(
                    fen::ephem::Body::EarthBary, fen::ephem::Body::Sun,
                    fen::Epoch{now_tdb + (t_tcm2 - now_days) * cst::DAY}).r;
                const double delai2 =
                    fen::mission::comms_delay_s(fen::norm(K2.r - r_terre2));
                CHECK(K2.converged && delai2 > 5.0 * VD.delai_com_s,
                      "delai : a TCM-2 le vaisseau est bien plus loin, le delai bien plus long");
                std::printf("     delai de communication : TCM-1 %.1f min -> TCM-2 %.1f min ;"
                            " cout du retard sur le manque au but : %.0f km\n",
                            VD.delai_com_s / 60.0, delai2 / 60.0,
                            std::fabs(miss_retarde - miss_immediat));

                // ---- 24. LA BOUCLE SOL [GDD 9.6, 15.3] -------------------
                // « Le logiciel de vol embarque prepare l'autonomie QUAND LE
                // SOL EST HORS DE PORTEE. » Ce bloc dit QUAND, en comparant
                // deux grandeurs physiques : l'aller-retour de la lumiere et la
                // duree PROPRE de la manoeuvre. Aucun seuil de confort.
                {
                  const double d_edl = fen::mission::phase_duration_s(
                      fen::mission::FlightPhase::Edl);
                  CHECK(std::fabs(d_edl - 7.0 * 60.0) < 1e-9,
                        "boucle : la duree de l EDL est celle de MSL, 7 min");

                  // (a) L EDL MARTIENNE : la boucle NE SE FERME PAS. C'est le
                  // fait historique — le sol de MSL a regarde « seven minutes
                  // of terror » sans pouvoir rien faire.
                  const double d_mars = fen::norm(K2.r - r_terre2);
                  CHECK(!fen::mission::ground_loop_closes(d_mars, d_edl),
                        "boucle : a distance martienne, une descente de 7 min ECHAPPE au sol");
                  CHECK(fen::mission::comms_roundtrip_s(d_mars) > d_edl,
                        "boucle : ... parce que l aller-retour depasse la duree de la descente");

                  // (b) L AMARRAGE EN ORBITE BASSE : la boucle SE FERME. Le sol
                  // est dans la boucle, et c'est ainsi que se conduisent les
                  // operations LEO. Meme predicat, autre distance.
                  const double d_leo = 418.0e3;
                  CHECK(fen::mission::ground_loop_closes(d_leo, d_edl),
                        "boucle : en orbite basse, le sol est LARGEMENT dans la boucle");

                  // (c) UNE CROISIERE NE SE « CONDUIT » PAS : pas de duree
                  // opposable, donc pas de boucle a fermer. C'est pourquoi une
                  // TCM, elle, se commande tres bien depuis le sol — elle est
                  // PREPAREE a l'avance, pas subie en temps reel.
                  CHECK(fen::mission::phase_duration_s(
                            fen::mission::FlightPhase::TransferCruise) == 0.0 &&
                        !fen::mission::ground_loop_closes(d_mars, 0.0),
                        "boucle : une phase sans duree opposable n a pas de boucle a fermer");

                  // (d) LA SESSION LE PUBLIE, avec ses deux chiffres — sans
                  // quoi l'ecran devrait les recalculer, donc les redefinir.
                  const fen::app::Session::BoucleSol B = tv.boucle_sol(m0);
                  CHECK(B.valide, "boucle : la session publie l etat de la boucle sol");
                  CHECK(std::fabs(B.aller_retour_s - 2.0 * VD.delai_com_s) < 1e-9,
                        "boucle : l aller-retour publie est DEUX fois le delai de la vue");
                  std::printf("     boucle sol : EDL a Mars %.0f min aller-retour contre "
                              "%.0f min de descente -> OUVERTE ; LEO %.2f s -> FERMEE\n",
                              fen::mission::comms_roundtrip_s(d_mars) / 60.0, d_edl / 60.0,
                              fen::mission::comms_roundtrip_s(d_leo));
                }

                // ---- 25. LE RYTHME DE MESURE [GDD 8.6] --------------------
                // « Le joueur choisit son rythme de mesure ; trop rare laisse
                // deriver, trop frequent coute des ressources et du temps. » Il
                // n'y avait rien a choisir : la poursuite s'achetait UNE FOIS a
                // la conception et la connaissance restait figee tout le vol.
                {
                  const double t_inj = T.nodes[0].t_days;
                  const double garde_prog = tv.mission_plan.program.tracking_days;

                  // (a) ON NE MESURE PAS LE FUTUR. Rien ne bornait l'arc : un
                  // `tracking_days` genereux donnait AU FEU VERT une solution que
                  // seules deux semaines d'ecoute peuvent produire.
                  tv.mission_plan.program.tracking_days = 300.0;
                  fen::mission::Mission m_arc = m0;
                  m_arc.poursuite_jours = 0.0;
                  CHECK(tv.arc_poursuite_disponible(m_arc, t_inj, t_inj) == 0.0,
                        "rythme : a l injection l arc est NUL, quelle que soit la somme engagee");
                  CHECK(std::fabs(tv.arc_poursuite_disponible(m_arc, t_inj, t_inj + 10.0)
                                  - 10.0) < 1e-12,
                        "rythme : apres 10 jours on dispose de 10 jours d arc, pas de 300");
                  // ... et acheter n'accelere rien : le temps ecoule commande.
                  m_arc.poursuite_jours = 500.0;
                  CHECK(std::fabs(tv.arc_poursuite_disponible(m_arc, t_inj, t_inj + 10.0)
                                  - 10.0) < 1e-12,
                        "rythme : acheter plus n avance pas les horloges");

                  // (b) TROP RARE LAISSE DERIVER. A budget d'ecoute borne, la
                  // solution est moins bonne — c'est la phrase du GDD, mesuree.
                  tv.mission_plan.program.tracking_days = 2.0;
                  const fen::app::Session::ContexteVol c_peu =
                      tv.contexte_vol(m0, t_inj + 14.0);
                  tv.mission_plan.program.tracking_days = 14.0;
                  const fen::app::Session::ContexteVol c_beaucoup =
                      tv.contexte_vol(m0, t_inj + 14.0);
                  CHECK(c_peu.ok && c_beaucoup.ok, "rythme : les deux solutions se calculent");
                  CHECK(std::fabs(c_peu.arc_jours - 2.0) < 1e-12 &&
                            std::fabs(c_beaucoup.arc_jours - 14.0) < 1e-12,
                        "rythme : l arc exploite est bien celui qu on a paye, borne par l ecoule");
                  CHECK(c_beaucoup.sol.n_mesures > c_peu.sol.n_mesures,
                        "rythme : ecouter plus longtemps donne PLUS de mesures");
                  CHECK(c_beaucoup.sol.sigma_v < c_peu.sol.sigma_v,
                        "rythme : ... et une solution MEILLEURE [GDD 8.6]");

                  // (c) TROP FREQUENT COUTE. Le tarif derive de la passe de 8 h
                  // deja dans le modele et du fee d'antenne du DSN — avant, on
                  // pouvait acheter cent jours d ecoute pour zero.
                  CHECK(fen::mission::cout_poursuite_me(0.0) == 0.0,
                        "rythme : ne rien ecouter ne coute rien");
                  const double c14 = fen::mission::cout_poursuite_me(14.0);
                  CHECK(c14 > 0.0 && std::fabs(fen::mission::cout_poursuite_me(28.0) - 2.0 * c14)
                                         < 1e-12,
                        "rythme : le cout est PROPORTIONNEL aux heures d antenne ouvertes");
                  fen::mission::Mission m_ach = m0;
                  auto& F = Gt.finance;
                  const double treso_avant = F.treasury_me;
                  CHECK(tv.acheter_poursuite(m_ach, 14.0),
                        "rythme : on achete 14 jours d ecoute supplementaires");
                  CHECK(std::fabs((treso_avant - F.treasury_me) - c14) < 1e-9,
                        "rythme : ... et la tresorerie les paie au tarif d antenne");
                  CHECK(std::fabs(m_ach.poursuite_jours - 14.0) < 1e-12,
                        "rythme : l achat s ajoute a l arc autorise");
                  F.treasury_me = 0.0;
                  fen::mission::Mission m_pauvre = m0;
                  CHECK(!tv.acheter_poursuite(m_pauvre, 14.0),
                        "rythme : sans tresorerie, pas d ecoute — on ne creuse pas la reserve");
                  F.treasury_me = treso_avant;

                  std::printf("     rythme de mesure : 2 j -> %d mesures, sigma_v %.3f m/s | "
                              "14 j -> %d mesures, sigma_v %.3f m/s | 14 j d ecoute = %.3f M EUR\n",
                              c_peu.sol.n_mesures, c_peu.sol.sigma_v,
                              c_beaucoup.sol.n_mesures, c_beaucoup.sol.sigma_v, c14);
                  tv.mission_plan.program.tracking_days = garde_prog;
                }

                // ---- 26. LE CARNET [GDD 15.4] et LA BASCULE [GDD 2.3] -----
                // `career::Notebook` etait serialise et transmis au successeur
                // depuis le premier jour... et VIDE : personne n'y ecrivait,
                // personne ne le lisait. Encore un modele que rien ne consomme.
                // Et la bascule Normal -> Pro, que GameState.hpp declarait en
                // commentaire, n'existait nulle part.
                {
                  fen::game::GameState& Gc = *tv.jeu.ares.etat;

                  // (a) LES MAN PAGES SONT UNE LECTURE DE L'API, pas une copie
                  // [GDD 2.2, 15.4]. Chaque primitive doit y figurer AVEC la
                  // fonction qu'elle est — sinon l'equivalence stricte n'est
                  // qu'une promesse.
                  const auto Man = fen::career::man_pages_api(10.0);
                  bool toutes = true;
                  for (const auto& d : fen::mission::noeuds_disponibles())
                    if (Man.body.find(d.nom) == std::string::npos ||
                        Man.body.find(d.appel) == std::string::npos) toutes = false;
                  CHECK(toutes,
                        "carnet : la man page NOMME chaque primitive ET sa fonction d API");
                  CHECK(!fen::mission::noeuds_disponibles().empty() && !Man.body.empty(),
                        "carnet : ... et elle n est pas vide");

                  // (b) LE DEBRIEF EST UN FAIT, pas un commentaire. On y
                  // retrouve QUI a conduit les corrections — ce que la passe
                  // precedente a rendu decisif.
                  fen::mission::Mission m_c = m0;
                  m_c.flight_success = true;
                  m_c.vol_conduit_par = 2;      // le logiciel de bord
                  m_c.nav_miss_km = 19.0;
                  m_c.nav_dv_required = 48.7;
                  const auto Deb = fen::career::debrief_mission(m_c, 600.0, 42.0);
                  CHECK(Deb.mission_ref == m_c.contract.id,
                        "carnet : l entree est rattachee a SA mission (mission_ref)");
                  CHECK(Deb.body.find("logiciel de bord") != std::string::npos,
                        "carnet : elle dit QUI a conduit les corrections");
                  CHECK(Deb.body.find("19") != std::string::npos &&
                            Deb.body.find("48.7") != std::string::npos,
                        "carnet : ... avec les chiffres du vol, pas une appreciation");

                  // (c) LA BASCULE EST UNIDIRECTIONNELLE [GDD 2.3], et elle
                  // COUTE le graphe : « cette perte est intentionnelle ».
                  const auto garde_mode = tv.jeu.agence.mode;
                  const std::size_t avant = Gc.notebook.entries.size();
                  tv.jeu.agence.mode = fen::app::ModeAide::Normal;
                  tv.graphe = fen::mission::graphe_correction_reference();
                  CHECK(!tv.graphe.empty(), "carnet : on part d un graphe assemble");
                  CHECK(tv.basculer_en_pro(), "bascule : Normal -> Pro est possible");
                  CHECK(tv.jeu.agence.mode == fen::app::ModeAide::Pro,
                        "bascule : le mode a bien change");
                  CHECK(tv.graphe.empty(),
                        "bascule : le graphe est PERDU — la perte est intentionnelle [GDD 2.3]");
                  CHECK(Gc.notebook.entries.size() == avant + 2,
                        "bascule : deux pages ecrites — l archive et les man pages");
                  CHECK(!tv.basculer_en_pro(),
                        "bascule : elle est UNIDIRECTIONNELLE — on ne revient jamais");

                  // (d) L ARCHIVE EST CONSULTABLE ET INEXECUTABLE. Pas par
                  // interdiction : c'est du TEXTE, et aucun chemin ne
                  // reconstruit un graphe depuis une chaine.
                  const auto& Arch = Gc.notebook.entries[avant];
                  CHECK(Arch.title.find("archive") != std::string::npos,
                        "bascule : l entree se nomme comme une archive");
                  for (auto t : fen::mission::graphe_correction_reference())
                    CHECK(Arch.body.find(fen::mission::noeud_def(t).nom) != std::string::npos,
                          "bascule : chaque noeud archive est LISIBLE dans le carnet");

                  // (e) LE CARNET SURVIT A LA SAUVEGARDE — c'est LE bien
                  // transmis en passation [GDD 3.5], il ne peut pas s'evaporer.
                  {
                    fen::save::Writer w2;
                    Gc.save(w2);
                    fen::save::Reader r2(w2.bytes().data(), w2.bytes().size());
                    fen::game::GameState G3 = Gc;
                    G3.notebook.entries.clear();
                    CHECK(G3.load(r2), "carnet : la sauvegarde se relit");
                    CHECK(G3.notebook.entries.size() == Gc.notebook.entries.size(),
                          "carnet : toutes les pages survivent");
                    if (!G3.notebook.entries.empty())
                      CHECK(G3.notebook.entries.back().body ==
                                Gc.notebook.entries.back().body,
                            "carnet : ... texte compris, au caractere pres");
                  }
                  std::printf("     carnet : %d pages ecrites (man pages %d primitives, "
                              "archive de graphe, debriefs)\n",
                              (int)Gc.notebook.entries.size(),
                              (int)fen::mission::noeuds_disponibles().size());
                  tv.jeu.agence.mode = garde_mode;
                }
              }

              tv.mission_plan.program.tracking_days = garde_track;
              Gt.finance.suspended = garde_susp;
            }
          }
        }
      }
    }
  }

  // ═══════════════════════════════════════════════════════════════════════════
  // 27. LA MISSION VÉCUE [GDD 9, décision 18] — le joueur EMBARQUE
  // ═══════════════════════════════════════════════════════════════════════════
  // `mission/Crew.hpp` était complet et sous oracle depuis toujours, et
  // AUCUN chemin n'y menait : `try_embark` n'était appelé nulle part,
  // `VitalState` n'était jamais instancié, `AgencyFinance::suspended` n'était
  // posé que par les tests eux-mêmes, et `career::journal_absence` n'avait pas
  // d'appelant. Un chapitre entier du GDD sans porte d'entrée — la famille de
  // `Mission::phase`, `show_moons`, `ModeAide` et du carnet.
  {
    Session s;
    s.nouvelle_partie("Oracle Vecu", ModeAide::Normal);
    s.tick(0.0);                      // `AresLayer::assurer` cree l etat ARES
    CHECK(s.jeu.ares.initialisee(), "vecu : la couche ARES est prete");
    fen::game::GameState& G = *s.jeu.ares.etat;

    // ---- (a) LA PORTE REFUSE, ET ELLE DIT POURQUOI ---------------------------
    CHECK(!s.peut_embarquer().possible && !s.embarquer(),
          "vecu : sans mission pilotee, on n embarque pas");
    CHECK(!s.dernier_refus_embarquement.empty(),
          "vecu : ... et le refus est MOTIVE, affichable au poste");

    // Une mission habitée, en vol : la seule configuration embarquable.
    fen::mission::Mission mv;
    mv.contract.id = "VECU-1";
    mv.contract.title = "Rotation d equipage";
    mv.contract.family = "habite";        // séjour de 180 j = incrément ISS
    mv.contract.crewed = true;
    // LES TERMES DU CONTRAT PORTENT L OBJECTIF, dont l EFFECTIF [GDD 3.1] : une
    // mission fabriquee a la main doit traverser les memes portes qu une mission
    // jouee, sinon la fixture teste autre chose que le jeu.
    mv.contract.terms = fen::mission::contract_terms_for_family(mv.contract.family);
    // ON EMBARQUE AVANT LE FEU VERT [GDD 9.2, 4.1] : la decision se prend a la
    // planification, pas en route. Defaut trouve EN CAPTURE — la premiere
    // version laissait rejoindre un vaisseau deja parti vers Mars.
    mv.state = fen::mission::MissionState::Qualification;
    G.missions.push_back(mv);
    s.piloter_premiere_mission();
    CHECK(s.mission_courante() != nullptr, "vecu : la mission habitee est pilotee");

    // Un vol DEJA PARTI ne se rejoint pas.
    {
      fen::mission::Mission* p = s.mission_courante();
      const auto garde = p->state;
      p->state = fen::mission::MissionState::Launched;
      CHECK(!s.peut_embarquer().possible,
            "vecu : on ne rejoint pas un vaisseau deja en route [GDD 9.2]");
      p->state = fen::mission::MissionState::Received;
      CHECK(!s.peut_embarquer().possible,
            "vecu : ni une mission dont la conception n a pas commence [GDD 4.1]");
      p->state = garde;
    }

    // La confiance FILTRE l'habité, et c'est le MÊME seuil que l'acceptation
    // d'un contrat habité [GDD 13.4] — un seul barème, pas deux.
    const double conf_garde = G.career.confidence_ares;
    G.career.confidence_ares = 45.0;
    CHECK(!s.peut_embarquer().possible,
          "vecu : confiance < 60 -> missions habitees suspendues [GDD 13.4]");
    G.career.confidence_ares = conf_garde;

    // ---- (b) UNE MISSION LONGUE EXIGE LA FIN DE CARRIÈRE [GDD 9.2] ----------
    // « Le personnage ne quitte ARES que lorsqu'il n'a plus de carriere a
    // construire. » Un incrément ISS (180 j) n'est pas une mission longue ; un
    // aller-retour martien (780 j) l'est.
    CHECK(!fen::mission::mission_longue("habite", 0.0),
          "vecu : un increment ISS n est pas une mission LONGUE");
    CHECK(fen::mission::mission_longue("mars_habite", 779.9),
          "vecu : un aller-retour martien en est une");
    // Un rang de départ (Stagiaire) embarque donc sur la rotation LEO...
    CHECK(!fen::career::terminal_rank(G.career.rank),
          "vecu : la partie ne commence pas au rang terminal");
    CHECK(s.peut_embarquer().possible,
          "vecu : ... et une mission COURTE lui reste ouverte");
    // ...mais pas sur Mars : rang ET maturite manquent tous les deux.
    fen::mission::Mission* pm = s.mission_courante();
    const std::string fam_garde = pm->contract.family;
    pm->contract.family = "mars_habite";
    CHECK(!s.peut_embarquer().possible,
          "vecu : mission longue refusee hors fin de carriere [GDD 9.2]");
    G.career.rank = fen::career::Rank::Directeur;
    CHECK(!s.peut_embarquer().possible &&
              s.peut_embarquer().raison.find("long sejour") != std::string::npos,
          "vecu : le rang ne suffit pas — il faut la MATURITE [GDD 5.10, 9.2]");
    pm->contract.family = fam_garde;
    G.career.rank = fen::career::Rank::Stagiaire;

    // ---- (c) L'EMBARQUEMENT ARME LES SOUTES ET GÈLE L'AGENCE [GDD 9.3] -----
    const double conf_depart = G.career.confidence_ares;
    CHECK(!G.finance.suspended, "vecu : avant l embarquement, l agence est exposee");
    CHECK(s.embarquer(), "vecu : l embarquement reussit");
    CHECK(G.lived.active && G.lived.mission_id == "VECU-1",
          "vecu : la mission vecue est NOMMEE, pas un indice de tableau");
    CHECK(G.lived.n_crew == 7, "vecu : 7 personnes a bord (increment ISS)");
    CHECK(G.lived.vitals.o2_kg > 0.0 && G.lived.vitals.water_kg > 0.0 &&
              G.lived.vitals.food_kg > 0.0 && G.lived.vitals.co2_scrub_capacity_kg > 0.0,
          "vecu : les quatre soutes sont armees, CO2 compris");
    CHECK(G.finance.suspended,
          "vecu : la chaine de fin de partie financiere est SUSPENDUE [GDD 9.3]");
    const double jours_bord = G.lived.days_left();
    std::printf("     vecu : %d a bord, %.0f jours d autonomie provisionnee\n",
                G.lived.n_crew, jours_bord);
    CHECK(jours_bord > 180.0,
          "vecu : l autonomie couvre le sejour, marge comprise");

    // UNE SEULE À LA FOIS [GDD 9.2].
    CHECK(!s.embarquer() &&
              s.dernier_refus_embarquement.find("une seule") != std::string::npos,
          "vecu : on n embarque pas deux fois [GDD 9.2]");

    // LE FEU VERT : la mission decolle avec son Architecte a bord. (On pose
    // l etat de vol a la main plutot que de derouler les gates — la boucle de
    // mission a ses propres oracles ; ce qui se verifie ici est la VIE A BORD.)
    s.mission_courante()->state = fen::mission::MissionState::Launched;
    s.mission_courante()->state_entered_days = G.clock.now_days();

    // ---- (d) LA CONFIANCE EST GELÉE PENDANT L'ABSENCE [GDD 9.3] ------------
    // « Aucune perte de credibilite ne peut survenir en l'absence du joueur. »
    // L'adjoint conduit de vraies missions : on simule une anomalie qui, hors
    // absence, couterait de la confiance.
    // ---- (d) UNE MISSION VÉCUE A BESOIN QUE LE TEMPS PASSE [GDD 14.3] ------
    // Au feu vert, le vol est en ASCENSION : le plafond de cadence RAMÈNE le
    // monde au temps réel, et il a raison de le faire. Un séjour de six mois ne
    // se vit donc pas d'un bloc — il se vit par phases, et c'est exactement ce
    // que dit [GDD 14.3] (« certaines tâches se gèrent en temps réel ; le reste
    // peut être accéléré »).
    CHECK(!s.jeu.regler_cadence(fen::game::TimeRate::Month),
          "vecu : au decollage, la phase critique REFUSE le mois/s [GDD 14.3]");
    CHECK(s.jeu.cadence == fen::game::TimeRate::Realtime,
          "vecu : ... et la borne est le temps reel, jamais la pause");
    // Une fois en exploitation, le monde peut de nouveau filer.
    s.mission_courante()->state_entered_days = G.clock.now_days() - 10.0;
    s.tick(0.0);                                   // la phase se re-derive
    CHECK(s.jeu.regler_cadence(fen::game::TimeRate::Month),
          "vecu : hors phase critique, l acceleration redevient permise");

    // ---- (d bis) LA CONFIANCE EST GELÉE PENDANT L'ABSENCE [GDD 9.3] --------
    // « Aucune perte de credibilite ne peut survenir en l'absence du joueur. »
    // L'adjoint conduit de vraies missions : on simule l'echec qui, hors
    // absence, couterait de la confiance.
    G.career.confidence_ares -= 25.0;
    s.tick(0.5);
    CHECK(s.jeu.agence.mois > 0.0, "vecu : le temps a bien coule");
    CHECK(std::fabs(G.career.confidence_ares - conf_depart) < 1e-9,
          "vecu : la confiance est REPOSEE a sa valeur de depart [GDD 9.3]");

    // ---- (e) LES VIVRES SE CONSOMMENT AVEC LE TEMPS, PAS AVEC LES FRAMES ----
    // Meme doctrine que le calendrier : le sous-pas fixe, donc 4 frames et 100
    // frames donnent le MEME etat. Un equipage qu on affame en achetant un
    // meilleur GPU serait un bug de rendu deguise en gameplay.
    // ═══ LE MEME ETAT, DEUX DECOUPAGES — ET C EST TOUT CE QUI CHANGE ═══
    // ⚠ CET ORACLE COMPARAIT DEUX PARTIES AUX PASSES DIFFERENTS (« on compare des
    // DELTAS et non des absolus »), et ca ne prouve plus rien depuis que les
    // AVARIES comptent : chaque partie a les siennes, a des dates differentes, et
    // leurs deltas DOIVENT differer. Mesure du 2026-07-29, apres le recalibrage du
    // taux de support-vie sur l ISS : une avarie de chaque cote, et 21,6 JOURS
    // d autonomie d ecart. L oracle disait « le sous-pas fuit » alors qu il
    // comparait deux vaisseaux differents.
    // On sauvegarde donc l ETAT EXACT, on le rejoue deux fois, et seul le
    // decoupage en frames change. C est enfin ce que le nom de l oracle annonce.
    // DEUX PARTIES FRAICHES ET IDENTIQUES : meme nom, donc meme graine, donc meme
    // etat — la seule difference est le decoupage en frames. (Recharger `s` en
    // place ne marche pas : `Session` garde l index de sa mission courante, que
    // `GameState::load` remplace sous ses pieds.)
    // 0,2 s PAR FRAME, et c'est deliberé : au-dela de 0,25 s le modele BORNE la
    // frame (« un gel de shaders ne coute pas des mois »), si bien que 4 frames
    // de 0,5 s ne valent pas 2 s de jeu mais 1 s. Le garde-fou est correct — il
    // faut simplement rester sous son seuil pour comparer des durees egales.
    auto conso_en = [&mv](int n_frames, double dt) {
      Session sc;
      sc.nouvelle_partie("Oracle Cadence", ModeAide::Normal);
      sc.tick(0.0);
      fen::game::GameState& Gc = *sc.jeu.ares.etat;
      Gc.missions.push_back(mv);
      sc.piloter_premiere_mission();
      sc.embarquer();
      sc.mission_courante()->state = fen::mission::MissionState::Launched;
      sc.mission_courante()->state_entered_days = Gc.clock.now_days() - 10.0;
      sc.tick(0.0);
      sc.jeu.regler_cadence(fen::game::TimeRate::Month);
      const double avant = Gc.lived.days_left();
      for (int i = 0; i < n_frames; ++i) sc.tick(dt);
      return avant - Gc.lived.days_left();
    };
    const double reste_avant = G.lived.days_left();
    const double conso_40 = conso_en(40, 0.05);      // 2 s reelles en 40 frames
    const double conso_10 = conso_en(10, 0.20);      // 2 s reelles en 10 frames
    std::printf("     vecu : %.2f j d autonomie provisionnee ; 40 frames -> %.6f j "
                "consommes | 10 frames -> %.6f j (ecart %.2e)\n",
                reste_avant, conso_40, conso_10, std::fabs(conso_40 - conso_10));
    CHECK(conso_40 > 0.0, "vecu : le temps qui coule CONSOMME les vivres [GDD 9.1]");
    CHECK(std::fabs(conso_40 - conso_10) < 1e-9,
          "vecu : 10 frames ou 40 frames consomment AUTANT (sous-pas fixe)");

    // ---- (f) L'ABSENCE SURVIT À UNE SAUVEGARDE ------------------------------
    // Perdre `lived` au rechargement remettrait le joueur au sol au milieu de sa
    // croisiere ET degelerait une chaine que [GDD 9.3] promet suspendue.
    {
      fen::save::Writer w;
      G.save(w);
      fen::save::Reader r(w.bytes().data(), w.bytes().size());
      // MEME precondition que l oracle de sauvegarde de vol ci-dessus : le
      // catalogue est reconstruit par la graine AVANT le chargement. Charger
      // dans un GameState au catalogue vide n est pas un cas a supporter — c est
      // un etat que le jeu ne produit jamais (`AresLayer::assurer` seme toujours
      // avant de lire un fichier).
      fen::game::GameState G3 = G;
      G3.lived = {};
      G3.finance.suspended = false;
      CHECK(G3.load(r), "vecu : la sauvegarde d une partie embarquee se relit");
      CHECK(G3.lived.active && G3.lived.mission_id == G.lived.mission_id,
            "vecu : on est TOUJOURS a bord apres rechargement");
      CHECK(std::fabs(G3.lived.days_left() - G.lived.days_left()) < 1e-9,
            "vecu : ... avec exactement les memes vivres");
      CHECK(G3.finance.suspended,
            "vecu : ... et l agence est toujours protegee [GDD 9.3]");
    }

    // ---- (g) LE RETOUR DÉGÈLE ET ÉCRIT LE CARNET [GDD 9.3, 15.4] -----------
    const std::size_t pages_avant = G.notebook.entries.size();
    CHECK(s.debarquer(), "vecu : le debarquement reussit");
    CHECK(!G.lived.active, "vecu : on n est plus a bord");
    CHECK(!G.finance.suspended,
          "vecu : l agence redevient exposee — l Architecte a repris son poste");
    CHECK(G.notebook.entries.size() == pages_avant + 1,
          "vecu : le retour ECRIT la reconstitution d absence [GDD 9.3, 15.4]");
    CHECK(G.notebook.entries.back().title == "Reconstitution d'absence",
          "vecu : ... et c est bien cette page-la");
    CHECK(!s.debarquer(), "vecu : on ne debarque pas deux fois");

    // ---- (h) LES RÉSERVES ÉPUISÉES TUENT [GDD 9.4, 10.3 niveau 5] ----------
    // « Voire en ECHEC si les reserves ne suffisent pas. » Le joueur est a bord :
    // la gravite MONTE d un palier par exposition humaine [GDD 10.3], elle n est
    // pas decretee. Un Critique + exposition == Catastrophe == Game Over.
    s.mission_courante()->state = fen::mission::MissionState::Qualification;
    CHECK(s.embarquer(), "vecu : on repart");
    s.mission_courante()->state = fen::mission::MissionState::Launched;
    G.lived.vitals.o2_kg = 0.0;              // la soute est vide
    CHECK(G.lived.consumables_exhausted(),
          "vecu : reserves epuisees detectees [GDD 9.4]");
    CHECK(!G.character.operational_death, "vecu : avant, le personnage est vivant");
    s.tick(0.1);
    CHECK(G.character.operational_death,
          "vecu : reserves epuisees a bord = mort operationnelle [GDD 10.3 niv. 5]");
    CHECK(!G.character.alive, "vecu : ... et c est un Game Over irrevocable [GDD 3.4]");
    CHECK(!G.lived.active && !G.finance.suspended,
          "vecu : il n y a plus d absent a proteger");
    // Et la regle appliquee est bien celle du GDD, pas un niveau ecrit a la main.
    fen::mission::SeverityModifiers hx; hx.human_lethal_exposure = true;
    CHECK(fen::mission::apply_modifiers(fen::mission::Severity::Critical, hx) ==
              fen::mission::Severity::Catastrophe,
          "vecu : c est le MODIFICATEUR d exposition humaine qui fait le niveau 5");
  }

  // ═══════════════════════════════════════════════════════════════════════════
  // 28. LE VERROU DES RADIATIONS, EN VOL [GDD 6.6, 7.7, 19.7]
  // ═══════════════════════════════════════════════════════════════════════════
  // `env/Radiation.hpp` etait complet et sans aucun consommateur : [GDD 7.7]
  // declare l'environnement « acteur de mission », il n'etait que decor. Ces
  // oracles verifient le BRANCHEMENT — le modele, lui, a les siens dans
  // test_mission_loop. Meme precaution qu'au piege n°72 : ce qui compte est que
  // le chemin VIF passe par la.
  {
    Session s;
    s.nouvelle_partie("Oracle Dose", ModeAide::Normal);
    s.tick(0.0);
    fen::game::GameState& G = *s.jeu.ares.etat;

    fen::mission::Mission mr;
    mr.contract.id = "DOSE-1";
    mr.contract.title = "Croisiere habitee";
    mr.contract.family = "habite";
    mr.contract.crewed = true;
    mr.contract.terms = fen::mission::contract_terms_for_family(mr.contract.family);
    mr.state = fen::mission::MissionState::Qualification;
    G.missions.push_back(mr);
    s.piloter_premiere_mission();

    // (a) LE BLINDAGE EMBARQUE EST CELUI DU PLAN — celui dont la masse a
    // reellement pese au decollage, pas un reglage de derniere minute.
    s.mission_plan.blindage = fen::env::Shielding{10.0, 1.0};
    CHECK(s.embarquer(), "dose : embarquement avec blindage");
    // LE BLINDAGE VU PAR L EQUIPAGE = LA COQUE + CE QU ON A PAYE. Partir de zero
    // decrivait un astronaute sans vehicule, et rendait toute eruption letale
    // quelle que soit l architecture.
    CHECK(std::fabs(s.jeu.ares.etat->lived.blindage.areal_density_gcm2
                    - (fen::mission::COQUE_STRUCTURE_GCM2 + 10.0)) < 1e-12,
          "dose : le blindage embarque = coque + conception [GDD 6.6]");

    // (b) LA DOSE S'ACCUMULE QUAND LE TEMPS COULE, et pas avant.
    s.mission_courante()->state = fen::mission::MissionState::Launched;
    s.mission_courante()->state_entered_days = G.clock.now_days() - 10.0;
    s.tick(0.0);
    const double d0 = G.dose_architecte.career_sv;
    CHECK(d0 == 0.0, "dose : rien pris tant que le temps n a pas coule");
    s.jeu.regler_cadence(fen::game::TimeRate::Month);
    for (int i = 0; i < 10; ++i) s.tick(0.2);
    const double d1 = G.dose_architecte.career_sv;
    std::printf("     dose : %.4f Sv apres %.0f jours de vol habite (blindage 10 g/cm2)\n",
                d1, s.jeu.agence.mois * 30.44);
    CHECK(d1 > 0.0, "dose : le temps qui coule IRRADIE l equipage [GDD 6.6, 7.7]");
    CHECK(std::fabs(G.dose_architecte.mission_sv - d1) < 1e-12,
          "dose : mission et carriere avancent ensemble sur un premier vol");

    // (c) ELLE SUIT LA PHASE : la meme duree en LEO coute MOINS qu en croisiere,
    // parce que la magnetosphere masque la moitie du ciel [GDD 7.7].
    //
    // ON MESURE LA DOSE CHRONIQUE SEULE, et c est indispensable depuis que les
    // ERUPTIONS FRAPPENT VRAIMENT (section 30) : un SPE tire pendant la fenetre
    // LEO gonflerait la comparaison et la ferait echouer au hasard. Le compteur
    // aigu est separe par construction dans `DoseAccumulator` — il suffisait de
    // s en servir. Oracle trouve en le voyant tomber, pas en le supposant.
    auto chronique = [&G]() {
      return G.dose_architecte.mission_sv
             - G.dose_architecte.mission_acute_gy * fen::env::SPE_QUALITY_FACTOR;
    };
    const double av = chronique();
    s.mission_courante()->phase = fen::mission::FlightPhase::LeoOps;
    s.jeu.avancer_temps(30.0);
    s.jeu.ares.assurer(s.jeu.agence, s.jeu.epoch_courant());
    const double d_leo = chronique() - av;
    const double av2 = chronique();
    s.mission_courante()->phase = fen::mission::FlightPhase::TransferCruise;
    s.jeu.avancer_temps(30.0);
    s.jeu.ares.assurer(s.jeu.agence, s.jeu.epoch_courant());
    const double d_cr = chronique() - av2;
    CHECK(d_leo > 0.0 && d_cr > d_leo,
          "dose : 30 j en croisiere coutent plus que 30 j en LEO [GDD 7.7]");

    // (d) LA DOSE DE CARRIERE SURVIT AU DEBARQUEMENT — sinon « un personnage
    // consomme ne revole pas » [GDD 6.6] n aurait aucun sens.
    const double carriere = G.dose_architecte.career_sv;
    CHECK(s.debarquer(), "dose : retour au sol");
    CHECK(G.dose_architecte.career_sv == carriere,
          "dose : la dose de CARRIERE ne redescend pas au debarquement [GDD 6.6]");

    // (e) ET ELLE VERROUILLE LE VOL. C est l arbitrage reel des programmes
    // habites : le compteur est irreversible, seule la passation change de
    // personne [GDD 3.5].
    s.mission_courante()->state = fen::mission::MissionState::Qualification;
    CHECK(s.peut_embarquer().possible, "dose : sous la limite, on revole");
    G.dose_architecte.career_sv = fen::env::CAREER_DOSE_LIMIT_SV + 0.01;
    CHECK(!s.peut_embarquer().possible &&
              s.peut_embarquer().raison.find("dose de carriere") != std::string::npos,
          "dose : au-dela de la limite, INAPTE AU VOL [GDD 6.6]");

    // (f) LA SAUVEGARDE PORTE LE VERROU. Le perdre rendrait apte quelqu un qui
    // ne l est plus — le meme piege que les vivres, une case plus loin.
    G.dose_architecte.career_sv = 0.42;
    CHECK(s.embarquer(), "dose : on repart pour verifier la persistance");
    // APRES l embarquement : il remet a zero les compteurs de MISSION (c est sa
    // raison d etre) et laisse la carriere intacte. Poser l aigu avant, c etait
    // mesurer ce que la porte venait d effacer.
    G.dose_architecte.mission_acute_gy = 0.13;
    CHECK(std::fabs(G.dose_architecte.career_sv - 0.42) < 1e-12,
          "dose : embarquer remet la MISSION a zero, jamais la CARRIERE");
    {
      fen::save::Writer w;
      G.save(w);
      fen::save::Reader r(w.bytes().data(), w.bytes().size());
      fen::game::GameState G2 = G;
      G2.dose_architecte = {};
      G2.lived = {};
      CHECK(G2.load(r), "dose : la sauvegarde se relit");
      CHECK(std::fabs(G2.dose_architecte.career_sv - 0.42) < 1e-12 &&
                std::fabs(G2.dose_architecte.mission_acute_gy - 0.13) < 1e-12,
            "dose : la dose de carriere et l aigu survivent au rechargement");
      CHECK(std::fabs(G2.lived.blindage.areal_density_gcm2
                      - (fen::mission::COQUE_STRUCTURE_GCM2 + 10.0)) < 1e-12,
            "dose : ... et le blindage embarque aussi");
    }

    // (g) UNE DOSE AIGUE LETALE TUE, et sans accuser le joueur : elle vient d une
    // eruption, pas d un oubli de provisionnement [GDD 7.7 — l environnement est
    // un ACTEUR].
    CHECK(!G.character.operational_death, "dose : vivant avant l eruption");
    G.dose_architecte.add_acute_gy(fen::env::ACUTE_LETHAL_GY + 0.1);
    s.tick(0.1);
    CHECK(G.character.operational_death && !G.character.alive,
          "dose : une dose aigue letale est une mort operationnelle [GDD 3.4, 6.6]");
    CHECK(s.jeu.raison_faillite.find("dose aigue letale") != std::string::npos,
          "dose : ... et le motif affiche DIT laquelle des deux morts c est");
  }

  // ═══════════════════════════════════════════════════════════════════════════
  // 29. LES DEBRIS RETOMBENT [GDD 7.8, 10.5]
  // ═══════════════════════════════════════════════════════════════════════════
  // `add_breakup` etait sur le chemin vif, `tick` seulement dans le tick MORT :
  // les nuages s accumulaient sans jamais decroitre, et la promesse de 7.8 —
  // « les couloirs LEO se nettoient, les couloirs hauts restent pollues » —
  // n avait que sa moitie punitive.
  {
    Session s;
    s.nouvelle_partie("Oracle Debris", ModeAide::Normal);
    s.tick(0.0);
    fen::game::GameState& G = *s.jeu.ares.etat;

    // Deux nuages identiques, a deux altitudes que TOUT separe.
    G.debris.add_breakup("BAS", 300.0, 2000.0, fen::env::BreakupKind::Explosion,
                         G.clock.now_days());
    G.debris.add_breakup("HAUT", 1200.0, 2000.0, fen::env::BreakupKind::Explosion,
                         G.clock.now_days());
    const fen::env::Corridor bas{250.0, 350.0}, haut{1150.0, 1250.0};
    const double n_bas0 = G.debris.population(bas);
    const double n_haut0 = G.debris.population(haut);
    CHECK(n_bas0 > 0.0 && n_haut0 > 0.0, "debris : les deux nuages existent");

    // DEUX ANS de temps de jeu, par le chemin VIF (pas GameState::tick).
    s.jeu.avancer_temps(730.0);
    s.jeu.ares.assurer(s.jeu.agence, s.jeu.epoch_courant());
    const double n_bas1 = G.debris.population(bas);
    const double n_haut1 = G.debris.population(haut);
    std::printf("     debris apres 2 ans : 300 km %.0f -> %.0f objets | "
                "1200 km %.0f -> %.0f objets\n", n_bas0, n_bas1, n_haut0, n_haut1);
    CHECK(n_bas1 < n_bas0,
          "debris : le couloir BAS se nettoie — la trainee fait son travail [GDD 7.8]");
    CHECK(n_haut1 > 0.9 * n_haut0,
          "debris : le couloir HAUT reste pollue, et c est l empreinte durable [GDD 10.5]");
  }

  // ═══════════════════════════════════════════════════════════════════════════
  // 30. LA BIBLIOTHEQUE D'ANOMALIES SE PRODUIT ENFIN [GDD 9.5, 9.1]
  // ═══════════════════════════════════════════════════════════════════════════
  // `mission/Events.hpp` tirait des evenements calibres depuis le premier jour et
  // PERSONNE ne les consommait : aucune anomalie ne se produisait jamais. Le
  // dernier gros modele sans appelant.
  {
    Session s;
    s.nouvelle_partie("Oracle Avaries", ModeAide::Normal);
    s.tick(0.0);
    fen::game::GameState& G = *s.jeu.ares.etat;

    fen::mission::Mission ma;
    ma.contract.id = "AVARIE-1";
    ma.contract.title = "Croisiere longue";
    ma.contract.family = "mars_habite";
    ma.contract.crewed = true;
    ma.contract.terms = fen::mission::contract_terms_for_family(ma.contract.family);
    ma.state = fen::mission::MissionState::Qualification;
    G.missions.push_back(ma);
    s.piloter_premiere_mission();
    G.career.rank = fen::career::Rank::Directeur;
    if (auto* n = G.tree.find_mut("sejour_long")) n->trl = fen::tech::TRL_OPERATIONAL;
    CHECK(s.embarquer(), "avaries : embarquement sur une longue croisiere");
    s.mission_courante()->state = fen::mission::MissionState::Launched;
    s.mission_courante()->state_entered_days = G.clock.now_days();
    s.mission_courante()->phase = fen::mission::FlightPhase::TransferCruise;

    // (a) LE MECANISME D ABORD, ROBUSTE ET SANS PARI. Les taux de
    // `event_library` valent ~1e-3/jour par type : sur 400 jours l esperance est
    // de l ordre de UN evenement, si bien qu un oracle « il s est passe quelque
    // chose en 400 jours » serait vrai six fois sur dix — un oracle bancal par
    // construction. On verifie donc le SAMPLER directement, sur une fenetre
    // longue ou l attente est ecrasante, puis son DETERMINISME.
    {
      fen::mission::EventContext ctx;
      ctx.crewed = true;
      ctx.phase = fen::mission::FlightPhase::TransferCruise;
      const Rng rg(1234567u);
      const auto e1 = fen::mission::sample_events(rg, 1, 0.0, 5000.0, ctx);
      const auto e2 = fen::mission::sample_events(rg, 1, 0.0, 5000.0, ctx);
      CHECK(!e1.empty(),
            "avaries : sur une fenetre longue, la bibliotheque PRODUIT [GDD 9.5]");
      CHECK(e1.size() == e2.size(),
            "avaries : le meme tirage rendu deux fois donne le meme resultat");
      // Et un vol ROBOTIQUE n a ni panne de support-vie ni urgence medicale.
      fen::mission::EventContext rob = ctx; rob.crewed = false;
      bool habite_seul = true;
      for (const auto& e : fen::mission::sample_events(rg, 1, 0.0, 5000.0, rob))
        if (e.kind == fen::mission::EventKind::LifeSupportFault ||
            e.kind == fen::mission::EventKind::MedicalEmergency) habite_seul = false;
      CHECK(habite_seul,
            "avaries : sans equipage, ni support-vie ni urgence medicale");
    }

    // (a bis) PUIS LE BRANCHEMENT : on avance par tranches de 400 jours (la borne
    // de rattrapage) jusqu a ce que le vol connaisse sa premiere avarie.
    int tranches = 0;
    while (G.avaries.empty() && tranches < 6) {
      s.jeu.avancer_temps(400.0);
      s.jeu.ares.assurer(s.jeu.agence, s.jeu.epoch_courant());
      ++tranches;
    }
    std::printf("     avaries : %d avarie(s) apres %d j de croisiere habitee "
                "(dose aigue cumulee %.3f Gy)\n",
                (int)G.avaries.size(), tranches * 400, G.dose_architecte.mission_acute_gy);
    CHECK(!G.avaries.empty(),
          "avaries : le vol FINIT par connaitre une anomalie, et elle est stockee");
    CHECK(G.lived.jour_evenements_tire > 0.0,
          "avaries : l index de tirage a avance");

    // (b) LE VOL EST REJOUABLE QUEL QUE SOIT LE DECOUPAGE DU TEMPS. Meme exigence
    // que les sous-pas [GDD 14.2], appliquee a l alea : un tirage par frame
    // aurait donne un vol different a chaque machine. Deux parties identiques,
    // une avance de 400 jours d un bloc contre huit de cinquante.
    {
      auto vol = [&ma](Session& sx, int n_tranches, double jours) {
        sx.nouvelle_partie("Oracle Rejeu", ModeAide::Normal);
        sx.tick(0.0);
        fen::game::GameState& Gx = *sx.jeu.ares.etat;
        Gx.missions.push_back(ma);
        sx.piloter_premiere_mission();
        Gx.career.rank = fen::career::Rank::Directeur;
        if (auto* n = Gx.tree.find_mut("sejour_long")) n->trl = fen::tech::TRL_OPERATIONAL;
        sx.embarquer();
        sx.mission_courante()->state = fen::mission::MissionState::Launched;
        sx.mission_courante()->state_entered_days = Gx.clock.now_days();
        sx.mission_courante()->phase = fen::mission::FlightPhase::TransferCruise;
        for (int i = 0; i < n_tranches; ++i) {
          sx.jeu.avancer_temps(jours);
          sx.jeu.ares.assurer(sx.jeu.agence, sx.jeu.epoch_courant());
        }
      };
      Session sa, sb;
      vol(sa, 1, 400.0);
      vol(sb, 8, 50.0);
      fen::game::GameState& Ga = *sa.jeu.ares.etat;
      fen::game::GameState& Gb = *sb.jeu.ares.etat;
      CHECK(Ga.avaries.size() == Gb.avaries.size(),
            "avaries : 1 avance de 400 j ou 8 de 50 j -> MEMES pannes (fenetres d un jour)");
      bool memes = Ga.avaries.size() == Gb.avaries.size();
      for (std::size_t k = 0; memes && k < Ga.avaries.size(); ++k)
        memes = Ga.avaries[k].kind == Gb.avaries[k].kind &&
                std::fabs(Ga.avaries[k].debut_days - Gb.avaries[k].debut_days) < 1e-9;
      CHECK(memes, "avaries : ... et aux MEMES dates, du meme type");
      // LA DOSE AIGUE AUSSI : les eruptions sont frequentes, c est donc ELLE qui
      // rend cet oracle mordant meme quand aucune panne n est tombee.
      CHECK(Ga.dose_architecte.mission_acute_gy > 0.0,
            "avaries : des eruptions ont bien frappe pendant ces 400 jours");
      CHECK(std::fabs(Ga.dose_architecte.mission_acute_gy
                      - Gb.dose_architecte.mission_acute_gy) < 1e-12,
            "avaries : ... et la dose aigue recue est IDENTIQUE dans les deux decoupages");
    }

    // (c) UNE AVARIE COUTE, ET ELLE COUTE SUR LES VIVRES. Une panne de
    // support-vie DEGRADE la boucle : ce n est pas une icone, c est une
    // hemorragie de consommables.
    {
      const fen::mission::RecyclingLoops nom = fen::mission::RecyclingLoops::iss();
      std::vector<fen::mission::Avarie> une;
      fen::mission::Avarie ls;
      ls.kind = fen::mission::EventKind::LifeSupportFault;
      ls.debut_days = 0.0; ls.gravite01 = 1.0;
      une.push_back(ls);
      const auto eff = fen::mission::effets_avaries(une, 10.0);
      const auto deg = fen::mission::boucles_degradees(nom, eff);
      CHECK(deg.water_recovery < nom.water_recovery && deg.o2_recovery < nom.o2_recovery,
            "avaries : une panne support-vie DEGRADE les boucles [GDD 9.1]");
      // Et la consequence se lit dans l autonomie, pas ailleurs.
      fen::mission::VitalState v1{100.0, 100.0, 100.0, 100.0};
      fen::mission::VitalState v2 = v1;
      v1.consume(6, 10.0, nom);
      v2.consume(6, 10.0, deg);
      CHECK(v2.o2_kg < v1.o2_kg && v2.water_kg < v1.water_kg,
            "avaries : ... donc l equipage consomme PLUS pendant la panne");
      // Une avarie reparee ne coute plus rien.
      une[0].reparee = true;
      CHECK(fen::mission::effets_avaries(une, 10.0).n_actives == 0,
            "avaries : une avarie reparee cesse de couter");
    }

    // (d) REPARER EST UNE CAPACITE, PAS UN DE [GDD 9.1, 5.10]. Sans la branche 4,
    // on ne repare rien — et le refus NOMME la techno manquante (piege n°42).
    if (!G.avaries.empty()) {
      std::size_t idx = 0;
      bool trouve = false;
      for (std::size_t k = 0; k < G.avaries.size(); ++k)
        if (G.avaries[k].kind != fen::mission::EventKind::MedicalEmergency &&
            G.avaries[k].kind != fen::mission::EventKind::CommLoss)
        { idx = k; trouve = true; break; }
      if (trouve) {
        CHECK(!s.reparer_avarie(idx),
              "avaries : sans maintenance_locale, on ne repare rien [GDD 5.10]");
        CHECK(s.dernier_refus_reparation.find("maintenance_locale") != std::string::npos,
              "avaries : ... et le refus NOMME la techno a rechercher");
        if (auto* n = G.tree.find_mut("maintenance_locale"))
          n->trl = fen::tech::TRL_OPERATIONAL;
        CHECK(s.reparer_avarie(idx),
              "avaries : avec la capacite embarquee, la reparation s engage");
        CHECK(G.avaries[idx].en_reparation(G.clock.now_days()),
              "avaries : ... et elle PREND DU TEMPS (l avarie coute encore)");
        CHECK(!s.reparer_avarie(idx),
              "avaries : on n engage pas deux fois la meme reparation");
        // Le temps passe : la reparation aboutit d elle-meme.
        s.jeu.avancer_temps(30.0);
        s.jeu.ares.assurer(s.jeu.agence, s.jeu.epoch_courant());
        CHECK(G.avaries[idx].reparee,
              "avaries : la reparation aboutit quand son temps est ecoule");
      }
      // LE DIAGNOSTIC AUTONOME RACCOURCIT LE TRAVAIL : la branche 4 achete du
      // temps, pas de la chance.
      fen::mission::Avarie t; t.gravite01 = 0.5;
      fen::mission::CapaciteBord nu; nu.maintenance_locale = true;
      fen::mission::CapaciteBord equipe = nu;
      equipe.diagnostics_autonomes = true; equipe.redondance_base = true;
      CHECK(fen::mission::duree_reparation_jours(t, equipe) <
                fen::mission::duree_reparation_jours(t, nu),
            "avaries : diagnostics et redondance RACCOURCISSENT la reparation");
    }

    // (e) LE BLINDAGE SAUVE CONTRE LES ERUPTIONS, LA OU IL NE SERT PAS CONTRE LE
    // GCR. C est la symetrie que le fond permanent cachait, et la raison d etre
    // des abris anti-tempete reels [GDD 6.6].
    {
      const double nu = fen::mission::dose_aigue_spe_gy(1.0, fen::env::Shielding{0.0, 1.0});
      const double abri = fen::mission::dose_aigue_spe_gy(1.0, fen::env::Shielding{20.0, 1.0});
      std::printf("     eruption majeure : %.2f Gy non blindee -> %.2f Gy derriere 20 g/cm2"
                  "  (letal a %.1f Gy)\n", nu, abri, fen::env::ACUTE_LETHAL_GY);
      CHECK(nu > fen::env::ACUTE_LETHAL_GY,
            "avaries : une eruption majeure non blindee est LETALE [GDD 6.6]");
      CHECK(abri < nu / 3.0,
            "avaries : 20 g/cm2 divisent la dose aigue par plus de trois (exponentiel)");
      // ET LA MESURE CORRIGE CE QUE J AVAIS ECRIT ICI : 20 g/cm2 ne font PAS
      // passer sous le seuil de syndrome aigu (1,32 Gy contre 1,0). Ils font
      // mieux et moins a la fois — ils transforment une dose LETALE en une dose
      // survivable. C est exactement ce que promet un abri anti-tempete reel :
      // il ne rend pas l eruption inoffensive, il fait rentrer l equipage.
      CHECK(abri < fen::env::ACUTE_LETHAL_GY,
            "avaries : 20 g/cm2 transforment une eruption LETALE en dose survivable");
      CHECK(abri > fen::env::ACUTE_SICKNESS_GY,
            "avaries : ... sans pour autant la rendre inoffensive [GDD 6.6]");
    }

    // (f) LES AVARIES SURVIVENT A LA SAUVEGARDE. Les perdre reparerait tout
    // gratuitement ET retirerait les memes fenetres en double.
    {
      fen::save::Writer w;
      G.save(w);
      fen::save::Reader r(w.bytes().data(), w.bytes().size());
      fen::game::GameState G3 = G;
      G3.avaries.clear();
      G3.lived.jour_evenements_tire = -1.0;
      CHECK(G3.load(r), "avaries : la sauvegarde se relit");
      CHECK(G3.avaries.size() == G.avaries.size(),
            "avaries : toutes les avaries survivent au rechargement");
      CHECK(G3.lived.jour_evenements_tire == G.lived.jour_evenements_tire,
            "avaries : ... et l index de tirage, sinon les memes pannes retombent");
    }
  }

  // ═══════════════════════════════════════════════════════════════════════════
  // 31. LES DEUX HORLOGES, ET LA PREPARATION MEDICALE [GDD 6.7, 14.4, 11.6]
  // ═══════════════════════════════════════════════════════════════════════════
  // MEME DEFAUT QUE LES DEBRIS, DEUX CRANS PLUS LOIN. `rel::DualClock` etait
  // declare sur GameState, sauvegarde, recharge — et AVANCE NULLE PART : le
  // vieillissement differentiel que [GDD 3.4] fait peser sur la passation valait
  // zero pour toute mission. Et `EventContext::medical_risk_factor` etait ecrit en
  // dur a 1,0 dans le tick alors que `station::effects` le calculait : le module
  // medical de Novellus coutait 110 M€ et ne changeait aucun tirage.
  // Ces oracles verifient le BRANCHEMENT ; la PHYSIQUE, elle, est confrontee aux
  // valeurs publiees (GPS, ISS) dans test_ares_modules.
  {
    Session s;
    s.nouvelle_partie("Oracle Horloges", ModeAide::Normal);
    s.tick(0.0);
    fen::game::GameState& G = *s.jeu.ares.etat;

    fen::mission::Mission mh;
    mh.contract.id = "HOR-1";
    mh.contract.title = "Croisiere martienne habitee";
    mh.contract.family = "mars_habite";
    mh.contract.crewed = true;
    mh.contract.terms = fen::mission::contract_terms_for_family(mh.contract.family);
    mh.state = fen::mission::MissionState::Qualification;
    G.missions.push_back(mh);
    s.piloter_premiere_mission();
    G.career.rank = fen::career::Rank::Directeur;
    if (auto* n = G.tree.find_mut("sejour_long")) n->trl = fen::tech::TRL_OPERATIONAL;

    // (a) LA PREPARATION MEDICALE EST CELLE QU ON A CONSTRUITE. Sans module, le
    // facteur est neutre ; avec, il vaut ce que `station::effects` dit — et c est
    // GELE au depart, comme la fiabilite et le blindage.
    CHECK(s.embarquer(), "horloges : embarquement sans module medical");
    CHECK(std::fabs(G.lived.facteur_risque_medical - 1.0) < 1e-12,
          "medical : sans module, le risque n est pas reduit");
    CHECK(s.debarquer(), "horloges : retour au sol");
    G.station.modules.push_back(
        fen::station::StationModule{fen::station::ModuleType::Medical, true});
    const double attendu =
        fen::station::effects(G.station).medical_risk_factor;
    CHECK(attendu < 1.0, "11.6 : le modele CALCULE bien une reduction");
    CHECK(s.embarquer(), "horloges : re-embarquement avec module medical");
    CHECK(std::fabs(G.lived.facteur_risque_medical - attendu) < 1e-12,
          "medical : le facteur de Novellus est EMBARQUE, plus ecrit en dur [GDD 11.6]");
    // ET IL MORD SUR LE TIRAGE : c est la seule chose qui compte. On compare des
    // TAUX, pas des tirages — un dé qui tombe pareil ne prouverait rien.
    {
      fen::mission::EventContext c0, c1;
      c0.crewed = c1.crewed = true;
      c1.medical_risk_factor = attendu;
      const fen::mission::EventSpec* med = nullptr;
      for (const auto& sp : fen::mission::event_library())
        if (sp.kind == fen::mission::EventKind::MedicalEmergency) med = &sp;
      CHECK(med != nullptr, "medical : l urgence medicale est dans la bibliotheque");
      if (med)
        CHECK(fen::mission::effective_rate(*med, c1) <
                  fen::mission::effective_rate(*med, c0),
              "medical : le module REDUIT le taux d urgence medicale [GDD 9.4]");
    }

    // (b) LA GEOMETRIE DES HORLOGES EST LUE SUR LES EPHEMERIDES, pas posee. Un
    // transfert vers Mars a un demi-grand axe STRICTEMENT entre celui de la Terre
    // et celui de Mars — c est la definition meme de l ellipse de Hohmann.
    const fen::mission::GeometrieHorloge& g = G.lived.horloge;
    CHECK(g.valide(), "horloges : la geometrie est gelee au depart");
    CHECK(g.a_croisiere_m > g.a_terre_m && g.a_croisiere_m < g.a_sejour_m,
          "horloges : a(transfert) est entre a(Terre) et a(Mars) [Hohmann]");
    std::printf("     horloges : a_Terre=%.4f UA  a_transfert=%.4f UA  a_Mars=%.4f UA\n",
                g.a_terre_m / cst::AU, g.a_croisiere_m / cst::AU, g.a_sejour_m / cst::AU);

    // (c) AU SOL, LES DEUX HORLOGES BATTENT ENSEMBLE. Ce n est pas un raccourci :
    // l Architecte partage alors litteralement l horloge du monde.
    s.mission_courante()->state = fen::mission::MissionState::Launched;
    s.mission_courante()->state_entered_days = G.clock.now_days();
    s.mission_courante()->phase = fen::mission::FlightPhase::Ground;
    s.jeu.avancer_temps(100.0);
    s.jeu.ares.assurer(s.jeu.agence, s.jeu.epoch_courant());
    CHECK(std::fabs(G.dual_clock.aging_gap()) < 1e-9,
          "6.7 : au sol, aucun ecart entre le bord et la Terre");
    CHECK(G.dual_clock.t_earth > 0.0,
          "6.7 : ... mais l horloge Terre a bien tourne (le tick passe par la)");

    // (d) EN CROISIERE, ELLES DIVERGENT — ET DANS LE SENS QUE LA PHYSIQUE IMPOSE,
    // PAS DANS CELUI DU CLICHE. Plus haut dans le potentiel solaire ET plus lent
    // que la Terre : les deux termes vont dans le meme sens, l horloge de bord
    // GAGNE, et le voyageur revient PLUS VIEUX. Le signe est un RESULTAT.
    const double t0 = G.dual_clock.t_earth, tau0 = G.dual_clock.tau_board;
    const double age0 = G.character.age_bio_s;
    s.mission_courante()->phase = fen::mission::FlightPhase::TransferCruise;
    s.jeu.avancer_temps(400.0);
    s.jeu.ares.assurer(s.jeu.agence, s.jeu.epoch_courant());
    const double dt = G.dual_clock.t_earth - t0;
    const double dtau = G.dual_clock.tau_board - tau0;
    std::printf("     horloges : %.1f j de croisiere -> ecart %+.4f ms (bord - Terre)\n",
                dt / 86400.0, (dtau - dt) * 1e3);
    CHECK(dt > 0.0, "6.7 : le temps a coule cote Terre");
    CHECK(dtau > dt,
          "6.7 : en croisiere heliocentrique, l horloge de BORD gagne [potentiel > vitesse]");
    // L AGE BIOLOGIQUE SUIT LE TEMPS PROPRE, pas le calendrier. C est la seule
    // consequence qui compte pour [GDD 3.4] : un age, pas un compteur decoratif.
    CHECK(std::fabs((G.character.age_bio_s - age0) - dtau) < 1e-6,
          "6.7.4 : l age biologique avance du TEMPS PROPRE, pas du calendrier");
    CHECK(G.character.age_bio_s - age0 > dt,
          "6.7 : ... donc l Architecte vieillit PLUS VITE que son agence");
    // MAIS L ECART RESTE IMPERCEPTIBLE, ce que [GDD 6.7.2] affirme et que le
    // modele chiffre desormais : sous la seconde sur un aller-retour entier.
    CHECK(std::fabs(G.dual_clock.aging_gap()) < 1.0,
          "6.7.2 : sous la seconde — le GDD l affirmait, le modele le MESURE");
    CHECK(!G.dual_clock.diverged(),
          "6.7.2 : une croisiere martienne ne franchit pas le seuil d affichage");

    // (e) EN ORBITE BASSE, LE SIGNE S INVERSE : la vitesse l emporte sur
    // l altitude, et l equipage vieillit MOINS. Deux regimes, une seule formule.
    const double avant = G.dual_clock.aging_gap();
    s.mission_courante()->phase = fen::mission::FlightPhase::LeoOps;
    s.jeu.avancer_temps(200.0);
    s.jeu.ares.assurer(s.jeu.agence, s.jeu.epoch_courant());
    CHECK(G.dual_clock.aging_gap() > avant,
          "6.7 : en orbite BASSE, l horloge de bord PERD (l ISS retarde)");

    // (f) LA GEOMETRIE GELEE SURVIT A LA SAUVEGARDE. La perdre ferait qu un vol
    // recharge ne battrait plus au meme rythme ET ne tirerait plus les memes
    // urgences : la rejouabilite d un vol est une PROMESSE du modele.
    {
      fen::save::Writer w;
      G.save(w);
      fen::save::Reader r(w.bytes().data(), w.bytes().size());
      fen::game::GameState G4 = G;
      G4.lived.horloge = {};
      G4.lived.facteur_risque_medical = 1.0;
      G4.dual_clock = {};
      CHECK(G4.load(r), "horloges : la sauvegarde se relit");
      CHECK(std::fabs(G4.lived.horloge.a_croisiere_m - g.a_croisiere_m) < 1e-6 &&
                std::fabs(G4.lived.horloge.a_terre_m - g.a_terre_m) < 1e-6,
            "horloges : la geometrie gelee survit au rechargement");
      CHECK(std::fabs(G4.lived.facteur_risque_medical - attendu) < 1e-12,
            "medical : ... et la preparation medicale recue aussi");
      CHECK(std::fabs(G4.dual_clock.tau_board - G.dual_clock.tau_board) < 1e-9,
            "horloges : ... et les deux horloges elles-memes");
    }

    // (g) UNE ARCHIVE V2 SE RECHARGE SANS RIEN CASSER : elle retombe sur des
    // defauts qui reproduisent EXACTEMENT son comportement d origine (facteur
    // neutre, geometrie invalide donc rapport 1). Pas de derive silencieuse.
    {
      fen::mission::GeometrieHorloge vide;
      CHECK(!vide.valide(), "horloges : une geometrie non renseignee se declare invalide");
      CHECK(fen::mission::rapport_horloge_bord(
                fen::mission::FlightPhase::TransferCruise, vide) == 1.0,
            "horloges : sans geometrie, le rapport vaut 1 (comportement d avant)");
    }
  }

  // ═══════════════════════════════════════════════════════════════════════════
  // 32. L ANTIMATIERE TOURNE DANS LE TICK VIF [GDD 5.12.12, 19.3, 11.5]
  // ═══════════════════════════════════════════════════════════════════════════
  // Dernier maillon de la chaine relativiste sans consommateur : quatre
  // parametres de production et pas un gramme nulle part. Ces oracles verifient
  // le BRANCHEMENT (la physique a les siens dans test_ares_modules) et surtout
  // que la QUALIFICATION commande la production, la puissance le debit.
  {
    Session s;
    s.nouvelle_partie("Oracle Antimatiere", ModeAide::Normal);
    s.tick(0.0);
    fen::game::GameState& G = *s.jeu.ares.etat;

    // (a) SANS LA FILIERE QUALIFIEE, RIEN NE SE PRODUIT. Le noeud est en fin
    // d arbre [GDD 19.3] : une agence debutante n a pas d antimatiere qui
    // s accumule toute seule dans un coin.
    CHECK(G.antimatiere.grams == 0.0, "antimatiere : stock nul au depart");
    const fen::tech::TechNode* nam = G.tree.find("antimatiere");
    CHECK(nam != nullptr, "antimatiere : le noeud existe dans l arbre");
    CHECK(nam && !nam->operational(), "antimatiere : ... et n est PAS qualifie au depart");
    s.jeu.avancer_temps(365.0 * 5.0);
    s.jeu.ares.assurer(s.jeu.agence, s.jeu.epoch_courant());
    CHECK(G.antimatiere.grams == 0.0,
          "antimatiere : 5 ans sans la filiere ne produisent RIEN [GDD 19.3]");

    // (b) QUALIFIEE, ELLE PRODUIT — ET LE DEBIT VIENT DE LA BRANCHE 6, PAS DE LA
    // STATION. C etait le defaut : la puissance prise etait la MARGE DE NOVELLUS,
    // si bien qu aucune recherche de branche 6 ne pouvait deplacer le debit. Le
    // « vrai levier d equilibrage » [GDD 5.12.12] n etait pas branche sur son
    // levier. On verifie desormais que le PALIER commande, et que la marge de la
    // station n y est pour rien.
    CHECK(fen::app::antimatter_tier(G) == fen::rel::AntimatterTier::None,
          "5.12.12 : sans aucun palier de branche 6, il n y a pas d usine");
    if (auto* n = G.tree.find_mut("antimatiere")) n->trl = fen::tech::TRL_OPERATIONAL;
    s.jeu.ares.assurer(s.jeu.agence, s.jeu.epoch_courant());
    CHECK(fen::app::antimatter_tier(G) == fen::rel::AntimatterTier::Mature,
          "5.12.12 : le noeud antimatiere qualifie porte le palier ABOUTI");
    const double marge0 = G.station.power_margin_kw();
    s.jeu.avancer_temps(365.0);
    s.jeu.ares.assurer(s.jeu.agence, s.jeu.epoch_courant());
    const double debit = G.antimatiere.prod.rate_g_yr();
    std::printf("     antimatiere : %.3e g apres 1 an (usine %.0e W, marge station %.0f kW)\n",
                G.antimatiere.grams, G.antimatiere.prod.plant_power_w, marge0);
    CHECK(G.antimatiere.grams > 0.0,
          "antimatiere : qualifiee, le stock MONTE [GDD 5.12.12]");
    CHECK(G.antimatiere.prod.plant_power_w > marge0 * 1000.0 * 1.0e6,
          "5.12.12 : l usine n est PAS un module de station (six ordres au-dessus)");
    CHECK(debit > 0.0, "5.12.12 : ... et son debit est celui du palier, pas de la marge");

    // ═══════════════════════════════════════════════════════════════════════
    // (c) LE PROGRAMME DE FIN DE JEU EST COUPLÉ À LA SANTÉ DE L'AGENCE
    // ═══════════════════════════════════════════════════════════════════════
    // TROUVÉ EN RECALIBRANT : l'oracle précédent avançait de 200 puis 200 ans et
    // concluait « le stock CONVERGE — 200 ans de plus n'ajoutent rien ». Il
    // passait, et POUR UNE RAISON FAUSSE : au bout de quelques années sans aucun
    // programme, l'agence fait FAILLITE et `Jeu::avancer_temps` s'arrête net
    // (« faillite : le calendrier s'arrête là »). Le stock ne convergeait pas, LE
    // TEMPS S'ARRÊTAIT. Tant que la calibration rendait le stock dérisoire, les
    // deux se ressemblaient assez pour que personne ne les distingue.
    //
    // ET LE FAIT DE JEU QUE CELA RÉVÈLE EST MEILLEUR QUE L'ORACLE PERDU : les
    // 140 ans d'accumulation de [GDD 3.5] ne s'obtiennent PAS en laissant filer le
    // temps. La pression d'inactivité [GDD 13.2] les interdit. Il faut faire
    // tourner une agence pendant plusieurs vies pour payer un vol relativiste —
    // c'est exactement ce que « atteindre la fin de la branche 6 demande souvent
    // plusieurs vies » veut dire, et cela a désormais un mécanisme.
    {
      Session inactif;
      inactif.nouvelle_partie("Oracle Inaction", ModeAide::Normal);
      inactif.tick(0.0);
      auto& Gi = *inactif.jeu.ares.etat;
      if (auto* n = Gi.tree.find_mut("antimatiere")) n->trl = fen::tech::TRL_OPERATIONAL;
      inactif.jeu.avancer_temps(365.0 * 140.0);
      CHECK(inactif.jeu.game_over,
            "13.2 : 140 ans sans rien produire = faillite, le calendrier s arrete");
      CHECK(fen::rel::beta_from_antimatter(5000.0, Gi.antimatiere.grams)
                < fen::rel::AntimatterProduction::CALIB_TARGET_BETA,
            "3.5 : ... donc l inaction n achete PAS le regime relativiste");
    }

    // Agence tenue à flot — la seule façon d'atteindre l'horizon de [GDD 3.5].
    // Le stock converge alors vraiment, et c'est la FUITE qui le borne.
    for (int an = 0; an < 400; ++an) {
      s.jeu.agence.tresorerie += 60000.0;   // une agence qui produit
      s.jeu.avancer_temps(365.0);
      s.jeu.ares.assurer(s.jeu.agence, s.jeu.epoch_courant());
      if (s.jeu.game_over) break;
    }
    CHECK(!s.jeu.game_over, "13.2 : une agence financee traverse les quatre siecles");
    // LE STOCK CONVERGE VERS L'ÉQUILIBRE, PAR EN DESSOUS, SANS JAMAIS LE PASSER.
    // La constante de temps est 1/λ = 274 ans : c'est ELLE qui dit combien de
    // siècles il faut, et non un chiffre rond — d'où le fait que quatre siècles
    // n'y suffisent pas. On en laisse passer cinq.
    const double gain_400 = G.antimatiere.grams;
    for (int an = 0; an < 1100; ++an) {
      s.jeu.agence.tresorerie += 60000.0;
      s.jeu.avancer_temps(365.0);
      s.jeu.ares.assurer(s.jeu.agence, s.jeu.epoch_courant());
    }
    const double eq = G.antimatiere.equilibrium_g();
    std::printf("     antimatiere : 1500 ans -> %.3e g pour un equilibre de %.3e g\n",
                G.antimatiere.grams, eq);
    CHECK(G.antimatiere.grams > gain_400,
          "antimatiere : quatre siecles ne suffisent pas — le stock monte encore");
    CHECK(G.antimatiere.grams > 0.99 * eq,
          "5.12.12 : ... a cinq constantes de temps il est a 1 % de l equilibre");
    CHECK(G.antimatiere.grams <= eq * (1.0 + 1e-9),
          "5.12.12 : ... et il l approche PAR EN DESSOUS, sans jamais le passer");
    CHECK(G.antimatiere.grams <= G.antimatiere.prod.confinement_capacity_g + 1e-12,
          "antimatiere : le confinement n est jamais depasse");
    CHECK(G.antimatiere.borne_par_la_fuite(),
          "5.12.12 : ... et ce qui le borne est la FUITE, pas le reservoir");

    // (d) ET LE VERDICT REMONTE JUSQU AU VOL : le beta d une mission relativiste
    // est celui que l antimatiere EMBARQUEE achete. LA CALIBRATION [Annexe E] A
    // ETE TRANCHEE PAR LE CORPS DU GDD, et le verdict s est donc inverse : ce
    // stock-la FRANCHIT le seuil, ce que [GDD 6.7.2] exige (« seule l antimatiere
    // franchit beta >= 0,3 »). Deux vies d accumulation, et le regime existe.
    const double b = fen::rel::beta_from_antimatter(5000.0, G.antimatiere.grams);
    std::printf("     antimatiere : stock a l equilibre %.3e g -> beta = %.3f "
                "sur une sonde de 5 t\n", G.antimatiere.grams, b);
    CHECK(b > fen::rel::BETA_THRESHOLD,
          "6.7.2 : au palier abouti, le stock atteignable FRANCHIT le seuil");
    CHECK(b >= fen::rel::AntimatterProduction::CALIB_TARGET_BETA,
          "6.7.2 : ... et depasse la cible de calibration beta = 0,3");
    // Ce qui reste hors d atteinte l est pour une raison PHYSIQUE et non par
    // decret : le verrou de l aller-retour [GDD 6.7.4], ratio a la puissance 4.
    // Mesure sur l ARCHITECTURE HABITEE de reference, qui est la seule ou le
    // relativisme a un interet (decision de l utilisateur) : un aller-retour a
    // beta = 0,9 demande 4,3e15 g contre 9,6e9 disponibles — cinq ordres.
    CHECK(G.antimatiere.hors_atteinte(fen::rel::antimatter_needed_g(
              fen::rel::AntimatterProduction::CALIB_DRY_MASS_KG, 0.9,
              fen::rel::AntimatterProduction::CALIB_BURNS)),
          "6.7.4 : l aller-retour HABITE a 0,9 reste hors d atteinte — le ratio^4");
    // Le chemin existe, et il est EXACT : un beta au-dela du seuil bascule
    // l horloge sur la cinematique pure.
    fen::mission::GeometrieHorloge gr;
    gr.a_terre_m = gr.a_croisiere_m = gr.a_sejour_m = cst::AU;
    gr.beta_croisiere = 0.7;
    const double rap = fen::mission::rapport_horloge_bord(
        fen::mission::FlightPhase::TransferCruise, gr);
    CHECK(std::fabs(rap - 1.0 / fen::rel::lorentz_gamma(0.7)) < 1e-15,
          "6.7 : au-dela du seuil, l horloge passe a la cinematique PURE (exacte)");
    CHECK(rap < 0.72, "6.7 : ... et a beta=0,7 le bord ne bat plus qu a 71 % du sol");

    // (e) LE STOCK SURVIT A LA SAUVEGARDE. Des annees d accumulation qui
    // repartiraient de zero au rechargement remettraient le programme de fin de
    // jeu a plat en silence.
    {
      fen::save::Writer w;
      G.save(w);
      fen::save::Reader r(w.bytes().data(), w.bytes().size());
      fen::game::GameState G5 = G;
      G5.antimatiere.grams = 0.0;
      CHECK(G5.load(r), "antimatiere : la sauvegarde se relit");
      CHECK(std::fabs(G5.antimatiere.grams - G.antimatiere.grams) < 1e-18,
            "antimatiere : le stock survit au rechargement");
    }

    // ═══════════════════════════════════════════════════════════════════════
    // ═══════════════════════════════════════════════════════════════════════
    // (e1a) LE CISLUNAIRE EXISTE, ET L ALUNISSAGE SE CALCULE [GDD 3.3, 7.6, 19.7]
    // ═══════════════════════════════════════════════════════════════════════
    // Le GDD nommait le lunaire QUATRE fois et le catalogue n en avait aucune
    // mission ; `flight/Descent.hpp` attendait exactement ce consommateur.
    {
      const fen::mission::CatalogEntry* lune = nullptr;
      for (const auto& e : G.catalog.entries())
        if (e.contract.id == "CAT-12") lune = &e;
      CHECK(lune != nullptr, "10.1 : le catalogue porte enfin une mission cislunaire");
      CHECK(lune->contract.crewed && lune->contract.prerequisites.min_rank ==
                fen::career::Rank::Principal,
            "3.3 : le rang Principal est DEFINI par le vol habite cislunaire");
      CHECK(lune->contract.terms.crew_required == 3,
            "10.1 : trois a bord, comme Apollo");
      // Les CINQ verrous de la matrice 19.7 sont des prerequis reels.
      for (const char* v : {"lanceur_super_lourd", "eclss_habite", "radioprotection",
                            "automatisation_bord", "amarrage_habite"}) {
        bool trouve = false;
        for (const auto& t : lune->contract.prerequisites.required_tech)
          if (t == v) trouve = true;
        CHECK(trouve, "19.7 : chaque verrou de la matrice est un prerequis");
      }

      // LE CONTROLE QUI COMPTE : le budget post-LEO doit retrouver celui d Apollo
      // (~8,9 km/s : TLI 3050 + LOI 900 + descente 2050 + remontee 1850 + TEI 1000).
      // Le forfait n en donne que 5 000 ; le reste SORT de l integration.
      fen::mission::Mission ml;
      ml.contract = lune->contract;
      ml.state = fen::mission::MissionState::Qualification;
      G.missions.clear(); G.missions.push_back(ml);
      s.piloter_premiere_mission();
      s.mission_plan = fen::mission::MissionPlan{};
      s.evaluer_plan();
      const auto& AL = s.mission_plan.assessment;
      const double forfait = fen::mission::trajectory_dv_for_family("lunaire_habite");
      {   // Le T/W de l atterrisseur, sur sa masse REELLEMENT allumee.
        const double g_l = fen::cst::MU_MOON / (fen::cst::R_MOON * fen::cst::R_MOON);
        const auto& lp = s.mission_plan.pile.back().engine_part();
        const double ma = AL.m0_dernier_etage_kg;
        std::printf("     cislunaire : atterrisseur %s %.0f kN, masse allumee %.0f kg, "
                    "T/W lunaire %.2f\n",
                    lp.id, lp.thrust_vac_n / 1000.0, ma,
                    ma > 0.0 ? lp.thrust_vac_n / (ma * g_l) : 0.0);
        CHECK(ma > ml.contract.terms.payload_kg,
              "7.6 : la masse allumee comprend l etage et ses ergols, pas la seule charge");
      }
      std::printf("     cislunaire : forfait %.0f m/s + alunissage derive -> "
                  "Dv de conception %.0f m/s (Apollo post-LEO ~8900)\n",
                  forfait, AL.dv_design);
      CHECK(AL.dv_design > forfait + 2500.0,
            "7.6 : l alunissage AJOUTE un Delta-v derive, il n est pas dans le forfait");
      CHECK(AL.dv_design > 8000.0 && AL.dv_design < 9800.0,
            "7.6 : le total retrouve le budget post-LEO d Apollo (~8,9 km/s)");

      // ET IL DEPEND DU MOTEUR CHOISI — mais SEULEMENT dans le regime ou les
      // pertes de gravite existent. A T/W 12, le RL10 est deja au PLANCHER
      // IMPULSIONNEL (v_circ = 1680 m/s) : plus de poussee n y achete rien, et
      // c est la physique qui le dit, pas une limite du modele. Le comparer a un
      // RS-25 ne mesurerait donc rien. Ce qui mesure, c est un atterrisseur SOUS
      // -dimensionne en poussee.
      const double v_circ_l = std::sqrt(fen::cst::MU_MOON / fen::cst::R_MOON);
      const double dv_fort = AL.dv_design;
      CHECK(std::fabs((dv_fort - forfait - s.mission_plan.finite_loss)
                      - 2.0 * v_circ_l) < 5.0,
            "7.6 : a T/W eleve, l alunissage colle au plancher impulsionnel");
      // AJ10-190 : 26,7 kN, le moteur de manoeuvre orbitale de la navette. Sur un
      // atterrisseur habite, il est FAIBLE — et les pertes de gravite se paient.
      s.vehicule_design.stages.back().engine =
          fen::app::VehicleDesign::index_moteur("AJ10-190");
      s.evaluer_plan();
      const double dv_faible = s.mission_plan.assessment.dv_design;
      const double ma_faible = s.mission_plan.assessment.m0_dernier_etage_kg;
      std::printf("     cislunaire : RL10 102 kN -> %.0f m/s (plancher) | "
                  "AJ10 26,7 kN -> %.0f m/s (T/W %.2f, pertes de gravite %.0f m/s)\n",
                  dv_fort, dv_faible,
                  ma_faible > 0.0 ? 26700.0 / (ma_faible * (fen::cst::MU_MOON /
                      (fen::cst::R_MOON * fen::cst::R_MOON))) : 0.0,
                  0.5 * (dv_faible - dv_fort));
      CHECK(dv_faible > dv_fort + 50.0,
            "7.6 : un moteur trop faible paie des pertes de gravite REELLES");
      // Un moteur qui ne souleve pas l atterrisseur est REFUSE, et le refus le dit.
      for (auto& st : s.vehicule_design.stages)
        st.engine = fen::app::VehicleDesign::index_moteur("SPT-100");  // 83 mN
      s.evaluer_plan();
      CHECK(!s.mission_plan.assessment.ok &&
            s.mission_plan.assessment.why.find("SOULEVE") != std::string::npos,
            "6.3 : un propulseur de 83 mN ne pose pas un atterrisseur habite");
      s.vehicule_design = fen::app::VehicleDesign::starter();
    }

    // ═══════════════════════════════════════════════════════════════════════
    // (e1b) « PERTE DE CONFINEMENT = EVENEMENT CATASTROPHIQUE » [GDD 12.4]
    // ═══════════════════════════════════════════════════════════════════════
    // Le modele savait le dire et ne le faisait nulle part :
    // `antimatter_confinement_survival` n avait aucun appelant vivant. Le TAUX
    // n est pas invente ici — c est celui que le palier d antimatiere DECLARE
    // (`loss_rate_per_day`), deja calibre avec la fin de jeu. Une seconde
    // constante serait un nombre que personne n aurait calibre.
    {
      using namespace fen::reliability;
      const double j_ar = 8365.0;   // aller-retour Proxima, 22,9 ans terrestres
      // Le palier ABOUTI : 1e-5/j. Sur vingt-deux ans, c est un RISQUE, pas un mur.
      const double taux_abouti =
          fen::rel::AntimatterProduction::for_tier(fen::rel::AntimatterTier::Mature)
              .loss_rate_per_day;
      const double p_ar = 1.0 - antimatter_confinement_survival(j_ar, 1.0, taux_abouti);
      std::printf("     confinement 12.4 : palier abouti %.0e /j -> %.1f %% de perte sur "
                  "l aller-retour de 22,9 ans\n", taux_abouti, 100.0 * p_ar);
      CHECK(p_ar > 0.02 && p_ar < 0.30,
            "12.4 : le confinement de fin d arbre rend l aller-retour risque, pas impossible");
      // Et le palier D AUJOURD HUI (1e-2/j, le CERN) l interdit purement.
      const double taux_aujourdhui =
          fen::rel::AntimatterProduction::for_tier(fen::rel::AntimatterTier::Fission)
              .loss_rate_per_day;
      const double p_1an = 1.0 - antimatter_confinement_survival(365.0, 1.0, taux_aujourdhui);
      std::printf("     confinement 12.4 : palier d aujourd hui %.0e /j -> %.1f %% de perte "
                  "sur UN an\n", taux_aujourdhui, 100.0 * p_1an);
      CHECK(p_1an > 0.95,
            "12.4 : au palier d aujourd hui, un an de vol perd le confinement a coup sur");
      CHECK(taux_aujourdhui > taux_abouti,
            "5.12.12 : progresser dans la branche 6 AMELIORE le confinement");
      // Le risque CROIT avec la duree — c est un processus de Poisson, pas un seuil.
      CHECK(antimatter_confinement_survival(2.0 * j_ar, 1.0, taux_abouti) <
            antimatter_confinement_survival(j_ar, 1.0, taux_abouti),
            "12.4 : deux fois plus longtemps, deux fois plus d occasions de perdre");
    }

    // (e2) LA LIMITE DE DOSE PROTEGE UNE CARRIERE — PAS LE DERNIER VOL
    // ═══════════════════════════════════════════════════════════════════════
    // [GDD 6.6, 9.2] Elle est l'instrument qui protege un astronaute REUTILISABLE.
    // Sur le vol terminal — celui qu'on prend « lorsqu'il n'a plus de carriere a
    // construire » — elle protegeait une carriere qui n'existe plus, et elle
    // interdisait donc precisement le seul vol pour lequel tout le reste existe.
    // Elle reste opposable a TOUTE mission ordinaire.
    {
      Session sd;
      sd.nouvelle_partie("Oracle Dose", ModeAide::Normal);
      sd.tick(0.0);
      auto& Gd = *sd.jeu.ares.etat;
      Gd.dose_architecte.career_sv = 2.0;                 // deja consomme
      CHECK(Gd.dose_architecte.career_exceeded(),
            "6.6 : deux Sv de carriere depassent la limite institutionnelle");
      // Le RISQUE, lui, se lit toujours — c'est ce qui rend l acceptation informee.
      CHECK(Gd.dose_architecte.reid_career() > 0.03,
            "6.6 : ... et il se chiffre en REID, pas en booleen");
      std::printf("     dose : carriere 2,0 Sv -> REID %.1f %% ; limite %s\n",
                  100.0 * Gd.dose_architecte.reid_career(),
                  Gd.dose_architecte.career_exceeded() ? "depassee" : "tenue");
    }

    // ═══════════════════════════════════════════════════════════════════════
    // (f) LE VOL RELATIVISTE A ENFIN UNE ARRIVEE [GDD 3.4, 9.3]
    // ═══════════════════════════════════════════════════════════════════════
    // `window_target_for_family` ne nommait AUCUNE cible a la famille
    // « relativiste » : `transfer_tof_days` rendait 0, la croisiere restait
    // ouverte (`dated == false`) et le vol ne revenait jamais. Toute la chaine
    // antimatiere -> beta -> horloges existait pour un vol SANS DESTINATION.
    // Elle en a une (Proxima, fait mesure), et la duree en decoule.
    {
      fen::mission::Mission m;
      m.contract.id = "CAT-11";
      m.contract.family = "relativiste";
      m.contract.crewed = false;                 // sonde : deux poussees
      m.contract.terms = fen::mission::contract_terms_for_family("relativiste");
      m.state = fen::mission::MissionState::Launched;

      // Sans antimatiere, rien a dater — et le modele le DIT plutot que
      // d inventer une duree [GDD 6.8].
      CHECK(fen::mission::transfer_tof_days(
                m, fen::Epoch{s.jeu.epoch_courant()}, s.jeu.eph) == 0.0,
            "6.8 : sans antimatiere embarquee, la croisiere reste non datee");

      // AVEC L ARCHITECTURE HABITEE DE REFERENCE — la seule ou le relativisme a
      // un interet (decision de l utilisateur, 2026-07-29) : six personnes, en
      // aller-retour, donc quatre poussees [GDD 6.7.4].
      m.contract.crewed = true;
      m.beta_croisiere = fen::rel::beta_from_antimatter(
          fen::rel::AntimatterProduction::CALIB_DRY_MASS_KG, G.antimatiere.grams,
          fen::rel::AntimatterProduction::CALIB_BURNS);
      const double tof = fen::mission::transfer_tof_days(
          m, fen::Epoch{s.jeu.epoch_courant()}, s.jeu.eph);
      m.tof_days = tof;
      const double ans = tof / 365.25;
      std::printf("     relativiste : beta = %.3f -> Proxima en %.1f ans (%.0f j)\n",
                  m.beta_croisiere, ans, tof);
      CHECK(tof > 0.0, "3.4 : la mission relativiste a desormais une duree");
      // Elle ne peut pas battre la lumiere, et un aller-retour habite se compte
      // en DECENNIES terrestres [GDD 9.3] — c est ce qui justifie que l agence
      // tourne sous l adjoint pendant l absence.
      CHECK(ans > fen::rel::PROXIMA_DISTANCE_LY,
            "19.1 : ... et elle ne franchit pas la vitesse de la lumiere");
      const double rt_ans = 2.0 * ans;
      CHECK(rt_ans > 10.0 && rt_ans < 60.0,
            "9.3 : ... et l aller-retour habite dure plusieurs DECENNIES terrestres");
      // ET IL TIENT DANS UNE VIE [GDD 3.4] : c est la condition pour que l ecart
      // d age pese sur la carriere au lieu de la terminer.
      CHECK(rt_ans / fen::rel::lorentz_gamma(m.beta_croisiere) < 53.0,
            "3.4 : ... sans depasser ce qu il reste de vie a l architecte");
      // ET LA CHRONOLOGIE LA DATE : le gate d arrivee peut enfin chiffrer
      // l attente, ce qu il ne pouvait pas faire sur une croisiere ouverte.
      const fen::mission::FlightTimeline tl = fen::mission::build_flight_timeline(m);
      CHECK(tl.dated, "4.1 : la chronologie du vol relativiste est DATEE");
      CHECK(tl.duree_jours > 0.0, "4.1 : ... et sa duree est celle du transit");
      // Plus l architecture est lourde, plus le vol est long : la duree DECOULE
      // de l architecture [decision 10], elle n est pas une propriete du contrat.
      // Le comparant est DERIVE de la reference, pas ecrit en dur : le jour ou
      // l ancre bougera, l oracle suivra au lieu de s inverser en silence — ce
      // qui vient d arriver quand la reference est passee d une sonde de 5 t a
      // une architecture habitee de 183 t.
      fen::mission::Mission lourd = m;
      lourd.beta_croisiere = fen::rel::beta_from_antimatter(
          4.0 * fen::rel::AntimatterProduction::CALIB_DRY_MASS_KG,
          G.antimatiere.grams, fen::rel::AntimatterProduction::CALIB_BURNS);
      CHECK(lourd.beta_croisiere < m.beta_croisiere,
            "decision 10 : quadrupler la masse seche RALENTIT le vaisseau");
      CHECK(fen::mission::transfer_tof_days(
                lourd, fen::Epoch{s.jeu.epoch_courant()}, s.jeu.eph) > tof,
            "decision 10 : ... donc il met PLUS longtemps — beta decoule de l architecture");

      // (g) LE BETA FIGE SURVIT A LA SAUVEGARDE (V5). Le recalculer au
      // chargement rendrait un vol deja parti sensible au stock d aujourd hui.
      G.missions.push_back(m);
      fen::save::Writer w;
      G.save(w);
      fen::save::Reader r(w.bytes().data(), w.bytes().size());
      CHECK(r.version() >= 5, "sauvegarde : le schema est passe en V5");
      fen::game::GameState G6 = G;
      G6.missions.clear();
      CHECK(G6.load(r), "sauvegarde V5 : elle se relit");
      bool trouve = false;
      for (const auto& mm : G6.missions)
        if (mm.contract.id == "CAT-11" && mm.beta_croisiere > 0.0) {
          trouve = std::fabs(mm.beta_croisiere - m.beta_croisiere) < 1e-15;
          break;
        }
      CHECK(trouve, "6.7 : le beta fige au feu vert survit au rechargement");
    }
  }

  // ═══════════════════════════════════════════════════════════════════════════
  // 33. AUCUNE MISSION NE NAIT AVEC DES TERMES NULS [GDD 4.1]
  // ═══════════════════════════════════════════════════════════════════════════
  // Trouve EN AUDITANT UNE CAPTURE : le bloc « BILAN DE VIABILITE » du poste
  // CONTROLE affichait « 0 / 0 M EUR », « 0 / 0 mois », « P(SUCCES) 0,0 % » et un
  // VERROU rouge dans chaque image en vol. La table des termes vivait dans une
  // boucle interne a `seed_catalogue`, si bien que toute mission construite hors
  // catalogue naissait vide. Une alarme fausse dans chaque image est pire qu une
  // alarme absente : elle apprend a ne plus les lire.
  {
    // (a) LA TABLE COUVRE TOUTES LES FAMILLES DU CATALOGUE, et aucune ne rend
    // des termes nuls — y compris le repli, qui doit rester utilisable.
    Session s;
    s.nouvelle_partie("Oracle Termes", ModeAide::Normal);
    s.tick(0.0);
    fen::game::GameState& G = *s.jeu.ares.etat;
    bool tous_ok = true;
    for (const auto& e : G.catalog.entries()) {
      const fen::mission::Contract& t = e.contract.terms;
      if (!(t.payload_kg > 0.0 && t.budget_musd > 0.0 &&
            t.deadline_months > 0 && t.min_success_prob > 0.0))
        tous_ok = false;
      // ET LE CATALOGUE LIT BIEN LA MEME TABLE que tout le monde : c est la
      // propriete qui empeche les deux de rediverger demain.
      const fen::mission::Contract ref =
          fen::mission::contract_terms_for_family(e.contract.family);
      if (std::fabs(t.payload_kg - ref.payload_kg) > 1e-9 ||
          std::fabs(t.budget_musd - ref.budget_musd) > 1e-9) tous_ok = false;
    }
    CHECK(tous_ok,
          "4.1 : chaque entree du catalogue porte des termes non nuls, tires de LA table");

    // (b) UNE FAMILLE INCONNUE TOMBE SUR UN REPLI UTILISABLE, pas sur zero :
    // c est ce qui garantit qu une mission fabriquee a la main (harnais de
    // capture, contenu futur) reste evaluable par `assess`.
    const fen::mission::Contract inc =
        fen::mission::contract_terms_for_family("famille_inexistante");
    CHECK(inc.payload_kg > 0.0 && inc.budget_musd > 0.0 && inc.deadline_months > 0,
          "4.1 : une famille inconnue rend un repli EVALUABLE, jamais des zeros");

    // (c) ET LE BILAN DE VIABILITE DIT ALORS QUELQUE CHOSE. Avec des termes
    // nuls, `assess` verrouillait sur les trois axes a la fois quel que soit le
    // plan — le rouge ne portait aucune information.
    fen::mission::Mission mv;
    mv.contract.id = "TERMES-1";
    mv.contract.family = "sat";
    mv.contract.terms = fen::mission::contract_terms_for_family("sat");
    mv.state = fen::mission::MissionState::Design;
    G.missions.push_back(mv);
    s.piloter_premiere_mission();
    s.mission_plan.evaluate(*s.mission_courante());
    CHECK(s.mission_plan.assessment.cost_total > 0.0,
          "4.1 : avec de vrais termes, le bilan chiffre un cout");
    CHECK(s.mission_plan.assessment.p_success > 0.0,
          "4.1 : ... et une probabilite de succes non nulle");
    CHECK(s.mission_plan.assessment.m0_kg > 0.0,
          "4.1 : ... et une masse au decollage non nulle");
    // Le meme plan sur des termes NULS verrouille TOUT : la demonstration que
    // l ancienne capture ne montrait rien d exploitable. Le rouge y etait
    // permanent, donc sans information.
    fen::mission::Mission mz = mv;
    mz.contract.terms = fen::mission::Contract{};
    fen::mission::MissionPlan pz = s.mission_plan;
    pz.evaluate(mz);
    // LA MESURE CORRIGE CE QUE J AVAIS ECRIT ICI : ce ne sont pas les trois axes
    // qui tombent, ce sont EXACTEMENT ceux dont le terme nul est une BORNE
    // SUPERIEURE — budget (cout <= 0 : impossible) et calendrier (duree <= 0 :
    // impossible). La masse passe (un lanceur souleve toujours une charge nulle)
    // et le risque aussi (p_success >= 0 est toujours vrai). L axe MASSE ne
    // tombait dans la capture que parce que les consommables d un aller-retour
    // martien y ajoutaient 114 t — un fait de la mission, pas des termes.
    CHECK(!pz.assessment.fits_budget && !pz.assessment.fits_schedule,
          "4.1 : un terme nul qui est une BORNE SUPERIEURE est impossible a tenir");
    CHECK(pz.assessment.fits_mass && pz.assessment.fits_risk,
          "4.1 : ... alors que masse et risque passent — le rouge etait donc muet");
    CHECK(!pz.assessment.ok, "4.1 : verdict rouge, quel que soit le plan");

    // (d) UN ARRET PRECOCE NOMME SA CAUSE, il n enumere pas des symptomes.
    // TROUVE EN AUDITANT LA CAPTURE : un aller-retour martien habite pese 194 t
    // au decollage, aucun lanceur ne le souleve, `assess` s arrete la — et
    // `finalize` ecrasait « AUCUN LANCEUR NE SOULEVE CETTE MASSE » par
    // « MASSE BUDGET CALENDRIER RISQUE », dont trois quarts n avaient jamais ete
    // calcules. Un refus doit nommer ce qui manque (piege n°42).
    fen::mission::Contract enorme = fen::mission::contract_terms_for_family("sat");
    enorme.payload_kg = 5.0e6;                       // 5000 t : hors de tout lanceur
    fen::mission::Mission mm2 = mv;
    mm2.contract.terms = enorme;
    fen::mission::MissionPlan pm = s.mission_plan;
    pm.evaluate(mm2);
    CHECK(!pm.assessment.fits_mass, "4.1 : 5000 t ne decollent pas");
    // DEPUIS L ASSEMBLAGE ORBITAL, la cause exacte a change et elle est MEILLEURE :
    // ce n est plus « aucun lanceur » mais « le rendez-vous automatise n est pas
    // qualifie ». On epingle donc la PROPRIETE voulue et non la formulation —
    // le refus doit donner une DIRECTION, c est-a-dire nommer une techno.
    CHECK(pm.assessment.why.find("RECHERCHER") != std::string::npos ||
              pm.assessment.why.find("LANCEUR") != std::string::npos,
          "4.1 : ... et le refus NOMME la cause, il ne liste pas les symptomes");
    CHECK(pm.assessment.why.find("rdv_automatise") != std::string::npos,
          "5.2 : ... ici, que l assemblage orbital n est pas encore qualifie");
    CHECK(pm.assessment.why.find("CALENDRIER") == std::string::npos &&
              pm.assessment.why.find("RISQUE") == std::string::npos,
          "4.1 : ... sans accuser des axes que l etude n a jamais atteints");
    CHECK(pm.assessment.cost_total == 0.0 && pm.assessment.p_success == 0.0,
          "4.1 : ces axes valent zero parce qu ils sont INCONNUS, pas mauvais");
    CHECK(!pm.assessment.ok, "4.1 : le verdict reste NON, evidemment");
  }

  // ═══════════════════════════════════════════════════════════════════════════
  // 34. LE CATALOGUE EST-IL PHYSIQUEMENT REALISABLE ? [GDD 4.1, 5.4, 12.1]
  // ═══════════════════════════════════════════════════════════════════════════
  // AUDIT EXHAUSTIF, parce que « la plupart des contrats passent » n est pas une
  // reponse. Mesure de depart : avec TOUT l arbre qualifie et le rang Directeur,
  // CINQ contrats sur onze etaient bloques par le plafond des lanceurs (8 300 kg),
  // dont trois dont la charge NUE le depassait. Deux causes, toutes deux du
  // contenu NOMME mais non connecte :
  //   . `lanceur_super_lourd` etait un noeud d arbre, prerequis de CAT-09, qui ne
  //     debloquait AUCUN lanceur — un prerequis qui ment ;
  //   . les quatre noeuds « lanceur » de la branche 1 ne gardaient RIEN : toute la
  //     gamme etait disponible des la premiere mission.
  {
    Session s;
    s.nouvelle_partie("Oracle Catalogue", ModeAide::Normal);
    s.tick(0.0);
    fen::game::GameState& G = *s.jeu.ares.etat;

    // (a) CHAQUE LANCEUR EST ADOSSE A UN NOEUD DE L ARBRE, et chaque noeud
    // « lanceur » de l arbre debloque un lanceur. Sans cette bijection, soit un
    // prerequis ne debloque rien, soit un lanceur est gratuit.
    bool bijection = true;
    for (const auto& L : fen::mission::launchers()) {
      if (L.tech_id.empty()) { bijection = false; continue; }
      if (G.tree.find(L.tech_id) == nullptr) bijection = false;
    }
    CHECK(bijection, "5.4 : chaque lanceur est qualifie par un noeud QUI EXISTE");
    int n_noeuds = 0;
    for (const auto& n : G.tree.all())
      if (n.id.rfind("lanceur_", 0) == 0) ++n_noeuds;
    CHECK(n_noeuds == (int)fen::mission::launchers().size(),
          "5.4 : autant de lanceurs que de noeuds 'lanceur_' — aucun ne ment");

    // (b) LA BRANCHE 1 GARDE VRAIMENT. Sans le noeud, le lanceur n est pas
    // achetable — et le refus NOMME la techno a rechercher (piege n°42).
    fen::mission::Mission ml;
    ml.contract.id = "CATAL-1";
    ml.contract.family = "habite";                 // 13,3 t : demande le lourd
    ml.contract.crewed = true;
    ml.contract.terms = fen::mission::contract_terms_for_family("habite");
    ml.state = fen::mission::MissionState::Design;
    G.missions.push_back(ml);
    s.piloter_premiere_mission();
    if (auto* n = G.tree.find_mut("lanceur_lourd")) n->trl = 4;   // NON qualifie
    if (auto* n = G.tree.find_mut("lanceur_super_lourd")) n->trl = 4;
    s.evaluer_plan();
    CHECK(!s.mission_plan.assessment.fits_mass,
          "5.4 : sans le noeud, le lanceur qui souleverait n est pas achetable");
    CHECK(s.mission_plan.assessment.why.find("lanceur_lourd") != std::string::npos,
          "5.4 : ... et le refus NOMME la techno a rechercher (piege n°42)");
    // Et « non qualifie » se distingue de « aucun lanceur ne souleve » : une
    // seule des deux situations se resout en cherchant.
    CHECK(s.mission_plan.assessment.why.find("NON QUALIFIE") != std::string::npos,
          "5.4 : 'non qualifie' n est pas 'impossible' — deux verdicts distincts");
    if (auto* n = G.tree.find_mut("lanceur_lourd")) n->trl = fen::tech::TRL_OPERATIONAL;
    s.evaluer_plan();
    CHECK(s.mission_plan.assessment.fits_mass,
          "5.4 : la recherche aboutie DEBLOQUE reellement le vol");

    // (c) L INVARIANT DECLARE DE LA TABLE DES TERMES : « cale pour qu un plan
    // raisonnable au rang requis soit VIABLE ». Un contrat dont le budget ne
    // paie meme pas la FUSEE est du contenu casse — c etait le cas de CAT-09
    // (1 200 M$ de budget, 1 400 M$ de lanceur). On epingle le plancher
    // economique, qui ne depend d aucun choix de conception.
    for (auto& n : const_cast<std::vector<fen::tech::TechNode>&>(G.tree.all()))
      n.trl = fen::tech::TRL_OPERATIONAL;
    G.career.rank = fen::career::Rank::Directeur;
    int n_viables = 0, n_hors_portee = 0;
    bool plancher_ok = true;
    for (const auto& e : G.catalog.entries()) {
      fen::mission::Mission m;
      m.contract = e.contract;
      m.contract.terms = fen::mission::contract_terms_for_family(e.contract.family);
      m.state = fen::mission::MissionState::Qualification;
      G.missions.clear();
      G.missions.push_back(m);
      s.piloter_premiere_mission();
      s.mission_plan = fen::mission::MissionPlan{};
      s.evaluer_plan();
      const auto& A = s.mission_plan.assessment;
      if (!A.fits_mass) { ++n_hors_portee; continue; }
      ++n_viables;
      const double prix_fusee = fen::mission::launchers()[A.launcher_index].cost_musd;
      if (m.contract.terms.budget_musd < prix_fusee) {
        plancher_ok = false;
        std::printf("     catalogue : %s budget %.0f < lanceur %.0f M$ !\n",
                    e.contract.id.c_str(), m.contract.terms.budget_musd, prix_fusee);
      }
    }
    std::printf("     catalogue : %d contrats realisables en masse, %d hors de portee\n",
                n_viables, n_hors_portee);
    CHECK(plancher_ok,
          "4.1 : aucun contrat n a un budget inferieur au prix de sa fusee");
    // (d) ET LA MESURE FIXE LE NOMBRE, pour qu une regression se voie. DEUX
    // contrats restent hors de portee d un lancement UNIQUE, et les deux le sont
    // pour des raisons PHYSIQUES documentees, pas par oubli :
    //   . CAT-10 (cargo NEP, 152 t meme en 4 etages) : il faudrait un assemblage
    //     en orbite, que le GDD ne nomme pas — a ne pas inventer ;
    //   . CAT-11 (relativiste, 30 km/s) : 346 000 TONNES en chimique. C est
    //     Tsiolkovsky, et c est precisement pourquoi [GDD 19.3] reserve ce regime
    //     a l antimatiere.
    // AVANT L ASSEMBLAGE ORBITAL ils etaient DEUX ; CAT-10 (cargo NEP, 181 t)
    // est devenu realisable en deux tirs du super-lourd. Ne reste que CAT-11 :
    // 346 495 TONNES en chimique pour 30 km/s, soit plus de vingt lancements —
    // c est Tsiolkovsky, et c est pourquoi [GDD 19.3] reserve ce regime a
    // l antimatiere. Aucun assemblage ne rattrape une exponentielle.
    CHECK(n_hors_portee == 1,
          "4.1 : un SEUL contrat hors de portee, et pour raison physique [GDD 19.3]");
    CHECK(n_viables == (int)G.catalog.entries().size() - 1,
          "4.1 : ... tous les autres sont realisables en masse");

    // (e) ET L ARBITRAGE MASSE / PROTECTION / MISSION MORD POUR DE BON [GDD 6.6].
    // Le GDD le NOMME depuis toujours ; il ne coutait rien tant qu aucun plafond
    // de lanceur n existait. Mesure : sur un aller-retour martien habite, la
    // SEULE decision de blindage fait passer d une architecture lancable a une
    // architecture qui ne l est plus — le blindage pese en charge utile, et
    // Tsiolkovsky multiplie ce poids par le rapport de masse.
    fen::mission::Mission mm9;
    mm9.contract.id = "ARBITRAGE";
    mm9.contract.family = "mars_habite";
    mm9.contract.crewed = true;
    mm9.contract.terms = fen::mission::contract_terms_for_family("mars_habite");
    mm9.state = fen::mission::MissionState::Qualification;
    G.missions.clear();
    G.missions.push_back(mm9);
    s.piloter_premiere_mission();
    s.mission_plan = fen::mission::MissionPlan{};
    s.evaluer_plan();
    const auto A_nu = s.mission_plan.assessment;
    s.mission_plan.blindage = fen::env::Shielding{10.0, 1.0};
    s.evaluer_plan();
    const auto A_bl = s.mission_plan.assessment;
    std::printf("     arbitrage 6.6 : %.0f t / %d tir(s) / P=%.3f / %.0f M$"
                "   ->  %.0f t / %d tir(s) / P=%.3f / %.0f M$  (avec 10 g/cm2)\n",
                A_nu.m0_kg / 1000.0, A_nu.assemblage.n_lancements, A_nu.p_launcher,
                A_nu.cost_total,
                A_bl.m0_kg / 1000.0, A_bl.assemblage.n_lancements, A_bl.p_launcher,
                A_bl.cost_total);
    CHECK(A_bl.m0_kg > A_nu.m0_kg,
          "6.6 : le blindage pese en charge utile, et Tsiolkovsky le multiplie");
    // ═══ L ASSEMBLAGE A CHANGE LA NATURE DE L ARBITRAGE, PAS SA SEVERITE ═══
    // AVANT, proteger l equipage rendait le vol NON LANCABLE : une falaise. Le
    // vol reste desormais possible, mais il se paie — en TIRS, donc en RISQUE et
    // en ARGENT. Un arbitrage gradue vaut mieux qu un mur : le joueur peut
    // choisir combien de protection il achete, au lieu de buter dessus.
    CHECK(A_bl.fits_mass,
          "5.2 : l assemblage rend l architecture blindee LANCABLE");
    // CE QUI EST UNE LOI, ET CE QUI N EN EST PAS UNE. Blinder ajoute TOUJOURS de
    // la masse et TOUJOURS du cout ; le nombre de tirs, lui, ne monte que si la
    // campagne FRANCHIT un palier de lanceur. Ma premiere version exigeait un tir
    // de plus a tous les coups — c etait decrire l endroit ou la base se trouvait,
    // pas une propriete du modele. Monotonie ici, franchissement plus bas.
    CHECK(A_bl.cost_total > A_nu.cost_total,
          "6.6 : la protection se paie TOUJOURS. Elle ne se decrete pas");
    CHECK(A_bl.assemblage.n_lancements >= A_nu.assemblage.n_lancements,
          "6.6 : ... et elle ne fait JAMAIS baisser le nombre de tirs");
    CHECK(A_bl.p_launcher <= A_nu.p_launcher,
          "6.6 : ... ni monter la fiabilite du segment de mise en orbite");
    // LE FRANCHISSEMENT, DEMONTRE : un blindage assez lourd fait basculer la
    // campagne d un palier, et c est LA que l arbitrage devient exponentiel.
    s.mission_plan.blindage = fen::env::Shielding{30.0, 1.0};
    s.evaluer_plan();
    const auto A_lourd = s.mission_plan.assessment;
    std::printf("     franchissement : 30 g/cm2 -> %.0f t / %d tirs / P=%.3f\n",
                A_lourd.m0_kg / 1000.0, A_lourd.assemblage.n_lancements,
                A_lourd.p_launcher);
    CHECK(A_lourd.assemblage.n_lancements > A_nu.assemblage.n_lancements,
          "6.6 : un blindage lourd FRANCHIT un palier de campagne");
    CHECK(A_lourd.p_launcher < A_nu.p_launcher,
          "6.6 : ... et c est la que le risque monte, en R^N");
    // C est exactement l architecture martienne REELLE (DRA 5.0 : ~850 t, 7 a 9
    // lancements). Le modele retrouve la contrainte au lieu de la contourner.
  }

  // ═══════════════════════════════════════════════════════════════════════════
  // 35. ARES DIT OU ALLER ; L ARCHITECTE DIT COMMENT [GDD 3.1]
  // ═══════════════════════════════════════════════════════════════════════════
  // « L Architecte Mission decide COMMENT concevoir et conduire, dans des
  // enveloppes budgetaires imposees par ARES. Il ne fixe pas le budget. »
  // Le contrat portait pourtant `payload_kg = 20 000` pour Mars habite : une
  // masse d HABITAT, c est-a-dire une architecture deja choisie, posee par le
  // client. Elle est desormais DERIVEE de deux decisions d architecte (combien
  // d equipage, combien de volume chacun) et d un fait mesure.
  {
    using namespace fen::mission;
    // (a) LE FAIT : 137 kg/m3, les modules pressurises de l ISS. Trois modules,
    // trois agences, la meme densite a 1 % pres — ce n est pas un reglage.
    CHECK(std::fabs(MASSE_HABITAT_KG_PAR_M3 * 106.0 - 14515.0) < 200.0,
          "12.1 : la densite retrouve Destiny (14 515 kg / 106 m3)");
    CHECK(std::fabs(MASSE_HABITAT_KG_PAR_M3 * 75.0 - 10275.0) < 200.0,
          "12.1 : ... et Columbus (10 275 kg / 75 m3)");
    CHECK(std::fabs(MASSE_HABITAT_KG_PAR_M3 * 116.0 - 15900.0) < 200.0,
          "12.1 : ... et Kibo (15 900 kg / 116 m3)");

    // (b) L HABITAT EST UNE CONSEQUENCE, ET ELLE EST LINEAIRE EN CE QUI LA CAUSE.
    CHECK(std::fabs(masse_habitat_kg(6) - 6 * 25.0 * MASSE_HABITAT_KG_PAR_M3) < 1e-9,
          "3.1 : la masse d habitat se DEDUIT de l equipage et du volume");
    CHECK(masse_habitat_kg(12) > 1.99 * masse_habitat_kg(6),
          "3.1 : deux fois plus d equipage, deux fois plus de coque");
    CHECK(masse_habitat_kg(6, 12.5) < masse_habitat_kg(6, 25.0),
          "3.1 : serrer l habitat l allege — c est une DECISION, pas un forfait");
    CHECK(masse_habitat_kg(0) == 0.0, "3.1 : pas d equipage, pas d habitat");
    // ... et le blindage suit la MEME geometrie : un habitat serre a moins de
    // surface a proteger. Les deux decisions ne sont pas independantes.
    CHECK(masse_blindage_kg(6, 10.0, 12.5) < masse_blindage_kg(6, 10.0, 25.0),
          "6.6 : un habitat serre a moins de surface a blinder");

    // (c) LE CONTRAT NE PRESCRIT PLUS L ARCHITECTURE. Ce qu il garde est la
    // charge que le CLIENT fournit — la ou la masse EST l objectif.
    const Contract mars = contract_terms_for_family("mars_habite");
    CHECK(mars.payload_kg < masse_habitat_kg(crew_size_for_family("mars_habite")),
          "3.1 : la charge du CONTRAT est plus petite que l habitat qu elle exigeait");
    CHECK(mars.budget_musd > 0.0 && mars.deadline_months > 0 && mars.min_success_prob > 0.0,
          "3.1 : ce qu ARES impose reste l ENVELOPPE — argent, delai, fiabilite");

    // (c bis) L EFFECTIF EST PORTE PAR L OBJECTIF, plus par une table relue
    // partout. Deux contrats de la meme filiere peuvent donc demander des
    // equipages differents — c est ARES qui le dit, pas la famille.
    CHECK(mars.crew_required > 0,
          "3.1 : un contrat habite PORTE l effectif que l objectif demande");
    CHECK(contract_terms_for_family("sat").crew_required == 0,
          "3.1 : ... et un contrat robotique n en demande aucun");
    // ET TOUTE ENTREE DU CATALOGUE HABITEE EN PORTE UN. Un contrat marque
    // `crewed` avec zero personne a bord serait un equipage fantome : vivres
    // nuls, dose nulle, reserves inepuisables — exactement le defaut que cette
    // bascule a fait apparaitre dans quatre fixtures d oracles.
    {
      Session sc;
      sc.nouvelle_partie("Oracle Objectif", ModeAide::Normal);
      sc.tick(0.0);
      bool coherent = true;
      for (const auto& e : sc.jeu.ares.etat->catalog.entries())
        if (e.contract.crewed != (e.contract.terms.crew_required > 0)) coherent = false;
      CHECK(coherent,
            "3.1 : `crewed` et `crew_required` disent la MEME chose, pour les 11 contrats");
    }

    // (d) ET LE VOLUME PAR PERSONNE MORD SUR LA MASSE AU DECOLLAGE. C est le
    // test qui prouve que la decision d architecte traverse tout le modele
    // jusqu a Tsiolkovsky, au lieu de rester un champ decoratif.
    Session s;
    s.nouvelle_partie("Oracle Architecte", ModeAide::Normal);
    s.tick(0.0);
    fen::game::GameState& G = *s.jeu.ares.etat;
    for (auto& n : const_cast<std::vector<fen::tech::TechNode>&>(G.tree.all()))
      n.trl = fen::tech::TRL_OPERATIONAL;
    G.career.rank = fen::career::Rank::Directeur;
    Mission mh;
    mh.contract.id = "ARCHI-1";
    mh.contract.family = "mars_habite";
    mh.contract.crewed = true;
    mh.contract.terms = contract_terms_for_family("mars_habite");
    mh.state = MissionState::Qualification;
    G.missions.push_back(mh);
    s.piloter_premiere_mission();
    s.mission_plan = MissionPlan{};
    s.evaluer_plan();
    const double m0_large = s.mission_plan.assessment.m0_kg;
    const double hab_large = s.mission_plan.masse_habitat_kg_;
    s.mission_plan.volume_par_personne_m3 = 15.0;      // habitat serre
    s.evaluer_plan();
    const double m0_serre = s.mission_plan.assessment.m0_kg;
    std::printf("     architecte : 25 m3/pers -> %.1f t d habitat, %.0f t au decollage"
                "  |  15 m3/pers -> %.1f t, %.0f t\n",
                hab_large / 1000.0, m0_large / 1000.0,
                s.mission_plan.masse_habitat_kg_ / 1000.0, m0_serre / 1000.0);
    CHECK(hab_large > 0.0, "3.1 : une croisiere habitee emporte sa propre coque");
    CHECK(m0_serre < m0_large,
          "3.1 : le VOLUME choisi par l architecte change la masse au decollage");
    CHECK(s.mission_plan.masse_habitat_kg_ < hab_large,
          "3.1 : ... par l habitat, et par le blindage qui en depend");

    // (e) MAIS UN VOL NEAR-EARTH N EMPORTE PAS D HABITAT : il s amarre a une
    // station existante — c est tres exactement a ca qu une station sert. Le
    // critere est un FAIT deja calcule (y a-t-il un aller-retour date), pas une
    // liste de familles a tenir a jour.
    Mission ml;
    ml.contract.id = "ARCHI-2";
    ml.contract.family = "habite";
    ml.contract.crewed = true;
    ml.contract.terms = contract_terms_for_family("habite");
    ml.state = MissionState::Qualification;
    G.missions.clear();
    G.missions.push_back(ml);
    s.piloter_premiere_mission();
    s.mission_plan = MissionPlan{};
    s.evaluer_plan();
    CHECK(s.mission_plan.crew_round_trip_days == 0.0,
          "9.4 : un vol habite near-Earth n a pas d aller-retour synodique");
    CHECK(s.mission_plan.masse_habitat_kg_ == 0.0,
          "3.1 : ... il s amarre a une station, il n emporte pas sa maison");
    CHECK(s.mission_plan.vital.total_kg() > 0.0,
          "9.4 : ... mais il emporte bien ses consommables");
  }

  // ═══════════════════════════════════════════════════════════════════════════
  // 36. L ASSISTANCE GRAVITATIONNELLE EST UNE DECISION D ARCHITECTE [GDD 5.11]
  // ═══════════════════════════════════════════════════════════════════════════
  // La branche entiere (`mission/Assistance.hpp` + quatre modules d astro_core)
  // n avait d appelant que dans les oracles de PHYSIQUE : le catalogue n avait
  // aucune mission qu un tour puisse servir. CAT-13 est cette mission, et
  // `Session::choisir_tour` est la porte.
  {
    using namespace fen::mission;
    Session s;
    s.nouvelle_partie("Oracle Assistance", ModeAide::Normal);
    s.tick(0.0);
    fen::game::GameState& G = *s.jeu.ares.etat;
    for (auto& n : const_cast<std::vector<fen::tech::TechNode>&>(G.tree.all()))
      n.trl = fen::tech::TRL_OPERATIONAL;
    G.career.rank = fen::career::Rank::Directeur;

    // (a) LES REGLAGES DE FENETRE ETAIENT CEUX DE MARS, ET RIEN NE LE DISAIT.
    // Le transfert de Hohmann vers Jupiter dure 997 j ; les bornes par defaut
    // explorent 150 a 400 j. Le balayage butait sur son plafond et rendait un arc
    // de 400 jours a 17,6 km/s d injection.
    {
      const fen::Epoch t0{s.jeu.epoch_courant()};
      const auto pm = fen::mission::mission_window_params_for(
          fen::ephem::Body::EarthBary, fen::ephem::Body::Mars, s.jeu.eph, t0);
      const auto pj = fen::mission::mission_window_params_for(
          fen::ephem::Body::EarthBary, fen::ephem::Body::Jupiter, s.jeu.eph, t0);
      const auto def = fen::mission::mission_window_params();
      CHECK(pm.tof_min_days == def.tof_min_days && pm.tof_max_days == def.tof_max_days
                && pm.horizon_days == def.horizon_days && pm.slop_days == def.slop_days,
            "7.3 : MARS NE BOUGE PAS D UN BIT — les bornes par defaut la decrivent deja");
      CHECK(pj.tof_max_days > 1000.0,
            "7.3 : ... et Jupiter recoit des bornes qui contiennent son Hohmann (997 j)");
      CHECK(pj.tof_min_days > def.tof_min_days,
            "7.3 : ... derivees de la geometrie, pas d une seconde table");
      std::printf("     ASSISTANCE : fenetre Mars %.0f-%.0f j (defaut) | Jupiter %.0f-%.0f j"
                  " (derivee du Hohmann)\n",
                  pm.tof_min_days, pm.tof_max_days, pj.tof_min_days, pj.tof_max_days);
    }

    // (b) LE CONTRAT EXISTE ET IL EST REALISABLE.
    const fen::mission::CatalogEntry* src = nullptr;
    for (const auto& e : G.catalog.entries())
      if (e.contract.id == "CAT-13") src = &e;
    CHECK(src != nullptr, "10.1 : le catalogue porte l orbiteur du systeme solaire externe");
    if (src) {
      Mission m;
      m.contract = src->contract;
      m.state = MissionState::Design;
      G.missions.clear();
      G.missions.push_back(m);
      s.piloter_premiere_mission();
      s.mission_plan = MissionPlan{};
      s.mission_plan.program.dv_margin = 150.0;   // une marge de conception normale
      s.evaluer_plan();
      const double dv_direct = s.mission_plan.dv_traj_override;
      const double m0_direct = s.mission_plan.assessment.m0_kg;
      const double cout_direct = s.mission_plan.assessment.cost_total;
      const double tof_direct = s.duree_transit_jours(*s.mission_courante());
      CHECK(s.mission_plan.assessment.ok,
            "4.1 : CAT-13 est realisable en transfert direct");
      CHECK(dv_direct > 6000.0 && dv_direct < 12000.0,
            "7.3 : le Dv d un transfert direct vers Jupiter est de l ordre de 8-9 km/s");
      CHECK(tof_direct > 700.0 && tof_direct < 1300.0,
            "7.3 : ... et sa duree de l ordre du Hohmann (997 j)");

      // (c) LE TOUR EST OFFERT, ET SEULEMENT A QUI VA LA-BAS.
      CHECK(!s.tours_offerts(*s.mission_courante()).empty(),
            "5.11 : un vol vers Jupiter se voit offrir les tours qui y menent");
      {
        Mission mm;
        mm.contract.id = "TOUR-MARS";
        mm.contract.family = "mars";
        mm.contract.terms = contract_terms_for_family("mars");
        mm.state = MissionState::Design;
        G.missions.push_back(mm);
        const Mission& ref = G.missions.back();
        CHECK(s.tours_offerts(ref).empty(),
              "5.11 : un vol martien ne se voit PAS offrir un tour vers Jupiter");
        G.missions.pop_back();
      }

      // (d) LE TROC EST REEL, ET IL SE PAIE EN ANNEES.
      const bool pris = s.choisir_tour("E-E-J");
      CHECK(pris, "5.11 : le tour se choisit, et le modele en calcule les consequences");
      if (pris) {
        const double dv_tour = s.mission_plan.dv_traj_override;
        const double m0_tour = s.mission_plan.assessment.m0_kg;
        const double tof_tour = s.duree_transit_jours(*s.mission_courante());
        std::printf("     ASSISTANCE : CAT-13 direct %.0f m/s / %.1f t / %.0f M$ / %.0f j"
                    "  ->  E-E-J %.0f m/s / %.1f t / %.0f M$ / %.0f j\n",
                    dv_direct, m0_direct / 1000.0, cout_direct, tof_direct,
                    dv_tour, m0_tour / 1000.0, s.mission_plan.assessment.cost_total,
                    tof_tour);
        CHECK(dv_tour < dv_direct, "5.11 : le tour coute MOINS de Delta-v que le direct");
        CHECK(m0_tour < m0_direct, "6.1 : ... donc moins de masse au decollage (Tsiolkovsky)");
        CHECK(s.mission_plan.assessment.cost_total < cout_direct,
              "13.3 : ... donc un lanceur moins cher");
        CHECK(tof_tour > tof_direct * 1.5,
              "5.11 : ... et il se paie en ANNEES de vol, ce qui est tout son prix");
        CHECK(s.mission_courante()->tour_id == "E-E-J",
              "5.11 : le choix vit sur la MISSION, pas sur l ecran qui l a pris");
        // La duree est la MEME dans l evaluation du plan et au feu vert : un seul
        // endroit la decide (piege n°94).
        CHECK(std::fabs(s.mission_plan.env_mission.duree_vol_jours - tof_tour) < 1e-9,
              "12.4 : la duree qui use le vaisseau est celle du tour, pas celle du direct");
      }

      // (e) LES DEUX AXES DE [GDD 5.4] GARDENT VRAIMENT.
      s.choisir_tour("");
      CHECK(s.mission_courante()->tour_id.empty(),
            "5.11 : on revient au transfert direct, et le bilan est jete");
      if (auto* n = G.tree.find_mut("gravity_assist")) n->trl = 4;
      CHECK(!s.choisir_tour("E-E-J"),
            "5.4 : sans le noeud d arbre, l agence ne sait pas planifier d assistance");
      CHECK(s.tour_bilan.cause.find("gravity_assist") != std::string::npos,
            "5.4 : ... et le refus NOMME la techno a rechercher");
      if (auto* n = G.tree.find_mut("gravity_assist")) n->trl = fen::tech::TRL_OPERATIONAL;
      G.career.rank = fen::career::Rank::Junior;
      CHECK(!s.choisir_tour("E-E-J"),
            "3.2 : le RANG est l autre verrou, et il est distinct de la maturite");
      CHECK(s.tour_bilan.cause.find("Senior") != std::string::npos,
            "3.2 : ... le refus nomme le rang exige");
      G.career.rank = fen::career::Rank::Directeur;

      // (e2) LA TRACE DU TOUR EST CELLE DU TOUR [GDD 8.3, 17.3]
      // « Trajectoires, position et prochain noeud sont dessines DANS LE MONDE, a
      // leur position reelle. » Un vol qui passe par la Terre et met 4,8 ans ne
      // peut pas s afficher avec l arc direct de 893 jours : ce serait montrer une
      // trajectoire que personne ne vole. Les morceaux sont ceux que l optimiseur
      // a PARCOURUS, figes sur la mission au feu vert.
      CHECK(s.choisir_tour("E-E-J"), "5.11 : le tour se reprend pour la trace");
      if (s.tour_bilan_valide(*s.mission_courante())) {
        Mission mv;
        mv.contract = src->contract;
        mv.state = MissionState::Launched;
        mv.state_entered_days = G.clock.now_days();
        mv.tof_days = s.tour_bilan.tof_ans * 365.25;
        mv.tour_id = "E-E-J";
        for (const auto& a : s.tour_bilan.arcs) {
          Mission::TourArc ta;
          ta.r0[0] = a.r0.x; ta.r0[1] = a.r0.y; ta.r0[2] = a.r0.z;
          ta.v0[0] = a.v0.x; ta.v0[1] = a.v0.y; ta.v0[2] = a.v0.z;
          ta.t0_tdb = a.t0; ta.dt_s = a.dt;
          mv.tour_arcs.push_back(ta);
        }
        const FlightTrace tv = build_flight_trace(
            mv, G.clock.now_days(), s.jeu.epoch_courant(), s.jeu.eph);
        CHECK(tv.ok, "8.3 : un vol qui suit un tour a une trace");
        CHECK(tv.n_arcs == 4,
              "8.3 : deux morceaux par jambe — derive vers la manoeuvre profonde, puis arc");
        CHECK(tv.n_nodes == 5,
              "8.3 : cinq noeuds — depart, DSM, survol, DSM, arrivee");
        // LE CONTROLE QUI PROUVE QUE C EST BIEN UN SURVOL : au noeud du survol, le
        // vaisseau est A LA TERRE. Ce n est pas une tolerance de dessin — dans le
        // modele a coniques raccordees, le survol A LIEU a la position du corps.
        {
          const double t_fb = tv.nodes[2].t_days;
          const double tdb = s.jeu.epoch_courant()
                           + (t_fb - G.clock.now_days()) * fen::cst::DAY;
          const fen::Vec3 rT = s.jeu.eph.state(fen::ephem::Body::EarthBary,
                                               fen::ephem::Body::Sun,
                                               fen::Epoch{tdb}).r;
          const double d_km = norm(tv.nodes[2].pos - rT) / 1000.0;
          std::printf("     ASSISTANCE : au noeud de survol, le vaisseau est a %.0f km"
                      " de la Terre (%.2f ans apres le depart)\n",
                      d_km, (t_fb - tv.depart().t_days) / 365.25);
          CHECK(d_km < 1.0, "8.3 : le noeud de survol EST a la position de la Terre");
        }
        // LA POSITION SUIT LES MORCEAUX, pas le premier : apres le survol, propager
        // l etat de depart donnerait un vaisseau qui n est plus sur sa trajectoire.
        {
          FlightTrace t2 = tv;
          const double t_mid = 0.5 * (tv.nodes[2].t_days + tv.arrivee().t_days);
          trace_avancer(t2, t_mid);
          double d_min = 1e30;
          for (int k = 0; k < t2.n; ++k)
            d_min = std::min(d_min, norm(t2.pos - t2.traj[k]));
          CHECK(t2.sur_arc, "8.3 : a mi-chemin de la seconde jambe, le vol est sur son arc");
          CHECK(d_min < 5.0e9,
                "8.3 : ... et sa position est UN POINT de la polyligne tracee");
        }
        // ET LA NAVIGATION SE JUGE SUR LA PREMIERE VISEE, pas sur Jupiter dans
        // cinq ans : la matrice de transition n aurait aucune validite sur un tel
        // arc, et aucune correction ne se fait « pour dans cinq ans ».
        CHECK(tv.t_nav_fin_days < tv.arrivee().t_days,
              "8.4 : un tour vise d abord sa manoeuvre profonde, pas sa destination");
        CHECK(std::fabs(tv.t_nav_fin_days
                        - (tv.depart().t_days + tv.arcs[0].dt_days)) < 1e-9,
              "8.4 : ... c est-a-dire la fin du premier morceau");
      }

      // (e3) LE SURVOL SE VISE DANS LE PLAN-B [GDD 8.4, 8.5]
      // `astro/BPlane.hpp` etait le quatrieme en-tete mort de la serie, et son
      // commentaire disait a quoi il devait servir : « l ellipse de dispersion
      // superposee au corridor admissible EST l interface de la sanction ». Il n
      // avait aucun appelant parce que le jeu n avait aucun survol.
      s.evaluer_plan();
      if (s.nav_survol_.ok) {
        const auto& sv = s.nav_survol_;
        std::printf("     ASSISTANCE : survol — b vise %.0f km, corridor %.0f km,"
                    " derniere correction %.1f m/s, residu %.0f km -> P(survol) %.4f\n",
                    sv.b_vise_m / 1000.0, sv.demi_corridor_m / 1000.0,
                    sv.dv_derniere_corr, sv.sigma_b_m / 1000.0, sv.p_survol);
        CHECK(sv.b_vise_m > sv.b_limite_m,
              "8.4 : le parametre d impact vise est AU-DESSUS de celui qui fait rentrer");
        // LE CORRIDOR EST BORNE PAR L ATMOSPHERE, pas par une regle de jeu : la
        // conversion b <-> rp est exacte (conservation de h et de l energie), et
        // on la verifie en revenant.
        {
          const double vinf = s.tour_bilan.vinf_survol_ms[0];
          const double rp = fen::astro::rp_from_b(sv.b_vise_m, vinf, fen::cst::MU_EARTH);
          CHECK(std::fabs(rp - s.tour_bilan.rp_survol_m[0]) < 1.0,
                "8.4 : b -> rp -> b est une identite, pas une approximation");
          const double rp_lim = fen::astro::rp_from_b(sv.b_limite_m, vinf, fen::cst::MU_EARTH);
          CHECK(std::fabs(rp_lim - (fen::cst::R_EARTH + 122000.0)) < 1000.0,
                "8.4 : le bord du corridor EST l interface atmospherique (122 km)");
        }
        CHECK(sv.p_survol > 0.0 && sv.p_survol <= 1.0,
              "8.4 : P(survol) est une probabilite, jamais un verdict binaire");
        // ET ELLE ENTRE DANS P(NAVIGATION) : viser un survol est une exigence de
        // PLUS, pas la meme exigence.
        CHECK(s.mission_plan.p_physics <= s.nav_disp.p_marge + 1e-12,
              "8.4 : un tour ne peut pas etre plus sur qu un vol qui n a rien a viser");
        // ET L ARCHITECTE PEUT EXIGER PLUS HAUT [GDD 3.1] — c est LA decision,
        // puisque l optimiseur colle toujours le periastre a sa borne basse.
        {
          const double rp_defaut = s.tour_bilan.rp_survol_m[0];
          const double p_defaut = s.nav_survol_.p_survol;
          s.alt_survol_min_km = 2000.0;
          const bool ok2 = s.choisir_tour("E-E-J");
          s.evaluer_plan();
          CHECK(ok2, "3.1 : un plancher de survol plus haut reste faisable");
          if (ok2) {
            std::printf("     ASSISTANCE : plancher %s -> rp %.0f km, P %.4f ;"
                        " plancher 2000 km -> rp %.0f km, P %.4f\n",
                        "du vol reel", (rp_defaut - fen::cst::R_EARTH) / 1000.0,
                        p_defaut,
                        (s.tour_bilan.rp_survol_m[0] - fen::cst::R_EARTH) / 1000.0,
                        s.nav_survol_.p_survol);
            CHECK(s.tour_bilan.rp_survol_m[0] > rp_defaut,
                  "3.1 : ... et le survol trouve respecte le plancher exige");
            // ⚠ ON N EXIGE PAS ICI que P monte : relancer l optimiseur avec un
            // autre plancher rend un AUTRE tour (autre v_inf, autre date, autre
            // dispersion), et comparer deux vols differents ne prouverait rien.
            // La pente du modele se verifie a v_inf FIXE, juste en dessous.
            CHECK(s.nav_survol_.ok || s.tour_bilan.faisable,
                  "8.5 : ... et le tour releve reste evaluable de bout en bout");
          }
          s.alt_survol_min_km = 0.0;
          s.choisir_tour("E-E-J");
          s.evaluer_plan();
        }
        // LE MODELE A UNE PENTE, ET ELLE VA DANS LE BON SENS : un survol plus BAS
        // laisse moins de corridor, donc moins de chances. On le verifie sur le
        // modele pur, sans retoucher la mission.
        {
          const fen::mission::FlightTrace tr = s.trace_prospective(*s.mission_courante());
          const double vinf = s.tour_bilan.vinf_survol_ms[0];
          const double rp_lim = fen::cst::R_EARTH + 122000.0;
          const auto bas = fen::mission::nav_survol(
              tr, s.nav_disp, 1, rp_lim + 50000.0, rp_lim, vinf, fen::cst::MU_EARTH);
          const auto haut = fen::mission::nav_survol(
              tr, s.nav_disp, 1, rp_lim + 2000000.0, rp_lim, vinf, fen::cst::MU_EARTH);
          if (bas.ok && haut.ok) {
            std::printf("     ASSISTANCE : survol a +50 km d altitude -> P %.4f ;"
                        " a +2000 km -> P %.4f\n", bas.p_survol, haut.p_survol);
            CHECK(bas.p_survol < haut.p_survol,
                  "8.4 : raser le corps est plus RISQUE que passer large — la pente existe");
          }
        }
      }

      // (f) LE CHOIX SURVIT A UNE SAUVEGARDE (V6).
      CHECK(s.choisir_tour("E-E-J"), "5.11 : le tour se reprend apres un refus");
      const std::string avant = s.mission_courante()->tour_id;
      fen::save::Writer w;
      G.save(w);
      fen::save::Reader r(w.bytes().data(), w.bytes().size());
      // MÊME précondition que les autres oracles de sauvegarde : le catalogue est
      // reconstruit par la graine AVANT le chargement.
      fen::game::GameState G2 = G;
      G2.missions.clear();
      G2.load(r);
      bool retrouve = false;
      for (const auto& mm : G2.missions)
        if (mm.contract.id == "CAT-13" && mm.tour_id == avant) retrouve = true;
      CHECK(retrouve, "18 : le tour figé se recharge — une trajectoire n est pas un reglage d ecran");
    }
  }

  // ═══════════ LE SCORE A TROIS CRITÈRES [GDD 3.3] ═════════════════════════
  // « Score cumulé à PONDÉRATION ÉGALE de trois critères. » Le code n'en portait
  // qu'un — « +40 par réussite, −10 par échec », compté sur les compteurs de
  // l'agence —, si bien que dépenser deux fois son enveloppe ou perdre un
  // équipage par impréparation ne pesait RIEN sur la carrière.
  {
    using fen::career::MissionBilan;
    using fen::career::score_mission;
    using fen::career::POINTS_PAR_MISSION;

    // --- (a) LA PONDÉRATION EST ÉGALE, ET C'EST VÉRIFIABLE -----------------
    // Aucun critère ne peut peser plus qu'un autre : à note extrême égale, les
    // trois déplacent le total de la même quantité.
    MissionBilan neutre;
    neutre.succes = true;
    neutre.budget_contrat_musd = 1000.0;
    neutre.cout_engage_musd = 1000.0;   // enveloppe tenue de justesse : note nulle
    neutre.gravite = 2;                 // une anomalie modérée : crise à 0
    const double t0 = score_mission(neutre).total();
    MissionBilan mieux_budget = neutre; mieux_budget.cout_engage_musd = 750.0;
    MissionBilan mieux_crise = neutre;  mieux_crise.gravite = 0;
    const double d_budget = score_mission(mieux_budget).total() - t0;
    const double d_crise = score_mission(mieux_crise).total() - t0;
    CHECK(std::fabs(d_budget - d_crise) < 1e-9,
          "3.3 : ponderation EGALE — un point de budget vaut un point de crise");
    CHECK(std::fabs(d_budget - POINTS_PAR_MISSION / 3.0) < 1e-9,
          "3.3 : chaque critere vaut au plus un tiers du bareme");

    // --- (b) LA CALIBRATION NE BOUGE PAS ----------------------------------
    // Une mission NOMINALE — réussie, dans son enveloppe avec de la marge, sans
    // anomalie — vaut toujours 40 points, comme le comptage d'avant. Les seuils
    // de promotion gardent donc exactement le sens qu'ils avaient.
    MissionBilan nominale;
    nominale.succes = true;
    nominale.budget_contrat_musd = 1000.0;
    nominale.cout_engage_musd = 600.0;   // 40 % de marge : note pleine
    const fen::career::MissionScore sn = score_mission(nominale);
    CHECK(std::fabs(sn.total() - 40.0) < 1e-9,
          "3.3 : une mission nominale vaut toujours 40 points (calibration tenue)");
    CHECK(sn.reussite == 1.0 && sn.budget == 1.0 && sn.crise == 1.0,
          "3.3 : ... et c est parce que les trois criteres sont pleins");

    // --- (c) CHAQUE CRITÈRE MORD, ET SEUL LE SIEN --------------------------
    MissionBilan gouffre = nominale;
    gouffre.cout_engage_musd = 1600.0;    // 60 % de dépassement
    const fen::career::MissionScore sg = score_mission(gouffre);
    CHECK(sg.reussite == 1.0 && sg.crise == 1.0,
          "3.3 : un depassement budgetaire ne touche PAS les deux autres criteres");
    CHECK(sg.budget == -1.0, "3.3 : ... et il coute la note pleine en negatif");
    CHECK(sg.total() < sn.total(),
          "3.3 : une mission reussie mais ruineuse rapporte MOINS qu une mission tenue");
    std::printf("     SCORE 3.3 : nominale %+.1f pts | ruineuse (+60 %% de budget) %+.1f | "
                "perdue %+.1f\n",
                sn.total(), sg.total(),
                [&]{ MissionBilan p = nominale; p.succes = false; p.gravite = 4;
                     return score_mission(p).total(); }());

    // --- (d) LA GESTION DE CRISE SE MESURE SUR CE QU'ON A FAIT ------------
    // « Qualité de la RÉPONSE aux anomalies. » Deux vols identiques, dont un où
    // tout ce qui est tombé a été réparé : ce n'est pas la même carrière.
    MissionBilan subi;
    subi.succes = true; subi.budget_contrat_musd = 1000.0; subi.cout_engage_musd = 600.0;
    subi.gravite = 2; subi.avaries_subies = 4; subi.avaries_reparees = 0;
    MissionBilan sauve = subi; sauve.avaries_reparees = 4;
    CHECK(score_mission(sauve).crise > score_mission(subi).crise,
          "3.3 : reparer ce qui tombe AMELIORE la gestion de crise");
    CHECK(std::fabs((score_mission(sauve).crise - score_mission(subi).crise) - 0.5) < 1e-9,
          "10.3 : tout reparer vaut exactement le DEMI-PALIER du bareme");
    CHECK(score_mission(subi).crise < 1.0,
          "3.3 : une mission qui a connu des anomalies n a pas la note pleine");

    // --- (e) ET LE DEMI-PALIER DE [10.3] EXISTE POUR DE BON ----------------
    // `brilliant_recovery` était sauvegardé et POSÉ PAR PERSONNE : le seul
    // modificateur adoucissant du barème ne pouvait jamais s appliquer.
    {
      fen::mission::SeverityModifiers m0;
      const fen::mission::Severity base = fen::mission::Severity::Major;
      m0.brilliant_recovery = true;
      CHECK(static_cast<int>(fen::mission::apply_modifiers(base, m0)) <
                static_cast<int>(base),
            "10.3 : le sauvetage brillant retrograde d un demi-palier");
      fen::mission::SeverityModifiers m1;
      CHECK(fen::mission::apply_modifiers(base, m1) == base,
            "10.3 : ... et sans lui la gravite ne bouge pas");
    }
  }

  // ═══════════ LA PASSATION [GDD 3.4, 3.5, décisions 6 et 7] ═══════════════
  // « Trois fins de partie seulement : mort naturelle (ouvre une PASSATION),
  // mort opérationnelle (Game Over sec), licenciement. » Le personnage
  // vieillissait et pouvait mourir ; `natural_death_due()` et
  // `career::Succession` n'avaient AUCUN lecteur. Un Architecte de 120 ans
  // gardait son poste, un Architecte mort aussi, et la portée
  // multi-générationnelle que [3.5] exige pour finir la branche 6 n'existait pas.
  {
    Session s;
    s.nouvelle_partie("PASSATION", ModeAide::Normal);
    s.chemin_sauvegarde = tmp + "/oracle_passation.sav";
    s.tick(0.0);                      // `AresLayer::assurer` cree l etat ARES
    CHECK(s.jeu.ares.initialisee(), "passation : la couche ARES est prete");
    auto& G = *s.jeu.ares.etat;

    CHECK(G.generation == 1, "passation : la partie commence a la 1re generation");
    CHECK(!s.passation_en_attente(), "passation : rien a passer au depart");
    const double age0 = G.character.age_bio_years();
    CHECK(std::fabs(age0 - fen::career::ENTRY_AGE_Y) < 1e-9,
          "3.4 : l Architecte entre en fonction a 32 ans");

    // --- (a) LE TEMPS FAIT SON OFFICE, ET LA FIN DE VIE SE CONSTATE ---------
    // On ne pose aucun drapeau : on fait COULER le temps, exactement comme le
    // joueur qui accélère. La mort doit arriver toute seule.
    // ⚠ `avancer_temps` DÉPLACE LE CALENDRIER ; la couche ARES (donc le
    // vieillissement) ne rattrape qu au tick suivant — c est `assurer` qui
    // appelle `avancer`. Le jeu le fait a chaque frame ; un oracle doit le dire.
    auto couler = [&s](double jours) {
      s.jeu.avancer_temps(jours);
      s.jeu.ares.assurer(s.jeu.agence, s.jeu.epoch_courant());
    };
    // ⚠ ON NE PEUT PAS SIMPLEMENT LAISSER COULER CINQUANTE-TROIS ANS, et le
    // modele a raison de l interdire : une agence qui n entreprend rien fait
    // FAILLITE en six ans [GDD 13.2, 14.2] — mesure ici meme —, si bien que le
    // calendrier s arrete bien avant la fin de vie. C est la TROISIEME issue de
    // [GDD 3.4], le licenciement, et elle est deja sous oracle ailleurs. On pose
    // donc l AGE, qui est un fait du personnage, et on laisse le temps le
    // constater — la meme doctrine que les drapeaux de capture.
    couler(120.0);
    CHECK(G.character.age_bio_years() > age0,
          "3.4 : le temps qui passe VIEILLIT l Architecte");
    G.character.age_bio_s = (fen::career::LIFE_EXPECTANCY_Y - 0.4) * fen::career::YEAR_S;
    couler(30.0);
    CHECK(G.character.alive && !G.passation_ouverte,
          "3.4 : a 84,7 ans l Architecte est toujours en fonction");
    couler(200.0);
    CHECK(!G.character.alive, "3.4 : la mort naturelle survient vers 85 ans");
    CHECK(G.passation_ouverte, "3.5 : et elle OUVRE une passation");
    CHECK(!G.character.operational_death,
          "3.4 : une mort naturelle n est pas une mort operationnelle");
    CHECK(s.passation_en_attente(), "passation : la session la propose");
    std::printf("     PASSATION : fin de vie a %.1f ans apres %.1f ans de fonction — %s\n",
                G.character.age_bio_years(),
                G.character.age_bio_years() - fen::career::ENTRY_AGE_Y,
                G.passation_motif.c_str());

    // --- (b) LA MODALE SE POSE, ET CE N EST PAS UN GAME OVER ---------------
    s.tick(0.016);
    CHECK(s.modal == Modal::Passation, "3.4 : l ecran constate la passation");
    CHECK(!s.jeu.game_over, "3.4 : une mort naturelle ne termine PAS la partie");

    // --- (c) CE QUI SE TRANSMET, ET CE QUI NE SE TRANSMET PAS --------------
    // On monte le rang et on abîme la confiance AVANT, pour que les deux
    // colonnes du tableau de [3.5] se distinguent l'une de l'autre.
    G.career.rank = fen::career::Rank::Principal;
    G.career.confidence_ares = 31.0;
    G.career.score = 4242.0;
    G.dose_architecte.add_chronic(3.0);
    G.notebook.write({"note du predecesseur", "corps", 12.0, ""});
    const std::size_t notes_avant = G.notebook.entries.size();
    const double tresorerie_avant = G.finance.treasury_me;
    const std::size_t noeuds_avant = G.tree.all().size();

    CHECK(s.passer_la_main(), "3.5 : la passation s execute");
    CHECK(G.generation == 2, "3.5 : le poste change de titulaire");
    CHECK(G.career.rank == fen::career::Rank::Principal,
          "decision 6 : le RANG est conserve — c est un droit du poste");
    CHECK(std::fabs(G.career.confidence_ares - 70.0) < 1e-12,
          "decision 7 : la confiance PERSONNELLE repart a 70");
    CHECK(G.career.score == 0.0, "3.5 : le score est personnel, il ne se legue pas");
    CHECK(G.dose_architecte.career_sv == 0.0,
          "6.6 : la dose est celle d un CORPS — le successeur repart neuf");
    CHECK(G.character.alive && !G.character.operational_death,
          "3.5 : le successeur est vivant");
    CHECK(std::fabs(G.character.age_bio_years() - fen::career::ENTRY_AGE_Y) < 1e-9,
          "3.5 : le successeur entre en fonction au meme age");
    CHECK(G.notebook.entries.size() == notes_avant + 1,
          "3.5 : le CARNET est transmis (et la passation s y ecrit)");
    CHECK(std::fabs(G.finance.treasury_me - tresorerie_avant) < 1e-12,
          "3.5 : l etat programmatique passe INTEGRALEMENT (finances)");
    CHECK(G.tree.all().size() == noeuds_avant,
          "3.5 : ... et l acces technologique, qui appartient a ARES");
    CHECK(!G.passation_ouverte && !s.passation_en_attente(),
          "passation : une fois faite, elle se referme");
    CHECK(s.modal != Modal::Passation, "passation : la modale se leve");

    // --- (d) ET ELLE SURVIT A UNE SAUVEGARDE (V9) --------------------------
    // Sans cela, quitter pendant la modale RESSUSCITERAIT le defunt.
    Session s2;
    s2.nouvelle_partie("PASSATION2", ModeAide::Normal);
    s2.chemin_sauvegarde = tmp + "/oracle_passation2.sav";
    s2.tick(0.0);
    auto& G2 = *s2.jeu.ares.etat;
    G2.character.age_bio_s = 90.0 * fen::career::YEAR_S;
    s2.jeu.avancer_temps(40.0);
    s2.jeu.ares.assurer(s2.jeu.agence, s2.jeu.epoch_courant());
    CHECK(G2.passation_ouverte, "3.4 : la fin de vie est constatee au tick");
    s2.sauvegarder_partie();
    Session s3;
    CHECK(s3.charger_partie(s2.chemin_sauvegarde), "passation : la partie se recharge");
    if (s3.jeu.ares.initialisee()) {
      auto& G3 = *s3.jeu.ares.etat;
      CHECK(G3.passation_ouverte && !G3.character.alive,
            "V9 : le defunt ne ressuscite pas au rechargement");
      CHECK(G3.generation == 1, "V9 : la generation survit");
      CHECK(s3.passer_la_main() && G3.generation == 2,
            "V9 : la passation se conclut apres rechargement");
    }

    // --- (e) L INVARIANT QUI PROTEGE LE GAME OVER --------------------------
    // « Une mort operationnelle reste un Game Over — la passation ne l annule
    // JAMAIS » [GDD 3.5]. C est l invariant que `Career.hpp` declare en tete.
    Session s4;
    s4.nouvelle_partie("PASSATION3", ModeAide::Normal);
    s4.chemin_sauvegarde = tmp + "/oracle_passation3.sav";
    s4.tick(0.0);
    auto& G4 = *s4.jeu.ares.etat;
    G4.character.alive = false;
    G4.character.operational_death = true;
    s4.jeu.avancer_temps(365.25);
    s4.jeu.ares.assurer(s4.jeu.agence, s4.jeu.epoch_courant());
    CHECK(!G4.passation_ouverte,
          "3.5 : un deces OPERATIONNEL n ouvre aucune passation");
    CHECK(!s4.passation_en_attente() && !s4.passer_la_main(),
          "3.5 : ... et la session refuse de la faire");
    CHECK(!fen::career::Succession::allowed(G4.character),
          "3.5 : l invariant est celui que Career.hpp declare");
  }

  std::printf("\nSESSION : %d oracles OK, %d en echec.\n", g_ok, g_ko);
  return g_ko == 0 ? 0 : 1;
}

#endif // SP_STANDALONE_TESTS
