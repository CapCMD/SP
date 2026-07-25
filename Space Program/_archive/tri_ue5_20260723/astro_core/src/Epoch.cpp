#include "fen/core/Epoch.hpp"
#include <cstdio>
#include <stdexcept>

namespace fen {

Epoch epoch_from_iso(const std::string& iso) {
  int Y = 0, Mo = 0, D = 0, h = 0, mi = 0;
  double sec = 0.0;
  const int n = std::sscanf(iso.c_str(), "%d-%d-%dT%d:%d:%lf", &Y, &Mo, &D, &h, &mi, &sec);
  if (n < 3) throw std::runtime_error("epoch_from_iso: format invalide '" + iso + "'");
  const long long days = days_from_civil(Y, static_cast<unsigned>(Mo), static_cast<unsigned>(D));
  const double sod = h * 3600.0 + mi * 60.0 + sec;
  return Epoch{static_cast<double>(days - DAYS_1970_TO_20000101) * cst::DAY + sod - 43200.0};
}

std::string epoch_to_iso(Epoch e) {
  double s = e.tdb + 43200.0;                       // secondes depuis 2000-01-01T00:00
  long long d = static_cast<long long>(std::floor(s / cst::DAY));
  double sod = s - static_cast<double>(d) * cst::DAY;
  const CivilDate c = civil_from_days(d + DAYS_1970_TO_20000101);
  const int hh = static_cast<int>(sod / 3600.0);
  sod -= hh * 3600.0;
  const int mm = static_cast<int>(sod / 60.0);
  sod -= mm * 60.0;
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%04d-%02u-%02uT%02d:%02d:%06.3f", c.y, c.m, c.d, hh, mm, sod);
  return std::string(buf);
}

} // namespace fen
