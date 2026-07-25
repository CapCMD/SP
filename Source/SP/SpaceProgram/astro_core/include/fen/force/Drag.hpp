// fen/force/Drag.hpp — TRAÎNÉE ATMOSPHÉRIQUE [GDD 7.1, 7.7]
//
// Elle manquait à la pile de forces : sans elle, une orbite basse ne décroît
// jamais, l'activité solaire n'a aucun effet sur la durée de vie orbitale, et
// l'aérofreinage est impossible — trois choses que le GDD exige explicitement.
//
// ═══ MODÈLE ═══
//     a_drag = −½ · ρ(h) · |v_rel| · v_rel · Cd·A/m
// avec la VITESSE RELATIVE À L'ATMOSPHÈRE :
//     v_rel = v − ω × r
// L'atmosphère tourne avec le corps. L'ignorer fausse la traînée d'environ 5 %
// en orbite basse équatoriale et biaise systématiquement la durée de vie : le
// GDD interdit ce genre de simplification silencieuse [6.8].
//
// Le coefficient balistique B = m/(Cd·A) est la grandeur qui gouverne tout :
// c'est lui, et non la masse seule, qui décide si un objet retombe en semaines
// ou en siècles. On garde donc `Cd·A` séparé de la masse, que la consommation
// d'ergols fait varier au cours du vol.
//
// DOMAINE DE VALIDITÉ : régime continu (h ≲ 1000 km). Au-delà, ρ est résiduel
// et la traînée devient négligeable devant la pression de radiation solaire
// (cf. `force/Srp.hpp`). Régime moléculaire libre non modélisé finement : Cd
// est traité comme constant (2,2 est la valeur usuelle pour un satellite en
// flux moléculaire libre) — approximation DÉCLARÉE.
#pragma once
#include <cmath>
#include <string>

#include "fen/core/Constants.hpp"
#include "fen/core/Vec3.hpp"
#include "fen/env/Atmosphere.hpp"
#include "fen/force/Forces.hpp"

namespace fen::force {

// Coefficient de traînée usuel d'un satellite en flux moléculaire libre.
inline constexpr double CD_SATELLITE_FREE_MOLECULAR = 2.2;

class AtmosphericDrag final : public IForce {
 public:
  // `cd_area_m2` = Cd · A (m²). La masse vient du contexte : elle change quand
  // le véhicule consomme.
  AtmosphericDrag(const env::IAtmosphere* atmo, double cd_area_m2)
      : atmo_(atmo), cd_area_(cd_area_m2) {}

  void accumulate(const Ctx& c, Vec3& a, double&) const override {
    if (!atmo_ || cd_area_ <= 0.0 || c.m <= 0.0) return;
    const double r = norm(c.r);
    const double alt = r - atmo_->body_radius();
    if (alt <= 0.0) return;                    // sous la surface : hors domaine
    const double rho = atmo_->density(alt);
    if (rho <= 0.0) return;

    // L'atmosphère est entraînée par la rotation du corps (axe z du repère).
    const Vec3 omega{0.0, 0.0, atmo_->body_omega()};
    const Vec3 v_rel = c.v - cross(omega, c.r);
    const double vr = norm(v_rel);
    if (vr <= 0.0) return;

    a += v_rel * (-0.5 * rho * vr * cd_area_ / c.m);
  }

  std::string name() const override { return "AtmosphericDrag"; }

  // Coefficient balistique B = m/(Cd·A) pour une masse donnée (kg/m²).
  double ballistic_coefficient(double mass_kg) const {
    return cd_area_ > 0.0 ? mass_kg / cd_area_ : 0.0;
  }

 private:
  const env::IAtmosphere* atmo_;
  double cd_area_;
};

} // namespace fen::force
