// render/app/calc_eval.hpp
//
// EVALUATEUR D'EXPRESSIONS (mini-langage) pour les POSTES DE CALCUL du jeu.
// C'est le coeur du mode PRO : le joueur TAPE la formule (ex. "sqrt(mu/r)") ;
// le jeu l'evalue avec les VARIABLES DONNEES de l'etape, et compare le resultat a
// la bonne reponse calculee par astro_core. Aucun RNG, aucune dependance externe.
//
// Grammaire (descente recursive, precedence standard) :
//   expr  = term (('+'|'-') term)*
//   term  = factor (('*'|'/'|'%') factor)*
//   factor= unary
//   unary = ('+'|'-') unary | power
//   power = atom ('^' unary)?            (associatif a DROITE ; -3^2 = -9)
//   atom  = number | ident ['(' args ')'] | '(' expr ')'
//
// Fonctions : sqrt cbrt abs sign exp ln log log10 sin cos tan asin acos atan
//             deg rad  (1 arg) ; pow atan2 min max hypot (2 args).
// Constantes : pi, tau, e  (injectees dans l'environnement).
// Erreurs (ok=false + message clair, jamais d'exception ni de crash) : symbole
// inconnu, parenthese non fermee, mauvais nombre d'arguments, domaine invalide
// (sqrt<0, ln<=0), division par zero, caracteres en trop.
#pragma once
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>
#include <unordered_map>

namespace calc {

struct Env { std::unordered_map<std::string, double> vars; };
struct Result { bool ok{false}; double value{0.0}; std::string error; };

class Parser {
 public:
  Parser(const std::string& src, const Env& env) : s_(src), env_(env) {}

  Result run() {
    const double v = parse_expr();
    skip_ws();
    if (!err_ && i_ != s_.size()) fail("caractere inattendu");
    Result r; r.ok = !err_; r.value = err_ ? 0.0 : v; r.error = msg_;
    return r;
  }

 private:
  const std::string& s_;
  const Env&         env_;
  std::size_t        i_{0};
  bool               err_{false};
  std::string        msg_;

  void fail(const std::string& m) { if (!err_) { err_ = true; msg_ = m; } }
  void skip_ws() { while (i_ < s_.size() && std::isspace((unsigned char)s_[i_])) ++i_; }
  char peek() { skip_ws(); return i_ < s_.size() ? s_[i_] : '\0'; }
  bool eat(char c) { if (peek() == c) { ++i_; return true; } return false; }

  double parse_expr() {
    double v = parse_term();
    for (;;) {
      const char c = peek();
      if (c == '+') { ++i_; v += parse_term(); }
      else if (c == '-') { ++i_; v -= parse_term(); }
      else break;
      if (err_) return 0.0;
    }
    return v;
  }
  double parse_term() {
    double v = parse_unary();
    for (;;) {
      const char c = peek();
      if (c == '*') { ++i_; v *= parse_unary(); }
      else if (c == '/') { ++i_; const double d = parse_unary();
                           if (!err_ && d == 0.0) { fail("division par zero"); return 0.0; } v /= d; }
      else if (c == '%') { ++i_; const double d = parse_unary();
                           if (!err_ && d == 0.0) { fail("modulo par zero"); return 0.0; } v = std::fmod(v, d); }
      else break;
      if (err_) return 0.0;
    }
    return v;
  }
  double parse_unary() {
    const char c = peek();
    if (c == '-') { ++i_; return -parse_unary(); }
    if (c == '+') { ++i_; return  parse_unary(); }
    return parse_power();
  }
  double parse_power() {
    const double base = parse_atom();
    if (err_) return 0.0;
    if (peek() == '^') { ++i_; const double e = parse_unary();   // droite : a^b^c = a^(b^c)
      if (err_) return 0.0;
      const double r = std::pow(base, e);
      if (!std::isfinite(r)) { fail("puissance invalide"); return 0.0; }
      return r;
    }
    return base;
  }
  double parse_atom() {
    const char c = peek();
    if (c == '(') { ++i_; const double v = parse_expr();
      if (err_) return 0.0;
      if (!eat(')')) { fail("parenthese ) manquante"); return 0.0; }
      return v;
    }
    if (c == '.' || std::isdigit((unsigned char)c)) return parse_number();
    if (std::isalpha((unsigned char)c) || c == '_') return parse_ident();
    fail(c ? "symbole inattendu" : "expression incomplete");
    return 0.0;
  }
  double parse_number() {
    skip_ws();
    const char* start = s_.c_str() + i_;
    char* end = nullptr;
    const double v = std::strtod(start, &end);
    if (end == start) { fail("nombre invalide"); return 0.0; }
    i_ += static_cast<std::size_t>(end - start);
    return v;
  }
  double parse_ident() {
    skip_ws();
    const std::size_t b = i_;
    while (i_ < s_.size() && (std::isalnum((unsigned char)s_[i_]) || s_[i_] == '_')) ++i_;
    const std::string name = s_.substr(b, i_ - b);
    if (peek() == '(') {   // appel de fonction
      ++i_;
      double a0 = 0.0, a1 = 0.0; int n = 0;
      if (peek() != ')') {
        a0 = parse_expr(); n = 1;
        while (peek() == ',') { ++i_; a1 = parse_expr(); ++n; }
      }
      if (err_) return 0.0;
      if (!eat(')')) { fail("parenthese ) manquante apres " + name); return 0.0; }
      return call(name, a0, a1, n);
    }
    // variable / constante
    auto it = env_.vars.find(name);
    if (it != env_.vars.end()) return it->second;
    if (name == "pi")  return 3.14159265358979323846;
    if (name == "tau") return 6.28318530717958647692;
    if (name == "e")   return 2.71828182845904523536;
    fail("inconnu : " + name);
    return 0.0;
  }
  double call(const std::string& f, double a, double b, int n) {
    auto need = [&](int k) { if (n != k) { fail(f + " attend " + std::to_string(k) + " argument(s)"); return false; } return true; };
    // 1 argument
    if (f == "sqrt")  { if (!need(1)) return 0.0; if (a < 0) { fail("sqrt d'un negatif"); return 0.0; } return std::sqrt(a); }
    if (f == "cbrt")  { if (!need(1)) return 0.0; return std::cbrt(a); }
    if (f == "abs")   { if (!need(1)) return 0.0; return std::fabs(a); }
    if (f == "sign")  { if (!need(1)) return 0.0; return (a > 0) - (a < 0); }
    if (f == "exp")   { if (!need(1)) return 0.0; return std::exp(a); }
    if (f == "ln" || f == "log") { if (!need(1)) return 0.0; if (a <= 0) { fail(f + " d'un non-positif"); return 0.0; } return std::log(a); }
    if (f == "log10") { if (!need(1)) return 0.0; if (a <= 0) { fail("log10 d'un non-positif"); return 0.0; } return std::log10(a); }
    if (f == "sin")   { if (!need(1)) return 0.0; return std::sin(a); }
    if (f == "cos")   { if (!need(1)) return 0.0; return std::cos(a); }
    if (f == "tan")   { if (!need(1)) return 0.0; return std::tan(a); }
    if (f == "asin")  { if (!need(1)) return 0.0; if (a < -1 || a > 1) { fail("asin hors [-1,1]"); return 0.0; } return std::asin(a); }
    if (f == "acos")  { if (!need(1)) return 0.0; if (a < -1 || a > 1) { fail("acos hors [-1,1]"); return 0.0; } return std::acos(a); }
    if (f == "atan")  { if (!need(1)) return 0.0; return std::atan(a); }
    if (f == "deg")   { if (!need(1)) return 0.0; return a * (180.0 / 3.14159265358979323846); }
    if (f == "rad")   { if (!need(1)) return 0.0; return a * (3.14159265358979323846 / 180.0); }
    // 2 arguments
    if (f == "pow")   { if (!need(2)) return 0.0; const double r = std::pow(a, b); if (!std::isfinite(r)) { fail("pow invalide"); return 0.0; } return r; }
    if (f == "atan2") { if (!need(2)) return 0.0; return std::atan2(a, b); }
    if (f == "min")   { if (!need(2)) return 0.0; return a < b ? a : b; }
    if (f == "max")   { if (!need(2)) return 0.0; return a > b ? a : b; }
    if (f == "hypot") { if (!need(2)) return 0.0; return std::hypot(a, b); }
    fail("fonction inconnue : " + f);
    return 0.0;
  }
};

// Evalue `expr` avec les variables de `env`. Ne lance jamais : renvoie ok=false +
// message en cas d'erreur (symbole inconnu, syntaxe, domaine, division par zero).
inline Result eval(const std::string& expr, const Env& env) {
  if (expr.find_first_not_of(" \t\r\n") == std::string::npos) {
    Result r; r.ok = false; r.error = "expression vide"; return r;
  }
  Parser p(expr, env);
  return p.run();
}

}  // namespace calc
