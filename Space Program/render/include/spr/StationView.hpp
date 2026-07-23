// spr/StationView.hpp
//
// Canal ADDITIF de la "vue station" : l'INTERIEUR de l'ISS, QG jouable du joueur.
// Meme doctrine que MapView (cf. spr/MapView.hpp) : c'est de la PRESENTATION PURE,
// composee par le point d'entree (composition root). Il ne touche NI la physique
// NI le RenderSnapshot. L'interieur de l'ISS n'a AUCUNE physique : ce ne sont que
// des maillages placeholder (cylindres = modules, boites = consoles/ecrans/postes)
// disposes dans un petit repere local metrique.
//
// PLACEHOLDERS SWAPPABLES : chaque piece (StationPart) reference un MeshHandle cree
// par l'app + une matrice modele + une couleur + un nom clair. Remplacer un
// placeholder par un vrai modele = echanger UNE entree (le mesh), sans toucher au
// reste. Aucune dependance complexe sur les meshes eux-memes (les zones interactives
// sont des spheres analytiques, pas des colliders de maillage).
//
// Passer un `const StationView*` a RenderCore::render active la vue station ;
// nullptr laisse la carte (ou le rendu classique) inchangee.
#pragma once
#include "spr/core/Math.hpp"
#include "spr/bridge/RenderSnapshot.hpp"   // Dvec3
#include "spr/rhi/Rhi.hpp"                  // MeshHandle, MaterialHandle, DrawStyle, INVALID_*

namespace spr {

// Une piece placeholder de la station (module, console, ecran, rack, siege...).
// `mesh` est cree et possede par l'app (make_cylinder / make_box). `model` place la
// piece dans le repere LOCAL de la station (metres, proche de l'origine). `name`
// documente la piece pour la remplacer plus tard (ex. "MODULE_CONTROLE").
struct StationPart {
  MeshHandle     mesh{INVALID_MESH};
  Mat4           model{};
  Vec4           color{0.8f, 0.82f, 0.85f, 1.0f};
  DrawStyle      style{DrawStyle::PlanetLit};   // plat eclaire : lisible sans etoile
  MaterialHandle material{INVALID_MATERIAL};
  char           name[24]{};
};

// Un poste interactif (zone de gameplay). Delimitation ANALYTIQUE : une sphere de
// rayon `radius` centree en `center` (repere local station). Pas de collider de
// maillage -> independant des meshes placeholder (contrainte de swap).
struct StationZone {
  Dvec3  center{};
  double radius{2.5};
  char   id[24]{};      // identifiant stable : "controle" | "agence" | "planification" ...
  char   label[32]{};   // libelle affiche : "SALLE DE CONTROLE" ...
  char   sub[32]{};     // sous-titre : module ISS reel ("ZVEZDA . DIRECTION")
  // Couleur d'accent de la DA (holographie du poste) : teinte les ecrans 3D, le
  // marqueur du poste et le panneau 2D. Bleu tres clair par defaut (facon SC).
  Vec3   accent{0.42f, 0.78f, 1.0f};
};

// --- contenu VIVANT d'un panneau de poste (migration 2D -> 3D) ----------------
// Rempli par l'app depuis le MODELE DE JEU (fen::app::Jeu) ; le render-lib reste
// AGNOSTIQUE du jeu (aucune dependance sur `Jeu` ici, exactement comme MapView ne
// depend pas de la physique). Si `filled` est faux pour une zone, le HUD retombe
// sur son contenu de demonstration (panel_content). Chaines bornees (pas d'alloc).
struct PanelKV  { char key[28]{}; char val[28]{}; };
struct PanelBar { char key[28]{}; float frac{0.0f}; };

// --- vue LISTE (catalogue de missions) : navigable + verrouillage par palier ---
struct PanelListItem {
  char title[44]{};   // "Apollo 11            1969"
  char tag[18]{};     // badge : difficulte + palier ("***  LUNE")
  bool locked{false}; // palier d'agence ou technologies insuffisants
  bool done{false};   // mission accomplie (coche verte)
};
struct PanelReq { char name[24]{}; bool met{false}; };   // techno requise (acquise ?)
struct PanelList {
  const PanelListItem* items{nullptr};
  int      count{0};
  int      selected{0};          // element mis en avant (detail affiche)
  PanelKV  detail[7]{};  int detail_count{0};   // fiche de l'element selectionne
  PanelReq reqs[6]{};    int req_count{0};       // technologies requises (puces)
  bool     can_launch{false};    // l'element selectionne est debloque + non accompli
  bool     sel_done{false};      // l'element selectionne est accompli
  // PLAN DE VOL DETERMINISTE (aucun RNG) : Δv reel requis vs disponible, calcule
  // depuis les vraies positions des corps a l'epoque de lancement (patched-conic).
  bool     plan_feasible{false}; // le vehicule fournit assez de Δv reel
  bool     window_open{true};    // fenetre de lancement ouverte (angle de phase)
  char     plan_a[52]{};         // "Δv  12.4 / 18.7 km/s"
  char     plan_b[52]{};         // "C3 16  |  vol 258 j  |  1.52 UA"
  char     window_txt[48]{};     // "Fenetre ouverte" / "Prochaine fenetre : 96 j"
  char     detail_title[44]{};
  char     detail_note[160]{};
};

// --- vue ARBRE (competences) : grande toile pannable (facon ARK) --------------
// Coordonnees en PIXELS de CANVAS (le HUD affiche une fenetre du canvas et la
// deplace : molette = vertical, glisser = pan libre). Les libelles de categorie
// (`PanelTreeLane`) sont dessines COLLES a gauche.
struct PanelTreeNode {
  char  label[24]{};
  float x{0.0f}, y{0.0f};   // centre du noeud, en PIXELS de canvas
  int   prereq{-1};         // predecesseur dans la branche (lien) ; -1 = racine
  int   xreq{-1};           // prerequis CROISE (autre branche) ; -1 = aucun
  int   state{0};           // 0 verrouille / 1 disponible / 2 acquis
  int   cost{0};            // points de recherche
  bool  afford{true};       // points suffisants pour la recherche
  Vec3  accent{0.42f, 0.78f, 1.0f};
};
struct PanelTreeLane {       // libelle de categorie (colle a gauche)
  char  name[20]{};
  float y{0.0f};             // centre vertical, en pixels de canvas
  Vec3  accent{0.42f, 0.78f, 1.0f};
};
struct PanelTree {
  const PanelTreeNode* nodes{nullptr};
  int   count{0};
  const PanelTreeLane* lanes{nullptr};
  int   lane_count{0};
  float canvas_w{0.0f}, canvas_h{0.0f};   // taille totale de la toile (pixels)
  int   points{0};                        // points de recherche disponibles
  char  legend[112]{};                    // pied : niveau / points / acquises
};

// --- vue ETAPES : deroulement d'une mission LANCEE (checklist verticale) -------
// Chaque etape a un etat : 0 a venir / 1 en cours / 2 franchie. Le HUD dessine
// puce / fleche / coche selon l'etat. Rempli par l'app depuis le MissionRun 3D.
struct PanelStep { char label[24]{}; int state{0}; };

struct ZonePanel {
  char     status[24]{};     // etat affiche a droite de l'en-tete ("EN LIGNE"...)
  PanelKV  kv[6]{};   int kv_count{0};
  PanelBar bars[3]{}; int bar_count{0};
  char     note[144]{};      // ligne de pied (contexte)
  bool     filled{false};    // false -> le HUD utilise son contenu par defaut
  // Vues RICHES optionnelles (prioritaires sur les KV si non nulles) : l'app les
  // pointe vers ses donnees (catalogue de missions / arbre de competences).
  const PanelList* list{nullptr};
  const PanelTree* tree{nullptr};
  // Deroulement d'etapes (mission en cours) : liste + bouton d'action optionnels,
  // rendus DANS le panneau KV (ex. CONTROLE). `button` vide -> pas de bouton.
  const PanelStep* steps{nullptr};
  int              step_count{0};
  char             button[24]{};   // libelle du bouton d'action (ex. "ETAPE SUIVANTE")
};

// --- CONSOLE DE CALCUL (mode PRO) : le joueur TAPE la formule d'une etape --------
// L'app remplit l'enonce (quoi trouver + donnees + indice) ; le HUD affiche la
// fenetre, edite `input` (formule tapee) et pose `verify`/`close` ; l'app evalue
// (calc::eval) et renvoie `feedback`/`solved`. La physique/verite reste cote app.
struct CalcConsole {
  bool  active{false};       // la console est-elle affichee ?
  char  title[48]{};         // "INJECTION" (phase)
  char  find[64]{};          // "Delta-v d'injection (echappement Terre)"
  char  sym[8]{};            // "dv"
  char  unit[12]{};          // "km/s"
  char  hint[96]{};          // loi utilisee (aide)
  char  givens[6][44]{};     // "mu = 398600 km3/s2" (preformate par l'app)
  int   given_count{0};
  char  input[160]{};        // buffer editable : la formule tapee (edite par le HUD)
  int   feedback_kind{0};    // 0 neutre / 1 succes / 2 erreur ou faux
  char  feedback[112]{};     // message de retour
  bool  solved{false};       // etape resolue (le verrou d'avancement est leve)
  // requetes HUD -> app :
  bool  verify{false};       // VERIFIER clique (ou Entree dans le champ)
  bool  close{false};        // fermer la console
};

// LA "vue station" additive passee a RenderCore::render(). Sans possession : l'app
// detient les tableaux (comme MapView).
struct StationView {
  // --- geometrie placeholder -------------------------------------------------
  const StationPart* parts{nullptr};
  int                part_count{0};

  // --- postes interactifs ----------------------------------------------------
  const StationZone* zones{nullptr};
  int                zone_count{0};

  // --- contenu VIVANT des panneaux (meme index que `zones`) ------------------
  // Rempli par l'app depuis le modele de jeu. nullptr ou `filled==false` pour une
  // zone -> le HUD affiche son contenu de demonstration (migration progressive).
  const ZonePanel*   panels{nullptr};
  int                panel_count{0};

  // --- eclairage de cabine (l'interieur n'a pas d'etoile) --------------------
  // Le rendu synthetise une source directionnelle placee loin le long de light_dir
  // depuis l'oeil (terminateur doux) + un ambiant releve pour la lisibilite. La
  // lumiere vient du "plafond" (+Z) et descend (-Z) : sol illumine, parois nuancees.
  Vec3 light_dir{0.25f, 0.15f, -1.0f};
  Vec3 light_color{1.0f, 0.97f, 0.92f};
  Vec3 ambient{0.16f, 0.17f, 0.20f};

  // --- etat partage app <-> HUD (facon MapView::focus_request) ---------------
  // Zone la plus proche de l'oeil (proximite "Entrer"), calculee par l'app, lue
  // par le HUD pour l'invite "[E] ENTRER". -1 = aucune.
  int  near_zone{-1};
  // Poste dont l'interface 2D placeholder est ouverte. L'app le pose (touche E) ;
  // le HUD affiche la fenetre et le remet a -1 via [Fermer]. -1 = aucune.
  int  active_panel{-1};
  // Demande de sortie de l'ISS (bouton HUD "SORTIR"), consommee par l'app.
  bool exit_request{false};
  bool show_labels{true};
  // Retour visuel de la SAUVEGARDE RAPIDE ([F5]) : secondes restantes d'affichage
  // du bandeau "PARTIE SAUVEGARDEE". L'app le pose (>0) ; le HUD l'affiche et le
  // decremente. 0 = rien a l'ecran.
  float save_flash{0.0f};

  // --- interaction des vues riches (partagee HUD <-> app, facon focus_request) -
  // Le HUD ECRIT l'index clique ; l'app le LIT puis le remet a -1. Un seul panneau
  // actif a la fois -> des champs partages suffisent.
  int ui_list_click{-1};      // element de liste clique (catalogue de missions)
  int ui_tree_click{-1};      // noeud d'arbre clique (competences)
  int ui_mission_launch{-1};  // mission a LANCER (bouton fiche) -> demarre le vol
  int ui_mission_wait{-1};    // AVANCER le calendrier jusqu'a la fenetre de lancement
  int ui_panel_button{-1};    // zone dont le BOUTON d'action a ete clique (ex. CONTROLE : etape suivante)

  // Console de calcul (mode PRO) : le joueur derive la formule de l'etape courante.
  CalcConsole calc{};

  // Aide au placement (mode --noclamp) : affiche la position de l'oeil a l'ecran
  // pour relever des coordonnees (ex. localiser Novellus / le couloir).
  bool  show_eye{false};
  Dvec3 eye_pos{};
};

} // namespace spr
