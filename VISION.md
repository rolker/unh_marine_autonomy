# Vision: UNH Marine Autonomy Framework

The **UNH Marine Autonomy Framework** (aka Project11) functions as a generalized control system for marine robotics. Developed at the Center for Coastal and Ocean Mapping / Joint Hydrographic Center (CCOM/JHC), the framework abstracts vehicle control logic to support heterogeneous fleets while maintaining specific capabilities for high-precision seafloor mapping.

## Strategic Objectives

### 1. Multi-Domain Marine Robotics
The framework is designed to be extensible to multiple domains. While the current implementation focuses on the control of **Uncrewed Surface Vehicles (USV)**, the architecture is structured to support the wider marine ecosystem.
*   **Current Capability**: Active control of USVs and monitoring/coordination of heterogeneous assets (e.g., tracking AUV progress during OECI Tech Challenge).
*   **Future-Proofing**: The system does not preclude future integration of **Autonomous Underwater Vehicles (AUV)** or **Uncrewed Aerial Vehicles (UAV)**. Interfaces are designed to be generic enough to eventually support direct control of these platforms.

**Objective**: Ensure seamless interoperability and data exchange between diverse agents, allowing this framework to act as a coordinator within a multi-domain fleet.

### 2. Reliable Seafloor Mapping ("Safety First")
Operational reliability is the primary metric for success. Originating as a supervised "backseat driver" for ASVs, the framework is **iteratively evolving** toward higher levels of autonomy. We adopt a "safety-first" philosophy, validating each incremental step away from direct supervision.
*   **Legacy**: Deep roots in operator-supervised mission planning and execution.
*   **Simulation-First Validation**: Development relies heavily on simulation (currently Python-based, evolving toward Gazebo) to validate missions and safety behaviors before they ever touch the water.
*   **Field Proven**: Beyond simulation, the framework is actively deployed and tested on CCOM/JHC research vessels, ensuring performance in real-world maritime conditions.
*   **In-Situ Intelligence**: The system leverages real-time processing (e.g., CUBE bathymetry) to assess data quality on the edge, ensuring "good data" is captured before returning.
*   **Evolution**: Moving systematically from supervised operations to diverse autonomous capabilities.
*   **Roadmap**: Future integration of advanced fail-safe behaviors (e.g., automated return-to-home) to further reduce operator cognitive load.

### 3. Marine-Specific Solvency
The framework addresses specific environmental challenges inherent to the maritime domain that are often absent in terrestrial robotics.
*   **Active Perception**: Integration of marine-specific sensors (Radar, AIS) with modern AI (sea surface segmentation) to maintain situational awareness in dynamic surface environments.
*   **Vertical Datums**: Explicit handling of Geoid, Ellipsoid, and Tidal datums for accurate depth georeferencing.
*   **Dynamic forcing**: Path planning algorithms account for external forces such as wind, current set, and drift.
*   **Comms Denied/Degraded**: Mission execution logic assumes intermittent or low-bandwidth telemetry.

### 4. Community Ecosystem
The architecture promotes modularity to benefit the wider ROS 2 Maritime community.
*   **Decoupled Packages**: Tools such as `mru_transform` (motion reference unit processing) and `s57_grids` (chart handling) are designed as standalone ROS 2 packages.
*   **Ease of Access**: We aim for binary distribution (`apt install`) of these agnostic packages ensuring they are easily consumable by the community without adopting the entire framework.
*   **Industrial Utility**: Specific drivers and utilities developed within this ecosystem are currently deployed in commercial products, verifying their correctness and robustness in real-world applications.
*   **Integrator Friendly**: We avoid "black box" solutions. The framework is designed for modular integration, allowing startups and companies to adopt specific capabilities (e.g., just the Helm Manager) without being forced into a monolithic stack.
*   **Standardization**: Adherence to REP-2000 (ROS 2 Standards) and REP-105 (Coordinate Frames).
*   **Educational Platform**: A proven enabler for student research. The framework provides a safe, stable baseline for graduate and undergraduate projects, allowing students to focus on novel algorithms without rebuilding a vehicle stack.
*   **Target Audience**: Academic researchers, industry developers, and marine technology integrators.

### 5. Modern ROS 2 Architecture
The stack is built on current ROS 2 LTS (Jazzy) and Rolling distributions to ensure long-term support and access to modern features.
*   **Layered Workspaces**: Concerns are strictly separated via `colcon` workspace overlays (Core Logic -> Perception -> UI).
*   **Agent-Friendly**: Workflows and scripts are structured to facilitate maintenance by AI agents.
