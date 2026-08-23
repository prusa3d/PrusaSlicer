///|/ Copyright (c) 2026
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "slic3r/GUI/BedMeshData.hpp"
#include "slic3r/Utils/BedMeshSerial.hpp"

#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

using namespace Slic3r;
using namespace Slic3r::GUI;
using Catch::Matchers::WithinAbs;

// ----------------------------------------------------------------------------
// BedMeshData — basic accessors / statistics / validity
// ----------------------------------------------------------------------------

TEST_CASE("BedMeshData: default-constructed mesh is invalid", "[bedmesh]") {
    BedMeshData m;
    REQUIRE(m.status == BedMeshData::Status::Empty);
    REQUIRE(!m.is_valid());
    REQUIRE(m.rows == 0);
    REQUIRE(m.cols == 0);
}

TEST_CASE("BedMeshData::is_valid requires Loaded status and >=2x2 grid", "[bedmesh]") {
    BedMeshData m;
    m.rows = 3; m.cols = 3;
    m.z_values = std::vector<float>(9, 0.0f);

    // status still Empty → invalid
    REQUIRE(!m.is_valid());

    m.status = BedMeshData::Status::Loaded;
    REQUIRE(m.is_valid());

    // 1x1 rejected
    m.rows = 1; m.cols = 1; m.z_values = { 0.f };
    REQUIRE(!m.is_valid());

    // size mismatch rejected
    m.rows = 3; m.cols = 3; m.z_values.resize(5);
    REQUIRE(!m.is_valid());
}

TEST_CASE("BedMeshData::get returns row-major elements", "[bedmesh]") {
    BedMeshData m;
    m.rows = 2; m.cols = 3;
    m.z_values = { 0.0f, 1.0f, 2.0f, 10.0f, 11.0f, 12.0f };
    REQUIRE(m.get(0, 0) == 0.0f);
    REQUIRE(m.get(0, 2) == 2.0f);
    REQUIRE(m.get(1, 0) == 10.0f);
    REQUIRE(m.get(1, 2) == 12.0f);
}

TEST_CASE("BedMeshData::recompute_range finds min and max", "[bedmesh]") {
    BedMeshData m;
    m.z_values = { -0.5f, 0.0f, 0.25f, -0.1f, 0.4f };
    m.recompute_range();
    REQUIRE_THAT(m.z_min, WithinAbs(-0.5f, 1e-6f));
    REQUIRE_THAT(m.z_max, WithinAbs( 0.4f, 1e-6f));

    // Empty mesh → zeros, no crash
    BedMeshData empty;
    empty.recompute_range();
    REQUIRE(empty.z_min == 0.0f);
    REQUIRE(empty.z_max == 0.0f);
}

TEST_CASE("BedMeshData::mean computes arithmetic mean", "[bedmesh]") {
    BedMeshData m;
    m.z_values = { 1.0f, 2.0f, 3.0f, 4.0f };
    REQUIRE_THAT(m.mean(), WithinAbs(2.5f, 1e-6f));

    BedMeshData empty;
    REQUIRE(empty.mean() == 0.0f);
}

TEST_CASE("BedMeshData::std_dev computes sample standard deviation", "[bedmesh]") {
    BedMeshData m;
    // Values 2,4,4,4,5,5,7,9 have mean 5, population stddev 2, sample stddev ≈ 2.138
    m.z_values = { 2.f, 4.f, 4.f, 4.f, 5.f, 5.f, 7.f, 9.f };
    REQUIRE_THAT(m.std_dev(), WithinAbs(2.1380899f, 1e-4f));

    // Constant → zero
    BedMeshData flat;
    flat.z_values = { 0.3f, 0.3f, 0.3f, 0.3f };
    REQUIRE_THAT(flat.std_dev(), WithinAbs(0.0f, 1e-6f));

    // Fewer than 2 samples → zero, no divide-by-zero
    BedMeshData one;
    one.z_values = { 1.0f };
    REQUIRE(one.std_dev() == 0.0f);

    BedMeshData empty;
    REQUIRE(empty.std_dev() == 0.0f);
}

// ----------------------------------------------------------------------------
// BedMeshData::create_mock
// ----------------------------------------------------------------------------

TEST_CASE("BedMeshData::create_mock produces a 7x7 grid centered around zero",
          "[bedmesh][mock]") {
    const Vec2d bed_min(0.0, 0.0);
    const Vec2d bed_max(250.0, 220.0);
    BedMeshData m = BedMeshData::create_mock(bed_min, bed_max);

    REQUIRE(m.is_valid());
    REQUIRE(m.rows == 7);
    REQUIRE(m.cols == 7);
    REQUIRE(m.z_values.size() == 49);

    // Probe grid inset by 10mm margin as documented.
    REQUIRE_THAT(m.origin.x(), WithinAbs(10.0, 1e-9));
    REQUIRE_THAT(m.origin.y(), WithinAbs(10.0, 1e-9));
    // spacing = (230 / 6, 200 / 6)
    REQUIRE_THAT(m.spacing.x(), WithinAbs(230.0 / 6.0, 1e-9));
    REQUIRE_THAT(m.spacing.y(), WithinAbs(200.0 / 6.0, 1e-9));

    // Values should be within realistic bed-deviation range and cross zero.
    REQUIRE(m.z_min < 0.0f);
    REQUIRE(m.z_max > 0.0f);
    REQUIRE(m.z_max - m.z_min < 0.5f);
    for (float z : m.z_values) {
        REQUIRE(std::isfinite(z));
        REQUIRE(std::abs(z) < 0.3f);
    }
}

// ----------------------------------------------------------------------------
// BedMeshData::parse_m420_output
// ----------------------------------------------------------------------------

TEST_CASE("parse_m420_output parses a Buddy firmware CSV block",
          "[bedmesh][parse]") {
    std::vector<std::string> lines = {
        "echo:Bed Leveling ON",
        "Bed Topography Report for CSV:",
        "0.10\t0.15\t0.20",
        "0.05\t0.00\t-0.05",
        "-0.10\t-0.05\t0.00",
        "ok"
    };
    // Nominal 100x100 probe extent, first row corresponds to Y_max so the
    // loader should reverse: row 0 of output = last raw row.
    BedMeshData m = BedMeshData::parse_m420_output(lines,
        Vec2d(0.0, 0.0), Vec2d(100.0, 100.0));

    REQUIRE(m.is_valid());
    REQUIRE(m.rows == 3);
    REQUIRE(m.cols == 3);
    // Row 0 == the *last* input row (Y_max convention is reversed).
    REQUIRE_THAT(m.get(0, 0), WithinAbs(-0.10f, 1e-6f));
    REQUIRE_THAT(m.get(0, 2), WithinAbs( 0.00f, 1e-6f));
    // Row 2 == the *first* input row.
    REQUIRE_THAT(m.get(2, 0), WithinAbs( 0.10f, 1e-6f));
    REQUIRE_THAT(m.get(2, 2), WithinAbs( 0.20f, 1e-6f));

    REQUIRE_THAT(m.z_min, WithinAbs(-0.10f, 1e-6f));
    REQUIRE_THAT(m.z_max, WithinAbs( 0.20f, 1e-6f));
    REQUIRE_THAT(m.spacing.x(), WithinAbs(50.0, 1e-9));
    REQUIRE_THAT(m.spacing.y(), WithinAbs(50.0, 1e-9));
}

TEST_CASE("parse_m420_output errors when the CSV header is missing",
          "[bedmesh][parse]") {
    std::vector<std::string> lines = {
        "echo:Bed Leveling OFF",
        "0.1 0.2",
        "0.3 0.4",
        "ok"
    };
    BedMeshData m = BedMeshData::parse_m420_output(lines,
        Vec2d(0, 0), Vec2d(10, 10));
    REQUIRE(!m.is_valid());
    REQUIRE(m.status == BedMeshData::Status::Error);
    REQUIRE(m.error_message.find("Bed Topography Report") != std::string::npos);
}

TEST_CASE("parse_m420_output rejects a mesh containing nan/inf tokens",
          "[bedmesh][parse]") {
    // "nan" / "inf" text in a row should never produce a valid mesh.
    // The exact error depends on the stdlib's float-parsing: glibc parses
    // "nan" → NaN (triggers the NaN/Inf guard in finalize_mesh), whereas
    // Apple's libc++ fails the parse (so the line is treated as end-of-CSV
    // and we hit the "fewer than 2 rows" guard instead). Either way, the
    // mesh must be flagged invalid so a bad probe never silently loads.
    std::vector<std::string> lines = {
        "Bed Topography Report for CSV:",
        "0.1 0.2",
        "nan 0.4",
        "ok"
    };
    BedMeshData m = BedMeshData::parse_m420_output(lines,
        Vec2d(0, 0), Vec2d(10, 10));
    REQUIRE(!m.is_valid());
    REQUIRE(m.status == BedMeshData::Status::Error);
    REQUIRE(!m.error_message.empty());
}

TEST_CASE("parse_m420_output rejects inconsistent row widths",
          "[bedmesh][parse]") {
    std::vector<std::string> lines = {
        "Bed Topography Report for CSV:",
        "0.1 0.2 0.3",
        "0.4 0.5",
        "ok"
    };
    BedMeshData m = BedMeshData::parse_m420_output(lines,
        Vec2d(0, 0), Vec2d(10, 10));
    REQUIRE(!m.is_valid());
    REQUIRE(m.status == BedMeshData::Status::Error);
    REQUIRE(m.error_message.find("row") != std::string::npos);
}

TEST_CASE("parse_m420_output rejects fewer than 2 rows",
          "[bedmesh][parse]") {
    std::vector<std::string> lines = {
        "Bed Topography Report for CSV:",
        "0.1 0.2 0.3",
        "ok"
    };
    BedMeshData m = BedMeshData::parse_m420_output(lines,
        Vec2d(0, 0), Vec2d(10, 10));
    REQUIRE(!m.is_valid());
    REQUIRE(m.status == BedMeshData::Status::Error);
}

TEST_CASE("parse_m420_output tolerates CR line endings and trailing junk",
          "[bedmesh][parse]") {
    std::vector<std::string> lines = {
        "Bed Topography Report for CSV:\r",
        "0.10\t0.20\r",
        "0.30\t0.40\r",
        "ok\r"
    };
    BedMeshData m = BedMeshData::parse_m420_output(lines,
        Vec2d(0, 0), Vec2d(10, 10));
    REQUIRE(m.is_valid());
    REQUIRE(m.rows == 2);
    REQUIRE(m.cols == 2);
}

// ----------------------------------------------------------------------------
// BedMeshData::load_from_csv
// ----------------------------------------------------------------------------

TEST_CASE("load_from_csv returns an error for a missing file",
          "[bedmesh][csv]") {
    BedMeshData m = BedMeshData::load_from_csv(
        "/tmp/__bedmesh_does_not_exist__.csv",
        Vec2d(0, 0), Vec2d(10, 10));
    REQUIRE(!m.is_valid());
    REQUIRE(m.status == BedMeshData::Status::Error);
    REQUIRE(m.error_message.find("Cannot open") != std::string::npos);
}

TEST_CASE("save_to_csv + load_from_csv round-trips a mesh",
          "[bedmesh][csv]") {
    namespace bfs = boost::filesystem;
    BedMeshData m;
    m.rows = 2; m.cols = 3;
    m.z_values = { 0.10f, 0.20f, 0.30f, 0.40f, 0.50f, 0.60f };
    m.origin   = Vec2d(0, 0);
    m.spacing  = Vec2d(50.0, 100.0);
    m.recompute_range();
    m.status = BedMeshData::Status::Loaded;

    bfs::path tmp = bfs::unique_path("%%%%-%%%%-bedmesh_rt.csv");
    tmp = bfs::temp_directory_path() / tmp;

    const std::string err = m.save_to_csv(tmp.string());
    REQUIRE(err.empty());

    BedMeshData r = BedMeshData::load_from_csv(tmp.string(),
                                               Vec2d(0, 0), Vec2d(100, 100));
    bfs::remove(tmp);

    REQUIRE(r.is_valid());
    REQUIRE(r.rows == 2);
    REQUIRE(r.cols == 3);
    for (std::size_t i = 0; i < 6; ++i)
        REQUIRE_THAT(r.z_values[i], WithinAbs(m.z_values[i], 1e-4f));
}

TEST_CASE("save_to_csv refuses an invalid mesh",
          "[bedmesh][csv]") {
    BedMeshData empty;
    const std::string err = empty.save_to_csv("/tmp/should_never_be_written.csv");
    REQUIRE(!err.empty());
    REQUIRE(err.find("invalid") != std::string::npos);
}

// ----------------------------------------------------------------------------
// BedMeshData::subtract
// ----------------------------------------------------------------------------

TEST_CASE("subtract returns element-wise difference for matching meshes",
          "[bedmesh][compare]") {
    BedMeshData a;
    a.rows = 2; a.cols = 2;
    a.z_values = { 0.10f, 0.20f, 0.30f, 0.40f };
    a.origin   = Vec2d(0, 0);
    a.spacing  = Vec2d(100, 100);
    a.recompute_range();
    a.status = BedMeshData::Status::Loaded;

    BedMeshData b = a;
    b.z_values = { 0.05f, 0.10f, 0.20f, 0.30f };
    b.recompute_range();

    BedMeshData d = a.subtract(b);
    REQUIRE(d.is_valid());
    REQUIRE(d.rows == 2);
    REQUIRE(d.cols == 2);
    REQUIRE_THAT(d.z_values[0], WithinAbs(0.05f, 1e-6f));
    REQUIRE_THAT(d.z_values[1], WithinAbs(0.10f, 1e-6f));
    REQUIRE_THAT(d.z_values[2], WithinAbs(0.10f, 1e-6f));
    REQUIRE_THAT(d.z_values[3], WithinAbs(0.10f, 1e-6f));
    // XY metadata is copied from lhs.
    REQUIRE_THAT(d.origin.x(), WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(d.spacing.x(), WithinAbs(100.0, 1e-9));
}

// ----------------------------------------------------------------------------
// BedMeshData::fit_plane / quality_grade
// ----------------------------------------------------------------------------

TEST_CASE("fit_plane reports zero tilt on a perfectly flat mesh",
          "[bedmesh][plane]") {
    BedMeshData m;
    m.rows = 5; m.cols = 5;
    m.z_values.assign(25, 0.0f);
    m.origin  = Vec2d(0, 0);
    m.spacing = Vec2d(50, 50);
    m.recompute_range();
    m.status = BedMeshData::Status::Loaded;

    const auto pf = m.fit_plane();
    REQUIRE_THAT(pf.tilt_x_arcmin, WithinAbs(0.f, 1e-3f));
    REQUIRE_THAT(pf.tilt_y_arcmin, WithinAbs(0.f, 1e-3f));
    REQUIRE_THAT(pf.rms_after, WithinAbs(0.f, 1e-5f));
}

TEST_CASE("fit_plane recovers a synthetic 1 mm/200 mm tilt",
          "[bedmesh][plane]") {
    // Synthesize Z = 0.005 * X (1 mm rise over 200 mm X). Expected tilt_x:
    // atan(0.005) ≈ 17.19 arcmin.
    BedMeshData m;
    m.rows = 5; m.cols = 5;
    m.z_values.resize(25);
    m.origin  = Vec2d(0, 0);
    m.spacing = Vec2d(50, 50);
    for (std::size_t r = 0; r < 5; ++r)
        for (std::size_t c = 0; c < 5; ++c)
            m.z_values[r * 5 + c] = float(0.005 * double(c) * 50.0);
    m.recompute_range();
    m.status = BedMeshData::Status::Loaded;

    const auto pf = m.fit_plane();
    REQUIRE_THAT(pf.tilt_x_arcmin, WithinAbs(17.19f, 0.1f));
    REQUIRE_THAT(pf.tilt_y_arcmin, WithinAbs(0.f,    0.1f));
    REQUIRE_THAT(pf.rms_after,     WithinAbs(0.f,    1e-4f));
}

TEST_CASE("fit_plane recovers Y-axis tilt independently of X",
          "[bedmesh][plane]") {
    // Z = 0.003 * Y (0.6 mm rise over 200 mm Y). atan(0.003) ≈ 10.31 arcmin.
    BedMeshData m;
    m.rows = 5; m.cols = 5;
    m.z_values.resize(25);
    m.origin  = Vec2d(0, 0);
    m.spacing = Vec2d(50, 50);
    for (std::size_t r = 0; r < 5; ++r)
        for (std::size_t c = 0; c < 5; ++c)
            m.z_values[r * 5 + c] = float(0.003 * double(r) * 50.0);
    m.recompute_range();
    m.status = BedMeshData::Status::Loaded;

    const auto pf = m.fit_plane();
    REQUIRE_THAT(pf.tilt_x_arcmin, WithinAbs(0.f,    0.1f));
    REQUIRE_THAT(pf.tilt_y_arcmin, WithinAbs(10.31f, 0.1f));
    REQUIRE_THAT(pf.rms_after,     WithinAbs(0.f,    1e-4f));
}

TEST_CASE("fit_plane offset_z is the plane's Z at mesh center",
          "[bedmesh][plane]") {
    // Constant mesh at Z = 0.4 mm. Plane should sit at 0.4 everywhere, so
    // offset_z (plane Z at mesh center) must be 0.4.
    BedMeshData m;
    m.rows = 3; m.cols = 3;
    m.z_values.assign(9, 0.4f);
    m.origin  = Vec2d(0, 0);
    m.spacing = Vec2d(100, 100);
    m.recompute_range();
    m.status = BedMeshData::Status::Loaded;

    const auto pf = m.fit_plane();
    REQUIRE_THAT(pf.offset_z, WithinAbs(0.4f, 1e-5f));
}

// ----------------------------------------------------------------------------
// BedMeshData::sample_bilinear
// ----------------------------------------------------------------------------

static BedMeshData make_bilinear_test_mesh()
{
    // A 3×3 grid with asymmetric values so every neighbor combination
    // produces a distinct midpoint, and the center is unambiguously the
    // four-corner average. Lay out as:
    //   row 0: 0.00  0.20  0.40
    //   row 1: 0.10  0.30  0.50
    //   row 2: 0.20  0.40  0.60
    BedMeshData m;
    m.rows = 3; m.cols = 3;
    m.z_values = {
        0.00f, 0.20f, 0.40f,
        0.10f, 0.30f, 0.50f,
        0.20f, 0.40f, 0.60f,
    };
    m.origin  = Vec2d(0, 0);
    m.spacing = Vec2d(50, 50);
    m.recompute_range();
    m.status = BedMeshData::Status::Loaded;
    return m;
}

TEST_CASE("sample_bilinear returns exact values at integer coordinates",
          "[bedmesh][bilinear]") {
    BedMeshData m = make_bilinear_test_mesh();
    // At every integer (s,t), the bilinear sample must equal get(t,s).
    // This is the "source data is preserved exactly" invariant that keeps
    // mesh tessellation from distorting the probe data.
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            REQUIRE_THAT(m.sample_bilinear(double(c), double(r)),
                         WithinAbs(m.get(r, c), 1e-6f));
}

TEST_CASE("sample_bilinear midpoints interpolate neighbors",
          "[bedmesh][bilinear]") {
    BedMeshData m = make_bilinear_test_mesh();

    // Horizontal midpoint between (0,0) and (1,0): (0.00 + 0.20) / 2 = 0.10
    REQUIRE_THAT(m.sample_bilinear(0.5, 0.0), WithinAbs(0.10f, 1e-6f));

    // Vertical midpoint between (0,0) and (0,1): (0.00 + 0.10) / 2 = 0.05
    REQUIRE_THAT(m.sample_bilinear(0.0, 0.5), WithinAbs(0.05f, 1e-6f));

    // Diagonal midpoint in the top-left quad: average of 4 corners
    // (0.00 + 0.20 + 0.10 + 0.30) / 4 = 0.15
    REQUIRE_THAT(m.sample_bilinear(0.5, 0.5), WithinAbs(0.15f, 1e-6f));

    // Off-center weighted sample at (1.25, 1.5) — between (1,1)=0.30,
    // (2,1)=0.50, (1,2)=0.40, (2,2)=0.60. Bilinear result:
    //   lerp_x at t=1: 0.30 + 0.25*(0.50-0.30) = 0.35
    //   lerp_x at t=2: 0.40 + 0.25*(0.60-0.40) = 0.45
    //   lerp_y:        0.35 + 0.5*(0.45-0.35) = 0.40
    REQUIRE_THAT(m.sample_bilinear(1.25, 1.5), WithinAbs(0.40f, 1e-6f));
}

TEST_CASE("sample_bilinear clamps out-of-range coordinates to the edge",
          "[bedmesh][bilinear]") {
    BedMeshData m = make_bilinear_test_mesh();

    // Below zero → clamp to (0,0).
    REQUIRE_THAT(m.sample_bilinear(-2.0, -2.0), WithinAbs(m.get(0, 0), 1e-6f));
    // Past the top-right corner → clamp to (rows-1, cols-1).
    REQUIRE_THAT(m.sample_bilinear( 99.0, 99.0), WithinAbs(m.get(2, 2), 1e-6f));
    // Half-step outside the right edge — s is clamped to cols-1, so this is
    // the same as an exact right-edge sample: get(t, cols-1).
    REQUIRE_THAT(m.sample_bilinear(2.5, 1.0), WithinAbs(m.get(1, 2), 1e-6f));
    // Half-step outside the bottom edge.
    REQUIRE_THAT(m.sample_bilinear(1.0, 2.5), WithinAbs(m.get(2, 1), 1e-6f));
}

TEST_CASE("sample_bilinear returns 0 for an invalid mesh",
          "[bedmesh][bilinear]") {
    BedMeshData empty;
    REQUIRE(empty.sample_bilinear(0.0, 0.0) == 0.f);
    REQUIRE(empty.sample_bilinear(1.5, 2.5) == 0.f);

    // Not enough rows/cols → is_valid() is false → returns 0.
    BedMeshData too_small;
    too_small.rows = 1; too_small.cols = 1;
    too_small.z_values = { 0.42f };
    too_small.status = BedMeshData::Status::Loaded;
    REQUIRE(!too_small.is_valid());
    REQUIRE(too_small.sample_bilinear(0.0, 0.0) == 0.f);
}

TEST_CASE("sample_bilinear output is continuous across grid cell boundaries",
          "[bedmesh][bilinear]") {
    // Bilinear interpolation is C^0 continuous — the two values on either
    // side of an integer coordinate must agree (no seams at quad edges).
    // This is what keeps the tessellated render free of visible lines
    // where one data cell ends and the next begins.
    BedMeshData m = make_bilinear_test_mesh();
    const double eps = 1e-7;
    for (int c_i = 1; c_i < 3; ++c_i) {
        const double c = double(c_i);
        REQUIRE_THAT(m.sample_bilinear(c - eps, 1.0),
                     WithinAbs(m.sample_bilinear(c + eps, 1.0), 1e-5f));
    }
    for (int r_i = 1; r_i < 3; ++r_i) {
        const double r = double(r_i);
        REQUIRE_THAT(m.sample_bilinear(1.0, r - eps),
                     WithinAbs(m.sample_bilinear(1.0, r + eps), 1e-5f));
    }
}

TEST_CASE("max_deviation_from_plane isolates warp from tilt",
          "[bedmesh][plane]") {
    // Tilted + one warped point in the middle.
    BedMeshData m;
    m.rows = 3; m.cols = 3;
    m.z_values = {
        0.0f, 0.1f, 0.2f,
        0.0f, 0.2f, 0.2f,  // center raised 0.1 above the plane
        0.0f, 0.1f, 0.2f,
    };
    m.origin  = Vec2d(0, 0);
    m.spacing = Vec2d(100, 100);
    m.recompute_range();
    m.status = BedMeshData::Status::Loaded;

    // The plane fit should pick up ~0.1 mm across 200 mm in X, and the
    // bump in the middle should show up as ~0.1 mm worst-point deviation.
    REQUIRE_THAT(m.max_deviation_from_plane(), WithinAbs(0.1f, 0.05f));
}

TEST_CASE("quality_grade classifies using threshold_mm",
          "[bedmesh][plane]") {
    BedMeshData flat;
    flat.rows = 3; flat.cols = 3;
    flat.z_values.assign(9, 0.f);
    flat.origin = Vec2d(0, 0); flat.spacing = Vec2d(100, 100);
    flat.status = BedMeshData::Status::Loaded;
    REQUIRE(flat.quality_grade(0.15f) == BedMeshData::Quality::Excellent);

    // Bump of 0.12 at the center: plane fit drifts up by ~0.013 (mean), worst
    // deviation ≈ 0.107 mm. Threshold 0.15 → not Excellent (>0.075), ≤0.15 → Good.
    BedMeshData good = flat;
    good.z_values[4] = 0.12f; // center
    good.recompute_range();
    REQUIRE(good.quality_grade(0.15f) == BedMeshData::Quality::Good);

    BedMeshData marginal = flat;
    marginal.z_values[4] = 0.20f;
    marginal.recompute_range();
    REQUIRE(marginal.quality_grade(0.15f) == BedMeshData::Quality::Marginal);

    BedMeshData bad = flat;
    bad.z_values[4] = 0.50f;
    bad.recompute_range();
    REQUIRE(bad.quality_grade(0.15f) == BedMeshData::Quality::Bad);

    // Invalid mesh → Bad.
    BedMeshData empty;
    REQUIRE(empty.quality_grade(0.15f) == BedMeshData::Quality::Bad);
}

TEST_CASE("subtract errors on dimension mismatch or invalid input",
          "[bedmesh][compare]") {
    BedMeshData a;
    a.rows = 2; a.cols = 2;
    a.z_values = { 0, 0, 0, 0 };
    a.status = BedMeshData::Status::Loaded;

    BedMeshData b = a;
    b.rows = 3; b.cols = 2;
    b.z_values.assign(6, 0.f);

    BedMeshData d = a.subtract(b);
    REQUIRE(!d.is_valid());
    REQUIRE(d.status == BedMeshData::Status::Error);
    REQUIRE(d.error_message.find("dimensions") != std::string::npos);

    BedMeshData empty;
    BedMeshData d2 = a.subtract(empty);
    REQUIRE(!d2.is_valid());
    REQUIRE(d2.status == BedMeshData::Status::Error);
}

TEST_CASE("load_from_csv reads a well-formed mesh and reverses Y",
          "[bedmesh][csv]") {
    namespace bfs = boost::filesystem;
    bfs::path tmp = bfs::unique_path("%%%%-%%%%-bedmesh.csv");
    tmp = bfs::temp_directory_path() / tmp;
    {
        bfs::ofstream os(tmp);
        os << "0.10\t0.20\n"
              "0.30\t0.40\n";
    }

    BedMeshData m = BedMeshData::load_from_csv(tmp.string(),
        Vec2d(0, 0), Vec2d(100, 100));
    bfs::remove(tmp);

    REQUIRE(m.is_valid());
    REQUIRE(m.rows == 2);
    REQUIRE(m.cols == 2);
    // Y reversed: row 0 in memory = last line in file
    REQUIRE_THAT(m.get(0, 0), WithinAbs(0.30f, 1e-6f));
    REQUIRE_THAT(m.get(1, 0), WithinAbs(0.10f, 1e-6f));
}

// ----------------------------------------------------------------------------
// z_deviation_to_color
// ----------------------------------------------------------------------------

TEST_CASE("z_deviation_to_color returns white on a flat mesh",
          "[bedmesh][color]") {
    ColorRGBA c = z_deviation_to_color(0.0f, 0.0f, 0.0f);
    REQUIRE(c.r() == 1.0f);
    REQUIRE(c.g() == 1.0f);
    REQUIRE(c.b() == 1.0f);
}

TEST_CASE("z_deviation_to_color is symmetric about z_ref",
          "[bedmesh][color]") {
    // Reference = 0, range [-0.2, 0.2]
    ColorRGBA center = z_deviation_to_color(0.0f, -0.2f, 0.2f);
    REQUIRE_THAT(center.r(), WithinAbs(1.0f, 1e-4f));
    REQUIRE_THAT(center.g(), WithinAbs(1.0f, 1e-4f));
    REQUIRE_THAT(center.b(), WithinAbs(1.0f, 1e-4f));

    ColorRGBA high = z_deviation_to_color( 0.2f, -0.2f, 0.2f);
    ColorRGBA low  = z_deviation_to_color(-0.2f, -0.2f, 0.2f);

    // Top of ramp → dark red; bottom → dark blue.
    // Red channel: high > low. Blue channel: low > high.
    REQUIRE(high.r() > high.b());
    REQUIRE(low.b()  > low.r());
    REQUIRE(high.r() > low.r());
    REQUIRE(low.b()  > high.b());
}

TEST_CASE("z_deviation_to_color supports a non-zero reference",
          "[bedmesh][color]") {
    // Mesh ranges 0.1..0.5, reference = mean = 0.3
    // At z = z_ref, result must be white regardless of range.
    ColorRGBA at_ref = z_deviation_to_color(0.3f, 0.1f, 0.5f, 0.3f);
    REQUIRE_THAT(at_ref.r(), WithinAbs(1.0f, 1e-4f));
    REQUIRE_THAT(at_ref.g(), WithinAbs(1.0f, 1e-4f));
    REQUIRE_THAT(at_ref.b(), WithinAbs(1.0f, 1e-4f));

    ColorRGBA below_ref = z_deviation_to_color(0.1f, 0.1f, 0.5f, 0.3f);
    REQUIRE(below_ref.b() > below_ref.r()); // blue-ish
    ColorRGBA above_ref = z_deviation_to_color(0.5f, 0.1f, 0.5f, 0.3f);
    REQUIRE(above_ref.r() > above_ref.b()); // red-ish
}

TEST_CASE("z_deviation_to_color clamps values outside the range",
          "[bedmesh][color]") {
    ColorRGBA way_high = z_deviation_to_color( 10.0f, -0.1f, 0.1f);
    ColorRGBA at_max   = z_deviation_to_color(  0.1f, -0.1f, 0.1f);
    // Both should land at the top stop (dark red) → identical.
    REQUIRE_THAT(way_high.r(), WithinAbs(at_max.r(), 1e-6f));
    REQUIRE_THAT(way_high.g(), WithinAbs(at_max.g(), 1e-6f));
    REQUIRE_THAT(way_high.b(), WithinAbs(at_max.b(), 1e-6f));

    ColorRGBA way_low = z_deviation_to_color(-10.0f, -0.1f, 0.1f);
    ColorRGBA at_min  = z_deviation_to_color( -0.1f, -0.1f, 0.1f);
    REQUIRE_THAT(way_low.r(), WithinAbs(at_min.r(), 1e-6f));
    REQUIRE_THAT(way_low.g(), WithinAbs(at_min.g(), 1e-6f));
    REQUIRE_THAT(way_low.b(), WithinAbs(at_min.b(), 1e-6f));
}

// ----------------------------------------------------------------------------
// expected_probe_count_from_m115_lines
// ----------------------------------------------------------------------------

TEST_CASE("expected_probe_count returns 49 for Core One and MK4 family",
          "[bedmesh][probe_count]") {
    using Utils::expected_probe_count_from_m115_lines;

    // Core One
    REQUIRE(expected_probe_count_from_m115_lines({
        "FIRMWARE_NAME:Prusa-Firmware-Buddy 6.5.3+12780 "
        "PROTOCOL_VERSION:1.0 MACHINE_TYPE:Prusa-Core-One EXTRUDER_COUNT:1",
        "ok"
    }) == 49);

    // Core One L (hypothetical — same machine-type string per current Buddy
    // firmware); the "Core-One" substring still matches.
    REQUIRE(expected_probe_count_from_m115_lines({
        "MACHINE_TYPE:Prusa-Core-One-L"
    }) == 49);

    // CoreOne spelling variant (no dash)
    REQUIRE(expected_probe_count_from_m115_lines({
        "MACHINE_TYPE:Prusa-CoreOne"
    }) == 49);

    // MK4
    REQUIRE(expected_probe_count_from_m115_lines({
        "MACHINE_TYPE:Prusa-MK4"
    }) == 49);

    // MK4S
    REQUIRE(expected_probe_count_from_m115_lines({
        "MACHINE_TYPE:Prusa-MK4S"
    }) == 49);

    // MK3.5
    REQUIRE(expected_probe_count_from_m115_lines({
        "MACHINE_TYPE:Prusa-MK3.5"
    }) == 49);
}

TEST_CASE("expected_probe_count returns 144 for XL",
          "[bedmesh][probe_count]") {
    using Utils::expected_probe_count_from_m115_lines;
    REQUIRE(expected_probe_count_from_m115_lines({
        "FIRMWARE_NAME:Prusa-Firmware-Buddy ... MACHINE_TYPE:Prusa-XL ..."
    }) == 144);
}

TEST_CASE("expected_probe_count returns 81 for iX",
          "[bedmesh][probe_count]") {
    using Utils::expected_probe_count_from_m115_lines;
    REQUIRE(expected_probe_count_from_m115_lines({
        "MACHINE_TYPE:Prusa-iX"
    }) == 81);
}

TEST_CASE("expected_probe_count returns 16 for MINI",
          "[bedmesh][probe_count]") {
    using Utils::expected_probe_count_from_m115_lines;
    REQUIRE(expected_probe_count_from_m115_lines({
        "MACHINE_TYPE:Prusa-MINI"
    }) == 16);
}

TEST_CASE("expected_probe_count is case-insensitive",
          "[bedmesh][probe_count]") {
    using Utils::expected_probe_count_from_m115_lines;
    REQUIRE(expected_probe_count_from_m115_lines({
        "MACHINE_TYPE:prusa-core-one"
    }) == 49);
    REQUIRE(expected_probe_count_from_m115_lines({
        "MACHINE_TYPE:prusa-xl"
    }) == 144);
    REQUIRE(expected_probe_count_from_m115_lines({
        "MACHINE_TYPE:PRUSA-MINI"
    }) == 16);
}

TEST_CASE("expected_probe_count returns 0 for unknown or missing models",
          "[bedmesh][probe_count]") {
    using Utils::expected_probe_count_from_m115_lines;

    // No MACHINE_TYPE: line at all
    REQUIRE(expected_probe_count_from_m115_lines({
        "FIRMWARE_NAME:Some-Random-Firmware 1.0 ok"
    }) == 0);

    // Empty input
    REQUIRE(expected_probe_count_from_m115_lines({}) == 0);

    // Future / unrecognized model
    REQUIRE(expected_probe_count_from_m115_lines({
        "MACHINE_TYPE:Prusa-MK5"
    }) == 0);
    REQUIRE(expected_probe_count_from_m115_lines({
        "MACHINE_TYPE:Ender-3-Pro"
    }) == 0);
}

TEST_CASE("expected_probe_count scans multiple lines to find MACHINE_TYPE",
          "[bedmesh][probe_count]") {
    using Utils::expected_probe_count_from_m115_lines;
    REQUIRE(expected_probe_count_from_m115_lines({
        "echo: some unrelated banner",
        "Cap: MMU2:0",
        "MACHINE_TYPE:Prusa-XL",
        "ok"
    }) == 144);
}

TEST_CASE("expected_probe_count uses first match only",
          "[bedmesh][probe_count]") {
    using Utils::expected_probe_count_from_m115_lines;
    // If a line contains MACHINE_TYPE:, it stops scanning even if the match
    // is unknown — the first MACHINE_TYPE value wins.
    REQUIRE(expected_probe_count_from_m115_lines({
        "MACHINE_TYPE:Unknown-Printer",
        "MACHINE_TYPE:Prusa-XL"
    }) == 0);
}

TEST_CASE("expected_probe_count handles lowercase MACHINE_TYPE prefix",
          "[bedmesh][probe_count]") {
    using Utils::expected_probe_count_from_m115_lines;
    // Some firmware forks lowercase the whole M115 response; the prefix
    // match itself must be case-insensitive, not just the model substring.
    REQUIRE(expected_probe_count_from_m115_lines({
        "machine_type:Prusa-Core-One"
    }) == 49);
}

// ----------------------------------------------------------------------------
// parse_m73_progress
// ----------------------------------------------------------------------------

TEST_CASE("parse_m73_progress extracts percentage from standard M73 lines",
          "[bedmesh][m73]") {
    using Utils::parse_m73_progress;
    REQUIRE(parse_m73_progress("M73 P0")       == 0);
    REQUIRE(parse_m73_progress("M73 P56 R2")   == 56);
    REQUIRE(parse_m73_progress("M73 P100 R0")  == 100);
    REQUIRE(parse_m73_progress("M73 P7 R3 C1") == 7);
}

TEST_CASE("parse_m73_progress tolerates echo:/// prefixes and whitespace",
          "[bedmesh][m73]") {
    using Utils::parse_m73_progress;
    REQUIRE(parse_m73_progress("echo:M73 P42")   == 42);
    REQUIRE(parse_m73_progress("// M73 P19")     == 19);
    REQUIRE(parse_m73_progress("//M73 P25")      == 25);
    REQUIRE(parse_m73_progress("  M73 P  33 R1") == 33);
    REQUIRE(parse_m73_progress("\tM73 P8")       == 8);
    REQUIRE(parse_m73_progress("m73 P12")        == 12); // lowercase
}

TEST_CASE("parse_m73_progress clamps out-of-range values",
          "[bedmesh][m73]") {
    using Utils::parse_m73_progress;
    REQUIRE(parse_m73_progress("M73 P150") == 100);
    REQUIRE(parse_m73_progress("M73 P999") == 100);
}

TEST_CASE("parse_m73_progress returns -1 for non-M73 lines",
          "[bedmesh][m73]") {
    using Utils::parse_m73_progress;
    REQUIRE(parse_m73_progress("M73")                   == -1); // no separator
    REQUIRE(parse_m73_progress("M73 R2")                == -1); // no P field
    REQUIRE(parse_m73_progress("M104 S170")             == -1);
    REQUIRE(parse_m73_progress("T:169.5 /170.0 B:60.0") == -1);
    REQUIRE(parse_m73_progress("")                      == -1);
    REQUIRE(parse_m73_progress("Probe classified as clean and OK") == -1);
}

TEST_CASE("parse_m73_progress ignores M73 without a numeric P value",
          "[bedmesh][m73]") {
    using Utils::parse_m73_progress;
    REQUIRE(parse_m73_progress("M73 Pxx") == -1);
    REQUIRE(parse_m73_progress("M73 P")   == -1);
}

// ----------------------------------------------------------------------------
// extruder_count_from_m115_lines
// ----------------------------------------------------------------------------

TEST_CASE("extruder_count_from_m115_lines extracts EXTRUDER_COUNT",
          "[bedmesh][extruder_count]") {
    using Utils::extruder_count_from_m115_lines;
    REQUIRE(extruder_count_from_m115_lines({
        "FIRMWARE_NAME:Prusa-Firmware-Buddy MACHINE_TYPE:Prusa-XL EXTRUDER_COUNT:5"
    }) == 5);
    REQUIRE(extruder_count_from_m115_lines({
        "MACHINE_TYPE:Prusa-MK4 EXTRUDER_COUNT:1"
    }) == 1);
    REQUIRE(extruder_count_from_m115_lines({
        "extruder_count:3"  // case-insensitive
    }) == 3);
    REQUIRE(extruder_count_from_m115_lines({
        "EXTRUDER_COUNT: 2"  // whitespace tolerated
    }) == 2);
}

// ----------------------------------------------------------------------------
// resolve_probe_tool — printer-safety invariant
//
// G28 parks the tool on a toolchanger, and the nozzle is what trips the bed
// probe, so a G29 run with an empty carriage drives the bed into the toolhead.
// These cases pin both halves of the rule: pick a tool up when there is one to
// pick up, and emit nothing at all when there is not.
// ----------------------------------------------------------------------------

TEST_CASE("physical_tool_count does not treat MMU slots as tools",
          "[bedmesh][probe_tool]") {
    using Utils::physical_tool_count;
    // An MMU profile carries one nozzle_diameter per filament slot behind ONE hot
    // end — e.g. PrusaResearch.ini's MK2.5 MMU is single_extruder_multi_material
    // with "nozzle_diameter = 0.4,0.4,0.4,0.4,0.4". Counting those as tools would
    // emit T<n>, which on an MMU selects a slot and loads filament mid-probe.
    REQUIRE(physical_tool_count(5, /*semm*/ true) == 1);
    REQUIRE(physical_tool_count(2, /*semm*/ true) == 1);
    // A real toolchanger (XL, INDX) is not SEMM: every nozzle is its own toolhead.
    REQUIRE(physical_tool_count(5, /*semm*/ false) == 5);
    REQUIRE(physical_tool_count(2, /*semm*/ false) == 2);
    // Plain single-extruder printers.
    REQUIRE(physical_tool_count(1, /*semm*/ false) == 1);
    // Missing/'empty nozzle_diameter must never read as "many tools".
    REQUIRE(physical_tool_count(0, /*semm*/ false) == 1);
    REQUIRE(physical_tool_count(-1, /*semm*/ false) == 1);
}

TEST_CASE("MMU profiles emit no T command end-to-end",
          "[bedmesh][probe_tool]") {
    using Utils::physical_tool_count;
    using Utils::resolve_probe_tool;
    // The composition that matters: a 5-slot MMU must reach probe_tool == -1.
    const int tools = physical_tool_count(5, /*semm*/ true);
    REQUIRE(resolve_probe_tool(tools, 0, false) == -1);
    REQUIRE(resolve_probe_tool(tools, 3, false) == -1);
    REQUIRE(resolve_probe_tool(tools, 0, true)  == -1);
    // ...while a 5-tool changer still picks its tool up.
    const int changer = physical_tool_count(5, /*semm*/ false);
    REQUIRE(resolve_probe_tool(changer, 2, false) == 2);
}

TEST_CASE("resolve_probe_tool emits no T command on single-tool printers",
          "[bedmesh][probe_tool]") {
    using Utils::resolve_probe_tool;
    // MK4 / MK4S / MK3.9 / Core One / MINI: G28 docks nothing, so no tool select
    // is needed — and T<n> would select an MMU filament slot, triggering a load.
    // -1 keeps the command stream byte-for-byte identical to before this feature.
    REQUIRE(resolve_probe_tool(1, 0, false) == -1);
    REQUIRE(resolve_probe_tool(1, 3, false) == -1);   // stray selection ignored
    REQUIRE(resolve_probe_tool(1, 0, true)  == -1);   // even with probe-all set
    // Defensive: a profile that reports a nonsense count must not emit a T either.
    REQUIRE(resolve_probe_tool(0, 0, false) == -1);
    REQUIRE(resolve_probe_tool(-1, 0, false) == -1);
}

TEST_CASE("resolve_probe_tool picks the requested tool on a toolchanger",
          "[bedmesh][probe_tool]") {
    using Utils::resolve_probe_tool;
    REQUIRE(resolve_probe_tool(5, 0, false) == 0);
    REQUIRE(resolve_probe_tool(5, 2, false) == 2);
    REQUIRE(resolve_probe_tool(5, 4, false) == 4);
    REQUIRE(resolve_probe_tool(2, 1, false) == 1);    // INDX-shaped, 2 tools
}

TEST_CASE("resolve_probe_tool clamps and defaults out-of-range selections",
          "[bedmesh][probe_tool]") {
    using Utils::resolve_probe_tool;
    // Never emit a T for a tool the printer does not have.
    REQUIRE(resolve_probe_tool(2, 7, false) == 1);
    REQUIRE(resolve_probe_tool(5, 99, false) == 4);
    // wxNOT_FOUND (-1) or any negative selection falls back to the first tool
    // rather than to "no tool", which on a toolchanger would be the unsafe answer.
    REQUIRE(resolve_probe_tool(5, -1, false) == 0);
    REQUIRE(resolve_probe_tool(2, -7, false) == 0);
}

TEST_CASE("resolve_probe_tool starts the per-tool sweep at T0",
          "[bedmesh][probe_tool]") {
    using Utils::resolve_probe_tool;
    // The sweep walks T0..Tn and pushes the first pass as T0's mesh, so it owns
    // the choice regardless of what the (disabled) picker says.
    REQUIRE(resolve_probe_tool(5, 3, true) == 0);
    REQUIRE(resolve_probe_tool(2, 1, true) == 0);
}

// ----------------------------------------------------------------------------
// G29ProgressTracker — state machine for probe progress
// ----------------------------------------------------------------------------

TEST_CASE("G29ProgressTracker emits percent from M73 lines",
          "[bedmesh][g29progress]") {
    Utils::G29ProgressTracker t(49);

    auto u = t.observe("M73 P5");
    REQUIRE(u.emit);
    REQUIRE(u.step == 5);
    REQUIRE(u.total == 100);
    REQUIRE(u.label == "5%");

    // Same percent → no re-emit (avoid flooding UI).
    u = t.observe("M73 P5 R2");
    REQUIRE_FALSE(u.emit);

    u = t.observe("M73 P42");
    REQUIRE(u.emit);
    REQUIRE(u.step == 42);
    REQUIRE(u.label == "42%");
}

TEST_CASE("G29ProgressTracker falls back to OK-counting when no M73",
          "[bedmesh][g29progress]") {
    Utils::G29ProgressTracker t(49);

    auto u = t.observe("Probe classified as clean and OK");
    REQUIRE(u.emit);
    REQUIRE(u.step == 1);
    REQUIRE(u.total == 49);
    REQUIRE(u.label == "Point 1 of 49");

    u = t.observe("Probe classified as clean and OK");
    REQUIRE(u.emit);
    REQUIRE(u.step == 2);
    REQUIRE(u.label == "Point 2 of 49");

    // Noise line → no emit.
    u = t.observe("busy: processing");
    REQUIRE_FALSE(u.emit);
}

TEST_CASE("G29ProgressTracker suppresses OK count once M73 has fired",
          "[bedmesh][g29progress]") {
    Utils::G29ProgressTracker t(49);

    // M73 arrives first.
    auto u = t.observe("M73 P20");
    REQUIRE(u.emit);
    REQUIRE(u.total == 100);

    // Subsequent "OK" lines silently bump the internal counter but don't
    // clobber the M73-driven percent bar.
    u = t.observe("Probe classified as clean and OK");
    REQUIRE_FALSE(u.emit);
    REQUIRE(t.probes_ok() == 1);
}

TEST_CASE("G29ProgressTracker pulse-mode when expected probes is 0",
          "[bedmesh][g29progress]") {
    Utils::G29ProgressTracker t(0);

    auto u = t.observe("Probe classified as clean and OK");
    REQUIRE(u.emit);
    REQUIRE(u.total == 0);          // pulse
    REQUIRE(u.label == "Point 1");
}

TEST_CASE("G29ProgressTracker overflow guard drops to pulse",
          "[bedmesh][g29progress]") {
    Utils::G29ProgressTracker t(3);

    auto u = t.observe("Probe classified as clean and OK"); REQUIRE(u.total == 3);
    u = t.observe("Probe classified as clean and OK");     REQUIRE(u.total == 3);
    u = t.observe("Probe classified as clean and OK");     REQUIRE(u.total == 3);
    // 4th > expected → switch to pulse so the bar can't exceed 100%.
    u = t.observe("Probe classified as clean and OK");
    REQUIRE(u.emit);
    REQUIRE(u.total == 0);
    REQUIRE(u.label == "Point 4");
}

TEST_CASE("G29ProgressTracker surfaces extrapolation/insufficient status",
          "[bedmesh][g29progress]") {
    Utils::G29ProgressTracker t(49);
    // After some probes, firmware may report "Extrapolating".
    t.observe("Probe classified as clean and OK");
    auto u = t.observe("Extrapolating unprobed areas");
    REQUIRE(u.emit);
    REQUIRE(u.label.find("Extrapolating") != std::string::npos);

    u = t.observe("Insufficient probe data; retrying");
    REQUIRE(u.emit);
    REQUIRE(u.label.find("Insufficient") != std::string::npos);
}

TEST_CASE("extruder_count_from_m115_lines returns 0 when missing or invalid",
          "[bedmesh][extruder_count]") {
    using Utils::extruder_count_from_m115_lines;
    REQUIRE(extruder_count_from_m115_lines({
        "MACHINE_TYPE:Prusa-MK4"  // no count field
    }) == 0);
    REQUIRE(extruder_count_from_m115_lines({}) == 0);
    REQUIRE(extruder_count_from_m115_lines({
        "EXTRUDER_COUNT:"  // no digits
    }) == 0);
    REQUIRE(extruder_count_from_m115_lines({
        "EXTRUDER_COUNT:0"  // zero is not a valid count
    }) == 0);
}

// ---- quality_grade boundary cases ----
// The classifier maps the worst-point deviation against `threshold_mm` to
// four buckets: <= 0.5*t (Excellent), <= t (Good), <= 2*t (Marginal), else
// Bad. Boundaries are inclusive (`<=`); pathological thresholds (≤ 0) and
// invalid meshes always return Bad.

namespace {
// Build a mesh whose worst-point deviation from the best-fit plane is
// approximately `target_mm`. Uses a 3×3 flat plate with the center bumped
// up by target_mm — for a plate this small the plane fit is essentially the
// mean and the worst-point deviation lands within ~10% of `target_mm`.
BedMeshData make_mesh_with_worst_deviation(float target_mm) {
    BedMeshData m;
    m.rows = 3; m.cols = 3;
    m.z_values.assign(9, 0.f);
    m.z_values[4] = target_mm;
    m.origin  = Vec2d(0, 0);
    m.spacing = Vec2d(100, 100);
    m.recompute_range();
    m.status = BedMeshData::Status::Loaded;
    return m;
}
} // namespace

TEST_CASE("quality_grade returns Bad for non-positive thresholds",
          "[bedmesh][plane]") {
    BedMeshData flat = make_mesh_with_worst_deviation(0.f);
    REQUIRE(flat.quality_grade(0.f)    == BedMeshData::Quality::Bad);
    REQUIRE(flat.quality_grade(-0.1f)  == BedMeshData::Quality::Bad);
}

TEST_CASE("quality_grade boundaries are inclusive at half-threshold and threshold",
          "[bedmesh][plane]") {
    // A 0.05 mm bump on a 3×3 grid leaves a worst-point deviation slightly
    // less than 0.05 (the plane fit absorbs ~10% of it). With threshold 0.10:
    //   worst ≈ 0.044 → 0.044 <= 0.05 (half-threshold) → Excellent.
    BedMeshData m = make_mesh_with_worst_deviation(0.05f);
    REQUIRE(m.quality_grade(0.10f) == BedMeshData::Quality::Excellent);

    // A 0.10 mm bump → worst ≈ 0.089 → just above 0.05 → Good (<= 0.10).
    m = make_mesh_with_worst_deviation(0.10f);
    REQUIRE(m.quality_grade(0.10f) == BedMeshData::Quality::Good);

    // A 0.20 mm bump → worst ≈ 0.18 → above 0.10 (Good cutoff), below
    // 0.20 (Marginal cutoff) → Marginal.
    m = make_mesh_with_worst_deviation(0.20f);
    REQUIRE(m.quality_grade(0.10f) == BedMeshData::Quality::Marginal);
}

TEST_CASE("quality_grade tightens monotonically as threshold shrinks",
          "[bedmesh][plane]") {
    // The same physical mesh must never improve when the threshold gets
    // tighter. Walk a fixed mesh through decreasing thresholds and verify
    // the grade never moves toward "better".
    BedMeshData m = make_mesh_with_worst_deviation(0.10f);
    auto grade_rank = [](BedMeshData::Quality q) {
        switch (q) {
            case BedMeshData::Quality::Excellent: return 0;
            case BedMeshData::Quality::Good:      return 1;
            case BedMeshData::Quality::Marginal:  return 2;
            case BedMeshData::Quality::Bad:       return 3;
        }
        return -1;
    };
    int prev = -1;
    for (float t : { 1.0f, 0.5f, 0.2f, 0.1f, 0.05f, 0.02f }) {
        int cur = grade_rank(m.quality_grade(t));
        if (prev >= 0) CHECK(cur >= prev);  // monotonically non-decreasing rank
        prev = cur;
    }
}

TEST_CASE("quality_grade Excellent for huge threshold even on warped beds",
          "[bedmesh][plane]") {
    BedMeshData m = make_mesh_with_worst_deviation(0.5f);  // 500 µm warp
    REQUIRE(m.quality_grade(10.0f) == BedMeshData::Quality::Excellent);
}

// ---- sample_bilinear corner cases on tiny grids ----

TEST_CASE("sample_bilinear: 2x2 grid with all four corners distinct",
          "[bedmesh][bilinear]") {
    // The smallest valid mesh. Verify each corner is exact and the center
    // sample is the four-corner average.
    BedMeshData m;
    m.rows = 2; m.cols = 2;
    m.z_values = {
        0.0f, 1.0f,
        2.0f, 3.0f,
    };
    m.origin = Vec2d(0, 0); m.spacing = Vec2d(10, 10);
    m.recompute_range();
    m.status = BedMeshData::Status::Loaded;

    REQUIRE(m.sample_bilinear(0.0, 0.0) == 0.0f);
    REQUIRE(m.sample_bilinear(1.0, 0.0) == 1.0f);
    REQUIRE(m.sample_bilinear(0.0, 1.0) == 2.0f);
    REQUIRE(m.sample_bilinear(1.0, 1.0) == 3.0f);
    // Center: (0+1+2+3)/4 = 1.5
    REQUIRE_THAT(m.sample_bilinear(0.5, 0.5), Catch::Matchers::WithinAbs(1.5f, 1e-6f));
}

TEST_CASE("sample_bilinear: NaN-free output even at the bottom-right corner",
          "[bedmesh][bilinear]") {
    // Internally sample_bilinear computes c1 = min(c0+1, cols-1) — at the
    // bottom-right corner c0 == cols-1, so the lookup degenerates. Make
    // sure that produces the corner value exactly, not NaN/inf.
    BedMeshData m;
    m.rows = 4; m.cols = 4;
    m.z_values.assign(16, 0.f);
    m.z_values[15] = 1.234f;  // bottom-right corner (rows-1, cols-1)
    m.origin = Vec2d(0, 0); m.spacing = Vec2d(10, 10);
    m.status = BedMeshData::Status::Loaded;
    m.recompute_range();

    const float v = m.sample_bilinear(3.0, 3.0);
    REQUIRE(std::isfinite(v));
    REQUIRE(v == 1.234f);
}

TEST_CASE("sample_bilinear: clamping is symmetric across both axes",
          "[bedmesh][bilinear]") {
    // Clamping must be applied independently to s and t — out-of-range on
    // one axis must not bleed into the other.
    BedMeshData m;
    m.rows = 3; m.cols = 3;
    m.z_values = {
        0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f,
        7.0f, 0.0f, 0.0f,  // bottom-left only
    };
    m.origin = Vec2d(0, 0); m.spacing = Vec2d(10, 10);
    m.status = BedMeshData::Status::Loaded;
    m.recompute_range();

    // (-100, 99): s clamps to 0, t clamps to rows-1=2 → get(2,0) = 7.0
    REQUIRE(m.sample_bilinear(-100.0, 99.0) == 7.0f);
    // (99, -100): s clamps to cols-1=2, t clamps to 0 → get(0,2) = 0
    REQUIRE(m.sample_bilinear(99.0, -100.0) == 0.0f);
}

TEST_CASE("sample_bilinear: large negative coordinates clamp to (0,0)",
          "[bedmesh][bilinear]") {
    BedMeshData m;
    m.rows = 3; m.cols = 3;
    m.z_values = {
        42.f, 0.f, 0.f,
        0.f,  0.f, 0.f,
        0.f,  0.f, 0.f,
    };
    m.origin = Vec2d(0, 0); m.spacing = Vec2d(10, 10);
    m.status = BedMeshData::Status::Loaded;
    m.recompute_range();

    REQUIRE(m.sample_bilinear(-1e9, -1e9) == 42.f);
}
