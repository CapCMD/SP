// ares/vol.hpp — API VOL : LOGICIEL EMBARQUÉ [GDD 15.3, v1.2]
//
// Espace de nom `ares::vol`. ÉCRITURE, CONSÉQUENCES RÉELLES, qualification
// obligatoire (banc d'essai, voir code/CodeQualification.hpp). C'est le logiciel
// qui s'exécute À BORD, y compris hors de portée d'une intervention du sol
// [GDD 9.6] : « le joueur ne pilote pas, il écrit à l'avance la logique qui
// décidera à sa place, avec ses propres garde-fous ».
//
// Ce fichier fournit le CONTEXTE que la fonction du joueur reçoit, et les types
// qu'elle manipule (Etat de navigation, Écart, Cible, Manœuvre, Réserves). Le
// Contexte ENREGISTRE les décisions (manœuvres exécutées ou différées, alertes,
// replanifications) : le simulateur les applique ensuite, et un oracle peut
// vérifier que les garde-fous du code se comportent comme voulu.
//
// La physique de la manœuvre reste celle du cœur (Δv, Tsiolkovsky) : ce fichier
// n'est que la médiation entre le code du joueur et le monde.
#pragma once
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

#include "fen/core/Vec3.hpp"

namespace ares::vol {

using fen::Vec3;

// Helpers d'unités (repris de l'API sol, pour un code de vol autonome).
inline double metres(double n) { return n; }
inline double km(double n)     { return n * 1000.0; }
inline double heures(double n) { return n * 3600.0; }
inline double jours(double n)  { return n * 86400.0; }

// --- ÉTAT DE NAVIGATION (la SOLUTION, jamais la vérité [GDD 7.5, 8.1]) -------
class Etat {
 public:
  Etat() = default;
  Etat(Vec3 pos, Vec3 vel, double sigma3) : pos_(pos), vel_(vel), sigma3_(sigma3) {}
  Vec3 position() const { return pos_; }
  Vec3 vitesse() const  { return vel_; }
  double incertitude_3sigma() const { return sigma3_; }   // mètres
 private:
  Vec3 pos_{}, vel_{};
  double sigma3_{0.0};
};

class Navigation {
 public:
  explicit Navigation(Etat s) : solution_(s) {}
  Etat solution() const { return solution_; }
 private:
  Etat solution_;
};

// --- ÉCART projeté à la cible -----------------------------------------------
class Ecart {
 public:
  Ecart() = default;
  explicit Ecart(Vec3 v) : v_(v) {}
  Vec3 vecteur() const { return v_; }
  double norme() const { return fen::norm(v_); }   // mètres
 private:
  Vec3 v_{};
};

class Manoeuvre;   // fwd

class Cible {
 public:
  Cible() = default;
  Cible(Vec3 pos_nominale, double tolerance_m)
      : pos_(pos_nominale), tol_(tolerance_m) {}
  // Écart PROJETÉ entre l'estimation et la cible nominale [GDD 8.3].
  Ecart ecart_projete(const Etat& e) const { return Ecart(e.position() - pos_); }
  double tolerance() const { return tol_; }   // mètres
  Vec3 position() const { return pos_; }
 private:
  Vec3 pos_{};
  double tol_{0.0};
};

// --- MANŒUVRE ---------------------------------------------------------------
class Manoeuvre {
 public:
  Manoeuvre() = default;
  Manoeuvre(Vec3 dv_vec) : dv_(dv_vec) {}
  Vec3 vecteur() const { return dv_; }
  double dv() const { return fen::norm(dv_); }   // m/s
 private:
  Vec3 dv_{};
};

// Le SOLVEUR de correction : ramène l'estimé vers la cible. Modèle DÉCLARÉ —
// correction proportionnelle à l'écart sur un temps caractéristique `tau`.
class Solveur {
 public:
  explicit Solveur(double tau_s = 86400.0) : tau_(tau_s) {}
  Manoeuvre corriger(const Etat& e, const Cible& c) const {
    const Vec3 ecart = c.position() - e.position();
    return Manoeuvre(ecart / tau_);   // Δv ~ écart / temps de manœuvre
  }
 private:
  double tau_;
};

// --- RÉSERVES ---------------------------------------------------------------
class Reserves {
 public:
  explicit Reserves(double dv_dispo) : dv_(dv_dispo) {}
  double dv_disponible() const { return dv_; }
  void consommer(double dv) { dv_ = std::max(0.0, dv_ - dv); }
 private:
  double dv_;
};

// --- LE CONTEXTE : ce que la fonction du joueur reçoit -----------------------
// Il donne accès aux mesures et ENREGISTRE les décisions. Le simulateur les
// applique ; un oracle les inspecte.
class Contexte {
 public:
  Contexte(Etat nav, Cible cible, Reserves reserves, Solveur solveur = Solveur())
      : nav_(nav), cible_(cible), reserves_(reserves), solveur_(solveur) {}

  Navigation navigation() const { return Navigation(nav_); }
  const Cible& cible() const { return cible_; }
  Reserves& reserves() { return reserves_; }
  const Solveur& solveur() const { return solveur_; }

  // DÉCISIONS (conséquences réelles) — enregistrées pour le simulateur.
  void executer(const Manoeuvre& m) {
    reserves_.consommer(m.dv());
    executees_.push_back(m);
  }
  void differer(const Manoeuvre& m) { differees_.push_back(m); }
  void alerte(const std::string& s) { alertes_.push_back(s); }
  void replanifier(double dt_s) { replans_.push_back(dt_s); }
  void journal_bord(const char* fmt, ...) {
    char buf[512];
    va_list ap; va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    journal_.emplace_back(buf);
  }

  // INSPECTION (pour le simulateur / l'oracle).
  const std::vector<Manoeuvre>& executees() const { return executees_; }
  const std::vector<Manoeuvre>& differees() const { return differees_; }
  const std::vector<std::string>& alertes() const { return alertes_; }
  const std::vector<double>& replans() const { return replans_; }
  const std::vector<std::string>& journal() const { return journal_; }
  double dv_restant() const { return reserves_.dv_disponible(); }

 private:
  Etat nav_;
  Cible cible_;
  Reserves reserves_;
  Solveur solveur_;
  std::vector<Manoeuvre> executees_, differees_;
  std::vector<std::string> alertes_, journal_;
  std::vector<double> replans_;
};

} // namespace ares::vol
