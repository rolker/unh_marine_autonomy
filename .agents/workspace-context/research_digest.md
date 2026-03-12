# Research Digest: Marine Robotics

<!-- Last updated: 2026-03-12 -->
<!-- If older than 30 days, consider running /research --refresh; entries older than 90 days should be flagged for review -->

## ROS 2 Autonomous Surface Vehicles (ASVs)

**Added**: 2026-02-27 | **Sources**: [VRX GitHub](https://github.com/osrf/vrx), [RoboBoat 2026](https://roboboat.org/programs/2026/), [ROS Maritime Working Group](https://github.com/ros-maritime/community)

Key takeaways:
- ROS 2 is the dominant framework for ASV software stacks, with competition teams (RoboBoat, RoboSub) building full autonomy pipelines on ROS 2 Jazzy
- The ROS Maritime Working Group maintains shared packages for USVs and UUVs covering controls, simulation, and drivers
- Reinforcement learning approaches for ASV decision-making and control are an active research area, with recent work on distributional RL for integrated planning and control

**Relevance**: Direct alignment with this project's ASV platforms (EchoBoats, BEN); ROS Maritime packages may offer reusable components for navigation and control

---

## VRX Simulation (Gazebo Harmonic + ROS 2 Jazzy)

**Added**: 2026-02-27 | **Sources**: [VRX 3.0 Release](https://github.com/osrf/vrx/releases), [VRX Wiki](https://github.com/osrf/vrx/wiki/)

Key takeaways:
- VRX 3.0 runs on Gazebo Harmonic and ROS 2 Jazzy by default — matches this workspace's target platform
- Provides realistic ocean surface dynamics, wind, and wave effects for USV testing
- Older Gazebo-based maritime simulators (UUVSim, DAVE, WAVE) are unmaintained and lack ROS 2 support
- SMaRCSim is a newer alternative providing maritime simulation modules, but VRX remains the most mature USV option

**Relevance**: VRX 3.0's Jazzy/Harmonic alignment makes it a natural simulation companion for this workspace's `ben_gazebo` and `unh_marine_simulation` packages

---

## Hydrographic Survey Automation

**Added**: 2026-02-27 | **Sources**: [Hydrographic Survey Equipment Market Report 2026](https://www.globenewswire.com/news-release/2026/01/27/3226189/28124/en/Hydrographic-Survey-Equipment-Market-Report-2026-4-47-Bn-Opportunities-Trends-Competitive-Landscape-Strategies-and-Forecasts-2020-2025-2025-2030F-2035F.html), [Next-gen AUVs (Hydro International)](https://www.hydro-international.com/content/article/the-revolutionary-capabilities-of-next-generation-autonomous-underwater-vehicles)

Key takeaways:
- Market growing from $3.22B (2025) to $3.44B (2026) at 6.8% CAGR, driven by autonomous survey vessels and AI-based mapping
- AUV swarm intelligence is being operationalized — coordinated fleets increase data collection efficiency by >50% vs single-vehicle missions
- Autonomous launch and recovery can reduce vessel-based operational costs by up to 30%
- AI-driven adaptive mission planning allows real-time survey adjustments based on sensor feedback

**Relevance**: Validates the trajectory of UNH CCOM's hydrographic survey automation work; swarm coordination and adaptive planning are potential research directions for `manda_coverage`

---

## Marine Perception: Sonar and Deep Learning

**Added**: 2026-02-27 | **Sources**: [Sonar-based Deep Learning in Underwater Robotics](https://arxiv.org/html/2412.11840v1), [AI-Driven Marine Robotics (arXiv)](https://arxiv.org/html/2509.01878v1), [Sonar AI Review (J. Field Robotics)](https://onlinelibrary.wiley.com/doi/10.1002/rob.70077)

Key takeaways:
- Sonar-based DL perception covers classification, object detection, segmentation, and SLAM — but limited training data and inherent noise challenge model robustness
- Multi-modal fusion (sonar + lidar + cameras) is the emerging standard for comprehensive marine perception
- Gazebo-based simulators (DAVE, Stonefish) support virtual sonar sensors including forward-looking sonar (FLS) and side-scan sonar (SSS) for synthetic training data generation
- Seabed-to-sky mapping systems combining sonar and lidar provide unified above/below waterline spatial awareness

**Relevance**: Directly relevant to `unh_marine_perception` and `unh_marine_radar` packages; synthetic sonar data from simulation could augment training pipelines

---

## MOOS-IvP and ROS 2 Integration

**Added**: 2026-02-27 | **Sources**: [ROS-IvP Paper](http://www.mjbays.com/OCEANS%20Paper%20-%20ROS-IvP.pdf), [CoUGARs AUV Platform](https://arxiv.org/html/2511.08822v1), [MOOS-IvP Project](https://oceanai.mit.edu/moos-ivp/)

Key takeaways:
- Custom software bridges between ROS 2 and MOOS middlewares enable using MOOS-IvP's proven marine autonomy behaviors with ROS 2's ecosystem
- MOOS-IvP's IvP Helm provides a large library of maritime-specific autonomy behaviors and behavior arbitration that ROS 2 alone doesn't match
- Recent low-cost AUV platforms (CoUGARs, <$3K/unit) demonstrate hybrid MOOS-IvP + ROS 2 architectures in production
- The combination leverages MOOS-IvP for mission planning/behavior arbitration and ROS 2 for communications, perception, and low-level control

**Relevance**: Relevant to `unh_marine_autonomy`'s mission management architecture; MOOS-IvP integration patterns could inform behavior planning approaches

---

## NOAA Coast Pilot — Structured Marine Data for World Generation

**Added**: 2026-03-12 | **Sources**: [Coast Pilot portal](https://nauticalcharts.noaa.gov/publications/coast-pilot/index.html), [Coast Pilot XML viewer](https://nauticalcharts.noaa.gov/publications/coast-pilot/xml2html.html?book=2), [Coast Pilot e-Publishing (PDF)](https://ocsdata.ncd.noaa.gov/media/noaa-industry-day/2017/01_03_Coast_Pilot_Software_Integration.pdf), [InPort metadata](https://www.fisheries.noaa.gov/inport/item/39970), [XML example (CP5 Ch6)](https://nauticalcharts.noaa.gov/publications/coast-pilot/files/cp5/CPB5_C06_WEB.xml)

Key takeaways:
- The U.S. Coast Pilot is a 10-volume NOAA publication supplementing nautical charts with data that is difficult to portray on a chart: bridge clearances, wharf dimensions, anchorage depths, prominent features, pilotage, weather, ice conditions, and facility descriptions
- **XML format available**: chapters are published as XML alongside PDF/HTML; URL pattern `files/cp{N}/CPB{N}_C{NN}_WEB.xml`; weekly updates
- **Geotagged features**: `<CP_GEO_LOC>` elements carry `lat_dec`/`long_dec` decimal coordinates, GNIS source IDs, feature class (Bay, Building, Tower, etc.), and state/county
- **Bridge data**: `<bridge bridgeid="...">` elements link to clearance values in surrounding text; vertical and horizontal clearances are described in prose, not structured attributes — NLP extraction needed
- **Wharf/facility dimensions**: lengths, depths alongside, and construction materials are embedded in `<paraText>` prose, not machine-structured fields
- **Volume 1** covers Maine to Cape Cod (including NH/Portsmouth); **Volume 2** covers Cape Cod to Sandy Hook — these are the primary volumes for UNH operations
- The main extraction challenge is that numeric data (heights, clearances, depths) is in natural-language paragraphs, not dedicated XML attributes; the `<CP_GEO_LOC>` tags provide spatial anchoring but the measurements require text parsing

**Relevance to `marine_charts_to_gazebo_world`** (rolker/unh_marine_simulation#40):
- **Bridge enrichment**: S57 BRIDGE features often lack vertical clearance (VERCLR); Coast Pilot provides specific clearance values that could fill these gaps
- **Building/wharf heights**: the package currently uses hardcoded heights (12m buildings, 2.5m wharves); Coast Pilot describes actual wharf lengths, depths, and sometimes heights
- **Anchorage areas**: could be rendered as marked zones in the simulation world
- **Prominent features**: Coast Pilot describes towers, tanks, stacks, and other landmarks with locations — could improve feature placement beyond what S57 provides
- **Implementation approach**: a Coast Pilot XML parser would need (1) XML download/cache by volume+chapter, (2) `<CP_GEO_LOC>` extraction for spatial indexing, (3) regex/NLP extraction of numeric values from `<paraText>` for bridge clearances, wharf dimensions, and facility heights, (4) spatial matching to existing S57 features by proximity
