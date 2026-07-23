// fen/vehicle/Vehicle.hpp
// Le véhicule est un ARBRE DE MASSE, pas un inventaire de pièces.
// Aucune "barre de carburant" : il y a des kilogrammes d'ergols, une masse
// sèche, un rapport de mélange, des résidus imbrûlés. Le Delta-v disponible
// n'est PAS un attribut du vaisseau : c'est une fonction de l'état courant,
// recalculée par Tsiolkovski à chaque instant.
#pragma once
#include <string>
#include <vector>
#include <cmath>
#include "fen/core/Constants.hpp"

namespace fen::vehicle {

struct Engine {
  std::string id;
  double thrust_vac{};     // N
  double isp_vac{};        // s
  double mass{};           // kg (moteur seul)
  double mixture_ratio{};  // O/F massique
  double min_burn{0.5};    // s (temps d'allumage minimal utile)
  int    max_restarts{-1}; // -1 = illimité
  double unit_cost_musd{0.0};
  double heritage{0.0};    // 0..1, alimente le modèle de fiabilité (V1)

  double mdot() const { return thrust_vac / (isp_vac * cst::G0); }
  double ve()   const { return isp_vac * cst::G0; }
};

struct Tank {
  double propellant_mass{};   // kg (chargés)
  double residual_fraction{0.02}; // imbrûlés + ullage non consommables (1-3 % réel)
  double dry_fraction{};      // masse sèche du réservoir / masse d'ergols
  double propellant_density{}; // kg/m^3 (moyenne pondérée par le MR)

  double usable() const { return propellant_mass * (1.0 - residual_fraction); }
  double dry_mass() const { return propellant_mass * dry_fraction; }
  double volume() const { return propellant_mass / propellant_density; }
};

struct Stage {
  std::string id;
  Engine engine;
  Tank   tank;
  double structure_mass{};   // avionics, structure, RCS sec, etc. [kg]

  double dry_mass() const {
    return engine.mass + tank.dry_mass() + structure_mass
           + tank.propellant_mass * tank.residual_fraction;
  }
  double wet_mass() const { return dry_mass() + tank.usable(); }
};

struct Vehicle {
  std::string id;
  std::vector<Stage> stages;     // [0] = allumé en premier
  double payload_dry{};          // kg — la charge utile CONTRACTUELLE

  double total_mass() const {
    double m = payload_dry;
    for (const auto& s : stages) m += s.wet_mass();
    return m;
  }
  // Masse au-dessus de l'étage k (charge utile effective de cet étage)
  double mass_above(std::size_t k) const {
    double m = payload_dry;
    for (std::size_t i = k + 1; i < stages.size(); ++i) m += stages[i].wet_mass();
    return m;
  }
  // Delta-v idéal de l'étage k (impulsionnel, sans pertes). BORNE SUPÉRIEURE.
  double stage_dv(std::size_t k) const {
    const Stage& s = stages[k];
    const double m0 = mass_above(k) + s.wet_mass();
    const double mf = mass_above(k) + s.dry_mass();
    return s.engine.ve() * std::log(m0 / mf);
  }
  double total_dv() const {
    double dv = 0.0;
    for (std::size_t k = 0; k < stages.size(); ++k) dv += stage_dv(k);
    return dv;
  }
};

// --- dimensionnement inverse -------------------------------------------------
// Étant donné un Delta-v CIBLE et une masse à emporter, quelle masse d'ergols ?
// Attention : c'est un POINT FIXE (les réservoirs pèsent proportionnellement aux
// ergols). Le jeu ne le résout PAS à la place du joueur dans l'UI — mais l'API
// est disponible pour ses scripts, parce que c'est une itération mécanique,
// pas une décision de conception.
struct SizingResult {
  double propellant{};
  double stage_dry{};
  double m0{};
  bool converged{false};
  int iterations{0};
};

inline SizingResult size_stage_for_dv(double dv_target, double mass_above,
                                      const Engine& eng, double tank_dry_fraction,
                                      double structure_mass, double residual_fraction = 0.02,
                                      int max_iter = 200, double tol = 1e-9) {
  SizingResult r;
  const double ve = eng.ve();
  double mp = 1000.0; // amorçage
  for (int i = 0; i < max_iter; ++i) {
    const double dry = eng.mass + structure_mass + tank_dry_fraction * mp
                       + residual_fraction * mp;
    const double mf = mass_above + dry;
    const double m0 = mf * std::exp(dv_target / ve);
    const double mp_new = m0 - mf;   // ergols UTILISABLES
    // mp est la masse CHARGÉE : utilisable = mp*(1 - residual)
    const double mp_loaded = mp_new / (1.0 - residual_fraction);
    if (std::fabs(mp_loaded - mp) < tol * std::max(1.0, mp)) {
      r.propellant = mp_loaded; r.stage_dry = dry; r.m0 = m0;
      r.converged = true; r.iterations = i; return r;
    }
    mp = mp_loaded;
    r.iterations = i;
  }
  r.propellant = mp;
  return r;
}

// --- dimensionnement inverse MULTI-ETAGES ------------------------------------
// Le joueur CHOISIT le partage du Delta-v entre etages (une decision de
// conception : gros etage lent en bas, petit etage vif en haut...). Ceci n'est
// que l'iteration mecanique qui en decoule. On dimensionne du HAUT vers le BAS :
// la charge utile d'un etage inferieur est la masse totale de tout ce qui le
// surmonte (m0 de l'etage du dessus). C'est ce qui rend une mission a 8 km/s
// faisable alors qu'un seul etage chimique plafonne vers ~ve*ln(1/frac_seche).
struct StageSpec {
  double dv_target{};                 // m/s attribue a cet etage (choix du joueur)
  Engine engine{};
  double tank_dry_fraction{};
  double structure_mass{150.0};
  double residual_fraction{0.02};
};
struct MultiSizingResult {
  std::vector<SizingResult> stages;   // meme ordre que l'entree (bas index 0 -> haut)
  double m0{};                        // masse au decollage (etage du bas)
  bool converged{false};
};
// `specs` : du BAS (allume en premier, index 0) vers le HAUT, comme Vehicle::stages.
inline MultiSizingResult size_multistage_for_dv(const std::vector<StageSpec>& specs,
                                                double payload) {
  MultiSizingResult out;
  out.stages.resize(specs.size());
  out.converged = !specs.empty();
  double mass_above = payload;
  for (std::size_t k = specs.size(); k-- > 0; ) {   // du HAUT vers le BAS
    const StageSpec& s = specs[k];
    SizingResult r = size_stage_for_dv(s.dv_target, mass_above, s.engine,
                                       s.tank_dry_fraction, s.structure_mass,
                                       s.residual_fraction);
    out.stages[k] = r;
    if (!r.converged || r.m0 <= mass_above) out.converged = false;
    mass_above = r.m0;                 // cet etage + le dessus = charge utile du dessous
  }
  out.m0 = mass_above;
  return out;
}

} // namespace fen::vehicle
