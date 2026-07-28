// tests/test_toolchain.cpp — LA TOOLCHAIN EMBARQUÉE [GDD 15.1, 15.5, 18]
//
// Ce que ces oracles prouvent, et c'est le contrat de [GDD 18] mot pour mot :
//   . le code du joueur est du VRAI C++, compilé pour de bon contre `ares::vol` ;
//   . ce qui ne compile pas est refusé avec les diagnostics du compilateur, à
//     coût nul [GDD 15.5 étape 1] ;
//   . « un pointeur invalide produit un ÉCHEC DE MISSION, jamais un crash du
//     jeu » — le processus fils meurt seul, le testeur continue ;
//   . une boucle infinie est TUÉE au bout du délai : le bac à sable borne le
//     temps, il ne l'espère pas ;
//   . les décisions remontent : exécuter, différer, alerter, replanifier.
#ifdef SP_STANDALONE_TESTS
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#include "fen/code/Toolchain.hpp"

static int g_ok = 0, g_ko = 0;
static void CHECK(bool c, const char* quoi) {
  if (c) { ++g_ok; }
  else { ++g_ko; std::printf("ECHEC : %s\n", quoi); }
}

int main(int argc, char** argv) {
  using namespace fen::code;

  const std::string racine = argc > 1 ? argv[1] : ".";
  const std::string tmp = (std::filesystem::temp_directory_path() / "sp_toolchain").string();
  std::error_code ec;
  std::filesystem::create_directories(tmp, ec);

  ToolchainConfig cfg;
  cfg.dossier_travail = tmp;
  cfg.includes = {racine + "/Source/SP/SpaceProgram/astro_core/include",
                  racine + "/Source/SP/SpaceProgram/mission/include",
                  racine + "/Source/SP/SpaceProgram"};
  cfg.sources = {racine + "/Source/SP/SpaceProgram/astro_core/src/Kepler.cpp"};
  cfg.vcvars = "C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Auxiliary\\Build\\vcvars64.bat";
  cfg.timeout_ms = 4000;

  // Un vol dont l'écart au but dépasse largement la tolérance : le code du
  // joueur DOIT décider quelque chose.
  EntreesVol e;
  e.pos = {1.0e11, 0.0, 0.0};
  e.vel = {0.0, 3.0e4, 0.0};
  e.sigma3_m = 2000.0;                 // solution SAINE (< 12 km du squelette)
  e.cible = {1.0e11 + 5.0e6, 0.0, 0.0};   // 5 000 km d'écart
  e.tolerance_m = 1000.0;
  e.dv_disponible = 500.0;

  // ---- 1) SÉRIALISATION : le journal des entrées se relit [GDD 18] --------
  {
    const EntreesVol r = EntreesVol::lire(e.serialiser());
    CHECK(r.pos.x == e.pos.x && r.cible.x == e.cible.x &&
          r.sigma3_m == e.sigma3_m && r.dv_disponible == e.dv_disponible,
          "entrees : une execution se REJOUE — ses entrees se relisent a l identique");
    EntreesVol h = e; h.tau_s = 137.0 * 86400.0;
    CHECK(EntreesVol::lire(h.serialiser()).tau_s == h.tau_s,
          "entrees : l horizon de manoeuvre fait partie du journal [GDD 18]");
  }

  // ---- 2) LE SQUELETTE DU GDD COMPILE ET S'EXÉCUTE ------------------------
  // C'est LITTÉRALEMENT l'exemple de [GDD 15.3]. S'il ne tournait pas, le
  // document décrirait une API que le jeu n'a pas.
  const ResultatToolchain r_ok = compiler_et_executer(squelette_vol(), e, cfg);
  if (r_ok.issue == IssueCode::Indisponible) {
    std::printf("TOOLCHAIN : compilateur absent de cette machine — oracles ignores.\n");
    std::printf("  (le mecanisme est teste la ou `cl.exe` existe ; [GDD 18] veut\n"
                "   qu'il soit EMBARQUE dans la distribution)\n");
    return 0;
  }
  if (!r_ok.ok()) std::printf("diagnostics:\n%s\n", r_ok.diagnostics.c_str());
  CHECK(r_ok.ok(), "squelette : l exemple de [GDD 15.3] compile et s execute pour de vrai");
  CHECK(r_ok.decisions.execute, "squelette : il EXECUTE une correction");
  CHECK(fen::norm(r_ok.decisions.dv) > 0.0, "squelette : le Dv remonte, non nul");
  CHECK(!r_ok.decisions.journal.empty(), "squelette : le journal de bord remonte");
  std::printf("     toolchain : Dv execute = %.3f m/s | journal : %s\n",
              fen::norm(r_ok.decisions.dv),
              r_ok.decisions.journal.empty() ? "-" : r_ok.decisions.journal[0].c_str());

  // ---- 2 bis) L'HORIZON DE MANŒUVRE COMMANDE L'AMPLEUR DU Δv -------------
  // Le `Solveur` de [GDD 15.3] corrige « proportionnellement à l'écart sur un
  // temps caractéristique ». Ce temps est celui qui RESTE avant le point de
  // visée : rattraper 5 000 km en un jour ou en deux cents n'est pas la même
  // manœuvre. Si l'horizon n'était pas transmis, tout vol interplanétaire se
  // verrait commander un Δv deux cents fois trop grand — une erreur de cadrage
  // que rien à l'écran ne trahirait.
  {
    EntreesVol lointain = e;
    lointain.tau_s = 200.0 * 86400.0;
    const ResultatToolchain r = compiler_et_executer(squelette_vol(), lointain, cfg);
    CHECK(r.ok() && r.decisions.execute, "horizon : le squelette execute aussi de loin");
    const double proche = fen::norm(r_ok.decisions.dv);
    const double loin = fen::norm(r.decisions.dv);
    CHECK(loin < proche,
          "horizon : plus l arrivee est loin, plus la correction est douce");
    // Le rapport n'est PLUS exactement 1/200 : le solveur résout sur la matrice
    // de transition képlérienne, pas sur une règle de trois (piège n°72). Sur un
    // arc long, la géométrie de l'orbite domine — c'est précisément ce que la
    // formule proportionnelle ignorait.
    CHECK(loin < 0.1 * proche,
          "horizon : ... et d au moins un ordre de grandeur sur deux cents jours");
    std::printf("     toolchain : Dv a 1 j = %.3f m/s | a 200 j = %.4f m/s\n", proche, loin);
  }

  // ---- 3) CE QUI NE COMPILE PAS EST REFUSÉ, AVEC LES DIAGNOSTICS ----------
  {
    const ResultatToolchain r = compiler_et_executer(
        "#include <ares/vol.hpp>\n"
        "void sequence_correction(ares::vol::Contexte& ctx) { ce_symbole_n_existe_pas(); }\n",
        e, cfg);
    CHECK(r.issue == IssueCode::ErreurCompilation,
          "compilation : un code faux est REFUSE avant tout cout [GDD 15.5]");
    CHECK(!r.diagnostics.empty() &&
          r.diagnostics.find("ce_symbole_n_existe_pas") != std::string::npos,
          "compilation : les diagnostics du compilateur remontent tels quels");
  }

  // ---- 4) UN POINTEUR INVALIDE = ÉCHEC DE MISSION, PAS UN CRASH DU JEU ----
  // L'oracle central de [GDD 18]. Si ce test passe, c'est que le processus fils
  // est mort SEUL : le testeur, lui, a survécu pour l'écrire.
  {
    const ResultatToolchain r = compiler_et_executer(
        "#include <ares/vol.hpp>\n"
        "void sequence_correction(ares::vol::Contexte& ctx) {\n"
        "  volatile int* p = (int*)0; *p = 42; (void)ctx;\n"
        "}\n",
        e, cfg);
    CHECK(r.issue == IssueCode::Plantage,
          "isolation : un pointeur invalide produit un ECHEC DE MISSION [GDD 18]");
    CHECK(r.code_sortie != 0, "isolation : ... et le processus fils meurt SEUL");
    std::printf("     toolchain : plantage isole, code de sortie 0x%X\n",
                (unsigned)r.code_sortie);
  }

  // ---- 5) UNE BOUCLE INFINIE EST TUÉE AU DÉLAI ---------------------------
  {
    ToolchainConfig court = cfg;
    court.timeout_ms = 1500;
    const ResultatToolchain r = compiler_et_executer(
        "#include <ares/vol.hpp>\n"
        "void sequence_correction(ares::vol::Contexte& ctx) {\n"
        "  volatile long long n = 0; while (true) { ++n; } (void)ctx;\n"
        "}\n",
        e, court);
    if (r.issue != IssueCode::Delai)
      std::printf("  [delai] issue=%s code=0x%X duree=%.0f ms\n%s\n",
                  issue_nom(r.issue), (unsigned)r.code_sortie, r.duree_ms,
                  r.diagnostics.c_str());
    CHECK(r.issue == IssueCode::Delai,
          "isolation : une boucle infinie est TUEE au delai — le jeu ne gele pas");
  }

  // ---- 5 bis) ... ET IL NE RESTE AUCUN FUYARD -----------------------------
  // L'oracle du délai ci-dessus ne prouvait QUE la détection : `depasse` était
  // vrai alors que le programme du joueur continuait de tourner derrière son
  // shell tué (piège n°71). On vérifie donc ce qui compte vraiment — que le
  // binaire soit LIBÉRÉ après le délai. S'il ne l'était pas, il serait encore
  // ouvert par son processus et le système refuserait de l'effacer.
  {
    ToolchainConfig court = cfg;
    court.timeout_ms = 1200;
    const ResultatToolchain r = compiler_et_executer(
        "#include <ares/vol.hpp>\n"
        "void sequence_correction(ares::vol::Contexte& ctx) {\n"
        "  volatile long long n = 0; while (true) { ++n; } (void)ctx;\n"
        "}\n",
        e, court);
    CHECK(r.issue == IssueCode::Delai, "isolation : ... et le suivant l est aussi");
    // Le dernier binaire produit porte le numéro de compilation le plus élevé ;
    // on le retrouve en balayant le dossier de travail.
    std::filesystem::path dernier;
    std::filesystem::file_time_type quand{};
    for (const auto& f : std::filesystem::directory_iterator(tmp)) {
      if (f.path().extension() != ".exe") continue;
      if (dernier.empty() || f.last_write_time() > quand) {
        dernier = f.path(); quand = f.last_write_time();
      }
    }
    bool libere = false;
    if (!dernier.empty()) {
      std::error_code e2;
      libere = std::filesystem::remove(dernier, e2);
    }
    CHECK(libere,
          "isolation : au delai, le PROCESSUS DU JOUEUR meurt — pas seulement son shell");
  }

  // ---- 5 ter) UNE LIMITE DE MÉMOIRE, PAS SEULEMENT DE TEMPS [GDD 18] ------
  // « Limites de temps ET DE MÉMOIRE ». Une allocation en boucle doit être
  // arrêtée par le système, et se présenter au jeu comme un échec de mission.
  {
    ToolchainConfig borne = cfg;
    borne.memoire_max_mo = 64;
    borne.timeout_ms = 8000;
    const ResultatToolchain r = compiler_et_executer(
        "#include <ares/vol.hpp>\n"
        "#include <vector>\n"
        "void sequence_correction(ares::vol::Contexte& ctx) {\n"
        "  std::vector<char*> gard;\n"
        "  for (int i = 0; i < 100000; ++i) {\n"
        "    char* p = new char[1 << 20];\n"
        "    for (int k = 0; k < (1 << 20); k += 4096) p[k] = (char)i;\n"
        "    gard.push_back(p);\n"
        "  }\n"
        "  (void)ctx;\n"
        "}\n",
        e, borne);
    if (r.ok()) std::printf("  [memoire] le code a alloue 100 Go sans etre arrete\n");
    CHECK(!r.ok(), "isolation : une allocation sans fin est ARRETEE [GDD 18]");
    CHECK(r.issue == IssueCode::Plantage,
          "isolation : ... et se presente au jeu comme un ECHEC DE MISSION");
  }

  // ---- 6) LES QUATRE DÉCISIONS DU CONTEXTE REMONTENT ---------------------
  // `ares::vol::Contexte` ENREGISTRE ; le simulateur applique. On vérifie que le
  // canal porte les quatre, pas seulement la manœuvre.
  {
    const ResultatToolchain r = compiler_et_executer(
        "#include <ares/vol.hpp>\n"
        "using namespace ares::vol;\n"
        "void sequence_correction(Contexte& ctx) {\n"
        "  ctx.alerte(\"solution degradee\");\n"
        "  ctx.replanifier(heures(48));\n"
        "  ctx.differer(Manoeuvre(fen::Vec3{1,0,0}));\n"
        "  ctx.journal_bord(\"rien execute\");\n"
        "}\n",
        e, cfg);
    if (!r.ok())
      std::printf("  [decisions] issue=%s\n%s\n", issue_nom(r.issue), r.diagnostics.c_str());
    CHECK(r.ok(), "decisions : le code qui n execute rien s execute quand meme");
    CHECK(!r.decisions.execute, "decisions : ne rien executer est une decision");
    CHECK(r.decisions.differees == 1, "decisions : une manoeuvre DIFFEREE remonte");
    CHECK(r.decisions.replan_s == 48.0 * 3600.0, "decisions : la REPLANIFICATION remonte");
    CHECK(r.decisions.alertes.size() == 1 && r.decisions.alertes[0] == "solution degradee",
          "decisions : l ALERTE remonte, mot pour mot");
  }

  // ---- 7) DÉTERMINISME : mêmes entrées, mêmes décisions [GDD 18] ---------
  {
    const ResultatToolchain a = compiler_et_executer(squelette_vol(), e, cfg);
    CHECK(a.ok() && a.decisions.dv.x == r_ok.decisions.dv.x &&
          a.decisions.dv.y == r_ok.decisions.dv.y,
          "determinisme : memes entrees, memes decisions — une execution se rejoue");
  }

  std::printf("\nTOOLCHAIN : %d oracles OK, %d en echec.\n", g_ok, g_ko);
  return g_ko == 0 ? 0 : 1;
}
#endif
