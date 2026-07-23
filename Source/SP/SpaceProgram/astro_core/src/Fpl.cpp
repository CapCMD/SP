#include "fen/io/Fpl.hpp"
#include "fen/astro/Elements.hpp"
#include "fen/core/Constants.hpp"
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>

namespace fen::io {
using namespace fen::cst;

namespace {

struct UnitDef { const char* tok; const char* dim; double si; };

// Table des unités reconnues. AJOUTER une unité ici est un acte volontaire.
const UnitDef kUnits[] = {
    {"m",    "L", 1.0},        {"km",   "L", 1000.0},
    {"s",    "T", 1.0},        {"min",  "T", 60.0},   {"h", "T", 3600.0},
    {"d",    "T", 86400.0},    {"day",  "T", 86400.0},
    {"kg",   "M", 1.0},        {"t",    "M", 1000.0},
    {"m/s",  "V", 1.0},        {"km/s", "V", 1000.0},
    {"N",    "F", 1.0},        {"kN",   "F", 1000.0},
    {"deg",  "A", DEG},        {"rad",  "A", 1.0},
    {"-",    "1", 1.0},        // sans dimension : le token "-" est OBLIGATOIRE aussi
};

std::string trim(const std::string& s) {
  const auto b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) return "";
  const auto e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}

[[noreturn]] void fail(int line, const std::string& msg) {
  throw std::runtime_error("fpl:" + std::to_string(line) + " : " + msg);
}

} // namespace

double parse_quantity(const std::string& tok, const char* expected_dim, int line) {
  // Sépare le nombre du suffixe d'unité.
  // Subtilité volontaire : '-' est AUSSI le token de l'adimensionnel. Le signe
  // n'est donc accepté qu'en tête, ou juste après un exposant. Sans ça,
  // "5.88-" serait lu comme le nombre "5.88-" au lieu de "5.88" + unité "-".
  std::size_t i = 0;
  while (i < tok.size()) {
    const char c = tok[i];
    const bool is_digit = std::isdigit(static_cast<unsigned char>(c)) != 0;
    const bool is_sign_ok = (c == '-' || c == '+') &&
                            (i == 0 || tok[i - 1] == 'e' || tok[i - 1] == 'E');
    const bool is_exp_ok = (c == 'e' || c == 'E') && i > 0 &&
                           (i + 1 < tok.size()) &&
                           (std::isdigit(static_cast<unsigned char>(tok[i + 1])) ||
                            tok[i + 1] == '-' || tok[i + 1] == '+');
    if (is_digit || c == '.' || is_sign_ok || is_exp_ok) ++i;
    else break;
  }
  if (i == 0) fail(line, "'" + tok + "' : nombre attendu");
  const std::string num = tok.substr(0, i);
  const std::string unit = trim(tok.substr(i));

  if (unit.empty())
    fail(line, "'" + tok + "' : UNITE MANQUANTE. Tout nombre dimensionnel porte "
               "son unite (ex: 6578.137km). Un nombre sans dimension porte le token '-'.");

  for (const auto& u : kUnits) {
    if (unit == u.tok) {
      if (std::strcmp(u.dim, expected_dim) != 0)
        fail(line, "'" + tok + "' : unite de dimension '" + u.dim +
                   "' la ou '" + expected_dim + "' est attendu.");
      return std::strtod(num.c_str(), nullptr) * u.si;
    }
  }
  fail(line, "'" + unit + "' : unite inconnue.");
}

Vec3 parse_vector(const std::string& tok, const char* expected_dim, int line) {
  const auto lb = tok.find('[');
  const auto rb = tok.find(']');
  if (lb == std::string::npos || rb == std::string::npos || rb < lb)
    fail(line, "'" + tok + "' : vecteur attendu, forme [a,b,c]unite");
  const std::string inner = tok.substr(lb + 1, rb - lb - 1);
  const std::string unit = trim(tok.substr(rb + 1));
  if (unit.empty()) fail(line, "'" + tok + "' : UNITE MANQUANTE sur le vecteur.");

  double comp[3] = {0, 0, 0};
  std::stringstream ss(inner);
  std::string item;
  int n = 0;
  while (std::getline(ss, item, ',') && n < 3)
    comp[n++] = parse_quantity(trim(item) + unit, expected_dim, line);
  if (n != 3) fail(line, "'" + tok + "' : 3 composantes attendues.");
  return Vec3{comp[0], comp[1], comp[2]};
}

namespace {

std::map<std::string, std::string> kv(const std::vector<std::string>& toks, int from) {
  std::map<std::string, std::string> m;
  for (std::size_t i = from; i < toks.size(); ++i) {
    const auto eq = toks[i].find('=');
    if (eq == std::string::npos) continue;
    m[toks[i].substr(0, eq)] = toks[i].substr(eq + 1);
  }
  return m;
}

std::string need(const std::map<std::string, std::string>& m, const char* k, int line) {
  auto it = m.find(k);
  if (it == m.end()) fail(line, std::string("cle manquante : ") + k);
  return it->second;
}

ephem::Body body_from_name(const std::string& s, int line) {
  if (s == "SUN") return ephem::Body::Sun;
  if (s == "EARTH") return ephem::Body::EarthBary;
  if (s == "MOON") return ephem::Body::Moon;
  if (s == "MARS") return ephem::Body::Mars;
  if (s == "VENUS") return ephem::Body::Venus;
  if (s == "JUPITER") return ephem::Body::Jupiter;
  if (s == "SATURN") return ephem::Body::Saturn;
  if (s == "TITAN") return ephem::Body::Titan;
  fail(line, "corps inconnu : " + s);
}

} // namespace

FplDocument parse_fpl(const std::string& path) {
  std::ifstream in(path);
  if (!in) throw std::runtime_error("impossible d'ouvrir " + path);

  FplDocument doc;
  auto& plan = doc.plan;
  plan.center = ephem::Body::EarthBary;

  std::map<std::string, vehicle::Engine> engines;
  astro::Elements el;
  bool have_elements = false, have_epoch = false;
  double t_stop_rel = -1.0;
  std::vector<std::pair<flight::BurnCmd, double>> burns_rel; // (cmd, t relatif)

  std::string line_s;
  int line = 0;
  while (std::getline(in, line_s)) {
    ++line;
    // continuation de ligne par '\\' en fin de ligne
    while (!line_s.empty() && trim(line_s).back() == '\\') {
      std::string nxt;
      if (!std::getline(in, nxt)) break;
      ++line;
      line_s = trim(line_s);
      line_s.pop_back();
      line_s += " " + nxt;
    }
    const auto hash = line_s.find('#');
    if (hash != std::string::npos) line_s = line_s.substr(0, hash);
    line_s = trim(line_s);
    if (line_s.empty()) continue;

    std::vector<std::string> toks;
    { std::stringstream ss(line_s); std::string t; while (ss >> t) toks.push_back(t); }
    const std::string key = toks[0];

    if (key == "MISSION") {
      plan.mission_id = toks.at(1);

    } else if (key == "CENTER") {
      plan.center = body_from_name(toks.at(1), line);

    } else if (key == "PERTURBERS") {
      for (std::size_t i = 1; i < toks.size(); ++i)
        plan.perturbers.push_back(body_from_name(toks[i], line));

    } else if (key == "EPOCH") {
      plan.epoch0 = epoch_from_iso(toks.at(1)).tdb;
      have_epoch = true;

    } else if (key == "ENGINE") {
      auto m = kv(toks, 1);
      vehicle::Engine e;
      e.id = need(m, "id", line);
      e.thrust_vac = parse_quantity(need(m, "thrust", line), "F", line);
      e.isp_vac    = parse_quantity(need(m, "isp", line), "T", line);
      e.mass       = parse_quantity(need(m, "mass", line), "M", line);
      if (m.count("mr")) e.mixture_ratio = parse_quantity(m["mr"], "1", line);
      if (m.count("cost")) e.unit_cost_musd = parse_quantity(m["cost"], "1", line);
      engines[e.id] = e;

    } else if (key == "STAGE") {
      auto m = kv(toks, 1);
      vehicle::Stage s;
      s.id = need(m, "id", line);
      const std::string eid = need(m, "engine", line);
      if (!engines.count(eid)) fail(line, "moteur inconnu : " + eid);
      s.engine = engines[eid];
      s.tank.propellant_mass    = parse_quantity(need(m, "propellant", line), "M", line);
      s.tank.dry_fraction       = parse_quantity(need(m, "tank_dry_frac", line), "1", line);
      s.tank.residual_fraction  = m.count("residual")
                                  ? parse_quantity(m["residual"], "1", line) : 0.02;
      s.tank.propellant_density = m.count("prop_density")
                                  ? parse_quantity(m["prop_density"], "1", line) : 1000.0;
      s.structure_mass          = parse_quantity(need(m, "structure", line), "M", line);
      plan.vehicle.stages.push_back(s);

    } else if (key == "PAYLOAD") {
      plan.vehicle.payload_dry = parse_quantity(toks.at(1), "M", line);

    } else if (key == "ELEMENTS") {
      auto m = kv(toks, 1);
      el.a    = parse_quantity(need(m, "sma", line), "L", line);
      el.e    = parse_quantity(need(m, "ecc", line), "1", line);
      el.i    = parse_quantity(need(m, "inc", line), "A", line);
      el.raan = parse_quantity(need(m, "raan", line), "A", line);
      el.argp = parse_quantity(need(m, "argp", line), "A", line);
      el.nu   = parse_quantity(need(m, "ta", line), "A", line);
      have_elements = true;

    } else if (key == "GATES") {
      auto m = kv(toks, 1);
      plan.gates.sigma_mag_fixed   = parse_quantity(need(m, "mag_fixed", line), "V", line);
      plan.gates.sigma_mag_prop    = parse_quantity(need(m, "mag_prop", line), "1", line);
      plan.gates.sigma_point_fixed = parse_quantity(need(m, "point_fixed", line), "V", line);
      plan.gates.sigma_point_prop  = parse_quantity(need(m, "point_prop", line), "1", line);

    } else if (key == "BURN") {
      auto m = kv(toks, 1);
      flight::BurnCmd b;
      b.id = need(m, "id", line);
      const double t_rel = parse_quantity(need(m, "t", line), "T", line);
      b.dv = parse_vector(need(m, "dv", line), "V", line);
      const std::string fr = m.count("frame") ? m["frame"] : "RSW";
      b.frame = (fr == "RSW") ? flight::DvFrame::RSW : flight::DvFrame::Inertial;
      const std::string hd = m.count("hold") ? m["hold"] : "INERTIAL";
      b.hold = (hd == "RSW") ? force::ThrustFrame::RswFixed : force::ThrustFrame::InertialFixed;
      // Un INDEX d'etage n'est pas une grandeur physique : pas de token d'unite.
      // La regle "toute grandeur porte son unite" s'applique aux grandeurs, pas
      // aux identifiants. Confondre les deux serait du formalisme, pas de la rigueur.
      b.stage = m.count("stage") ? static_cast<std::size_t>(std::stoul(m["stage"])) : 0;
      burns_rel.emplace_back(b, t_rel);

    } else if (key == "STOP") {
      auto m = kv(toks, 1);
      t_stop_rel = parse_quantity(need(m, "t", line), "T", line);

    } else if (key == "GOAL") {
      auto m = kv(toks, 1);
      Goal g;
      for (const auto& [k, v] : m) {
        if (k == "tol") continue;
        g.key = k;
        if      (k == "sma" || k == "rp" || k == "ra") g.target = parse_quantity(v, "L", line);
        else if (k == "inc")                          g.target = parse_quantity(v, "A", line);
        else if (k == "payload")                    { g.target = parse_quantity(v, "M", line);
                                                      g.is_min = true; }
        else                                          g.target = parse_quantity(v, "1", line);
      }
      if (m.count("tol")) {
        if      (g.key == "sma" || g.key == "rp" || g.key == "ra")
          g.tol = parse_quantity(m["tol"], "L", line);
        else if (g.key == "inc") g.tol = parse_quantity(m["tol"], "A", line);
        else                     g.tol = parse_quantity(m["tol"], "1", line);
      }
      doc.goals.push_back(g);

    } else if (key == "BUDGET") {
      doc.budget_musd = parse_quantity(toks.at(1), "1", line);
    } else if (key == "DEADLINE") {
      doc.deadline_days = parse_quantity(toks.at(1), "T", line) / DAY;
    } else {
      doc.warnings.push_back("ligne " + std::to_string(line) + " : mot-cle ignore '" + key + "'");
    }
  }

  if (!have_epoch)    throw std::runtime_error("fpl : EPOCH manquant");
  if (!have_elements) throw std::runtime_error("fpl : ELEMENTS manquant");
  if (plan.vehicle.stages.empty()) throw std::runtime_error("fpl : aucun STAGE");

  // état initial
  Vec3 r, v;
  astro::elements_to_rv(el, ephem::body_mu(plan.center), r, v);
  plan.initial.r = r;
  plan.initial.v = v;
  plan.initial.m = plan.vehicle.total_mass();

  for (auto& [b, t_rel] : burns_rel) { b.t = plan.epoch0 + t_rel; plan.burns.push_back(b); }
  plan.t_stop = plan.epoch0 + (t_stop_rel > 0 ? t_stop_rel : 0.0);

  return doc;
}

} // namespace fen::io
