// fen/core/Units.hpp
//
// DOCTRINE DES UNITÉS  (à lire avant de toucher à ce fichier)
// ----------------------------------------------------------------------------
// Il existe TROIS classes d'erreur d'unité, et une seule barrière ne les couvre
// pas toutes. Le jeu doit les traiter séparément :
//
//   (1) ERREUR DIMENSIONNELLE  — additionner des mètres et des secondes.
//       => attrapée à la COMPILATION par Q<Dim<...>> ci-dessous.
//
//   (2) ERREUR D'ÉCHELLE       — livrer des lbf·s là où on attend des N·s
//       (Mars Climate Orbiter, 1999). Dimensionnellement CORRECT.
//       => attrapée uniquement par un TOKEN D'UNITÉ EXPLICITE ET OBLIGATOIRE
//          à chaque frontière (fichier de mission, saisie joueur, API script).
//          Voir io/FplParser : un nombre sans unité = erreur de parsing, pas
//          un défaut silencieux.
//
//   (3) ERREUR DE DÉRIVATION   — la bonne unité, la mauvaise formule.
//       => AUCUN système de types ne l'attrape. Seule la propagation la révèle.
//          C'est le cœur du jeu.
//
// Conséquence architecturale : les unités fortes vivent AUX INTERFACES
// (mission/, io/, vehicle/, ui/). Les boucles chaudes (prop/, force/) utilisent
// des double SI nus, pour ne pas payer d'abstraction dans un Monte-Carlo à 1e5
// trajectoires. La frontière est explicite et testée.
#pragma once
#include <ratio>
#include <cmath>

namespace fen::units {

template <class M, class L, class T>
struct Dim { using mass = M; using len = L; using time = T; };

template <class A, class B>
using DimMul = Dim<std::ratio_add<typename A::mass, typename B::mass>,
                   std::ratio_add<typename A::len,  typename B::len>,
                   std::ratio_add<typename A::time, typename B::time>>;
template <class A, class B>
using DimDiv = Dim<std::ratio_subtract<typename A::mass, typename B::mass>,
                   std::ratio_subtract<typename A::len,  typename B::len>,
                   std::ratio_subtract<typename A::time, typename B::time>>;
template <class A>
using DimSqrt = Dim<std::ratio_divide<typename A::mass, std::ratio<2>>,
                    std::ratio_divide<typename A::len,  std::ratio<2>>,
                    std::ratio_divide<typename A::time, std::ratio<2>>>;

template <class D>
class Q {
  double v_{};
 public:
  using dim = D;
  constexpr Q() = default;
  constexpr explicit Q(double v) : v_(v) {}
  constexpr double si() const { return v_; }          // valeur en unités SI de base

  constexpr Q  operator+(Q o) const { return Q{v_ + o.v_}; }
  constexpr Q  operator-(Q o) const { return Q{v_ - o.v_}; }
  constexpr Q  operator-()    const { return Q{-v_}; }
  constexpr Q  operator*(double s) const { return Q{v_ * s}; }
  constexpr Q  operator/(double s) const { return Q{v_ / s}; }
  constexpr Q& operator+=(Q o) { v_ += o.v_; return *this; }
  constexpr Q& operator-=(Q o) { v_ -= o.v_; return *this; }
  constexpr bool operator<(Q o)  const { return v_ <  o.v_; }
  constexpr bool operator>(Q o)  const { return v_ >  o.v_; }
  constexpr bool operator<=(Q o) const { return v_ <= o.v_; }
  constexpr bool operator>=(Q o) const { return v_ >= o.v_; }
};

template <class D> constexpr Q<D> operator*(double s, Q<D> q) { return q * s; }
template <class A, class B> constexpr Q<DimMul<A, B>> operator*(Q<A> a, Q<B> b) {
  return Q<DimMul<A, B>>{a.si() * b.si()};
}
template <class A, class B> constexpr Q<DimDiv<A, B>> operator/(Q<A> a, Q<B> b) {
  return Q<DimDiv<A, B>>{a.si() / b.si()};
}
template <class A> inline Q<DimSqrt<A>> sqrt(Q<A> a) { return Q<DimSqrt<A>>{std::sqrt(a.si())}; }

using R0 = std::ratio<0>;
using R1 = std::ratio<1>;
using R2 = std::ratio<2>;
using R3 = std::ratio<3>;
using Rm1 = std::ratio<-1>;
using Rm2 = std::ratio<-2>;

using DimScalar  = Dim<R0, R0, R0>;
using DimLength  = Dim<R0, R1, R0>;
using DimTime    = Dim<R0, R0, R1>;
using DimMass    = Dim<R1, R0, R0>;
using DimArea    = Dim<R0, R2, R0>;
using DimVel     = Dim<R0, R1, Rm1>;
using DimAccel   = Dim<R0, R1, Rm2>;
using DimForce   = Dim<R1, R1, Rm2>;
using DimGM      = Dim<R0, R3, Rm2>;   // m^3 s^-2
using DimMdot    = Dim<R1, R0, Rm1>;
using DimPower   = Dim<R1, R2, std::ratio<-3>>;
using DimDensity = Dim<R1, std::ratio<-3>, R0>;

using Scalar   = Q<DimScalar>;
using Length   = Q<DimLength>;
using Duration = Q<DimTime>;
using Mass     = Q<DimMass>;
using Area     = Q<DimArea>;
using Velocity = Q<DimVel>;   // et Delta-v
using Accel    = Q<DimAccel>;
using Force    = Q<DimForce>;
using GM       = Q<DimGM>;
using MassFlow = Q<DimMdot>;
using Power    = Q<DimPower>;
using Density  = Q<DimDensity>;

inline namespace literals {
#define FEN_LIT(name, Type, factor)                                              \
  constexpr Type operator"" name(long double v) { return Type{static_cast<double>(v) * (factor)}; } \
  constexpr Type operator"" name(unsigned long long v) { return Type{static_cast<double>(v) * (factor)}; }
FEN_LIT(_m,    Length,   1.0)
FEN_LIT(_km,   Length,   1000.0)
FEN_LIT(_s,    Duration, 1.0)
FEN_LIT(_min,  Duration, 60.0)
FEN_LIT(_h,    Duration, 3600.0)
FEN_LIT(_day,  Duration, 86400.0)
FEN_LIT(_kg,   Mass,     1.0)
FEN_LIT(_t,    Mass,     1000.0)
FEN_LIT(_mps,  Velocity, 1.0)
FEN_LIT(_kmps, Velocity, 1000.0)
FEN_LIT(_N,    Force,    1.0)
FEN_LIT(_kN,   Force,    1000.0)
FEN_LIT(_W,    Power,    1.0)
FEN_LIT(_kW,   Power,    1000.0)
#undef FEN_LIT
} // namespace literals

// Preuve que la barrière (1) fonctionne : décommenter -> erreur de compilation.
// static_assert(sizeof(decltype(1.0_m + 1.0_s)) > 0, "ne doit pas compiler");

} // namespace fen::units
