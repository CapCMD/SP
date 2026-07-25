// ui/main_glfw.cpp — L'UI INTERACTIVE, pour un poste avec ecran.
// (Sur la machine de dev sans display, c'est main_headless.cpp qui rend en PNG.
//  Le panels.hpp est LE MEME : l'UI ne sait pas si elle est a l'ecran ou non.)
//
// Build (Linux/macOS, GLFW + OpenGL3) :
//   g++ -std=c++20 -O2 ui/main_glfw.cpp \
//       imgui*.cpp imgui_impl_glfw.cpp imgui_impl_opengl3.cpp implot*.cpp \
//       -lglfw -lGL -o fenetre_ui
#include <cstdio>
#include "imgui.h"
#include "implot.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include "ui/panels.hpp"

// Sur un vrai poste, ce Board serait rempli par une session vivante (les memes
// couches que les scripts m01_*). Ici : les chiffres MESURES de m01_corridor.
static fen::ui::Board make_board() {
  fen::ui::Board B;
  B.mission = "M01 — Terre -> Mars, depart 2026-10-30, TOF 294 j";
  B.vinf_arr_kms = 2.704;
  B.b_min_km=7357; B.b_max_km=7760; B.b_aim_km=7674; B.bt_aim_km=6646; B.br_aim_km=3837;
  B.rp_min_km=3546; B.rp_max_km=3865;
  B.events = {{0,"lancement",0},{30,"TCM-1",1},{30,"poursuite",2},{120,"TCM-2",1},
              {120,"poursuite",2},{279,"TCM-3",1},{294,"insertion Mars",1},{285,"poursuite",2}};
  B.m0_kg=2650; B.prop_kg=1180; B.dry_kg=1470;
  B.dv_budget=1710; B.dv_used=1561; B.dv_margin=149;
  B.tracking_musd=18.4; B.cost_total_musd=214.0; B.p_success=0.94;
  return B;
}

int main() {
  if (!glfwInit()) { std::printf("glfwInit a echoue\n"); return 1; }
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
  GLFWwindow* win = glfwCreateWindow(1280, 1040, "FENETRE — console de vol", nullptr, nullptr);
  if (!win) { std::printf("pas de fenetre (pas de display ?)\n"); glfwTerminate(); return 1; }
  glfwMakeContextCurrent(win);
  glfwSwapInterval(1);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImPlot::CreateContext();
  ImGui::StyleColorsDark();
  ImGui::GetStyle().WindowRounding = 2.0f;
  ImGui_ImplGlfw_InitForOpenGL(win, true);
  ImGui_ImplOpenGL3_Init("#version 130");

  fen::ui::Board B = make_board();
  while (!glfwWindowShouldClose(win)) {
    glfwPollEvents();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    fen::ui::draw_all(B);          // <-- interactif : les boutons radio marchent
    ImGui::Render();
    int w, h; glfwGetFramebufferSize(win, &w, &h);
    glViewport(0, 0, w, h);
    glClearColor(0.09f, 0.09f, 0.11f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(win);
  }
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImPlot::DestroyContext();
  ImGui::DestroyContext();
  glfwDestroyWindow(win);
  glfwTerminate();
  return 0;
}
