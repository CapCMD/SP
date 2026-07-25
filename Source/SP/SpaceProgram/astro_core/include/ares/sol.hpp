// ares/sol.hpp — API SOL : ANALYSE ET CONCEPTION [GDD 15.2, v1.2]
//
// Espace de nom `ares::sol`. LECTURE SEULE, AUCUNE CONSÉQUENCE, recompilable et
// réexécutable sans coût. C'est l'équivalent des outils d'analyse de mission
// d'une agence réelle — et c'est CE QUE LE CODE DU JOUEUR APPELLE (mode Pro en
// C++, mode Normal via les nœuds, « les deux modes attaquent les mêmes API »).
//
// Ce fichier est la FAÇADE : il n'ajoute aucune physique, il ré-expose
// astro_core sous les noms de l'API du GDD (ephemeride, lambert, budget de
// masse, journal). Le calcul reste celui du cœur, sous oracle.
//
// L'exemple du GDD 15.2 s'exprime presque littéralement contre cette API (voir
// tests/test_api_sol.cpp).
#pragma once
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

#include "fen/astro/Lambert.hpp"
#include "fen/core/Constants.hpp"
#include "fen/core/Epoch.hpp"
#include "fen/core/Vec3.hpp"
#include "fen/ephem/Ephemeris.hpp"
#include "fen/vehicle/PartsCatalog.hpp"
#include "fen/vehicle/Vehicle.hpp"

namespace ares::sol {

using fen::Vec3;
using fen::Epoch;

// --- HELPERS D'UNITÉS (les mêmes noms que le GDD) ---------------------------
inline Epoch  date(const std::string& iso) { return fen::epoch_from_iso(iso); }
inline double jours(double n)   { return n * fen::cst::DAY; }
inline double heures(double n)  { return n * 3600.0; }
inline double minutes(double n) { return n * 60.0; }
inline double km(double n)      { return n * 1000.0; }
inline double metres(double n)  { return n; }

// --- LE JOURNAL D'ANALYSE ---------------------------------------------------
// `journal(...)` écrit dans un tampon consultable (aucune conséquence). Le sink
// est thread_local pour rester déterministe en Monte-Carlo parallèle.
inline std::vector<std::string>& journal_lines() {
  static thread_local std::vector<std::string> lines;
  return lines;
}
inline void journal_clear() { journal_lines().clear(); }
inline void journal(const char* fmt, ...) {
  char buf[512];
  va_list ap; va_start(ap, fmt);
  std::vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  journal_lines().emplace_back(buf);
}

// --- CORPS (état d'un corps à une époque) -----------------------------------
class Corps {
 public:
  Corps() = default;
  Corps(std::string nom, Vec3 pos, Vec3 vel)
      : nom_(std::move(nom)), pos_(pos), vel_(vel) {}
  const std::string& nom() const { return nom_; }
  Vec3 position() const { return pos_; }
  Vec3 vitesse() const  { return vel_; }
 private:
  std::string nom_;
  Vec3 pos_{}, vel_{};
};

// Correspondance nom -> corps de l'éphéméride. Insensible à la casse simple.
inline fen::ephem::Body corps_from_name(const std::string& nom) {
  using B = fen::ephem::Body;
  if (nom == "TERRE" || nom == "EARTH") return B::EarthBary;
  if (nom == "MARS")    return B::Mars;
  if (nom == "VENUS")   return B::Venus;
  if (nom == "MERCURE" || nom == "MERCURY") return B::Mercury;
  if (nom == "JUPITER") return B::Jupiter;
  if (nom == "SATURNE" || nom == "SATURN")  return B::Saturn;
  if (nom == "LUNE" || nom == "MOON") return B::Moon;
  if (nom == "SOLEIL" || nom == "SUN") return B::Sun;
  return B::EarthBary;
}

// L'éphéméride Standish est sans état : une instance partagée suffit.
inline const fen::ephem::StandishEphemeris& ephemeride_moteur() {
  static const fen::ephem::StandishEphemeris eph;
  return eph;
}

// État HÉLIOCENTRIQUE d'un corps à une époque [GDD 15.2].
inline Corps ephemeride(const std::string& nom, Epoch t) {
  const fen::ephem::Body b = corps_from_name(nom);
  const fen::ephem::PosVel pv = ephemeride_moteur().state(b, fen::ephem::Body::Sun, t);
  return Corps(nom, pv.r, pv.v);
}

// --- TRANSFERT (solution de Lambert) ----------------------------------------
class Transfert {
 public:
  Transfert() = default;
  bool ok() const { return ok_; }
  double dv_depart() const { return dv_dep_; }   // injection depuis l'orbite de départ
  double dv_arrivee() const { return dv_arr_; }  // freinage/insertion à l'arrivée
  double dv_total() const { return dv_dep_ + dv_arr_; }
  Vec3 v_depart() const { return v1_; }          // vitesse héliocentrique requise
  Vec3 v_arrivee() const { return v2_; }
  double tof() const { return tof_; }

  friend Transfert lambert(const Corps&, const Corps&, double);
 private:
  bool ok_{false};
  double dv_dep_{}, dv_arr_{}, tof_{};
  Vec3 v1_{}, v2_{};
};

// Transfert balistique entre deux corps sur un temps de vol donné [GDD 15.2].
// Le Δv total = injection (v1 − v_départ) + insertion (v2 − v_arrivée) : c'est
// la grandeur physique de la mission, calculée sur les VRAIES vitesses des
// corps (l'exemple du GDD ne passe que les positions ; on fait mieux).
inline Transfert lambert(const Corps& depart, const Corps& arrivee, double tof) {
  Transfert t;
  t.tof_ = tof;
  // prograde=true : sens direct du système solaire (h_z > 0), celui d'un
  // transfert Terre->Mars réel. max_revs=0 : transfert direct (Type I/II).
  const fen::astro::LambertResult r = fen::astro::lambert(
      depart.position(), arrivee.position(), tof, fen::cst::MU_SUN, true, 0);
  if (!r.ok || r.solutions.empty()) return t;
  t.v1_ = r.solutions[0].v1;
  t.v2_ = r.solutions[0].v2;
  t.dv_dep_ = fen::norm(t.v1_ - depart.vitesse());
  t.dv_arr_ = fen::norm(t.v2_ - arrivee.vitesse());
  t.ok_ = true;
  return t;
}

// --- VÉHICULE (budget de masse) ---------------------------------------------
// Valeur-objet : vitesse d'éjection effective, masse sèche (charge utile
// comprise, car elle est accélérée avec l'étage), capacité d'ergols UTILISABLES.
// C'est le budget MONO-ÉTAGE de l'exemple 15.2 ; il est EXACT pour un étage
// CONSTRUIT (charge d'ergols figée -> masse sèche constante).
class Vehicule {
 public:
  Vehicule() = default;
  Vehicule(std::string id, double ve, double masse_seche, double capacite_ergols)
      : id_(std::move(id)), ve_(ve), masse_seche_(masse_seche), capacite_(capacite_ergols) {}
  const std::string& id() const { return id_; }
  double ve_effective() const { return ve_; }
  double masse_seche() const  { return masse_seche_; }
  double capacite_ergols() const { return capacite_; }

  // Ergols requis pour un Δv (Tsiolkovsky) : mf·(exp(Δv/ve) − 1).
  double ergols_pour(double dv) const {
    return masse_seche_ * (std::exp(dv / ve_) - 1.0);
  }
  bool faisable(double dv) const { return ergols_pour(dv) <= capacite_; }
 private:
  std::string id_;
  double ve_{}, masse_seche_{}, capacite_{};
};

// Assemble un ÉTAGE RÉEL depuis le catalogue de pièces [GDD 12.1] : moteur et
// réservoir portent leurs vraies données publiques (Isp, masse, fraction sèche).
// AUCUNE physique dupliquée — on remplit un `fen::vehicle::Stage`, dont les
// formules de masse font foi ; ce fichier ne fait que les LIRE.
inline fen::vehicle::Stage etage_reel(const std::string& engine_id,
                                      const std::string& tank_id,
                                      double ergols_charges_kg,
                                      double structure_kg) {
  fen::vehicle::Stage s;
  if (const fen::vehicle::EnginePart* e = fen::vehicle::find_engine(engine_id))
    s.engine = fen::vehicle::to_engine(*e);
  if (const fen::vehicle::TankPart* t = fen::vehicle::find_tank(tank_id))
    s.tank = fen::vehicle::to_tank(*t, ergols_charges_kg);
  s.structure_mass = structure_kg;
  return s;
}

// Dérive le budget mono-étage d'un étage réel : ve du moteur, ergols utilisables
// du réservoir, masse sèche = inerte de l'étage + charge utile qui l'accompagne.
// À pleine charge, `Vehicule` restitue EXACTEMENT le stage_dv de Vehicle.hpp
// (vérifié par oracle) : la façade et le cœur donnent le même Δv.
inline Vehicule vehicule_depuis_etage(std::string id, const fen::vehicle::Stage& s,
                                      double charge_utile_kg) {
  return Vehicule(std::move(id), s.engine.ve(), s.dry_mass() + charge_utile_kg,
                  s.tank.usable());
}

// Architectures NOMMÉES, définies par de vraies pièces (plus de constante
// abstraite). ARV-3 = étage cryogénique RL10C-1 (lignée Centaur), LOX/LH2.
struct Architecture {
  const char* id;
  const char* engine;
  const char* tank;
  double ergols_kg;
  double structure_kg;
  double charge_utile_kg;
};

inline const std::vector<Architecture>& architectures() {
  static const std::vector<Architecture> v = {
    {"ARV-3", "RL10C-1", "TANK-LOX-LH2", 10000.0, 400.0, 2000.0},
    {"AVU-1", "VINCI",   "TANK-LOX-LH2",  8000.0, 350.0, 1500.0},  // étage sup. Vinci
    {"ASK-2", "AJ10-190","TANK-STOCK",    3000.0, 200.0, 1200.0},  // manœuvre stockable
  };
  return v;
}

// Charge une architecture nommée depuis le catalogue réel. À défaut : un étage
// cryo Vinci générique mais RÉEL, jamais une pièce abstraite [GDD 12.1].
inline Vehicule charger(const std::string& id) {
  for (const Architecture& a : architectures())
    if (id == a.id)
      return vehicule_depuis_etage(id, etage_reel(a.engine, a.tank, a.ergols_kg,
                                                  a.structure_kg), a.charge_utile_kg);
  return vehicule_depuis_etage(id, etage_reel("VINCI", "TANK-LOX-LH2", 8000.0, 350.0),
                               1500.0);
}

} // namespace ares::sol
