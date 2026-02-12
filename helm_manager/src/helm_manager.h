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

#ifndef HELM_MANAGER_HELM_MANAGER_H
#define HELM_MANAGER_HELM_MANAGER_H

#include <memory>
#include <string>
#include <vector>

#include "rclcpp/rclcpp.hpp"
#include "rclcpp_lifecycle/lifecycle_node.hpp"
#include "geometry_msgs/msg/twist_stamped.hpp"
#include "marine_interfaces/msg/helm.hpp"
#include "std_msgs/msg/string.hpp"
#include "std_msgs/msg/bool.hpp"
#include "marine_interfaces/msg/heartbeat.hpp"

namespace helm_manager
{

  using CallbackReturn = rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn;

  class PilotingMode;

  class HelmManager: public rclcpp_lifecycle::LifecycleNode
  {
public:
    explicit HelmManager(
      const std::string & node_name,
      const rclcpp::NodeOptions & options = rclcpp::NodeOptions());

    CallbackReturn on_configure(const rclcpp_lifecycle::State & state) override;
    CallbackReturn on_activate(const rclcpp_lifecycle::State & state) override;
    CallbackReturn on_deactivate(const rclcpp_lifecycle::State & state) override;
    CallbackReturn on_cleanup(const rclcpp_lifecycle::State & state) override;
    CallbackReturn on_shutdown(const rclcpp_lifecycle::State & state) override;

    void update(const std::string & mode, const geometry_msgs::msg::TwistStamped & msg);
    void update(const std::string & mode, const marine_interfaces::msg::Helm & msg);

private:
    void addPilotingMode(const std::string & mode, bool enable_output = true);

    bool canPublish(const std::string & mode);

    void pilotingModeCallback(const std_msgs::msg::String & msg);

    void helmStatusCallback(const marine_interfaces::msg::Heartbeat & msg);

    void updateParameters(const std::vector < rclcpp::Parameter > &parameters);

    rclcpp::Subscription < std_msgs::msg::String > ::SharedPtr piloting_mode_subscription_;
    std::string piloting_mode_;

    rclcpp::Publisher < marine_interfaces::msg::Heartbeat > ::SharedPtr heartbeat_publisher_;
    rclcpp::Subscription < marine_interfaces::msg::Heartbeat >
    ::SharedPtr helm_status_subscription_;

    std::vector < std::shared_ptr < PilotingMode >> piloting_modes_;
    std::string output_type_;

    rclcpp::Publisher < marine_interfaces::msg::Helm > ::SharedPtr helm_publisher_;
    rclcpp::Publisher < geometry_msgs::msg::TwistStamped > ::SharedPtr twist_publisher_;

    double max_speed_ = 1.0;
    double max_yaw_speed_ = 1.0;

    rclcpp::node_interfaces::PostSetParametersCallbackHandle::SharedPtr update_parameters_callback_;
  };


}  // namespace helm_manager

#endif  // HELM_MANAGER_HELM_MANAGER_H
