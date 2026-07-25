// ui/main_headless.cpp — rend UNE frame de l'UI hors-ecran, en PNG.
// Sur une machine sans display : OSMesa. Le meme panels.hpp tournerait sous
// GLFW+OpenGL sur un poste normal (voir ui/main_glfw.cpp).
#include <GL/osmesa.h>
#include <GL/gl.h>
#include <cstdio>
#include <cstring>
#include <vector>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include "imgui.h"
#include "implot.h"
#include "backends/imgui_impl_opengl3.h"
#include "ui/panels.hpp"

// L'UI ne calcule rien : on lui PASSE les chiffres, mesures par les couches
// du dessous (m01_corridor, assess, ...). Ici on les recopie tels quels.
static fen::ui::Board make_board() {
  fen::ui::Board B;
  B.mission = "M01 — Terre -> Mars, depart 2026-10-30, TOF 294 j";
  B.vinf_arr_kms = 2.704;
  B.b_min_km = 7357; B.b_max_km = 7760; B.b_aim_km = 7674;
  B.bt_aim_km = 6646; B.br_aim_km = 3837;
  B.rp_min_km = 3546; B.rp_max_km = 3865;
  B.events = {
      {0,   "lancement", 0}, {30,  "TCM-1", 1}, {30, "poursuite", 2},
      {120, "TCM-2", 1}, {120, "poursuite", 2}, {279, "TCM-3", 1},
      {294, "insertion Mars", 1}, {285, "poursuite", 2}};
  B.m0_kg = 2650; B.prop_kg = 1180; B.dry_kg = 1470;
  B.dv_budget = 1710; B.dv_used = 1561; B.dv_margin = 149;
  B.tracking_musd = 18.4; B.cost_total_musd = 214.0; B.p_success = 0.94;
  return B;
}

int main() {
  const int W = 1280, H = 1600;
  OSMesaContext ctx = OSMesaCreateContextExt(OSMESA_RGBA, 24, 8, 0, nullptr);
  std::vector<unsigned char> buf(W * H * 4);
  if (!OSMesaMakeCurrent(ctx, buf.data(), GL_UNSIGNED_BYTE, W, H)) {
    std::printf("OSMesaMakeCurrent a echoue\n"); return 1;
  }

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImPlot::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.DisplaySize = ImVec2((float)W, (float)H);
  io.Fonts->AddFontDefault();
  io.Fonts->Build();
  ImGui::StyleColorsDark();
  ImGui::GetStyle().WindowRounding = 2.0f;
  ImGui_ImplOpenGL3_Init("#version 130");

  fen::ui::Board B = make_board();

  const char* names[4] = {"ui_corridor_0.png","ui_corridor_1.png","ui_corridor_2.png","ui_corridor_3.png"};
  for (int choice = 0; choice < 4; ++choice) {
  B.tcm_choice = choice;
  for (int frame = 0; frame < 2; ++frame) {
    ImGui_ImplOpenGL3_NewFrame();
    io.DeltaTime = 1.0f / 60.0f;
    ImGui::NewFrame();

    // disposition fixe (pas d'interaction en mode image)
    ImGui::SetNextWindowPos(ImVec2(12, 12), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(760, 720), ImGuiCond_Always);
    fen::ui::panel_corridor(B);
    ImGui::SetNextWindowPos(ImVec2(784, 12), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(484, 360), ImGuiCond_Always);
    fen::ui::panel_numbers(B);
    ImGui::SetNextWindowPos(ImVec2(12, 744), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(1256, 300), ImGuiCond_Always);
    fen::ui::panel_timeline(B);

    ImGui::Render();
    glViewport(0, 0, W, H);
    glClearColor(0.09f, 0.09f, 0.11f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glFinish();
  }

  std::vector<unsigned char> flip(W * H * 4);
  for (int y = 0; y < H; ++y)
    std::memcpy(&flip[(H - 1 - y) * W * 4], &buf[y * W * 4], W * 4);
  stbi_write_png(names[choice], W, H, 4, flip.data(), W * 4);
  std::printf(">>> %s ecrit\n", names[choice]);
  }

  ImGui_ImplOpenGL3_Shutdown();
  ImPlot::DestroyContext();
  ImGui::DestroyContext();
  OSMesaDestroyContext(ctx);
  return 0;
}
