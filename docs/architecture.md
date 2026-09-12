# Source organization and refactoring boundaries

## Public product name and stable internal identifiers

The public Windows product name is **FlightRig** from version 0.13.0. Window
titles, reports, release archives, documentation and user-facing messages use
that name. The generated executable is `FlightRig.exe`, while the CMake target
remains `FlightBench`.

`FlightBench` is intentionally retained for the QML module URI, CMake/test
targets, serialized format identifiers such as `FlightBenchSession`, ROS topic
names, Docker network/container prefixes and `FLIGHTBENCH_*` environment
variables. These values are technical interfaces rather than visible branding;
renaming them would create compatibility risk without improving the interface.

The optional ROS 2 sensor image is user-visible setup material and therefore
uses the public name `flightrig/ros-sensor-bridge` from version 0.13.0.

User data created by the renamed product is stored under `Documents/FlightRig`.
No automatic migration from the former `Documents/FlightBench` directory is
performed. This keeps the brand change explicit and does not modify or delete
older files.

## Directory migration

The first structural change moves files without changing algorithms, settings
keys, session formats, flight commands, or QObject public interfaces.

- `src/app`: application coordination and QML facade.
- `src/flight`: vehicle communication and PX4 events.
- `src/mission`: route storage and deviation analysis.
- `src/testing`: plans, preflight evaluation and acceptance.
- `src/sessions`: recording, replay and reports.
- `src/sensors`: sensor gateway and clock synchronization.
- `src/mapping`: world geometry, trajectories and mapping.
- `src/inspection`: visual survey and gimbal inspection.
- `src/infrastructure`: Docker and filesystem locations.

Internal C++ includes use paths relative to `src`, such as
`#include "sessions/replaycontroller.h"`. Do not add all subdirectories to the
include path: doing so hides ownership and allows ambiguous header names.

QML keeps one `FlightBench` module. `controls` holds generic widgets; `features`
holds business-specific components. `pages` and `shell` remain composition roots.
Use explicit relative imports for feature/control dependencies and preserve the
`App` facade until a separately tested interface migration is warranted.

## Subsequent changes (not completed by moving files)

Settings row extraction is implemented: eight local components are standalone
`Settings*` types with explicit column-width inputs and navigation signals.
Six settings pages are now standalone components. They retain the existing
App/Theme settings bindings, and expose return/colour/import/reset/trash signals
to `SettingsCenter`, which owns navigation and confirmation dialogs. No parent
IDs are accessed from a page. Column widths remain explicit inputs.
Records now delegates acceptance rendering to `SessionAcceptanceSection` using
a session snapshot, and the lazily loaded historical depth map to
`SessionDepthMapDialog` with an explicit host. Session selection, deletion,
export and replay coordination remain owned by `RecordsPage`.
`SettingsRows` tests cover navigation/toggle signals and right-edge alignment.
`UiSections` covers extracted page navigation and empty/populated acceptance
sections at multiple widths and in both themes. Further record panels and
controller extraction remain unfinished.

`SessionSensorSummary` renders a supplied sensor summary and emits history-map
and delete requests. The records page still loads historical data and opens
confirmation dialogs; the card never deletes data or controls App services.
`WaypointEditorPanel` displays the selected model entry supplied by
`MissionPlanner`. Edits are signals back to the planner's existing
`setPointValue`; the panel does not copy the route model or persist settings.
End-action changes, dirty state, deletion and focus restoration are owned by
the planner. `FeaturePanels` tests cover live ListModel role updates, selection
changes, numeric edit forwarding, locked controls, end-action requests, and
sensor-card actions. The map canvas, route library and mission execution remain
in MissionPlanner and are not yet extracted, except the route-library dialog:
`RouteLibraryDialog` now receives routes, selection, operation text and the
duration formatter explicitly. It emits selection/load/new/delete/duplicate/
rename requests. The planner retains confirmation dialogs, repository calls,
selected-route state and load-time reanchoring. Feature-panel tests cover an
empty library, list replacement, keyboard selection/loading and locked actions.

`RouteMapPainter.js` is a stateless Canvas renderer. The planner supplies an
explicit projection/model/style input object at paint time; the renderer has no
App, Theme, settings, timers or model mutations. Existing repaint scheduling,
map interaction and coordinate functions remain in the planner. Command-level
tests cover north/east projection, mission visibility independent of actual
track visibility, active/completed/risky segments, depth collision markers,
track discontinuities and drawing-state restoration. This does not yet extract
the interactive map shell or the flight-execution controller.

The background mouse handler is now `MissionMapInteraction`: it emits pixel
pan/click/zoom requests and owns only gesture-local state. The planner retains
east/north conversion, zoom anchoring, route mutation and viewport state.
Interaction tests exercise left clicks, middle clicks, drags and editing locks;
the 4-pixel drag threshold is unchanged. Waypoint marker dragging remains in
the planner and is separate from background-map gestures.

`WaypointMapMarker` now owns marker appearance and local drag gesture state.
Required ListModel roles remain index/eastMeters/northMeters; the planner binds
projection, risk and selection, and handles requested coordinate updates with
the original 0.1-metre rounding, dirty flag and safety/repaint calls. The marker
does not mutate the model. Tests instantiate it through a Repeater to verify
inherited role injection, selection, pixel drag offsets and locked behavior.

1. Extract settings sections and record detail panels with explicit properties
   and signals; preserve layout, focus and scrolling behavior.
2. Extract mission editor panels while retaining one authoritative route model.
3. Extract AppController settings and runtime coordination behind its existing
   public API. Preserve sensor finalization before session shutdown.
4. Separate sensor transport, recording and presentation; preserve thread
   affinity, bounded queues and destruction order.
5. Separate mapping kernels from rendering/export, using identical captured
   inputs and numerical output comparisons.

Moving files is not responsibility separation. Do not count splitting a class
across several `.cpp` files as completing its architectural refactor.

## Verification gates

### Mapping round

DepthSurfaceGeometry is now a separate Quick3D presentation class with its own
MOC registration. DepthVoxelCoordinates supplies the unchanged signed coordinate
packing to both fusion and geometry. DepthMapExport receives stable surface
centres and bounds to write OBJ/PNG without depending on a controller or Quick3D
object. PLY/CSV writing also delegates there using the shared DepthVoxel value
type and read-only references, preserving column names and numeric precision.
DepthMapReader streams current-format CSV cells into the controller's chunk index,
preserving file order and clear-after-open behavior. The controller still
applies metadata and owns fusion/surface selection. Rendering topology, mesh
scale, colours and precision are unchanged. The depth regression exports only
into a fresh QTemporaryDir rather than deleting a working-directory test folder.
LidarMapOutput owns raster presentation and PNG/PGM/YAML/JSON/pose-graph
export. Its synchronous snapshot borrows const grid/track/graph references;
it neither copies the large map nor modifies SLAM evidence. LidarMapTypes
shares the original scan, pose, keyframe and constraint value types.
Scan matching, loop closure, graph optimization and occupancy updates remain
together in LidarMappingController. Resolution, thresholds and file schemas
are unchanged. The unused controller pose-graph forwarding method was removed.

The second structural round is complete at this boundary. QML remains a single
FlightBench module: private RouteMapPainter.js must keep
QT_QML_SKIP_QMLDIR_ENTRY to avoid duplicate module discovery. All QML files
passed qmllint; the 15 feature/settings/acceptance/gimbal layout tests passed
at scales 1, 1.25 and 1.5. These tests are not full-application visual or SITL
acceptance. Larger controllers are intentionally retained where they own one
cohesive algorithm or orchestration policy, rather than split by line count.

### Session and sensor I/O extraction

- `sensors/SensorSessionStorage` owns the session sensor directory and open
  timeline file. Directory/timeline creation failures are returned explicitly;
  the service retains the original preview fallback, messages, quotas, sample
  counts and error signals. Frame writes, periodic flush, close and atomic
  manifest writes are delegated without changing their caller ordering.
  SessionIo tests include creation failures and manifest writing after closing
  the timeline; the existing sensor tests still exercise recording limits.

- `sensors/MjpegAviWriter` owns the AVI file, frame index, sizes and header
  patching. SensorServiceController retains stream selection, status, capture
  path, notifications and start/stop decisions. MJPEG bytes, 10 Hz header and
  frame-dimension filtering are unchanged; this is not a codec upgrade.
- `sessions/ReplayData` owns CSV parsing and the sensor timeline index.
  ReplayController keeps its public Sample alias and playback API. The index
  stores lightweight metadata and byte offsets; large depth/scan fields remain
  lazily read. Target and actual gimbal samples stay separate. No second
  playback clock or copied index is introduced.
- `sessions/SessionArchive` owns completed/trash catalog loading for
  `FlightBenchSession` format version 5 and the 50-session limit. Older formats
  are ignored without repair or migration. The recorder still owns active
  recording, timers, signals and destructive operations.
- `FlightBench.SessionIo` covers AVI layout/index/padding, empty recordings,
  timeline metadata stripping/lazy reads, gimbal sample separation and CSV
  duplicate/out-of-order handling. Existing replay, sensor and recorder tests
  are additionally compiled and run against the extracted implementations.

The first application/data-flow round now additionally includes:

- `SensorSessionRecorder` owns recording state, sampling intervals, per-stream
  statistics, capacity protection, expected-sample integration and final summary.
  The service forwards received data and dropout notifications; it no longer
  keeps duplicate session counters. Synchronous callbacks read current performance
  settings and forward existing notifications without adding a thread or timer.
- `SimulationRunCoordinator` owns start-gate ordering, landing/stop guards,
  deferred landing confirmation and the pending restart flag. The facade supplies
  live state queries and actions, including its context-bound timer. The original
  600 ms recheck and finalization-before-stop order are retained.
- `FlightBench.RunCoordinator` covers start short-circuit order, airborne/armed
  blocking, busy commands, re-arming/disconnection during deferred confirmation,
  duplicate confirmations and one-shot restart consumption.

AppController remains the composition root for signal wiring, selected plans
and application-specific mission policy. SensorServiceController remains the
live transport/preview controller. Mapping kernel/export separation and the
second-round UI structural review are complete; complete application linking and SITL
verification are separate from these targeted compiled regression tests.

`sessions/SensorSessionFinalizer` now owns synchronous sensor-session closing:
pause inspection, finalize sensor recording, save derived maps/inspection data,
rewrite the sensor manifest, and attach the summary to the active flight session.
It borrows existing controllers and owns no QObject, timer or duplicate state.
AppController retains shutdown guards, landing confirmation, service/container
stop calls and destructor disconnect order. All existing closing paths still
delegate before stopping services, now through the run coordinator. The extracted
body was compared with its original
(identifier substitution only), compiled with the project's C++20 setting, and
registered in all three AppController build targets. Full runtime shutdown
verification still requires an application build and flight-session regression.

The first C++ extraction is `infrastructure/simulationcatalog`: it owns the
model definitions and cached on-disk world-catalog lookup previously local to
AppController. Definitions, order, validation, application-relative path and
fallback semantics are unchanged. AppController delegates lookup while keeping
its public QML API and all selection/flight state. Every CMake target compiling
AppController also compiles the catalog. This is not the pending runtime or
settings-controller extraction; those responsibilities remain in AppController.

`RuntimeInformation` owns read-only application/Qt/MAVSDK/compiler/OS identity.
AppController keeps its public getters as forwarding methods so QML, reports
and diagnostics retain the same API. The implementation is compiled into each
existing AppController target with that target's original compile definitions;
there is no separate library with potentially different version/build flags.

`ApplicationPreferences` owns the 15 view/interaction/recording preference
values, their load-time defaults, setter validation and persistence. AppController
getters forward to this one instance; it no longer duplicates these fields.
Signals, sample-timer changes and trash cleanup remain in the facade after a
successful change. Reset/import application still calls those same facade
setters to preserve side effects and ordering. The settings-document snapshot,
schema validation and atomic export writing are owned by ApplicationPreferences.
An optional injected QSettings store supports isolated temporary-file tests.
Simulation selection and QML-owned appearance state remain outside this value
owner. Geometry and vehicle values retain their existing authoritative owners;
their persistence boundary is described below.
`FlightBench.Preferences` covers defaults, bounds, enums, persistence/reload,
document round trips, malformed/version-mismatched input and export failure.

Flight-path visibility, automatic clearing and colour now also belong to
ApplicationPreferences. Width/length persistence is delegated there, but their
authoritative normalized values remain in FlightPathGeometry: the facade still
updates live and replay geometry in the original order before persisting and
notifying. This avoids a second width/length state or changed geometry rules.

Flight control settings now use a read-only `FlightControls` restoration value.
VehicleConnection still validates and owns all live altitude/speed/yaw values.
Only its accepted values are persisted. Legacy speed migration is explicitly
completed after the vehicle setter, and reading settings never mutates them.
Tests cover old modes (including out-of-range values), precedence of the new
key, migration completion and all four persistence keys.

- Reconfigure CMake after source-path changes; check every test target, not only
  the main executable. Keep explicit resource/source registration.
- Run QML lint and responsive layout tests. Then build and launch the actual
  application before moving on to behavioral ownership changes.
- Recordings and derived maps support only their current schema versions. Loading
  never upgrades or repairs an older format, and never deletes unsupported data.
  Plan and persistent-setting compatibility remain separate concerns.
- Check publishing and test scripts for old paths.
- Each logical change must be independently reviewable/revertible. Do not mix
  a structural refactor with dependency upgrades, threshold tuning or new flight
  behavior. Git commits require the user's authorization.
