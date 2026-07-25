// fen/core/Epoch.hpp
// Échelle de temps unique dans tout le noyau : TDB, en SECONDES depuis J2000.
//
// Honnêteté du modèle (à afficher au joueur dans le panneau "modèle") :
//  - TT - TDB : périodique, |.| <= 1.7 ms  -> NÉGLIGÉ en MVP. Erreur de position
//    résultante sur la Terre : |v|*1.7ms ~ 5 cm. Sans effet.
//  - UTC (sauts de seconde) : JAMAIS utilisé en interne. Les fichiers de mission
//    déclarent explicitement TDB. Aucune conversion implicite.
//  - V2 : TDB<->TT<->TAI<->UTC via table de sauts, quand on branchera DE440/SPK.
#pragma once
#include <cmath>
#include <cstdint>
#include <string>
#include "fen/core/Constants.hpp"

namespace fen {

struct Epoch {
  double tdb{0.0}; // s depuis J2000 TDB

  constexpr Epoch() = default;
  constexpr explicit Epoch(double s) : tdb(s) {}
  constexpr Epoch operator+(double dt) const { return Epoch{tdb + dt}; }
  constexpr Epoch operator-(double dt) const { return Epoch{tdb - dt}; }
  constexpr double operator-(Epoch o) const { return tdb - o.tdb; }
  constexpr bool operator<(Epoch o) const { return tdb < o.tdb; }

  double jd() const { return cst::JD_J2000 + tdb / cst::DAY; }
  double centuries_since_j2000() const { return tdb / cst::JULIAN_CENTURY; }
};

// --- calendrier grégorien proleptique (Howard Hinnant, exact en entiers) -----
constexpr long long days_from_civil(int y, unsigned m, unsigned d) {
  y -= (m <= 2);
  const long long era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153u * (m + (m > 2 ? -3u : 9u)) + 2u) / 5u + d - 1u;
  const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
  return era * 146097LL + static_cast<long long>(doe) - 719468LL;
}

struct CivilDate { int y; unsigned m, d; };

constexpr CivilDate civil_from_days(long long z) {
  z += 719468;
  const long long era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const long long y = static_cast<long long>(yoe) + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  const unsigned d = doy - (153 * mp + 2) / 5 + 1;
  const unsigned m = mp + (mp < 10 ? 3 : -9);
  return CivilDate{static_cast<int>(y + (m <= 2)), m, d};
}

// J2000 = 2000-01-01T12:00:00 -> jour civil 2000-01-01 = 10957 depuis 1970-01-01
inline constexpr long long DAYS_1970_TO_20000101 = 10957LL;

// "YYYY-MM-DDTHH:MM:SS(.sss)"  — TDB implicite, aucune ambiguïté tolérée.
Epoch epoch_from_iso(const std::string& iso);
std::string epoch_to_iso(Epoch e);

} // namespace fen
