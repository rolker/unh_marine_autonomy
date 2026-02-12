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

#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <chrono>
#include <thread>

#include "rclcpp/rclcpp.hpp"
#include "rclcpp_lifecycle/lifecycle_node.hpp"
#include "lifecycle_msgs/msg/state.hpp"
#include "geometry_msgs/msg/twist_stamped.hpp"
#include "marine_interfaces/msg/helm.hpp"
#include "marine_interfaces/msg/heartbeat.hpp"
#include "marine_interfaces/msg/key_value.hpp"
#include "std_msgs/msg/string.hpp"
#include "std_msgs/msg/bool.hpp"

#include "../src/helm_manager.h"

using namespace std::chrono_literals;

class HelmManagerTestFixture : public ::testing::Test
{
protected:
  void SetUp() override
  {
    rclcpp::init(0, nullptr);
    node_ = std::make_shared<helm_manager::HelmManager>("test_helm_manager");
  }

  void TearDown() override
  {
    node_.reset();
    rclcpp::shutdown();
  }

  /// Transition the node through configure and activate.
  void configureAndActivate()
  {
    auto state = node_->configure();
    ASSERT_EQ(
      state.id(),
      lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE);

    state = node_->activate();
    ASSERT_EQ(
      state.id(),
      lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE);
  }

  /// Spin the node for a given duration to process callbacks.
  void spinFor(std::chrono::milliseconds duration)
  {
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < duration) {
      rclcpp::spin_some(node_->get_node_base_interface());
      std::this_thread::sleep_for(1ms);
    }
  }

  std::shared_ptr<helm_manager::HelmManager> node_;
};

// ---------------------------------------------------------------------------
// Lifecycle Tests
// ---------------------------------------------------------------------------

TEST_F(HelmManagerTestFixture, NodeConstructionSucceeds)
{
  ASSERT_NE(node_, nullptr);
  EXPECT_EQ(
    node_->get_current_state().id(),
    lifecycle_msgs::msg::State::PRIMARY_STATE_UNCONFIGURED);
}

TEST_F(HelmManagerTestFixture, ConfigureSucceeds)
{
  auto state = node_->configure();
  EXPECT_EQ(state.id(), lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE);
}

TEST_F(HelmManagerTestFixture, ActivateSucceeds)
{
  auto state = node_->configure();
  ASSERT_EQ(state.id(), lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE);

  state = node_->activate();
  EXPECT_EQ(state.id(), lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE);
}

TEST_F(HelmManagerTestFixture, DeactivateSucceeds)
{
  configureAndActivate();

  auto state = node_->deactivate();
  EXPECT_EQ(state.id(), lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE);
}

TEST_F(HelmManagerTestFixture, CleanupSucceeds)
{
  auto state = node_->configure();
  ASSERT_EQ(state.id(), lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE);

  state = node_->cleanup();
  EXPECT_EQ(
    state.id(),
    lifecycle_msgs::msg::State::PRIMARY_STATE_UNCONFIGURED);
}

TEST_F(HelmManagerTestFixture, ShutdownFromInactiveSucceeds)
{
  auto state = node_->configure();
  ASSERT_EQ(state.id(), lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE);

  state = node_->shutdown();
  EXPECT_EQ(
    state.id(),
    lifecycle_msgs::msg::State::PRIMARY_STATE_FINALIZED);
}

TEST_F(HelmManagerTestFixture, ShutdownFromActiveSucceeds)
{
  configureAndActivate();

  auto state = node_->shutdown();
  EXPECT_EQ(
    state.id(),
    lifecycle_msgs::msg::State::PRIMARY_STATE_FINALIZED);
}

TEST_F(HelmManagerTestFixture, FullLifecycleRoundTrip)
{
  // unconfigured -> inactive -> active -> inactive -> unconfigured
  auto state = node_->configure();
  ASSERT_EQ(state.id(), lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE);

  state = node_->activate();
  ASSERT_EQ(state.id(), lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE);

  state = node_->deactivate();
  ASSERT_EQ(state.id(), lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE);

  state = node_->cleanup();
  EXPECT_EQ(
    state.id(),
    lifecycle_msgs::msg::State::PRIMARY_STATE_UNCONFIGURED);
}

// ---------------------------------------------------------------------------
// Parameter Tests
// ---------------------------------------------------------------------------

TEST_F(HelmManagerTestFixture, DefaultOutputTypeIsHelm)
{
  node_->configure();
  auto output_type = node_->get_parameter("output_type").as_string();
  EXPECT_EQ(output_type, "helm");
}

TEST_F(HelmManagerTestFixture, DefaultMaxSpeedIsOne)
{
  node_->configure();
  auto max_speed = node_->get_parameter("max_speed").as_double();
  EXPECT_DOUBLE_EQ(max_speed, 1.0);
}

TEST_F(HelmManagerTestFixture, DefaultMaxYawSpeedIsOne)
{
  node_->configure();
  auto max_yaw_speed = node_->get_parameter("max_yaw_speed").as_double();
  EXPECT_DOUBLE_EQ(max_yaw_speed, 1.0);
}

// ---------------------------------------------------------------------------
// Mode Switching Tests (via topic)
// ---------------------------------------------------------------------------

TEST_F(HelmManagerTestFixture, PilotingModeActivePublishers)
{
  // After configure, there should be active publishers for each mode
  configureAndActivate();

  // Verify by checking that the topics exist
  auto topic_names = node_->get_topic_names_and_types();
  bool found_standby = false;
  bool found_manual = false;
  bool found_autonomous = false;

  for (const auto & topic : topic_names) {
    if (topic.first.find("piloting_mode/standby/active") != std::string::npos) {
      found_standby = true;
    }
    if (topic.first.find("piloting_mode/manual/active") != std::string::npos) {
      found_manual = true;
    }
    if (topic.first.find("piloting_mode/autonomous/active") != std::string::npos) {
      found_autonomous = true;
    }
  }
  EXPECT_TRUE(found_standby) << "Missing standby active topic";
  EXPECT_TRUE(found_manual) << "Missing manual active topic";
  EXPECT_TRUE(found_autonomous) << "Missing autonomous active topic";
}

TEST_F(HelmManagerTestFixture, StandbyModeHasNoInputSubscriptions)
{
  // Standby is created with enable_output=false, so it should NOT have
  // helm or cmd_vel subscriptions.
  configureAndActivate();

  auto topic_names = node_->get_topic_names_and_types();
  bool found_standby_helm = false;
  bool found_standby_twist = false;

  for (const auto & topic : topic_names) {
    if (topic.first.find("piloting_mode/standby/helm") != std::string::npos) {
      found_standby_helm = true;
    }
    if (topic.first.find("piloting_mode/standby/cmd_vel") != std::string::npos) {
      found_standby_twist = true;
    }
  }
  EXPECT_FALSE(found_standby_helm)
    << "Standby mode should NOT have helm input subscription";
  EXPECT_FALSE(found_standby_twist)
    << "Standby mode should NOT have cmd_vel input subscription";
}

TEST_F(HelmManagerTestFixture, ManualModeHasInputSubscriptions)
{
  configureAndActivate();

  auto topic_names = node_->get_topic_names_and_types();
  bool found_manual_helm = false;
  bool found_manual_twist = false;

  for (const auto & topic : topic_names) {
    if (topic.first.find("piloting_mode/manual/helm") != std::string::npos) {
      found_manual_helm = true;
    }
    if (topic.first.find("piloting_mode/manual/cmd_vel") != std::string::npos) {
      found_manual_twist = true;
    }
  }
  EXPECT_TRUE(found_manual_helm) << "Manual mode should have helm subscription";
  EXPECT_TRUE(found_manual_twist)
    << "Manual mode should have cmd_vel subscription";
}

TEST_F(HelmManagerTestFixture, AutonomousModeHasInputSubscriptions)
{
  configureAndActivate();

  auto topic_names = node_->get_topic_names_and_types();
  bool found_auto_helm = false;
  bool found_auto_twist = false;

  for (const auto & topic : topic_names) {
    if (topic.first.find("piloting_mode/autonomous/helm") != std::string::npos) {
      found_auto_helm = true;
    }
    if (topic.first.find("piloting_mode/autonomous/cmd_vel") != std::string::npos) {
      found_auto_twist = true;
    }
  }
  EXPECT_TRUE(found_auto_helm) << "Autonomous mode should have helm subscription";
  EXPECT_TRUE(found_auto_twist)
    << "Autonomous mode should have cmd_vel subscription";
}

// ---------------------------------------------------------------------------
// Mode Switching via Topic -- command arbitration
// ---------------------------------------------------------------------------

/// Helper fixture with a pilot mode publisher and helm subscriber
class HelmManagerModeTest : public HelmManagerTestFixture
{
protected:
  void SetUp() override
  {
    HelmManagerTestFixture::SetUp();

    // Create a helper node for publishing/subscribing
    helper_node_ = std::make_shared<rclcpp::Node>("test_helper");

    // Publisher for piloting mode commands
    mode_pub_ = helper_node_->create_publisher<std_msgs::msg::String>(
      "/piloting_mode", 1);

    // Subscribers for active flags
    standby_active_sub_ = helper_node_->create_subscription<std_msgs::msg::Bool>(
      "/piloting_mode/standby/active", rclcpp::QoS(1).transient_local(),
      [this](const std_msgs::msg::Bool::SharedPtr msg) {
        standby_active_ = msg->data;
        standby_active_received_ = true;
      });

    manual_active_sub_ = helper_node_->create_subscription<std_msgs::msg::Bool>(
      "/piloting_mode/manual/active", rclcpp::QoS(1).transient_local(),
      [this](const std_msgs::msg::Bool::SharedPtr msg) {
        manual_active_ = msg->data;
        manual_active_received_ = true;
      });

    autonomous_active_sub_ = helper_node_->create_subscription<std_msgs::msg::Bool>(
      "/piloting_mode/autonomous/active", rclcpp::QoS(1).transient_local(),
      [this](const std_msgs::msg::Bool::SharedPtr msg) {
        autonomous_active_ = msg->data;
        autonomous_active_received_ = true;
      });
  }

  void TearDown() override
  {
    mode_pub_.reset();
    standby_active_sub_.reset();
    manual_active_sub_.reset();
    autonomous_active_sub_.reset();
    helper_node_.reset();
    HelmManagerTestFixture::TearDown();
  }

  void publishMode(const std::string & mode)
  {
    std_msgs::msg::String msg;
    msg.data = mode;
    mode_pub_->publish(msg);
  }

  void spinBoth(std::chrono::milliseconds duration)
  {
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < duration) {
      rclcpp::spin_some(node_->get_node_base_interface());
      rclcpp::spin_some(helper_node_);
      std::this_thread::sleep_for(1ms);
    }
  }

  /// Spin both nodes until a predicate is true, or until timeout.
  /// Returns true if the predicate was satisfied before the timeout.
  template<typename Predicate>
  bool spinUntil(Predicate pred, std::chrono::milliseconds timeout = 2000ms)
  {
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < timeout) {
      rclcpp::spin_some(node_->get_node_base_interface());
      rclcpp::spin_some(helper_node_);
      if (pred()) {
        return true;
      }
      std::this_thread::sleep_for(1ms);
    }
    return false;
  }

  void resetActiveFlags()
  {
    standby_active_received_ = false;
    manual_active_received_ = false;
    autonomous_active_received_ = false;
  }

  std::shared_ptr<rclcpp::Node> helper_node_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr mode_pub_;
  rclcpp::Subscription<std_msgs::msg::Bool>::SharedPtr standby_active_sub_;
  rclcpp::Subscription<std_msgs::msg::Bool>::SharedPtr manual_active_sub_;
  rclcpp::Subscription<std_msgs::msg::Bool>::SharedPtr autonomous_active_sub_;

  bool standby_active_ = false;
  bool manual_active_ = false;
  bool autonomous_active_ = false;
  bool standby_active_received_ = false;
  bool manual_active_received_ = false;
  bool autonomous_active_received_ = false;
};

TEST_F(HelmManagerModeTest, SwitchToManualMode)
{
  configureAndActivate();

  publishMode("manual");
  spinUntil([this] {return manual_active_received_;});

  EXPECT_TRUE(manual_active_received_) << "Should have received manual active flag";
  EXPECT_TRUE(manual_active_) << "Manual should be active";
  if (standby_active_received_) {
    EXPECT_FALSE(standby_active_) << "Standby should be inactive";
  }
  if (autonomous_active_received_) {
    EXPECT_FALSE(autonomous_active_) << "Autonomous should be inactive";
  }
}

TEST_F(HelmManagerModeTest, SwitchToAutonomousMode)
{
  configureAndActivate();

  publishMode("autonomous");
  spinUntil([this] {return autonomous_active_received_;});

  EXPECT_TRUE(autonomous_active_received_)
    << "Should have received autonomous active flag";
  EXPECT_TRUE(autonomous_active_) << "Autonomous should be active";
  if (manual_active_received_) {
    EXPECT_FALSE(manual_active_) << "Manual should be inactive";
  }
}

TEST_F(HelmManagerModeTest, SwitchToStandbyMode)
{
  configureAndActivate();

  publishMode("standby");
  spinUntil([this] {return standby_active_received_;});

  EXPECT_TRUE(standby_active_received_)
    << "Should have received standby active flag";
  EXPECT_TRUE(standby_active_) << "Standby should be active";
  if (manual_active_received_) {
    EXPECT_FALSE(manual_active_) << "Manual should be inactive";
  }
  if (autonomous_active_received_) {
    EXPECT_FALSE(autonomous_active_) << "Autonomous should be inactive";
  }
}

TEST_F(HelmManagerModeTest, ModeSwitchFromManualToAutonomous)
{
  configureAndActivate();

  // Switch to manual first
  publishMode("manual");
  spinUntil([this] {return manual_active_received_ && manual_active_;});
  ASSERT_TRUE(manual_active_);

  // Now switch to autonomous
  resetActiveFlags();
  publishMode("autonomous");
  spinUntil([this] {return autonomous_active_received_ && manual_active_received_;});

  EXPECT_TRUE(autonomous_active_received_);
  EXPECT_TRUE(autonomous_active_) << "Autonomous should now be active";
  EXPECT_TRUE(manual_active_received_);
  EXPECT_FALSE(manual_active_) << "Manual should now be inactive";
}

TEST_F(HelmManagerModeTest, ModeSwitchFromAutonomousToStandby)
{
  configureAndActivate();

  // Switch to autonomous first
  publishMode("autonomous");
  spinUntil([this] {return autonomous_active_received_ && autonomous_active_;});
  ASSERT_TRUE(autonomous_active_);

  // Switch back to standby
  resetActiveFlags();
  publishMode("standby");
  spinUntil([this] {return standby_active_received_ && autonomous_active_received_;});

  EXPECT_TRUE(standby_active_received_);
  EXPECT_TRUE(standby_active_) << "Standby should now be active";
  EXPECT_TRUE(autonomous_active_received_);
  EXPECT_FALSE(autonomous_active_) << "Autonomous should now be inactive";
}

TEST_F(HelmManagerModeTest, RapidModeSwitch)
{
  configureAndActivate();

  // Rapidly switch modes
  publishMode("manual");
  publishMode("autonomous");
  publishMode("standby");
  publishMode("manual");
  spinUntil([this] {return manual_active_received_ && manual_active_;});

  // After processing all, manual should be the final active mode
  EXPECT_TRUE(manual_active_) << "Manual should be the final active mode";
}

// ---------------------------------------------------------------------------
// Command Forwarding Tests
// ---------------------------------------------------------------------------

/// Fixture that also subscribes to the output helm topic
class HelmManagerCommandTest : public HelmManagerModeTest
{
protected:
  void SetUp() override
  {
    HelmManagerModeTest::SetUp();

    helm_output_sub_ = helper_node_->create_subscription<marine_interfaces::msg::Helm>(
      "/out/helm", 1,
      [this](const marine_interfaces::msg::Helm::SharedPtr msg) {
        last_helm_output_ = *msg;
        helm_output_received_ = true;
        helm_output_count_++;
      });

    manual_helm_pub_ = helper_node_->create_publisher<marine_interfaces::msg::Helm>(
      "/piloting_mode/manual/helm", 10);

    autonomous_helm_pub_ = helper_node_->create_publisher<marine_interfaces::msg::Helm>(
      "/piloting_mode/autonomous/helm", 10);

    manual_twist_pub_ = helper_node_->create_publisher<geometry_msgs::msg::TwistStamped>(
      "/piloting_mode/manual/cmd_vel", 10);

    autonomous_twist_pub_ = helper_node_->create_publisher<geometry_msgs::msg::TwistStamped>(
      "/piloting_mode/autonomous/cmd_vel", 10);
  }

  void TearDown() override
  {
    helm_output_sub_.reset();
    manual_helm_pub_.reset();
    autonomous_helm_pub_.reset();
    manual_twist_pub_.reset();
    autonomous_twist_pub_.reset();
    HelmManagerModeTest::TearDown();
  }

  rclcpp::Subscription<marine_interfaces::msg::Helm>::SharedPtr helm_output_sub_;
  rclcpp::Publisher<marine_interfaces::msg::Helm>::SharedPtr manual_helm_pub_;
  rclcpp::Publisher<marine_interfaces::msg::Helm>::SharedPtr autonomous_helm_pub_;
  rclcpp::Publisher<geometry_msgs::msg::TwistStamped>::SharedPtr manual_twist_pub_;
  rclcpp::Publisher<geometry_msgs::msg::TwistStamped>::SharedPtr autonomous_twist_pub_;

  marine_interfaces::msg::Helm last_helm_output_;
  bool helm_output_received_ = false;
  int helm_output_count_ = 0;
};

TEST_F(HelmManagerCommandTest, ManualHelmCommandForwardedWhenActive)
{
  configureAndActivate();

  // Set manual mode
  publishMode("manual");
  spinUntil([this] {return manual_active_received_ && manual_active_;});
  ASSERT_TRUE(manual_active_);

  // Publish helm command on manual topic
  marine_interfaces::msg::Helm helm_cmd;
  helm_cmd.throttle = 0.5;
  helm_cmd.rudder = 0.3;
  manual_helm_pub_->publish(helm_cmd);
  spinUntil([this] {return helm_output_received_;});

  EXPECT_TRUE(helm_output_received_) << "Helm output should be received";
  EXPECT_FLOAT_EQ(last_helm_output_.throttle, 0.5f);
  EXPECT_FLOAT_EQ(last_helm_output_.rudder, 0.3f);
}

TEST_F(HelmManagerCommandTest, AutonomousHelmCommandBlockedWhenManualActive)
{
  configureAndActivate();

  // Set manual mode
  publishMode("manual");
  spinUntil([this] {return manual_active_received_ && manual_active_;});
  ASSERT_TRUE(manual_active_);

  // Publish helm command on autonomous topic -- should be blocked
  marine_interfaces::msg::Helm helm_cmd;
  helm_cmd.throttle = 0.8;
  helm_cmd.rudder = -0.5;
  autonomous_helm_pub_->publish(helm_cmd);
  spinBoth(100ms);

  EXPECT_FALSE(helm_output_received_)
    << "Autonomous command should NOT be forwarded when manual is active";
}

TEST_F(HelmManagerCommandTest, ManualHelmCommandBlockedWhenAutonomousActive)
{
  configureAndActivate();

  // Set autonomous mode
  publishMode("autonomous");
  spinUntil([this] {return autonomous_active_received_ && autonomous_active_;});
  ASSERT_TRUE(autonomous_active_);

  // Publish helm command on manual topic -- should be blocked
  marine_interfaces::msg::Helm helm_cmd;
  helm_cmd.throttle = 0.5;
  helm_cmd.rudder = 0.3;
  manual_helm_pub_->publish(helm_cmd);
  spinBoth(100ms);

  EXPECT_FALSE(helm_output_received_)
    << "Manual command should NOT be forwarded when autonomous is active";
}

TEST_F(HelmManagerCommandTest, AutonomousHelmForwardedWhenActive)
{
  configureAndActivate();

  // Set autonomous mode
  publishMode("autonomous");
  spinUntil([this] {return autonomous_active_received_ && autonomous_active_;});
  ASSERT_TRUE(autonomous_active_);

  // Publish helm command on autonomous topic
  marine_interfaces::msg::Helm helm_cmd;
  helm_cmd.throttle = 0.7;
  helm_cmd.rudder = -0.2;
  autonomous_helm_pub_->publish(helm_cmd);
  spinUntil([this] {return helm_output_received_;});

  EXPECT_TRUE(helm_output_received_) << "Autonomous helm should be forwarded";
  EXPECT_FLOAT_EQ(last_helm_output_.throttle, 0.7f);
  EXPECT_FLOAT_EQ(last_helm_output_.rudder, -0.2f);
}

TEST_F(HelmManagerCommandTest, StandbyBlocksAllCommands)
{
  configureAndActivate();

  // Set standby mode
  publishMode("standby");
  spinUntil([this] {return standby_active_received_ && standby_active_;});
  ASSERT_TRUE(standby_active_);

  // Try to publish commands on both manual and autonomous topics
  marine_interfaces::msg::Helm helm_cmd;
  helm_cmd.throttle = 0.5;
  helm_cmd.rudder = 0.3;
  manual_helm_pub_->publish(helm_cmd);
  autonomous_helm_pub_->publish(helm_cmd);
  spinBoth(100ms);

  // Standby has no subscriptions so nothing should be forwarded even if mode
  // matched. The key safety property is: in standby, no output is produced.
  EXPECT_FALSE(helm_output_received_)
    << "Standby mode should block all commands";
}

TEST_F(HelmManagerCommandTest, ModeSwitchChangesWhichCommandsAreForwarded)
{
  configureAndActivate();

  // Start in manual mode
  publishMode("manual");
  spinUntil([this] {return manual_active_received_ && manual_active_;});

  marine_interfaces::msg::Helm helm_cmd;
  helm_cmd.throttle = 0.5;
  helm_cmd.rudder = 0.0;
  manual_helm_pub_->publish(helm_cmd);
  spinUntil([this] {return helm_output_received_;});

  ASSERT_TRUE(helm_output_received_);
  EXPECT_FLOAT_EQ(last_helm_output_.throttle, 0.5f);

  // Switch to autonomous
  helm_output_received_ = false;
  resetActiveFlags();
  publishMode("autonomous");
  spinUntil([this] {return autonomous_active_received_ && autonomous_active_;});

  // Manual commands should now be blocked
  manual_helm_pub_->publish(helm_cmd);
  spinBoth(100ms);
  EXPECT_FALSE(helm_output_received_)
    << "After switching to autonomous, manual commands should be blocked";

  // Autonomous commands should now be forwarded
  marine_interfaces::msg::Helm auto_cmd;
  auto_cmd.throttle = 0.9;
  auto_cmd.rudder = -0.1;
  autonomous_helm_pub_->publish(auto_cmd);
  spinUntil([this] {return helm_output_received_;});

  EXPECT_TRUE(helm_output_received_);
  EXPECT_FLOAT_EQ(last_helm_output_.throttle, 0.9f);
}

// ---------------------------------------------------------------------------
// Heartbeat / Status Publishing Tests
// ---------------------------------------------------------------------------

class HelmManagerHeartbeatTest : public HelmManagerModeTest
{
protected:
  void SetUp() override
  {
    HelmManagerModeTest::SetUp();

    heartbeat_sub_ = helper_node_->create_subscription<marine_interfaces::msg::Heartbeat>(
      "/heartbeat", 1,
      [this](const marine_interfaces::msg::Heartbeat::SharedPtr msg) {
        last_heartbeat_ = *msg;
        heartbeat_received_ = true;
      });

    helm_status_pub_ = helper_node_->create_publisher<marine_interfaces::msg::Heartbeat>(
      "/status/helm", 1);
  }

  void TearDown() override
  {
    heartbeat_sub_.reset();
    helm_status_pub_.reset();
    HelmManagerModeTest::TearDown();
  }

  rclcpp::Subscription<marine_interfaces::msg::Heartbeat>::SharedPtr heartbeat_sub_;
  rclcpp::Publisher<marine_interfaces::msg::Heartbeat>::SharedPtr helm_status_pub_;
  marine_interfaces::msg::Heartbeat last_heartbeat_;
  bool heartbeat_received_ = false;
};

TEST_F(HelmManagerHeartbeatTest, HeartbeatIncludesPilotingMode)
{
  configureAndActivate();

  // Set a piloting mode
  publishMode("manual");
  spinUntil([this] {return manual_active_received_;});

  // Publish a helm status heartbeat
  marine_interfaces::msg::Heartbeat status;
  status.header.stamp = node_->now();
  marine_interfaces::msg::KeyValue kv;
  kv.key = "status";
  kv.value = "ok";
  status.values.push_back(kv);
  helm_status_pub_->publish(status);
  spinUntil([this] {return heartbeat_received_;});

  ASSERT_TRUE(heartbeat_received_) << "Heartbeat should be republished";
  ASSERT_GE(last_heartbeat_.values.size(), 2u);
  EXPECT_EQ(last_heartbeat_.values[0].key, "piloting_mode");
  EXPECT_EQ(last_heartbeat_.values[0].value, "manual");
  // Original values should follow
  EXPECT_EQ(last_heartbeat_.values[1].key, "status");
  EXPECT_EQ(last_heartbeat_.values[1].value, "ok");
}

TEST_F(HelmManagerHeartbeatTest, HeartbeatReflectsCurrentMode)
{
  configureAndActivate();

  // Set autonomous mode
  publishMode("autonomous");
  spinUntil([this] {return autonomous_active_received_;});

  // Publish status
  marine_interfaces::msg::Heartbeat status;
  status.header.stamp = node_->now();
  helm_status_pub_->publish(status);
  spinUntil([this] {return heartbeat_received_;});

  ASSERT_TRUE(heartbeat_received_);
  ASSERT_GE(last_heartbeat_.values.size(), 1u);
  EXPECT_EQ(last_heartbeat_.values[0].key, "piloting_mode");
  EXPECT_EQ(last_heartbeat_.values[0].value, "autonomous");
}

TEST_F(HelmManagerHeartbeatTest, HeartbeatWithEmptyModeBeforeAnySwitch)
{
  configureAndActivate();

  // Without setting a mode, the piloting_mode_ will be empty
  marine_interfaces::msg::Heartbeat status;
  status.header.stamp = node_->now();
  helm_status_pub_->publish(status);
  spinUntil([this] {return heartbeat_received_;});

  ASSERT_TRUE(heartbeat_received_);
  ASSERT_GE(last_heartbeat_.values.size(), 1u);
  EXPECT_EQ(last_heartbeat_.values[0].key, "piloting_mode");
  EXPECT_EQ(last_heartbeat_.values[0].value, "")
    << "Piloting mode should be empty before any mode switch";
}
