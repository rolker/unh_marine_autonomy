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

#include <algorithm>

#include "helm_manager.h"
#include "piloting_mode.h"

namespace helm_manager
{

HelmManager::HelmManager(const std::string & node_name, const rclcpp::NodeOptions & options)
:rclcpp_lifecycle::LifecycleNode(node_name, options)
{
}

CallbackReturn HelmManager::on_configure(const rclcpp_lifecycle::State & state)
{
  rclcpp_lifecycle::LifecycleNode::on_configure(state);

  addPilotingMode("standby", false);
  addPilotingMode("manual");
  addPilotingMode("autonomous");

  update_parameters_callback_ =
    add_post_set_parameters_callback(std::bind(&HelmManager::updateParameters, this,
      std::placeholders::_1));

  declare_parameter<std::string>("output_type", "helm");

  heartbeat_publisher_ = create_publisher<marine_interfaces::msg::Heartbeat>("heartbeat", 1);
  piloting_mode_subscription_ = create_subscription<std_msgs::msg::String>("piloting_mode", 1,
      std::bind(&HelmManager::pilotingModeCallback, this, std::placeholders::_1));
  declare_parameter<double>("max_speed", 1.0);
  declare_parameter<double>("max_yaw_speed", 1.0);
  max_speed_ = get_parameter("max_speed").as_double();
  max_yaw_speed_ = get_parameter("max_yaw_speed").as_double();

  helm_status_subscription_ = create_subscription<marine_interfaces::msg::Heartbeat>("status/helm",
      1, std::bind(&HelmManager::helmStatusCallback, this, std::placeholders::_1));

  output_type_ = get_parameter("output_type").as_string();


  if(output_type_ == "helm" || output_type_ == "dual") {
    helm_publisher_ = create_publisher<marine_interfaces::msg::Helm>("out/helm", 1);
  }

  if(output_type_ == "twist" || output_type_ == "dual") {
    twist_publisher_ = create_publisher<geometry_msgs::msg::TwistStamped>("out/cmd_vel", 1);
  }


  return CallbackReturn::SUCCESS;
}

CallbackReturn HelmManager::on_activate(const rclcpp_lifecycle::State & state)
{
  return rclcpp_lifecycle::LifecycleNode::on_activate(state);
}

CallbackReturn HelmManager::on_deactivate(const rclcpp_lifecycle::State & state)
{
  return rclcpp_lifecycle::LifecycleNode::on_deactivate(state);
}

CallbackReturn HelmManager::on_cleanup(const rclcpp_lifecycle::State & state)
{
  piloting_modes_.clear();
  update_parameters_callback_.reset();
  heartbeat_publisher_.reset();
  piloting_mode_subscription_.reset();
  helm_status_subscription_.reset();
  helm_publisher_.reset();
  twist_publisher_.reset();

  return rclcpp_lifecycle::LifecycleNode::on_cleanup(state);
}

CallbackReturn HelmManager::on_shutdown(const rclcpp_lifecycle::State & state)
{
  piloting_modes_.clear();
  update_parameters_callback_.reset();
  heartbeat_publisher_.reset();
  piloting_mode_subscription_.reset();
  helm_status_subscription_.reset();
  helm_publisher_.reset();
  twist_publisher_.reset();
  return rclcpp_lifecycle::LifecycleNode::on_shutdown(state);
}

void HelmManager::updateParameters(const std::vector<rclcpp::Parameter> & parameters)
{
  for(const auto & param: parameters) {
    if(param.get_name() == "max_speed") {
      max_speed_ = param.as_double();
    }
    if(param.get_name() == "max_yaw_speed") {
      max_yaw_speed_ = param.as_double();
    }
  }
}


bool HelmManager::canPublish(const std::string & mode)
{
  return mode == piloting_mode_;
}

void HelmManager::helmStatusCallback(const marine_interfaces::msg::Heartbeat & msg)
{
  marine_interfaces::msg::Heartbeat hb;
  hb.header = msg.header;

  marine_interfaces::msg::KeyValue kv;

  kv.key = "piloting_mode";
  kv.value = piloting_mode_;
  hb.values.push_back(kv);

  for(const auto & kv: msg.values) {
    hb.values.push_back(kv);
  }

  heartbeat_publisher_->publish(hb);
}

void HelmManager::update(const std::string & mode, const marine_interfaces::msg::Helm & msg)
{
  if(canPublish(mode)) {
    if(output_type_ == "helm" || output_type_ == "dual") {
      helm_publisher_->publish(msg);
    } else {
      geometry_msgs::msg::TwistStamped twist;
      twist.header = msg.header;
      twist.twist.linear.x = msg.throttle * max_speed_;
      twist.twist.linear.x = std::max(-max_speed_, std::min(max_speed_, twist.twist.linear.x));
      twist.twist.angular.z = -msg.rudder * max_yaw_speed_;
      twist.twist.angular.z = std::max(-max_yaw_speed_,
          std::min(max_yaw_speed_, twist.twist.angular.z));
      twist_publisher_->publish(twist);
    }
  }
}

void HelmManager::update(const std::string & mode, const geometry_msgs::msg::TwistStamped & msg)
{
  if(canPublish(mode)) {
    if(output_type_ == "twist" || output_type_ == "dual") {
      geometry_msgs::msg::TwistStamped twist_clamped = msg;
      twist_clamped.twist.linear.x = std::max(-max_speed_,
          std::min(max_speed_, twist_clamped.twist.linear.x));
      twist_clamped.twist.angular.z = std::max(-max_yaw_speed_,
          std::min(max_yaw_speed_, twist_clamped.twist.angular.z));
      twist_publisher_->publish(twist_clamped);
    } else {
      marine_interfaces::msg::Helm helm;
      helm.header = msg.header;
      if(std::isnan(msg.twist.linear.x)) {
        helm.throttle = 0;
      } else {
        helm.throttle = msg.twist.linear.x / max_speed_;
      }
      helm.throttle = std::max(-1.0, std::min(1.0, static_cast<double>(helm.throttle)));
      helm.rudder = -msg.twist.angular.z / max_yaw_speed_;
      helm.rudder = std::max(-1.0, std::min(1.0, static_cast<double>(helm.rudder)));
      helm_publisher_->publish(helm);
    }
  }
}

void HelmManager::addPilotingMode(const std::string & mode, bool enable_output)
{
  piloting_modes_.push_back(std::make_shared<PilotingMode>(mode, *this, enable_output));
}

void HelmManager::pilotingModeCallback(const std_msgs::msg::String & msg)
{
  piloting_mode_ = msg.data;
  for(auto & m: piloting_modes_) {
    m->activeMode(piloting_mode_);
  }
}


}  // namespace helm_manager
