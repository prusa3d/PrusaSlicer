# Orca profile import

This branch imports Orca JSON profile data into native PrusaSlicer presets.
Normal configuration, building, importing and regression tests do not require
an Orca source checkout or an installed Orca executable.

The existing `orca_profiles` target populates `resources/profiles` from the
upstream OrcaSlicer main branch only when that path does not exist. It leaves
existing profile trees untouched. The converter uses the checked-in
`OrcaProfileDefaults.inc`; it does not inspect an external source tree at runtime.
Explicit inherited profile values override captured defaults, with nil values
and native fallback behavior kept distinct.

## Transferred features

The non-build follow-up includes imported fixed/matrix purge volume handling,
wipe speed and retraction distribution, tower speed/spacing/ramming corrections,
separate filament-operation timing and the wipe-speed settings-control fix.
Loader fixes preserve explicit normal support style and scalar options encoded
as singleton arrays. Conversion cache format 14 refreshes older results.

The transfer contains source code, generated-fixture tests and portable audit
tools. It excludes local build wrappers, compiler/linker settings, installation
records and machine-specific diagnostic documents. Existing build configuration
on this branch is preserved.

## Tests

The standalone importer suite needs CMake, a C++20 compiler and the
`nlohmann/json.hpp` dependency. It generates its profile fixtures in a temporary
directory; it does not need downloaded profiles, a source checkout or a 3MF.

```text
cmake -S tests/orca_profile_import -B build/orca-import-tests -DJSON_INCLUDE_DIR=JSON_INCLUDE_DIRECTORY
cmake --build build/orca-import-tests --config Release
ctest --test-dir build/orca-import-tests -C Release --output-on-failure
python -B -m unittest discover -s tests/orca_profile_import -p test_audit_orca_defaults.py
```

Full project builds also register `orca_profile_native`, which exercises the
native loader, settings evaluator, roundtrip serialization and G-code templates.
The optional `ORCA_PRUSA_COMPARISON_ROOT` setting takes a **profile data tree**,
not a source checkout. It defaults to empty and enables the real Prusa comparison
only when supplied. The native executable can also evaluate a caller-supplied
profile corpus. Regression geometries added by this follow-up are generated
from code; no personal project/model assets are included.

The transplanted standalone importer and three Python audit regressions passed
in a clean target worktree. The transferred implementation/test files match the
previously tested source branch. A full application build was not repeated for
the target branch.

## Optional audit maintenance

These tools are explicitly invoked by maintainers; none runs during normal
configuration, building, startup, importing or tests:

- `audit_orca_defaults.py` takes a native snapshot and a caller-supplied profile
  tree. It reads the checked-in default snapshot and mapping table.
- `compare_tower_paths.py` takes two caller-supplied G-code files. It records
  hashes and linear extrusion lengths; it requires neither source code nor 3MFs.
- `project_export_smoke.ps1` requires explicit application and project arguments.
  No project paths or models are supplied by this branch's Orca tests.
- `capture_defaults.py` deliberately regenerates the checked-in default snapshot.
  For that maintenance operation only, the caller supplies an isolated Orca
  settings export and the matching source checkout to identify option ownership.
  It has no implicit source location, performs no source download and is not a
  dependency of importing or testing.

Source-only settings without a native representation remain diagnostic entries.
Existing native projects retain embedded settings; importing profiles does not
silently rewrite them or import the contents of an Orca project archive.

## Existing upstream 3MF test fixtures

No 3MF was added or modified by this transfer. The branch's seven tracked 3MFs
are unchanged upstream PrusaSlicer regression fixtures, with these provenance
commits. No user-supplied diagnostic projects are included.

| Fixture under the repository root | Upstream provenance |
|---|---|
| `src/slic3r-shared/test/data/fdm_roundtrip1.3mf` | Added in `a447707f5f`, updated in `52c1006bb1` |
| `src/slic3r-shared/test/data/fdm_roundtrip2.3mf` | Added in `a447707f5f`, updated in `52c1006bb1` |
| `src/slic3r-shared/test/data/sla_roundtrip1.3mf` | `a447707f5f` |
| `src/slic3r-shared/test/data/sla_roundtrip2.3mf` | `a447707f5f` |
| `src/slic3r-shared/test/data/test_3mf/production_ext.3mf` | `12016d06c5` |
| `src/slic3r-shared/test/data/wipe_tower.3mf` | `eb2f8d4da3` |
| `tests/data/seam_test_object.3mf` | `e60b8b1193` |
