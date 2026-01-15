# UNH Marine Autonomy Framework

[![ROS 2 Jazzy](https://img.shields.io/badge/ROS_2-Jazzy-blue)](https://docs.ros.org/en/jazzy/)

Welcome to the **UNH Marine Autonomy Framework** repository. This is the home of the autonomous control software developed at the Center for Coastal and Ocean Mapping / Joint Hydrographic Center (CCOM/JHC), also known as *Project11*.

> **North Star**: "Tell a robot to map an area, have it do it safely, and return with good data."

## 🔭 Vision
We are building a multi-domain, community-driven framework for marine robotics.
👉 **[Read our Vision & Strategic Pillars](./VISION.md)**

## 📂 Repository Structure

This repository is a collection of packages that form the core of the autonomy system.

*   **[`marine_autonomy`](./marine_autonomy/)**: The meta-package and core documentation for the legacy "Project11" logic.
*   **`helm_manager`**: Arbitrates control commands to the hardware.
*   **`mission_manager`**: High-level mission execution and state machine.
*   **`camp`** (External): The CCOM Autonomous Mission Planner (UI).

## 🚀 Getting Started
This repository is typically part of a layered workspace setup.

### Quick Build
```bash
colcon build --symlink-install
```

### Community & Contributing
We welcome contributions from the marine robotics community!
- **Issues**: Please use GitHub Issues for bug reports and feature requests.
- **Pull Requests**: We follow a standard PR workflow.
