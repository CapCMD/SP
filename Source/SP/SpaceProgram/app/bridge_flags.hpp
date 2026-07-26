// app/bridge_flags.hpp — le pont d'état UI (ImGui) -> monde UE (rendu 3D).
//
// Même doctrine que spr/bridge/RenderSnapshot.hpp : SENS UNIQUE (le jeu écrit,
// le rendu lit) et AUCUN recalcul côté rendu. C++ pur, inclus des deux côtés de
// la frontière : JAMAIS d'entête UnrealEngine ici.
//
// RÈGLES GDD GRAVÉES ICI :
//   [GDD 14]  le temps est UNIQUE : la carte affiche l'époque de jeu COURANTE.
//             Pas de curseur temporel libre, pas de « saut » vers un vol.
//   [GDD 7.5] le joueur ne voit JAMAIS la vérité absolue : la position publiée
//             d'un vaisseau est l'ESTIMATION de navigation, avec son corridor.
//   [GDD 8.3] la carte montre : trajectoire nominale, position estimée,
//             corridor d'incertitude, prochain nœud de manœuvre — et TOUS les
//             éléments de mission en service (satellites, orbiteurs, sondes).
#pragma once
#include <atomic>

namespace fen::app {

// LE MONDE UNIQUE 1:1 [GDD v1.2 décision 19, ch.17.3-17.4] :
//   Titre — menu SPACE PROGRAM (fond étoilé). SEUL écran distinct, AVANT la
//           partie.
//   Monde — LA scène 3D persistante = le système solaire à l'échelle 1:1.
//           Novellus ET les vaisseaux y sont placés à leur position réelle. On
//           y entre à bord (Novellus, module Vigie, première personne).
// La « carte » n'est PAS une scène séparée : c'est un CADRAGE lointain de ce
// même Monde (voir Cadrage ci-dessous). [M] est un signet de caméra [ch.8.3].
enum class SceneJeu { Titre = 0, Monde = 1 };

// CADRAGE DE LA CAMÉRA dans le Monde unique. Ce n'est PAS une scène : le monde
// est le même, seule la caméra change de plan. La transition SERA continue (un
// simple zoom, [ch.17.4]) ; cet état discret en est le point d'ancrage pour la
// première passe (rendu à l'identique de l'ancien couple Station/Carte).
//   Bord    — caméra au plan vaisseau/station : ambulation 1re personne active
//             (marche dans Novellus ou dans une mission vécue, [ch.9.1]) ;
//   Systeme — caméra tirée au plan système (ex-« carte ») : orbite, zoom,
//             picking d'un corps.
enum class Cadrage { Bord = 0, Systeme = 1 };

// Focus caméra SPÉCIAL : Novellus. La station n'est pas un corps du catalogue
// (`fen::ephem::Body`), mais elle est focalisable/cliquable comme un corps. On
// lui réserve un id de focus hors de l'enum Body pour ne jamais entrer en
// collision — le rendu (FocusWorldKm), le picking, le zoom et le HUD le
// reconnaissent et le traitent à part.
inline constexpr int FOCUS_STATION = 1000;

struct RenderBridge {
  // Scène active, pilotée par l'UI (Interface::dessiner) et appliquée par les
  // subsystems UE (chacun s'active pour la sienne).
  std::atomic<int> scene{static_cast<int>(SceneJeu::Titre)};
  // Le CADRAGE SYSTÈME est-il actif ? (Cadrage::Systeme dans le Monde ; l'ancien
  // « la carte est l'écran actif »). Le rendu station s'active à l'inverse quand
  // on est en Monde SANS ce drapeau (= Cadrage::Bord). Publié par Session::tick.
  std::atomic<bool> carte3d_active{false};

  // --- L'INTÉRIEUR DE L'ISS : commandes de déplacement -----------------------
  // L'overlay Slate capte tout le clavier/souris (cf. plus bas) : l'ambulation
  // première personne est donc COMMANDÉE ici par le HUD et appliquée par le
  // pawn UE. Repère caméra : avant/droite/haut, normalisés [-1,1].
  struct StationInput {
    std::atomic<float> fwd{0.0f};      // ZQSD / WASD
    std::atomic<float> right{0.0f};
    std::atomic<float> up{0.0f};       // espace / ctrl (l'ISS est en apesanteur)
    std::atomic<float> look_dx{0.0f};  // delta souris consommé par le pawn
    std::atomic<float> look_dy{0.0f};
    std::atomic<bool>  boost{false};   // maj : déplacement rapide
  } station_in;

  // --- L'INTÉRIEUR DE L'ISS : état publié par UE -----------------------------
  struct StationOut {
    std::atomic<bool>  ready{false};      // la scène est construite
    std::atomic<int>   near_post{-1};     // index du poste à portée, -1 sinon
    std::atomic<float> eye_m[3];          // position de l'œil (m, repère station)
    std::atomic<float> yaw{0.0f}, pitch{0.0f};
  } station_out;

  // Postes de travail publiés par l'UI vers UE (position en mètres, repère
  // station) — UE n'a qu'à tester la proximité et publier `near_post`.
  struct PostSnap {
    static constexpr int MAX = 12;
    std::atomic<int> n{0};
    struct Item { float x, y, z, radius_m; };
    Item items[MAX];
  } posts;

  // Époque de jeu COURANTE (s TDB depuis J2000). Vol en cours -> l'horloge du
  // vol EST le temps de jeu ; sinon calendrier agence. Jamais autre chose.
  std::atomic<double> epoch_tdb{0.0};

  // Options d'affichage (présentation pure).
  std::atomic<bool> show_moons{false};
  std::atomic<int>  focus_body{-1};   // fen::ephem::Body ; -1 = vue système
  // Corps sous le curseur, publié par la couche d'entrée native
  // (SPPlayerController) à partir de la projection écran ci-dessous ; le HUD ne
  // fait que le mettre en évidence. -1 = aucun.
  std::atomic<int>  hover_body{-1};
  // Le MENU utilise la carte comme décor : fond étoilé et orbites ténues,
  // exactement comme docs/reference_solar_system_map/ref_menu.png. Le monde de
  // la carte s'active donc aussi hors scène Carte, mais en retrait.
  std::atomic<bool> menu_backdrop{false};

  // --- LA CAMÉRA DE LA CARTE (façon NASA Eyes) -------------------------------
  // L'overlay Slate capte TOUTE la souris : l'entrée caméra vit donc côté UI
  // (ImGui) et le monde UE l'APPLIQUE. Même doctrine que le reste du pont :
  // sens unique, aucun recalcul côté rendu.
  //   dist_km  : distance de l'œil au point visé (échelle VRAIE, en km)
  //   yaw/pitch: orientation de l'orbite caméra (rad ; pitch borné ±1,55)
  //   focus_body pilote le point visé ; -1 = le Soleil (vue système).
  struct MapCam {
    std::atomic<double> dist_km{9.0e8};   // ~6 UA : le système interne cadré
    std::atomic<double> yaw{0.60};
    std::atomic<double> pitch{1.05};      // vu de dessus de l'écliptique
    std::atomic<double> fov_deg{45.0};
  } cam;

  // --- PROJECTION ÉCRAN publiée par UE --------------------------------------
  // À l'échelle vraie, une planète est sous-pixellique dès qu'on s'éloigne : le
  // HUD dessine alors un MARQUEUR et son libellé, et c'est lui qui fait le
  // picking (le clic ne peut pas descendre jusqu'au monde UE, cf. ci-dessus).
  // Coordonnées NORMALISÉES [0,1] : le HUD les multiplie par sa taille d'écran.
  struct ScreenBodies {
    static constexpr int MAX = 32;
    std::atomic<int> n{0};
    struct Item {
      int    body{-1};      // fen::ephem::Body
      float  nx{0}, ny{0};  // position écran normalisée
      float  r_norm{0};     // rayon apparent (fraction de la largeur d'écran)
      double dist_km{0};    // distance à l'œil
      int    on_screen{0};  // devant la caméra ET dans le cadre
    };
    Item items[MAX];
  } screen;

  // --- LA FLOTTE EN SERVICE [GDD 8.3, 10.1] ----------------------------------
  // v0.7 : chaque engin publie SA position ESTIMÉE relative à son corps de
  // référence (éphéméride képlérienne 2 corps entretenue par le jeu, modèle
  // déclaré [GDD 6.8]). Le rendu applique un rayon d'affichage PLANCHER autour
  // des planètes (une orbite GEO est invisible à l'échelle carte) : direction
  // et phase VRAIES, amplification déclarée dans le HUD [GDD 7.5].
  struct FleetSnap {
    static constexpr int MAX = 18;          // 6 par catégorie (cf. FLEET_PER_CAT)
    std::atomic<int> n{0};
    struct Craft {
      int type{0};        // EnginFlotte::Type : 0 relais GEO, 1 orbiteur Mars, 2 sonde
      int parent{0};      // fen::ephem::Body du corps de référence
      double rel_m[3]{};  // position ESTIMÉE relative au parent (écliptique, m)
    } craft[MAX];
    std::atomic<bool> vol_geo_actif{false}; // mission GEO en exploitation
  } fleet;

  // --- NOVELLUS DANS LE MONDE [GDD v1.2 11.1, 17.3] --------------------------
  // La station N'EST PAS une scène à part : elle a une position RÉELLE dans le
  // monde unique, en orbite LEO autour de la Terre (418 km). Publiée ici pour
  // que le rendu la place comme tout autre objet du monde — première brique du
  // passage au monde unique (incr.3 : Novellus littéralement dans la scène 1:1).
  // Orbite circulaire, plan écliptique : cercle DÉCLARÉ, même approximation que
  // la flotte [GDD 6.8].
  struct StationWorld {
    std::atomic<bool> valid{false};
    double rel_m[3]{};          // position rel. à la Terre (écliptique, m)
    double altitude_km{0.0};    // altitude au-dessus de la surface (HUD)
    double envergure_m{0.0};    // taille du modèle (échelle du rendu proche)
  } station;

  // --- LE VOL INTERPLANÉTAIRE EN COURS [GDD 8.3] -----------------------------
  // L'écran publie : la trajectoire NOMINALE (l'arc figé au commit), la
  // position ESTIMÉE à l'époque de jeu, le corridor 3σ, et les nœuds de
  // manœuvre (TCM) restants. `gen` s'incrémente quand la POLYLIGNE change.
  // z=0 : le modèle interplanétaire v0.6 est plan (approximation déclarée).
  struct VehicleSnap {
    std::atomic<bool> valid{false};
    std::atomic<int>  gen{0};
    double pos_m[3]{};            // position ESTIMÉE (héliocentrique écliptique)
    double corridor_3s_m{0.0};    // rayon 3σ du corridor d'incertitude
    static constexpr int MAX_PTS = 512;
    int n{0};
    double traj_m[MAX_PTS][3]{};  // trajectoire NOMINALE
    int n_nodes{0};               // nœuds de manœuvre affichés (TCM1, TCM2)
    double nodes_m[2][3]{};
    bool node_done[2]{};
  } vehicle;

  // --- LE VOL GEO EN COURS : VUE RAPPROCHÉE TERRE [GDD 8.3] ------------------
  // L'orbite GEO est INVISIBLE à l'échelle carte (42 164 km = 14 u). La vue
  // rapprochée bascule sur une scène géocentrique 1:1 (1 u = 100 m). Publiés :
  //   - la trace ESTIMÉE (la solution de navigation du jeu — jamais la vérité,
  //     [GDD 7.5]), la position estimée courante et son σ ;
  //   - l'orbite CIBLE du contrat : la référence nominale du suivi.
  std::atomic<int> close_view{0};   // 0 = carte système ; 1 = vue rapprochée Terre
  struct GeoFlightSnap {
    std::atomic<bool> valid{false};
    std::atomic<int>  gen{0};       // ++ quand la TRACE publiée change
    double pos_km[3]{};             // position ESTIMÉE, géocentrique (km)
    double sigma_km{0.0};           // 1σ position de la solution de navigation
    double target_sma_km{0.0};      // demi-grand axe cible du contrat (nominal)
    static constexpr int MAX_PTS = 512;
    int n{0};
    double traj_km[MAX_PTS][3]{};   // trace estimée échantillonnée
  } geo;

  // game-thread : signatures des dernières publications (détection de changement)
  double last_arc_sig{-1.0};
  double last_geo_sig{-1.0};
};

inline RenderBridge g_render_bridge;

} // namespace fen::app
