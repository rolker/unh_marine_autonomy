# marine_contacts

A lightweight, **Qt-free** home for building and curating
`marine_interfaces/msg/Contact` records. Depends only on `marine_interfaces` and
`rclcpp`, so any Contact producer can link it without pulling in a UI or
perception toolchain.

## What it provides (`marine_contacts::marine_contacts`)

- **`make_box_contact(points, id, source, frame, stamp_s)`** — build a human-origin
  (`ORIGIN_HUMAN`, `STATUS_PROPOSED`) `BOX` Contact from a map-frame footprint. The
  pose is the points' centroid, the BOX dimensions their extent. `geo_pose` is left
  unresolved (NaN latitude) — geodetic resolution is the caller's job.
- **`ContactStore`** — an in-memory set of contacts with a map-frame bounding-box
  query (`inBox`) and CDR `ContactArray` persistence (`save`/`load`), forward-
  compatible with the standalone contact-manager store (`unh_marine_autonomy#167`).
- **`export_contacts_geojson(contacts, path)`** — write geo-resolved contacts as an
  RFC 7946 GeoJSON `FeatureCollection` (skips contacts with no geo reference).

All of it is Qt-free and unit-tested without a display (`test_contact_store`).

## Linking it

```cmake
find_package(marine_contacts REQUIRED)
# ...
target_link_libraries(your_target marine_contacts::marine_contacts)
```

```cpp
#include "marine_contacts/contact_store.hpp"
```

## History

Extracted from `marine_perception_tools` so consumers (the live waterfall target
marker `rqt_operator_tools#86`, and future automated detectors) get the builder
without the offline sidescan-tuner dependency chain. See `marine_perception_tools`
for the original extraction (`#13`).
