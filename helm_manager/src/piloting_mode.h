// Copyright 2021 Roland Arsenault, University of New Hampshire
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
//    * Neither the name of the Roland Arsenault, University of New Hampshire nor the names of its
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

#ifndef HELM_MANAGER_PILOTING_MODE_H
#define HELM_MANAGER_PILOTING_MODE_H

#include <string>

#include <rclcpp/rclcpp.hpp>
#include "helm_manager.h"

namespace helm_manager
{

/// Listens to command topics for a given piloting mode
  class PilotingMode
  {
public:
    PilotingMode(std::string mode, HelmManager & helm_manager, bool enable = true);

    void activeMode(std::string const & mode);

private:
    template < typename T > void callback(const T & msg)
    {
      if(active_) {
        helm_manager_.update(piloting_mode_, msg);
      }
    }

    std::string piloting_mode_;
    bool active_ = false;
    rclcpp::Publisher < std_msgs::msg::Bool > ::SharedPtr active_publisher_;
    rclcpp::Subscription < marine_interfaces::msg::Helm > ::SharedPtr helm_subscription_;
    rclcpp::Subscription < geometry_msgs::msg::TwistStamped > ::SharedPtr twist_subscription_;
    HelmManager & helm_manager_;
  };

}  // namespace helm_manager

#endif  // HELM_MANAGER_PILOTING_MODE_H
