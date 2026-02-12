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

#include <string>

#include "piloting_mode.h"

namespace helm_manager
{

PilotingMode::PilotingMode(std::string mode, HelmManager & helm_manager, bool enable)
:piloting_mode_(mode),
  helm_manager_(helm_manager)
{
  rclcpp::QoS qos(1);
  qos.transient_local();

  active_publisher_ = helm_manager.create_publisher<std_msgs::msg::Bool>("piloting_mode/" + mode +
      "/active", qos);
  if(enable) {
    helm_subscription_ =
      helm_manager.create_subscription<marine_interfaces::msg::Helm>("piloting_mode/" + mode +
        "/helm", 10,
        std::bind(&PilotingMode::callback<marine_interfaces::msg::Helm const>, this,
        std::placeholders::_1));

    twist_subscription_ =
      helm_manager.create_subscription<geometry_msgs::msg::TwistStamped>("piloting_mode/" + mode +
        "/cmd_vel", 10,
        std::bind(&PilotingMode::callback<geometry_msgs::msg::TwistStamped const>, this,
        std::placeholders::_1));
  }
}

void PilotingMode::activeMode(std::string const & mode)
{
  active_ = (mode == piloting_mode_);
  std_msgs::msg::Bool active;
  active.data = active_;
  active_publisher_->publish(active);
}

}  // namespace helm_manager
