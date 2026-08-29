# How the Marine Autonomy Stack Works

A plain-language tour of the Project11 marine autonomy framework: how an operator
at CAMP and a boat on the water find each other, how commands flow, and what is
actually steering the boat. The goal is **understanding** — enough of the moving
parts that, when something looks wrong on the water, you can reason about *which*
part to look at rather than guess.

This is the operator/new-engineer overview. For depth, follow the links:

- [`autonomy_modes.md`](autonomy_modes.md) — the helm manager's piloting-mode state machine
- [`data_flows.md`](data_flows.md) — command/telemetry data flows in detail
- [`interfaces.md`](interfaces.md) — every ROS topic/service/action

> **Topic naming.** Deployed topics use a `marine/` prefix under a per-boat
> namespace (e.g. `/bizzy/marine/heartbeat`). One exception: `piloting_mode` is
> a bare topic with no `marine/` prefix. The launch and node source are the
> source of truth.

---

## 1. It's a framework, not one boat

Project11 is a **vehicle-agnostic autonomy framework**. The same core
(`unh_marine_autonomy` + `unh_marine_navigation`) runs on very different
platforms — for example **Ben**, **DriX**, and the **EchoBoat** family
(BizzyBoat / IzzyBoat). The autonomy layer thinks in terms of *where to go* and
*how fast*, and emits a generic velocity / helm command. Each platform then
adapts that command to its own propulsion and low-level control:

- **BizzyBoat (EchoBoat 240)** routes the command through **ArduPilot/MAVROS**
  to its thrusters.
- **Ben** and **DriX** do **not** use ArduPilot — they have their own low-level
  backends.

So "the stack outputs `cmd_vel`" is a framework fact; "ArduPilot drives the
thrusters" is a *BizzyBoat* fact. Throughout this guide, anything tied to a
specific sensor rig or hull (the obstacle layers, the reflex zones, the control
gains) is **per-platform configuration**, and is called out as such. The
EchoBoat/BizzyBoat configuration is used below as the worked example because it
is the most fully built-out.

---

## 2. The whole system in one picture

```
  OPERATOR STATION                                    PLATFORM (boat)
 ┌──────────────────┐                              ┌────────────────────────┐
 │      CAMP        │   commands  (send-until-ack) │  command_bridge        │
 │  (mission plan,  │ ───────────────────────────► │  → mission_manager     │
 │   overrides,     │       UDP bridge link        │  → piloting_mode        │
 │   live display)  │ ◄─────────────────────────── │                        │
 │                  │   telemetry / heartbeat /    │  navigation → helm →    │
 │  PlatformManager │   /marine/platforms          │  thrusters              │
 └──────────────────┘                              └────────────────────────┘
```

Two programs, one radio/UDP link between them. CAMP is the operator's window and
mission planner; everything autonomous happens **on the boat**. The link can drop
(over-the-horizon operation is normal) without stopping the boat — see §7.

---

## 3. How a platform advertises itself to CAMP

CAMP does not need to be pre-configured for each boat. A platform **announces
itself**, and CAMP draws it.

- On the boat, **`platform_send.py`** (node `platform_publisher`, a managed
  *lifecycle* node) publishes a **`PlatformList`** message on **`/marine/platforms`**
  once per second.
- Each entry is a **`Platform`** message
  (`marine_interfaces/msg/Platform`) describing one boat:

  | Field | Meaning |
  |-------|---------|
  | `name` | Display name (becomes the CAMP tab label) |
  | `platform_namespace` | ROS namespace the boat's topics live under |
  | `robot_description` | URDF — the boat's shape/model |
  | `nav_sources[]` | Named nav feeds (position/orientation/velocity topics) with a `priority` |
  | `width`, `length` | Hull dimensions, for drawing to scale |
  | `reference_x`, `reference_y` | Reference point on the hull |
  | `color` | RGBA the boat is drawn in |

  These values come from per-boat ROS parameters (set in the platform's config),
  so each boat ships its own identity.

- In CAMP, **`PlatformManager`** subscribes to `/marine/platforms`. The first
  time it sees a platform `name`, it **creates a new tab and a map drawing** for
  it; subsequent messages just update it. This is why a boat "just appears" in
  CAMP when it comes online, and how CAMP knows *which topics* to read for that
  boat's position (from `nav_sources`).

The practical upshot for an operator: **if a boat is not showing up in CAMP, the
question is whether `/marine/platforms` is reaching the operator station** — the
link, the bridge, and the platform publisher being active (it's a lifecycle node,
so it must be *activated*, not merely launched).

---

## 4. Who is allowed to drive: piloting modes

All control authority funnels through one node: the **helm manager**
(`helm_manager`). It is a **single-active-mode mux** — only one source commands
the boat at a time, so manual and autonomous commands can never fight. The three
modes (full detail in [`autonomy_modes.md`](autonomy_modes.md)):

- **Standby** — nothing is forwarded to the thrusters. This is the safe/idle
  state. (On BizzyBoat, standby hands the vehicle back to direct RC control, so
  the boat may drift with wind/current — that is expected, not a fault.)
- **Manual** — operator joystick/helm goes straight through.
- **Autonomous** — the navigation stack drives (see §5).

The helm manager publishes a **heartbeat** on **`marine/heartbeat`**
(`marine_interfaces/Heartbeat`) reporting the currently active mode and status.
**The heartbeat is the acknowledgment**: when you command a mode or override from
CAMP, you know it took effect because the heartbeat comes back reporting the new
state. If the heartbeat lags, that is usually **link saturation**, not a stuck
command.

The helm manager's outputs are remapped to the platform's control topics:

- `out/cmd_vel` → **`marine/control/cmd_vel`** (`geometry_msgs/TwistStamped`)
- `out/helm` → **`marine/control/helm`** (`marine_interfaces/Helm`: `throttle`,
  `rudder`, each −1.0…1.0)

A platform consumes whichever of these it needs — BizzyBoat's MAVROS backend
takes the velocity command from here.

---

## 5. The autonomous control chain (EchoBoat worked example)

When the boat is in **autonomous** mode running a mission, this is the chain of
hands the command passes through. The components are stock Nav2 where possible,
with marine-specific plugins where the water demands it.

```
 mission (tasks)
      │
      ▼
 BT task navigator        marine_nav_bt_task_navigator::TaskNavigator
   run_tasks.xml          task types: hover · goto · survey_line ·
      │                                survey_line_set · survey_area
      ▼
 planner  "GridBased"     nav2_smac_planner::SmacPlannerHybrid  (Hybrid-A*)
      │                   (240: minimum_turning_radius 1.5 m)
      ▼
 path smoother            nav2_smoother::SimpleSmoother   (active)
      │
      ▼
 controller "FollowPath"  marine_nav_crabbing_path_follower::CrabbingPathFollower
      │   publishes cmd_vel → remapped to  cmd_vel_nav
      ▼
 ┌─────────────────────────────────────────────────────────────┐
 │  velocity_smoother   ── INTENTIONALLY NOT IN THE PATH (#170)  │
 └─────────────────────────────────────────────────────────────┘
      │  cmd_vel_nav
      ▼
 Collision Monitor        nav2_collision_monitor  (the safety gate)
      │   cmd_vel_in: cmd_vel_nav  →  cmd_vel_out: piloting_mode/autonomous/cmd_vel
      ▼
 helm manager (autonomous mode) ──► marine/control/cmd_vel + marine/control/helm
      ▼
 platform backend (BizzyBoat: ArduPilot/MAVROS) ──► thrusters
```

Things worth knowing as an operator:

- **The controller is a *crabbing* path follower.** It can hold a line while the
  hull points off-heading to fight current — important for survey quality. Its
  speed comes from the task/path (per-pose), not a fixed constant.
- **The `velocity_smoother` is deliberately disconnected** on these boats
  (issue #170). The controller feeds the Collision Monitor directly. This is a
  conscious choice — re-enabling it would put a second publisher on the helm
  topic and fight the safety gate. (Don't confuse this with the *path*
  `smoother_server`, which **is** active — that smooths the planned path, not the
  velocity command.)
- **The Collision Monitor is the last thing between the controller and the
  thrusters** — see §6.
- **Turning is vectored thrust**, not a rudder: yaw rate is produced by
  differential/steered thrust, so it scales with thrust, not boat speed. Don't
  reason about it with rudder-boat intuition.

---

## 6. How the boat sees obstacles — two independent systems

There are **two** obstacle systems, at different levels, and they are easy to
confuse. Both are EchoBoat sensor-rig configuration, not framework guarantees.

**Deliberative — the costmap (planning).** The planner avoids obstacles by
planning around them in a costmap built from:

- a **`sea_surface_layer`** fusing segmentation from **all four OAK cameras**
  (forward, port, starboard, aft) into one shared, persistent buffer — an
  obstacle seen by any camera accumulates evidence and survives the handoff
  between camera views;
- an **S57 chart layer** (charted hazards, shoreline);
- inflation.

This is *deliberative*: it reshapes the planned path ahead of time.

**Reflex — the Collision Monitor (safety floor).** Independently, the Nav2
**Collision Monitor** watches a **reflex point cloud** and gates the velocity
command in real time. On BizzyBoat (240) it has two zones:

- **`CollisionSlowdown`** — a ~20 m × 6 m box ahead; an intrusion scales speed
  down (to 0.3×).
- **`CollisionStop`** — a ~5 m × 4 m box close in; an intrusion **stops** the
  boat.

Its input (`collision_monitor/pointcloud`) is the **forward OAK camera's**
segmentation projected to the boat's reference level by `segments_to_pointcloud`
— **forward camera only**, unlike the four-camera costmap. With no reflex feed
configured, the monitor is a harmless pass-through.

The mental model: **the costmap is how the boat plans politely around things; the
Collision Monitor is the binary safety floor that doesn't care about the plan.**
A boat can be following a perfectly planned path and still get slowed or stopped
by the reflex if something enters the forward zone.

---

## 7. Limits and governors

- **Helm clamp.** The helm manager clamps speed and yaw (`max_speed`,
  `max_yaw_speed`). These are *capability backstops*, not the survey limit — the
  survey speed/turn limits belong in the navigation config.
- **Turning radius.** The planner's `minimum_turning_radius` (1.5 m on the 240)
  keeps planned paths flyable by the hull.
- **Footprint.** The costmap uses the real hull footprint (re-tuned to the 240
  hull) so it doesn't plan through gaps the boat can't fit.

---

## 8. The link between boat and operator

Commands and telemetry cross the water over a **UDP bridge** (`udp_bridge`),
typically WiFi backhaul with a Starlink/VPN fallback path.

- **Commands** use a **send-until-acknowledged** protocol with timestamp
  deduplication: CAMP's `command_bridge_sender` keeps resending until the boat's
  `command_bridge_receiver` acks on `marine/response`. The receiver fans commands
  out to mission management (`marine/mission_manager/command`), mission plans
  (`marine/mission_plan`), and mode changes (`piloting_mode`).
- **Telemetry** flowing back includes nav (position/orientation/velocity),
  display data, mission-manager status, the helm output (for monitoring), `/tf`,
  and the **heartbeat**.

**A dropped link is a normal operating mode, not an error.** Over-the-horizon
operation is expected; the boat keeps executing its mission, and the bridge
reconciles when the link returns. Diagnostics treat a wireless drop as data, not
as an `ERROR`.

---

## 9. Quick "where do I look?" map

| Symptom | Most likely area |
|---------|------------------|
| Boat doesn't appear in CAMP | `/marine/platforms` not arriving — link, bridge, or `platform_publisher` not activated (§3) |
| Command "didn't take" | Watch the **heartbeat** for the new state; lag usually means link saturation (§4) |
| Boat went to Standby and drifted | Expected — Standby hands back to RC; wind/current drift is normal (§4) |
| Boat slowed/stopped for "no reason" | Collision Monitor reflex on the **forward** camera (§6) |
| Boat planned a wide path around open water | Costmap obstacle (chart layer or a segmentation false-positive) (§6) |
| Turn feels weak/strong vs. speed | Turning is vectored thrust — scales with thrust, not speed (§5) |

---

*Audience note: this guide favors the operational mental model over exhaustive
parameter lists. Every component, topic, and behavior above is drawn from the
launch and config in `unh_marine_autonomy`, `unh_marine_navigation`, and the
EchoBoat platform packages — verify against those before relying on a specific
value, since per-boat config changes in the field.*
