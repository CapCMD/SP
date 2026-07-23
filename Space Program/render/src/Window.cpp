// spr/Window.cpp
#include "spr/Window.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

namespace spr {

Window::~Window() { destroy(); }

bool Window::create(const std::string& title, int w, int h) {
  if (!glfwInit()) return false;
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);  // pas d'OpenGL : Vulkan possede le device
  win_ = glfwCreateWindow(w, h, title.c_str(), nullptr, nullptr);
  if (!win_) { glfwTerminate(); return false; }
  w_ = w; h_ = h;
  glfwSetWindowUserPointer(win_, this);
  glfwSetFramebufferSizeCallback(win_, &Window::on_resize);
  glfwSetScrollCallback(win_, &Window::on_scroll);
  glfwGetCursorPos(win_, &last_x_, &last_y_);
  return true;
}

void Window::destroy() {
  if (win_) { glfwDestroyWindow(win_); win_ = nullptr; glfwTerminate(); }
}

void Window::on_resize(GLFWwindow* w, int nw, int nh) {
  auto* self = static_cast<Window*>(glfwGetWindowUserPointer(w));
  if (!self) return;
  self->w_ = nw; self->h_ = nh; self->resized_ = true;
}

void Window::on_scroll(GLFWwindow* w, double, double dy) {
  auto* self = static_cast<Window*>(glfwGetWindowUserPointer(w));
  if (self) self->input_.scroll += dy;
}

bool Window::should_close() const { return win_ && glfwWindowShouldClose(win_); }

void Window::poll() {
  glfwPollEvents();
  double x, y;
  glfwGetCursorPos(win_, &x, &y);
  if (skip_delta_) {                 // premier tour apres (dis)activation de la capture
    input_.mouse_dx = input_.mouse_dy = 0.0;
    skip_delta_ = false;
  } else {
    input_.mouse_dx = x - last_x_;
    input_.mouse_dy = y - last_y_;
  }
  last_x_ = x; last_y_ = y;
  input_.mouse_x = x; input_.mouse_y = y;
  input_.dragging = glfwGetMouseButton(win_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
  input_.panning  = glfwGetMouseButton(win_, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
}

void Window::set_cursor_disabled(bool disabled) {
  if (!win_ || disabled == cursor_disabled_) return;
  glfwSetInputMode(win_, GLFW_CURSOR, disabled ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
  if (disabled && glfwRawMouseMotionSupported())
    glfwSetInputMode(win_, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
  cursor_disabled_ = disabled;
  skip_delta_ = true;              // le passage de mode provoque un saut : on l'ignore
}

void Window::wait_events_if_minimized() {
  int w = 0, h = 0;
  glfwGetFramebufferSize(win_, &w, &h);
  while (w == 0 || h == 0) {
    glfwWaitEvents();
    glfwGetFramebufferSize(win_, &w, &h);
  }
  w_ = w; h_ = h;
}

bool Window::resized_consume() {
  const bool r = resized_;
  resized_ = false;
  return r;
}

void Window::reset_frame_deltas() {
  input_.mouse_dx = input_.mouse_dy = 0.0;
  input_.scroll = 0.0;
}

void* Window::hwnd() const { return win_ ? (void*)glfwGetWin32Window(win_) : nullptr; }
void* Window::hinstance() const { return (void*)GetModuleHandleW(nullptr); }

} // namespace spr
