// ui/game_main.cpp — LE JEU. Fenetre, menu, bouton JOUER, salle de vol.
// Cible : Windows (via mingw) ou Linux (GLFW+GL). Le bouton LANCER execute
// vraiment la mission, via la console Lua embarquee dans le meme binaire.
#include <cstdio>
#include <string>
#include "imgui.h"
#include "implot.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include "ui/game.hpp"

// La console Lua, appelee en bibliotheque (definie dans lua_runner.cpp).
namespace fen { std::string run_tool(const std::string& tool, const std::string& args); }

int main() {
  if (!glfwInit()) { std::printf("glfwInit KO\n"); return 1; }
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
  GLFWwindow* win = glfwCreateWindow(1000, 760, "FENETRE", nullptr, nullptr);
  if (!win) { glfwTerminate(); return 1; }
  glfwMakeContextCurrent(win);
  glfwSwapInterval(1);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext(); ImPlot::CreateContext();
  ImGui::StyleColorsDark();
  ImGui::GetStyle().WindowRounding = 2.0f;
  ImGui::GetStyle().FrameRounding = 3.0f;
  ImGui_ImplGlfw_InitForOpenGL(win, true);
  ImGui_ImplOpenGL3_Init("#version 130");

  fen::ui::Game g;
  g.run = [](const std::string& tool, const std::string& args){ return fen::run_tool(tool, args); };

  while (!glfwWindowShouldClose(win) && !g.quit) {
    glfwPollEvents();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    // les menus prennent tout l'ecran ; la salle de vol gere ses fenetres
    if (g.screen != fen::ui::Screen::Flight) {
      ImGui::SetNextWindowPos(ImVec2(0,0), ImGuiCond_Always);
      int w,h; glfwGetFramebufferSize(win,&w,&h);
      ImGui::SetNextWindowSize(ImVec2((float)w,(float)h), ImGuiCond_Always);
    }
    g.draw();
    ImGui::Render();
    int w,h; glfwGetFramebufferSize(win,&w,&h);
    glViewport(0,0,w,h);
    glClearColor(0.07f,0.07f,0.09f,1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(win);
  }
  ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplGlfw_Shutdown();
  ImPlot::DestroyContext(); ImGui::DestroyContext();
  glfwDestroyWindow(win); glfwTerminate();
  return 0;
}
