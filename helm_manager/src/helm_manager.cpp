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
#include <memory>
#include <string>
#include <tuple>
#include <vector>

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

  declareOnce<std::string>("output_type", "helm");

  // Optional startup mode. Empty (the default) keeps the real-boat behaviour:
  // no mode until one arrives on the piloting_mode topic. The value is
  // validated against the modes added above when the node is activated.
  rcl_interfaces::msg::ParameterDescriptor initial_mode_descriptor;
  // Read-only: the value is consumed once at activation, so a runtime change
  // could only be silently ignored or fail a later activation. Launch/YAML
  // overrides still apply at declaration.
  initial_mode_descriptor.read_only = true;
  initial_mode_descriptor.description =
    "Piloting mode applied once on activation, as if it had arrived on the "
    "piloting_mode topic. Must name a configured mode (standby, manual, "
    "autonomous) or activation fails. Empty (default) starts with no mode. "
    "Read-only; set it in the launch file or parameter YAML.";
  declareReadOnlyString("initial_piloting_mode", "", initial_mode_descriptor);
  initial_piloting_mode_applied_ = false;

  heartbeat_publisher_ = create_publisher<marine_interfaces::msg::Heartbeat>("heartbeat", 1);
  piloting_mode_subscription_ = create_subscription<std_msgs::msg::String>("piloting_mode", 1,
      std::bind(&HelmManager::pilotingModeCallback, this, std::placeholders::_1));
  declareOnce<double>("max_speed", 1.0);
  declareOnce<double>("max_yaw_speed", 1.0);
  max_speed_ = get_parameter("max_speed").as_double();
  max_yaw_speed_ = get_parameter("max_yaw_speed").as_double();

  // Curvature-preserving speed regulation (ADR-0012, #292): per-platform
  // capability envelope; default off so existing platforms are unaffected.
  declareOnce<bool>("capability_curve_enabled", false);
  declareOnce<std::vector<double>>(
    "capability_curve_v_omega_max", std::vector<double>());
  declareOnce<double>("capability_curve_margin", 0.8);
  declareOnce<double>("capability_curve_pivot_speed", 0.05);
  loadCurvatureConfig();

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
  const std::string initial_mode = get_parameter("initial_piloting_mode").as_string();

  // Validate before activating anything: a typo in a launch file must stop
  // activation (the node stays inactive) rather than silently leave the boat
  // with no mode.
  if(!initial_mode.empty() && !hasPilotingMode(initial_mode)) {
    std::string known;
    for(const auto & m: piloting_modes_) {
      known += (known.empty() ? "" : ", ") + m->name();
    }
    RCLCPP_ERROR(get_logger(),
      "initial_piloting_mode '%s' names no configured piloting mode (configured: %s); "
      "refusing to activate",
      initial_mode.c_str(), known.c_str());
    return CallbackReturn::FAILURE;
  }

  auto result = rclcpp_lifecycle::LifecycleNode::on_activate(state);
  if(result != CallbackReturn::SUCCESS) {
    return result;
  }

  // A mode already chosen on the piloting_mode topic (even before activation)
  // wins: the callback marks the initial mode as consumed. Both sources go
  // through setPilotingMode(), so the "active" flags and the heartbeat agree.
  // (The per-mode "active" publishers are stored as plain rclcpp::Publisher,
  // so they publish regardless of lifecycle state; a topic message received
  // while inactive is therefore already reflected and needs no replay here.)
  if(!initial_mode.empty() && !initial_piloting_mode_applied_) {
    RCLCPP_INFO(get_logger(), "Applying initial_piloting_mode '%s'", initial_mode.c_str());
    setPilotingMode(initial_mode);
  }
  initial_piloting_mode_applied_ = true;
  return result;
}

CallbackReturn HelmManager::on_deactivate(const rclcpp_lifecycle::State & state)
{
  return rclcpp_lifecycle::LifecycleNode::on_deactivate(state);
}

CallbackReturn HelmManager::on_cleanup(const rclcpp_lifecycle::State & state)
{
  // A fresh configure cycle starts with no mode, as after construction.
  piloting_mode_.clear();
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

template<typename T>
void HelmManager::declareOnce(
  const std::string & name, const T & default_value,
  const rcl_interfaces::msg::ParameterDescriptor & descriptor)
{
  // Parameters outlive cleanup, so a second configure must not re-declare.
  if(!has_parameter(name)) {
    declare_parameter<T>(name, default_value, descriptor);
  }
}

void HelmManager::declareReadOnlyString(
  const std::string & name, const std::string & default_value,
  const rcl_interfaces::msg::ParameterDescriptor & descriptor)
{
  if(has_parameter(name) && !describe_parameter(name).read_only) {
    // Declared earlier with the default writable descriptor, which declareOnce
    // cannot change: NodeOptions::automatically_declare_parameters_from_overrides
    // does this for every override before on_configure runs. Undeclare it and
    // declare it again read-only, keeping the value it was given.
    const std::string value = get_parameter(name).as_string();
    undeclare_parameter(name);
    declare_parameter<std::string>(name, value, descriptor);
    return;
  }
  declareOnce<std::string>(name, default_value, descriptor);
}

void HelmManager::updateParameters(const std::vector<rclcpp::Parameter> & parameters)
{
  bool curvature_touched = false;
  for(const auto & param: parameters) {
    if(param.get_name() == "max_speed") {
      max_speed_ = param.as_double();
    }
    if(param.get_name() == "max_yaw_speed") {
      max_yaw_speed_ = param.as_double();
    }
    if(param.get_name().rfind("capability_curve_", 0) == 0) {
      curvature_touched = true;
    }
  }
  if(curvature_touched) {
    loadCurvatureConfig();
  }
}

void HelmManager::loadCurvatureConfig()
{
  // The post-set-parameter callback fires during each declare_parameter in
  // on_configure — before the full set exists. Load only once all four are
  // declared (the explicit loadCurvatureConfig() call at the end of the
  // declares performs the first real load).
  if(!has_parameter("capability_curve_enabled") ||
    !has_parameter("capability_curve_v_omega_max") ||
    !has_parameter("capability_curve_margin") ||
    !has_parameter("capability_curve_pivot_speed"))
  {
    return;
  }

  CurvatureRegulationConfig config;
  config.enabled = get_parameter("capability_curve_enabled").as_bool();
  config.curve = get_parameter("capability_curve_v_omega_max").as_double_array();
  config.margin = get_parameter("capability_curve_margin").as_double();
  config.pivot_speed = get_parameter("capability_curve_pivot_speed").as_double();

  std::string error;
  if(!validateCurvatureConfig(config, error)) {
    // Fail-safe: refuse the bad envelope rather than guess with it; the
    // independent max_speed/max_yaw_speed clamps remain in effect.
    RCLCPP_ERROR(get_logger(),
      "capability curve rejected, curvature regulation disabled: %s",
      error.c_str());
    config.enabled = false;
  }
  std::lock_guard<std::mutex> lock(curvature_config_mutex_);
  curvature_config_ = config;
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
    // Regulate once, before the output-type branch, so both the twist
    // output and the twist->helm conversion carry regulated values
    // (ADR-0012). The max clamps below stay as the backstop. Copy the
    // config under the lock (small: a handful of doubles) so the
    // parameter-callback writer can never tear a read mid-command.
    CurvatureRegulationConfig config;
    {
      std::lock_guard<std::mutex> lock(curvature_config_mutex_);
      config = curvature_config_;
    }
    geometry_msgs::msg::TwistStamped regulated = msg;
    std::tie(regulated.twist.linear.x, regulated.twist.angular.z) =
      applyCurvatureRegulation(msg.twist.linear.x, msg.twist.angular.z,
        config);

    if(output_type_ == "twist" || output_type_ == "dual") {
      geometry_msgs::msg::TwistStamped twist_clamped = regulated;
      twist_clamped.twist.linear.x = std::max(-max_speed_,
          std::min(max_speed_, twist_clamped.twist.linear.x));
      twist_clamped.twist.angular.z = std::max(-max_yaw_speed_,
          std::min(max_yaw_speed_, twist_clamped.twist.angular.z));
      twist_publisher_->publish(twist_clamped);
    } else {
      marine_interfaces::msg::Helm helm;
      helm.header = regulated.header;
      if(std::isnan(regulated.twist.linear.x)) {
        helm.throttle = 0;
      } else {
        helm.throttle = regulated.twist.linear.x / max_speed_;
      }
      helm.throttle = std::max(-1.0, std::min(1.0, static_cast<double>(helm.throttle)));
      helm.rudder = -regulated.twist.angular.z / max_yaw_speed_;
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
  // An explicit mode from the topic always outranks initial_piloting_mode.
  initial_piloting_mode_applied_ = true;
  setPilotingMode(msg.data);
}

void HelmManager::setPilotingMode(const std::string & mode)
{
  piloting_mode_ = mode;
  for(auto & m: piloting_modes_) {
    m->activeMode(piloting_mode_);
  }
}

bool HelmManager::hasPilotingMode(const std::string & mode) const
{
  return std::any_of(piloting_modes_.begin(), piloting_modes_.end(),
           [&mode](const std::shared_ptr<PilotingMode> & m) {return m->name() == mode;});
}


}  // namespace helm_manager
