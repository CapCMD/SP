// spr/Window.hpp
//
// Fenetre + entrees, via GLFW (deja vendore dans extern/glfw). Cree une fenetre
// SANS contexte OpenGL (GLFW_NO_API) : c'est Vulkan qui possede le device. Expose
// le handle natif Win32 (HWND/HINSTANCE) pour que le backend cree sa surface, et
// un etat d'entree simple (souris/molette) pour piloter la camera.
#pragma once
#include <cstdint>
#include <string>

struct GLFWwindow;

namespace spr {

struct InputState {
  double mouse_x{}, mouse_y{};
  double mouse_dx{}, mouse_dy{};   // delta depuis la derniere frame
  double scroll{};                 // molette accumulee cette frame
  bool   dragging{false};          // bouton gauche enfonce
  bool   panning{false};           // bouton droit enfonce
};

class Window {
 public:
  Window() = default;
  ~Window();
  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;

  bool create(const std::string& title, int w, int h);
  void destroy();

  bool should_close() const;
  void poll();                     // pompe les evenements, met a jour InputState
  void wait_events_if_minimized(); // bloque tant que minimise (w==0||h==0)

  GLFWwindow* glfw() const { return win_; }
  void*  hwnd() const;             // HWND
  void*  hinstance() const;        // HINSTANCE
  int    width()  const { return w_; }
  int    height() const { return h_; }
  bool   resized_consume();        // true une fois si le framebuffer a change

  const InputState& input() const { return input_; }
  void  reset_frame_deltas();      // remet dx/dy/scroll a 0 (fin de frame)

  // Curseur CAPTURE (GLFW_CURSOR_DISABLED) : souris libre illimitee pour une vraie
  // vue FPS (mouse-look sans clic), curseur masque. A relacher (false) pour rendre
  // l'UI cliquable. Le premier delta apres capture est ignore (evite le saut).
  void set_cursor_disabled(bool);
  bool cursor_disabled() const { return cursor_disabled_; }

 private:
  static void on_resize(GLFWwindow*, int, int);
  static void on_scroll(GLFWwindow*, double, double);

  GLFWwindow* win_{nullptr};
  int  w_{1280}, h_{720};
  bool resized_{false};
  InputState input_{};
  double last_x_{}, last_y_{};
  bool cursor_disabled_{false};
  bool skip_delta_{false};         // ignore le prochain delta souris (post-capture)
};

} // namespace spr
