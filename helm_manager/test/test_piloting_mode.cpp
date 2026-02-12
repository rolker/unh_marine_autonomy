// Copyright 2026 University of New Hampshire
// SPDX-License-Identifier: BSD-3-Clause

/// Unit tests for the PilotingMode class.
/// PilotingMode manages per-mode subscriptions and publishes
/// an "active" flag when the mode matches the current piloting mode.

#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <chrono>
#include <thread>
#include <vector>

#include "rclcpp/rclcpp.hpp"
#include "rclcpp_lifecycle/lifecycle_node.hpp"
#include "lifecycle_msgs/msg/state.hpp"
#include "geometry_msgs/msg/twist_stamped.hpp"
#include "marine_interfaces/msg/helm.hpp"
#include "std_msgs/msg/string.hpp"
#include "std_msgs/msg/bool.hpp"

#include "../src/helm_manager.h"
#include "../src/piloting_mode.h"

using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// PilotingMode Unit Tests
// ---------------------------------------------------------------------------

class PilotingModeTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
    rclcpp::init(0, nullptr);
    // HelmManager is a LifecycleNode; PilotingMode requires a reference to it.
    node_ = std::make_shared<helm_manager::HelmManager>("test_pm_node");

    // We must configure the node to set up its infrastructure
    auto state = node_->configure();
    ASSERT_EQ(state.id(), lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE);
    state = node_->activate();
    ASSERT_EQ(state.id(), lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE);

    helper_ = std::make_shared<rclcpp::Node>("test_pm_helper");
  }

  void TearDown() override
  {
    helper_.reset();
    node_.reset();
    rclcpp::shutdown();
  }

  void spinBoth(std::chrono::milliseconds duration)
  {
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < duration) {
      rclcpp::spin_some(node_->get_node_base_interface());
      rclcpp::spin_some(helper_);
      std::this_thread::sleep_for(1ms);
    }
  }

  std::shared_ptr<helm_manager::HelmManager> node_;
  std::shared_ptr<rclcpp::Node> helper_;
};

TEST_F(PilotingModeTest, EnabledModeCreatesSubscriptions)
{
  // Create a piloting mode with enable=true
  auto pm = std::make_shared<helm_manager::PilotingMode>("test_enabled", *node_, true);

  spinBoth(50ms);

  // Check that helm and cmd_vel subscriptions exist for this mode
  auto topic_names = node_->get_topic_names_and_types();
  bool found_helm = false;
  bool found_twist = false;
  for (const auto & topic : topic_names) {
    if (topic.first.find("piloting_mode/test_enabled/helm") != std::string::npos) {
      found_helm = true;
    }
    if (topic.first.find("piloting_mode/test_enabled/cmd_vel") != std::string::npos) {
      found_twist = true;
    }
  }
  EXPECT_TRUE(found_helm) << "Enabled mode should have helm subscription";
  EXPECT_TRUE(found_twist) << "Enabled mode should have cmd_vel subscription";
}

TEST_F(PilotingModeTest, DisabledModeDoesNotCreateSubscriptions)
{
  // Create a piloting mode with enable=false (like standby)
  auto pm = std::make_shared<helm_manager::PilotingMode>("test_disabled", *node_, false);

  spinBoth(50ms);

  auto topic_names = node_->get_topic_names_and_types();
  bool found_helm = false;
  bool found_twist = false;
  for (const auto & topic : topic_names) {
    if (topic.first.find("piloting_mode/test_disabled/helm") != std::string::npos) {
      found_helm = true;
    }
    if (topic.first.find("piloting_mode/test_disabled/cmd_vel") != std::string::npos) {
      found_twist = true;
    }
  }
  EXPECT_FALSE(found_helm)
    << "Disabled mode should NOT have helm subscription";
  EXPECT_FALSE(found_twist)
    << "Disabled mode should NOT have cmd_vel subscription";
}

TEST_F(PilotingModeTest, ActivePublisherAlwaysCreated)
{
  // Both enabled and disabled modes should have an active publisher
  auto pm_enabled = std::make_shared<helm_manager::PilotingMode>(
    "test_en_active", *node_, true);
  auto pm_disabled = std::make_shared<helm_manager::PilotingMode>(
    "test_dis_active", *node_, false);

  spinBoth(50ms);

  auto topic_names = node_->get_topic_names_and_types();
  bool found_en_active = false;
  bool found_dis_active = false;
  for (const auto & topic : topic_names) {
    if (topic.first.find("piloting_mode/test_en_active/active") != std::string::npos) {
      found_en_active = true;
    }
    if (topic.first.find("piloting_mode/test_dis_active/active") != std::string::npos) {
      found_dis_active = true;
    }
  }
  EXPECT_TRUE(found_en_active)
    << "Enabled mode should have active publisher";
  EXPECT_TRUE(found_dis_active)
    << "Disabled mode should also have active publisher";
}

TEST_F(PilotingModeTest, ActiveModePublishesTrueWhenMatching)
{
  auto pm = std::make_shared<helm_manager::PilotingMode>("test_match", *node_, true);

  bool active_received = false;
  bool active_value = false;
  auto sub = helper_->create_subscription<std_msgs::msg::Bool>(
    "/test_pm_node/piloting_mode/test_match/active",
    rclcpp::QoS(1).transient_local(),
    [&](const std_msgs::msg::Bool::SharedPtr msg) {
      active_received = true;
      active_value = msg->data;
    });

  // Allow DDS discovery before publishing
  spinBoth(200ms);

  // Set active mode to matching
  pm->activeMode("test_match");
  spinBoth(200ms);

  EXPECT_TRUE(active_received) << "Active flag should be published";
  EXPECT_TRUE(active_value) << "Active should be true when mode matches";
}

TEST_F(PilotingModeTest, ActiveModePublishesFalseWhenNotMatching)
{
  auto pm = std::make_shared<helm_manager::PilotingMode>("test_nomatch", *node_, true);

  bool active_received = false;
  bool active_value = true;
  auto sub = helper_->create_subscription<std_msgs::msg::Bool>(
    "/test_pm_node/piloting_mode/test_nomatch/active",
    rclcpp::QoS(1).transient_local(),
    [&](const std_msgs::msg::Bool::SharedPtr msg) {
      active_received = true;
      active_value = msg->data;
    });

  // Allow DDS discovery before publishing
  spinBoth(200ms);

  // Set active mode to something different
  pm->activeMode("other_mode");
  spinBoth(200ms);

  EXPECT_TRUE(active_received) << "Active flag should be published";
  EXPECT_FALSE(active_value) << "Active should be false when mode does not match";
}

TEST_F(PilotingModeTest, ActiveModeToggle)
{
  auto pm = std::make_shared<helm_manager::PilotingMode>("test_toggle", *node_, true);

  std::vector<bool> active_values;
  auto sub = helper_->create_subscription<std_msgs::msg::Bool>(
    "/test_pm_node/piloting_mode/test_toggle/active",
    rclcpp::QoS(10).transient_local(),
    [&](const std_msgs::msg::Bool::SharedPtr msg) {
      active_values.push_back(msg->data);
    });

  // Allow DDS discovery before publishing
  spinBoth(200ms);

  // Toggle: activate, deactivate, activate
  pm->activeMode("test_toggle");
  spinBoth(100ms);
  pm->activeMode("other");
  spinBoth(100ms);
  pm->activeMode("test_toggle");
  spinBoth(200ms);

  ASSERT_GE(active_values.size(), 3u) << "Should have received at least 3 active flags";
  EXPECT_TRUE(active_values[0]);
  EXPECT_FALSE(active_values[1]);
  EXPECT_TRUE(active_values[2]);
}

TEST_F(PilotingModeTest, InactiveModeDoesNotForwardCommands)
{
  // Create an enabled piloting mode
  auto pm = std::make_shared<helm_manager::PilotingMode>("test_inactive_fwd", *node_, true);

  // Subscribe to helm output
  bool helm_received = false;
  auto helm_sub = helper_->create_subscription<marine_interfaces::msg::Helm>(
    "/test_pm_node/out/helm", 1,
    [&](const marine_interfaces::msg::Helm::SharedPtr) {
      helm_received = true;
    });

  // Publish a helm command to this mode's topic WITHOUT activating it
  auto helm_pub = helper_->create_publisher<marine_interfaces::msg::Helm>(
    "/test_pm_node/piloting_mode/test_inactive_fwd/helm", 10);

  spinBoth(50ms);

  marine_interfaces::msg::Helm cmd;
  cmd.throttle = 0.5;
  cmd.rudder = 0.3;
  helm_pub->publish(cmd);
  spinBoth(100ms);

  EXPECT_FALSE(helm_received)
    << "Inactive mode should NOT forward commands to output";
}

TEST_F(PilotingModeTest, ActiveModeForwardsHelmCommands)
{
  // Use the built-in manual mode (added during configure) rather than creating
  // a new PilotingMode, because canPublish checks piloting_mode_ which is set
  // via the piloting_mode topic callback.
  bool helm_received = false;
  marine_interfaces::msg::Helm last_helm;
  auto helm_sub = helper_->create_subscription<marine_interfaces::msg::Helm>(
    "/test_pm_node/out/helm", 1,
    [&](const marine_interfaces::msg::Helm::SharedPtr msg) {
      helm_received = true;
      last_helm = *msg;
    });

  auto mode_pub = helper_->create_publisher<std_msgs::msg::String>(
    "/test_pm_node/piloting_mode", 1);

  auto helm_pub = helper_->create_publisher<marine_interfaces::msg::Helm>(
    "/test_pm_node/piloting_mode/manual/helm", 10);

  // Allow DDS discovery
  spinBoth(200ms);

  // Set mode to manual
  std_msgs::msg::String mode_msg;
  mode_msg.data = "manual";
  mode_pub->publish(mode_msg);
  spinBoth(200ms);

  // Publish helm command
  marine_interfaces::msg::Helm cmd;
  cmd.throttle = 0.7;
  cmd.rudder = -0.4;
  helm_pub->publish(cmd);
  spinBoth(200ms);

  EXPECT_TRUE(helm_received) << "Active mode should forward helm commands";
  EXPECT_FLOAT_EQ(last_helm.throttle, 0.7f);
  EXPECT_FLOAT_EQ(last_helm.rudder, -0.4f);
}

TEST_F(PilotingModeTest, ActiveModeForwardsTwistCommands)
{
  // Use the built-in manual mode
  bool helm_received = false;
  marine_interfaces::msg::Helm last_helm;
  auto helm_sub = helper_->create_subscription<marine_interfaces::msg::Helm>(
    "/test_pm_node/out/helm", 1,
    [&](const marine_interfaces::msg::Helm::SharedPtr msg) {
      helm_received = true;
      last_helm = *msg;
    });

  auto mode_pub = helper_->create_publisher<std_msgs::msg::String>(
    "/test_pm_node/piloting_mode", 1);

  auto twist_pub = helper_->create_publisher<geometry_msgs::msg::TwistStamped>(
    "/test_pm_node/piloting_mode/manual/cmd_vel", 10);

  // Allow DDS discovery
  spinBoth(200ms);

  // Set mode to manual
  std_msgs::msg::String mode_msg;
  mode_msg.data = "manual";
  mode_pub->publish(mode_msg);
  spinBoth(200ms);

  // Publish twist command (with helm output mode, it converts to helm)
  geometry_msgs::msg::TwistStamped twist;
  twist.twist.linear.x = 0.5;
  twist.twist.angular.z = -0.3;
  twist_pub->publish(twist);
  spinBoth(200ms);

  EXPECT_TRUE(helm_received) << "Active mode should forward twist commands (as helm)";
  // throttle = linear.x/max_speed = 0.5/1.0 = 0.5
  // rudder = -angular.z/max_yaw_speed = -(-0.3)/1.0 = 0.3
  EXPECT_FLOAT_EQ(last_helm.throttle, 0.5f);
  EXPECT_FLOAT_EQ(last_helm.rudder, 0.3f);
}
