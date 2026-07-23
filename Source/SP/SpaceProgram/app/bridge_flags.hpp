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

struct RenderBridge {
  // La carte 3D est-elle l'écran actif ? (piloté par Interface::dessiner)
  std::atomic<bool> carte3d_active{false};

  // Époque de jeu COURANTE (s TDB depuis J2000). Vol en cours -> l'horloge du
  // vol EST le temps de jeu ; sinon calendrier agence. Jamais autre chose.
  std::atomic<double> epoch_tdb{0.0};

  // Options d'affichage (présentation pure).
  std::atomic<bool> show_moons{false};
  std::atomic<int>  focus_body{-1};   // fen::ephem::Body ; -1 = vue système

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
