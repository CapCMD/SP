// spr/RenderCore.cpp
#include "spr/RenderCore.hpp"
#include <cmath>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include "imgui.h"

namespace spr {

RenderCore::~RenderCore() { shutdown(); }

bool RenderCore::init(const RenderConfig& cfg) {
  if (!window_.create(cfg.title, cfg.width, cfg.height)) return false;

  DeviceConfig dc{};
  dc.hwnd = window_.hwnd();
  dc.hinstance = window_.hinstance();
  dc.width = static_cast<std::uint32_t>(window_.width());
  dc.height = static_cast<std::uint32_t>(window_.height());
  dc.enable_validation = cfg.validation;
  device_ = create_vulkan_device(dc);
  if (!device_) return false;

  scene_.init(*device_);
  device_->imgui_init(window_.glfw());

  camera_.mode = CameraMode::Inertial;
  camera_.set_distance(1.2e8);   // cadre une orbite terrestre haute + la Lune
  camera_.pitch = 0.5;
  return true;
}

void RenderCore::shutdown() {
  if (device_) {
    device_->wait_idle();
    scene_.shutdown(*device_);
    device_->imgui_shutdown();
    device_.reset();
  }
  window_.destroy();
}

bool RenderCore::should_close() const { return window_.should_close(); }

void RenderCore::request_capture(const char* ppm_path) {
  if (device_) device_->request_capture(ppm_path);
}

void RenderCore::begin_frame(float dt) {
  window_.poll();

  GLFWwindow* w = window_.glfw();
  const bool fp = (camera_.mode == CameraMode::FirstPerson);

  // Raccourcis clavier : modes camera 1..5 (desactives en premiere personne, ou
  // les touches lettres servent au deplacement et les modes sont piotes par l'app).
  if (!fp) {
    if (glfwGetKey(w, GLFW_KEY_1) == GLFW_PRESS) camera_.mode = CameraMode::Inertial;
    if (glfwGetKey(w, GLFW_KEY_2) == GLFW_PRESS) camera_.mode = CameraMode::BodyCentered;
    if (glfwGetKey(w, GLFW_KEY_3) == GLFW_PRESS) camera_.mode = CameraMode::Vehicle;
    if (glfwGetKey(w, GLFW_KEY_4) == GLFW_PRESS) camera_.mode = CameraMode::Surface;
    if (glfwGetKey(w, GLFW_KEY_5) == GLFW_PRESS) camera_.mode = CameraMode::Map;
  }

  // Ne pas piloter la camera quand la souris est captee par le HUD (boutons du
  // panneau TEMPS, etc.). WantCaptureMouse vient de la frame ImGui precedente.
  const bool ui_mouse = ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse;
  const bool ui_keys  = ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureKeyboard;

  const InputState& in = window_.input();
  if (fp) {
    // VRAIE VUE FPS : mouse-look direct quand le curseur est capture (sans clic).
    // (Le curseur est relache par l'app quand un panneau de gestion est ouvert.)
    // Souris HAUT -> regard HAUT (pitch non inverse : -mouse_dy).
    if (window_.cursor_disabled()) {
      camera_.orbit(static_cast<float>(-in.mouse_dx) * 0.0022f,
                    static_cast<float>(-in.mouse_dy) * 0.0022f);
    }
    // Deplacement : ZQSD (AZERTY) / WASD (QWERTY) + haut/bas, avec INERTIE
    // (impesanteur) integree par la camera. Poussee dans {-1,0,1}.
    double fwd = 0.0, right = 0.0, up = 0.0;
    if (!ui_keys) {
      if (glfwGetKey(w, GLFW_KEY_W) == GLFW_PRESS || glfwGetKey(w, GLFW_KEY_Z) == GLFW_PRESS) fwd += 1.0;
      if (glfwGetKey(w, GLFW_KEY_S) == GLFW_PRESS) fwd -= 1.0;
      if (glfwGetKey(w, GLFW_KEY_D) == GLFW_PRESS) right += 1.0;
      if (glfwGetKey(w, GLFW_KEY_A) == GLFW_PRESS || glfwGetKey(w, GLFW_KEY_Q) == GLFW_PRESS) right -= 1.0;
      if (glfwGetKey(w, GLFW_KEY_SPACE) == GLFW_PRESS) up += 1.0;
      if (glfwGetKey(w, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
          glfwGetKey(w, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) up -= 1.0;
    }
    camera_.fp_move(fwd, right, up, static_cast<double>(dt));   // inertie + amortissement
    camera_.fp_update_idle(static_cast<double>(dt));            // ballant d'impesanteur
  } else {
    if (in.dragging && !ui_mouse)  // bouton GAUCHE : pivoter (arcball)
      camera_.orbit(static_cast<float>(-in.mouse_dx) * 0.005f,
                    static_cast<float>(in.mouse_dy) * 0.005f);
    if (in.panning && !ui_mouse)   // bouton DROIT : se deplacer (pan libre du focus)
      camera_.pan(static_cast<float>(in.mouse_dx), static_cast<float>(in.mouse_dy));
    if (in.scroll != 0.0 && !ui_mouse)
      camera_.dolly(std::pow(0.88f, static_cast<float>(in.scroll)));
  }

  if (dt > 1e-6f) fps_ = fps_ * 0.9f + (1.0f / dt) * 0.1f;
  window_.reset_frame_deltas();
}

void RenderCore::render(const RenderSnapshot& snap, int focused_body, MapView* map,
                        StationView* station, MenuView* menu) {
  if (window_.width() == 0 || window_.height() == 0) {
    window_.wait_events_if_minimized();
    return;
  }
  if (window_.resized_consume())
    device_->resize(static_cast<std::uint32_t>(window_.width()),
                    static_cast<std::uint32_t>(window_.height()));

  camera_.follow(snap, focused_body);

  if (!device_->begin_frame()) return;  // swapchain a recreer : on saute la frame

  device_->imgui_new_frame();
  hud_.build(snap, camera_, device_->device_name(), fps_, map, station, menu);

  const float aspect =
      static_cast<float>(window_.width()) / static_cast<float>(window_.height());
  const DrawList& dl = scene_.build(snap, camera_, aspect, map, station);

  device_->submit(dl);
  device_->draw_hud();
  device_->end_frame();
}

} // namespace spr
