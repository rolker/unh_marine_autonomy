#include "piloting_mode.h"

namespace helm_manager
{

PilotingMode::PilotingMode(std::string mode, HelmManager &helm_manager, bool enable):
    piloting_mode_(mode),
    helm_manager_(helm_manager)
{
  rclcpp::QoS qos(1);
  qos.transient_local();
  
  active_publisher_ = helm_manager.create_publisher<std_msgs::msg::Bool>("piloting_mode/"+mode+"/active", qos);
  if(enable)
  {
    helm_subscription_ = helm_manager.create_subscription<marine_interfaces::msg::Helm>("piloting_mode/"+mode+"/helm", 10, std::bind(&PilotingMode::callback<marine_interfaces::msg::Helm const>, this, std::placeholders::_1));

    twist_subscription_ = helm_manager.create_subscription<geometry_msgs::msg::TwistStamped>("piloting_mode/"+mode+"/cmd_vel", 10, std::bind(&PilotingMode::callback<geometry_msgs::msg::TwistStamped const>, this, std::placeholders::_1));
  }
}

void PilotingMode::activeMode(std::string const &mode)
{
  active_ = (mode == piloting_mode_);
  std_msgs::msg::Bool active;
  active.data = active_;
  active_publisher_->publish(active);
}

} // namespace helm_manager
