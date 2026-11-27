# Plan: contact_manager — CRUD .srv + manager node + standalone store + distribution (v1 core)

## Issue

https://github.com/rolker/unh_marine_autonomy/issues/167

## Context

The unified perception `Contact` / `ContactArray` messages landed in
`marine_interfaces` (#156, ADR-0004). This issue (#167) builds the **v1 core**
of the contact manager (umbrella #157): the operator-facing CRUD service, an
L3 manager node that owns the curated `Contact` set, a standalone persistence
store, and the distribution topic that publishes the set. The three design
forks are already resolved on #157 (see "Design forks resolved" comment) and
are captured here — they are **not** re-litigated:

- **Fork 1 (CAMP interaction)**: one-way + confirm. The `.srv` is full-CRUD
  from day one, but v1 producers are limited — rqt_operator_tools#59 is the
  sole creator (separate issue) and CAMP rendering is camp#95 (separate).
- **Fork 2 (persistence)**: lean **standalone SQLite** (write-on-mutation,
  load-on-startup) behind a clean interface, **not** the #86 bathy-store.
- **Fork 3 (package)**: a **new `contact_manager` package** inside
  `unh_marine_autonomy`, sibling to `marine_bathymetry_store`. The CRUD `.srv`
  lives in `marine_interfaces`.

Distribution follows the **ADR-0003 / marine_control `state` pattern**: a
`RELIABLE` + `VOLATILE` `ContactArray` "state" topic republished on a periodic
heartbeat (never `TRANSIENT_LOCAL` — a late joiner over `udp_bridge` must get
fresh state from the heartbeat, not a stale latched sample). CRUD mutations go
through the service; the resulting set is published on the state topic.

## Language decision

**Recommend C++.** Rationale: the issue names `marine_bathymetry_store` as the
sibling precedent, and that package is C++ with a "clean library behind an
interface + thin tests" shape that maps directly onto Fork 2's
"persistence behind a clean interface" requirement. C++ also keeps the SQLite
store and the curated-set CRUD logic in one statically-typed, gtest-covered
library that the ROS node wraps thinly — matching the bathy-store layering
(`BathymetryStore` library + `tile_io` persistence free functions, tested
independently of any ROS spin). The counter-precedent is `mission_manager`,
the repo's other "manager" node, which is Python. This is flagged as the lead
**Open Question** for Roland — if he prefers Python (`sqlite3` stdlib, faster
iteration, matches mission_manager), the package layout below maps cleanly to
an `ament_python` package. The plan is written C++-first; the Python variant
is noted where it diverges.

## Approach

1. **CRUD `.srv` in `marine_interfaces`** — add a `srv/` directory with four
   services operating on `Contact`:
   - `AddContact.srv` — request `Contact contact` (id may be empty → server
     assigns); response `string id`, `bool success`, `string message`.
   - `UpdateContact.srv` — request `Contact contact` (id required); response
     `bool success`, `string message`. Covers status transitions
     (PROPOSED→CONFIRMED/REJECTED) and `note` edits — the v1 CAMP path.
   - `DeleteContact.srv` — request `string id`; response `bool success`,
     `string message`.
   - `ListContacts.srv` — empty request; response `Contact[] contacts`.
   Wire into `marine_interfaces/CMakeLists.txt` (`SRV_FILES` +
   `rosidl_generate_interfaces`) and confirm `package.xml` deps already cover
   `Contact`'s transitive deps (they do — no new dep). Service-for-mutation /
   topic-for-state split aligns with ADR-0003 D1 and ADR-0008.

2. **New `contact_manager` package** (`ament_cmake`, C++) — sibling to
   `marine_bathymetry_store`. Layered like the bathy-store:
   - **`ContactStore` (in-memory curated set + lifecycle logic)** — pure
     library, no ROS spin. Holds `std::map<std::string, Contact>`; methods
     `add` (assigns id if empty; sets `origin_kind`/`status` per the caller's
     intent — human/confirmed vs auto/proposed), `update`, `remove`, `list`,
     and a `transition(id, status)` helper enforcing valid lifecycle moves
     (PROPOSED→CONFIRMED, PROPOSED→REJECTED; reject illegal transitions).
   - **`ContactPersistence` interface + `SqliteContactStore` impl** — clean
     abstract interface (`load() -> vector<Contact>`, `upsert(Contact)`,
     `erase(id)`) so a future content-hash sync backend can drop in (Fork 2).
     SQLite: one `contacts` table keyed by `id`, `Contact` serialized
     (CDR blob via `rclcpp::Serialization`, or explicit columns — decide in
     impl; CDR blob is simplest and round-trips the whole message). Write on
     every mutation; load the full set on startup.
   - **`ContactManagerNode` (thin ROS wrapper)** — owns a `ContactStore` +
     a `ContactPersistence`. Advertises the four services; on any successful
     mutation, persists (write-on-mutation) and republishes the full
     `ContactArray` on the `state` topic. Publishes the state on a periodic
     heartbeat timer (ADR-0003 D5 QoS). On construction, loads from the store
     into memory and publishes the initial state.

3. **Standalone SQLite store** — implemented as the `SqliteContactStore` in
   step 2; DB path is a node parameter (default under a per-platform data dir,
   matching how bathy-store dirs are configured). Round-trip tested
   independently of ROS.

4. **Distribution: `ContactArray` state topic** — topic name
   `~/contacts` (resolved under the node namespace, e.g.
   `marine/contacts` to match the `marine/` convention). QoS: `RELIABLE`,
   `VOLATILE`, depth 1; periodic heartbeat republish (parameterized period,
   default ~1 Hz). Explicitly **not** `TRANSIENT_LOCAL` (ADR-0003 D5). The
   consumer pattern is CAMP's `AISManager`, which subscribes and rebuilds a
   contact map from each message — a full-set `ContactArray` heartbeat serves
   that directly.

5. **Tests** (gtest, mirroring bathy-store's `test/`):
   - `test_contact_store.cpp` — CRUD ops (add assigns id, update mutates,
     delete removes, list returns set); lifecycle transitions (valid moves
     succeed, illegal moves rejected); origin/status defaulting on add.
   - `test_sqlite_store.cpp` — persistence round-trip: upsert N contacts,
     reopen the DB, `load()` returns the same set; erase persists; load on
     empty DB is empty.
   - Optionally a launch_test for the node's service round-trip if cheap;
     otherwise the library tests cover the logic and the node is thin glue.

6. **Project ADR-0005** — `docs/decisions/0005-contact-manager-architecture.md`,
   mirroring ADR-0004's format (Status / Context / Decision Dn / Consequences /
   References). Records: service-for-CRUD + topic-for-state (aligned to
   ADR-0003), the three resolved forks, the persistence-behind-an-interface
   choice, the package placement, and the language decision.

7. **Consequences carried in-PR** —
   - `marine_interfaces`: new `srv/` files → `CMakeLists.txt`, `package.xml`
     (verify no new dep), `.agents/README` interface count (39 msg → "+ 4 srv").
   - New package → `.agents/README` Package Inventory + Repository Layout +
     Build-order notes (`marine_interfaces` before `contact_manager`); launch
     wiring (add a `contact_manager_launch.py` and include it from
     `robot_core_launch.py`, mirroring how `mission_manager` is included).
   - `contact_manager/README.md` (package doc) per the documentation workflow.

## Files to Change

| File | Change |
|------|--------|
| `marine_interfaces/srv/AddContact.srv` | New — add contact, returns assigned id |
| `marine_interfaces/srv/UpdateContact.srv` | New — update contact (status/note) |
| `marine_interfaces/srv/DeleteContact.srv` | New — delete by id |
| `marine_interfaces/srv/ListContacts.srv` | New — list full set |
| `marine_interfaces/CMakeLists.txt` | Add `SRV_FILES` + pass to `rosidl_generate_interfaces` |
| `marine_interfaces/package.xml` | Verify deps (likely no change; `Contact` deps already present) |
| `contact_manager/package.xml` | New — `ament_cmake`, deps: `rclcpp`, `marine_interfaces`, SQLite |
| `contact_manager/CMakeLists.txt` | New — library + node executable + gtests |
| `contact_manager/include/contact_manager/contact_store.hpp` | New — in-memory set + lifecycle |
| `contact_manager/include/contact_manager/contact_persistence.hpp` | New — abstract persistence interface |
| `contact_manager/include/contact_manager/sqlite_contact_store.hpp` | New — SQLite impl |
| `contact_manager/src/contact_store.cpp` | New — CRUD + lifecycle logic |
| `contact_manager/src/sqlite_contact_store.cpp` | New — SQLite load/upsert/erase |
| `contact_manager/src/contact_manager_node.cpp` | New — ROS node: services + state topic |
| `contact_manager/launch/contact_manager_launch.py` | New — launch the node |
| `contact_manager/test/test_contact_store.cpp` | New — CRUD + lifecycle tests |
| `contact_manager/test/test_sqlite_store.cpp` | New — persistence round-trip tests |
| `contact_manager/README.md` | New — package documentation |
| `docs/decisions/0005-contact-manager-architecture.md` | New — ADR-0005 |
| `marine_autonomy/launch/robot_core_launch.py` | Include `contact_manager_launch.py` |
| `.agents/README.md` | Package inventory + layout + build order + interface count |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Human control & transparency | Operator CRUD + PROPOSED→CONFIRMED/REJECTED lifecycle is the human-in-the-loop shape; service is the explicit operator action, state topic is the transparent current set. |
| Only what's needed | Full-CRUD srv but v1 single producer; lean standalone SQLite (no #86 coupling); thin node over a tested library. No multi-producer ingest, no in-CAMP edit. |
| Capture decisions | ADR-0005 records the three forks + distribution contract + language choice (mirrors ADR-0004). |
| A change includes its consequences | `.agents/README`, CMake/package.xml, launch wiring, package README all in-PR. |
| Test what breaks | Real logic (CRUD, lifecycle transitions, persistence round-trip) gets gtest; framework glue does not. |
| Improve incrementally | This is the carved v1 core slice of umbrella #157; CAMP (camp#95) and producer (#59) are separate sub-issues. |
| Modularity & decoupling (proj) | One focused package; persistence behind a clean interface; node is a thin wrapper over a ROS-free library. |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| proj ADR-0003 (marine_control state/change) | Yes (alignment) | State topic uses the `state` shape + D5 QoS (RELIABLE/VOLATILE/heartbeat, never TRANSIENT_LOCAL); mutations via service (D1: services can't cross udp_bridge, but CRUD is operator-station-local; the bridged surface is the state topic). |
| proj ADR-0004 (unified Contact) | Yes | Consumes `Contact`/`ContactArray` as-is; honors status enum + geo_pose/unset conventions; ADR-0005 mirrors its format. |
| proj ADR-0002 (bathy store) | No (scoping) | Explicitly NOT reused (Fork 2); persistence is standalone SQLite, not GGGS tiles. |
| ws ADR-0008 (ROS 2 conventions) | Yes | srv naming, `marine/` namespace, REP-compliant topic/QoS choices. |
| ws ADR-0001 (adopt ADRs) | Yes | New project ADR-0005 captures the manager architecture. |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| Add `.srv` to `marine_interfaces` | CMakeLists, package.xml, `.agents/README` interface count, build order | Yes |
| Add new `contact_manager` package | `.agents/README` inventory + layout + build order; launch wiring | Yes |
| Add distribution topic | ops-bag record list (which topics get recorded) | No — flag as follow-up (bag config lives in platform/config repos, not this repo) |
| Producer must emit `Contact` | rqt_operator_tools#59 | No — separate sub-issue, out of scope |
| CAMP must render contacts | camp#95 | No — separate sub-issue, out of scope |

## Open Questions

- **Language: C++ (recommended, matches `marine_bathymetry_store` sibling +
  clean-library-behind-interface shape) vs Python (matches `mission_manager`,
  the repo's other manager node; faster iteration, `sqlite3` stdlib).** Lead
  question for Roland. Plan is written C++-first.
- **State topic name / namespace** — `~/contacts` resolving to `marine/contacts`
  under the node namespace? Confirm it fits the `marine/` convention and the
  bridge config naming.
- **SQLite serialization** — store `Contact` as a CDR blob (simplest, whole-msg
  round-trip) vs explicit columns (queryable but couples schema to the msg).
  Recommend CDR blob for v1; decide in implementation.
- **Where does the node run** — boat-side, operator-side, or both? Affects
  launch wiring (robot_core vs operator_core) and which direction the state
  topic is bridged. Default assumption: boat-side curated set, state bridged
  boat→operator (matches the `state` distribution pattern); confirm.

## Estimated Scope

Single PR (the v1 core slice). It spans two packages (`marine_interfaces` srv +
new `contact_manager`) but they are one logical, build-ordered change reviewed
together, with ADR-0005 and the `.agents/README`/launch consequences in-PR.
If review prefers, the `.srv` addition could split into its own PR ahead of the
node, but the issue scopes them as one slice.

## Implementation Notes

_(none yet — populated during implementation for rationale-bearing design pivots)_
