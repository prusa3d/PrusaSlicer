///|/ Copyright (c) 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "BedMeshData.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>

namespace Slic3r {
namespace GUI {

BedMeshData BedMeshData::create_mock(const Vec2d& bed_min, const Vec2d& bed_max)
{
    BedMeshData mesh;
    mesh.rows = 7;
    mesh.cols = 7;

    const Vec2d bed_size = bed_max - bed_min;
    // Inset the probe grid slightly from bed edges (10mm margin)
    const double margin = 10.0;
    const Vec2d probe_min = bed_min + Vec2d(margin, margin);
    const Vec2d probe_max = bed_max - Vec2d(margin, margin);
    const Vec2d probe_size = probe_max - probe_min;

    mesh.origin = probe_min;
    mesh.spacing = Vec2d(probe_size.x() / double(mesh.cols - 1),
                         probe_size.y() / double(mesh.rows - 1));

    mesh.z_values.resize(mesh.rows * mesh.cols);

    // Simulate a realistic bed mesh: slight saddle shape with a low front-left
    // corner and high back-right, plus local waviness. Centered around zero
    // with values typically in the -0.15 to +0.15mm range.
    for (size_t r = 0; r < mesh.rows; ++r) {
        for (size_t c = 0; c < mesh.cols; ++c) {
            double nx = double(c) / double(mesh.cols - 1); // 0..1
            double ny = double(r) / double(mesh.rows - 1); // 0..1

            // Tilt: bed slopes slightly from front-left (-) to back-right (+)
            double tilt = 0.08 * (nx + ny - 1.0);

            // Saddle: high on two diagonal corners, low on the other two
            double saddle = 0.06 * (2.0 * nx - 1.0) * (2.0 * ny - 1.0);

            // Bowl: slight center depression
            double dx = nx - 0.5;
            double dy = ny - 0.5;
            double bowl = -0.04 * (1.0 - 4.0 * (dx * dx + dy * dy));

            // Local waviness to simulate surface imperfections
            double wave = 0.02 * std::sin(nx * 9.42) * std::cos(ny * 6.28)
                        + 0.015 * std::cos(nx * 12.57 + ny * 3.14);

            mesh.z_values[r * mesh.cols + c] = float(tilt + saddle + bowl + wave);
        }
    }

    mesh.recompute_range();
    mesh.status = Status::Loaded;
    return mesh;
}

// Parse one line as a row of whitespace/tab-separated floats.
// Returns a row on success, or empty vector on failure (non-numeric/empty line).
// NaN/Inf are preserved in the output — caller decides how to handle them.
static std::vector<float> parse_numeric_row(const std::string& in)
{
    std::string line = in;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) return {};

    std::istringstream iss(line);
    std::vector<float> row;
    float v;
    while (iss >> v) row.push_back(v);

    // Must be fully numeric (no trailing garbage like "|" or "19" row labels)
    iss.clear();
    std::string leftover;
    if (iss >> leftover) return {};
    if (row.size() < 2) return {};
    return row;
}

// Shared row-assembly from the collected raw rows (top-row = Y_max convention).
static BedMeshData finalize_mesh(std::vector<std::vector<float>>&& raw_rows,
                                 const Vec2d& probe_min,
                                 const Vec2d& probe_max)
{
    BedMeshData mesh;
    if (raw_rows.size() < 2) {
        mesh.status = BedMeshData::Status::Error;
        mesh.error_message = "Mesh has fewer than 2 rows";
        return mesh;
    }
    const size_t cols = raw_rows.front().size();
    for (const auto& r : raw_rows) {
        if (r.size() != cols) {
            mesh.status = BedMeshData::Status::Error;
            mesh.error_message = "Inconsistent row widths";
            return mesh;
        }
        for (float v : r) {
            if (std::isnan(v) || std::isinf(v)) {
                mesh.status = BedMeshData::Status::Error;
                mesh.error_message = "Mesh contains NaN/Inf (no mesh stored, or probing failed)";
                return mesh;
            }
        }
    }

    mesh.rows = raw_rows.size();
    mesh.cols = cols;
    mesh.z_values.resize(mesh.rows * mesh.cols);

    // Buddy firmware's CSV top row = Y_max. Renderer expects row 0 = probe_min.y,
    // so reverse the row order here.
    for (size_t r = 0; r < mesh.rows; ++r) {
        const auto& src = raw_rows[mesh.rows - 1 - r];
        std::copy(src.begin(), src.end(), mesh.z_values.begin() + r * mesh.cols);
    }

    const Vec2d probe_size = probe_max - probe_min;
    mesh.origin = probe_min;
    mesh.spacing = Vec2d(probe_size.x() / double(mesh.cols - 1),
                         probe_size.y() / double(mesh.rows - 1));

    mesh.recompute_range();
    mesh.status = BedMeshData::Status::Loaded;
    return mesh;
}

BedMeshData BedMeshData::load_from_csv(const std::string& path,
                                       const Vec2d& probe_min,
                                       const Vec2d& probe_max)
{
    BedMeshData mesh;
    std::ifstream f(path);
    if (!f.is_open()) {
        mesh.status = Status::Error;
        mesh.error_message = "Cannot open CSV: " + path;
        return mesh;
    }

    std::vector<std::vector<float>> raw_rows;
    std::string line;
    while (std::getline(f, line)) {
        std::vector<float> row = parse_numeric_row(line);
        if (!row.empty())
            raw_rows.push_back(std::move(row));
        else if (!raw_rows.empty())
            break; // end of numeric block
    }

    return finalize_mesh(std::move(raw_rows), probe_min, probe_max);
}

std::string BedMeshData::save_to_csv(const std::string& path) const
{
    if (!is_valid())
        return "Cannot save an invalid or empty mesh.";
    std::ofstream f(path);
    if (!f.is_open())
        return "Cannot open file for writing: " + path;

    // Write with Y reversed so reloading via load_from_csv round-trips.
    // Precision: 4 decimal places is enough for 0.1 micron resolution and
    // keeps files small — the firmware itself emits 2-3 digits.
    for (std::size_t r = 0; r < rows; ++r) {
        const std::size_t src_row = rows - 1 - r;
        for (std::size_t c = 0; c < cols; ++c) {
            if (c > 0) f << '\t';
            f << std::fixed;
            f.precision(4);
            f << z_values[src_row * cols + c];
        }
        f << '\n';
    }
    if (!f.good())
        return "Write failed: " + path;
    return {};
}

BedMeshData::PlaneFit BedMeshData::fit_plane() const
{
    PlaneFit out;
    if (!is_valid())
        return out;

    // Least-squares plane fit: minimise sum (z_i - (a*x_i + b*y_i + c))^2.
    // Normal equations: [Sxx Sxy Sx][a]   [Sxz]
    //                   [Sxy Syy Sy][b] = [Syz]
    //                   [Sx  Sy  N ][c]   [Sz ]
    // Here X,Y are world-space mm (origin + col*spacing, origin + row*spacing).
    double Sxx = 0, Sxy = 0, Sx = 0, Syy = 0, Sy = 0, Sxz = 0, Syz = 0, Sz = 0;
    const std::size_t N = rows * cols;
    for (std::size_t r = 0; r < rows; ++r) {
        const double y = origin.y() + double(r) * spacing.y();
        for (std::size_t c = 0; c < cols; ++c) {
            const double x = origin.x() + double(c) * spacing.x();
            const double z = z_values[r * cols + c];
            Sxx += x * x;  Sxy += x * y;  Sx += x;
            Syy += y * y;  Sy  += y;
            Sxz += x * z;  Syz += y * z;  Sz += z;
        }
    }
    // 3x3 linear solve via Cramer's rule — 3 equations are plenty for a
    // probe grid and it avoids pulling Eigen into this TU.
    const double Nd = double(N);
    const double M[3][3] = { { Sxx, Sxy, Sx },
                             { Sxy, Syy, Sy },
                             { Sx,  Sy,  Nd } };
    const double b[3] = { Sxz, Syz, Sz };
    auto det3 = [](const double m[3][3]) {
        return m[0][0] * (m[1][1]*m[2][2] - m[1][2]*m[2][1])
             - m[0][1] * (m[1][0]*m[2][2] - m[1][2]*m[2][0])
             + m[0][2] * (m[1][0]*m[2][1] - m[1][1]*m[2][0]);
    };
    const double D = det3(M);
    if (std::abs(D) < 1e-12)
        return out; // degenerate (co-linear or single-row); leave zeros.
    auto replace_col = [](const double m[3][3], int col, const double v[3], double out_m[3][3]) {
        for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j)
            out_m[i][j] = (j == col) ? v[i] : m[i][j];
    };
    double Ma[3][3], Mb[3][3], Mc[3][3];
    replace_col(M, 0, b, Ma);
    replace_col(M, 1, b, Mb);
    replace_col(M, 2, b, Mc);
    const double a   = det3(Ma) / D;
    const double bb  = det3(Mb) / D;
    const double cc  = det3(Mc) / D;

    // Slope → arc-minutes (rise/run → degrees → arcmin).
    // atan(|slope|) in radians × 180/π × 60.
    constexpr double kRadToArcmin = 180.0 * 60.0 / 3.14159265358979323846;
    out.tilt_x_arcmin = float(std::atan(a) * kRadToArcmin);
    out.tilt_y_arcmin = float(std::atan(bb) * kRadToArcmin);

    // Residuals.
    double sum_sq = 0.0;
    for (std::size_t r = 0; r < rows; ++r) {
        const double y = origin.y() + double(r) * spacing.y();
        for (std::size_t c = 0; c < cols; ++c) {
            const double x = origin.x() + double(c) * spacing.x();
            const double z_fit = a * x + bb * y + cc;
            const double resid = z_values[r * cols + c] - z_fit;
            sum_sq += resid * resid;
        }
    }
    out.rms_after = float(std::sqrt(sum_sq / double(N)));

    // Plane Z at mesh center — useful as "what leveling nudge would we need".
    const double cx = origin.x() + 0.5 * double(cols - 1) * spacing.x();
    const double cy = origin.y() + 0.5 * double(rows - 1) * spacing.y();
    out.offset_z = float(a * cx + bb * cy + cc);
    return out;
}

float BedMeshData::max_deviation_from_plane() const
{
    if (!is_valid()) return 0.f;
    const PlaneFit pf = fit_plane();
    // Reconstruct a/b/c from tilt_x/y and offset at center.
    // Easier: just re-run a minimal fit in place. For mesh-size test grids
    // the cost is negligible; for a 21×21 runtime grid it's still <1 µs.
    constexpr double kArcminToRad = 3.14159265358979323846 / (180.0 * 60.0);
    const double a = std::tan(double(pf.tilt_x_arcmin) * kArcminToRad);
    const double b = std::tan(double(pf.tilt_y_arcmin) * kArcminToRad);
    const double cx = origin.x() + 0.5 * double(cols - 1) * spacing.x();
    const double cy = origin.y() + 0.5 * double(rows - 1) * spacing.y();
    const double c = double(pf.offset_z) - a * cx - b * cy;

    float worst = 0.f;
    for (std::size_t r = 0; r < rows; ++r) {
        const double y = origin.y() + double(r) * spacing.y();
        for (std::size_t col = 0; col < cols; ++col) {
            const double x = origin.x() + double(col) * spacing.x();
            const double z_fit = a * x + b * y + c;
            const float dev = std::abs(float(z_values[r * cols + col] - z_fit));
            if (dev > worst) worst = dev;
        }
    }
    return worst;
}

BedMeshData::Quality BedMeshData::quality_grade(float threshold_mm) const
{
    if (!is_valid() || threshold_mm <= 0.f)
        return Quality::Bad;
    const float worst = max_deviation_from_plane();
    if (worst <= threshold_mm * 0.5f) return Quality::Excellent;
    if (worst <= threshold_mm       ) return Quality::Good;
    if (worst <= threshold_mm * 2.f ) return Quality::Marginal;
    return Quality::Bad;
}

float BedMeshData::sample_bilinear(double s, double t) const
{
    if (!is_valid()) return 0.f;
    const double max_s = double(cols - 1);
    const double max_t = double(rows - 1);
    if (s < 0.0) s = 0.0;
    if (t < 0.0) t = 0.0;
    if (s > max_s) s = max_s;
    if (t > max_t) t = max_t;
    const std::size_t c0 = std::size_t(std::floor(s));
    const std::size_t r0 = std::size_t(std::floor(t));
    const std::size_t c1 = std::min(c0 + 1, cols - 1);
    const std::size_t r1 = std::min(r0 + 1, rows - 1);
    const float fs = float(s - double(c0));
    const float ft = float(t - double(r0));
    const float z00 = get(r0, c0);
    const float z01 = get(r0, c1);
    const float z10 = get(r1, c0);
    const float z11 = get(r1, c1);
    const float zi0 = z00 + fs * (z01 - z00);
    const float zi1 = z10 + fs * (z11 - z10);
    return zi0 + ft * (zi1 - zi0);
}

BedMeshData BedMeshData::subtract(const BedMeshData& rhs) const
{
    BedMeshData out;
    if (!is_valid() || !rhs.is_valid()) {
        out.status = Status::Error;
        out.error_message = "Both meshes must be valid to subtract.";
        return out;
    }
    if (rows != rhs.rows || cols != rhs.cols) {
        out.status = Status::Error;
        out.error_message = "Mesh dimensions differ: "
                          + std::to_string(rows) + "x" + std::to_string(cols) + " vs "
                          + std::to_string(rhs.rows) + "x" + std::to_string(rhs.cols);
        return out;
    }
    out.rows    = rows;
    out.cols    = cols;
    out.origin  = origin;
    out.spacing = spacing;
    out.z_values.resize(z_values.size());
    for (std::size_t i = 0; i < z_values.size(); ++i)
        out.z_values[i] = z_values[i] - rhs.z_values[i];
    out.recompute_range();
    out.status = Status::Loaded;
    return out;
}

BedMeshData BedMeshData::parse_m420_output(const std::vector<std::string>& lines,
                                           const Vec2d& probe_min,
                                           const Vec2d& probe_max)
{
    BedMeshData mesh;

    // Locate the CSV header. Collect numeric rows immediately following it.
    bool in_csv_block = false;
    std::vector<std::vector<float>> raw_rows;
    for (const auto& raw : lines) {
        if (!in_csv_block) {
            if (raw.find("Bed Topography Report for CSV") != std::string::npos)
                in_csv_block = true;
            continue;
        }
        std::vector<float> row = parse_numeric_row(raw);
        if (!row.empty())
            raw_rows.push_back(std::move(row));
        else if (!raw_rows.empty())
            break;
    }

    if (!in_csv_block) {
        mesh.status = Status::Error;
        mesh.error_message = "No 'Bed Topography Report for CSV:' header in response";
        return mesh;
    }

    return finalize_mesh(std::move(raw_rows), probe_min, probe_max);
}

ColorRGBA z_deviation_to_color(float z, float z_min, float z_max, float z_ref)
{
    // Diverging colormap, symmetric around z_ref. Multi-stop ramp so modest
    // deviations still show a distinct color — a 2-color blue↔red gradient
    // compresses mid-magnitude points into dim tints and looks monochrome for
    // meshes dominated by one sign.
    const float max_abs = std::max(std::abs(z_min - z_ref), std::abs(z_max - z_ref));
    if (max_abs < 1e-6f)
        return ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);

    const float t = std::clamp((z - z_ref) / max_abs, -1.0f, 1.0f); // -1..+1

    // Below zero: dark blue -> blue -> cyan -> pale cyan -> white
    static const ColorRGBA stops_neg[5] = {
        { 0.05f, 0.05f, 0.45f, 1.0f }, // t = -1.0
        { 0.10f, 0.40f, 0.90f, 1.0f }, // t = -0.75
        { 0.25f, 0.80f, 0.95f, 1.0f }, // t = -0.50
        { 0.75f, 0.95f, 0.98f, 1.0f }, // t = -0.25
        { 1.00f, 1.00f, 1.00f, 1.0f }, // t =  0.0
    };
    // Above zero: white -> yellow -> orange -> red -> dark red
    static const ColorRGBA stops_pos[5] = {
        { 1.00f, 1.00f, 1.00f, 1.0f }, // t = 0.0
        { 1.00f, 0.95f, 0.35f, 1.0f }, // t = 0.25
        { 1.00f, 0.55f, 0.10f, 1.0f }, // t = 0.50
        { 0.90f, 0.15f, 0.05f, 1.0f }, // t = 0.75
        { 0.45f, 0.00f, 0.00f, 1.0f }, // t = 1.0
    };

    auto lerp = [](const ColorRGBA& a, const ColorRGBA& b, float u) {
        return ColorRGBA(a.r() + (b.r() - a.r()) * u,
                         a.g() + (b.g() - a.g()) * u,
                         a.b() + (b.b() - a.b()) * u,
                         1.0f);
    };
    auto sample = [&](const ColorRGBA* stops, float u) {
        // u in [0,1], 4 segments between 5 stops
        float s = u * 4.0f;
        int   i = std::min(3, std::max(0, int(s)));
        float f = s - float(i);
        return lerp(stops[i], stops[i + 1], f);
    };

    if (t >= 0.f)
        return sample(stops_pos, t);                 // 0..1
    return sample(stops_neg, 1.0f + t);              // t=-1 -> u=0, t=0 -> u=1
}

} // namespace GUI
} // namespace Slic3r
