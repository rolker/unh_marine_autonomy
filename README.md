# UNH Marine Autonomy Framework

[![ROS 2 Jazzy](https://img.shields.io/badge/ROS_2-Jazzy-blue)](https://docs.ros.org/en/jazzy/)

Welcome to the **UNH Marine Autonomy Framework** repository. This is the home of the autonomous control software developed at the Center for Coastal and Ocean Mapping / Joint Hydrographic Center (CCOM/JHC) at the University of New Hampshire (UNH), also known as *Project11*.

> **North Star**: "Tell a robot to map an area, have it do it safely, and return with good data."

## Vision
The framework is a multi-domain, community-driven platform for marine robotics.
**[Read the Vision & Strategic Pillars](./VISION.md)**

## Repository Structure

This repository is a collection of packages that form the core of the autonomy system.

*   **[`marine_autonomy`](./marine_autonomy/)**: The meta-package and core documentation for the legacy "Project11" logic.
*   **`helm_manager`**: Arbitrates control commands to the hardware.
*   **`mission_manager`**: High-level mission execution and state machine.
*   **`camp`** (External): The CCOM Autonomous Mission Planner (UI).

## Getting Started
This repository is typically part of a layered workspace setup.

### Quick Build
```bash
colcon build --symlink-install
```

### Workspace Integration

This repository is the core layer of the
[ros2_agent_workspace](https://github.com/rolker/ros2_agent_workspace), which
provides layered `colcon` workspace management, CI tooling, and AI-agent support
across all project repos. The `config/` directory in this repo defines the `.repos`
files that pull in dependencies across six layers (underlay, core, platforms, sensors,
simulation, ui). See the workspace repo for setup instructions.

### Community & Contributing
Contributions from the marine robotics community are welcome.
- **Issues**: Please use GitHub Issues for bug reports and feature requests.
- **Pull Requests**: The project follows a standard PR workflow.
