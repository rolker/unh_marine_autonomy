// Copyright 2026 University of New Hampshire
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
//    * Neither the name of the University of New Hampshire nor the names of its
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

/// Tests for helm/twist command conversion and value clamping logic.
/// The HelmManager converts between Helm (throttle/rudder) and
/// TwistStamped (linear.x / angular.z) depending on the output_type
/// parameter. These tests verify the conversion math and safety clamping.

#include <gtest/gtest.h>
#include <cmath>
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

#include "../src/helm_manager.h"

using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// Base fixture: output_type = "helm" (default)
// ---------------------------------------------------------------------------

class HelmOutputTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
    rclcpp::init(0, nullptr);
    node_ = std::make_shared<helm_manager::HelmManager>("test_helm_conv");
    helper_ = std::make_shared<rclcpp::Node>("test_conv_helper");

    mode_pub_ = helper_->create_publisher<std_msgs::msg::String>(
      "/piloting_mode", 1);

    helm_sub_ = helper_->create_subscription<marine_interfaces::msg::Helm>(
      "/out/helm", 1,
      [this](const marine_interfaces::msg::Helm::SharedPtr msg) {
        last_helm_ = *msg;
        helm_received_ = true;
      });

    manual_helm_pub_ = helper_->create_publisher<marine_interfaces::msg::Helm>(
      "/piloting_mode/manual/helm", 10);

    manual_twist_pub_ = helper_->create_publisher<geometry_msgs::msg::TwistStamped>(
      "/piloting_mode/manual/cmd_vel", 10);
  }

  void TearDown() override
  {
    mode_pub_.reset();
    helm_sub_.reset();
    manual_helm_pub_.reset();
    manual_twist_pub_.reset();
    helper_.reset();
    node_.reset();
    rclcpp::shutdown();
  }

  void configureAndActivate()
  {
    auto state = node_->configure();
    ASSERT_EQ(state.id(), lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE);
    state = node_->activate();
    ASSERT_EQ(state.id(), lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE);
  }

  void setManualMode()
  {
    std_msgs::msg::String msg;
    msg.data = "manual";
    mode_pub_->publish(msg);
    spinBoth(100ms);
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

  template<typename Predicate>
  bool spinUntil(Predicate pred, std::chrono::milliseconds timeout = 2000ms)
  {
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < timeout) {
      rclcpp::spin_some(node_->get_node_base_interface());
      rclcpp::spin_some(helper_);
      if (pred()) {
        return true;
      }
      std::this_thread::sleep_for(1ms);
    }
    return false;
  }

  std::shared_ptr<helm_manager::HelmManager> node_;
  std::shared_ptr<rclcpp::Node> helper_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr mode_pub_;
  rclcpp::Subscription<marine_interfaces::msg::Helm>::SharedPtr helm_sub_;
  rclcpp::Publisher<marine_interfaces::msg::Helm>::SharedPtr manual_helm_pub_;
  rclcpp::Publisher<geometry_msgs::msg::TwistStamped>::SharedPtr manual_twist_pub_;

  marine_interfaces::msg::Helm last_helm_;
  bool helm_received_ = false;
};

TEST_F(HelmOutputTest, HelmPassthroughInHelmMode)
{
  // Default output_type is "helm", so Helm messages should pass through
  configureAndActivate();
  setManualMode();

  marine_interfaces::msg::Helm cmd;
  cmd.throttle = 0.5;
  cmd.rudder = -0.3;
  manual_helm_pub_->publish(cmd);
  spinUntil([this] {return helm_received_;});

  ASSERT_TRUE(helm_received_);
  EXPECT_FLOAT_EQ(last_helm_.throttle, 0.5f);
  EXPECT_FLOAT_EQ(last_helm_.rudder, -0.3f);
}

TEST_F(HelmOutputTest, TwistToHelmConversion)
{
  // When output_type = "helm" and a TwistStamped arrives, it should be
  // converted: throttle = linear.x/max_speed, rudder = -angular.z/max_yaw_speed
  configureAndActivate();
  setManualMode();

  geometry_msgs::msg::TwistStamped twist;
  twist.twist.linear.x = 0.5;   // max_speed=1.0, so throttle = 0.5
  twist.twist.angular.z = -0.3;  // rudder = -(-0.3)/1.0 = 0.3
  manual_twist_pub_->publish(twist);
  spinUntil([this] {return helm_received_;});

  ASSERT_TRUE(helm_received_);
  EXPECT_FLOAT_EQ(last_helm_.throttle, 0.5f);
  EXPECT_FLOAT_EQ(last_helm_.rudder, 0.3f);
}

TEST_F(HelmOutputTest, TwistToHelmClampingThrottle)
{
  // Values exceeding max should be clamped to [-1, 1] for throttle/rudder
  configureAndActivate();
  setManualMode();

  geometry_msgs::msg::TwistStamped twist;
  twist.twist.linear.x = 5.0;   // exceeds max_speed=1.0, so throttle would be 5.0
  twist.twist.angular.z = 0.0;
  manual_twist_pub_->publish(twist);
  spinUntil([this] {return helm_received_;});

  ASSERT_TRUE(helm_received_);
  EXPECT_FLOAT_EQ(last_helm_.throttle, 1.0f)
    << "Throttle should be clamped to 1.0";
}

TEST_F(HelmOutputTest, TwistToHelmClampingRudder)
{
  configureAndActivate();
  setManualMode();

  geometry_msgs::msg::TwistStamped twist;
  twist.twist.linear.x = 0.0;
  twist.twist.angular.z = 5.0;  // rudder = -5.0/1.0 = -5.0, clamped to -1.0
  manual_twist_pub_->publish(twist);
  spinUntil([this] {return helm_received_;});

  ASSERT_TRUE(helm_received_);
  EXPECT_FLOAT_EQ(last_helm_.rudder, -1.0f)
    << "Rudder should be clamped to -1.0";
}

TEST_F(HelmOutputTest, TwistToHelmNegativeClamping)
{
  configureAndActivate();
  setManualMode();

  geometry_msgs::msg::TwistStamped twist;
  twist.twist.linear.x = -5.0;   // throttle = -5.0, clamped to -1.0
  twist.twist.angular.z = -5.0;   // rudder = 5.0, clamped to 1.0
  manual_twist_pub_->publish(twist);
  spinUntil([this] {return helm_received_;});

  ASSERT_TRUE(helm_received_);
  EXPECT_FLOAT_EQ(last_helm_.throttle, -1.0f)
    << "Throttle should be clamped to -1.0";
  EXPECT_FLOAT_EQ(last_helm_.rudder, 1.0f)
    << "Rudder should be clamped to 1.0";
}

TEST_F(HelmOutputTest, TwistToHelmNanHandling)
{
  // If linear.x is NaN, throttle should be 0
  configureAndActivate();
  setManualMode();

  geometry_msgs::msg::TwistStamped twist;
  twist.twist.linear.x = std::nan("");
  twist.twist.angular.z = 0.5;
  manual_twist_pub_->publish(twist);
  spinUntil([this] {return helm_received_;});

  ASSERT_TRUE(helm_received_);
  EXPECT_FLOAT_EQ(last_helm_.throttle, 0.0f)
    << "NaN linear.x should result in zero throttle";
}

TEST_F(HelmOutputTest, ZeroCommandPassthrough)
{
  configureAndActivate();
  setManualMode();

  marine_interfaces::msg::Helm cmd;
  cmd.throttle = 0.0;
  cmd.rudder = 0.0;
  manual_helm_pub_->publish(cmd);
  spinUntil([this] {return helm_received_;});

  ASSERT_TRUE(helm_received_);
  EXPECT_FLOAT_EQ(last_helm_.throttle, 0.0f);
  EXPECT_FLOAT_EQ(last_helm_.rudder, 0.0f);
}

TEST_F(HelmOutputTest, FullReverseThrottle)
{
  configureAndActivate();
  setManualMode();

  marine_interfaces::msg::Helm cmd;
  cmd.throttle = -1.0;
  cmd.rudder = 0.0;
  manual_helm_pub_->publish(cmd);
  spinUntil([this] {return helm_received_;});

  ASSERT_TRUE(helm_received_);
  EXPECT_FLOAT_EQ(last_helm_.throttle, -1.0f);
}

// ---------------------------------------------------------------------------
// Twist output mode tests
// ---------------------------------------------------------------------------

class TwistOutputTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
    rclcpp::init(0, nullptr);

    // Override the output_type parameter before configure
    rclcpp::NodeOptions options;
    options.parameter_overrides({
      rclcpp::Parameter("output_type", "twist"),
      rclcpp::Parameter("max_speed", 2.0),
      rclcpp::Parameter("max_yaw_speed", 1.5)
    });
    node_ = std::make_shared<helm_manager::HelmManager>("test_twist_out", options);
    helper_ = std::make_shared<rclcpp::Node>("test_twist_helper");

    mode_pub_ = helper_->create_publisher<std_msgs::msg::String>(
      "/piloting_mode", 1);

    twist_sub_ = helper_->create_subscription<geometry_msgs::msg::TwistStamped>(
      "/out/cmd_vel", 1,
      [this](const geometry_msgs::msg::TwistStamped::SharedPtr msg) {
        last_twist_ = *msg;
        twist_received_ = true;
      });

    manual_helm_pub_ = helper_->create_publisher<marine_interfaces::msg::Helm>(
      "/piloting_mode/manual/helm", 10);

    manual_twist_pub_ = helper_->create_publisher<geometry_msgs::msg::TwistStamped>(
      "/piloting_mode/manual/cmd_vel", 10);
  }

  void TearDown() override
  {
    mode_pub_.reset();
    twist_sub_.reset();
    manual_helm_pub_.reset();
    manual_twist_pub_.reset();
    helper_.reset();
    node_.reset();
    rclcpp::shutdown();
  }

  void configureAndActivate()
  {
    auto state = node_->configure();
    ASSERT_EQ(state.id(), lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE);
    state = node_->activate();
    ASSERT_EQ(state.id(), lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE);
  }

  void setManualMode()
  {
    std_msgs::msg::String msg;
    msg.data = "manual";
    mode_pub_->publish(msg);
    spinBoth(100ms);
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

  template<typename Predicate>
  bool spinUntil(Predicate pred, std::chrono::milliseconds timeout = 2000ms)
  {
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < timeout) {
      rclcpp::spin_some(node_->get_node_base_interface());
      rclcpp::spin_some(helper_);
      if (pred()) {
        return true;
      }
      std::this_thread::sleep_for(1ms);
    }
    return false;
  }

  std::shared_ptr<helm_manager::HelmManager> node_;
  std::shared_ptr<rclcpp::Node> helper_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr mode_pub_;
  rclcpp::Subscription<geometry_msgs::msg::TwistStamped>::SharedPtr twist_sub_;
  rclcpp::Publisher<marine_interfaces::msg::Helm>::SharedPtr manual_helm_pub_;
  rclcpp::Publisher<geometry_msgs::msg::TwistStamped>::SharedPtr manual_twist_pub_;

  geometry_msgs::msg::TwistStamped last_twist_;
  bool twist_received_ = false;
};

TEST_F(TwistOutputTest, HelmToTwistConversion)
{
  // output_type=twist, max_speed=2.0, max_yaw_speed=1.5
  // Helm(throttle=0.5, rudder=0.3) -> Twist(linear.x = 0.5*2.0 = 1.0,
  //                                         angular.z = -0.3*1.5 = -0.45)
  configureAndActivate();
  setManualMode();

  marine_interfaces::msg::Helm helm_cmd;
  helm_cmd.throttle = 0.5;
  helm_cmd.rudder = 0.3;
  manual_helm_pub_->publish(helm_cmd);
  spinUntil([this] {return twist_received_;});

  ASSERT_TRUE(twist_received_);
  EXPECT_NEAR(last_twist_.twist.linear.x, 1.0, 0.001);
  EXPECT_NEAR(last_twist_.twist.angular.z, -0.45, 0.001);
}

TEST_F(TwistOutputTest, HelmToTwistClamping)
{
  // max_speed=2.0. Throttle=1.0 -> linear.x = 2.0 (at max).
  // But the code clamps to max_speed, so 1.0*2.0 = 2.0 is exactly max.
  configureAndActivate();
  setManualMode();

  marine_interfaces::msg::Helm helm_cmd;
  helm_cmd.throttle = 1.0;
  helm_cmd.rudder = 1.0;
  manual_helm_pub_->publish(helm_cmd);
  spinUntil([this] {return twist_received_;});

  ASSERT_TRUE(twist_received_);
  EXPECT_NEAR(last_twist_.twist.linear.x, 2.0, 0.001)
    << "linear.x should be max_speed when throttle=1.0";
  EXPECT_NEAR(last_twist_.twist.angular.z, -1.5, 0.001)
    << "angular.z should be -max_yaw_speed when rudder=1.0";
}

TEST_F(TwistOutputTest, TwistPassthroughInTwistMode)
{
  // Twist input -> twist output should be a clamped passthrough
  configureAndActivate();
  setManualMode();

  geometry_msgs::msg::TwistStamped twist;
  twist.twist.linear.x = 1.5;
  twist.twist.angular.z = -0.8;
  manual_twist_pub_->publish(twist);
  spinUntil([this] {return twist_received_;});

  ASSERT_TRUE(twist_received_);
  EXPECT_NEAR(last_twist_.twist.linear.x, 1.5, 0.001);
  EXPECT_NEAR(last_twist_.twist.angular.z, -0.8, 0.001);
}

TEST_F(TwistOutputTest, TwistClampedToMaxSpeed)
{
  // max_speed=2.0, input 5.0 -> should be clamped to 2.0
  configureAndActivate();
  setManualMode();

  geometry_msgs::msg::TwistStamped twist;
  twist.twist.linear.x = 5.0;
  twist.twist.angular.z = 0.0;
  manual_twist_pub_->publish(twist);
  spinUntil([this] {return twist_received_;});

  ASSERT_TRUE(twist_received_);
  EXPECT_NEAR(last_twist_.twist.linear.x, 2.0, 0.001)
    << "linear.x should be clamped to max_speed";
}

TEST_F(TwistOutputTest, TwistClampedToMaxYawSpeed)
{
  // max_yaw_speed=1.5, input 5.0 -> should be clamped to 1.5
  configureAndActivate();
  setManualMode();

  geometry_msgs::msg::TwistStamped twist;
  twist.twist.linear.x = 0.0;
  twist.twist.angular.z = 5.0;
  manual_twist_pub_->publish(twist);
  spinUntil([this] {return twist_received_;});

  ASSERT_TRUE(twist_received_);
  EXPECT_NEAR(last_twist_.twist.angular.z, 1.5, 0.001)
    << "angular.z should be clamped to max_yaw_speed";
}

TEST_F(TwistOutputTest, TwistNegativeClampedToNegativeMax)
{
  configureAndActivate();
  setManualMode();

  geometry_msgs::msg::TwistStamped twist;
  twist.twist.linear.x = -10.0;
  twist.twist.angular.z = -10.0;
  manual_twist_pub_->publish(twist);
  spinUntil([this] {return twist_received_;});

  ASSERT_TRUE(twist_received_);
  EXPECT_NEAR(last_twist_.twist.linear.x, -2.0, 0.001);
  EXPECT_NEAR(last_twist_.twist.angular.z, -1.5, 0.001);
}

// ---------------------------------------------------------------------------
// Parameter update tests
// ---------------------------------------------------------------------------

class ParameterUpdateTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
    rclcpp::init(0, nullptr);

    rclcpp::NodeOptions options;
    options.parameter_overrides({
      rclcpp::Parameter("output_type", "twist"),
      rclcpp::Parameter("max_speed", 1.0),
      rclcpp::Parameter("max_yaw_speed", 1.0)
    });
    node_ = std::make_shared<helm_manager::HelmManager>("test_param_update", options);
    helper_ = std::make_shared<rclcpp::Node>("test_param_helper");

    mode_pub_ = helper_->create_publisher<std_msgs::msg::String>(
      "/piloting_mode", 1);

    twist_sub_ = helper_->create_subscription<geometry_msgs::msg::TwistStamped>(
      "/out/cmd_vel", 1,
      [this](const geometry_msgs::msg::TwistStamped::SharedPtr msg) {
        last_twist_ = *msg;
        twist_received_ = true;
      });

    manual_helm_pub_ = helper_->create_publisher<marine_interfaces::msg::Helm>(
      "/piloting_mode/manual/helm", 10);
  }

  void TearDown() override
  {
    mode_pub_.reset();
    twist_sub_.reset();
    manual_helm_pub_.reset();
    helper_.reset();
    node_.reset();
    rclcpp::shutdown();
  }

  void configureAndActivate()
  {
    auto state = node_->configure();
    ASSERT_EQ(state.id(), lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE);
    state = node_->activate();
    ASSERT_EQ(state.id(), lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE);
  }

  void setManualMode()
  {
    std_msgs::msg::String msg;
    msg.data = "manual";
    mode_pub_->publish(msg);
    spinBoth(100ms);
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

  template<typename Predicate>
  bool spinUntil(Predicate pred, std::chrono::milliseconds timeout = 2000ms)
  {
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < timeout) {
      rclcpp::spin_some(node_->get_node_base_interface());
      rclcpp::spin_some(helper_);
      if (pred()) {
        return true;
      }
      std::this_thread::sleep_for(1ms);
    }
    return false;
  }

  std::shared_ptr<helm_manager::HelmManager> node_;
  std::shared_ptr<rclcpp::Node> helper_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr mode_pub_;
  rclcpp::Subscription<geometry_msgs::msg::TwistStamped>::SharedPtr twist_sub_;
  rclcpp::Publisher<marine_interfaces::msg::Helm>::SharedPtr manual_helm_pub_;

  geometry_msgs::msg::TwistStamped last_twist_;
  bool twist_received_ = false;
};

TEST_F(ParameterUpdateTest, MaxSpeedUpdateAffectsConversion)
{
  configureAndActivate();
  setManualMode();

  // Initial max_speed=1.0: throttle 0.5 -> linear.x = 0.5
  marine_interfaces::msg::Helm cmd;
  cmd.throttle = 0.5;
  cmd.rudder = 0.0;
  manual_helm_pub_->publish(cmd);
  spinUntil([this] {return twist_received_;});

  ASSERT_TRUE(twist_received_);
  EXPECT_NEAR(last_twist_.twist.linear.x, 0.5, 0.001);

  // Update max_speed to 3.0
  twist_received_ = false;
  node_->set_parameter(rclcpp::Parameter("max_speed", 3.0));
  spinBoth(50ms);

  // Now throttle 0.5 -> linear.x = 0.5 * 3.0 = 1.5
  manual_helm_pub_->publish(cmd);
  spinUntil([this] {return twist_received_;});

  ASSERT_TRUE(twist_received_);
  EXPECT_NEAR(last_twist_.twist.linear.x, 1.5, 0.001)
    << "After updating max_speed to 3.0, linear.x should be 1.5";
}

TEST_F(ParameterUpdateTest, MaxYawSpeedUpdateAffectsConversion)
{
  configureAndActivate();
  setManualMode();

  // Update max_yaw_speed to 2.0
  node_->set_parameter(rclcpp::Parameter("max_yaw_speed", 2.0));
  spinBoth(50ms);

  marine_interfaces::msg::Helm cmd;
  cmd.throttle = 0.0;
  cmd.rudder = 0.5;   // angular.z = -0.5 * 2.0 = -1.0
  manual_helm_pub_->publish(cmd);
  spinUntil([this] {return twist_received_;});

  ASSERT_TRUE(twist_received_);
  EXPECT_NEAR(last_twist_.twist.angular.z, -1.0, 0.001);
}
