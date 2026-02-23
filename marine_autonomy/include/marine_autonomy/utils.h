// Copyright 2016-2020 Roland Arsenault
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//    * Redistributions of source code must retain the above copyright
//      notice, this list of conditions and the following disclaimer.
//
//    * Redistributions in binary form must reproduce the above copyright
//      notice, this list of conditions and the following disclaimer in the
//      documentation and/or other materials provided with the distribution.
//
//    * Neither the name of the Roland Arsenault nor the names of its
//      contributors may be used to endorse or promote products derived from
//      this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#ifndef P11_UTILS_H
#define P11_UTILS_H

#include "tf2_geometry_msgs/tf2_geometry_msgs.hpp"
#include <tf2/utils.h>
#include "gz4d_geo.h"

namespace marine
{
  template <typename A> double quaternionToHeadingDegrees(const A& a)
  {
    return 90.0-180.0*tf2::getYaw(a)/M_PI;
  }

  template <typename A> double quaternionToHeadingRadians(const A& a)
  {
    return (M_PI/2.0)-tf2::getYaw(a);
  }

  template <typename T> inline double speedOverGround(const T & v)
  {
    tf2::Vector3 v3;
    tf2::fromMsg(v, v3);
    return v3.length();
  }
  
  using LatLongDegrees = gz4d::GeoPointLatLongDegrees;
  using ECEF = gz4d::GeoPointECEF;
  using Point = gz4d::Point<double>;
  using ENUFrame = gz4d::LocalENU;
  
  // angle types that do not wrap
  using AngleDegrees = gz4d::Angle<double, gz4d::pu::Degree, gz4d::rt::Unclamped>;
  using AngleRadians = gz4d::Angle<double, gz4d::pu::Radian, gz4d::rt::Unclamped>;
  
  // angle types that wrap at +/- half a circle
  using AngleDegreesZeroCentered = gz4d::Angle<double, gz4d::pu::Degree, gz4d::rt::ZeroCenteredPeriod>;
  using AngleRadiansZeroCentered = gz4d::Angle<double, gz4d::pu::Radian, gz4d::rt::ZeroCenteredPeriod>;
  
  // angle types that wrap at 0 and full circle
  using AngleDegreesPositive = gz4d::Angle<double, gz4d::pu::Degree, gz4d::rt::PositivePeriod>;
  using AngleRadiansPositive = gz4d::Angle<double, gz4d::pu::Radian, gz4d::rt::PositivePeriod>;
  
  using WGS84 = gz4d::geo::WGS84::Ellipsoid;
  
  template <typename A> void fromMsg(const A& a, LatLongDegrees &b)
  {
    b.latitude() = a.latitude;
    b.longitude() = a.longitude;
    b.altitude() = a.altitude;
  }

  template <typename B> void toMsg(const LatLongDegrees &a, B &b)
  {
    b.latitude = a.latitude();
    b.longitude = a.longitude();
    b.altitude = a.altitude();
  }

  template <typename A> void fromMsg(const A& a, Point &b)
  {
    b = Point(a.x, a.y, a.z);
  }

  template <typename B> void toMsg(const Point& a, B &b)
  {
    b.x = a[0];
    b.y = a[1];
    b.z = a[2];
  }
}

std::ostream& operator<< (std::ostream &out, const marine::LatLongDegrees &p);
std::ostream& operator<< (std::ostream &out, const marine::ECEF &p);
std::ostream& operator<< (std::ostream &out, const marine::AngleDegrees &p);
std::ostream& operator<< (std::ostream &out, const marine::AngleRadians &p);

#endif
