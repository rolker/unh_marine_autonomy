[![docker-jazzy-ros-core](../../actions/workflows/ros-base-docker.yml/badge.svg?branch=jazzy)](../../actions/workflows/ros-base-docker.yml)

# Package: marine_autonomy
 
 > **Note**: This is the documentation for the `marine_autonomy` ROS 2 package. For the full framework documentation, please see the [Repository Root](../README.md).
 
 # Project11: A mapping focused open-sourced software framework for Autonomous Surface Vehicles

The Project 11 framework was developed as a backseat driver for Autonomous Surface Vehicles
(ASVs). Key design features include the ability to quickly and easily specify survey plans; monitoring of mission progress, even
over unreliable wireless networks; and to provide an environment to develop advanced autonomous technologies.

## Quick Start Guide

### System requirements

On an Ubuntu 24.04 system, install ROS2 Jazzy including the developer tools following instructions on the ROS website.

https://docs.ros.org/en/jazzy/Installation.html

A system with decent performance is required. While testing in a virtual machine (vm) using VirtualBox on an Ubuntu host with 32G of ram, an Intel i7-10875H processor and an NVidia graphics card, I needed to set the vm to have 16G of ram, 8 CPUs and enabled 3D acceleration to reduce timeout errors while running the simulation.

### Installation and launch

Once ROS2 Jazzy is installed, you can quickly install and run Project11 with the following:

    mkdir -p ~/project11/jazzy_ws/src
    cd ~/project11/jazzy_ws/src
    git clone https://github.com/CCOMJHC/project11.git -b jazzy

    # If rosdep is not installed:
    # sudo apt-get install python3-rosdep 
    sudo rosdep init
    rosdep update

    # If vcs is not installed:
    # sudo apt-get install python3-vcstool   
    vcs import < project11/config/repos/simulator.repos
    
    rosdep install --from-paths . --ignore-src -r -y

    cd ..
    source /opt/ros/jazzy/setup.bash
    colcon build --symlink-install
    
    source install/setup.bash

    # download nautical charts
    cd ..
    mkdir data
    cd data
    wget https://charts.noaa.gov/ENCs/02Region_ENCs.zip
    unzip 02Region_ENCs.zip

    ROS_S57_ENC_ROOT=~/project11/data/ENC_ROOT ros2 launch project11_simulation simulator_launch.py

Two windows should appear, CAMP and RViz. RViz can seem to freeze when loading the robot model. Be patient.

In the CAMP window, zoom out with the mouse wheel to find the boat. Right click on a target area and select "Hover here" to have the boat go into autonomous mode, transit to the location, and hover in place once it gets there.
    
## Major Components & Concepts

The framework is a modular ecosystem of ROS 2 nodes. For a detailed deep-dive into data flows, piloting modes, and hardware abstraction, see the **[System Architecture Documentation](./docs/system_architecture.md)**.

### Operator Interface (CAMP)
The **CCOM Autonomous Mission Planner (CAMP)** is the primary UI. It allows operators to:
- Visualize vehicle position on georeferenced charts.
- Plan and transmit complex survey missions.
- Monitor real-time status and command mode changes.

### Mission Management
The `mission_manager` receives JSON plans from CAMP and translates them into discrete navigation tasks (e.g., "Follow this path", "Hover here"). It manages the high-level mission state machine.

### Control Arbitration (Helm Manager)
The `helm_manager` is the safety-critical arbitrator. It switches between **Standby**, **Manual**, and **Autonomous** modes, ensuring only one source of control is ever active on the hardware.

### Reliable Communications (UDP & Command Bridge)
To support operations over low-bandwidth or unreliable wireless links, we use:
- **`udp_bridge`**: Optimized message transport.
- **`command_bridge`**: A transaction layer ensuring high-level commands (like "Start") are successfully delivered even if the link drops momentarily.

### Hardware Agnosticism
The framework uses a **Low-Level Helm** pattern. Core nodes output generic control messages, which are then translated by a vehicle-specific node (e.g., `ben_helm`) into hardware commands. This allows the same framework to drive diverse vessels with minimal changes.

## Piloting Modes

The system operates in three states, managed by the `helm_manager`:
1. **Standby**: No control output; safest state.
2. **Manual**: Passthrough for joystick or teleoperation commands.
3. **Autonomous**: Passthrough for Mission Manager and Navigation Stack outputs.

---

## Detailed Navigation Stack

The `mission_manager` interacts with a Behavior Tree-based navigation stack. High-level directives like "Survey this area" are decomposed into track lines which are then sent to path followers. Eventually, these result in `Helm` or `Twist` messages reaching the `helm_manager`.
