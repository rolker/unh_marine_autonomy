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
