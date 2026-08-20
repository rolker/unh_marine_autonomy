# marine_vertical_datum

ROS-free vertical-datum resolution library
([ADR-0010 D6](../docs/decisions/0010-geospatial-world-model.md)): answers
"what is the chart datum (MLLW, optionally MHHW) relative to the WGS84
ellipsoid at this point?" for chart importers (`s57_tools`), operator-side
display (CAMP), and `mru_transform`'s `chart_datum_node`.

Datum conversion happens **at import time, wherever import runs** — the
navigation runtime never consults this library or its grids (ADR-0010 D5).

## API

All in `namespace marine_vertical_datum`. No `rclcpp` anywhere; PROJ and
yaml-cpp are private link dependencies (the headers are PROJ-free).

### `vdatum_query.hpp` — the VDatum/PROJ grid query

| Symbol | Purpose |
|---|---|
| `VDatumConfig{geoid_grid, vdatum_grid_dir}` | Paths to the geoid file and the directory of regional `*_mllw*.gtx` / `*_mhhw*.gtx` grids |
| `VDatumQueryFn` | `(lat, lon) → std::optional<VDatumResult>`; nullopt = no MLLW coverage there (a normal gap, no diagnostic) |
| `DiagFn` | Optional setup-diagnostic sink (`std::function<void(const std::string&)>`); default discards |
| `make_vdatum_query(config, diag)` | **The production entry.** Builds the PROJ context (networking disabled — strictly offline) and pipelines **once**; returns an empty function on setup failure (test `if (query)`). Call the factory once, then query per point — never rebuild per point. |

### `datum_config.hpp` — the precedence-chain resolver

| Symbol | Purpose |
|---|---|
| `resolve_datum(lat, lon, lake_datum, lake_datum_mhhw, vdatum, entries)` | The full chain: lake param → override polygons → VDatum result → fallback polygons → `nullopt` |
| `load_datum_config(path)` | Parse the `datum_polygons:` YAML (see `mru_transform`'s `config/datum_polygons.example.yaml`); throws `std::runtime_error` on malformed input |
| `point_in_ring(lat, lon, ring)` | Ray-casting point-in-polygon (boundary counts as inside; no antimeridian support) |
| `DatumEntry`, `VDatumResult`, `DatumResult`, `DatumSource`, `LatLon` | Value types for the above |

Typical import-time wiring:

```cpp
auto diag = [](const std::string & m) {std::cerr << m << "\n";};
auto vdatum = marine_vertical_datum::make_vdatum_query(
  {"/path/us_noaa_g2018u0.tif", "/path/vdatum"}, diag);
auto entries = marine_vertical_datum::load_datum_config("datum_polygons.yaml");

// per point / per cell:
auto result = marine_vertical_datum::resolve_datum(
  lat, lon, lake_datum, lake_datum_mhhw,
  vdatum ? vdatum(lat, lon) : std::nullopt, entries);
```

Sign convention: heights are **signed metres relative to the WGS84
ellipsoid** (negative when the datum surface is below the ellipsoid),
matching the bathymetry store's convention.

## Grid provisioning

Grids live **wherever imports run** — dev machines and the boat as offline
tooling — never on the navigation runtime (ADR-0010 D6/D7).

The **canonical on-host location** is the world tree (ADR-0010 D3, amended
by [#288](https://github.com/rolker/unh_marine_autonomy/issues/288)):
`~/data/world/datum/geoid/` for the geoid file and `~/data/world/datum/vdatum/`
for the regional `.gtx` directory. Provisioning these grids into the world tree
is an intended updater/provisioning step (a queued `s57_tools` follow-on); until
it lands, populate them manually as below.

- **Geoid** (ellipsoid → NAVD88): e.g. `us_noaa_g2018u0.tif`, via
  `projsync --file us_noaa_g2018u0.tif` or from
  <https://cdn.proj.org/>.
- **VDatum regional grids** (NAVD88 → MLLW/MHHW): download the regional
  VDatum grid bundles from NOAA (<https://vdatum.noaa.gov/>) and extract the
  `*.gtx` files into one directory; the query scans it recursively for
  `*_mllw*.gtx` / `*_mhhw*.gtx`.
- Point `VDatumConfig` at both — pass **literal absolute paths**; the library
  does not expand `~` or environment variables, so a tilde form is used as-is
  and will not resolve. Canonically
  `{"/home/<user>/data/world/datum/geoid/us_noaa_g2018u0.tif", "/home/<user>/data/world/datum/vdatum"}`.
  PROJ networking is disabled in this library, so nothing is fetched at query
  time — missing grids fail setup loudly via `DiagFn`.

Where VDatum has no coverage (inland lakes), use the polygon config /
lake-datum override instead — that is the precedence chain's job.

## Testing

`test_datum_config` covers the precedence chain and YAML parsing;
`test_vdatum_query` covers the injectable seam (`VDatumQueryFn` stubs) and
the factory's grids-free failure paths. CI needs **no PROJ grids**: the seam
makes consumers testable without them, and factory setup-validation fails
before any grid is read.

## Consumers

- `s57_tools` chart exporter (per-cell chart-datum → ellipsoid at export).
- CAMP (display-time chart-datum readouts, operator side).
- `mru_transform` `chart_datum_node` — still carries its own inline copy;
  migrating it onto this library is a tracked follow-on
  (see [#274](https://github.com/rolker/unh_marine_autonomy/issues/274)).
