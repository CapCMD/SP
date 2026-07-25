// ui/game.hpp — LA COQUILLE DE JEU, refaite pour suivre LES DEUX BOUCLES DU GDD.
//
// Le reproche etait juste : la version precedente n'exposait qu'un choix de
// poursuite. Le GDD (Boucle A puis Boucle B) exige que le joueur CONCOIVE avant
// d'executer. La salle de vol de M00 a donc maintenant DEUX phases :
//
//   PHASE 1 — CONCEPTION : le joueur DERIVE ses Delta-v. Le jeu verifie chaque
//             valeur, lui enseigne la manoeuvre combinee (1154,5 m/s), et ne le
//             laisse avancer que si sa conception est payable. (outil m00_design)
//   PHASE 2 — VOL : il achete de la navigation, vole, et recoit un POST-MORTEM
//             qui DECOMPOSE l'echec au lieu de dire "rate". (outil m00_play)
//
// L'UI ne calcule toujours RIEN (regle 1). Elle DELEGUE aux binaires de verite
// (m00_design.exe, m00_play.exe) et affiche leur sortie. Un seul moteur.
#pragma once
#include <cstdio>
#include <functional>
#include <string>
#include <vector>
#include "imgui.h"
#include "implot.h"
#include "ui/panels.hpp"

namespace fen::ui {

enum class Screen { Title, MissionSelect, Briefing, Flight };
enum class Phase  { Conception, Vol };

struct MissionCard {
  std::string id, title, subtitle, briefing;
  bool playable{false};        // M00 : jouable de bout en bout. M01 : visualisation.
  bool has_corridor{false};
};

struct Game {
  Screen screen{Screen::Title};
  Phase  phase{Phase::Conception};
  int    selected{-1};
  bool   quit{false};

  // --- PHASE 1 : conception. Le joueur SAISIT ses derivations. ---
  char   dv_inj_buf[32]  = "";        // Delta-v d'injection qu'il a calcule
  char   dv_comb_buf[32] = "";        // insertion combinee qu'il a calculee
  std::string design_out;             // sortie de m00_design (verif/corrige)
  bool   design_ok{false};            // les deux valeurs justes -> vol debloque
  bool   design_checked{false};

  // --- PHASE 2 : vol. ---
  int    track_level{5};
  int    seed{4071};
  std::string flight_out;
  bool   flight_ran{false}, last_ok{false};

  Board  board;                       // M01 : le corridor
  std::vector<MissionCard> missions;

  // delegation aux binaires de verite. args = ligne de commande complete.
  std::function<std::string(const std::string& tool, const std::string& args)> run;

  Game() {
    missions.push_back({
        "M00", "La manoeuvre qui ferme le bilan",
        "Tutoriel  —  LEO 200 km / 28,5 deg  ->  GEO",
        "Le cahier des charges ne te donne AUCUN Delta-v. Tu dois les DERIVER.\n\n"
        "Puis tu voles : tu ne connais pas ta position vraie, tu ACHETES de la "
        "navigation pour l'estimer, et tu corriges. Le jeu ne te corrige jamais : "
        "il propage, et il te juge sur la REALITE.\n\n"
        "OBJECTIF : a = 42 164 km +/- 50, e < 2e-3, i < 0,25 deg.",
        true, false});
    missions.push_back({
        "M01", "Le corridor du plan-B",
        "Terre -> Mars  —  visualisation (pas encore jouable)",
        "Une trajectoire vers Mars vise un ANNEAU dans le plan-B. Ta dispersion "
        "3-sigma doit y tenir. Pour l'instant, cet ecran est une VISUALISATION : "
        "tu vois l'anneau et l'ellipse selon la correction achetee, mais le "
        "pilotage des corrections n'est pas encore branche.",
        false, true});
  }

  void draw() {
    switch (screen) {
      case Screen::Title:         draw_title();  break;
      case Screen::MissionSelect: draw_select(); break;
      case Screen::Briefing:      draw_brief();  break;
      case Screen::Flight:        draw_flight(); break;
    }
  }

  // appele quand le joueur clique VERIFIER en phase conception
  void check_design() {
    if (!run) return;
    std::string a = dv_inj_buf[0] ? dv_inj_buf : "0";
    std::string c = dv_comb_buf[0] ? dv_comb_buf : "0";
    design_out = run("m00_design", "--check " + a + " " + c);
    design_checked = true;
    design_ok = design_out.find("Les deux justes") != std::string::npos;
  }
  void show_corrige() {
    if (!run) return;
    design_out = run("m00_design", "");
  }
  void fly_now() {
    if (!run) return;
    char a[64]; std::snprintf(a, sizeof(a), "%d %d", seed, track_level);
    flight_out = run("m00_play", a);
    flight_ran = true;
    last_ok = flight_out.find("MISSION REUSSIE") != std::string::npos;
  }

 private:
  void centered(const char* t, float s=1.0f){
    float w=ImGui::GetWindowSize().x, tw=ImGui::CalcTextSize(t).x*s;
    ImGui::SetCursorPosX((w-tw)*0.5f);
    if(s!=1.0f){ImGui::SetWindowFontScale(s);ImGui::TextUnformatted(t);ImGui::SetWindowFontScale(1.0f);}
    else ImGui::TextUnformatted(t);
  }

  void draw_title() {
    ImGui::Begin("##t",nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoScrollbar);
    ImGui::Dummy(ImVec2(0,80)); centered("FENETRE",3.5f);
    ImGui::Dummy(ImVec2(0,8)); centered("le joueur concoit, le monde propage, la physique tranche");
    ImGui::Dummy(ImVec2(0,60));
    const float bw=280,bh=46;
    ImGui::SetCursorPosX((ImGui::GetWindowSize().x-bw)*0.5f);
    if(ImGui::Button("JOUER",ImVec2(bw,bh))) screen=Screen::MissionSelect;
    ImGui::Dummy(ImVec2(0,12));
    ImGui::SetCursorPosX((ImGui::GetWindowSize().x-bw)*0.5f);
    if(ImGui::Button("Quitter",ImVec2(bw,bh))) quit=true;
    ImGui::Dummy(ImVec2(0,90));
    centered("v0.3 — boucle de conception + vol + post-mortem — 84 oracles au vert");
    ImGui::End();
  }

  void draw_select() {
    ImGui::Begin("##s",nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove);
    ImGui::Dummy(ImVec2(0,20)); centered("CHOISIS TA MISSION",2.0f); ImGui::Dummy(ImVec2(0,26));
    for(int i=0;i<(int)missions.size();++i){
      const auto&m=missions[i]; ImGui::PushID(i);
      ImGui::BeginChild("c",ImVec2(-1,120),true);
      ImGui::SetWindowFontScale(1.4f);
      ImGui::Text("%s — %s",m.id.c_str(),m.title.c_str());
      ImGui::SetWindowFontScale(1.0f);
      ImGui::TextDisabled("%s",m.subtitle.c_str());
      if(!m.playable){ ImGui::SameLine(); ImGui::TextColored(ImVec4(0.9f,0.7f,0.3f,1)," [visualisation]"); }
      ImGui::Dummy(ImVec2(0,6));
      if(ImGui::Button("Ouvrir >",ImVec2(140,32))){ selected=i; reset_mission(); screen=Screen::Briefing; }
      ImGui::EndChild(); ImGui::PopID(); ImGui::Dummy(ImVec2(0,10));
    }
    ImGui::Dummy(ImVec2(0,8));
    if(ImGui::Button("< Menu",ImVec2(120,34))) screen=Screen::Title;
    ImGui::End();
  }

  void reset_mission() {
    phase=Phase::Conception; design_checked=false; design_ok=false;
    design_out.clear(); flight_ran=false; flight_out.clear();
    dv_inj_buf[0]=0; dv_comb_buf[0]=0;
  }

  void draw_brief() {
    const auto&m=missions[selected];
    ImGui::Begin("##b",nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove);
    ImGui::Dummy(ImVec2(0,16)); centered((m.id+" — "+m.title).c_str(),1.8f);
    ImGui::Dummy(ImVec2(0,4)); centered(m.subtitle.c_str());
    ImGui::Dummy(ImVec2(0,18));
    ImGui::BeginChild("x",ImVec2(-1,-70),true);
    ImGui::PushTextWrapPos(0.0f); ImGui::TextUnformatted(m.briefing.c_str()); ImGui::PopTextWrapPos();
    ImGui::EndChild(); ImGui::Dummy(ImVec2(0,8));
    if(ImGui::Button("< Retour",ImVec2(120,40))) screen=Screen::MissionSelect;
    ImGui::SameLine();
    const float rw=240; ImGui::SetCursorPosX(ImGui::GetWindowSize().x-rw-16);
    if(ImGui::Button(m.playable?"COMMENCER LA CONCEPTION":"OUVRIR LA VISUALISATION",ImVec2(rw,40))){
      if(m.playable){ phase=Phase::Conception; if(run) show_corrige(); }
      screen=Screen::Flight;
    }
    ImGui::End();
  }

  void draw_flight() {
    const auto&m=missions[selected];
    if(m.has_corridor){ panel_corridor(board); panel_numbers(board); }
    else if(phase==Phase::Conception) draw_conception();
    else draw_vol();
    // barre de navigation
    ImGui::Begin("##nav",nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove);
    if(ImGui::Button("< Missions",ImVec2(130,34))) screen=Screen::MissionSelect;
    ImGui::SameLine(); if(ImGui::Button("Menu",ImVec2(90,34))) screen=Screen::Title;
    if(m.playable){
      ImGui::SameLine();
      if(phase==Phase::Vol){ if(ImGui::Button("< Revoir la conception",ImVec2(200,34))) phase=Phase::Conception; }
    }
    ImGui::End();
  }

  // PHASE 1 — le joueur derive ses Delta-v, le jeu enseigne
  void draw_conception() {
    ImGui::Begin("PHASE 1 — CONCEPTION : derive tes Delta-v");
    ImGui::TextWrapped("Le cahier des charges ne donne aucun Delta-v. A toi de les "
                       "calculer (v = sqrt(mu/r), vis-viva, loi des cosinus pour la "
                       "manoeuvre combinee). Saisis tes deux valeurs cle :");
    ImGui::Separator();
    ImGui::SetNextItemWidth(180);
    ImGui::InputText("Delta-v injection (m/s)", dv_inj_buf, sizeof(dv_inj_buf));
    ImGui::SetNextItemWidth(180);
    ImGui::InputText("insertion combinee (m/s)", dv_comb_buf, sizeof(dv_comb_buf));
    if(ImGui::Button("VERIFIER MES DERIVATIONS",ImVec2(-1,40))) check_design();
    ImGui::SameLine(0,0);
    ImGui::Dummy(ImVec2(0,4));
    if(ImGui::Button("je seche : montre-moi le corrige complet",ImVec2(-1,0))) show_corrige();
    if(design_ok){
      ImGui::Separator();
      ImGui::TextColored(ImVec4(0.35f,0.85f,0.45f,1),"Conception validee. Tu peux passer au vol.");
      if(ImGui::Button(">>> PASSER AU VOL >>>",ImVec2(-1,44))) phase=Phase::Vol;
    } else if(design_checked){
      ImGui::Separator();
      ImGui::TextColored(ImVec4(0.95f,0.6f,0.3f,1),"Pas encore : lis le diagnostic a droite.");
      ImGui::TextDisabled("(tu peux quand meme forcer le passage au vol pour experimenter)");
      if(ImGui::Button("passer au vol quand meme",ImVec2(-1,0))) phase=Phase::Vol;
    }
    ImGui::End();

    ImGui::Begin("LE PROFESSEUR");
    ImGui::BeginChild("d",ImVec2(-1,-1),true,ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::TextUnformatted(design_out.empty()
        ? "Saisis tes valeurs et clique VERIFIER, ou demande le corrige."
        : design_out.c_str());
    ImGui::EndChild();
    ImGui::End();
  }

  // PHASE 2 — vol + post-mortem
  void draw_vol() {
    ImGui::Begin("PHASE 2 — VOL : achete ta navigation, puis vole");
    ImGui::TextWrapped("Tu ne vois pas ta position vraie. Achete de la navigation "
                       "pour l'estimer. Trop peu -> tu perds la sonde. Trop -> tu "
                       "brules ton budget.");
    ImGui::Separator();
    static const char* names[7]={
        "0 — AVEUGLE (0 M$, ~5%%)","1 — 1 station 30 min (~50%%)",
        "2 — 1 station 3h (~40%%)","3 — 1 arc/manoeuvre (~40%%)",
        "4 — 3 stations, arcs courts (~85%%)","5 — 3 stations, arcs COMPLETS (~90%%)",
        "6 — tout + 2 revolutions (~100%%, 33 M$)"};
    ImGui::Text("NIVEAU DE POURSUITE :");
    for(int l=0;l<=6;++l) ImGui::RadioButton(names[l],&track_level,l);
    ImGui::Separator();
    ImGui::SetNextItemWidth(160); ImGui::InputInt("graine",&seed);
    ImGui::SameLine(); if(ImGui::Button("aleatoire")) seed=(int)(ImGui::GetTime()*1000)%100000;
    ImGui::TextDisabled("rejoue plusieurs graines : la reussite est une probabilite.");
    ImGui::Separator();
    if(ImGui::Button("  >>>  VOLER  <<<  ",ImVec2(-1,52))) fly_now();
    ImGui::End();

    ImGui::Begin("RESULTAT + POST-MORTEM");
    if(!flight_ran) ImGui::TextDisabled("Choisis un niveau, puis clique VOLER.");
    else{
      ImGui::SetWindowFontScale(1.6f);
      ImGui::TextColored(last_ok?ImVec4(0.35f,0.85f,0.45f,1):ImVec4(0.95f,0.35f,0.30f,1),
                         last_ok?"MISSION REUSSIE":"MISSION RATEE");
      ImGui::SetWindowFontScale(1.0f); ImGui::Separator();
      ImGui::BeginChild("l",ImVec2(-1,-1),true,ImGuiWindowFlags_HorizontalScrollbar);
      ImGui::TextUnformatted(flight_out.c_str());
      ImGui::EndChild();
    }
    ImGui::End();
  }
};

} // namespace fen::ui
