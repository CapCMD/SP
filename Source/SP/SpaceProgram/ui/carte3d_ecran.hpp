// ui/carte3d_ecran.hpp — l'écran CARTE 3D : le monde UE dessine, ici on publie.
//
// LA CARTE MONTRE L'ÉTAT ACTUEL, POINT [GDD 8.3, 14] : époque de jeu courante,
// tous les éléments de mission, trajectoire nominale + position ESTIMÉE +
// corridor d'incertitude + prochain nœud [GDD 7.5 : jamais la vérité absolue].
// Aucun curseur temporel, aucun « saut » : le temps appartient au jeu.
// ZERO rendu ici : tout passe par app/bridge_flags.hpp, sens unique.
#pragma once
#include <cmath>
#include <cstdio>
#include "imgui.h"
#include "app/bridge_flags.hpp"
#include "app/jeu.hpp"
#include "fen/core/Epoch.hpp"

namespace fen::ui {

// Actions demandees par le HUD de la carte (traitees par Interface::dessiner —
// la carte est l'ECRAN PRINCIPAL du jeu, le HUD porte donc les actions d'agence).
enum class ActionCarte { Rien = 0, Reglages, Sauver };

inline ActionCarte ecran_carte3d(app::Jeu& jeu, double /*dt_reel*/) {
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

  // --- vol GEO : publication de la vue rapprochée [GDD 8.3, 7.5] -------------
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
    if (!vol_geo) B.close_view = 0;        // pas de vol : la vue rapprochée ferme
  }

  // --------------------------- HUD -------------------------------------------
  // La carte est l'ECRAN PRINCIPAL [plan ARES jalon A] : le HUD porte l'etat de
  // l'agence et ses actions (le bandeau du jeu 2D est debranche).
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
    ImGui::BulletText("%d relais GEO — ephemeride propre (kepler, declare)",
                      jeu.relais_geo);
    ImGui::TextDisabled("      phase vraie ; rayon d'affichage plancher (GEO");
    ImGui::TextDisabled("      invisible a l'echelle carte)");
  }
  if (jeu.orbiteurs_mars > 0) {
    un_element = true;
    ImGui::BulletText("%d orbiteur(s) science — ephemeride propre autour de Mars",
                      jeu.orbiteurs_mars);
    ImGui::TextDisabled("      phase vraie ; rayon d'affichage plancher");
  }
  if (jeu.sondes_lointaines > 0) {
    un_element = true;
    ImGui::BulletText("%d sonde(s) lointaine(s) — orbite de sortie propagee",
                      jeu.sondes_lointaines);
    ImGui::TextDisabled("      (kepler heliocentrique depuis le survol, declare)");
  }
  // le detail par engin : nom, corps de reference, age de mise en service
  if (!jeu.flotte.empty() && ImGui::TreeNode("detail des engins en service")) {
    for (const auto& e : jeu.flotte) {
      const char* corps = e.type == app::EnginFlotte::RelaisGeo ? "Terre"
                        : e.type == app::EnginFlotte::OrbiteurMars ? "Mars" : "Soleil";
      ImGui::BulletText("%s — ref. %s, en service depuis %.1f mois",
                        e.nom.c_str(), corps,
                        (epoch - e.t0) / (30.44 * cst::DAY));
    }
    ImGui::TreePop();
  }
  if (vol_geo) {
    un_element = true;
    ImGui::BulletText("VOL GEO EN COURS — marqueur pres de la Terre (symbolique :");
    ImGui::TextDisabled("      l'orbite GEO est invisible a l'echelle carte)");
    const bool rapp = B.close_view.load() == 1;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 22);
    if (ImGui::SmallButton(rapp ? "RETOUR VUE SYSTEME"
                                : "VUE RAPPROCHEE TERRE (1 u = 100 m)"))
      B.close_view = rapp ? 0 : 1;
    if (rapp && B.geo.valid.load()) {
      ImGui::TextDisabled("      trace cyan = solution de navigation (ESTIMEE)");
      ImGui::TextDisabled("      anneau vert = orbite CIBLE du contrat (nominale)");
      ImGui::TextDisabled("      incertitude : 1-sigma %.1f km (cercles 1/3 sigma)",
                          B.geo.sigma_km);
    }
  }
  if (vol_interp) {
    un_element = true;
    ImGui::BulletText("VOL INTERPLANETAIRE EN COURS :");
    ImGui::TextDisabled("      trajectoire NOMINALE (jaune) + position ESTIMEE");
    ImGui::TextDisabled("      corridor d'incertitude 3-sigma : +/- %.0f km",
                        vi.ellipse_km);
    const double t_next = !vi.tcm1_faite ? vi.t_tcm1 : (!vi.tcm2_faite ? vi.t_tcm2 : 0.0);
    if (t_next > vi.t)
      ImGui::TextDisabled("      prochain noeud de manoeuvre : TCM dans %.1f j",
                          (t_next - vi.t) / cst::DAY);
  }
  if (!un_element) ImGui::TextDisabled("  (aucun element en service ni en vol)");

  // --- navigation : la camera UE suit le corps focalise ----------------------
  ImGui::Dummy(ImVec2(0, 6));
  ImGui::TextUnformatted("FOCUS CAMERA :");
  struct FocusOpt { const char* nom; int corps; };  // valeurs de fen::ephem::Body
  static constexpr FocusOpt focus_opts[] = {
      {"Systeme", -1}, {"Mercure", 1}, {"Venus", 2}, {"Terre", 3}, {"Mars", 5},
      {"Jupiter", 6},  {"Saturne", 7}, {"Uranus", 9}, {"Neptune", 10}, {"Pluton", 11}};
  const int focus_courant = B.focus_body.load();
  int par_ligne = 0;
  for (const auto& o : focus_opts) {
    if (par_ligne > 0) ImGui::SameLine();
    const bool actif = focus_courant == o.corps;
    if (actif) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.26f, 0.42f, 0.66f, 1));
    if (ImGui::SmallButton(o.nom)) B.focus_body = o.corps;
    if (actif) ImGui::PopStyleColor();
    if (++par_ligne >= 5) par_ligne = 0;
  }

  bool lunes = B.show_moons.load();
  if (ImGui::Checkbox("afficher les lunes (distances exagerees x20)", &lunes))
    B.show_moons = lunes;

  ImGui::Dummy(ImVec2(0, 4));
  ImGui::TextDisabled("Echelle CARTE declaree : 1 UA = 500 m, rayons x200 (Soleil x8).");
  ImGui::TextDisabled("Rotation propre : periodes siderales vraies, axe ecliptique");
  ImGui::TextDisabled("(obliquite ignoree — approximation declaree).");
  ImGui::TextDisabled("Planetes GLB : Tools/import_glb_planets.py (sinon spheres).");

  ImGui::EndChild();
  ImGui::PopStyleColor();
  return action;
}

} // namespace fen::ui
