# Windows GMP / MPFR dependency builds

This directory and `.github/workflows/build_windows_math.yml` build GMP 6.2.1
and MPFR 4.2.1 DLLs, MSVC import libraries and headers on native Windows ARM64
and x64 from pinned upstream sources. No prebuilt GMP or MPFR binary is used.
The versions match the existing non-MSVC recipes in `deps/+GMP` and
`deps/+MPFR`. Nothing here changes the committed Windows binaries or how the
slicer and its dependencies consume them.

## Running

The workflow only runs on `workflow_dispatch`, with read-only repository
permissions. It does not publish releases, cache binaries or build the slicer.
Once the workflow is on the default branch, run **Windows GMP and MPFR** from
the Actions tab, or:

```powershell
gh workflow run build_windows_math.yml --repo OWNER/PrusaSlicer --ref BRANCH
```

Each architecture job uploads its diagnostic logs (kept 14 days) and, if every
check passes, the package (kept 30 days). Use a package only when both jobs
passed.

To build locally, use PowerShell 7 running natively on the target architecture,
with Visual Studio C++ tools for that architecture, the Windows SDK, Git, and
the `curl` and `tar` that ship with Windows:

```powershell
.\deps\windows-math\build.ps1 -Architecture arm64 -WorkRoot C:\math-build-arm64
```

`-Architecture` is `arm64` or `x64`. `-WorkRoot` must be a new directory
without spaces; all downloads and tools stay inside it. The recipe and workflow
must be committed, because the package records the commit.

## Build and ABI choices

- `inputs.json` pins every source and tool archive by URL and SHA256.
- The compiler is LLVM-MinGW 20260922 (Clang, MinGW-w64, UCRT), native to the
  target, driving upstream's Autoconf/Libtool MinGW build.
- Assembly is disabled (portable C) on both architectures. The C++ `gmpxx`
  library is not built; the C API is the integration boundary.
- On x64, `-mlong-double-64` matches MSVC's 64-bit `long double`, which
  `mpfr_set_ld`/`mpfr_get_ld` depend on. ARM64 already uses that layout.
- `__USE_MINGW_ANSI_STDIO=0` uses UCRT formatted I/O instead of MinGW's printf
  wrappers.
- The pinned MSYS2 base system, make, m4 and diffutils are build tools only and
  are not shipped. On ARM64 they run under x64 emulation. The compilers, DLLs
  and smoke executables are checked for the target PE machine type, and the
  smoke test uses `IsWow64Process2` to reject emulated execution.
- `--build` and `--host` are both the target, so `make check` runs the upstream
  GMP and MPFR test suites natively; any failure, and any remaining compiler or
  build warning, fails the build. Expected skips are MPFR's decimal64/decimal128
  tests (Clang has no `_Decimal64`/`_Decimal128`) and, on ARM64,
  `tset_float128`.
- MSVC `lib.exe` generates the import libraries from the DLL exports, marking
  data exports as `DATA`. The DLLs may only import from KERNEL32, ADVAPI32, the
  UCRT and, for MPFR, `libgmp-10.dll`.
- `smoke.cpp` checks arithmetic, version data, allocation, MPFR thread-local
  precision and type layouts. It is built with MSVC (`/W4 /WX`) and with Clang;
  both must pass and print the same layout description. It is then rebuilt
  with MSVC against the unpacked ZIP and run with only the package and Windows
  directories on `PATH`.

The workflow uses the `windows-11-vs2026-arm` and `windows-2025` runners.
`provenance.json` records the runner image, Visual Studio, MSVC and Clang
versions, inputs, patches and recipe commit. Hosted images are updated over
time, so a result only applies to the recorded image. Bit-for-bit
reproducibility is not established.

## Patches

Each patch is checked against the pinned sources with `git apply --check`, and
its SHA256 is recorded in `provenance.json`.

- `libtool-response-files.patch` adapts
  [MSYS2's libtool fix](https://github.com/msys2/MINGW-packages/blob/95b093e888/mingw-w64-libtool/0012-Prefer-response-files-over-linker-scripts-for-mingw-.patch)
  to the bundled `configure` and `ltmain.sh`, so LLD gets `@file` response
  files instead of GNU linker scripts it cannot read. Build machinery only.
- `windows-warning-cleanup.patch` fixes Clang, MSVC and LLP64 warnings instead
  of suppressing them, e.g. explicit unsigned arithmetic in `gmp.h` for MSVC
  C4244/C4146 and local inline definitions for Clang DLL consumers.
- `gmp-replacement-vsnprintf-hex.patch` fixes `%a`/`%A` in GMP's replacement
  `vsnprintf`, which GMP's configure selects with the UCRT, and adds tests.

## Package layout

Artifact names include the GMP/MPFR versions, architecture, LLVM version, MSVC
toolset, `inputs.json` hash and recipe commit. Each artifact contains a ZIP and
its SHA256. The ZIP contains:

```text
bin/              libgmp-10.dll, libmpfr-6.dll
lib/              libgmp-10.lib, libmpfr-6.lib and their .def files
include/          generated gmp.h, mpfr.h, mpf2mpfr.h
source/           GMP and MPFR source archives
source/recipe/    this directory and the workflow
licenses/         GMP, MPFR, LLVM-MinGW and recipe licenses
provenance.json   inputs, toolchain, runner and smoke test output
SHA256SUMS        hashes of all other files
```

## Adoption

- Use the DLLs, import libraries and headers from one package together.
- Do not combine them with the GMP 5.0.1 / MPFR 3.0.0 headers and libraries
  committed for Windows. MPFR 4.2.1 has a different ABI (`libmpfr-6.dll`); do
  not rename it to `libmpfr-4.dll`.
- Both DLLs use the UCRT, and the smoke test links with `/MD`. Free
  GMP-allocated memory with the function from `mp_get_memory_functions` and
  MPFR strings with `mpfr_free_str`.
- Using these packages in the slicer is a separate change. `deps/+GMP/GMP.cmake`
  and `deps/+MPFR/MPFR.cmake` copy the `win${DEPS_BITS}` files, and
  `cmake/modules/Utils.cmake` copies the runtime DLLs from the source tree.
  Both need updating together, with an explicit ARM64/x64 choice, because
  pointer size cannot tell them apart.

## Licensing

GMP 6.2.1 is LGPL-3.0-or-later or GPL-2.0-or-later; MPFR 4.2.1 is
LGPL-3.0-or-later. The scripts in this directory are AGPL-3.0-or-later.
Packages include the GMP, MPFR and LLVM-MinGW license notices and the GMP and
MPFR sources. No build tools or Microsoft runtime files are included; the UCRT
ships with Windows. Redistributing the DLLs requires following those licenses,
including notices, corresponding source and relinking rights.
