// spr/MenuView.hpp
//
// ECRAN TITRE + MENUS : canal de presentation ADDITIF, branche comme MapView /
// StationView. L'APP possede le MenuView, fait avancer sa PROPRE machine a etats,
// et lit les *_request apres chaque render. Le HUD (Hud::build) ne fait que
// DESSINER l'ecran courant et POSER des requetes -- il ne connait pas le jeu.
//
// Flux (cf. Plan) :
//   Title  --[Nouvelle partie]--> Difficulty --[Lancer]--> (l'app demarre la partie)
//   Title  --[Reprendre]-------->  Saves      --[Charger]--> (l'app charge la partie)
//   *      --[Retour]----------->  Title
//   Title  --[Quitter]---------->  (l'app ferme la fenetre)
#pragma once
#include <cstdint>

namespace spr {

enum class MenuScreen {
  Title,       // ecran titre : NOUVELLE PARTIE / REPRENDRE / QUITTER
  Difficulty,  // saisie du nom de l'agence + choix NORMAL / PRO, puis LANCER
  Saves,       // liste des sauvegardes -> reprise d'une partie
};

// Une entree de la liste des sauvegardes (remplie par l'app avant d'afficher Saves).
struct MenuSaveItem {
  char label[112];   // resume lisible : nom d'agence + calendrier + reussites
  char path[260];    // chemin du fichier de sauvegarde (.sav)
};

struct MenuView {
  MenuScreen screen{MenuScreen::Title};

  // --- saisie "Nouvelle partie" (edite par le HUD) ---------------------------
  char agency_name[64]{"NOUVELLE AGENCE"};
  int  difficulty{0};        // 0 = NORMAL (assistant) ; 1 = PRO (sans aide)

  // --- liste des sauvegardes (remplie par l'app, lue par le HUD) -------------
  const MenuSaveItem* saves{nullptr};
  int save_count{0};
  int save_selected{-1};     // ligne surlignee (edite par le HUD)

  // --- REQUETES : posees par le HUD, LUES puis remises a zero par l'app -------
  bool go_new_game{false};   // Title  -> Difficulty
  bool go_saves{false};      // Title  -> Saves (l'app rescanne le dossier)
  bool go_back{false};       // retour a Title
  bool start_game{false};    // Difficulty : lancer avec agency_name + difficulty
  int  load_index{-1};       // Saves     : reprendre saves[load_index]
  bool quit{false};          // fermer le jeu

  // Remet toutes les requetes a zero (appele par l'app apres les avoir traitees).
  void clear_requests() {
    go_new_game = go_saves = go_back = start_game = quit = false;
    load_index = -1;
  }
};

} // namespace spr
