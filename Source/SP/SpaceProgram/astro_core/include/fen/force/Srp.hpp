// fen/force/Srp.hpp — PRESSION DE RADIATION SOLAIRE [GDD 7.1, 7.5, 8.2]
//
// Elle manquait à la pile de forces. Ce n'est pas une perturbation exotique :
// sur une croisière interplanétaire de plusieurs mois, la SRP est la PREMIÈRE
// cause de dérive après la gravité. [GDD 8.2] range explicitement parmi les
// origines d'écart les « perturbations sous-estimées ou mal anticipées » — sans
// SRP, la trajectoire estimée et la trajectoire réelle ne divergeraient jamais
// pour la bonne raison, et le chapitre 8 perdrait son sens physique.
//
// ═══ MODÈLE ═══
//     a_srp = ν · Cr · (A/m) · P0 · (AU / d_sol)² · û
// où :
//   . P0 = S0/c = 1361 / 299 792 458 ≈ 4,54e-6 N/m², pression à 1 UA ;
//   . d_sol = distance héliocentrique du véhicule ; la loi en 1/d² est la même
//     que celle du flux (cf. env/Thermal.hpp, un seul Soleil) ;
//   . û = direction Soleil → véhicule (la lumière POUSSE) ;
//   . Cr ∈ [1, 2] : 1 = absorption totale, 2 = réflexion spéculaire parfaite ;
//   . ν ∈ [0, 1] : facteur d'ombre.
//
// ═══ OMBRE CONIQUE ═══
// Un cylindre d'ombre serait faux : le Soleil n'est pas ponctuel, il y a une
// PÉNOMBRE. On calcule les demi-angles apparents du Soleil et du corps
// occulteur vus du véhicule, et leur séparation angulaire :
//   . séparation > a_sol + a_corps            -> plein soleil (ν = 1)
//   . séparation < a_corps − a_sol            -> ombre totale (ν = 0)
//   . sinon                                    -> pénombre : ν = fraction du
//     disque solaire non couverte (aire d'intersection de deux disques).
// C'est le modèle « conical shadow » standard ; il est exact au titre de la
// géométrie des disques, l'approximation restant l'assombrissement centre-bord
// du Soleil, négligé — DÉCLARÉ [GDD 6.8].
#pragma once
#include <algorithm>
#include <cmath>
#include <string>

#include "fen/core/Constants.hpp"
#include "fen/core/Vec3.hpp"
#include "fen/ephem/Ephemeris.hpp"
#include "fen/force/Forces.hpp"

namespace fen::force {

// Pression de radiation solaire à 1 UA (N/m²) — dérivée, jamais saisie.
inline constexpr double SRP_P0 = cst::SOLAR_IRRADIANCE_1AU / cst::C_LIGHT;

// Fraction du disque solaire visible depuis le véhicule, corps occulteur inclus.
// `r_sun` : vecteur véhicule -> Soleil. `r_occ` : vecteur véhicule -> centre du
// corps occulteur. Rayons en mètres.
inline double shadow_factor(const Vec3& r_sun, const Vec3& r_occ,
                            double sun_radius_m, double occ_radius_m) {
  const double d_sun = norm(r_sun);
  const double d_occ = norm(r_occ);
  if (d_sun <= 0.0 || d_occ <= 0.0) return 1.0;
  if (d_occ <= occ_radius_m) return 0.0;              // à l'intérieur du corps

  // Demi-angles apparents (asin borné : le véhicule peut être très proche).
  const double a_sun = std::asin(std::min(1.0, sun_radius_m / d_sun));
  const double a_occ = std::asin(std::min(1.0, occ_radius_m / d_occ));
  // Séparation angulaire des deux centres vus du véhicule.
  const double cos_sep =
      std::clamp(dot(r_sun, r_occ) / (d_sun * d_occ), -1.0, 1.0);
  const double sep = std::acos(cos_sep);

  if (sep >= a_sun + a_occ) return 1.0;               // plein soleil
  if (sep <= a_occ - a_sun) return 0.0;               // ombre totale (umbra)
  if (sep <= a_sun - a_occ) {
    // Le corps occulteur est ENTIÈREMENT devant le Soleil sans le couvrir :
    // transit annulaire. Fraction perdue = rapport des aires apparentes.
    const double f = (a_occ * a_occ) / (a_sun * a_sun);
    return std::clamp(1.0 - f, 0.0, 1.0);
  }

  // PÉNOMBRE : aire d'intersection de deux disques de rayons angulaires
  // a_sun et a_occ dont les centres sont distants de `sep`.
  const double s2 = sep * sep, as2 = a_sun * a_sun, ao2 = a_occ * a_occ;
  const double x = (s2 + as2 - ao2) / (2.0 * sep);
  const double y2 = as2 - x * x;
  const double y = y2 > 0.0 ? std::sqrt(y2) : 0.0;
  const double area =
      as2 * std::acos(std::clamp(x / a_sun, -1.0, 1.0)) +
      ao2 * std::acos(std::clamp((sep - x) / a_occ, -1.0, 1.0)) - sep * y;
  const double sun_area = cst::PI * as2;
  return std::clamp(1.0 - area / sun_area, 0.0, 1.0);
}

class SolarRadiationPressure final : public IForce {
 public:
  // `cr` : coefficient de réflectivité [1..2]. `area_m2` : section efficace.
  // `center` : corps central du repère d'intégration (le Soleil s'y rapporte).
  // `occulter` : corps qui fait de l'ombre (souvent le corps central lui-même) ;
  // passer `Body::COUNT` pour désactiver l'ombre (croisière lointaine).
  SolarRadiationPressure(const ephem::IEphemeris* eph, ephem::Body center,
                         double cr, double area_m2,
                         ephem::Body occulter = ephem::Body::COUNT)
      : eph_(eph), center_(center), occulter_(occulter), cr_(cr), area_(area_m2) {}

  void accumulate(const Ctx& c, Vec3& a, double&) const override {
    if (!eph_ || area_ <= 0.0 || c.m <= 0.0) return;

    // Position du véhicule par rapport au SOLEIL.
    const Vec3 sun_rel_center =
        (center_ == ephem::Body::Sun)
            ? Vec3{}
            : eph_->state(ephem::Body::Sun, center_, Epoch{c.t}).r;
    const Vec3 r_sun_to_sat = c.r - sun_rel_center;   // Soleil -> véhicule
    const double d = norm(r_sun_to_sat);
    if (d <= 0.0) return;

    double nu = 1.0;
    if (occulter_ != ephem::Body::COUNT) {
      const Vec3 occ_rel_center =
          (occulter_ == center_)
              ? Vec3{}
              : eph_->state(occulter_, center_, Epoch{c.t}).r;
      const Vec3 sat_to_sun = -r_sun_to_sat;
      const Vec3 sat_to_occ = occ_rel_center - c.r;
      nu = shadow_factor(sat_to_sun, sat_to_occ, cst::R_SUN,
                         ephem::body_radius(occulter_));
    }
    if (nu <= 0.0) return;

    const double p = SRP_P0 * (cst::AU / d) * (cst::AU / d);
    a += r_sun_to_sat * (nu * cr_ * area_ * p / (c.m * d));  // /d : normalise û
  }

  std::string name() const override { return "SolarRadiationPressure"; }

 private:
  const ephem::IEphemeris* eph_;
  ephem::Body center_, occulter_;
  double cr_, area_;
};

} // namespace fen::force
