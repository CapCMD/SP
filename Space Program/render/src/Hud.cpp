// spr/Hud.cpp
#include "spr/Hud.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "imgui.h"

namespace spr {

namespace {

// Projette une position MONDE (double) a l'ecran via la camera. false si derriere.
bool project_to_screen(const Camera& cam, const Dvec3& world, float aspect,
                       float w, float h, ImVec2& out) {
  const Vec3 rel = cam.world_to_render(world);
  const Mat4 mvp = cam.proj(aspect) * cam.view();
  const Vec4 clip = mvp * Vec4{rel, 1.0f};
  if (clip.w <= 1e-6f) return false;
  out.x = ((clip.x / clip.w) * 0.5f + 0.5f) * w;
  out.y = ((clip.y / clip.w) * 0.5f + 0.5f) * h;
  return true;
}

// Texte en MAJUSCULES espacees (facon NASA Eyes) sur une draw list.
void spaced_caps(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, ImU32 col,
                 const char* s, float spacing) {
  float x = pos.x;
  for (const char* p = s; *p; ++p) {
    char c[2] = {static_cast<char>(std::toupper(static_cast<unsigned char>(*p))), 0};
    dl->AddText(font, size, ImVec2(x, pos.y), col, c);
    x += font->CalcTextSizeA(size, FLT_MAX, 0.0f, c).x + spacing;
  }
}
const char* kMonth[12] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                          "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

// "2026-07-17T15:39:06..." -> "JUL 17, 2026" + "03:39:06 PM".
void fmt_datetime(const char* iso, char* date_out, char* time_out) {
  int y = 0, mo = 1, d = 1, h = 0, mi = 0, se = 0;
  std::sscanf(iso, "%4d-%2d-%2dT%2d:%2d:%2d", &y, &mo, &d, &h, &mi, &se);
  if (mo < 1 || mo > 12) mo = 1;
  std::snprintf(date_out, 24, "%s %d, %d", kMonth[mo - 1], d, y);
  const char* ap = (h < 12) ? "AM" : "PM";
  int h12 = h % 12; if (h12 == 0) h12 = 12;
  std::snprintf(time_out, 16, "%02d:%02d:%02d %s", h12, mi, se, ap);
}

const char* rate_name(TimeMode m, int rev) {
  switch (m) {
    case TimeMode::Pause:       return "PAUSED";
    case TimeMode::RealTime:    return "REAL RATE";
    case TimeMode::DayPerSec:   return rev ? "- 1 DAY / SEC"   : "1 DAY / SEC";
    case TimeMode::WeekPerSec:  return rev ? "- 1 WEEK / SEC"  : "1 WEEK / SEC";
    case TimeMode::MonthPerSec: return rev ? "- 1 MONTH / SEC" : "1 MONTH / SEC";
    default:                    return "";
  }
}

// Scrubber : 7 crans. Centre (3) = temps reel ; droite = avance rapide ; gauche =
// recul. Renvoie le cran a partir de l'etat temps courant.
int stop_from_time(const TimeControl& tc) {
  switch (tc.mode) {
    case TimeMode::MonthPerSec: return tc.reverse ? 0 : 6;
    case TimeMode::WeekPerSec:  return tc.reverse ? 1 : 5;
    case TimeMode::DayPerSec:   return tc.reverse ? 2 : 4;
    default:                    return 3;   // RealTime / Pause : centre
  }
}
void time_from_stop(TimeControl& tc, int stop) {
  static const TimeMode M[7] = {TimeMode::MonthPerSec, TimeMode::WeekPerSec, TimeMode::DayPerSec,
                                TimeMode::RealTime, TimeMode::DayPerSec, TimeMode::WeekPerSec,
                                TimeMode::MonthPerSec};
  tc.mode = M[std::clamp(stop, 0, 6)];
  tc.reverse = (stop < 3) ? 1 : 0;
}

// Fenetre ImGui minimaliste, sans decor (overlay).
void begin_overlay(const char* id, ImVec2 pos, ImVec2 pivot) {
  ImGui::SetNextWindowPos(pos, ImGuiCond_Always, pivot);
  ImGui::Begin(id, nullptr,
               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
               ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBackground |
               ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize |
               ImGuiWindowFlags_NoFocusOnAppearing);
}

// --- HUD "debug" legacy (garde pour l'app de demonstration, map == nullptr) ---
void row(const char* label, const char* fmt, double v) {
  ImGui::TextUnformatted(label);
  ImGui::SameLine(180.0f);
  char buf[64]; std::snprintf(buf, sizeof buf, fmt, v);
  ImGui::TextUnformatted(buf);
}
void legacy_hud(const RenderSnapshot& s, Camera& cam, const char* device_name, float fps,
                bool show_telemetry, bool show_bodies) {
  if (ImGui::BeginMainMenuBar()) {
    ImGui::Text("SPACE PROGRAM  |  RenderCore Vulkan");
    ImGui::SameLine(0, 30); ImGui::TextDisabled("%s", device_name ? device_name : "?");
    ImGui::SameLine(0, 30); ImGui::Text("%.0f FPS", fps);
    ImGui::SameLine(0, 30); ImGui::Text("EPOQUE %s TDB", s.epoch_iso);
    ImGui::EndMainMenuBar();
  }
  ImGui::SetNextWindowPos(ImVec2(10, 40), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(300, 0), ImGuiCond_FirstUseEver);
  if (ImGui::Begin("CAMERA")) {
    ImGui::Text("Mode : %s", camera_mode_name(cam.mode));
    float fov = cam.fov_deg;
    if (ImGui::SliderFloat("FOV", &fov, 20.0f, 90.0f, "%.0f deg")) cam.fov_deg = fov;
    row("Distance", "%.3e m", cam.distance);
    ImGui::TextDisabled("Glisser: orbiter  |  Molette: zoom");
  }
  ImGui::End();
  if (show_telemetry && s.vehicle.valid && s.telemetry.valid) {
    ImGui::SetNextWindowPos(ImVec2(10, 250), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("TELEMETRIE")) {
      const Telemetry& t = s.telemetry;
      row("Altitude", "%.3f km", t.altitude / 1000.0);
      row("Vitesse", "%.4f km/s", t.speed / 1000.0);
      row("Demi-grand axe", "%.3f km", t.sma / 1000.0);
      row("Excentricite", "%.6f", t.ecc);
      row("Inclinaison", "%.4f deg", t.inc_deg);
    }
    ImGui::End();
  }
  if (show_bodies) {
    ImGui::SetNextWindowPos(ImVec2(320, 40), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("CORPS")) {
      ImGui::Text("Centre : %s", s.central_name);
      for (int i = 0; i < s.body_count; ++i)
        ImGui::Text("%-10s d=%.3e m", s.bodies[i].name, std::sqrt(
            s.bodies[i].position.x * s.bodies[i].position.x +
            s.bodies[i].position.y * s.bodies[i].position.y +
            s.bodies[i].position.z * s.bodies[i].position.z));
    }
    ImGui::End();
  }
}

// --- HUD INTERIEUR ISS (premiere personne) -----------------------------------
// Presentation pure : etiquettes des postes projetees, invite de proximite,
// bouton de sortie et INTERFACE HOLOGRAPHIQUE du poste actif (DA futuriste/epuree,
// verre bleu tres clair facon Star Citizen, avec animation d'ouverture sobre).
// Aucun calcul physique. L'etat gameplay (near_zone/active_panel/exit_request) est
// partage avec l'app, comme MapView::focus_request.

float smooth01(float x) { x = x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); return x * x * (3.0f - 2.0f * x); }

// Contenu thematique d'un poste (etape du cycle de mission). Display-only pour
// l'instant : ces interfaces seront reliees aux vrais systemes (fenetre_jeu). On
// garde des lignes (cle/valeur) et des jauges (fraction) simples et lisibles.
struct PanelRow { const char* k; const char* v; };
struct PanelBar { const char* k; float f; };
struct PanelContent {
  PanelRow rows[4]; int nrow{0};
  PanelBar bars[3]; int nbar{0};
  const char* note{""};
};
PanelContent panel_content(const char* id) {
  PanelContent c{};
  auto R = [&](const char* k, const char* v) { if (c.nrow < 4) c.rows[c.nrow++] = {k, v}; };
  auto B = [&](const char* k, float f)       { if (c.nbar < 3) c.bars[c.nbar++] = {k, f}; };
  if (std::strcmp(id, "agence") == 0) {
    R("PROGRAMME", "ARTEMIS-LIKE"); R("MISSIONS ACTIVES", "3");
    R("BUDGET", "4 280 M$");        R("EXERCICE", "T+128 j");
    B("Budget engage", 0.62f);      B("Objectifs annuels", 0.45f);
    c.note = "Direction du programme : arbitrages, financement, calendrier.";
  } else if (std::strcmp(id, "conception") == 0) {
    R("VEHICULE", "SPX-9 HEAVY");   R("MASSE A VIDE", "18.4 t");
    R("ISP (VIDE)", "452 s");       R("ETAGES", "2 + CU");
    B("Maturite conception", 0.71f);B("Marge de masse", 0.28f);
    c.note = "Bureau d'etudes : definition et dimensionnement du lanceur.";
  } else if (std::strcmp(id, "planification") == 0) {
    R("TRANSFERT", "TERRE -> LUNE");R("DELTA-V TOTAL", "3.94 km/s");
    R("DEPART", "T+12 j");          R("ARRIVEE", "T+16 j");
    B("Optimisation porkchop", 0.78f); B("Marge propulsive", 0.34f);
    c.note = "Trajectoires, fenetres de lancement, optimisation (porkchop).";
  } else if (std::strcmp(id, "controle") == 0) {
    R("PHASE", "ASCENSION");        R("ALTITUDE", "182 km");
    R("VITESSE", "7.81 km/s");      R("GO / NO-GO", "GO");
    B("Sequence de vol", 0.52f);    B("Reserve ergols", 0.66f);
    c.note = "Controle de vol : conduite de la mission en temps reel.";
  } else if (std::strcmp(id, "operations") == 0) {
    R("SYSTEMES", "NOMINAUX");      R("LIAISON", "KU 42 Mb/s");
    R("EQUIPAGE", "4");             R("ORBITE", "LEO 418 km");
    B("Energie (bus)", 0.88f);      B("Charge thermique", 0.41f); B("Consommables", 0.73f);
    c.note = "Operations : suivi des systemes bord et de la telemetrie.";
  } else if (std::strcmp(id, "analyse") == 0) {
    R("MISSIONS BOUCLEES", "7");    R("REUSSITE", "86 %");
    R("ANOMALIES", "2");            R("DERNIER VOL", "SUCCES");
    B("Couverture donnees", 0.94f); B("Actions correctives", 0.60f);
    c.note = "Post-mortem : debrief, archives, retour d'experience.";
  } else if (std::strcmp(id, "observation") == 0) {
    R("VISEE", "TERRE (NADIR)");    R("ALTITUDE", "418 km");
    R("PERIODE", "92.7 min");       R("PROCHAIN LEVER", "T+41 min");
    B("Eclairement orbital", 0.70f);
    c.note = "Coupole : observation de la Terre et de l'orbite (7 hublots).";
  } else {
    c.note = "Poste relie aux systemes de gestion (a venir).";
  }
  return c;
}

// Dessine le panneau holographique du poste (DA + animation). `open` in [0,1] :
// 0 = ferme, 1 = pleinement ouvert. Rend l'ouverture/fermeture (balayage + verre
// qui se deploie + contenu en fondu). Peut poser *active_panel = -1 (croix).
void station_panel(ImDrawList* fg, ImFont* font, ImGuiIO& io, float W, float H,
                   const StationZone& z, const ZonePanel* live, float open, StationView* station) {
  int* active_panel = &station->active_panel;
  const Vec3 ac = z.accent;
  auto accA = [&](float a) {
    return IM_COL32(int(ac.x * 255), int(ac.y * 255), int(ac.z * 255),
                    int((a < 0 ? 0 : (a > 1 ? 1 : a)) * 255));
  };
  const float e  = smooth01(open);
  const float ew = smooth01(open / 0.5f);            // balayage horizontal
  const float eh = smooth01((open - 0.32f) / 0.68f); // deploiement vertical
  const float ec = smooth01((open - 0.60f) / 0.40f); // fondu du contenu

  // VUES RICHES (catalogue de missions / arbre de competences) prioritaires.
  const bool is_list = live && live->filled && live->list && live->list->count > 0;
  const bool is_tree = live && live->filled && live->tree && live->tree->count > 0;

  // SOURCE DU CONTENU KV : VIVANT (modele de jeu) si fourni et `filled`, sinon
  // contenu de DEMONSTRATION (panel_content). Ignore pour les vues liste/arbre.
  spr::PanelKV  rows[6]{}; int nrow = 0;   // spr:: : evite les PanelRow/PanelBar locaux (mockup)
  spr::PanelBar bars[3]{}; int nbar = 0;
  char note[160] = {0};
  char status[24]; std::snprintf(status, sizeof status, "EN LIGNE");
  const PanelStep* steps = nullptr; int nstep = 0;   // deroulement de mission (CONTROLE)
  char button[24] = {0};                             // bouton d'action optionnel
  if (live && live->filled) {
    if (live->status[0]) std::snprintf(status, sizeof status, "%s", live->status);
    if (is_list)      std::snprintf(note, sizeof note, "%s", live->list->detail_note);
    else if (is_tree) std::snprintf(note, sizeof note, "%s", live->tree->legend);
    else {
      nrow = std::min(live->kv_count, 6);
      for (int i = 0; i < nrow; ++i) rows[i] = live->kv[i];
      nbar = std::min(live->bar_count, 3);
      for (int i = 0; i < nbar; ++i) bars[i] = live->bars[i];
      std::snprintf(note, sizeof note, "%s", live->note);
      if (live->steps && live->step_count > 0) { steps = live->steps; nstep = std::min(live->step_count, 10); }
      if (live->button[0]) std::snprintf(button, sizeof button, "%s", live->button);
    }
  } else {
    const PanelContent c = panel_content(z.id);
    nrow = c.nrow;
    for (int i = 0; i < nrow; ++i) {
      std::snprintf(rows[i].key, sizeof rows[i].key, "%s", c.rows[i].k);
      std::snprintf(rows[i].val, sizeof rows[i].val, "%s", c.rows[i].v);
    }
    nbar = c.nbar;
    for (int i = 0; i < nbar; ++i) {
      std::snprintf(bars[i].key, sizeof bars[i].key, "%s", c.bars[i].k);
      bars[i].frac = c.bars[i].f;
    }
    std::snprintf(note, sizeof note, "%s", c.note);
  }

  // dimensions : les vues riches sont plus grandes (liste + fiche / grande toile).
  float pw, need;
  if (is_tree)      { pw = std::min(940.0f, W - 40.0f); need = std::min(560.0f, H - 120.0f); }
  else if (is_list) { pw = std::min(720.0f, W - 60.0f); need = 520.0f; }
  else              { pw = std::min(600.0f, W - 80.0f); need = 82.0f + nrow * 22.0f + 8.0f + nbar * 34.0f
                            + nstep * 20.0f + (button[0] ? 42.0f : 0.0f) + 46.0f; }
  const float ph = std::min(std::max(need, 220.0f), H - 150.0f);
  const float cx = W * 0.5f, cy = H * 0.5f;
  const float hw = pw * 0.5f * ew;
  const float hh = std::max(1.4f, ph * 0.5f * eh);
  const ImVec2 a0(cx - hw, cy - hh), a1(cx + hw, cy + hh);
  const float pad = 20.0f;

  // voile de mise au point (assombrit la scene derriere le panneau)
  fg->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(3, 6, 11, int(150 * e)));

  // corps "verre" : degrade vertical bleu tres sombre, translucide
  fg->AddRectFilledMultiColor(a0, a1,
      IM_COL32(16, 29, 46, int(214 * e)), IM_COL32(16, 29, 46, int(214 * e)),
      IM_COL32(8, 15, 26, int(226 * e)),  IM_COL32(8, 15, 26, int(226 * e)));
  // voile d'accent en haut (fondu vers le bas)
  fg->AddRectFilledMultiColor(a0, ImVec2(a1.x, a0.y + (a1.y - a0.y) * 0.5f),
      accA(0.10f * e), accA(0.10f * e), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0));
  // barre d'accent (haut) + cadre fin
  fg->AddRectFilled(a0, ImVec2(a1.x, a0.y + 3.0f), accA(0.95f * e));
  fg->AddRect(a0, a1, accA(0.55f * e), 0.0f, 0, 1.4f);

  // equerres d'angle (facon SC)
  if (eh > 0.15f) {
    const float L = 22.0f, T = 2.2f; const ImU32 c = accA(0.95f * e);
    fg->AddLine(a0, ImVec2(a0.x + L, a0.y), c, T); fg->AddLine(a0, ImVec2(a0.x, a0.y + L), c, T);
    fg->AddLine(ImVec2(a1.x, a0.y), ImVec2(a1.x - L, a0.y), c, T); fg->AddLine(ImVec2(a1.x, a0.y), ImVec2(a1.x, a0.y + L), c, T);
    fg->AddLine(ImVec2(a0.x, a1.y), ImVec2(a0.x + L, a1.y), c, T); fg->AddLine(ImVec2(a0.x, a1.y), ImVec2(a0.x, a1.y - L), c, T);
    fg->AddLine(a1, ImVec2(a1.x - L, a1.y), c, T); fg->AddLine(a1, ImVec2(a1.x, a1.y - L), c, T);
  }

  // balayage (scanline) pendant l'ouverture : bandeau lumineux qui descend
  if (open < 0.999f && eh > 0.10f) {
    const float sy = a0.y + (a1.y - a0.y) * smooth01(open / 0.9f);
    fg->AddRectFilledMultiColor(ImVec2(a0.x, sy - 12.0f), ImVec2(a1.x, sy + 2.0f),
        IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0), accA(0.16f), accA(0.16f));
    fg->AddLine(ImVec2(a0.x, sy), ImVec2(a1.x, sy), accA(0.5f), 1.0f);
  }

  if (ec <= 0.01f) return;   // contenu pas encore visible

  const ImU32 kKey = IM_COL32(150, 160, 176, int(230 * ec));
  const ImU32 kVal = IM_COL32(232, 238, 246, int(255 * ec));
  const float x = a0.x + pad;
  float y = a0.y + 16.0f;

  // en-tete : libelle du poste + module reel + etat
  spaced_caps(fg, font, 22.0f, ImVec2(x, y), IM_COL32(236, 241, 248, int(255 * ec)), z.label, 2.0f);
  y += 30.0f;
  fg->AddText(font, 13.0f, ImVec2(x, y), accA(0.90f * ec), z.sub);
  {
    char st[28]; std::snprintf(st, sizeof st, "* %s", status);
    const ImVec2 ts = font->CalcTextSizeA(12.0f, FLT_MAX, 0.0f, st);
    fg->AddText(font, 12.0f, ImVec2(a1.x - pad - ts.x, y + 1.0f), accA(0.85f * ec), st);
  }
  y += 22.0f;
  fg->AddLine(ImVec2(x, y), ImVec2(a1.x - pad, y), accA(0.30f * ec), 1.0f);
  y += 14.0f;

  const float fy = a1.y - 30.0f;
  const float bodyx0 = x, bodyx1 = a1.x - pad, bodyy0 = y;

  if (is_list) {
    // ===== CATALOGUE DE MISSIONS : liste defilante (gauche) + fiche (droite) ===
    const PanelList& L = *live->list;
    const float listw = (bodyx1 - bodyx0) * 0.50f;
    const float lx0 = bodyx0, lx1 = bodyx0 + listw - 10.0f;
    const float dx0 = bodyx0 + listw + 14.0f;
    const float ly0 = bodyy0, ly1 = fy - 6.0f;
    const float rh = 24.0f;
    const int rows_fit = std::max(1, static_cast<int>((ly1 - ly0) / rh));
    static float s_scroll = 0.0f;
    const bool over_list = io.MousePos.x >= lx0 && io.MousePos.x <= lx1 + 6 &&
                           io.MousePos.y >= ly0 && io.MousePos.y <= ly1;
    if (over_list && io.MouseWheel != 0.0f) s_scroll -= io.MouseWheel * 3.0f;
    const int max_top = std::max(0, L.count - rows_fit);
    if (L.selected < static_cast<int>(s_scroll)) s_scroll = static_cast<float>(L.selected);
    if (L.selected >= static_cast<int>(s_scroll) + rows_fit) s_scroll = static_cast<float>(L.selected - rows_fit + 1);
    s_scroll = std::clamp(s_scroll, 0.0f, static_cast<float>(max_top));
    const int top = static_cast<int>(s_scroll);
    if (L.count > rows_fit) {   // ascenseur
      const float th = ly1 - ly0, kh = th * rows_fit / L.count, ky = ly0 + th * top / L.count;
      fg->AddRectFilled(ImVec2(lx1 + 3, ly0), ImVec2(lx1 + 6, ly1), IM_COL32(255, 255, 255, int(18 * ec)), 2.0f);
      fg->AddRectFilled(ImVec2(lx1 + 3, ky), ImVec2(lx1 + 6, ky + kh), accA(0.55f * ec), 2.0f);
    }
    for (int r = 0; r < rows_fit; ++r) {
      const int i = top + r;
      if (i >= L.count) break;
      const PanelListItem& it = L.items[i];
      const float ry = ly0 + r * rh;
      const ImVec2 r0(lx0 - 2, ry), r1(lx1, ry + rh - 3.0f);
      const bool hot = io.MousePos.x >= r0.x && io.MousePos.x <= r1.x &&
                       io.MousePos.y >= r0.y && io.MousePos.y <= r1.y;
      if (i == L.selected) fg->AddRectFilled(r0, r1, accA(0.18f * ec), 3.0f);
      else if (hot)        fg->AddRectFilled(r0, r1, IM_COL32(255, 255, 255, int(14 * ec)), 3.0f);
      const ImU32 tc = it.locked ? IM_COL32(122, 130, 142, int(210 * ec))
                     : it.done   ? IM_COL32(168, 222, 186, int(235 * ec)) : kVal;
      float tx = lx0 + 4.0f;
      if (it.locked) {   // petit cadenas dessine en primitives (police sans glyphes)
        const float lxp = lx0 + 2.0f, lyp = ry + 8.0f;
        const ImU32 lk = IM_COL32(196, 150, 90, int(220 * ec));
        fg->AddRect(ImVec2(lxp + 1.5f, lyp - 3.5f), ImVec2(lxp + 6.5f, lyp + 1.0f), lk, 1.0f, 0, 1.2f);
        fg->AddRectFilled(ImVec2(lxp, lyp), ImVec2(lxp + 8.0f, lyp + 6.0f), lk, 1.0f);
        tx = lx0 + 16.0f;
      } else if (it.done) {   // coche verte (mission accomplie)
        const float lxp = lx0 + 2.0f, lyp = ry + 9.0f;
        const ImU32 ck = IM_COL32(96, 214, 136, int(235 * ec));
        fg->AddLine(ImVec2(lxp, lyp), ImVec2(lxp + 3.0f, lyp + 3.0f), ck, 1.8f);
        fg->AddLine(ImVec2(lxp + 3.0f, lyp + 3.0f), ImVec2(lxp + 9.0f, lyp - 4.0f), ck, 1.8f);
        tx = lx0 + 16.0f;
      }
      fg->AddText(font, 13.0f, ImVec2(tx, ry + 4.0f), tc, it.title);
      const ImVec2 ts = font->CalcTextSizeA(11.0f, FLT_MAX, 0.0f, it.tag);
      fg->AddText(font, 11.0f, ImVec2(lx1 - ts.x - 2.0f, ry + 5.0f),
                  it.locked ? IM_COL32(160, 130, 96, int(200 * ec)) : accA(0.9f * ec), it.tag);
      if (hot && io.MouseClicked[0]) station->ui_list_click = i;
    }
    fg->AddLine(ImVec2(dx0 - 9, ly0), ImVec2(dx0 - 9, fy - 6.0f), accA(0.20f * ec), 1.0f);
    // fiche de l'element selectionne
    float dy = ly0 + 2.0f;
    spaced_caps(fg, font, 15.0f, ImVec2(dx0, dy), IM_COL32(236, 241, 248, int(255 * ec)), L.detail_title, 1.0f);
    dy += 26.0f;
    for (int i = 0; i < L.detail_count; ++i) {
      fg->AddText(font, 11.0f, ImVec2(dx0, dy), kKey, L.detail[i].key);
      dy += 14.0f;
      fg->AddText(font, 13.0f, ImVec2(dx0 + 6.0f, dy), kVal, L.detail[i].val);
      dy += 19.0f;
    }
    // technologies requises (l'arbre de competences GATE la mission) : puces
    // vertes (acquise) / rouges (a rechercher).
    if (L.req_count > 0) {
      const float dx1r = bodyx1;
      dy += 4.0f;
      fg->AddText(font, 11.0f, ImVec2(dx0, dy), kKey, "TECHNOLOGIES REQUISES"); dy += 17.0f;
      float chx = dx0, chy = dy;
      for (int i = 0; i < L.req_count; ++i) {
        const PanelReq& rq = L.reqs[i];
        const ImVec2 ts = font->CalcTextSizeA(11.0f, FLT_MAX, 0.0f, rq.name);
        const float cw = ts.x + 14.0f;
        if (chx + cw > dx1r && chx > dx0) { chx = dx0; chy += 22.0f; }
        const ImU32 fillc = rq.met ? IM_COL32(26, 58, 38, int(200 * ec)) : IM_COL32(58, 32, 30, int(200 * ec));
        const ImU32 bord  = rq.met ? IM_COL32(90, 210, 130, int(230 * ec)) : IM_COL32(224, 126, 108, int(230 * ec));
        const ImU32 tcol  = rq.met ? IM_COL32(150, 232, 182, int(255 * ec)) : IM_COL32(232, 168, 156, int(255 * ec));
        fg->AddRectFilled(ImVec2(chx, chy), ImVec2(chx + cw, chy + 18.0f), fillc, 3.0f);
        fg->AddRect(ImVec2(chx, chy), ImVec2(chx + cw, chy + 18.0f), bord, 3.0f, 0, 1.0f);
        fg->AddText(font, 11.0f, ImVec2(chx + 7.0f, chy + 3.0f), tcol, rq.name);
        chx += cw + 6.0f;
      }
      dy = chy + 24.0f;   // bloc d'action place SOUS les puces (evite tout chevauchement)
    }
    // ACTION : lancer la mission (débloquée, non accomplie) ou badge ACCOMPLIE.
    {
      const float by = dy + 62.0f;   // sous les lignes de plan (elles-memes sous les puces)
      if (L.sel_done) {
        const char* t = "MISSION ACCOMPLIE";
        const ImVec2 ts = font->CalcTextSizeA(13.0f, FLT_MAX, 0.0f, t);
        const ImVec2 b0(dx0, by), b1(dx0 + ts.x + 34.0f, by + 24.0f);
        fg->AddRectFilled(b0, b1, IM_COL32(24, 56, 36, int(200 * ec)), 4.0f);
        fg->AddRect(b0, b1, IM_COL32(96, 214, 136, int(220 * ec)), 4.0f, 0, 1.2f);
        const float cyc = (b0.y + b1.y) * 0.5f;
        fg->AddLine(ImVec2(dx0 + 10.0f, cyc + 1.0f), ImVec2(dx0 + 14.0f, cyc + 5.0f), IM_COL32(120, 226, 160, int(255 * ec)), 2.0f);
        fg->AddLine(ImVec2(dx0 + 14.0f, cyc + 5.0f), ImVec2(dx0 + 22.0f, cyc - 5.0f), IM_COL32(120, 226, 160, int(255 * ec)), 2.0f);
        fg->AddText(font, 13.0f, ImVec2(dx0 + 28.0f, by + 5.0f), IM_COL32(160, 232, 188, int(255 * ec)), t);
      } else if (L.can_launch) {
        // PLAN DE VOL REEL (deterministe) : Δv requis/dispo + C3/duree + FENETRE.
        const ImU32 pcol = L.plan_feasible ? IM_COL32(120, 220, 150, int(245 * ec))
                                           : IM_COL32(232, 162, 112, int(245 * ec));
        fg->AddText(font, 12.0f, ImVec2(dx0, by - 52.0f), pcol, L.plan_a);
        fg->AddText(font, 11.0f, ImVec2(dx0, by - 36.0f), IM_COL32(150, 160, 176, int(220 * ec)), L.plan_b);
        if (!L.plan_feasible) {
          const char* t = "DELTA-V INSUFFISANT";   // ameliorer propulsion / lanceur
          const ImVec2 ts = font->CalcTextSizeA(13.0f, FLT_MAX, 0.0f, t);
          fg->AddRectFilled(ImVec2(dx0, by), ImVec2(dx0 + ts.x + 24.0f, by + 24.0f), IM_COL32(48, 34, 30, int(190 * ec)), 4.0f);
          fg->AddRect(ImVec2(dx0, by), ImVec2(dx0 + ts.x + 24.0f, by + 24.0f), IM_COL32(214, 142, 112, int(200 * ec)), 4.0f, 0, 1.2f);
          fg->AddText(font, 13.0f, ImVec2(dx0 + 12.0f, by + 5.0f), IM_COL32(228, 170, 142, int(245 * ec)), t);
        } else if (L.window_open) {
          fg->AddText(font, 11.0f, ImVec2(dx0, by - 18.0f), IM_COL32(120, 220, 150, int(235 * ec)), L.window_txt);
          const char* t = "LANCER LA MISSION";
          const ImVec2 ts = font->CalcTextSizeA(14.0f, FLT_MAX, 0.0f, t);
          const ImVec2 b0(dx0, by), b1(dx0 + ts.x + 42.0f, by + 26.0f);
          const bool hot = io.MousePos.x >= b0.x && io.MousePos.x <= b1.x &&
                           io.MousePos.y >= b0.y && io.MousePos.y <= b1.y;
          fg->AddRectFilled(b0, b1, hot ? accA(0.34f * ec) : accA(0.18f * ec), 4.0f);
          fg->AddRect(b0, b1, accA(0.92f * ec), 4.0f, 0, 1.5f);
          const float cyc = (b0.y + b1.y) * 0.5f;
          fg->AddTriangleFilled(ImVec2(dx0 + 14.0f, cyc - 6.0f), ImVec2(dx0 + 14.0f, cyc + 6.0f),
                                ImVec2(dx0 + 23.0f, cyc), accA(0.95f * ec));
          fg->AddText(font, 14.0f, ImVec2(dx0 + 32.0f, by + 5.0f), IM_COL32(236, 242, 250, int(255 * ec)), t);
          if (hot && io.MouseClicked[0]) station->ui_mission_launch = L.selected;
        } else {   // fenetre fermee : avancer le calendrier jusqu'a l'alignement
          fg->AddText(font, 11.0f, ImVec2(dx0, by - 18.0f), IM_COL32(232, 182, 112, int(238 * ec)), L.window_txt);
          const char* t = "AVANCER A LA FENETRE";
          const ImVec2 ts = font->CalcTextSizeA(13.0f, FLT_MAX, 0.0f, t);
          const ImVec2 b0(dx0, by), b1(dx0 + ts.x + 30.0f, by + 26.0f);
          const bool hot = io.MousePos.x >= b0.x && io.MousePos.x <= b1.x &&
                           io.MousePos.y >= b0.y && io.MousePos.y <= b1.y;
          fg->AddRectFilled(b0, b1, hot ? IM_COL32(72, 56, 30, int(215 * ec)) : IM_COL32(48, 38, 24, int(190 * ec)), 4.0f);
          fg->AddRect(b0, b1, IM_COL32(232, 182, 112, int(215 * ec)), 4.0f, 0, 1.4f);
          fg->AddText(font, 13.0f, ImVec2(dx0 + 12.0f, by + 6.0f), IM_COL32(242, 210, 152, int(250 * ec)), t);
          if (hot && io.MouseClicked[0]) station->ui_mission_wait = L.selected;
        }
      }
    }
  } else if (is_tree) {
    // ===== ARBRE DE COMPETENCES : grande TOILE PANNABLE (facon ARK) ============
    // Molette = defilement vertical ; GLISSER = pan libre ; CLIC net = recherche.
    // Rendu clippe au cadre ; libelles de categorie COLLES a gauche.
    const PanelTree& T = *live->tree;
    const float ax0 = bodyx0, ax1 = bodyx1, ay0 = bodyy0 + 2.0f, ay1 = fy - 8.0f;
    const float vw = ax1 - ax0, vh = ay1 - ay0, nhw = 76.0f, nhh = 15.0f;
    static const PanelTree* s_tree = nullptr;
    static float s_px = 0.0f, s_py = 0.0f, s_pressx = 0.0f, s_pressy = 0.0f;
    static bool  s_drag = false, s_moved = false;
    if (s_tree != &T) { s_tree = &T; s_px = 0.0f; s_py = 0.0f; s_drag = false; }
    const float maxpx = std::max(0.0f, T.canvas_w - vw), maxpy = std::max(0.0f, T.canvas_h - vh);
    const bool over = io.MousePos.x >= ax0 && io.MousePos.x <= ax1 &&
                      io.MousePos.y >= ay0 && io.MousePos.y <= ay1;
    if (over && io.MouseWheel != 0.0f) s_py -= io.MouseWheel * 30.0f;
    bool did_click = false;
    if (over && io.MouseDown[0]) {
      if (!s_drag) { s_drag = true; s_moved = false; s_pressx = io.MousePos.x; s_pressy = io.MousePos.y; }
      s_px -= io.MouseDelta.x; s_py -= io.MouseDelta.y;
      if (std::fabs(io.MousePos.x - s_pressx) + std::fabs(io.MousePos.y - s_pressy) > 5.0f) s_moved = true;
    } else if (s_drag && !io.MouseDown[0]) {
      if (!s_moved) did_click = true;   // clic net (sans glisser) = recherche
      s_drag = false;
    }
    s_px = std::clamp(s_px, 0.0f, maxpx);
    s_py = std::clamp(s_py, 0.0f, maxpy);
    auto scr = [&](float cx, float cy) { return ImVec2(ax0 + cx - s_px, ay0 + cy - s_py); };

    fg->PushClipRect(ImVec2(ax0, ay0), ImVec2(ax1, ay1), true);
    for (int i = 0; i < T.count; ++i) {   // liens (predecesseur + prerequis croise)
      const PanelTreeNode& n = T.nodes[i];
      const ImVec2 b = scr(n.x, n.y);
      auto link = [&](int j, bool cross) {
        if (j < 0 || j >= T.count) return;
        const PanelTreeNode& p = T.nodes[j];
        const ImVec2 a = scr(p.x, p.y);
        const int st = std::min(n.state, p.state);
        const ImU32 lc = (n.state == 2) ? accA(0.62f * ec)
                       : (st >= 1)       ? accA(0.28f * ec) : IM_COL32(82, 90, 104, int(105 * ec));
        if (cross) { fg->AddLine(a, ImVec2(a.x, b.y), lc, 1.0f); fg->AddLine(ImVec2(a.x, b.y), ImVec2(b.x - nhw, b.y), lc, 1.0f); }
        else       { fg->AddLine(ImVec2(a.x + nhw, a.y), ImVec2(b.x - nhw, b.y), lc, (n.state == 2) ? 2.0f : 1.2f); }
      };
      link(n.prereq, false);
      link(n.xreq, true);
    }
    for (int i = 0; i < T.count; ++i) {   // noeuds
      const PanelTreeNode& n = T.nodes[i];
      const ImVec2 c = scr(n.x, n.y);
      if (c.x + nhw < ax0 || c.x - nhw > ax1 || c.y + nhh < ay0 || c.y - nhh > ay1) continue;
      const ImVec2 q0(c.x - nhw, c.y - nhh), q1(c.x + nhw, c.y + nhh);
      const Vec3 na = n.accent;
      auto naA = [&](float a) { return IM_COL32(int(na.x * 255), int(na.y * 255), int(na.z * 255),
                                                int(std::clamp(a, 0.0f, 1.0f) * 255)); };
      const bool hot = io.MousePos.x >= q0.x && io.MousePos.x <= q1.x &&
                       io.MousePos.y >= q0.y && io.MousePos.y <= q1.y && over;
      ImU32 fill, brd, txt;
      if (n.state == 2)      { fill = naA(0.34f * ec); brd = naA(0.95f * ec); txt = IM_COL32(242, 246, 251, int(255 * ec)); }
      else if (n.state == 1) { fill = hot ? naA(0.26f * ec) : IM_COL32(19, 31, 47, int(210 * ec)); brd = naA(0.9f * ec); txt = naA(0.97f * ec); }
      else                   { fill = IM_COL32(14, 20, 30, int(150 * ec)); brd = IM_COL32(74, 82, 96, int(150 * ec)); txt = IM_COL32(112, 120, 132, int(190 * ec)); }
      fg->AddRectFilled(q0, q1, fill, 4.0f);
      fg->AddRect(q0, q1, brd, 4.0f, 0, (n.state == 2) ? 1.8f : 1.2f);
      const ImVec2 lts = font->CalcTextSizeA(10.5f, FLT_MAX, 0.0f, n.label);
      fg->AddText(font, 10.5f, ImVec2(c.x - lts.x * 0.5f, c.y - lts.y * 0.5f - 3.0f), txt, n.label);
      char cs[12];
      if (n.state == 2) std::snprintf(cs, sizeof cs, "acquis");
      else              std::snprintf(cs, sizeof cs, "%d PsR", n.cost);
      const ImU32 cc = (n.state == 2)               ? naA(0.85f * ec)
                     : (n.state == 1 && n.afford)   ? accA(0.85f * ec)
                     : (n.state == 1)               ? IM_COL32(224, 132, 110, int(220 * ec))
                                                    : IM_COL32(108, 116, 128, int(180 * ec));
      const ImVec2 cts = font->CalcTextSizeA(9.0f, FLT_MAX, 0.0f, cs);
      fg->AddText(font, 9.0f, ImVec2(c.x - cts.x * 0.5f, c.y + 2.0f), cc, cs);
      if (hot && did_click && n.state == 1 && n.afford) station->ui_tree_click = i;
    }
    fg->PopClipRect();
    for (int i = 0; i < T.lane_count; ++i) {   // libelles de categorie colles a gauche
      const PanelTreeLane& L = T.lanes[i];
      const float ly = ay0 + L.y - s_py;
      if (ly < ay0 - 6.0f || ly > ay1 + 6.0f) continue;
      const Vec3 la = L.accent;
      fg->AddRectFilled(ImVec2(ax0 - 2.0f, ly - 8.0f), ImVec2(ax0 + 3.0f, ly + 8.0f),
                        IM_COL32(int(la.x * 255), int(la.y * 255), int(la.z * 255), int(220 * ec)), 1.0f);
      spaced_caps(fg, font, 10.0f, ImVec2(ax0 + 8.0f, ly - 6.0f), IM_COL32(202, 210, 222, int(230 * ec)), L.name, 0.5f);
    }
  } else {
    // ===== KV : lignes (cle/valeur) puis jauges (accent) =======================
    for (int i = 0; i < nrow; ++i) {
      fg->AddText(font, 14.0f, ImVec2(x, y), kKey, rows[i].key);
      const ImVec2 ts = font->CalcTextSizeA(14.0f, FLT_MAX, 0.0f, rows[i].val);
      fg->AddText(font, 14.0f, ImVec2(a1.x - pad - ts.x, y), kVal, rows[i].val);
      y += 22.0f;
    }
    y += 8.0f;
    for (int i = 0; i < nbar; ++i) {
      const float frac = std::clamp(bars[i].frac, 0.0f, 1.0f);
      fg->AddText(font, 13.0f, ImVec2(x, y), kKey, bars[i].key);
      char pc[8]; std::snprintf(pc, sizeof pc, "%d%%", int(frac * 100.0f + 0.5f));
      const ImVec2 ts = font->CalcTextSizeA(13.0f, FLT_MAX, 0.0f, pc);
      fg->AddText(font, 13.0f, ImVec2(a1.x - pad - ts.x, y), accA(0.90f * ec), pc);
      y += 18.0f;
      const float bx0 = x, bx1 = a1.x - pad;
      fg->AddRectFilled(ImVec2(bx0, y), ImVec2(bx1, y + 5.0f), IM_COL32(255, 255, 255, int(26 * ec)), 2.0f);
      fg->AddRectFilled(ImVec2(bx0, y), ImVec2(bx0 + (bx1 - bx0) * frac, y + 5.0f), accA(0.90f * ec), 2.0f);
      y += 16.0f;
    }
    // ETAPES de la mission en cours (checklist : puce / fleche / coche) ---------
    for (int i = 0; i < nstep; ++i) {
      const PanelStep& s = steps[i];
      const float iy = y + 8.0f;
      const ImU32 col = (s.state == 2) ? IM_COL32(96, 214, 136, int(235 * ec))   // franchie
                      : (s.state == 1) ? accA(0.98f * ec)                        // en cours
                                       : IM_COL32(120, 128, 140, int(200 * ec)); // a venir
      if (s.state == 2) {          // coche verte
        fg->AddLine(ImVec2(x, iy), ImVec2(x + 3.0f, iy + 3.0f), col, 1.8f);
        fg->AddLine(ImVec2(x + 3.0f, iy + 3.0f), ImVec2(x + 9.0f, iy - 4.0f), col, 1.8f);
      } else if (s.state == 1) {   // fleche pleine (etape courante)
        fg->AddTriangleFilled(ImVec2(x, iy - 4.5f), ImVec2(x, iy + 4.5f), ImVec2(x + 8.0f, iy), col);
      } else {                     // puce
        fg->AddCircle(ImVec2(x + 4.0f, iy), 2.6f, col, 12, 1.4f);
      }
      fg->AddText(font, 14.0f, ImVec2(x + 16.0f, y), col, s.label);
      y += 20.0f;
    }
    // BOUTON d'action (ex. "ETAPE SUIVANTE") -> pose station->ui_panel_button ----
    if (button[0]) {
      y += 6.0f;
      const ImVec2 b0(x, y), b1(x + 200.0f, y + 30.0f);
      const bool hot = io.MousePos.x >= b0.x && io.MousePos.x <= b1.x &&
                       io.MousePos.y >= b0.y && io.MousePos.y <= b1.y;
      fg->AddRectFilled(b0, b1, hot ? accA(0.42f * ec) : accA(0.22f * ec), 4.0f);
      fg->AddRect(b0, b1, accA(0.85f * ec), 4.0f, 0, 1.3f);
      const ImVec2 ts = font->CalcTextSizeA(14.0f, FLT_MAX, 0.0f, button);
      fg->AddText(font, 14.0f, ImVec2((b0.x + b1.x) * 0.5f - ts.x * 0.5f, y + 8.0f),
                  IM_COL32(236, 241, 248, int(255 * ec)), button);
      if (hot && io.MouseClicked[0] && active_panel) station->ui_panel_button = *active_panel;
      y += 34.0f;
    }
  }

  // pied : note + rappel de fermeture
  fg->AddLine(ImVec2(x, fy), ImVec2(a1.x - pad, fy), accA(0.22f * ec), 1.0f);
  fg->AddText(font, 12.0f, ImVec2(x, fy + 8.0f), IM_COL32(150, 160, 176, int(220 * ec)), note);
  {
    const char* cl = "[ ECHAP ] FERMER";
    const ImVec2 ts = font->CalcTextSizeA(12.0f, FLT_MAX, 0.0f, cl);
    fg->AddText(font, 12.0f, ImVec2(a1.x - pad - ts.x, fy + 8.0f), accA(0.80f * ec), cl);
  }

  // croix de fermeture (coin haut-droit), cliquable quand le panneau est ouvert
  if (open > 0.60f && active_panel && *active_panel >= 0) {
    const ImVec2 cc(a1.x - 16.0f, a0.y + 18.0f);
    const bool hot = std::fabs(io.MousePos.x - cc.x) < 9.0f && std::fabs(io.MousePos.y - cc.y) < 9.0f;
    const ImU32 xc = hot ? IM_COL32(255, 255, 255, 255) : accA(0.85f);
    fg->AddLine(ImVec2(cc.x - 4, cc.y - 4), ImVec2(cc.x + 4, cc.y + 4), xc, 1.6f);
    fg->AddLine(ImVec2(cc.x - 4, cc.y + 4), ImVec2(cc.x + 4, cc.y - 4), xc, 1.6f);
    if (hot && io.MouseClicked[0]) *active_panel = -1;
  }
}

// CONSOLE DE CALCUL (mode PRO) : le joueur TAPE la formule de l'etape courante ;
// l'app l'evalue (calc::eval) et renvoie le verdict. Fenetre ImGui (le champ de
// saisie exige un vrai widget) ; le voile assombrit la scene derriere.
void calc_console_hud(StationView* station) {
  CalcConsole& c = station->calc;
  ImGuiIO& io = ImGui::GetIO();
  const float W = io.DisplaySize.x, H = io.DisplaySize.y;
  ImDrawList* fg = ImGui::GetForegroundDrawList();
  const ImU32 kWhite = IM_COL32(236, 240, 248, 255);
  const ImU32 kGrey  = IM_COL32(150, 160, 176, 230);
  const ImU32 kCyan  = IM_COL32(120, 210, 255, 255);
  fg->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(3, 6, 11, 180));

  ImGui::SetNextWindowPos(ImVec2(W * 0.5f, H * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(580.0f, 452.0f), ImGuiCond_Always);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(12, 20, 30, 236));
  ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(60, 110, 150, 190));
  ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(6, 12, 20, 235));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f, 20.0f));
  ImGui::Begin("##calc", nullptr,
               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
               ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings);

  ImGui::PushStyleColor(ImGuiCol_Text, kCyan);
  ImGui::TextUnformatted("POSTE DE CALCUL  -  MODE PRO"); ImGui::PopStyleColor();
  ImGui::PushStyleColor(ImGuiCol_Text, kWhite); ImGui::TextUnformatted(c.title); ImGui::PopStyleColor();
  ImGui::PushStyleColor(ImGuiCol_Text, kGrey);
  ImGui::TextWrapped("Trouver : %s   [ %s en %s ]", c.find, c.sym, c.unit); ImGui::PopStyleColor();
  ImGui::Separator();

  ImGui::PushStyleColor(ImGuiCol_Text, kGrey); ImGui::TextUnformatted("DONNEES"); ImGui::PopStyleColor();
  for (int i = 0; i < c.given_count; ++i) ImGui::BulletText("%s", c.givens[i]);
  if (c.hint[0]) {
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(120, 150, 182, 220));
    ImGui::TextWrapped("Indice : %s", c.hint); ImGui::PopStyleColor();
  }
  ImGui::Separator();

  ImGui::PushStyleColor(ImGuiCol_Text, kGrey); ImGui::Text("%s  =", c.sym); ImGui::PopStyleColor();
  ImGui::SetNextItemWidth(-FLT_MIN);
  bool verify = ImGui::InputText("##formule", c.input, sizeof(c.input), ImGuiInputTextFlags_EnterReturnsTrue);
  ImGui::Spacing();
  ImGui::PushStyleColor(ImGuiCol_Button,        IM_COL32(22, 42, 62, 235));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(38, 74, 106, 245));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive,  IM_COL32(58, 108, 148, 255));
  ImGui::PushStyleColor(ImGuiCol_Text, kWhite);
  if (ImGui::Button("VERIFIER", ImVec2(170, 32))) verify = true;
  ImGui::SameLine();
  if (ImGui::Button(c.solved ? "FERMER" : "ABANDONNER", ImVec2(170, 32))) c.close = true;
  ImGui::PopStyleColor(4);
  if (verify) c.verify = true;

  if (c.feedback[0]) {
    const ImU32 fc = c.feedback_kind == 1 ? IM_COL32(120, 220, 150, 255)
                   : c.feedback_kind == 2 ? IM_COL32(232, 140, 120, 255) : kGrey;
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, fc); ImGui::TextWrapped("%s", c.feedback); ImGui::PopStyleColor();
  }
  ImGui::Spacing();
  ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(108, 116, 128, 200));
  ImGui::TextWrapped("Syntaxe : + - * / ^  sqrt( ) pow( , )  pi ...   ex : sqrt(mu/r)");
  ImGui::PopStyleColor();

  ImGui::End();
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor(3);
}

void station_hud(const RenderSnapshot& /*s*/, Camera& cam, StationView* station) {
  if (station->calc.active) { calc_console_hud(station); return; }   // console de calcul (priorite)
  ImGuiIO& io = ImGui::GetIO();
  const float W = io.DisplaySize.x, H = io.DisplaySize.y;
  const float aspect = (H > 0.0f) ? W / H : 1.0f;
  ImDrawList* fg = ImGui::GetForegroundDrawList();
  ImFont* font = ImGui::GetFont();
  const ImU32 kWhite = IM_COL32(235, 238, 245, 255);
  const ImU32 kGrey  = IM_COL32(150, 156, 168, 220);

  // --- etat d'animation du panneau (presentation pure, cote HUD) -------------
  static int   s_panel = -1;      // poste dont le panneau est (de)anime
  static float s_open  = 0.0f;    // progression 0..1
  const float dt = std::min(io.DeltaTime, 0.05f);
  const int want = (station->active_panel >= 0 && station->active_panel < station->zone_count)
                       ? station->active_panel : -1;
  if (want >= 0) {
    if (s_panel != want) { s_panel = want; s_open = 0.0f; }   // (re)demarre l'ouverture
    s_open = std::min(1.0f, s_open + dt / 0.42f);
  } else if (s_panel >= 0) {
    s_open = std::max(0.0f, s_open - dt / 0.22f);             // fermeture
    if (s_open <= 0.0f) s_panel = -1;
  }
  const bool panel_active = (s_panel >= 0);

  // --- etiquettes des postes (projetees) : teinte d'accent du poste ----------
  if (station->show_labels && !panel_active) {
    for (int i = 0; i < station->zone_count; ++i) {
      const StationZone& z = station->zones[i];
      ImVec2 p;
      if (!project_to_screen(cam, z.center, aspect, W, H, p)) continue;
      if (p.x < -80 || p.x > W + 80 || p.y < -50 || p.y > H + 50) continue;
      const bool near = (station->near_zone == i);
      const ImU32 acc = IM_COL32(int(z.accent.x * 255), int(z.accent.y * 255), int(z.accent.z * 255),
                                 near ? 255 : 210);
      fg->AddCircle(p, near ? 8.0f : 5.0f, acc, 24, near ? 2.6f : 1.6f);
      spaced_caps(fg, font, near ? 16.0f : 14.0f, ImVec2(p.x + 12.0f, p.y - 8.0f),
                  near ? kWhite : kGrey, z.label, 1.5f);
    }
  }

  // --- invite de proximite "[E] OUVRIR — <label>" (bas-centre) ---------------
  if (station->near_zone >= 0 && station->near_zone < station->zone_count && !panel_active) {
    const StationZone& z = station->zones[station->near_zone];
    const Vec3 ac = z.accent;
    char msg[64];
    std::snprintf(msg, sizeof msg, "[ E ]  OUVRIR  -  %s", z.label);
    const ImVec2 ts = font->CalcTextSizeA(20.0f, FLT_MAX, 0.0f, msg);
    const ImVec2 c{W * 0.5f - ts.x * 0.5f, H - 96.0f};
    fg->AddRectFilled(ImVec2(c.x - 16, c.y - 9), ImVec2(c.x + ts.x + 16, c.y + ts.y + 9),
                      IM_COL32(12, 18, 26, 205), 5.0f);
    fg->AddRect(ImVec2(c.x - 16, c.y - 9), ImVec2(c.x + ts.x + 16, c.y + ts.y + 9),
                IM_COL32(int(ac.x * 255), int(ac.y * 255), int(ac.z * 255), 170), 5.0f, 0, 1.2f);
    fg->AddText(font, 20.0f, c, kWhite, msg);
  }

  // --- bandeau d'aide (haut-centre) ------------------------------------------
  if (!panel_active) {
    const char* hint = "ISS - QG   |   ZQSD/WASD : se deplacer   |   SOURIS : regarder   |   E : poste   |   F5 : sauvegarder   |   M : carte";
    const ImVec2 ts = font->CalcTextSizeA(14.0f, FLT_MAX, 0.0f, hint);
    fg->AddText(font, 14.0f, ImVec2(W * 0.5f - ts.x * 0.5f, 12.0f), kGrey, hint);
  }
  // Aide au placement (--noclamp) : position de l'oeil, copiable en --isseye.
  if (station->show_eye) {
    char pb[112];
    std::snprintf(pb, sizeof pb, "POSITION   --isseye %.1f %.1f %.1f",
                  station->eye_pos.x, station->eye_pos.y, station->eye_pos.z);
    fg->AddText(font, 16.0f, ImVec2(20.0f, 42.0f), IM_COL32(120, 210, 255, 255), pb);
  }

  // --- bouton "SORTIR DE L'ISS" (haut-gauche) --------------------------------
  {
    begin_overlay("##sortie_iss", ImVec2(24.0f, 16.0f), ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, kGrey);
    if (ImGui::Selectable("< CARTE  [M]", false, 0, ImVec2(170, 0)))
      station->exit_request = true;
    ImGui::PopStyleColor();
    ImGui::End();
  }

  // --- retour visuel de la SAUVEGARDE RAPIDE ([F5], pose par l'app) -----------
  if (station->save_flash > 0.0f) {
    station->save_flash = std::max(0.0f, station->save_flash - dt);
    const float a = std::min(1.0f, station->save_flash / 0.6f);   // fondu sortant
    const char* msg = "PARTIE SAUVEGARDEE";
    const ImVec2 ts = font->CalcTextSizeA(18.0f, FLT_MAX, 0.0f, msg);
    const ImVec2 c{W * 0.5f - ts.x * 0.5f, 54.0f};
    fg->AddRectFilled(ImVec2(c.x - 18, c.y - 8), ImVec2(c.x + ts.x + 18, c.y + ts.y + 8),
                      IM_COL32(14, 30, 22, int(200 * a)), 5.0f);
    fg->AddRect(ImVec2(c.x - 18, c.y - 8), ImVec2(c.x + ts.x + 18, c.y + ts.y + 8),
                IM_COL32(96, 214, 136, int(210 * a)), 5.0f, 0, 1.2f);
    fg->AddText(font, 18.0f, c, IM_COL32(150, 232, 182, int(255 * a)), msg);
  }

  // --- interface holographique du poste actif (DA + animation) ---------------
  if (panel_active) {
    const ZonePanel* live = (station->panels && s_panel < station->panel_count)
                                ? &station->panels[s_panel] : nullptr;
    station_panel(fg, font, io, W, H, station->zones[s_panel], live, s_open, station);
  }
}

// ============================ ECRAN TITRE + MENUS ==========================
// Dessine l'ecran courant (Title / Difficulty / Saves) et POSE les requetes dans
// `m` ; l'app les lit et fait avancer sa machine a etats. DA coherente avec les
// postes holographiques : voile sombre, titre en capitales espacees, accents cyan.
void menu_hud(MenuView& m) {
  ImGuiIO& io = ImGui::GetIO();
  const float W = io.DisplaySize.x, H = io.DisplaySize.y;
  ImDrawList* fg = ImGui::GetForegroundDrawList();
  ImFont* font = ImGui::GetFont();

  const ImU32 kWhite = IM_COL32(236, 240, 248, 255);
  const ImU32 kGrey  = IM_COL32(150, 160, 176, 230);
  const ImU32 kCyan  = IM_COL32(120, 210, 255, 255);

  // Voile : assombrit la scene 3D de fond pour la lisibilite.
  fg->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(4, 8, 14, 190));

  // Titre (haut-centre) : grand, capitales espacees + filet d'accent + sous-titre.
  {
    const char* t = "SPACE PROGRAM";
    const float sz = 52.0f, sp = 8.0f;
    float tw = 0.0f;
    for (const char* p = t; *p; ++p) {
      char c[2] = {static_cast<char>(std::toupper(static_cast<unsigned char>(*p))), 0};
      tw += font->CalcTextSizeA(sz, FLT_MAX, 0.0f, c).x + sp;
    }
    const float tx = W * 0.5f - tw * 0.5f, ty = H * 0.15f;
    spaced_caps(fg, font, sz, ImVec2(tx, ty), kWhite, t, sp);
    fg->AddLine(ImVec2(tx, ty + sz + 8.0f), ImVec2(tx + tw - sp, ty + sz + 8.0f), kCyan, 1.5f);
    const char* sub = "AGENCE SPATIALE  -  QG A BORD DE L'ISS";
    const ImVec2 ss = font->CalcTextSizeA(15.0f, FLT_MAX, 0.0f, sub);
    fg->AddText(font, 15.0f, ImVec2(W * 0.5f - ss.x * 0.5f, ty + sz + 16.0f), kGrey, sub);
  }

  // Panneau central (ImGui) : boutons cliquables, fond translucide + liseré.
  ImGui::SetNextWindowPos(ImVec2(W * 0.5f, H * 0.44f), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
  ImGui::SetNextWindowSize(ImVec2(520.0f, 366.0f), ImGuiCond_Always);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(10, 16, 24, 208));
  ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(60, 110, 150, 185));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(26.0f, 22.0f));
  ImGui::Begin("##menu", nullptr,
               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
               ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
               ImGuiWindowFlags_NoSavedSettings);

  auto big_button = [&](const char* label, float h) -> bool {
    ImGui::PushStyleColor(ImGuiCol_Button,        IM_COL32(22, 42, 62, 235));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(38, 74, 106, 245));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  IM_COL32(58, 108, 148, 255));
    ImGui::PushStyleColor(ImGuiCol_Text, kWhite);
    const bool c = ImGui::Button(label, ImVec2(-FLT_MIN, h));
    ImGui::PopStyleColor(4);
    return c;
  };

  if (m.screen == MenuScreen::Title) {
    if (big_button("NOUVELLE PARTIE", 46.0f)) m.go_new_game = true;
    ImGui::Dummy(ImVec2(0, 10));
    if (big_button("REPRENDRE", 46.0f))       m.go_saves = true;
    ImGui::Dummy(ImVec2(0, 10));
    if (big_button("QUITTER", 46.0f))         m.quit = true;

  } else if (m.screen == MenuScreen::Difficulty) {
    ImGui::PushStyleColor(ImGuiCol_Text, kGrey); ImGui::TextUnformatted("NOM DE L'AGENCE"); ImGui::PopStyleColor();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputText("##agency", m.agency_name, sizeof(m.agency_name));
    ImGui::Dummy(ImVec2(0, 12));
    ImGui::PushStyleColor(ImGuiCol_Text, kGrey); ImGui::TextUnformatted("DIFFICULTE"); ImGui::PopStyleColor();

    auto diff_btn = [&](const char* label, int idx) {
      const bool sel = (m.difficulty == idx);
      ImGui::PushStyleColor(ImGuiCol_Button, sel ? IM_COL32(40, 82, 116, 245) : IM_COL32(18, 30, 44, 220));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(48, 92, 128, 250));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive,  IM_COL32(58, 108, 148, 255));
      ImGui::PushStyleColor(ImGuiCol_Text, sel ? kWhite : kGrey);
      if (ImGui::Button(label, ImVec2(214, 34))) m.difficulty = idx;
      ImGui::PopStyleColor(4);
      if (sel) ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                                                   kCyan, 0.0f, 0, 1.5f);
    };
    diff_btn("NORMAL", 0); ImGui::SameLine(); diff_btn("PRO", 1);
    ImGui::Dummy(ImVec2(0, 4));
    ImGui::PushStyleColor(ImGuiCol_Text, kGrey);
    ImGui::TextWrapped("%s", m.difficulty == 0
        ? "NORMAL : acces a l'assistant, il te guide dans les calculs (dotation 45 M$)."
        : "PRO : aucune aide, tu realises tous les calculs toi-meme (dotation 32 M$).");
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, 12));
    if (big_button("LANCER LA PARTIE", 44.0f)) m.start_game = true;
    ImGui::Dummy(ImVec2(0, 4));
    ImGui::PushStyleColor(ImGuiCol_Text, kGrey);
    if (ImGui::Selectable("< RETOUR", false, 0, ImVec2(120, 0))) m.go_back = true;
    ImGui::PopStyleColor();

  } else {  // MenuScreen::Saves
    ImGui::PushStyleColor(ImGuiCol_Text, kGrey); ImGui::TextUnformatted("REPRENDRE UNE PARTIE"); ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, 6));
    ImGui::BeginChild("##saves", ImVec2(0, 186), true);
    if (m.save_count <= 0) {
      ImGui::PushStyleColor(ImGuiCol_Text, kGrey);
      ImGui::TextWrapped("Aucune sauvegarde trouvee.\nLance une nouvelle partie, puis sauvegarde avec [F5] a bord de l'ISS.");
      ImGui::PopStyleColor();
    } else {
      for (int i = 0; i < m.save_count; ++i) {
        const bool sel = (m.save_selected == i);
        if (ImGui::Selectable(m.saves[i].label, sel)) m.save_selected = i;
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) m.load_index = i;
      }
    }
    ImGui::EndChild();
    ImGui::Dummy(ImVec2(0, 8));
    const bool can_load = (m.save_selected >= 0 && m.save_selected < m.save_count);
    if (big_button(can_load ? "REPRENDRE" : "SELECTIONNE UNE SAUVEGARDE", 42.0f) && can_load)
      m.load_index = m.save_selected;
    ImGui::Dummy(ImVec2(0, 4));
    ImGui::PushStyleColor(ImGuiCol_Text, kGrey);
    if (ImGui::Selectable("< RETOUR", false, 0, ImVec2(120, 0))) m.go_back = true;
    ImGui::PopStyleColor();
  }

  ImGui::End();
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor(2);
}

}  // namespace

void Hud::build(const RenderSnapshot& s, Camera& cam, const char* device_name, float fps,
                MapView* map, StationView* station, MenuView* menu) {
  if (menu)    { menu_hud(*menu); return; }                 // ecran titre + menus (priorite max)
  if (station) { station_hud(s, cam, station); return; }   // interieur ISS (priorite)
  if (!map) { legacy_hud(s, cam, device_name, fps, show_telemetry, show_bodies); return; }

  // ======================= INTERFACE FACON NASA EYES =========================
  ImGuiIO& io = ImGui::GetIO();
  const float W = io.DisplaySize.x, H = io.DisplaySize.y;
  const float aspect = (H > 0.0f) ? W / H : 1.0f;
  ImDrawList* fg = ImGui::GetForegroundDrawList();
  ImFont* font = ImGui::GetFont();

  const ImU32 kWhite = IM_COL32(235, 238, 245, 255);
  const ImU32 kGreen = IM_COL32(60, 210, 120, 255);
  const ImU32 kGrey  = IM_COL32(150, 156, 168, 220);
  const ImU32 kTrack = IM_COL32(90, 96, 108, 200);

  // --- etiquettes des corps (cercle + nom en capitales espacees) -------------
  // On dessine TOUJOURS le marqueur ; on masque le NOM s'il chevauche un nom deja
  // pose (dechevauchement facon NASA Eyes ; l'ordre du snapshot = priorite).
  if (map->show_labels) {
    ImVec2 placed[64];
    int    nplaced = 0;
    for (int i = 0; i < s.body_count; ++i) {
      const BodyView& b = s.bodies[i];
      ImVec2 p;
      if (!project_to_screen(cam, b.position, aspect, W, H, p)) continue;
      if (p.x < -60 || p.x > W + 60 || p.y < -40 || p.y > H + 40) continue;
      // Survol facon NASA Eyes : rond epaissi + plus grand + nom TOUJOURS visible.
      const bool hovered = (map->hover_body == i);
      const ImU32 mk = hovered
          ? IM_COL32(255, 255, 255, 255)
          : IM_COL32(int(b.color.x * 255), int(b.color.y * 255), int(b.color.z * 255), 255);
      fg->AddCircle(p, hovered ? 8.5f : 5.0f, mk, 24, hovered ? 2.8f : 1.6f);
      bool clash = false;
      for (int k = 0; k < nplaced; ++k) {
        const float dx = placed[k].x - p.x, dy = placed[k].y - p.y;
        if (dx * dx + dy * dy < 34.0f * 34.0f) { clash = true; break; }
      }
      if (clash && !hovered) continue;                         // nom masque (chevauche)
      spaced_caps(fg, font, hovered ? 16.0f : 15.0f, ImVec2(p.x + 12.0f, p.y - 8.0f),
                  kWhite, b.name, 1.5f);
      placed[nplaced++] = p;
    }
    // lunes : marqueur plus petit + nom GRIS en minuscules (facon NASA Eyes)
    for (int i = 0; i < map->extra_count; ++i) {
      const MapBody& mb = map->extra_bodies[i];
      ImVec2 p;
      if (!project_to_screen(cam, mb.position, aspect, W, H, p)) continue;
      if (p.x < -40 || p.x > W + 40 || p.y < -30 || p.y > H + 30) continue;
      fg->AddCircle(p, 3.0f, kGrey, 16, 1.2f);
      bool clash = false;
      for (int k = 0; k < nplaced; ++k) {
        const float dx = placed[k].x - p.x, dy = placed[k].y - p.y;
        if (dx * dx + dy * dy < 30.0f * 30.0f) { clash = true; break; }
      }
      if (clash) continue;
      fg->AddText(font, 13.0f, ImVec2(p.x + 8.0f, p.y - 7.0f), kGrey, mb.name);
      if (nplaced < 64) placed[nplaced++] = p;
    }
  }

  // --- marqueur ISS (QG cliquable) : losange + label, entree au clic ----------
  // Cache quand la Terre occulte l'ISS (l'app calcule map->iss_occluded) : le
  // marqueur ne "flotte" plus devant la Terre quand la station est derriere.
  if (map->show_iss && !map->iss_occluded) {
    ImVec2 p;
    if (project_to_screen(cam, map->iss_position, aspect, W, H, p) &&
        p.x > -40 && p.x < W + 40 && p.y > -30 && p.y < H + 30) {
      const float mdx = static_cast<float>(io.MousePos.x) - p.x;
      const float mdy = static_cast<float>(io.MousePos.y) - p.y;
      const bool over = (mdx * mdx + mdy * mdy < 18.0f * 18.0f) && !io.WantCaptureMouse;
      const ImU32 col = over ? IM_COL32(120, 230, 255, 255) : IM_COL32(90, 190, 230, 255);
      // losange (icone station)
      const float r = over ? 9.0f : 6.5f;
      const ImVec2 d0(p.x, p.y - r), d1(p.x + r, p.y), d2(p.x, p.y + r), d3(p.x - r, p.y);
      fg->AddQuad(d0, d1, d2, d3, col, over ? 2.8f : 1.8f);
      spaced_caps(fg, font, over ? 16.0f : 14.0f, ImVec2(p.x + 12.0f, p.y - 8.0f), kWhite, "ISS", 1.5f);
      if (over && !map->iss_focused) {
        fg->AddText(font, 13.0f, ImVec2(p.x + 12.0f, p.y + 10.0f), kGreen, "cliquer pour approcher");
        if (io.MouseClicked[0]) map->focus_iss_request = true;   // gros plan (facon NASA Eyes)
      }
    }
  }

  // --- fiche STATION en gros plan : bouton ENTRER (bas-centre) ----------------
  // Placee AU-DESSUS de la barre de temps (bas-centre, haut ~H-87) : sinon le
  // bouton ENTRER chevauche la ligne "REAL RATE" du panneau de temps.
  if (map->iss_focused) {
    const char* t1 = "STATION SPATIALE INTERNATIONALE";
    const char* t2 = "ORBITE TERRESTRE BASSE - 418 km";
    const ImVec2 ts1 = font->CalcTextSizeA(15.0f, FLT_MAX, 0.0f, t1);
    fg->AddText(font, 15.0f, ImVec2(W * 0.5f - ts1.x * 0.5f, H - 178.0f), kWhite, t1);
    const ImVec2 ts2 = font->CalcTextSizeA(12.0f, FLT_MAX, 0.0f, t2);
    fg->AddText(font, 12.0f, ImVec2(W * 0.5f - ts2.x * 0.5f, H - 158.0f), kGrey, t2);
    // Plus de bouton : la touche M entre dans l'ISS (invite au clavier).
    const char* t3 = "[ M ]  ENTRER DANS L'ISS";
    const ImVec2 ts3 = font->CalcTextSizeA(18.0f, FLT_MAX, 0.0f, t3);
    fg->AddText(font, 18.0f, ImVec2(W * 0.5f - ts3.x * 0.5f, H - 132.0f), kGreen, t3);
  }

  // --- barre de temps (bas-centre) : date | regime | heure + scrubber --------
  if (map->show_time_panel) {
    char dstr[24], tstr[16];
    fmt_datetime(s.epoch_iso, dstr, tstr);
    const bool paused = (map->time.mode == TimeMode::Pause);

    begin_overlay("##timebar", ImVec2(W * 0.5f, H - 26.0f), ImVec2(0.5f, 1.0f));
    // ligne date / regime / heure, centree
    ImGui::PushStyleColor(ImGuiCol_Text, kGrey);
    const float sz = ImGui::GetFontSize();
    const char* rname = rate_name(map->time.mode, map->time.reverse);
    const float wD = ImGui::CalcTextSize(dstr).x, wR = ImGui::CalcTextSize(rname).x, wT = ImGui::CalcTextSize(tstr).x;
    const float total = wD + wR + wT + 80.0f;
    ImGui::Dummy(ImVec2(std::max(480.0f, total), 1.0f));       // largeur mini de la barre
    const float startx = ImGui::GetCursorPosX() + (std::max(480.0f, total) - total) * 0.5f;
    ImGui::SetCursorPosX(startx);
    ImGui::TextUnformatted(dstr);
    ImGui::SameLine(0, 40);
    ImGui::PushStyleColor(ImGuiCol_Text, paused ? kGrey : kGreen);
    ImGui::TextUnformatted(rname);
    ImGui::PopStyleColor();
    ImGui::SameLine(0, 40);
    ImGui::TextUnformatted(tstr);
    ImGui::PopStyleColor();
    (void)sz;

    // play / pause (bouton sans cadre) + scrubber a 7 crans
    static TimeMode s_last = TimeMode::RealTime;
    static int      s_lastrev = 0;
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(255, 255, 255, 26));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(255, 255, 255, 50));
    ImGui::PushStyleColor(ImGuiCol_Text, kWhite);
    if (ImGui::SmallButton(paused ? " |>  ##play" : " || ##pause")) {
      if (paused) { map->time.mode = s_last; map->time.reverse = s_lastrev; }
      else { s_last = map->time.mode; s_lastrev = map->time.reverse; map->time.mode = TimeMode::Pause; }
    }
    ImGui::PopStyleColor(4);
    ImGui::SameLine();
    const float trackW = 360.0f;
    ImGui::InvisibleButton("##scrub", ImVec2(trackW, 22.0f));
    const ImVec2 r0 = ImGui::GetItemRectMin(), r1 = ImGui::GetItemRectMax();
    const float y = (r0.y + r1.y) * 0.5f, x0 = r0.x + 10.0f, x1 = r1.x - 10.0f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddLine(ImVec2(x0, y), ImVec2(x1, y), kTrack, 2.0f);
    for (int k = 0; k < 7; ++k) {
      const float sx = x0 + (x1 - x0) * k / 6.0f;
      dl->AddCircleFilled(ImVec2(sx, y), (k == 3) ? 2.5f : 1.8f, kTrack);
    }
    int stop = stop_from_time(map->time);
    if (ImGui::IsItemActive()) {
      const float f = std::clamp((io.MousePos.x - x0) / (x1 - x0), 0.0f, 1.0f);
      stop = static_cast<int>(std::lround(f * 6.0f));
      time_from_stop(map->time, stop);
    }
    const float hx = x0 + (x1 - x0) * stop / 6.0f;
    const ImU32 hcol = paused ? kGrey : kGreen;
    dl->AddCircleFilled(ImVec2(hx, y), 7.0f, hcol);
    dl->AddCircle(ImVec2(hx, y), 9.5f, IM_COL32(255, 255, 255, 220), 24, 1.5f);
    ImGui::End();
  }

  // --- indicateur LIVE (bas-gauche) : vert si temps reel, cliquable ----------
  {
    begin_overlay("##live", ImVec2(28.0f, H - 26.0f), ImVec2(0.0f, 1.0f));
    const bool live = map->time.is_live;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 c = ImGui::GetCursorScreenPos();
    dl->AddCircleFilled(ImVec2(c.x + 6, c.y + 10), 5.0f, live ? kGreen : kGrey);
    ImGui::Dummy(ImVec2(18, 0)); ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, live ? kGreen : kGrey);
    if (ImGui::Selectable("LIVE", false, 0, ImVec2(48, 0))) {   // clic -> maintenant + temps reel
      map->time.mode = TimeMode::RealTime; map->time.reverse = 0; map->time.go_live = true;
    }
    ImGui::PopStyleColor();
    ImGui::End();
  }

  // --- bouton retour systeme (haut-gauche) -----------------------------------
  {
    begin_overlay("##home", ImVec2(24.0f, 18.0f), ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, kGrey);
    if (ImGui::Selectable("< SYSTEME SOLAIRE", false, 0, ImVec2(160, 0)))
      map->focus_request = -1;
    ImGui::PopStyleColor();
    ImGui::End();
  }
}

} // namespace spr
