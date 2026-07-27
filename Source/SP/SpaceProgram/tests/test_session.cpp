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
    // en axes de rendu (miroir en y sur le repere station).
    const Session::PoseBord pb = s.pose_bord();
    const double ox = pb.dist_km * std::cos(pb.pitch) * std::cos(pb.yaw);
    const double oy = pb.dist_km * std::cos(pb.pitch) * std::sin(pb.yaw);
    const double oz = pb.dist_km * std::sin(pb.pitch);
    CHECK(std::fabs(ox - NOVELLUS_OEIL_M[0] / 1000.0) < 1e-9 &&
          std::fabs(oy + NOVELLUS_OEIL_M[1] / 1000.0) < 1e-9 &&
          std::fabs(oz - NOVELLUS_OEIL_M[2] / 1000.0) < 1e-9,
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
    // la reprise invisible.
    CHECK(std::fabs(g_render_bridge.cam.dist_km.load() - pb.dist_km) < 1e-9 &&
          std::fabs(g_render_bridge.cam.yaw.load() - pb.yaw) < 1e-9 &&
          std::fabs(g_render_bridge.cam.pitch.load() - pb.pitch) < 1e-9,
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
    CHECK(std::fabs(pv.dist_km - std::sqrt(12.0 * 12.0 + 4.0 + 0.25) / 1000.0) < 1e-12,
          "handoff : la pose suit l oeil VIVANT du pawn");
    CHECK(std::fabs(pv.dist_km * std::cos(pv.pitch) * std::sin(pv.yaw) + 0.002) < 1e-9,
          "handoff : miroir en y applique a l oeil vivant");
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
    CHECK(fen::mission::flight_phase_of(m, 100.0 + asc_j * 1.01) == FlightPhase::TransferCruise,
          "tempo : passee l ascension, une mission lointaine croise");
    m.contract.family = "sat";
    CHECK(fen::mission::flight_phase_of(m, 100.0 + asc_j * 1.01) == FlightPhase::LeoOps,
          "tempo : passee l ascension, une mission proche opere en LEO");

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

  std::printf("\nSESSION : %d oracles OK, %d en echec.\n", g_ok, g_ko);
  return g_ko == 0 ? 0 : 1;
}

#endif // SP_STANDALONE_TESTS
