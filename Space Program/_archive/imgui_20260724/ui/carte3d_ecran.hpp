// ui/carte3d_ecran.hpp — LA CARTE DU SYSTÈME SOLAIRE : l'écran principal du jeu.
//
// LA CARTE MONTRE L'ÉTAT ACTUEL, POINT [GDD 8.3, 14] : époque de jeu courante,
// tous les éléments de mission, trajectoire nominale + position ESTIMÉE +
// corridor d'incertitude + prochain nœud [GDD 7.5 : jamais la vérité absolue].
// Aucun curseur temporel, aucun « saut » : le temps appartient au jeu.
//
// ZÉRO rendu 3D ici : tout passe par app/bridge_flags.hpp, sens unique. En
// revanche l'ENTRÉE vit ici — l'overlay Slate capte toute la souris, le monde UE
// ne reçoit aucun clic. Ce fichier :
//   . publie l'état à dessiner (époque, flotte, vols) ;
//   . COMMANDE la caméra (molette = zoom, glisser = orbite) ;
//   . dessine les MARQUEURS des corps et fait le PICKING, à partir de la
//     projection écran que le monde UE publie en retour.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdio>
#include "imgui.h"
#include "app/bridge_flags.hpp"
#include "app/jeu.hpp"
#include "fen/core/Epoch.hpp"
#include "fen/ephem/Ephemeris.hpp"

namespace fen::ui {

// Actions demandées par le HUD de la carte (traitées par Interface::dessiner —
// la carte est l'ÉCRAN PRINCIPAL du jeu, le HUD porte donc les actions d'agence).
enum class ActionCarte { Rien = 0, Reglages, Sauver, Menu, Station };

// Distance de vue par défaut pour un corps : de quoi le voir en entier.
inline double distance_cadrage(int body) {
  if (body < 0) return 9.0e8;                       // vue système (~6 UA)
  const double r_km = ephem::body_radius((ephem::Body)body) / 1000.0;
  return std::max(6.0 * r_km, 3000.0);
}

inline ActionCarte ecran_carte3d(app::Jeu& jeu, double dt_reel) {
  auto& B = app::g_render_bridge;
  const auto& vi = jeu.vinterp;
  const bool vol_interp = vi.commis && !vi.fini && vi.arc_t.size() >= 2;
  const bool vol_geo = jeu.vol.commis && !jeu.vol.fini;

  // ÉPOQUE = MAINTENANT. Un vol en cours EST l'horloge de jeu la plus avancée ;
  // sinon, calendrier agence. (Pendant un vol GEO — heures/jours — les planètes
  // ne bougent pas perceptiblement : calendrier agence, déclaré suffisant.)
  const double epoch = vol_interp ? vi.t : jeu.epoch_courant();
  B.epoch_tdb = epoch;

  // --- flotte en service : éphéméride PAR ENGIN [GDD 8.3] --------------------
  // Position ESTIMÉE de chaque engin à l'époque de jeu, relative à son corps
  // de référence. Le calcul vit dans app::Jeu (modèle déclaré) — ici on publie.
  {
    int n = 0;
    for (const auto& e : jeu.flotte) {
      if (n >= app::RenderBridge::FleetSnap::MAX) break;
      auto& c = B.fleet.craft[n];
      c.type = e.type;
      c.parent = jeu.flotte_parent(e);
      const Vec3 p = jeu.flotte_position_rel(e, epoch);
      c.rel_m[0] = p.x; c.rel_m[1] = p.y; c.rel_m[2] = p.z;
      ++n;
    }
    B.fleet.n = n;
    B.fleet.vol_geo_actif = vol_geo;
  }

  // --- vol interplanétaire : nominale + estimé + corridor + nœuds ------------
  if (vol_interp) {
    // interpolation linéaire de l'arc nominal à l'instant t (position ESTIMÉE :
    // dans le modèle v0.6 l'estimé suit la nominale, l'incertitude vit dans
    // ellipse_km — c'est le corridor, pas un troisième tracé. Déclaré.)
    auto pos_arc = [&vi](double t, double& x_ua, double& y_ua) {
      std::size_t k = 1;
      while (k + 1 < vi.arc_t.size() && vi.arc_t[k] < t) ++k;
      const double t0 = vi.arc_t[k - 1], t1 = vi.arc_t[k];
      const double a = (t1 > t0) ? (t - t0) / (t1 - t0) : 0.0;
      const double f = a < 0.0 ? 0.0 : (a > 1.0 ? 1.0 : a);
      x_ua = vi.arc_x[k - 1] + f * (vi.arc_x[k] - vi.arc_x[k - 1]);
      y_ua = vi.arc_y[k - 1] + f * (vi.arc_y[k] - vi.arc_y[k - 1]);
    };

    // l'arc nominal est FIGÉ au commit : publié une seule fois
    const double sig = vi.t_dep + static_cast<double>(vi.arc_t.size());
    if (B.last_arc_sig != sig) {
      const int n_src = static_cast<int>(vi.arc_t.size());
      const int n = n_src < app::RenderBridge::VehicleSnap::MAX_PTS
                        ? n_src : app::RenderBridge::VehicleSnap::MAX_PTS;
      for (int i = 0; i < n; ++i) {
        const int k = (n_src - 1) * i / (n - 1);
        B.vehicle.traj_m[i][0] = vi.arc_x[k] * cst::AU;
        B.vehicle.traj_m[i][1] = vi.arc_y[k] * cst::AU;
        B.vehicle.traj_m[i][2] = 0.0;
      }
      B.vehicle.n = n;
      B.last_arc_sig = sig;
      B.vehicle.gen.fetch_add(1);
    }

    double x, y;
    pos_arc(vi.t, x, y);
    B.vehicle.pos_m[0] = x * cst::AU;
    B.vehicle.pos_m[1] = y * cst::AU;
    B.vehicle.pos_m[2] = 0.0;
    B.vehicle.corridor_3s_m = vi.ellipse_km * 1000.0;   // 3σ courante du modèle

    // nœuds de manœuvre restants [GDD 8.3] : TCM1 / TCM2 sur l'arc
    int n_nodes = 0;
    const double t_noeuds[2] = {vi.t_tcm1, vi.t_tcm2};
    const bool faits[2] = {vi.tcm1_faite, vi.tcm2_faite};
    for (int i = 0; i < 2; ++i) {
      if (t_noeuds[i] <= vi.arc_t.front() || t_noeuds[i] >= vi.arc_t.back()) continue;
      pos_arc(t_noeuds[i], x, y);
      B.vehicle.nodes_m[n_nodes][0] = x * cst::AU;
      B.vehicle.nodes_m[n_nodes][1] = y * cst::AU;
      B.vehicle.nodes_m[n_nodes][2] = 0.0;
      B.vehicle.node_done[n_nodes] = faits[i];
      ++n_nodes;
    }
    B.vehicle.n_nodes = n_nodes;
    B.vehicle.valid = true;
  } else {
    B.vehicle.valid = false;
    B.last_arc_sig = -1.0;
  }

  // --- vol GEO : trace estimée + orbite cible [GDD 8.3, 7.5] ----------------
  // À l'échelle VRAIE, l'orbite GEO (42 164 km) se voit directement sur la carte
  // dès qu'on focalise la Terre : la scène « rapprochée » séparée d'avant n'a
  // plus lieu d'être.
  if (vol_geo && jeu.vol.traj_t.size() >= 2) {
    const auto& vo = jeu.vol;
    const double sig = static_cast<double>(vo.traj_t.size());
    if (B.last_geo_sig != sig) {           // la trace s'allonge : republier
      const int n_src = static_cast<int>(vo.traj_t.size());
      const int n = n_src < app::RenderBridge::GeoFlightSnap::MAX_PTS
                        ? n_src : app::RenderBridge::GeoFlightSnap::MAX_PTS;
      for (int i = 0; i < n; ++i) {
        const int k = (n_src - 1) * i / (n - 1);
        B.geo.traj_km[i][0] = vo.traj_x[k];
        B.geo.traj_km[i][1] = vo.traj_y[k];
        B.geo.traj_km[i][2] = vo.traj_z[k];
      }
      B.geo.n = n;
      B.last_geo_sig = sig;
      B.geo.gen.fetch_add(1);
    }
    const Vec3 p = jeu.vol_position_estimee();   // km — l'ESTIMÉ, pas la vérité
    B.geo.pos_km[0] = p.x;
    B.geo.pos_km[1] = p.y;
    B.geo.pos_km[2] = p.z;
    B.geo.sigma_km = vo.sigma_pos_km;
    B.geo.target_sma_km =
        (jeu.actif() ? jeu.actif()->cible_sma : 42164170.0) / 1000.0;
    B.geo.valid = true;
  } else {
    B.geo.valid = false;
    B.last_geo_sig = -1.0;
  }

  // ═══════════════ MARQUEURS DES CORPS + PICKING ═══════════════
  // À l'échelle vraie une planète est sous-pixellique de loin : le monde UE
  // publie sa projection, on dessine ici le marqueur, le libellé, et on capte
  // le clic (le monde UE ne reçoit aucune souris).
  const ImVec2 disp = ImGui::GetIO().DisplaySize;
  ImDrawList* fond = ImGui::GetBackgroundDrawList();
  const ImVec2 souris = ImGui::GetIO().MousePos;
  int survole = -1;
  float d2_survol = 1e30f;
  {
    const int n = B.screen.n.load();
    for (int i = 0; i < n && i < app::RenderBridge::ScreenBodies::MAX; ++i) {
      const auto& it = B.screen.items[i];
      if (!it.on_screen) continue;
      const ImVec2 p(it.nx * disp.x, it.ny * disp.y);
      const float r_px = it.r_norm * disp.x;          // rayon apparent réel
      const bool focalise = (B.focus_body.load() == it.body);
      // Marqueur : cercle constant (~7 px) tant que le corps est plus petit que
      // lui ; au-delà, le corps se voit tout seul et le marqueur s'efface.
      const float r_marq = 7.0f;
      if (r_px < r_marq) {
        const ImU32 col = focalise ? IM_COL32(255, 220, 120, 255)
                                   : IM_COL32(150, 190, 235, 190);
        fond->AddCircle(p, r_marq, col, 0, focalise ? 2.0f : 1.4f);
        fond->AddCircleFilled(p, 1.6f, col);
      } else if (focalise) {
        fond->AddCircle(p, r_px + 6.0f, IM_COL32(255, 220, 120, 160), 0, 1.6f);
      }
      // libellé
      const char* nom = ephem::body_name((ephem::Body)it.body);
      fond->AddText(ImVec2(p.x + std::max(r_px, r_marq) + 5.0f, p.y - 7.0f),
                    focalise ? IM_COL32(255, 235, 180, 255) : IM_COL32(190, 210, 235, 210),
                    nom);
      // survol / picking : le plus proche du curseur dans un rayon de 18 px
      const float dx = souris.x - p.x, dy = souris.y - p.y;
      const float d2 = dx * dx + dy * dy;
      const float seuil = std::max(18.0f, r_px);
      if (d2 < seuil * seuil && d2 < d2_survol) { d2_survol = d2; survole = it.body; }
    }
  }

  // --------------------------- HUD -------------------------------------------
  // La carte est l'ÉCRAN PRINCIPAL : le HUD porte l'état de l'agence et ses
  // actions (le bandeau du jeu 2D est débranché).
  ActionCarte action = ActionCarte::Rien;
  ImGui::SetCursorPosX(14.0f);
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.05f, 0.07f, 0.10f, 0.78f));
  ImGui::BeginChild("##carte3d_hud", ImVec2(440, 0),
                    ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY);
  {   // --- bandeau d'agence -----------------------------------------------
    const auto& A = jeu.agence;
    ImGui::TextColored(ImVec4(0.9f, 0.85f, 0.6f, 1), "%s", A.nom.c_str());
    ImGui::SameLine(0, 14);
    ImGui::TextColored(A.tresorerie < 0 ? ImVec4(0.95f, 0.35f, 0.3f, 1)
                                        : ImVec4(0.55f, 0.85f, 0.55f, 1),
                       "%.1f M$", A.tresorerie);
    ImGui::SameLine(0, 14); ImGui::Text("T+%.1f mois", A.mois);
    ImGui::SameLine(0, 14); ImGui::TextDisabled("confiance %.0f%%", 100 * A.confiance);
    if (ImGui::SmallButton("PASSER 1 MOIS")) jeu.passer_mois();
    ImGui::SameLine();
    if (ImGui::SmallButton("SAUVER")) action = ActionCarte::Sauver;
    ImGui::SameLine();
    if (ImGui::SmallButton("REGLAGES")) action = ActionCarte::Reglages;
    ImGui::SameLine();
    if (ImGui::SmallButton("MENU")) action = ActionCarte::Menu;
    // [M] : retour à bord — la carte est un MODE, le QG reste l'ISS.
    if (ImGui::IsKeyPressed(ImGuiKey_M, false)) action = ActionCarte::Station;
  }
  ImGui::Separator();
  ImGui::TextColored(ImVec4(0.55f, 0.85f, 0.55f, 1), "CARTE DU SYSTEME SOLAIRE");
  ImGui::TextDisabled("ephemeride %s", jeu.eph.model_name());
  ImGui::Text("Epoque de jeu : %s TDB", epoch_to_iso(Epoch{epoch}).c_str());
  ImGui::Separator();

  ImGui::TextUnformatted("ELEMENTS DE MISSION SUR LA CARTE :");
  bool un_element = false;
  if (jeu.relais_geo > 0) {
    un_element = true;
    ImGui::BulletText("%d relais GEO — ephemeride propre (kepler, declare)", jeu.relais_geo);
  }
  if (jeu.orbiteurs_mars > 0) {
    un_element = true;
    ImGui::BulletText("%d orbiteur(s) science autour de Mars", jeu.orbiteurs_mars);
  }
  if (jeu.sondes_lointaines > 0) {
    un_element = true;
    ImGui::BulletText("%d sonde(s) lointaine(s) — orbite de sortie propagee", jeu.sondes_lointaines);
  }
  if (!jeu.flotte.empty() && ImGui::TreeNode("detail des engins en service")) {
    for (const auto& e : jeu.flotte) {
      const char* corps = e.type == app::EnginFlotte::RelaisGeo ? "Terre"
                        : e.type == app::EnginFlotte::OrbiteurMars ? "Mars" : "Soleil";
      ImGui::BulletText("%s — ref. %s, en service depuis %.1f mois",
                        e.nom.c_str(), corps, (epoch - e.t0) / (30.44 * cst::DAY));
    }
    ImGui::TreePop();
  }
  if (vol_geo) {
    un_element = true;
    ImGui::BulletText("VOL GEO EN COURS — focalise la Terre pour le voir");
    ImGui::TextDisabled("      trace cyan = solution de navigation (ESTIMEE)");
    ImGui::TextDisabled("      anneau vert = orbite CIBLE du contrat (nominale)");
    ImGui::TextDisabled("      incertitude 1-sigma : %.1f km", B.geo.sigma_km);
  }
  if (vol_interp) {
    un_element = true;
    ImGui::BulletText("VOL INTERPLANETAIRE EN COURS :");
    ImGui::TextDisabled("      trajectoire NOMINALE (jaune) + position ESTIMEE");
    ImGui::TextDisabled("      corridor d'incertitude 3-sigma : +/- %.0f km", vi.ellipse_km);
    const double t_next = !vi.tcm1_faite ? vi.t_tcm1 : (!vi.tcm2_faite ? vi.t_tcm2 : 0.0);
    if (t_next > vi.t)
      ImGui::TextDisabled("      prochain noeud de manoeuvre : TCM dans %.1f j",
                          (t_next - vi.t) / cst::DAY);
  }
  if (!un_element) ImGui::TextDisabled("  (aucun element en service ni en vol)");

  // --- navigation : la caméra suit le corps focalisé ------------------------
  ImGui::Dummy(ImVec2(0, 6));
  ImGui::TextUnformatted("FOCUS CAMERA (ou clic sur un corps) :");
  struct FocusOpt { const char* nom; int corps; };  // valeurs de fen::ephem::Body
  static constexpr FocusOpt focus_opts[] = {
      {"Soleil", -1}, {"Mercure", 1}, {"Venus", 2}, {"Terre", 3}, {"Lune", 4},
      {"Mars", 5},    {"Jupiter", 6}, {"Saturne", 7}, {"Uranus", 9},
      {"Neptune", 10},{"Pluton", 11}};
  const int focus_courant = B.focus_body.load();
  int nouveau_focus = focus_courant;
  int par_ligne = 0;
  for (const auto& o : focus_opts) {
    if (par_ligne > 0) ImGui::SameLine();
    const bool actif = focus_courant == o.corps;
    if (actif) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.26f, 0.42f, 0.66f, 1));
    if (ImGui::SmallButton(o.nom)) nouveau_focus = o.corps;
    if (actif) ImGui::PopStyleColor();
    if (++par_ligne >= 4) par_ligne = 0;
  }

  bool lunes = B.show_moons.load();
  if (ImGui::Checkbox("afficher les lunes", &lunes)) B.show_moons = lunes;

  // --- l'échelle : plus rien à exagérer -------------------------------------
  ImGui::Dummy(ImVec2(0, 4));
  {
    const double d = B.cam.dist_km.load();
    if (d > 1.0e7) ImGui::Text("Distance de vue : %.3f UA", d * 1000.0 / cst::AU);
    else           ImGui::Text("Distance de vue : %.0f km", d);
  }
  ImGui::TextDisabled("ECHELLE VRAIE : 1 unite = 1 km. Rayons et distances REELS,");
  ImGui::TextDisabled("aucune exageration. Un corps lointain est sous-pixellique :");
  ImGui::TextDisabled("son marqueur et son libelle prennent le relais.");
  ImGui::TextDisabled("Rotation propre : periodes siderales vraies, axe ecliptique");
  ImGui::TextDisabled("(obliquite ignoree — approximation declaree).");
  ImGui::TextDisabled("Molette = zoom . glisser gauche = pivoter . clic = focus");

  const ImVec2 hud_min = ImGui::GetWindowPos();
  const ImVec2 hud_max = ImVec2(hud_min.x + ImGui::GetWindowSize().x,
                                hud_min.y + ImGui::GetWindowSize().y);
  ImGui::EndChild();
  ImGui::PopStyleColor();

  // ═══════════════ COMMANDE DE LA CAMÉRA (souris hors HUD) ═══════════════
  const bool sur_hud = souris.x >= hud_min.x && souris.x <= hud_max.x &&
                       souris.y >= hud_min.y && souris.y <= hud_max.y;
  const ImGuiIO& io = ImGui::GetIO();
  static bool glisse = false;                 // glissement commencé hors HUD
  if (!sur_hud && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) glisse = true;
  if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) glisse = false;

  if (!sur_hud && io.MouseWheel != 0.0f) {
    // ZOOM LOGARITHMIQUE : du rayon d'une planète à la ceinture de Kuiper, c'est
    // 9 ordres de grandeur — seul un pas multiplicatif est utilisable.
    const double f = std::exp(-io.MouseWheel * 0.22);
    const double r_focus = focus_courant < 0
        ? 1.0e6 : ephem::body_radius((ephem::Body)focus_courant) / 1000.0;
    B.cam.dist_km = std::clamp(B.cam.dist_km.load() * f, r_focus * 1.15, 2.0e10);
  }
  if (glisse && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f)) {
    const ImVec2 d = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 2.0f);
    ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
    B.cam.yaw = B.cam.yaw.load() - d.x * 0.006;
    B.cam.pitch = std::clamp(B.cam.pitch.load() + d.y * 0.006, -1.5, 1.5);
  }
  // PICKING : un clic franc (sans glissement) sur un corps le focalise.
  if (!sur_hud && survole >= 0 && ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
      ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 2.0f).x == 0.0f &&
      ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 2.0f).y == 0.0f)
    nouveau_focus = survole;

  if (nouveau_focus != focus_courant) {
    B.focus_body = nouveau_focus;
    B.cam.dist_km = distance_cadrage(nouveau_focus);   // cadrage d'arrivée
  }
  (void)dt_reel;
  return action;
}

} // namespace fen::ui
