// ui/hud.hpp - primitives de "salle de vol" : jauges, cadrans, vue 3D filaire.
// ZERO physique : tout est passe en argument depuis app::Jeu. Dessine avec le
// DrawList d'ImGui (aucune ressource externe, aucun shader).
#pragma once
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include "imgui.h"
#include "fen/core/Vec3.hpp"

namespace fen::ui {

inline ImU32 col(float r, float g, float b, float a = 1) { return ImGui::GetColorU32(ImVec4(r, g, b, a)); }

// --- barre-jauge horizontale etiquetee ------------------------------------
inline void jauge(const char* label, double val, double vmax, const char* unite,
                  ImVec4 teinte, double seuil = -1) {
  ImGui::TextUnformatted(label);
  ImGui::SameLine(150);
  char t[64]; std::snprintf(t, sizeof(t), "%.1f %s", val, unite);
  const float frac = vmax > 0 ? (float)std::fmax(0.0, std::fmin(1.0, val / vmax)) : 0;
  ImVec4 c = teinte;
  if (seuil > 0 && val < seuil) c = ImVec4(0.95f, 0.4f, 0.3f, 1);
  ImGui::PushStyleColor(ImGuiCol_PlotHistogram, c);
  ImGui::ProgressBar(frac, ImVec2(-1, 16), t);
  ImGui::PopStyleColor();
}

// --- cadran circulaire type "artificial horizon" pour une valeur 0..1 ------
inline void cadran(const char* label, float frac, const char* centre, ImVec4 teinte, float rayon = 46) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImVec2 p = ImGui::GetCursorScreenPos();
  ImVec2 c = ImVec2(p.x + rayon, p.y + rayon);
  dl->AddCircle(c, rayon, col(0.4f, 0.45f, 0.5f, 1), 48, 2.0f);
  dl->AddCircle(c, rayon - 6, col(0.2f, 0.22f, 0.26f, 1), 48, 1.0f);
  const float a0 = 2.4f, a1 = 2.4f + 4.4f;   // arc de 3/4
  const int N = 40;
  for (int i = 0; i < N; ++i) {
    const float t0 = a0 + (a1 - a0) * i / N, t1 = a0 + (a1 - a0) * (i + 1) / N;
    const bool on = (float)i / N <= frac;
    dl->AddLine(ImVec2(c.x + (rayon - 4) * cosf(t0), c.y + (rayon - 4) * sinf(t0)),
                ImVec2(c.x + (rayon - 4) * cosf(t1), c.y + (rayon - 4) * sinf(t1)),
                on ? ImGui::GetColorU32(teinte) : col(0.25f, 0.28f, 0.32f, 1), 3.0f);
  }
  const float ang = a0 + (a1 - a0) * std::fmax(0.f, std::fmin(1.f, frac));
  dl->AddLine(c, ImVec2(c.x + (rayon - 10) * cosf(ang), c.y + (rayon - 10) * sinf(ang)),
              ImGui::GetColorU32(teinte), 2.5f);
  const ImVec2 ts = ImGui::CalcTextSize(centre);
  dl->AddText(ImVec2(c.x - ts.x / 2, c.y - ts.y / 2), col(0.9f, 0.92f, 0.95f, 1), centre);
  const ImVec2 ls = ImGui::CalcTextSize(label);
  dl->AddText(ImVec2(c.x - ls.x / 2, c.y + rayon - 14), col(0.6f, 0.64f, 0.7f, 1), label);
  ImGui::Dummy(ImVec2(rayon * 2, rayon * 2 + 4));
}

// --- petite pastille d'etat (station de poursuite active/inactive) ---------
inline void voyant(const char* nom, bool actif) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImVec2 p = ImGui::GetCursorScreenPos();
  dl->AddCircleFilled(ImVec2(p.x + 7, p.y + 9), 6,
                      actif ? col(0.35f, 0.9f, 0.45f, 1) : col(0.3f, 0.32f, 0.36f, 1));
  if (actif) dl->AddCircle(ImVec2(p.x + 7, p.y + 9), 9, col(0.35f, 0.9f, 0.45f, 0.5f), 16, 1.5f);
  ImGui::Dummy(ImVec2(18, 18)); ImGui::SameLine();
  ImGui::TextColored(actif ? ImVec4(0.7f, 0.95f, 0.75f, 1) : ImVec4(0.5f, 0.52f, 0.56f, 1), "%s", nom);
}

// --- VUE 3D FILAIRE : orbites + Terre + marqueur vaisseau -------------------
// Projection isometrique tournante, dessin au DrawList. Entrees en km.
struct Vue3D {
  float yaw = 0.6f, pitch = 0.5f, zoom = 1.0f;
  ImVec2 origine; float echelle;   // calcules par begin()

  void projeter(double x, double y, double z, float& sx, float& sy) const {
    // rotation yaw autour de z, puis pitch autour de x, puis projection ortho
    const double cx = cos(yaw), sy_ = sin(yaw);
    double X = x * cx - y * sy_;
    double Y = x * sy_ + y * cx;
    const double cp = cos(pitch), sp = sin(pitch);
    double Yp = Y * cp - z * sp;
    sx = origine.x + (float)(X * echelle);
    sy = origine.y - (float)(Yp * echelle);
  }
  void cadrer(ImVec2 centre, float rayon_ecran, double rayon_monde_km) {
    origine = centre; echelle = (float)(rayon_ecran * zoom / rayon_monde_km);
  }
  void ligne(ImDrawList* dl, double x0, double y0, double z0,
             double x1, double y1, double z1, ImU32 c, float ep = 1.5f) const {
    float a, b, cc, d; projeter(x0, y0, z0, a, b); projeter(x1, y1, z1, cc, d);
    dl->AddLine(ImVec2(a, b), ImVec2(cc, d), c, ep);
  }
  void polyligne(ImDrawList* dl, const std::vector<double>& X, const std::vector<double>& Y,
                 const std::vector<double>& Z, ImU32 c, float ep = 1.5f) const {
    for (size_t i = 1; i < X.size(); ++i)
      ligne(dl, X[i-1], Y[i-1], Z.empty()?0:Z[i-1], X[i], Y[i], Z.empty()?0:Z[i], c, ep);
  }
  void cercle_equatorial(ImDrawList* dl, double r_km, ImU32 c, float ep = 1.5f) const {
    const int N = 72; float px = 0, py = 0, fx, fy;
    for (int i = 0; i <= N; ++i) {
      const double a = 2 * 3.14159265 * i / N;
      projeter(r_km * cos(a), r_km * sin(a), 0, fx, fy);
      if (i) dl->AddLine(ImVec2(px, py), ImVec2(fx, fy), c, ep);
      px = fx; py = fy;
    }
  }
  void sphere_fil(ImDrawList* dl, double r_km, ImU32 c) const {
    cercle_equatorial(dl, r_km, c, 1.5f);
    const int N = 48; float px = 0, py = 0, fx, fy;
    for (int i = 0; i <= N; ++i) {   // meridien XZ
      const double a = 2 * 3.14159265 * i / N;
      projeter(r_km * cos(a), 0, r_km * sin(a), fx, fy);
      if (i) dl->AddLine(ImVec2(px, py), ImVec2(fx, fy), c, 1.0f);
      px = fx; py = fy;
    }
    for (int i = 0; i <= N; ++i) {   // meridien YZ
      const double a = 2 * 3.14159265 * i / N;
      projeter(0, r_km * cos(a), r_km * sin(a), fx, fy);
      if (i) dl->AddLine(ImVec2(px, py), ImVec2(fx, fy), c, 1.0f);
      px = fx; py = fy;
    }
  }
  void marqueur(ImDrawList* dl, const Vec3& p_km, ImU32 c, const char* txt) const {
    float sx, sy; projeter(p_km.x, p_km.y, p_km.z, sx, sy);
    dl->AddCircleFilled(ImVec2(sx, sy), 5, c);
    dl->AddCircle(ImVec2(sx, sy), 9, c, 16, 1.5f);
    if (txt) dl->AddText(ImVec2(sx + 11, sy - 7), c, txt);
  }
};

} // namespace fen::ui
