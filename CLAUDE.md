# CLAUDE.md

## Build & Run

PrusaSlicer uses CMake presets. Build deps and the app in one step:

```bash
cmake --preset default -DPrusaSlicer_BUILD_DEPS=ON
cmake --build build-default -j$(sysctl -n hw.ncpu)
```

The binary is at `build-default/src/prusa-slicer`.

### Available Presets

| Preset | Description |
|--------|-------------|
| `default` | Static linking, Release, GTK3, PCH enabled |
| `no-occt` | Same as default, no STEP file support |
| `mac_universal_arm` | macOS arm64 slice for universal binary (no OCCT) |
| `mac_universal_x86` | macOS x86_64 slice for universal binary (no OCCT) |
| `shareddeps` | Dynamic linking, uses system libraries |

Deps presets (in `deps/CMakePresets.json`): `default`, `no-occt`, `mac_universal_x86`, `mac_universal_arm`.

### Key Build Options

```
-DSLIC3R_GUI=OFF                 # CLI only, no GUI
-DSLIC3R_ENABLE_FORMAT_STEP=OFF  # Disable STEP file support
-DSLIC3R_PCH=OFF                 # Disable precompiled headers
-DSLIC3R_ASAN=ON                 # AddressSanitizer
-DSLIC3R_UBSAN=ON                # UndefinedBehaviorSanitizer
-DSLIC3R_STATIC=OFF              # Dynamic linking (default ON on macOS/Windows)
-DSLIC3R_GTK=2                   # GTK version for wxWidgets (Linux, default 2)
-DSLIC3R_BUILD_SANDBOXES=ON      # Dev sandboxes
-DSLIC3R_BUILD_TESTS=OFF         # Disable tests
-DCMAKE_BUILD_TYPE=Debug         # Debug build (enables asserts in tests)
```

### Homebrew Conflicts

Homebrew `boost`, `eigen`, or `glew` can conflict with the deps build. Uninstall them if you hit cmake errors:

```bash
brew uninstall boost eigen glew
```

## Test

Test framework: Catch2 3.8+

```bash
cd build-default
make test                                    # all tests
./tests/libslic3r/libslic3r_tests           # core library tests
./tests/fff_print/fff_print_tests           # FFF print tests
./tests/sla_print/sla_print_tests           # SLA print tests
./tests/arrange/arrange_tests               # arrangement tests
./tests/thumbnails/thumbnails_tests         # thumbnail tests
./tests/slic3rutils/slic3rutils_tests       # utility tests (GUI only)
```

For full assert coverage, build with `-DCMAKE_BUILD_TYPE=Debug`.

Test data lives in `tests/data/`. Shared test data defined in `tests/data/prusaparts.cpp`.

The `tests/cpp17/` suite is `EXCLUDE_FROM_ALL` (not built by default).

## Architecture

### Slicing Pipeline

```
Input files (STL/3MF/AMF/OBJ/STEP)
  ↓
Model (ModelObject → ModelVolume → ModelInstance)
  ↓
Print (PrintObject → PrintRegion → PrintInstance)
  ↓
Per-object slicing steps (enum PrintObjectStep):
  posSlice             → Mesh → 2D cross-sections (TriangleMeshSlicer)
  posPerimeters        → Shell generation (Arachne or Classic engine)
  posPrepareInfill     → Top/bottom/internal surface detection
  posInfill            → Fill pattern generation
  posIroning           → Optional top surface smoothing
  posSupportSpotsSearch → Support point detection
  posSupportMaterial   → Support structure generation (grid/tree/snug)
  posEstimateCurledExtrusions → Warp prediction
  posCalculateOverhangingPerimeters → Overhang detection
  ↓
Print-level steps (enum PrintStep):
  psWipeTower          → Tool change / wipe tower planning
  psAlertWhenSupportsNeeded → Support requirement warning
  psSkirtBrim          → Skirt and brim generation
  psGCodeExport        → G-code file output
  ↓
GCodeGenerator::do_export
  → Order extrusions per layer
  → Travel moves (retract, avoid perimeters)
  → Extrusion moves (perimeters → infill → ironing)
  → Post-processors: CoolingBuffer, PressureEqualizer, SpiralVase, FindReplace
  ↓
Output .gcode / .bgcode
```

### Entry Points

- **`src/PrusaSlicer.cpp`** — main entry, dispatches to `Slic3r::CLI::run()`
- **`src/CLI/Run.cpp`** — CLI orchestrator: `setup()` → `load_print_data()` → `process_transform()` → `process_actions()` → optionally `start_gui_with_params()`
- **`src/CLI/Setup.cpp`** — argument parsing (5 config types: input, overrides, transform, misc, actions)
- **`src/CLI/LoadPrintData.cpp`** — model/config loading and merging
- **`src/CLI/ProcessActions.cpp`** — slicing and export logic

### Key Data Structures

| Type | File | Purpose |
|------|------|---------|
| `Model` | `Model.hpp` | Top-level container for all printable objects |
| `ModelObject` | `Model.hpp` | Single 3D object with volumes and instances |
| `ModelVolume` | `Model.hpp` | Individual mesh with config overrides |
| `ModelInstance` | `Model.hpp` | Positioned/rotated copy of a ModelObject |
| `Print` | `Print.hpp` | Main slicing orchestrator |
| `PrintObject` | `Print.hpp` | Per-object slicing state (layers, support, extrusions) |
| `PrintRegion` | `Print.hpp` | Shared config for volumes with same extrusion settings |
| `Layer` | `Layer.hpp` | 2D cross-section at a specific Z height |
| `LayerRegion` | `Layer.hpp` | Layer slice for a specific PrintRegion |
| `LayerSlice` | `Layer.hpp` | Connected island of a layer |
| `GCodeGenerator` | `GCode.hpp` | G-code exporter |
| `ExtrusionEntity` | — | Individual extrusion path with role |
| `ExPolygon` | — | Polygon with holes (primary 2D geometry type) |
| `Point` | — | 2D integer coordinates (scaled microns) |
| `Surface` | — | Connected region with role (top/bottom/internal) |

### Configuration System

Three-level architecture:

1. **`DynamicPrintConfig`** (`PrintConfig.hpp`, inherits from `DynamicConfig` in `Config.hpp`) — runtime key-value pairs, used by GUI, supports arbitrary merging
2. **`StaticPrintConfig`** (`PrintConfig.hpp`) — compile-time typed config hierarchy:
   ```
   FullPrintConfig
     ├── PrintObjectConfig   (per-object: support, infill, layer height)
     ├── PrintRegionConfig   (per-region: perimeter width, extrusion multiplier)
     └── PrintConfig         (global: printer, material, speed)
         └── GCodeConfig     (post-processor config)
   ```
3. **`AppConfig`** (`AppConfig.hpp`) — user preferences, window state, paths

Config inheritance: Model object config → material/print profiles → layer range overrides. PrintRegions created by config hash matching (volumes with same settings share a region).

### Threading Model

- **Library:** Intel TBB (`oneapi::tbb`)
- **Execution policies:** `ExecutionSeq.hpp` (sequential) and `ExecutionTBB.hpp` (parallel), selected via `Execution.hpp`
- **Background processing:** `Print::process()` runs on worker thread pool
- **State machine:** Each `PrintStep`/`PrintObjectStep` has `StateWithTimeStamp` (Fresh → Started → Done/Canceled → Invalidated)
- **Thread safety:** `PrintBase.hpp` uses `std::mutex` for state queries; main thread has unguarded access when background stopped
- **Cancellation:** Callback-based, checked between steps

### Key Directories

| Directory | Purpose |
|-----------|---------|
| `src/libslic3r/` | Core slicing library |
| `src/libslic3r/GCode/` | G-code generation, cooling buffer, conflict checker |
| `src/libslic3r/Fill/` | Infill patterns (30+: honeycomb, gyroid, lightning, adaptive cubic, etc.) |
| `src/libslic3r/Support/` | Support material (grid, tree, snug) |
| `src/libslic3r/Arachne/` | Modern perimeter generator (edge-based, variable width) |
| `src/libslic3r/SLA/` | Resin printer pipeline (separate from FFF) |
| `src/libslic3r/Format/` | File format readers (STL, 3MF, AMF, OBJ, SL1) |
| `src/libslic3r/Geometry/` | Geometric algorithms (normals, intersections, arc welding) |
| `src/libslic3r/Algorithm/` | Convex hull, offset, medial axis, path planning |
| `src/libslic3r/BranchingTree/` | Tree support generation |
| `src/libslic3r/CSGMesh/` | Constructive solid geometry |
| `src/libslic3r/Optimize/` | Path optimization, travel planning |
| `src/libslic3r/Execution/` | TBB/sequential execution policies |
| `src/slic3r/GUI/` | wxWidgets GUI application |
| `src/slic3r/Config/` | Configuration/preset management |
| `src/slic3r/Utils/` | GUI utility functions |
| `src/CLI/` | Command-line interface |
| `src/libvgcode/` | G-code viewer component |
| `src/occt_wrapper/` | STEP file support (OpenCASCADE, optional) |
| `src/clipper/` | Polygon clipping library |
| `src/slic3r-arrange/` | Object arrangement/nesting |
| `src/libseqarrange/` | Sequential arrangement |
| `src/platform/` | Platform-specific code (msw, unix, osx) |
| `deps/` | External dependency build scripts (28 packages) |
| `bundled_deps/` | Vendored libraries (see below) |
| `resources/profiles/` | Printer profiles (~106) |
| `resources/localization/` | Translations (~27 languages) |
| `resources/icons/` | UI icons |
| `tests/` | Unit tests (Catch2) |
| `cmake/modules/` | Custom CMake find modules |
| `build-utils/` | Encoding check utilities (UTF-8 verification) |

### Bundled Dependencies (`bundled_deps/`)

admesh, agg, ankerl, avrdude, fast_float, glu-libtess, hidapi, hints, imgui, int128, libigl, libnest2d, localesutils, miniz, qoi, semver, stb_dxt, stb_image, tcbspan

## Conventions

- C++17 (`CMAKE_CXX_STANDARD 17`)
- `.clang-format` enforced: 100-char column limit, 4-space indent, no tabs
- Pointer alignment: right (`*ptr`)
- Brace style: opening brace on new line for classes/structs; same line for functions/namespaces
- Constructor initializers: one per line, break before comma
- Namespace indentation: none (compact)
- Include ordering: preserve organization; Qt/gtest/json at special priority
- Precompiled headers enabled by default
- `from __future__ import annotations` style not applicable (C++ project)
- Compiler flags: `-fsigned-char -Wall -Werror=return-type` (GCC/Clang), `/MP -bigobj` (MSVC)

## Dependencies

Built automatically via `PrusaSlicer_BUILD_DEPS=ON` (28 packages in `deps/+<name>/`):

| Library | Purpose |
|---------|---------|
| **wxWidgets 3.2+** | UI framework |
| **Boost 1.83+** | system, filesystem, thread, log, locale, regex, chrono, atomic, date_time, iostreams, nowide |
| **Eigen3 3.3.7+** | Linear algebra |
| **TBB** | Intel Threading Building Blocks (parallel execution) |
| **CGAL** | Computational geometry |
| **OpenVDB 5.0+** | Sparse volumetric data |
| **OCCT** | STEP file support (optional) |
| **NLopt 1.4+** | Nonlinear optimization |
| **libcurl** | HTTP client |
| **OpenSSL** | Cryptography |
| **libBGCode** | Binary G-code format |
| **Catch2 3.8+** | Unit testing |
| **Cereal** | Serialization |
| **GMP / MPFR** | Arbitrary precision math (for CGAL) |
| **Blosc** | Compression (for OpenVDB) |
| **OpenEXR** | HDR image format (for OpenVDB) |
| **GLEW** | OpenGL extension loading |
| **PNG / JPEG / EXPAT / ZLIB** | Image and data formats |
| **Qhull** | Convex hull computation |
| **NanoSVG** | SVG parsing |
| **z3** | SMT solver |
| **heatshrink** | Compression |
| **json** | JSON parsing |
| **OpenCSG** | Constructive solid geometry rendering |

System-provided on macOS: OpenGL, ZLIB, CURL.

## CI

GitHub Actions workflows in `.github/workflows/` (self-contained, no external action dependencies):

| Workflow | Trigger | Purpose |
|----------|---------|---------|
| `build-macos.yml` | push to master, release, dispatch | macOS build + DMG |
| `build-linux.yml` | push to master, release, dispatch | Linux build |
| `build-windows.yml` | push to master, release, dispatch | Windows build + installer |

## Localization

gettext-based workflow with custom CMake targets:
- `gettext_make_pot` — extract strings
- `gettext_merge_community_po_with_pot` — merge community translations
- `gettext_concat_wx_po_with_po` — merge wxWidgets translations
- `gettext_po_to_mo` — compile .po to .mo
- Languages: cs, de, es, fr, it, ja, pl (and more in resources)

## Calibration Fork Additions

This fork adds a **Calibration** menu with built-in calibration tools:

| Tool | Geometry | G-code |
|------|----------|--------|
| Temperature Tower | Multi-tier tower with overhangs, holes, cones, text labels | Per-layer `M104` |
| Flow Rate (YOLO) | 11 rounded-rect pads with label tabs, Archimedean Chords spiral | Per-pad flow via in-process post-processor (scales each pad's deposition E, keyed on object-label name; dialog forces OctoPrint labeling so boundaries emit on every gcode flavor) |
| Pressure Advance | 2 styles: **Chevron tower** (per-layer PA); **Line** (garethky/K-factor) — a *generated tool path*, not sliced | **Tower:** per-layer `M572 S` / `M900 K` / `SET_PRESSURE_ADVANCE`. **Line** (`generate_line_pattern`): the dialog *generates the whole toolpath* (anchor bars + one constant-Y slow→fast→slow pass per PA with the firmware PA command per line + boundary ticks + 7-segment PA number labels, relative E, E-rate from nozzle/filament/layer incl. extrusion multiplier), writes it to a temp file, and loads a tiny **placeholder** STL. `CalibrationPALinePostProcessor` (`::builtin::pa_line_pattern?body=…`) then **splices** the toolpath over the placeholder's body (between the first post-marker `; printing object` / `; stop printing object`), keeping header/start/end. The slicer preview shows only the placeholder, so `generate_line_pattern` arms `Plater::set_pa_line_export_reminder(true)` → a one-shot "export to view" `RichMessageDialog` (`hide_pa_line_export_reminder` AppConfig) fires from `Plater::priv::on_process_completed`. **PA command** is `select_pa_command(flavor, printer_notes)` in `CalibrationPAPostProcessor` — M572 for Prusa Buddy input-shaper printers (matched on `printer_notes` markers `MK4IS\|XLIS\|MK4S\|MK3.9S\|MK3.5\|MINIIS\|COREONE`, since the model name is unreliable: the MK3.9 carries `PRINTER_MODEL_MK4IS`), M900 K for older Marlin/generic, `SET_PRESSURE_ADVANCE` for Klipper. Tower geometry `make_pa_pattern` (chevron) in `CalibrationModels.cpp`. (The Ellis "Pattern" style — single-layer zigzag, per-object PA via the now-removed `CalibrationPAPostProcessor` runner — was **removed in 1.9.2**: redundant-if-stacked with the tower, weak on input-shaper printers.) |
| Retraction | Two cylindrical towers on base plate (seam=nearest/inward) | Per-Z-band slicer-side retraction rewrite via in-process post-processor |
| Max FlowRate | Serpentine E-shape specimen | Per-layer `M220 S` speed override |
| Extrusion Multiplier | 40×40×40mm vase-mode cube | Vase mode settings |
| Fan Speed | Two-column tower with shelves, wedges, cones, standalone cylinder | Per-layer `M106 S` |
| Dimensional Accuracy | XYZ cross gauge with through-holes and labels | None (measure after printing) |

**XY Skew Correction** — Native coordinate shear transform in `GCodeGenerator::point_to_gcode()`. Configured per-printer in Printer Settings → General → Skew Correction (Expert mode). Automatically disables arc fitting when active.

**Bed Mesh Visualization** — Renders a 3D heatmap of the printer's bed mesh directly on the build plate. Six entry points in the Calibration menu:

| Command | What it does |
|--------|--------------|
| **Fetch Bed Mesh** | Auto-detects a Prusa printer on USB serial, sends `M420 S1` + `M420 V1 T1`, parses the "Bed Topography Report for CSV:" block, displays the heatmap. <1s latency. Requires the mesh to already be stored on the printer. |
| **Probe Bed Mesh…** | `CalibrationBedMeshDialog` (nozzle temp / bed temp / all-tools) → home (`G28`) → optional bed heat (`M140`/`M190` in parallel with `M104`) → nozzle heat (`M109`) → probe (`G29` with `M73` percent progress) → cool → fetch. `wxProgressDialog` on a worker thread. Second Cancel click offers `M112` force-stop. Typical duration: 3–5 min (8–10 with bed heating). |
| **Show Bed Mesh Overlay** | Checkable menu item that toggles overlay visibility. Handler uses `wxCommandEvent::IsChecked()` so the check state always matches actual visibility. |
| **Save Bed Mesh As CSV…** | Writes the current mesh (or active-tool mesh on XL) to a tab-separated CSV compatible with `load_from_csv` and Buddy's `M420 V1 T1` format. Remembers last dir via `AppConfig["bed_mesh_last_dir"]`. |
| **Load Bed Mesh From CSV…** | Reads a saved mesh and displays it. |
| **Compare Bed Mesh With CSV…** | Loads a baseline, computes `current − baseline` via `BedMeshData::subtract`, switches overlay to delta view. Legend title becomes "Δ from <filename>", color reference auto-flips to Zero. |

Probe progress priority: **M73 `P<pct>` from the firmware** (percent-accurate on every printer model including XL) → fallback OK-count against per-printer expected value (`GRID_MAJOR_POINTS_X × Y` from Buddy firmware configs: XL=144, iX=81, Core-One/MK4/MK4S/MK3.5=49, MINI=16) → pulse-mode if unknown / overflow. Extracted as testable `Utils::G29ProgressTracker` class.

Heatmap details:
- Diverging multi-stop color ramp (dark blue → blue → cyan → white → yellow → orange → red → dark red) so mid-magnitude points stay visible.
- Reference combo in the legend (Mean / Zero): Mean centers white on the mesh average (warp view), Zero centers on the nominal plane (absolute compensation view).
- Z exaggeration slider and absolute-Z labels on the color scale bar.
- **Warp report** (collapsible legend section): `fit_plane()` least-squares tilt in arc-minutes, RMS after plane removal, worst-point deviation, editable threshold-driven quality grade (Excellent/Good/Marginal/Bad).
- **Contour lines** (default on): iso-Z lines drawn in the fragment shader at configurable mm intervals (default 0.05). ~1.2 px opaque core + 1.2 px AA fade each side; density guard fades contours on steep slopes to avoid moire.
- **Cell values** (default off): per-probe-point Z labels rendered via ImGui background drawlist, projected from world XY through the active `Camera`.
- **Mesh tessellation**: `init_mesh_overlay` subdivides each source quad 4×4 with `BedMeshData::sample_bilinear` so contours read as smooth curves. Source probe points preserved exactly at integer fine-grid indices.
- **Per-tool picker (XL)**: when `probe_all_tools` is set and `M115` reports `EXTRUDER_COUNT > 1`, `probe_bed_mesh_from_printer` loops `T<n>`+`M109`+`G29`+`M420 V1 T1` for each extruder. `Bed3D::set_mesh_data_per_tool` stores the vector; legend gains a T0/T1/… button row.
- Error translation: serial exceptions containing "Permission denied"/"Resource busy"/"No such file" surface user-actionable messages naming likely culprit apps (PrusaConnect, OctoPrint, pronterface).
- Cross-platform USB port enumeration reuses `scan_serial_ports_extended()` (matches "Original Prusa" friendly name / VID 0x2C99).

Dev/env hooks (all optional; fall through to the next source if unset):
- `PRUSASLICER_BED_MESH_CSV=/path/to/csv` — load a saved `M420` CSV instead of querying the printer. Same tab-separated format the firmware emits.
- `PRUSASLICER_BED_MESH_EXTENT="xmin,ymin,xmax,ymax"` — override the XY extent the grid spans. Default: bed bounds inset by 10 mm. Core One's firmware reports 2..248 / 3..217.
- `PRUSASLICER_BED_MESH_PORT=/dev/cu.usbmodem101` — force a specific serial device instead of auto-detecting.
- `SLIC3R_LOGLEVEL=5` — route `[BedMesh probe]` Boost.Log debug traces to stderr.

Key files:
- `src/libslic3r/CalibrationModels.cpp/hpp` — geometry generators
- `src/slic3r/GUI/Calibration*Dialog.cpp/hpp` — dialog UIs (including `CalibrationBedMeshDialog`)
- `src/slic3r/GUI/MainFrame.cpp` — menu wiring
- `src/libslic3r/GCode.hpp` — skew transform in `point_to_gcode()`
- `src/slic3r/GUI/BedMeshData.cpp/hpp` — mesh data model, CSV/M420 parsing, color map, `subtract`, `fit_plane`, `quality_grade`, `sample_bilinear`
- `src/slic3r/GUI/3DBed.cpp/hpp` — overlay geometry (bilinear-tessellated), compare-mode state, per-tool vector, warp-report legend, cell-label projection
- `src/slic3r/Utils/BedMeshSerial.cpp/hpp` — fetch/probe over USB serial, `BedMeshProbeOptions`, `G29ProgressTracker`, `parse_m73_progress`, `expected_probe_count_from_m115_lines`, `extruder_count_from_m115_lines`
- `src/slic3r/GUI/Plater.cpp` (`fetch_bed_mesh`, `probe_bed_mesh`, `save/load/compare_bed_mesh_csv`) — orchestrates source selection and progress UI
- `resources/shaders/140/bed_mesh_overlay.{vs,fs}` — per-vertex-color shader with iso-Z contour AA
- `tests/slic3rutils/bedmesh_tests.cpp` — 56 test cases covering parsing, stats, plane fit, subtract, CSV round-trip, M73 progress, bilinear sampling, quality grade
- `doc/Calibration_Guide.md` — user documentation

**FilamentDB Sync** — Pushes a filament preset to a remote Filament DB REST service so calibration data (PA, EM, retraction, etc.) and metadata are shared across machines. Configured via `Preferences → filamentdb_url`. Two trigger paths:

| Trigger | Behavior |
|---|---|
| **Save Preset** (auto) | `TabFilament::save_current_preset` calls `sync_to_filamentdb(false)` after the local save succeeds. Result fires as a `RegularNotificationLevel`/`WarningNotificationLevel` notification on the Plater canvas (no modal — silent if user is on Plater, fully invisible if user is mid-edit on Filaments tab). |
| **Toolbar button** (manual) | `m_btn_sync_filamentdb` (icon: `filament_db_sync`, only on filament tabs) triggers `sync_to_filamentdb(true)`. Same notification fires AND a `wxMessageBox` shows the result modally — necessary because notifications render on the GL canvas and would be invisible while the user is on the Filaments tab. |

**Upsert flow** (`sync_filament_to_filamentdb_detailed` in `src/slic3r/Utils/FilamentDB.cpp`):
1. `POST /api/filaments/{name}` with `{"name": "...", "config": {INI key/value pairs}}` and query string `?nozzle_diameter=...&high_flow=0|1`. The server (filament-db Next.js app at `/api/filaments/[id]/route.ts`) accepts the path param as a URL-decoded name OR an ObjectId. On success → returns 200 and merges config keys into structured fields + per-nozzle calibration entry.
2. On HTTP 404/405/501 → fall back to **collection create**: `POST /api/filaments` with `{name, type, vendor}` extracted from `filament_type` / `filament_vendor` config keys (defaults `"PLA"` / `"User"` if missing). Returns 201 with the new `_id`.
3. **Retry the per-name update** so the calibration body the create endpoint silently ignores gets populated. `created_new=true` is reported back to the UI.
4. Other failures (transport error, 5xx) bubble up as-is — no retry.

`FilamentDBSyncResult` carries `{success, http_status, method, error_message, existed_before, created_new}` so the caller can render "Created new …" vs "Updated …" accurately.

Important constraints from the server (filament-db):
- The `/api/filaments/{id}` POST handler is update-only — it returns 404 if the filament doesn't exist. It is NOT an upsert.
- `/api/filaments` (collection POST) requires `type` and `vendor` minimum; other fields are accepted but optional.
- The `{id}` GET/PUT routes treat the path param as an ObjectId only (returns 400 "Cast to ObjectId failed" if you pass a name there).
- The two-call dance is therefore necessary; there is no single-call upsert.

Key files:
- `src/slic3r/Utils/FilamentDB.cpp/hpp` — `sync_filament_to_filamentdb_detailed` (upsert chain), `sync_filament_to_filamentdb` (bool wrapper for backward compat), `parse_filamentdb_bundle`, `fetch_filament_calibration`, `check_filament_spool`
- `src/slic3r/GUI/Tab.cpp` (`TabFilament::sync_to_filamentdb`) — orchestrates UI feedback (notification + manual-trigger modal)
- `resources/icons/filament_db_sync.svg` — toolbar icon (16×16 grey arrow + orange ground bar)
- `scripts/filamentdb_probe.sh` — curl-based decision-tree script for verifying a FilamentDB server's upsert behavior (reachability, pre-check 404, POST /name, PUT /name, POST /collection). Useful when bringing up a new server or diagnosing sync failures.

## Version

Current version defined in `version.inc`: **2.9.4** (base), **Filament-Edition-1.3.0** (fork)

```cmake
set(SLIC3R_APP_NAME "PrusaSlicer")
set(SLIC3R_VERSION "2.9.4")
set(SLIC3R_BUILD_ID "PrusaSlicer-${SLIC3R_VERSION}-Filament-Edition-1.3.0")
```
