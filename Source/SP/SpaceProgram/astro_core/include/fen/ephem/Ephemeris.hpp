// fen/ephem/Ephemeris.hpp
//
// Le propagateur de VÉRITÉ a besoin de la position des corps perturbateurs.
// L'éphéméride fait donc partie de la vérité : son erreur est une VRAIE erreur,
// pas une approximation de gameplay. Elle doit être bornée et affichée.
//
//  MVP : Standish (JPL, "Keplerian Elements for Approximate Positions of the
//        Major Planets"), valable 1800-2050. Erreur typique : ~10-30" en
//        longitude pour Mars, soit ~1e4 km à 1.5 UA. À comparer à l'ellipse de
//        dispersion Gates dans le plan-B (~1e4-1e5 km) : l'éphéméride N'EST PAS
//        le terme dominant, donc le choix est défendable en MVP — et il est
//        affiché au joueur comme tel.
//  V2  : SPK DE440 (interpolation de Tchebychev, type 2/3). Erreur ~ km.
//        L'interface ci-dessous ne change pas.
#pragma once
#include "fen/core/Vec3.hpp"
#include "fen/core/Epoch.hpp"

namespace fen::ephem {

enum class Body { Sun, Mercury, Venus, EarthBary, Moon, Mars, Jupiter, Saturn, Titan,
                  Uranus, Neptune, Pluto, COUNT };

const char* body_name(Body b);
double body_mu(Body b);      // m^3/s^2
double body_radius(Body b);  // m

struct PosVel { Vec3 r; Vec3 v; };

class IEphemeris {
 public:
  virtual ~IEphemeris() = default;
  // État du corps `b` rapporté au corps `center`, repère ÉCLIPTIQUE J2000, SI.
  virtual PosVel state(Body b, Body center, Epoch t) const = 0;
  virtual const char* model_name() const = 0;
  // Erreur de position 3-sigma annoncée par le modèle (m) — affichée au joueur.
  virtual double declared_position_error(Body b) const = 0;
};

class StandishEphemeris final : public IEphemeris {
 public:
  PosVel state(Body b, Body center, Epoch t) const override;
  const char* model_name() const override { return "Standish 1800-2050 (elements + rates)"; }
  double declared_position_error(Body b) const override;

 private:
  PosVel heliocentric(Body b, Epoch t) const;
};

} // namespace fen::ephem
