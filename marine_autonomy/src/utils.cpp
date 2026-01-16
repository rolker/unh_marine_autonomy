#include "marine_autonomy/utils.h"

std::ostream& operator<< (std::ostream &out, const marine::LatLongDegrees &p)
{
  out << std::setprecision (10) << "lat (deg): " << p.latitude() << ", lon (deg): " << p.longitude() << ", alt (m): " << p.altitude();
  return out;
}

std::ostream& operator<< (std::ostream &out, const marine::ECEF &p)
{
  out << "ECEF xyz(m): " << p.x() << ", " << p.y() << ", " << p.z();
  return out;
}

std::ostream& operator<< (std::ostream &out, const marine::AngleDegrees &a)
{
  out << a.value() << " degrees (" << marine::AngleRadians(a).value() << " radians)";
  return out;
}

std::ostream& operator<< (std::ostream &out, const marine::AngleRadians &a)
{
  out << a.value() << " radians (" << marine::AngleDegrees(a).value() << " degrees)";
  return out;
}
