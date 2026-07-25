// fen/core/Constants.hpp — constantes physiques, SI strict, sourcées.
// RÈGLE : aucune constante "de gameplay" ici. Uniquement des valeurs mesurées ou définies.
#pragma once

namespace fen::cst {

// --- définitions exactes (SI) ----------------------------------------------
inline constexpr double PI       = 3.14159265358979323846;
inline constexpr double TWO_PI   = 6.28318530717958647693;
inline constexpr double DEG      = PI / 180.0;
inline constexpr double G0       = 9.80665;            // m/s^2  — g0 conventionnel (déf. ISO). Sert UNIQUEMENT à Isp.
inline constexpr double AU       = 1.495978707e11;     // m      — IAU 2012, exact
inline constexpr double C_LIGHT  = 299792458.0;        // m/s    — exact
inline constexpr double K_BOLTZ  = 1.380649e-23;       // J/K    — exact (SI 2019)
inline constexpr double SIGMA_SB = 5.670374419e-8;     // W/m^2/K^4
inline constexpr double DAY      = 86400.0;            // s
inline constexpr double JD_J2000 = 2451545.0;          // 2000-01-01T12:00:00 TT
inline constexpr double JULIAN_CENTURY = 36525.0 * DAY;

// --- flux solaire -----------------------------------------------------------
inline constexpr double SOLAR_IRRADIANCE_1AU = 1361.0; // W/m^2 (TSI moyenne)

// --- paramètres gravitationnels GM [m^3 s^-2] -------------------------------
// Valeurs DE440 / IAU. Le jeu ne les arrondit jamais : une erreur de mu se paie.
inline constexpr double MU_SUN     = 1.32712440041279419e20;
inline constexpr double MU_MERCURY = 2.2031868551e13;
inline constexpr double MU_VENUS   = 3.24858592000e14;
inline constexpr double MU_EARTH   = 3.98600435507e14;
inline constexpr double MU_MOON    = 4.90280011800e12;
inline constexpr double MU_MARS    = 4.28283758157e13;   // système Mars (Mars + Phobos + Deimos)
inline constexpr double MU_JUPITER = 1.26712764100e17;   // système
inline constexpr double MU_SATURN  = 3.79340584100e16;   // système
inline constexpr double MU_TITAN   = 8.97813900000e12;
inline constexpr double MU_URANUS  = 5.79394130000e15;   // système
inline constexpr double MU_NEPTUNE = 6.83652710000e15;   // système
inline constexpr double MU_PLUTO   = 8.69610000000e11;   // Pluton (hors Charon)

// --- rayons équatoriaux moyens [m] ------------------------------------------
inline constexpr double R_SUN    = 6.957e8;
inline constexpr double R_EARTH  = 6378136.6;
inline constexpr double R_MOON   = 1737400.0;            // rayon moyen IAU
inline constexpr double R_MARS   = 3396200.0;
inline constexpr double R_SATURN = 60268000.0;
inline constexpr double R_TITAN  = 2574730.0;
inline constexpr double R_URANUS  = 25559000.0;   // equatorial
inline constexpr double R_NEPTUNE = 24764000.0;
inline constexpr double R_PLUTO   = 1188300.0;

// --- harmoniques zonales (V1) ------------------------------------------------
inline constexpr double J2_EARTH = 1.08262668e-3;
inline constexpr double J2_MARS  = 1.95545e-3;

// --- vitesses angulaires de rotation [rad/s] --------------------------------
inline constexpr double OMEGA_EARTH = 7.292115e-5;

// --- Titan (V2) — atmosphère & orbite ---------------------------------------
inline constexpr double TITAN_SMA_AROUND_SATURN = 1.221870e9;  // m
inline constexpr double TITAN_SURF_G            = 1.352;       // m/s^2
inline constexpr double TITAN_SURF_P            = 1.467e5;     // Pa  (1.467 bar)
inline constexpr double TITAN_SURF_T            = 93.7;        // K
inline constexpr double TITAN_SURF_RHO          = 5.4;         // kg/m^3 (N2 @ 93.7 K, 1.467 bar)
inline constexpr double TITAN_SCALE_HEIGHT_LOW  = 21000.0;     // m (0–50 km, approx.)
inline constexpr double TITAN_VESC_SURF         = 2639.0;      // m/s
inline constexpr double TITAN_SIDEREAL_PERIOD   = 15.945 * DAY;// s (synchrone)

} // namespace fen::cst
