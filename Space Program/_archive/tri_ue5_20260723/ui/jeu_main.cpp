// ui/jeu_main.cpp - SPACE PROGRAM v0.6, le binaire du jeu.
//   fenetre_jeu                      le jeu (GLFW + ImGui + ImPlot) - AUCUN terminal
//   fenetre_jeu --selftest           traverse tout le jeu SANS UI (verification)
//   fenetre_jeu --capture x.bmp ecran  rend l'ecran demande -> x.bmp
// Lie en -mwindows (sous-systeme GUI) : pas de console au double-clic. Les modes
// CLI rattachent la console du parent pour afficher leur sortie.
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif
#include "imgui.h"
#include "implot.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include "ui/jeu_ecrans.hpp"

using namespace fen;

// ---------------------------------------------------------------------------
static void jouer_vol_geo(app::Jeu& jeu) {
  jeu.vol_engager();                          // LANCER
  jeu.vol_sauter();                           // saute la croisiere (pas de temps reel en test)
  int garde = 0;
  while (!jeu.vol.fini && garde++ < 20) {
    using E = app::EtapeVol;
    if (jeu.vol.etape == E::PretAMF || jeu.vol.etape == E::PretAMF2 || jeu.vol.etape == E::PretTRIM) {
      jeu.vol_observer(); jeu.vol_analyser();
      if (!jeu.vol.prop.valide) { jeu.terminer_vol(); break; }
      jeu.vol_bruler_proposition();
    } else { jeu.vol_engager(); jeu.vol_sauter(); }
  }
}

static int selftest() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);   // pas de buffer : diagnostic fiable en cas de crash
  std::printf("=== SPACE PROGRAM v0.7 - AUTOTEST DU MODELE ===\n\n");
  app::Jeu jeu;
  jeu.creer_agence("AGENCE D'ESSAI", app::ModeAide::Normal);
  std::printf("[1] agence : %.1f M$\n", jeu.agence.tresorerie);

  // -- GEO --
  jeu.accepter_contrat(0);
  const auto d = app::deriver_m00(6378137.0+200e3, 28.5*cst::DEG, 42164170.0);
  jeu.conception.dv_inj_joueur = d.dv_inj; jeu.conception.dv_comb_joueur = d.dv_comb;
  jeu.verifier_derivations();
  std::printf("[2] GEO signe, derivations %s (economie %.1f m/s)\n",
              jeu.conception.derive_ok?"OK":"KO", d.economie);
  jeu.conception.niveau = 5; jeu.conception.revue = true; jeu.conception.instrument = true;
  jeu.recalculer_conception();
  std::printf("[3] bilan : m0 %.0f kg, %.1f M$, %.1f mois, P %.1f%%\n",
              jeu.conception.bilan.m0_kg, jeu.conception.bilan.cost_total,
              jeu.conception.bilan.schedule_months, 100*jeu.conception.bilan.p_success);
  if (!jeu.commit()) { std::printf("COMMIT KO: %s\n", jeu.erreur.c_str()); return 1; }
  jouer_vol_geo(jeu);
  std::printf("[4] VOL GEO : %s | %s\n", jeu.vol.ok?"REUSSIE":"PERDUE", jeu.vol.verdict.c_str());
  while (jeu.mc_en_cours) {}
  std::printf("[5] relais GEO en orbite : %d\n", jeu.relais_geo);

  // ECONOMIE STRICTE : l'autotest se finance (sinon : faillite legitime du scenario,
  // c'est precisement ce que le jeu doit faire - verifie au test [14]).
  jeu.agence.encaisser(400, "subvention d'essai (autotest)");

  // -- gestion / recherche --
  jeu.construire(0);                          // DSN
  jeu.lancer_recherche(0);                    // nav2
  for (int i=0;i<4;++i) jeu.passer_mois();
  std::printf("[6] gestion : DSN %s, nav2 %s, donnees %.0f Gbit\n",
              jeu.installations[0].construite?"OK":"KO",
              jeu.recherche_faite("nav2")?"OK":"en cours", jeu.donnees_gbit);

  // -- Mars (corridor) --
  jeu.accepter_contrat(3);
  jeu.cinterp.n_dep = 15; jeu.cinterp.n_tof = 15;
  jeu.interp_calculer_carte(); while (jeu.cinterp.calcul) {}
  if (jeu.cinterp.grille.empty()) { std::printf("ECHEC : carte non calculee\n"); return 1; }
  // choisir un bon point : parcourir la grille pour le min
  {
    int bi=0,bj=0; float best=1e9f;
    for (int i=0;i<jeu.cinterp.n_dep;++i) for (int j=0;j<jeu.cinterp.n_tof;++j){
      float g=jeu.cinterp.grille[(size_t)i*jeu.cinterp.n_tof+j]; if (g<best){best=g;bi=i;bj=j;} }
    // la grille est orientee bas->haut ; retrouver dep/tof
    double dep = jeu.cinterp.dep0 + (jeu.cinterp.dep1-jeu.cinterp.dep0)*(jeu.cinterp.n_dep-1-bi)/(jeu.cinterp.n_dep-1);
    double tof = jeu.cinterp.tof0 + (jeu.cinterp.tof1-jeu.cinterp.tof0)*bj/(jeu.cinterp.n_tof-1);
    jeu.interp_choisir(dep, tof);
  }
  jeu.cinterp.strategie_tcm = 3; jeu.interp_recalculer();
  std::printf("[7] MARS : C3 %.1f, vinf_arr %.2f km/s, dv_total %.0f m/s, cout %.0f M$\n",
              jeu.cinterp.c3, jeu.cinterp.vinf_arr/1000, jeu.cinterp.dv_total, jeu.cinterp.bilan.cost_total);
  if (jeu.interp_commit()) {
    jeu.interp_faire_tcm(); jeu.interp_passer_tcm();   // phase0 tcm1 -> phase1
    jeu.interp_faire_tcm();                            // tcm2
    jeu.interp_passer_tcm();                           // -> arrivee
    std::printf("[8] MARS ARRIVEE : %s | %s\n", jeu.vinterp.ok?"REUSSIE":"PERDUE",
                std::string(jeu.vinterp.verdict).substr(0,60).c_str());
  }

  // -- comete --
  app::Jeu jeu2; jeu2.creer_agence("COMETE TEST", app::ModeAide::Normal);
  jeu2.accepter_contrat(4);
  jeu2.cinterp.n_dep = 12; jeu2.cinterp.n_tof = 12;
  jeu2.interp_calculer_carte(); while (jeu2.cinterp.calcul) {}
  {
    int bi=0,bj=0; float best=1e9f;
    for (int i=0;i<jeu2.cinterp.n_dep;++i) for (int j=0;j<jeu2.cinterp.n_tof;++j){
      float g=jeu2.cinterp.grille[(size_t)i*jeu2.cinterp.n_tof+j]; if (g<best){best=g;bi=i;bj=j;} }
    double dep = jeu2.cinterp.dep0 + (jeu2.cinterp.dep1-jeu2.cinterp.dep0)*(jeu2.cinterp.n_dep-1-bi)/(jeu2.cinterp.n_dep-1);
    double tof = jeu2.cinterp.tof0 + (jeu2.cinterp.tof1-jeu2.cinterp.tof0)*bj/(jeu2.cinterp.n_tof-1);
    jeu2.interp_choisir(dep, tof);
  }
  jeu2.cinterp.collecteur = true; jeu2.cinterp.strategie_tcm = 3; jeu2.interp_recalculer();
  std::printf("[9] COMETE : C3 %.1f km2/s2, dv_total %.0f m/s\n", jeu2.cinterp.c3, jeu2.cinterp.dv_total);

  // -- TITAN : franchir le mur du mono-etage par l'EMPILEMENT (multi-etages) --
  app::Jeu jeuT; jeuT.creer_agence("TITAN TEST", app::ModeAide::Pro);
  jeuT.accepter_contrat(5);
  jeuT.cinterp.n_dep = 24; jeuT.cinterp.n_tof = 24;   // grille plus fine -> vrai optimum
  jeuT.interp_calculer_carte(); while (jeuT.cinterp.calcul) {}
  {
    int bi=0,bj=0; float best=1e9f;
    for (int i=0;i<jeuT.cinterp.n_dep;++i) for (int j=0;j<jeuT.cinterp.n_tof;++j){
      float g=jeuT.cinterp.grille[(size_t)i*jeuT.cinterp.n_tof+j]; if (g<best){best=g;bi=i;bj=j;} }
    double dep = jeuT.cinterp.dep0 + (jeuT.cinterp.dep1-jeuT.cinterp.dep0)*(jeuT.cinterp.n_dep-1-bi)/(jeuT.cinterp.n_dep-1);
    double tof = jeuT.cinterp.tof0 + (jeuT.cinterp.tof1-jeuT.cinterp.tof0)*bj/(jeuT.cinterp.n_tof-1);
    jeuT.cinterp.assistance = false; jeuT.interp_choisir(dep, tof);   // DIRECT (sans assist) = le pire cas
  }
  jeuT.cinterp.strategie_tcm = 3;
  std::printf("[9b] TITAN direct (sans assist) : C3 %.0f km2/s2, dv_total %.0f m/s\n",
              jeuT.cinterp.c3, jeuT.cinterp.dv_total);
  for (int ne=1; ne<=3; ++ne) { jeuT.cinterp.n_etages=ne; jeuT.interp_recalculer();
    std::printf("       %d etage(s) : m0 %8.0f kg -> %-10s cout %.0f M$\n", ne,
                jeuT.cinterp.bilan.m0_kg, jeuT.cinterp.bilan.fits_mass?"LANCEUR OK":"trop lourd",
                jeuT.cinterp.bilan.cost_total);
  }

  // -- etude --
  app::Jeu jeu3; jeu3.creer_agence("ETUDE TEST", app::ModeAide::Normal);
  jeu3.accepter_contrat(2);
  jeu3.etude.n_dep = 15; jeu3.etude.n_tof = 15;
  jeu3.etude_calculer_porkchop(); while (jeu3.etude.calcul_en_cours) {}
  jeu3.etude.corridor_vu = true; jeu3.etude_livrer();
  std::printf("[10] ETUDE : porkchop C3 min %.2f, livree, tresorerie %.1f M$\n",
              jeu3.etude.best_c3, jeu3.agence.tresorerie);

  // -- v0.7 : coherence matrice <-> bilan (LE bug corrige) --
  {
    app::Jeu jm; jm.creer_agence("MATRICE TEST", app::ModeAide::Normal);
    jm.accepter_contrat(0);
    const auto dm = app::deriver_m00(6378137.0+200e3, 28.5*cst::DEG, 42164170.0);
    jm.conception.dv_inj_joueur = dm.dv_inj; jm.conception.dv_comb_joueur = dm.dv_comb;
    jm.verifier_derivations();
    jm.construire(3);                        // hall d'integration (-1 mois) : le piege historique
    jm.acheter_matrice();
    int incoherences = 0;
    for (auto& cm : jm.matrice) {
      jm.conception.moteur = cm.moteur; jm.conception.niveau = cm.niveau;
      jm.conception.p_mesuree = false; jm.conception.lanceur = -1;
      jm.recalculer_conception();
      // meme config, meme P catalogue -> les 4 verdicts doivent etre IDENTIQUES
      const auto& b = jm.conception.bilan;
      if (b.fits_budget != cm.a.fits_budget || b.fits_schedule != cm.a.fits_schedule ||
          b.fits_mass != cm.a.fits_mass) ++incoherences;
    }
    std::printf("[11] matrice vs bilan (hall construit) : %d incoherence(s) sur %d cases %s\n",
                incoherences, (int)jm.matrice.size(), incoherences==0?"-> BUG CORRIGE":"-> BUG PRESENT");
  }
  // -- v0.7 : VAB (Delta-v Tsiolkovski), marche, faillite --
  {
    app::Jeu jv; jv.creer_agence("VAB TEST", app::ModeAide::Normal);
    jv.accepter_contrat(0);
    const auto dv2 = app::deriver_m00(6378137.0+200e3, 28.5*cst::DEG, 42164170.0);
    jv.conception.dv_inj_joueur = dv2.dv_inj; jv.conception.dv_comb_joueur = dv2.dv_comb;
    jv.verifier_derivations(); jv.recalculer_conception();
    const double dv_auto = jv.vab_dv(), besoin = jv.conception.bilan.dv_design;
    jv.conception.vab_auto = false; jv.conception.vab.ergols = 500; jv.recalculer_conception();
    std::printf("[12] VAB : auto %.0f m/s >= besoin %.0f %s | 500 kg d'ergols -> %.0f m/s (%s)\n",
                dv_auto, besoin, dv_auto >= besoin - 0.5 ? "OK" : "KO",
                jv.vab_dv(), jv.conception.bilan.ok ? "accepte A TORT" : "refuse, correct");
    jv.donnees_gbit = 50; jv.rafraichir_marche();
    jv.vendre_a(2, false, 20);               // OrbitalMedia paie cher les donnees
    std::printf("[13] MARCHE : vendu 20 Gbit a %.2f M$/Gbit, tresorerie %.1f, %d vente(s) en historique\n",
                jv.prix_donnees(2), jv.agence.tresorerie, (int)jv.historique_ventes.size());
    jv.agence.tresorerie = 0.4;              // presque a sec
    jv.passer_mois();                        // les charges du mois tombent
    std::printf("[14] FAILLITE : game_over=%s (%s)\n", jv.game_over?"OUI":"non",
                jv.game_over ? "raison enregistree" : "BUG : aurait du faire faillite");
  }
  // -- sauvegarde --
  bool s = jeu.sauvegarder("autotest.sauvegarde.txt");
  app::Jeu jr; bool r = jr.charger("autotest.sauvegarde.txt");
  std::printf("[15] sauvegarde/chargement : %s (tresorerie %.1f)\n", (s&&r)?"OK":"KO", jr.agence.tresorerie);

  std::printf("\n=== AUTOTEST TERMINE : GEO + Mars + comete + etude + gestion jouables. ===\n");
  return 0;
}

// ---------------------------------------------------------------------------
static bool ecrire_bmp(const char* chemin, int w, int h, const unsigned char* bgr) {
  const int ligne = (3*w+3)&~3, donnees = ligne*h;
  unsigned char e[54] = {'B','M'};
  auto u32=[&](int o,unsigned v){ e[o]=v&255; e[o+1]=(v>>8)&255; e[o+2]=(v>>16)&255; e[o+3]=(v>>24)&255; };
  u32(2,54+donnees); u32(10,54); u32(14,40); u32(18,w); u32(22,h); e[26]=1; e[28]=24; u32(34,donnees);
  FILE* f=std::fopen(chemin,"wb"); if(!f) return false;
  std::fwrite(e,1,54,f);
  std::vector<unsigned char> t(ligne,0);
  for (int y=0;y<h;++y){ std::memcpy(t.data(), bgr+(size_t)y*3*w, (size_t)3*w); std::fwrite(t.data(),1,ligne,f); }
  std::fclose(f); return true;
}
static void preparer(ui::Interface& ui, const std::string& e) {
  if (e=="titre"){ ui.ecran=ui::Ecran::Titre; return; }
  ui.jeu.creer_agence("CAP SPATIAL", app::ModeAide::Normal);
  if (e=="bureau"){ ui.jeu.tuto.actif=true; ui.guide_ouvert=true; ui.ecran=ui::Ecran::Bureau; return; }
  if (e=="systeme"){ ui.jeu.relais_geo=2; ui.jeu.orbiteurs_mars=1; ui.jeu.sondes_lointaines=1; ui.planete_sel=2; ui.ecran=ui::Ecran::Systeme; return; }
  if (e=="reglages"){ ui.ecran=ui::Ecran::Reglages; return; }
  if (e=="contrats"){ ui.ecran=ui::Ecran::Contrats; return; }
  if (e=="titan"){
    ui.jeu.accepter_contrat(5); ui.jeu.cinterp.n_dep=20; ui.jeu.cinterp.n_tof=20;
    ui.jeu.interp_calculer_carte(); while(ui.jeu.cinterp.calcul){}
    int bi=0,bj=0; float best=1e9f;
    for(int i=0;i<ui.jeu.cinterp.n_dep;++i)for(int j=0;j<ui.jeu.cinterp.n_tof;++j){float g=ui.jeu.cinterp.grille[(size_t)i*ui.jeu.cinterp.n_tof+j]; if(g<best){best=g;bi=i;bj=j;}}
    double dep=ui.jeu.cinterp.dep0+(ui.jeu.cinterp.dep1-ui.jeu.cinterp.dep0)*(ui.jeu.cinterp.n_dep-1-bi)/(ui.jeu.cinterp.n_dep-1);
    double tof=ui.jeu.cinterp.tof0+(ui.jeu.cinterp.tof1-ui.jeu.cinterp.tof0)*bj/(ui.jeu.cinterp.n_tof-1);
    ui.jeu.cinterp.assistance=true; ui.jeu.interp_choisir(dep,tof); ui.jeu.cinterp.strategie_tcm=3; ui.jeu.interp_recalculer();
    ui.ecran=ui::Ecran::Carte; return;
  }
  if (e=="gestion"){ ui.jeu.construire(0); ui.jeu.lancer_recherche(0); ui.ecran=ui::Ecran::Gestion; return; }
  if (e=="etude"){ ui.jeu.accepter_contrat(2); ui.jeu.etude.n_dep=25; ui.jeu.etude.n_tof=25;
    ui.jeu.etude_calculer_porkchop(); while(ui.jeu.etude.calcul_en_cours){} ui.ecran=ui::Ecran::Etude; return; }
  if (e=="carte"||e=="volmars"){
    ui.jeu.accepter_contrat(3); ui.jeu.cinterp.n_dep=25; ui.jeu.cinterp.n_tof=25;
    ui.jeu.interp_calculer_carte(); while(ui.jeu.cinterp.calcul){}
    int bi=0,bj=0; float best=1e9f;
    for(int i=0;i<ui.jeu.cinterp.n_dep;++i)for(int j=0;j<ui.jeu.cinterp.n_tof;++j){float g=ui.jeu.cinterp.grille[(size_t)i*ui.jeu.cinterp.n_tof+j]; if(g<best){best=g;bi=i;bj=j;}}
    double dep=ui.jeu.cinterp.dep0+(ui.jeu.cinterp.dep1-ui.jeu.cinterp.dep0)*(ui.jeu.cinterp.n_dep-1-bi)/(ui.jeu.cinterp.n_dep-1);
    double tof=ui.jeu.cinterp.tof0+(ui.jeu.cinterp.tof1-ui.jeu.cinterp.tof0)*bj/(ui.jeu.cinterp.n_tof-1);
    ui.jeu.interp_choisir(dep,tof); ui.jeu.cinterp.strategie_tcm=3; ui.jeu.interp_recalculer();
    if (e=="volmars"){ ui.jeu.interp_commit(); ui.jeu.vinterp.t = ui.jeu.vinterp.t_dep + ui.jeu.vinterp.tof*0.4; ui.ecran=ui::Ecran::VolInterp; }
    else ui.ecran=ui::Ecran::Carte;
    return;
  }
  ui.jeu.accepter_contrat(0);
  const auto d = app::deriver_m00(6378137.0+200e3, 28.5*cst::DEG, 42164170.0);
  ui.jeu.conception.dv_inj_joueur=d.dv_inj; ui.jeu.conception.dv_comb_joueur=d.dv_comb;
  ui.jeu.verifier_derivations(); ui.jeu.conception.revue=true; ui.jeu.recalculer_conception();
  if (e=="programme"){ ui.ecran=ui::Ecran::Programme; return; }
  if (e=="vab"){ ui.forcer_vab=true; ui.jeu.conception.vab_auto=false; ui.ecran=ui::Ecran::Programme; return; }
  if (e=="gameover"){ ui.jeu.agence.tresorerie=0.3; ui.jeu.passer_mois(); ui.ecran=ui::Ecran::GameOver; return; }
  if (e=="vol"||e=="postmortem"){
    ui.jeu.commit(); ui.jeu.vol_engager();
    if (e=="vol"){ ui.jeu.vol.tr_actif=true; ui.jeu.tick(2.0); ui.jeu.vol_observer(); }
    else { ui.jeu.vol_sauter();
      for (int g=0; g<12 && !ui.jeu.vol.fini; ++g){ using E=app::EtapeVol;
        if (ui.jeu.vol.etape==E::PretAMF||ui.jeu.vol.etape==E::PretAMF2||ui.jeu.vol.etape==E::PretTRIM){
          ui.jeu.vol_observer(); ui.jeu.vol_analyser(); if(!ui.jeu.vol.prop.valide){ui.jeu.terminer_vol();break;} ui.jeu.vol_bruler_proposition();
        } else { ui.jeu.vol_engager(); ui.jeu.vol_sauter(); } }
      while (ui.jeu.mc_en_cours) {}
    }
    ui.ecran=ui::Ecran::Vol; return;
  }
}

// Reconstruit la police + l'echelle des widgets a la taille voulue (crisp, pas
// juste un zoom flou). QUALITE : on charge une vraie TTF systeme (Segoe UI) avec
// sur-echantillonnage - fini le rendu pixelise de la police bitmap par defaut.
static ImGuiStyle g_base_style;
static void appliquer_echelle(float s) {
  ImGuiIO& io = ImGui::GetIO();
  ImGui_ImplOpenGL3_DestroyFontsTexture();
  io.Fonts->Clear();
  ImFontConfig cfg;
  cfg.OversampleH = 2; cfg.OversampleV = 2;
  bool ok = false;
#ifdef _WIN32
  const char* ttf[] = {"C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/tahoma.ttf",
                       "C:/Windows/Fonts/arial.ttf"};
  for (const char* f : ttf) {
    FILE* t = std::fopen(f, "rb");
    if (t) { std::fclose(t); io.Fonts->AddFontFromFileTTF(f, 17.0f * s, &cfg); ok = true; break; }
  }
#endif
  if (!ok) { cfg.SizePixels = 16.0f * s; io.Fonts->AddFontDefault(&cfg); }
  io.Fonts->Build();
  ImGui_ImplOpenGL3_CreateFontsTexture();
  ImGui::GetStyle() = g_base_style;
  ImGui::GetStyle().ScaleAllSizes(s);
}

int main(int argc, char** argv) {
#ifdef _WIN32
  // modes CLI : rattacher la console du terminal parent pour voir la sortie
  // (en GUI -mwindows il n'y en a pas ; au double-clic, aucun terminal).
  // Mais si stdout est deja redirige (fichier/pipe), on ne le detourne PAS.
  if (argc >= 2) {
    const DWORD ft = GetFileType(GetStdHandle(STD_OUTPUT_HANDLE));
    const bool redirige = (ft == FILE_TYPE_DISK || ft == FILE_TYPE_PIPE);
    if (!redirige && AttachConsole(ATTACH_PARENT_PROCESS)) {
      (void)!std::freopen("CONOUT$", "w", stdout);
      (void)!std::freopen("CONOUT$", "w", stderr);
    }
  }
#endif
  if (argc>=2 && !std::strcmp(argv[1], "--selftest")) return selftest();
  const bool capture = (argc>=4 && !std::strcmp(argv[1], "--capture"));

  if (!glfwInit()) { std::printf("glfwInit KO\n"); return 1; }
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
  if (capture) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  GLFWwindow* win = glfwCreateWindow(1360, 880, "SPACE PROGRAM", nullptr, nullptr);
  if (!win) { glfwTerminate(); return 1; }
  glfwMakeContextCurrent(win); glfwSwapInterval(1);
  IMGUI_CHECKVERSION();
  ImGui::CreateContext(); ImPlot::CreateContext();
  ImGui::StyleColorsDark();
  ImGui::GetStyle().WindowRounding = 2.0f; ImGui::GetStyle().FrameRounding = 3.0f;
  g_base_style = ImGui::GetStyle();     // reference avant toute mise a l'echelle
  ImGui_ImplGlfw_InitForOpenGL(win, true); ImGui_ImplOpenGL3_Init("#version 130");

  ui::Interface uif;
  // echelle par defaut deduite du DPI du moniteur (4K -> ~1.5-2.0)
  {
    float xs = 1, ys = 1; glfwGetMonitorContentScale(glfwGetPrimaryMonitor(), &xs, &ys);
    const float S[ui::Interface::NB_SCALE] = {0.85f,1.0f,1.25f,1.5f,1.75f,2.0f};
    int best = 1; float bd = 1e9f;
    for (int i = 0; i < ui::Interface::NB_SCALE; ++i) { float d = std::fabs(S[i]-xs); if (d<bd){bd=d;best=i;} }
    uif.scale_choix = capture ? 1 : best;   // capture : echelle 1 pour des images stables
    if (capture && argc >= 5) uif.scale_choix = std::atoi(argv[4]);   // capture --scale
  }
  appliquer_echelle(uif.ui_scale());
  if (capture) preparer(uif, argv[3]);

  int win_x = 60, win_y = 60, win_w = 1360, win_h = 880;   // memorise le mode fenetre
  double t_prev = 0; int images = 0;
  while (!glfwWindowShouldClose(win) && !uif.quitter) {
    glfwPollEvents();
    // appliquer les reglages d'affichage demandes par l'ecran Reglages
    if (uif.appliquer_scale) { uif.appliquer_scale = false; appliquer_echelle(uif.ui_scale()); }
    if (uif.appliquer_affichage) {
      uif.appliquer_affichage = false;
      GLFWmonitor* mon = glfwGetPrimaryMonitor();
      const GLFWvidmode* vm = glfwGetVideoMode(mon);
      if (uif.plein_ecran) {
        glfwGetWindowPos(win, &win_x, &win_y);
        glfwGetWindowSize(win, &win_w, &win_h);
        glfwSetWindowMonitor(win, mon, 0, 0, vm->width, vm->height, vm->refreshRate);
      } else {
        glfwSetWindowMonitor(win, nullptr, win_x, win_y, uif.res_w(), uif.res_h(), 0);
      }
      glfwSwapInterval(1);
    }
    double now = glfwGetTime(); double dt = t_prev>0 ? now - t_prev : 0.016; t_prev = now;
    ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplGlfw_NewFrame(); ImGui::NewFrame();
    int w,h; glfwGetFramebufferSize(win,&w,&h);
    uif.dessiner((float)w,(float)h, capture?0.0:dt);
    ImGui::Render();
    glViewport(0,0,w,h); glClearColor(0.05f,0.06f,0.08f,1); glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(win);
    if (capture && ++images>=3) {
      std::vector<unsigned char> px((size_t)3*w*h);
      glReadBuffer(GL_FRONT); glPixelStorei(GL_PACK_ALIGNMENT,1);
      glReadPixels(0,0,w,h,0x80E0,GL_UNSIGNED_BYTE,px.data());
      ecrire_bmp(argv[2], w, h, px.data());
      std::printf("capture -> %s\n", argv[2]); break;
    }
  }
  ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplGlfw_Shutdown();
  ImPlot::DestroyContext(); ImGui::DestroyContext();
  glfwDestroyWindow(win); glfwTerminate();
  return 0;
}
