// render/app/space_program_3d.cpp
//
// Point d'entree du RenderCore natif Vulkan. Il montre la chaine COMPLETE et
// respecte la doctrine :
//   PhysicsCore (astro_core)  ->  DataBridge (fige)  ->  RenderCore (affiche)
//
// La physique n'est ni modifiee ni simplifiee : on construit une orbite GTO avec
// astro_core (Elements + elements_to_rv), on la fait evoluer par propagation
// keplerienne d'astro_core (nu = M_to_nu(M0 + n.t)), et le pont photographie
// l'etat. Le rendu ne recalcule rien. Dans le VRAI jeu, l'etat viendrait de
// fen::flight::Session (verite/estime) au lieu de cette orbite de demonstration ;
// l'interface du pont ne changerait pas.
//
// Args :  --frames N   quitte apres N frames (verification / capture)
//         --validation active les couches de validation Vulkan (debug)
//         --warp X      facteur de temps (s simulees / s reelles, defaut 2000)
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>

#include "spr/RenderCore.hpp"
#include "spr/bridge/DataBridge.hpp"

#include "fen/astro/Elements.hpp"
#include "fen/core/Constants.hpp"
#include "fen/core/Epoch.hpp"
#include "fen/core/State.hpp"
#include "fen/ephem/Ephemeris.hpp"

int main(int argc, char** argv) {
  int  max_frames = 0;
  bool validation = false;
  double warp = 2000.0;
  const char* capture_path = nullptr;   // --capture <fichier.bmp> : capture la derniere frame
  double cam_dist = 0.0, cam_yaw = 1e9, cam_pitch = 1e9;   // 1e9 = "non fourni"
  bool face_sun = false;                // oriente la camera cote Soleil (face jour)
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) max_frames = std::atoi(argv[++i]);
    else if (std::strcmp(argv[i], "--validation") == 0) validation = true;
    else if (std::strcmp(argv[i], "--warp") == 0 && i + 1 < argc) warp = std::atof(argv[++i]);
    else if (std::strcmp(argv[i], "--capture") == 0 && i + 1 < argc) capture_path = argv[++i];
    else if (std::strcmp(argv[i], "--dist") == 0 && i + 1 < argc) cam_dist = std::atof(argv[++i]);
    else if (std::strcmp(argv[i], "--yaw") == 0 && i + 1 < argc) cam_yaw = std::atof(argv[++i]);
    else if (std::strcmp(argv[i], "--pitch") == 0 && i + 1 < argc) cam_pitch = std::atof(argv[++i]);
    else if (std::strcmp(argv[i], "--face-sun") == 0) face_sun = true;
  }

  using namespace fen;

  // --- 1) la VERITE : ephemeride + une orbite GTO construite par astro_core ---
  ephem::StandishEphemeris eph;
  const double mu = ephem::body_mu(ephem::Body::EarthBary);

  astro::Elements el0{};
  const double rp = cst::R_EARTH + 300.0e3;     // perigee 300 km
  const double ra = cst::R_EARTH + 35786.0e3;   // apogee GEO
  el0.a = 0.5 * (rp + ra);
  el0.e = (ra - rp) / (ra + rp);
  el0.i = 28.5 * cst::DEG;
  el0.raan = 40.0 * cst::DEG;
  el0.argp = 0.0;
  el0.nu = 0.0;
  el0.p = el0.a * (1.0 - el0.e * el0.e);
  el0.rp = rp;
  el0.ra = ra;

  const double M0 = astro::nu_to_M(el0.nu, el0.e);
  const double T  = astro::orbital_period(el0.a, mu);
  const double n  = 2.0 * cst::PI / T;           // moyen mouvement

  const Epoch t0 = epoch_from_iso("2026-07-16T00:00:00");

  spr::WorldConfig cfg;
  cfg.central = ephem::Body::EarthBary;
  cfg.bodies = {ephem::Body::Sun, ephem::Body::Moon};
  spr::DataBridge bridge(eph, cfg);

  std::printf("[space_program_3d] GTO : a=%.1f km  e=%.4f  i=%.1f deg  T=%.1f min\n",
              el0.a / 1000.0, el0.e, el0.i / cst::DEG, T / 60.0);

  // --- 2) le RENDU ------------------------------------------------------------
  spr::RenderCore core;
  spr::RenderConfig rc;
  rc.title = "SPACE PROGRAM - RenderCore (Vulkan)";
  rc.validation = validation;
  try {
    if (!core.init(rc)) { std::fprintf(stderr, "Echec init RenderCore\n"); return 1; }
  } catch (const std::exception& e) {
    std::fprintf(stderr, "Exception init : %s\n", e.what());
    return 1;
  }
  std::printf("[space_program_3d] GPU : %s\n", "(voir HUD)");

  // Reglages de pose camera optionnels (captures / oracles). Ne changent que le
  // point de vue, jamais la physique.
  if (face_sun) {
    // Place l'oeil du cote du Soleil : offset_dir (focus->oeil) = direction du
    // Soleil -> on voit l'hemisphere eclaire de face.
    ephem::PosVel sun_pv = eph.state(ephem::Body::Sun, ephem::Body::EarthBary, t0);
    const double sx = sun_pv.r.x, sy = sun_pv.r.y, sz = sun_pv.r.z;
    const double n = std::sqrt(sx * sx + sy * sy + sz * sz);
    if (n > 0.0) {
      core.camera().yaw = std::atan2(sy, sx);
      core.camera().pitch = std::asin(std::clamp(sz / n, -1.0, 1.0)) * 0.6;  // legere plongee
      std::printf("[cam] face-sun : yaw=%.4f pitch=%.4f\n",
                  core.camera().yaw, core.camera().pitch);
    }
  }
  if (cam_dist > 0.0)   core.camera().set_distance(cam_dist);
  if (cam_yaw < 1e8)    core.camera().yaw = cam_yaw;
  if (cam_pitch < 1e8)  core.camera().pitch = cam_pitch;

  // --- 3) boucle : physique -> pont -> rendu ----------------------------------
  using clock = std::chrono::steady_clock;
  auto prev = clock::now();
  double tau = 0.0;   // secondes simulees ecoulees
  int frame = 0;

  try {
    while (!core.should_close()) {
      auto now = clock::now();
      float dt = std::chrono::duration<float>(now - prev).count();
      prev = now;
      if (dt > 0.1f) dt = 0.1f;  // borne (debug/pause)

      core.begin_frame(dt);
      tau += dt * warp;

      // Etat du vaisseau : propagation keplerienne (astro_core).
      astro::Elements el = el0;
      const double M = M0 + n * tau;
      el.nu = astro::M_to_nu(M, el.e);
      Vec3 r, v;
      astro::elements_to_rv(el, mu, r, v);
      State st{r, v, 1200.0};

      spr::RenderSnapshot snap = bridge.freeze(t0 + tau, st);

      // Capture de la derniere frame (oracle visuel) si --capture fourni.
      if (capture_path && max_frames > 0 && frame == max_frames - 1)
        core.request_capture(capture_path);

      core.render(snap, /*focused_body=*/-1);

      if (max_frames > 0 && ++frame >= max_frames) break;
    }
  } catch (const std::exception& e) {
    std::fprintf(stderr, "Exception boucle : %s\n", e.what());
    core.shutdown();
    return 1;
  }

  core.shutdown();
  std::printf("[space_program_3d] termine proprement.\n");
  return 0;
}
