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

// Focus caméra SPÉCIAL : LE VAISSEAU EN VOL [GDD 17.4]. « Verrouillage sur tout
// objet actif » — et depuis que le véhicule conçu a une COQUE [12.2, 17.2], s'en
// approcher est ce qui fait passer du plan système au PLAN VAISSEAU (mètres).
// Même traitement que Novellus : un id hors de l'enum Body, reconnu par le rendu.
inline constexpr int FOCUS_VAISSEAU = 1001;

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
    // MAJ : S'AGRIPPER À LA MAIN COURANTE (2026-07-27). C'était un « boost »
    // (×3 la vitesse) — un réflexe de jeu de vol, pas d'impesanteur. En
    // microgravité il n'y a pas de frein : il y a une main courante qu'on
    // attrape. Cette touche fait donc les deux choses qu'une main courante fait
    // — pousser fort, et RETENIR — et elle n'a d'effet qu'à portée d'une paroi.
    // La loi vit dans `app/impesanteur.hpp`, sous oracle.
    std::atomic<bool>  agrippe{false};
  } station_in;

  // --- L'INTÉRIEUR DE L'ISS : état publié par UE -----------------------------
  struct StationOut {
    std::atomic<bool>  ready{false};      // la scène est construite
    std::atomic<int>   near_post{-1};     // index du poste à portée, -1 sinon
    std::atomic<float> eye_m[3];          // position de l'œil (m, repère station)
    std::atomic<float> yaw{0.0f}, pitch{0.0f};
    // Champ de vision HORIZONTAL de la caméra de bord (deg). Le plan système a le
    // sien (45°, cadrage façon NASA Eyes) ; sans convergence des DEUX champs, la
    // reprise du handoff change le grossissement d'un facteur tan(45°)/tan(22,5°)
    // = 2,4 — la coupure reviendrait, en zoom.
    std::atomic<float> fov_deg{90.0f};
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

  // CADENCE DU TEMPS [GDD 14.2] : `fen::game::TimeRate` (0 pause, 1 temps réel,
  // 2 jour/s, 3 semaine/s, 4 mois/s). Publiée pour que la barre de temps l'AFFICHE.
  // Elle ne se PILOTE pas ici : le réglage vit au poste AGENCE, avec ses coûts —
  // jamais dans le curseur de la barre, qui reste un indicateur [GDD 14].
  std::atomic<int> cadence{0};

  // LE RYTHME IMPOSÉ PAR LA MISSION [GDD 14.3] : « toute manœuvre fine ramène le
  // temps à un rythme lent ». Le plafond est DÉDUIT de la durée propre de la
  // phase critique en cours (`fen::mission::tempo_limit`) — le HUD ne le
  // recalcule pas, il le lit et grise ce qui est au-dessus. Sans lui, le joueur
  // verrait ses crans refusés sans savoir pourquoi.
  //   cadence_max   : `fen::game::TimeRate` le plus rapide autorisé (4 = libre) ;
  //   tempo_phase   : `fen::mission::FlightPhase` qui l'impose (0 = aucune).
  std::atomic<int>  cadence_max{4};
  std::atomic<int>  tempo_phase{0};
  std::atomic<bool> tempo_contraint{false};

  // LE VOL EN COURS [GDD 4.1, 9] — la chronologie de la mission conduite.
  // Le vol DURE désormais (fen/mission/FlightTimeline.hpp) : entre le feu vert
  // et le débrief il y a des mois de croisière, et une phase critique à
  // l'arrivée. Sans ces deux chiffres à l'écran, le joueur voit un bouton qui
  // refuse et une mission qui ne bouge plus.
  //   vol_actif      : une mission est en état EXPLOITATION ;
  //   vol_phase      : `fen::mission::FlightPhase` courante (dérivée) ;
  //   vol_arrivee_datee / vol_reste_jours : la date d'arrivée est-elle
  //                    calculable (cible nommée), et dans combien de jours.
  std::atomic<bool>   vol_actif{false};
  std::atomic<int>    vol_phase{0};
  std::atomic<bool>   vol_arrivee_datee{false};
  std::atomic<double> vol_reste_jours{0.0};

  // NB : `show_moons` a été SUPPRIMÉ (2026-07-27). C'était un booléen que RIEN
  // n'écrivait : il valait false pour toujours, et éteignait la Lune et Titan,
  // pourtant câblés au rendu. Un interrupteur que personne ne peut actionner est
  // un mécanisme absent (pièges n°20b et n°40). Les lunes sont désormais régies
  // par la SÉPARABILITÉ à l'écran, comme Novellus (piège n°41) : ce qui ne se
  // distingue pas de son parent ne s'affiche pas et ne se désigne pas.
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
    // HANDOFF VERS L'AMBULATION [GDD v1.2 17.4] : poids de mélange de
    // l'ORIENTATION de la caméra entre « regarder le point visé » (0, le
    // comportement de la carte) et « le regard du pawn à bord » (1, publié dans
    // `station_out.yaw/pitch`). Le vol [M] l'amène à 1 pile au moment où l'œil
    // atteint la position de l'œil du pawn : la reprise en 1re personne est alors
    // pixel pour pixel la même image, donc INVISIBLE.
    std::atomic<double> look_to_bord{0.0};
    // VOL SCRIPTÉ EN COURS (`Session::vol_cam`). Le rendu possède son propre
    // lissage de la pose (τ = 0,35 s) — indispensable quand la cible SAUTE (un
    // clic sur un corps devient un vol), néfaste quand la cible est DÉJÀ une
    // trajectoire lissée : le lissage la retarde, et le vol [M] n'arriverait pas
    // sur l'œil du pawn (handoff manqué de plusieurs mètres). Sous ce drapeau, le
    // rendu SUIT la pose publiée à l'identique.
    std::atomic<bool> vol_camera{false};
  } cam;

  // L'INTÉRIEUR COEXISTE avec le plan système [GDD v1.2 17.3, ch.18] : vrai
  // quand l'œil est DANS l'enveloppe de la station alors que le plan système
  // rend encore (fin du vol [M] d'entrée, début du vol de sortie). Le rendu
  // montre alors la géométrie intérieure à la position RÉELLE de Novellus et
  // cache le modèle extérieur — c'est la bascule de LOD, faite là où elle est le
  // moins visible : au franchissement de la coque. La MAIN, elle, ne passe à la
  // 1re personne qu'à la fin du vol (`Cadrage::Bord`).
  std::atomic<bool> interieur_coexiste{false};

  // --- PROJECTION ÉCRAN publiée par UE --------------------------------------
  // À l'échelle vraie, une planète est sous-pixellique dès qu'on s'éloigne : le
  // HUD dessine alors un MARQUEUR et son libellé, et c'est lui qui fait le
  // picking (le clic ne peut pas descendre jusqu'au monde UE, cf. ci-dessus).
  // Coordonnées NORMALISÉES [0,1] : le HUD les multiplie par sa taille d'écran.
  struct ScreenBodies {
    // 30 corps (12 planètes + 19 lunes... et le Soleil) + Novellus : 32 tenait
    // tout juste, sans marge. Élargi pour que l'ajout d'un corps ne se traduise
    // pas par une disparition silencieuse en fin de liste.
    static constexpr int MAX = 48;
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
  // Orbite CIRCULAIRE d'éléments moyens, inclinée de 51,64° sur l'équateur
  // terrestre, nœud en régression J2 : le modèle vit dans `app/novellus_orbite.hpp`
  // et ses approximations y sont déclarées [GDD 6.8].
  struct StationWorld {
    std::atomic<bool> valid{false};
    double rel_m[3]{};          // position rel. à la Terre (écliptique, m)
    // VITESSE, et pourquoi elle est PUBLIÉE (2026-07-27). Novellus a maintenant une
    // ATTITUDE : cupola au nadir, axe de vol le long de la trajectoire (le vol
    // « XVV » réel de l'ISS). Le nadir se déduit de la position seule, mais pas
    // l'axe de vol — il demande la direction du mouvement. Le rendu POURRAIT la
    // deviner en dérivant la position d'une frame à l'autre : ce serait FAUX aux
    // cadences rapides (à mois/s, une frame avance de ~12 h, soit près de huit
    // orbites LEO — la corde entre deux échantillons ne dit plus rien de la
    // tangente, et la station se mettrait à tomber en vrille). Elle est donc
    // calculée là où vit le modèle, par le MÊME helper que la position, et
    // traversée ici. Le rendu ne dérive rien [doctrine du pont].
    double vel_ms[3]{};         // vitesse rel. à la Terre (écliptique, m/s)
    // ═══ L'ATTITUDE, PUBLIÉE PARCE QU'ELLE A TROIS CONSOMMATEURS ═══ (2026-07-27)
    // Cupola au nadir, axe de vol dans la vitesse. Les trois vecteurs sont les
    // IMAGES des axes du modèle (+X avant, +Y tribord, +Z zénith), DÉJÀ dans le
    // repère de rendu (miroir en y). Le rendu n'a plus qu'à en faire les lignes
    // d'une matrice — il ne calcule rien [doctrine du pont].
    // Trois consommateurs, et c'est la raison d'être de ce champ : le modèle
    // EXTÉRIEUR (SPSolarSystem), la géométrie INTÉRIEURE (SPStation) et la pose de
    // caméra du handoff (`Session::pose_bord`). S'ils divergeaient d'un iota, la
    // bascule de LOD à la traversée de la coque ferait SAUTER l'orientation de la
    // station à l'écran. Une seule source, sous oracle.
    double att_avant[3]{1.0, 0.0, 0.0};
    double att_tribord[3]{0.0, 1.0, 0.0};
    double att_zenith[3]{0.0, 0.0, 1.0};
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
    // LA VITESSE, et pourquoi elle est publiée : elle donne l'AXE du vaisseau au
    // rendu [GDD 17.2]. Même doctrine que pour Novellus — le rendu ne dérive pas
    // une direction en différenciant deux frames, ce qui serait faux dès la
    // cadence « mois/s ». Approximation déclarée [GDD 6.8] : le vaisseau est
    // dessiné sur son vecteur vitesse, c'est-à-dire dans son attitude de POUSSÉE ;
    // un engin réel en croisière pointe son antenne, ce que rien ne modélise.
    double vel_ms[3]{};           // vitesse héliocentrique à la même date
    double corridor_3s_m{0.0};    // rayon 3σ du corridor d'incertitude
    static constexpr int MAX_PTS = 512;
    int n{0};
    double traj_m[MAX_PTS][3]{};  // trajectoire NOMINALE
    // NŒUDS DE MANŒUVRE. Deux pour un transfert direct (injection, arrivée) ;
    // jusqu'à MAX_NODES pour un TOUR d'assistance, où chaque manœuvre profonde et
    // chaque survol en est un [GDD 5.11, 8.3].
    static constexpr int MAX_NODES = 8;
    int n_nodes{0};
    double nodes_m[MAX_NODES][3]{};
    bool node_done[MAX_NODES]{};
  } vehicle;

  // --- LA COUPE DU VAISSEAU [GDD 12.2, 17.2, 17.4] ---------------------------
  // « Un véhicule assemblé par le joueur doit être RENDU, pas modélisé » : le
  // rendu ne connaît ni pièces, ni ergols, ni Tsiolkovsky — il reçoit la COUPE,
  // c'est-à-dire une pile de troncs de cône coaxiaux, en MÈTRES, telle que
  // `vehicle::build_hull` la calcule. C'est le même objet qui sert au dessin en
  // coupe du poste CONCEPTION [12.2] et à la géométrie 3D du monde [17.2] : une
  // seule source, deux consommateurs, aucune chance qu'ils divergent.
  //
  // DEUX COUPES, ET ELLES NE DISENT PAS LA MÊME CHOSE :
  //   . `hull_vol` = le vaisseau EN VOL, figé au feu vert (`Mission::vaisseau_*`) ;
  //   . `hull_design` = la conception EN COURS d'édition, qui bouge à chaque clic.
  // Les confondre ferait changer de forme un vaisseau déjà parti.
  struct HullSnap {
    static constexpr int MAX_SEG = 24;
    std::atomic<bool> valid{false};
    std::atomic<int>  gen{0};      // ++ quand la coupe CHANGE
    int    n{0};
    double length_m{0.0};
    double diameter_m{0.0};
    struct Seg {
      int    role{0};              // vehicle::HullRole
      int    stage{-1};
      double z0_m{}, z1_m{}, r0_m{}, r1_m{};
    } seg[MAX_SEG];
  } hull_vol, hull_design;

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
