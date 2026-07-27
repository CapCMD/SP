#include "fen/ephem/Ephemeris.hpp"
#include "fen/ephem/BodyOrientation.hpp"   // spin_axis_ecliptic (plan équatorial de Saturne)
#include "fen/ephem/Satellites.hpp"        // les lunes majeures (modèle déclaré)
#include "fen/astro/Elements.hpp"
#include "fen/astro/Kepler.hpp"
#include "fen/core/Constants.hpp"
#include <cmath>
#include <stdexcept>

namespace fen::ephem {
using namespace fen::cst;

namespace {
// Table Standish : éléments à J2000 et taux par siècle julien.
// a [UA], e [-], I [deg], L [deg], long. périhélie [deg], long. noeud [deg]
struct StdElem {
  double a, a_dot;
  double e, e_dot;
  double I, I_dot;
  double L, L_dot;
  double w_bar, w_bar_dot;   // longitude du périhélie
  double Omega, Omega_dot;
  double b{0.0};             // terme séculaire T² sur M (deg) — Table 2b (Pluton)
};

// Source : JPL/Solar System Dynamics, validité 1800 AD - 2050 AD.
constexpr StdElem kMercury = {0.38709927, 0.00000037, 0.20563593, 0.00001906,
                              7.00497902, -0.00594749, 252.25032350, 149472.67411175,
                              77.45779628, 0.16047689, 48.33076593, -0.12534081};
constexpr StdElem kVenus   = {0.72333566, 0.00000390, 0.00677672, -0.00004107,
                              3.39467605, -0.00078890, 181.97909950, 58517.81538729,
                              131.60246718, 0.00268329, 76.67984255, -0.27769418};
constexpr StdElem kEarth   = {1.00000261, 0.00000562, 0.01671123, -0.00004392,
                              -0.00001531, -0.01294668, 100.46457166, 35999.37244981,
                              102.93768193, 0.32327364, 0.0, 0.0};
constexpr StdElem kMars    = {1.52371034, 0.00001847, 0.09339410, 0.00007882,
                              1.84969142, -0.00813131, -4.55343205, 19140.30268499,
                              -23.94362959, 0.44441088, 49.55953891, -0.29257343};
constexpr StdElem kJupiter = {5.20288700, -0.00011607, 0.04838624, -0.00013253,
                              1.30439695, -0.00183714, 34.39644051, 3034.74612775,
                              14.72847983, 0.21252668, 100.47390909, 0.20469106};
constexpr StdElem kSaturn  = {9.53667594, -0.00125060, 0.05386179, -0.00050991,
                              2.48599187, 0.00193609, 49.95424423, 1222.49362201,
                              92.59887831, -0.41897216, 113.66242448, -0.28867794};
// Uranus & Neptune : Table 1 (1800-2050). Pluton : Table 2b (3000BC-3000AD, avec
// le terme séculaire b sur M) — seule table Standish qui l'inclut.
constexpr StdElem kUranus  = {19.18916464, -0.00196176, 0.04725744, -0.00004397,
                              0.77263783, -0.00242939, 313.23810451, 428.48202785,
                              170.95427630, 0.40805281, 74.01692503, 0.04240589};
constexpr StdElem kNeptune = {30.06992276, 0.00026291, 0.00859048, 0.00005105,
                              1.77004347, 0.00035372, -55.12002969, 218.45945325,
                              44.96476227, -0.32241464, 131.78422574, -0.00508664};
constexpr StdElem kPluto   = {39.48211675, -0.00031596, 0.24882730, 0.00005170,
                              17.14001206, 0.00004818, 238.92903833, 145.20780515,
                              224.06891629, -0.04062942, 110.30393684, -0.01183482,
                              -0.01262724};

const StdElem* table(Body b) {
  switch (b) {
    case Body::Mercury:   return &kMercury;
    case Body::Venus:     return &kVenus;
    case Body::EarthBary: return &kEarth;
    case Body::Mars:      return &kMars;
    case Body::Jupiter:   return &kJupiter;
    case Body::Saturn:    return &kSaturn;
    case Body::Uranus:    return &kUranus;
    case Body::Neptune:   return &kNeptune;
    case Body::Pluto:     return &kPluto;
    default:              return nullptr;
  }
}
} // namespace

const char* body_name(Body b) {
  switch (b) {
    case Body::Sun: return "SUN";
    case Body::Mercury: return "MERCURY";
    case Body::Venus: return "VENUS";
    case Body::EarthBary: return "EARTH";
    case Body::Moon: return "MOON";
    case Body::Mars: return "MARS";
    case Body::Jupiter: return "JUPITER";
    case Body::Saturn: return "SATURN";
    case Body::Titan: return "TITAN";
    case Body::Uranus: return "URANUS";
    case Body::Neptune: return "NEPTUNE";
    case Body::Pluto: return "PLUTO";
    // Les lunes portent leur nom dans la table satellitaire : une seule source.
    default: {
      const SatelliteDef* s = satellite_def(b);
      return s ? s->name : "?";
    }
  }
}

double body_mu(Body b) {
  switch (b) {
    case Body::Sun: return MU_SUN;
    case Body::Mercury: return MU_MERCURY;
    case Body::Venus: return MU_VENUS;
    case Body::EarthBary: return MU_EARTH;
    case Body::Moon: return MU_MOON;
    case Body::Mars: return MU_MARS;
    case Body::Jupiter: return MU_JUPITER;
    case Body::Saturn: return MU_SATURN;
    case Body::Titan: return MU_TITAN;
    case Body::Uranus: return MU_URANUS;
    case Body::Neptune: return MU_NEPTUNE;
    case Body::Pluto: return MU_PLUTO;
    default: {
      // Une lune a un GM comme un autre corps : le lire dans la table plutôt que
      // de rendre 0 (piège n°27 — un zéro se propage en silence).
      const SatelliteDef* s = satellite_def(b);
      return s ? s->mu : 0.0;
    }
  }
}

double body_radius(Body b) {
  // TOUS les corps de l'enum ont un rayon : un 0 ici se propage silencieusement
  // au rendu (corps de taille nulle) et au cadrage caméra (œil dans la planète).
  switch (b) {
    case Body::Sun: return R_SUN;
    case Body::Mercury: return R_MERCURY;
    case Body::Venus: return R_VENUS;
    case Body::EarthBary: return R_EARTH;
    case Body::Moon: return R_MOON;
    case Body::Mars: return R_MARS;
    case Body::Jupiter: return R_JUPITER;
    case Body::Saturn: return R_SATURN;
    case Body::Titan: return R_TITAN;
    case Body::Uranus: return R_URANUS;
    case Body::Neptune: return R_NEPTUNE;
    case Body::Pluto: return R_PLUTO;
    default: {
      const SatelliteDef* s = satellite_def(b);
      return s ? s->radius_m : 0.0;
    }
  }
}

double StandishEphemeris::declared_position_error(Body b) const {
  // 3-sigma, ordre de grandeur, sur 1800-2050 (JPL).
  switch (b) {
    case Body::EarthBary: return 5.0e6;   // ~5 000 km
    case Body::Mars:      return 2.5e7;   // ~25 000 km
    case Body::Venus:     return 6.0e6;
    case Body::Moon:      return 5.0e5;   // ~500 km (serie tronquee M&G)
    case Body::Jupiter:   return 1.0e8;
    case Body::Saturn:    return 3.0e8;
    default: {
      // LUNES : l'erreur est DOMINÉE par la phase, que le modèle ne cale pas sur
      // une éphéméride satellitaire (Satellites.hpp). Une phase inconnue sur une
      // orbite de rayon a, c'est une erreur de l'ordre de a — on l'annonce ainsi,
      // sans flatterie, plutôt que de rendre 0 (« exact »).
      const SatelliteDef* s = satellite_def(b);
      if (s) return s->sma_m + declared_position_error(s->parent);
      return 0.0;
    }
  }
}

namespace {

// --- LUNE : Montenbruck & Gill, "Satellite Orbits" §3.3.2 --------------------
// Serie tronquee, position GEOCENTRIQUE ecliptique.
// ERREUR DECLAREE : ~3' en direction, ~500 km en distance. Bornee, publiee,
// affichee au joueur. C'est un modele, pas un mensonge.
// V2 : DE440 remplace ceci sans changer une ligne d'interface.
Vec3 moon_geocentric(Epoch t) {
  const double T = t.centuries_since_j2000();
  auto frac = [](double x) { return x - std::floor(x); };
  const double ARCS = 3600.0 * 180.0 / PI;

  const double L0 = frac(0.606433 + 1336.851344 * T);          // longitude moyenne [rev]
  const double l  = TWO_PI * frac(0.374897 + 1325.552410 * T); // anomalie moy. Lune
  const double lp = TWO_PI * frac(0.993133 +   99.997361 * T); // anomalie moy. Soleil
  const double D  = TWO_PI * frac(0.827361 + 1236.853086 * T); // elongation
  const double F  = TWO_PI * frac(0.259086 + 1342.227825 * T); // argument de latitude

  const double dL = 22640.0 * std::sin(l)            - 4586.0 * std::sin(l - 2 * D)
                  +  2370.0 * std::sin(2 * D)        +  769.0 * std::sin(2 * l)
                  -   668.0 * std::sin(lp)           -  412.0 * std::sin(2 * F)
                  -   212.0 * std::sin(2 * l - 2 * D)-  206.0 * std::sin(l + lp - 2 * D)
                  +   192.0 * std::sin(l + 2 * D)    -  165.0 * std::sin(lp - 2 * D)
                  -   125.0 * std::sin(D)            -  110.0 * std::sin(l + lp)
                  +   148.0 * std::sin(l - lp)       -   55.0 * std::sin(2 * F - 2 * D);

  const double S = F + (dL + 412.0 * std::sin(2 * F) + 541.0 * std::sin(lp)) / ARCS;
  const double h = F - 2 * D;
  const double N = -526.0 * std::sin(h)      + 44.0 * std::sin(l + h)
                 -   31.0 * std::sin(-l + h) - 23.0 * std::sin(lp + h)
                 +   11.0 * std::sin(-lp + h)- 25.0 * std::sin(-2 * l + F)
                 +   21.0 * std::sin(-l + F);

  const double lambda = TWO_PI * frac(L0 + dL / 1296000.0);
  const double beta   = (18520.0 * std::sin(S) + N) / ARCS;
  const double R = 385000e3 - 20905e3 * std::cos(l)          - 3699e3 * std::cos(2 * D - l)
                 -  2956e3 * std::cos(2 * D)                 -  570e3 * std::cos(2 * l)
                 +   246e3 * std::cos(2 * l - 2 * D)         -  205e3 * std::cos(lp - 2 * D)
                 -   171e3 * std::cos(l + 2 * D)             -  152e3 * std::cos(l + lp - 2 * D);

  return Vec3{R * std::cos(beta) * std::cos(lambda),
              R * std::cos(beta) * std::sin(lambda),
              R * std::sin(beta)};
}

// NB : le cas particulier `titan_saturncentric` a été RETIRÉ (2026-07-27). Titan
// n'était qu'un premier satellite câblé à la main ; son modèle (orbite circulaire
// dans le plan équatorial du parent, pôle IAU) est devenu la règle générale de
// `fen/ephem/Satellites.hpp`, qui le sert désormais comme les dix-sept autres —
// avec en prime son inclinaison réelle (0,35°), qui était négligée ici.

} // namespace

PosVel StandishEphemeris::heliocentric(Body b, Epoch t) const {
  if (b == Body::Sun) return PosVel{Vec3{}, Vec3{}};

  if (b == Body::Moon) {
    // La Lune n'est pas heliocentrique dans le modele : Terre + geocentrique.
    // (Standish tabule le BARYCENTRE Terre-Lune ; l'ecart Terre/EMB, ~4670 km,
    //  est DANS l'erreur declaree. Il disparaitra avec DE440.)
    const PosVel e = heliocentric(Body::EarthBary, t);
    const Vec3 rm = moon_geocentric(t);
    const double dt = 60.0;
    const Vec3 vm = (moon_geocentric(t + dt) - moon_geocentric(t - dt)) / (2.0 * dt);
    return PosVel{e.r + rm, e.v + vm};
  }

  // LUNES MAJEURES : état du parent + orbite parentocentrique (Satellites.hpp).
  // Vitesse par différence centrée, comme pour la Lune. Un seul chemin pour les
  // dix-huit satellites — Titan compris, qui n'a plus de traitement propre.
  if (const SatelliteDef* s = satellite_def(b)) {
    const PosVel p = heliocentric(s->parent, t);
    const Vec3 rs = satellite_parentcentric(*s, t);
    const double dt = 60.0;
    const Vec3 vs = (satellite_parentcentric(*s, t + dt) -
                     satellite_parentcentric(*s, t - dt)) / (2.0 * dt);
    return PosVel{p.r + rs, p.v + vs};
  }

  const StdElem* E = table(b);
  if (!E) throw std::runtime_error("StandishEphemeris: corps non tabule");

  const double T = t.centuries_since_j2000();

  const double a     = (E->a + E->a_dot * T) * AU;                 // m
  const double e     =  E->e + E->e_dot * T;
  const double I     = (E->I + E->I_dot * T) * DEG;
  const double L     = (E->L + E->L_dot * T) * DEG;
  const double wbar  = (E->w_bar + E->w_bar_dot * T) * DEG;
  const double Omega = (E->Omega + E->Omega_dot * T) * DEG;

  const double argp = wbar - Omega;
  double M = L - wbar + (E->b * T * T) * DEG;   // terme séculaire T² (Pluton ; 0 sinon)
  M = std::fmod(M + PI, TWO_PI);
  if (M < 0) M += TWO_PI;
  M -= PI;

  // Reconstruction d'un état 2-corps cohérent (r ET v) à partir des éléments
  // osculateurs. On NÉGLIGE la contribution des dérivées séculaires à la vitesse
  // (a_dot ~ 1e-5 UA/siècle -> ~5e-7 m/s : sans effet).
  astro::Elements el;
  el.a = a; el.e = e; el.i = I; el.raan = Omega; el.argp = argp;
  el.nu = astro::M_to_nu(M, e);
  Vec3 r, v;
  astro::elements_to_rv(el, MU_SUN, r, v);
  return PosVel{r, v};
}

PosVel StandishEphemeris::state(Body b, Body center, Epoch t) const {
  if (b == center) return PosVel{Vec3{}, Vec3{}};
  const PosVel pb = heliocentric(b, t);
  const PosVel pc = heliocentric(center, t);
  return PosVel{pb.r - pc.r, pb.v - pc.v};
}

} // namespace fen::ephem
