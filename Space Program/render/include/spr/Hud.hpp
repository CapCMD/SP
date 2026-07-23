// spr/Hud.hpp
//
// HUD technique (ImGui). Il MET EN PAGE la telemetrie deja calculee par le pont
// (RenderSnapshot::telemetry) : il ne calcule aucune physique, ne rappelle jamais
// astro_core. N'inclut aucun backend ImGui (ni GLFW ni Vulkan) : le cycle
// NewFrame/Render est pilote par le device via l'interface RHI (imgui_new_frame /
// draw_hud). Ici, uniquement des widgets ImGui:: purs.
#pragma once
#include "spr/bridge/RenderSnapshot.hpp"
#include "spr/core/Camera.hpp"
#include "spr/MapView.hpp"
#include "spr/StationView.hpp"
#include "spr/MenuView.hpp"

namespace spr {

class Hud {
 public:
  // Construit les fenetres ImGui de la frame. A appeler ENTRE
  // device.imgui_new_frame() et device.draw_hud(). Si `map` != nullptr, ajoute le
  // panneau de CONTROLE DU TEMPS (edite `map->time`), la legende des orbites et
  // les etiquettes de corps projetees a l'ecran (facon NASA Eyes). Si `station` !=
  // nullptr (priorite sur `map`), affiche le HUD INTERIEUR ISS : etiquettes des
  // postes, invite "[E] ENTRER", bouton "SORTIR" et fenetres 2D placeholder. Le HUD
  // ne calcule aucune physique : il met en page des nombres deja figes. Si `menu`
  // != nullptr (PRIORITE sur map/station), affiche l'ECRAN TITRE + MENUS et pose
  // les requetes (nouvelle partie / reprendre / difficulte / quitter).
  void build(const RenderSnapshot& s, Camera& cam, const char* device_name, float fps,
             MapView* map = nullptr, StationView* station = nullptr, MenuView* menu = nullptr);

  bool show_telemetry{true};
  bool show_bodies{true};
  bool show_help{true};
};

} // namespace spr
