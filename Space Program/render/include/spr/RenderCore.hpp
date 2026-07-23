// spr/RenderCore.hpp
//
// L'ORCHESTRATEUR du rendu. Possede la fenetre, le device RHI (Vulkan), la scene,
// le HUD et la camera. Il ne connait PAS la physique : il consomme des
// RenderSnapshot deja figes par le DataBridge. La boucle applicative appelle
//   while (!core.should_close()) { core.begin_frame(); snap = bridge.freeze(...);
//                                  core.render(snap); }
// mais RenderCore n'exige pas de savoir d'ou vient le snapshot.
//
// Cycle d'update (une frame) :
//   poll entrees -> camera (controles)          [RenderCore::begin_frame]
//   [l'appelant fige un RenderSnapshot]
//   camera.follow(snapshot)                      .
//   device.begin_frame()  (acquire image)        |
//   device.imgui_new_frame(); hud.build(...)     |  RenderCore::render(snap)
//   scene.build(snapshot,camera) -> DrawList     |
//   device.submit(drawlist); device.draw_hud()   |
//   device.end_frame()  (present)                '
#pragma once
#include <memory>
#include "spr/Window.hpp"
#include "spr/rhi/Rhi.hpp"
#include "spr/scene/RenderScene.hpp"
#include "spr/core/Camera.hpp"
#include "spr/Hud.hpp"
#include "spr/bridge/RenderSnapshot.hpp"
#include "spr/MapView.hpp"
#include "spr/StationView.hpp"
#include "spr/MenuView.hpp"

namespace spr {

struct RenderConfig {
  std::string   title{"Space Program - Render Core (Vulkan)"};
  int           width{1280};
  int           height{720};
  bool          validation{false};   // couches de validation Vulkan (debug)
};

class RenderCore {
 public:
  RenderCore() = default;
  ~RenderCore();

  bool init(const RenderConfig&);
  void shutdown();

  bool should_close() const;

  // Debut de frame cote fenetre/entrees : pompe les evenements et applique les
  // controles a la camera. A appeler AVANT de figer le snapshot.
  void begin_frame(float dt);

  // Rend une frame a partir d'un snapshot fige. `focused_body` = index dans
  // snapshot.bodies pour les modes BodyCentered/Map (-1 = corps central).
  // `map` (optionnel) active la vue carte (orbites, plancher de taille, textures,
  // panneau temps) ; nullptr = rendu classique. Non-const : le HUD y edite l'etat
  // de controle du temps (map->time). La scene le lit en lecture seule.
  // `station` (optionnel) active la vue INTERIEUR ISS (premiere personne) : il a la
  // priorite sur `map`. Non-const : le HUD y edite l'etat des postes (active_panel,
  // exit_request).
  // `menu` (optionnel) active l'ECRAN TITRE + MENUS : il a la PRIORITE sur tout le
  // reste du HUD (seul le menu est dessine). La scene 3D passee via `map` reste
  // rendue EN FOND. Non-const : le HUD y edite la saisie et pose les requetes.
  void render(const RenderSnapshot& snap, int focused_body = -1, MapView* map = nullptr,
              StationView* station = nullptr, MenuView* menu = nullptr);

  // Ecrit la PROCHAINE frame presentee dans un PPM (oracle visuel). A appeler
  // avant le render() de la frame a capturer.
  void request_capture(const char* ppm_path);

  Camera&       camera()       { return camera_; }
  const Window& window() const { return window_; }
  Window&       window()       { return window_; }   // capture curseur (vue FPS ISS)
  Hud&          hud()          { return hud_; }
  // Peripherique de rendu (interface RHI, API-agnostique). Expose pour que le
  // point d'entree puisse creer des ressources de presentation (textures/materiaux
  // GLB de la carte). Aucun type Vulkan ne fuit : IRenderDevice est abstrait.
  IRenderDevice* device()      { return device_.get(); }

 private:
  Window                          window_;
  std::unique_ptr<IRenderDevice>  device_;
  RenderScene                     scene_;
  Camera                          camera_;
  Hud                             hud_;
  float                           fps_{0.0f};
};

} // namespace spr
