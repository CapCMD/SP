// ui/panels.hpp — LES PANNEAUX. ZERO PHYSIQUE (architecture, regle 1).
//
// Rien ici ne CALCULE. Tout est passe par `Board`, rempli par les couches du
// dessous. Un panneau qui calcule est un panneau qui MENT tot ou tard : il
// diverge de la verite du moteur sans que personne ne s'en apercoive.
//
// Style : schema d'ingenieur. ZERO art. Trois panneaux, plus LA PIECE MAITRESSE.
#pragma once
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include "imgui.h"
#include "implot.h"

namespace fen::ui {

struct Event { double t_days; std::string label; int kind; };   // 0=jalon 1=brulure 2=poursuite
struct Tcm   { const char* name; double ellipse_km; double p_success; double dv_ms; };

// TOUT ce que l'UI affiche. Rempli en dehors d'elle. Elle ne fait que MONTRER.
struct Board {
  // --- le corridor du plan-B (deduit, pas regle) ---
  double vinf_arr_kms{2.704};
  double b_min_km{7357.0};       // borne INTERIEURE : l'atmosphere
  double b_max_km{7760.0};       // borne EXTERIEURE : le budget d'insertion
  double b_aim_km{7674.0};
  double bt_aim_km{6646.0}, br_aim_km{3837.0};
  double rp_min_km{3546.0}, rp_max_km{3865.0};
  // --- l'echelle de TCM : le joueur choisit, la physique tranche ---
  std::vector<Tcm> tcm{
      {"aucune correction",        87778.0, 0.00, 0.0},
      {"TCM precoce seule",         5195.0, 0.29, 12.0},
      {"TCM tardive seule",          539.0, 0.82, 12.0},
      {"les DEUX (echelle)",         121.0, 1.00, 12.0}};
  int tcm_choice{1};
  // --- chronologie ---
  std::vector<Event> events;
  // --- chiffres ---
  double m0_kg{}, dry_kg{}, prop_kg{}, dv_budget{}, dv_used{}, dv_margin{};
  double tracking_musd{}, cost_total_musd{}, p_success{};
  std::string mission{"M01 — Terre -> Mars"};
};

// ---------------------------------------------------------------------------
// LA PIECE MAITRESSE. Le corridor, et l'ellipse de dispersion 3-sigma dedans.
// C'est la seule interface qui fasse comprendre SANS LIRE UN CHIFFRE si la
// marge tient : si l'ellipse deborde de l'anneau, la mission est perdue.
// ---------------------------------------------------------------------------
inline void panel_corridor(Board& B) {
  ImGui::Begin("CORRIDOR DU PLAN-B  (plan B, coordonnees B.T / B.R)");

  ImGui::TextWrapped(
      "L'anneau n'a ete DESSINE par personne. Il est DEDUIT : la borne interieure "
      "est l'atmosphere (r_p >= %.0f km), la borne exterieure est le budget "
      "d'insertion. Largeur : %.0f km. C'est une CONSEQUENCE.",
      B.rp_min_km, B.b_max_km - B.b_min_km);
  ImGui::Separator();

  ImGui::Text("Correction de mi-parcours achetee :");
  for (int i = 0; i < (int)B.tcm.size(); ++i) {
    ImGui::RadioButton(B.tcm[i].name, &B.tcm_choice, i);
    if (i < (int)B.tcm.size() - 1) ImGui::SameLine();
  }
  const Tcm& T = B.tcm[B.tcm_choice];
  ImGui::Separator();

  const bool tient = (T.ellipse_km < 0.5 * (B.b_max_km - B.b_min_km));
  ImGui::TextColored(tient ? ImVec4(0.35f, 0.85f, 0.45f, 1) : ImVec4(0.95f, 0.35f, 0.30f, 1),
                     "ellipse 3-sigma : %8.0f km   |   P(succes) = %3.0f %%   |   %s",
                     T.ellipse_km, 100.0 * T.p_success,
                     tient ? "LA MARGE TIENT" : "L'ELLIPSE DEBORDE DE L'ANNEAU");

  if (ImPlot::BeginPlot("##bplane", ImVec2(-1, 470), ImPlotFlags_Equal)) {
    ImPlot::SetupAxes("B.T  [km]", "B.R  [km]");
    // cadrage : centre sur le POINT DE VISEE, pas sur l'origine, pour voir
    // l'anneau ET l'ellipse. Echelle = la plus grande des deux structures.
    const double cx = B.bt_aim_km, cy = B.br_aim_km;
    const double half = std::fmax(1.15 * (B.b_max_km - B.b_min_km), 1.6 * T.ellipse_km)
                        + 0.5 * (B.b_max_km + B.b_min_km) * 0.0;
    const double S = std::fmax(half, 1.25 * T.ellipse_km);
    ImPlot::SetupAxesLimits(cx - S, cx + S, cy - S, cy + S, ImPlotCond_Always);

    // --- l'anneau : deux cercles concentriques ---
    const int N = 240;
    std::vector<double> x(N + 1), y(N + 1);
    auto circle = [&](double r) {
      for (int i = 0; i <= N; ++i) {
        const double a = 2.0 * 3.14159265358979 * i / N;
        x[i] = r * std::cos(a); y[i] = r * std::sin(a);
      }
    };
    ImPlot::SetNextLineStyle(ImVec4(0.95f, 0.45f, 0.25f, 1), 2.0f);
    circle(B.b_min_km);
    ImPlot::PlotLine("atmosphere (r_p min)", x.data(), y.data(), N + 1);
    ImPlot::SetNextLineStyle(ImVec4(0.35f, 0.65f, 0.95f, 1), 2.0f);
    circle(B.b_max_km);
    ImPlot::PlotLine("budget d'insertion (r_p max)", x.data(), y.data(), N + 1);

    // --- le point de visee ---
    ImPlot::SetNextMarkerStyle(ImPlotMarker_Cross, 8, ImVec4(1, 1, 1, 1), 2.0f);
    double ax[1] = {B.bt_aim_km}, ay[1] = {B.br_aim_km};
    ImPlot::PlotScatter("point de visee", ax, ay, 1);

    // --- L'ELLIPSE 3-SIGMA, centree sur le point de visee ---
    // (allongee le long de la direction de visee : c'est le long du vecteur B
    //  que la dispersion s'etale, la composante transverse est bien plus serree)
    const double th = std::atan2(B.br_aim_km, B.bt_aim_km);
    const double a3 = T.ellipse_km, b3 = T.ellipse_km * 0.22;
    for (int i = 0; i <= N; ++i) {
      const double u = 2.0 * 3.14159265358979 * i / N;
      const double ex = a3 * std::cos(u), ey = b3 * std::sin(u);
      x[i] = B.bt_aim_km + ex * std::cos(th) - ey * std::sin(th);
      y[i] = B.br_aim_km + ex * std::sin(th) + ey * std::cos(th);
    }
    ImPlot::SetNextLineStyle(tient ? ImVec4(0.35f, 0.85f, 0.45f, 1)
                                   : ImVec4(0.95f, 0.35f, 0.30f, 1), 2.5f);
    ImPlot::PlotLine("dispersion 3-sigma", x.data(), y.data(), N + 1);
    ImPlot::EndPlot();
  }
  ImGui::TextDisabled("v_inf d'arrivee = %.3f km/s. Le corridor se referme quand elle monte.",
                      B.vinf_arr_kms);
  ImGui::End();
}

// --- CHRONOLOGIE : evenements dates, arcs de poussee, fenetres de poursuite --
inline void panel_timeline(Board& B) {
  ImGui::Begin("CHRONOLOGIE");
  if (ImPlot::BeginPlot("##chrono", ImVec2(-1, 200))) {
    ImPlot::SetupAxes("jours depuis le lancement", nullptr,
                      0, ImPlotAxisFlags_NoTickLabels | ImPlotAxisFlags_NoGridLines);
    ImPlot::SetupAxesLimits(-10, 310, 0, 4, ImPlotCond_Once);
    for (const auto& e : B.events) {
      double xs[2] = {e.t_days, e.t_days};
      double ys[2] = {0.4, 3.6};
      const ImVec4 c = (e.kind == 1) ? ImVec4(0.95f, 0.55f, 0.20f, 1)
                     : (e.kind == 2) ? ImVec4(0.35f, 0.65f, 0.95f, 1)
                                     : ImVec4(0.80f, 0.80f, 0.80f, 1);
      ImPlot::SetNextLineStyle(c, (e.kind == 1) ? 3.0f : 1.5f);
      ImPlot::PlotLine(e.label.c_str(), xs, ys, 2);
      ImPlot::Annotation(e.t_days, 3.7, c, ImVec2(0, -6), true, "%s", e.label.c_str());
    }
    ImPlot::EndPlot();
  }
  ImGui::TextDisabled("orange = manoeuvre  |  bleu = fenetre de poursuite achetee  |  gris = jalon");
  ImGui::End();
}

// --- CHIFFRES : masses, Delta-v, marges. Rien de decoratif. -----------------
inline void panel_numbers(Board& B) {
  ImGui::Begin("CHIFFRES");
  ImGui::Text("%s", B.mission.c_str());
  ImGui::Separator();
  if (ImGui::BeginTable("n", 2, ImGuiTableFlags_SizingStretchProp)) {
    auto row = [](const char* k, const char* fmt, double v) {
      ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextUnformatted(k);
      ImGui::TableNextColumn(); ImGui::Text(fmt, v);
    };
    row("masse au decollage",  "%8.0f kg",   B.m0_kg);
    row("  dont ergols",       "%8.0f kg",   B.prop_kg);
    row("  dont a sec",        "%8.0f kg",   B.dry_kg);
    ImGui::TableNextRow();
    row("Delta-v embarque",    "%8.0f m/s",  B.dv_budget);
    row("  consomme",          "%8.0f m/s",  B.dv_used);
    row("  MARGE",             "%8.0f m/s",  B.dv_margin);
    ImGui::TableNextRow();
    row("navigation achetee",  "%8.1f M$",   B.tracking_musd);
    row("cout du programme",   "%8.1f M$",   B.cost_total_musd);
    row("P(succes)",           "%8.1f %%",   100.0 * B.p_success);
    ImGui::EndTable();
  }
  ImGui::Separator();
  const double frac = (B.dv_budget > 0) ? B.dv_margin / B.dv_budget : 0.0;
  ImGui::Text("marge de correction");
  ImGui::ProgressBar((float)std::fmax(0.0, std::fmin(1.0, frac)), ImVec2(-1, 0));
  ImGui::End();
}

inline void draw_all(Board& B) {
  panel_corridor(B);
  panel_timeline(B);
  panel_numbers(B);
}

} // namespace fen::ui
