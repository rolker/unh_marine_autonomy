#ifndef HELM_MANAGER_PILOTING_MODE_H
#define HELM_MANAGER_PILOTING_MODE_H

#include <rclcpp/rclcpp.hpp>
#include "helm_manager.h"

namespace helm_manager
{

/// Listens to command topics for a given piloting mode
class PilotingMode
{
public:
  PilotingMode(std::string mode, HelmManager &helm_manager, bool enable=true);
  
  void activeMode(std::string const &mode);
  
private:
  template<typename T> void callback(const T& msg)
  {
    if(active_)
      helm_manager_.update(piloting_mode_, msg);
  }

  std::string piloting_mode_;
  bool active_ = false;
  rclcpp::Publisher<std_msgs::msg::Bool>::SharedPtr active_publisher_;
  rclcpp::Subscription<marine_interfaces::msg::Helm>::SharedPtr helm_subscription_;
  rclcpp::Subscription<geometry_msgs::msg::TwistStamped>::SharedPtr twist_subscription_;
  HelmManager& helm_manager_;
};

}

#endif
