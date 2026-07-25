// ui/lua_runner.cpp — DELEGATION aux binaires de verite.
// Le jeu ne recalcule rien : il appelle l'outil (m00_design / m00_play) et lit
// sa sortie. Un seul moteur, jamais deux copies de la physique.
#include <array>
#include <cstdio>
#include <string>

namespace fen {
std::string run_tool(const std::string& tool, const std::string& args) {
#if defined(_WIN32)
  auto POPEN=_popen; auto PCLOSE=_pclose; const std::string ext=".exe";
#else
  auto POPEN=popen; auto PCLOSE=pclose; const std::string ext="";
#endif
  std::string cmd = "." ; cmd += "/"; cmd += tool; cmd += ext;
  if(!args.empty()){ cmd += " "; cmd += args; }
  cmd += " 2>&1";
  std::string out; FILE* p=POPEN(cmd.c_str(),"r");
  if(!p) return "(impossible de lancer "+cmd+")";
  std::array<char,512> b;
  while(std::fgets(b.data(),(int)b.size(),p)) out+=b.data();
  PCLOSE(p);
  if(out.empty()) out="(pas de sortie — "+tool+ext+" doit etre a cote du jeu, avec le dossier missions/)";
  return out;
}
}
