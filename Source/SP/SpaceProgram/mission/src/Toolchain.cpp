// fen/code/Toolchain.cpp — la partie PLATEFORME de la toolchain [GDD 15.1, 18].
//
// Ce qui est ici, et uniquement ici : écrire des fichiers, lancer un
// compilateur, lancer un processus fils avec un DÉLAI, le tuer s'il dépasse,
// lire son code de sortie. Le reste (contrat, sérialisation, diagnostics) est en
// C++ pur dans l'en-tête, donc sous oracle.
#include "fen/code/Toolchain.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

namespace fen::code {
namespace {

bool ecrire(const std::string& chemin, const std::string& contenu) {
  std::ofstream f(chemin, std::ios::binary | std::ios::trunc);
  if (!f) return false;
  f.write(contenu.data(), static_cast<std::streamsize>(contenu.size()));
  return static_cast<bool>(f);
}

std::string lire(const std::string& chemin) {
  std::ifstream f(chemin, std::ios::binary);
  if (!f) return {};
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

// Lance une commande et attend au plus `timeout_ms`. Rend le code de sortie ;
// `depasse` dit si le délai a expiré (le processus est alors TUÉ).
// C'est le cœur de l'isolation [GDD 18] : une boucle infinie dans le code du
// joueur ne gèle pas le jeu, elle meurt au bout du délai.
//
// ═══ ON TUE UN ARBRE, PAS UN PROCESSUS ═══ (piège n°71)
// La première version lançait TOUT via `cmd.exe /c` et, au délai, appelait
// `TerminateProcess` sur ce cmd. Elle tuait donc le SHELL — pas le programme du
// joueur, qui restait à brûler un cœur indéfiniment. L'oracle du délai passait
// quand même (`depasse` était bien vrai), et le seul symptôme visible arrivait
// AU RUN SUIVANT : un `LNK1104` sur un binaire qu'un fuyard de la veille tenait
// encore ouvert. Un bac à sable qui laisse échapper ses détenus n'en est pas un.
//   . le code du joueur est lancé DIRECTEMENT, sans shell : on connaît son
//     chemin et ses arguments, le shell n'apportait qu'un intermédiaire à tuer ;
//   . et l'exécution est placée dans un JOB OBJECT tué en bloc. C'est aussi ce
//     qui donne enfin la LIMITE DE MÉMOIRE que [GDD 18] exige à côté de la
//     limite de temps : un `new` en boucle est arrêté par le système, pas espéré.
int executer_processus(const std::string& commande, int timeout_ms, bool& depasse,
                       bool via_shell, int memoire_max_mo) {
  depasse = false;
#ifdef _WIN32
  STARTUPINFOA si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  PROCESS_INFORMATION pi{};
  // `cmd /c` : quand la commande contient elle-même des guillemets (et ici il y
  // en a — des chemins), il FAUT encadrer le tout d'une paire supplémentaire.
  // Sans elle, cmd découpe au premier guillemet et ne lance rien : ni erreur, ni
  // journal, juste un silence (piège n°68). Réservé à la COMPILATION, qui a
  // besoin d'un script et d'une redirection ; le code du joueur, lui, n'y passe
  // plus.
  std::string cmd = via_shell ? ("cmd.exe /c \"" + commande + "\"") : commande;
  std::vector<char> buf(cmd.begin(), cmd.end());
  buf.push_back('\0');

  HANDLE job = CreateJobObjectA(nullptr, nullptr);
  if (job) {
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION li{};
    li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (memoire_max_mo > 0) {
      li.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_PROCESS_MEMORY;
      li.ProcessMemoryLimit = static_cast<SIZE_T>(memoire_max_mo) * 1024u * 1024u;
    }
    SetInformationJobObject(job, JobObjectExtendedLimitInformation, &li, sizeof li);
  }
  // SUSPENDU d'abord : le processus entre dans le job AVANT d'avoir pu créer
  // quoi que ce soit. Sans cela, un enfant né dans l'intervalle échapperait au
  // filet — c'est-à-dire exactement le fuyard qu'on vient de corriger.
  if (!CreateProcessA(nullptr, buf.data(), nullptr, nullptr, FALSE,
                      CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &si, &pi)) {
    if (job) CloseHandle(job);
    return -1;
  }
  if (job) AssignProcessToJobObject(job, pi.hProcess);
  ResumeThread(pi.hThread);

  const DWORD att = WaitForSingleObject(pi.hProcess, static_cast<DWORD>(timeout_ms));
  DWORD code = 0;
  if (att == WAIT_TIMEOUT) {
    depasse = true;
    if (job) TerminateJobObject(job, 1);
    else     TerminateProcess(pi.hProcess, 1);
    WaitForSingleObject(pi.hProcess, 2000);
    code = 1;
  } else {
    GetExitCodeProcess(pi.hProcess, &code);
  }
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  // Fermer le job tue ce qui y resterait : c'est la garantie, pas l'intention.
  if (job) CloseHandle(job);
  return static_cast<int>(code);
#else
  (void)timeout_ms; (void)via_shell; (void)memoire_max_mo;
  return std::system(commande.c_str());
#endif
}

} // namespace

// ---------------------------------------------------------------------------
// SÉRIALISATION — clé=valeur, une par ligne. Texte volontairement : ces fichiers
// SONT la journalisation des exécutions [GDD 18], et un journal qu'on ne peut
// pas lire à l'œil ne sert à personne quand une mission a mal tourné.
// ---------------------------------------------------------------------------
std::string EntreesVol::serialiser() const {
  char b[512];
  std::snprintf(b, sizeof b,
                "pos=%.17g,%.17g,%.17g\nvel=%.17g,%.17g,%.17g\nsigma3=%.17g\n"
                "cible=%.17g,%.17g,%.17g\ntol=%.17g\ndv=%.17g\ntau=%.17g\n",
                pos.x, pos.y, pos.z, vel.x, vel.y, vel.z, sigma3_m,
                cible.x, cible.y, cible.z, tolerance_m, dv_disponible, tau_s);
  return b;
}

EntreesVol EntreesVol::lire(const std::string& texte) {
  EntreesVol e;
  std::istringstream in(texte);
  std::string ligne;
  auto vec3 = [](const std::string& s, Vec3& v) {
    std::sscanf(s.c_str(), "%lf,%lf,%lf", &v.x, &v.y, &v.z);
  };
  while (std::getline(in, ligne)) {
    const auto eq = ligne.find('=');
    if (eq == std::string::npos) continue;
    const std::string k = ligne.substr(0, eq), val = ligne.substr(eq + 1);
    if (k == "pos") vec3(val, e.pos);
    else if (k == "vel") vec3(val, e.vel);
    else if (k == "cible") vec3(val, e.cible);
    else if (k == "sigma3") e.sigma3_m = std::atof(val.c_str());
    else if (k == "tol") e.tolerance_m = std::atof(val.c_str());
    else if (k == "dv") e.dv_disponible = std::atof(val.c_str());
    else if (k == "tau") e.tau_s = std::atof(val.c_str());
  }
  return e;
}

std::string DecisionsVol::serialiser() const {
  std::ostringstream o;
  o.precision(17);
  o << "execute=" << (execute ? 1 : 0) << "\n";
  o << "dv=" << dv.x << "," << dv.y << "," << dv.z << "\n";
  o << "differees=" << differees << "\n";
  o << "replan=" << replan_s << "\n";
  for (const auto& a : alertes) o << "alerte=" << a << "\n";
  for (const auto& j : journal) o << "journal=" << j << "\n";
  return o.str();
}

DecisionsVol DecisionsVol::lire(const std::string& texte) {
  DecisionsVol d;
  std::istringstream in(texte);
  std::string ligne;
  while (std::getline(in, ligne)) {
    if (!ligne.empty() && ligne.back() == '\r') ligne.pop_back();
    const auto eq = ligne.find('=');
    if (eq == std::string::npos) continue;
    const std::string k = ligne.substr(0, eq), v = ligne.substr(eq + 1);
    if (k == "execute") d.execute = (std::atoi(v.c_str()) != 0);
    else if (k == "dv") std::sscanf(v.c_str(), "%lf,%lf,%lf", &d.dv.x, &d.dv.y, &d.dv.z);
    else if (k == "differees") d.differees = std::atoi(v.c_str());
    else if (k == "replan") d.replan_s = std::atof(v.c_str());
    else if (k == "alerte") d.alertes.push_back(v);
    else if (k == "journal") d.journal.push_back(v);
  }
  return d;
}

// ---------------------------------------------------------------------------
// LE HARNAIS — ce que la toolchain ajoute autour du code du joueur. Il n'appelle
// QUE `sequence_correction`, la fonction que [GDD 15.3] met en exemple : le
// joueur écrit une fonction, pas un programme.
// ---------------------------------------------------------------------------
std::string harnais_source() {
  return
"#include <ares/vol.hpp>\n"
"#include <cstdio>\n"
"#include <cstdlib>\n"
"#include <fstream>\n"
"#include <sstream>\n"
"#include <string>\n"
"void sequence_correction(ares::vol::Contexte&);\n"
"int main(int argc, char** argv) {\n"
"  if (argc < 3) return 2;\n"
"  double px=0,py=0,pz=0,vx=0,vy=0,vz=0,s3=0,cx=0,cy=0,cz=0,tol=0,dv=0,tau=86400;\n"
"  { std::ifstream f(argv[1]); std::string l;\n"
"    while (std::getline(f,l)) { size_t q=l.find('='); if(q==std::string::npos) continue;\n"
"      std::string k=l.substr(0,q), v=l.substr(q+1);\n"
"      if(k==\"pos\") std::sscanf(v.c_str(),\"%lf,%lf,%lf\",&px,&py,&pz);\n"
"      else if(k==\"vel\") std::sscanf(v.c_str(),\"%lf,%lf,%lf\",&vx,&vy,&vz);\n"
"      else if(k==\"cible\") std::sscanf(v.c_str(),\"%lf,%lf,%lf\",&cx,&cy,&cz);\n"
"      else if(k==\"sigma3\") s3=std::atof(v.c_str());\n"
"      else if(k==\"tol\") tol=std::atof(v.c_str());\n"
"      else if(k==\"dv\") dv=std::atof(v.c_str());\n"
"      else if(k==\"tau\") tau=std::atof(v.c_str()); } }\n"
"  using namespace ares::vol;\n"
"  Contexte ctx(Etat(fen::Vec3{px,py,pz}, fen::Vec3{vx,vy,vz}, s3),\n"
"               Cible(fen::Vec3{cx,cy,cz}, tol), Reserves(dv), Solveur(tau));\n"
"  sequence_correction(ctx);\n"
"  std::ostringstream o; o.precision(17);\n"
"  fen::Vec3 d{0,0,0};\n"
"  if (!ctx.executees().empty()) d = ctx.executees().back().vecteur();\n"
"  o << \"execute=\" << (ctx.executees().empty()?0:1) << \"\\n\";\n"
"  o << \"dv=\" << d.x << \",\" << d.y << \",\" << d.z << \"\\n\";\n"
"  o << \"differees=\" << (int)ctx.differees().size() << \"\\n\";\n"
"  o << \"replan=\" << (ctx.replans().empty()?0.0:ctx.replans().back()) << \"\\n\";\n"
"  for (const auto& a : ctx.alertes()) o << \"alerte=\" << a << \"\\n\";\n"
"  for (const auto& j : ctx.journal()) o << \"journal=\" << j << \"\\n\";\n"
"  std::ofstream r(argv[2]); r << o.str();\n"
"  return 0;\n"
"}\n";
}

// ---------------------------------------------------------------------------
ResultatToolchain compiler_et_executer(const std::string& source_joueur,
                                       const EntreesVol& entrees,
                                       const ToolchainConfig& cfg) {
  ResultatToolchain res;
  const std::string d = cfg.dossier_travail;
  const std::string f_src = d + "/vol_joueur.cpp";
  const std::string f_harn = d + "/vol_harnais.cpp";
  // UN ARTEFACT QUI DOIT ÊTRE REMPLACÉ DOIT ÊTRE NEUF, pas écrasé. Windows garde
  // un binaire verrouillé un instant après la fin du processus qui l'exécutait :
  // le lieur échouait alors sur `LNK1104 impossible d'ouvrir le fichier`, et
  // comme aucun diagnostic « erreur C » n'accompagnait ce message, la chaîne
  // concluait « compilateur absent » (piège n°70). Un nom unique par compilation
  // supprime la question au lieu de mieux la contourner.
  static int s_compilation = 0;
  const std::string f_exe =
      d + "/vol_joueur_" + std::to_string(++s_compilation) + ".exe";
  const std::string f_log = d + "/vol_compil.log";
  const std::string f_in  = d + "/vol_entrees.txt";
  const std::string f_out = d + "/vol_decisions.txt";
  const std::string f_bat = d + "/vol_compil.bat";

  if (!ecrire(f_src, source_joueur) || !ecrire(f_harn, harnais_source()) ||
      !ecrire(f_in, entrees.serialiser())) {
    res.issue = IssueCode::Indisponible;
    res.diagnostics = "dossier de travail inaccessible";
    return res;
  }
  std::remove(f_out.c_str());
  std::remove(f_exe.c_str());

  // ---- COMPILATION ---------------------------------------------------------
  // Coût NUL et instantané [GDD 15.5 étape 1] : ce qui ne compile pas ne coûte
  // ni temps de banc ni budget. Les diagnostics partent tels quels au joueur.
  std::ostringstream bat;
  bat << "@echo off\n";
  if (!cfg.vcvars.empty()) bat << "call \"" << cfg.vcvars << "\" >nul\n";
  bat << "cl /nologo /std:c++20 /EHsc /fp:precise";
  for (const auto& inc : cfg.includes) bat << " /I \"" << inc << "\"";
  bat << " /Fo:\"" << d << "\\\\\" /Fe:\"" << f_exe << "\" \""
      << f_src << "\" \"" << f_harn << "\"";
  for (const auto& s : cfg.sources) bat << " \"" << s << "\"";
  bat << "\n";
  if (!ecrire(f_bat, bat.str())) {
    res.issue = IssueCode::Indisponible;
    return res;
  }
  bool depasse = false;
  const int code_c = executer_processus("\"" + f_bat + "\" > \"" + f_log + "\" 2>&1",
                                        60000, depasse, /*via_shell*/ true,
                                        /*memoire_max_mo*/ 0);
  res.diagnostics = lire(f_log);
  {
    std::ifstream test(f_exe, std::ios::binary);
    if (code_c != 0 || !test) {
      // Un compilateur ABSENT et un code FAUX ne sont pas la même faute : le
      // premier est un défaut d'installation, le second est le jeu.
      // On tranche sur la PRESENCE D'UN DIAGNOSTIC DE COMPILATION (« error C… »
      // ou « erreur C… », le code MSVC est le meme dans les deux langues), pas
      // sur une phrase du shell : le script d'environnement du compilateur
      // imprime lui-meme des « n'est pas reconnu » sans rapport, et les prendre
      // pour une absence de compilateur faisait passer un code FAUX pour une
      // machine mal installee (piege n°69).
      const bool a_diag = res.diagnostics.find("error C") != std::string::npos ||
                          res.diagnostics.find("erreur C") != std::string::npos;
      res.issue = a_diag ? IssueCode::ErreurCompilation : IssueCode::Indisponible;
      return res;
    }
  }

  // ---- EXÉCUTION EN BAC À SABLE -------------------------------------------
  // Processus SÉPARÉ, délai borné. Ce qui meurt ici meurt seul.
  bool delai = false;
  const int code_x = executer_processus(
      "\"" + f_exe + "\" \"" + f_in + "\" \"" + f_out + "\"", cfg.timeout_ms, delai,
      /*via_shell*/ false, cfg.memoire_max_mo);
  res.code_sortie = code_x;
  if (delai) { res.issue = IssueCode::Delai; return res; }
  if (code_x != 0) { res.issue = IssueCode::Plantage; return res; }

  const std::string sortie = lire(f_out);
  if (sortie.empty()) { res.issue = IssueCode::Plantage; return res; }
  res.decisions = DecisionsVol::lire(sortie);
  res.issue = IssueCode::Ok;
  return res;
}

} // namespace fen::code
