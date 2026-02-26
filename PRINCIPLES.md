# Principles

Guiding principles for the UNH Marine Autonomy Framework. Each traces to the
project's [Vision](./VISION.md) and existing architecture.

## Safety First

Operational reliability takes priority over capability. The system defaults to a
safe standby mode where no commands reach the vehicle. Every increment of autonomy
is validated in simulation before field deployment, and operators always retain
override authority.

## Hardware Agnosticism

Interfaces are generic — control flows through abstract helm commands (throttle/rudder)
and standard ROS 2 messages, not platform-specific APIs. This enables the same
autonomy stack to drive USVs, coordinate with AUVs, and extend to new vehicle types
without rewriting core logic.

## Modularity and Decoupling

Each package solves one problem and can be adopted independently. The helm manager
arbitrates control without knowing what generates commands; the mission manager
decomposes tasks without knowing how they're executed. Packages like `marine_interfaces`
and `udp_bridge` are designed for community reuse outside this framework.

## Simulation-First Validation

No behavior reaches the water without passing through simulation. The workspace
layers (`unh_marine_simulation` in the simulation layer, `vrx` in the underlay)
provide a Gazebo-based test environment where missions, safety behaviors, and new
algorithms are validated before field trials.

## Iterative, Validated Evolution

The system evolves from supervised teleoperation toward autonomous operations in
deliberate, incremental steps. Each step is validated through simulation and field
testing before the next is attempted — the path from "backseat driver" to full
autonomy is a ladder, not a leap.

## Standards Compliance

The framework adheres to ROS 2 community standards: REP-2000 for package quality
and support tiers, REP-105 for coordinate frame conventions, and standard ROS 2
lifecycle patterns for node management. This ensures interoperability with the
broader ROS ecosystem and lowers the barrier for new contributors.
