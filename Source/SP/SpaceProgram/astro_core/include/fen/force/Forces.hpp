// fen/force/Forces.hpp
// Pile de forces du PROPAGATEUR DE VÉRITÉ.
// Contrat : chaque modèle ACCUMULE (+=) dans (a, mdot). Aucun ne remplace.
// Aucun n'a d'état mutable, aucun n'alloue, aucun ne lit l'horloge, aucun ne
// tire de nombre aléatoire. => thread-safe, donc Monte-Carlo parallèle gratuit.
#pragma once
#include <memory>
#include <vector>
#include <string>
#include "fen/core/Vec3.hpp"
#include "fen/core/Constants.hpp"
#include "fen/core/Epoch.hpp"
#include "fen/ephem/Ephemeris.hpp"

namespace fen::force {

struct Ctx {
  double t;      // s TDB depuis J2000
  Vec3 r;        // m, rel. corps central, inertiel
  Vec3 v;        // m/s
  double m;      // kg
};

class IForce {
 public:
  virtual ~IForce() = default;
  virtual void accumulate(const Ctx& c, Vec3& a, double& mdot) const = 0;
  virtual std::string name() const = 0;
};

// --- gravité du corps central (masse ponctuelle) ----------------------------
class CentralGravity final : public IForce {
 public:
  explicit CentralGravity(double mu) : mu_(mu) {}
  void accumulate(const Ctx& c, Vec3& a, double&) const override {
    const double r = norm(c.r);
    a += c.r * (-mu_ / (r * r * r));
  }
  std::string name() const override { return "CentralGravity"; }
 private:
  double mu_;
};

// --- troisième corps, formulation f(q) de Battin ----------------------------
// L'écriture naïve  a = -mu3*( d/|d|^3 + s/|s|^3 )  soustrait deux nombres
// quasi égaux quand |r| << |s| (cas nominal : sonde près de la Terre, Soleil à
// 1 UA) : on perd jusqu'à 10 chiffres significatifs. Battin réécrit la somme
// SANS annulation :
//     a = -(mu3/|d|^3) * ( r + f(q) * s ),   d = r - s
//     q = r.(r - 2s)/|s|^2 ,   f(q) = q(3+3q+q^2) / (1 + (1+q)^{3/2})
// (identité : (1+q)^{3/2} - 1 = q(3+3q+q^2)/((1+q)^{3/2}+1))
class ThirdBodyGravity final : public IForce {
 public:
  ThirdBodyGravity(const ephem::IEphemeris* eph, ephem::Body body, ephem::Body center)
      : eph_(eph), body_(body), center_(center), mu3_(ephem::body_mu(body)) {}

  void accumulate(const Ctx& c, Vec3& a, double&) const override {
    const Vec3 s = eph_->state(body_, center_, Epoch{c.t}).r;   // corps perturbateur rel. centre
    const Vec3 d = c.r - s;
    const double s2 = norm2(s);
    if (s2 <= 0.0) return;
    const double q = dot(c.r, c.r - 2.0 * s) / s2;
    const double one_plus_q = 1.0 + q;                          // = |d|^2/|s|^2 >= 0
    const double fq = q * (3.0 + 3.0 * q + q * q)
                      / (1.0 + one_plus_q * std::sqrt(one_plus_q));
    const double dn = norm(d);
    a += (c.r + s * fq) * (-mu3_ / (dn * dn * dn));
  }
  std::string name() const override {
    return std::string("ThirdBody[") + ephem::body_name(body_) + "]";
  }
 private:
  const ephem::IEphemeris* eph_;
  ephem::Body body_, center_;
  double mu3_;
};

// --- J2 (V1, présent pour la LEO/GTO) ---------------------------------------
class J2Gravity final : public IForce {
 public:
  J2Gravity(double mu, double Re, double J2) : mu_(mu), Re_(Re), J2_(J2) {}
  void accumulate(const Ctx& c, Vec3& a, double&) const override {
    const double r = norm(c.r);
    const double k = 1.5 * J2_ * mu_ * Re_ * Re_ / (r * r * r * r * r);
    const double z2r2 = 5.0 * c.r.z * c.r.z / (r * r);
    a += Vec3{-k * c.r.x * (1.0 - z2r2),
              -k * c.r.y * (1.0 - z2r2),
              -k * c.r.z * (3.0 - z2r2)};
  }
  std::string name() const override { return "J2"; }
 private:
  double mu_, Re_, J2_;
};

// --- poussée finie ----------------------------------------------------------
// LE point où le jeu refuse l'arcade. Le joueur COMMANDE une impulsion (t, dv).
// Le monde EXÉCUTE un arc de durée finie : a = F/m * u, mdot = -F/(Isp g0).
// Les pertes de gravité et de braquage tombent de l'intégration, elles ne sont
// PAS ajoutées à la main. Le Delta-v réalisé n'est jamais celui commandé.
enum class ThrustFrame { InertialFixed, RswFixed };

class FiniteThrust final : public IForce {
 public:
  FiniteThrust(double t_start, double t_end, double thrust_N, double isp_s,
               Vec3 dir, ThrustFrame frame, double dry_mass_kg)
      : t0_(t_start), t1_(t_end), F_(thrust_N), isp_(isp_s),
        dir_(unit(dir)), frame_(frame), mdry_(dry_mass_kg) {}

  void accumulate(const Ctx& c, Vec3& a, double& mdot) const override {
    if (c.t < t0_ || c.t > t1_) return;
    if (c.m <= mdry_) return;                 // réservoir vide : le moteur s'éteint. Point.
    Vec3 u = dir_;
    if (frame_ == ThrustFrame::RswFixed) {
      const Basis3 b = rsw_basis(c.r, c.v);   // recalculé à CHAQUE appel : c'est un
      u = rsw_to_inertial(b, dir_);           // programme d'attitude, pas une constante
    }
    a += u * (F_ / c.m);
    mdot += -F_ / (isp_ * cst::G0);
  }
  std::string name() const override { return "FiniteThrust"; }
  double t_start() const { return t0_; }
  double t_end() const { return t1_; }

 private:
  double t0_, t1_, F_, isp_;
  Vec3 dir_;
  ThrustFrame frame_;
  double mdry_;
};

// --- pile ------------------------------------------------------------------
class ForceStack {
 public:
  void add(std::shared_ptr<IForce> f) { forces_.push_back(std::move(f)); }
  void derivative(double t, const Vec3& r, const Vec3& v, double m,
                  Vec3& a, double& mdot) const {
    a = Vec3{}; mdot = 0.0;
    const Ctx c{t, r, v, m};
    for (const auto& f : forces_) f->accumulate(c, a, mdot);
  }
  const std::vector<std::shared_ptr<IForce>>& forces() const { return forces_; }

 private:
  std::vector<std::shared_ptr<IForce>> forces_;
};

} // namespace fen::force
