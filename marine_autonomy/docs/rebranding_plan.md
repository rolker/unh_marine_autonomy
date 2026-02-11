# Rebranding Plan: UNH Marine Autonomy

**Status**: Approved
**Date**: 2026-01-13

## Objective
Rename and restructure the ecosystem to reflect CCOM/JHC provenance and align with ROS 2 naming conventions.

## Naming Schema

| Current Name | Proposed Name | Rationale |
| :--- | :--- | :--- |
| `project11` | `marine_autonomy` | Core system launch/config. |
| `project11_msgs` | `marine_interfaces` | Standard ROS 2 naming (`_interfaces`). |
| `project11_nav_msgs` | `marine_interfaces` | Merged into `marine_interfaces`. |
| `camp` | `camp` | Keep as is. |
| `mission_manager` | `mission_manager` | Keep as is. |
| `helm_manager` | `helm_manager` | Keep as is. |
| `udp_bridge` | `udp_bridge` | Keep as is. |
| `command_bridge` | `command_bridge` | Keep as is. |

## Execution Plan

### 1. Repository Restructuring
*   Create `unh_marine_autonomy` repo.
*   Move core packages into it.
*   Ensure `unh_marine_navigation` remains focused on GNC.
*   Keep `camp` and `s57_tools` separate.

### 2. Package Renaming
*   Update `package.xml` and `CMakeLists.txt` for `project11` -> `marine_autonomy`.
*   Update `package.xml` and `CMakeLists.txt` for `project11_msgs` -> `marine_interfaces`.

### 3. Namespace Updates
*   Search and replace C++ namespace `project11::` with `marine::`.
*   Update Topic namespaces from `/project11/` to `/marine/` or `/autonomy/`.

### 4. Downstream Updates
*   Update robot-specific packages (e.g., `ben_project11`) to depend on new package names.
