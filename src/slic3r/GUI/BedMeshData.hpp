///|/ Copyright (c) 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_BedMeshData_hpp_
#define slic3r_BedMeshData_hpp_

#include "libslic3r/Point.hpp"
#include "libslic3r/Color.hpp"

#include <vector>
#include <string>
#include <cmath>
#include <numeric>

namespace Slic3r {
namespace GUI {

struct BedMeshData
{
    enum class Status : unsigned char { Empty, Fetching, Loaded, Error };

    // Reference point for the color map.
    //   Zero: white at Z=0, red above / blue below — shows absolute deviation
    //         from the nominal bed plane. Useful for "how much compensation?"
    //   Mean: white at the mesh mean — shows relative flatness. Useful for
    //         diagnosing warp/bowl/tilt independent of Z-offset calibration.
    enum class Reference : unsigned char { Zero, Mean };

    size_t rows = 0;
    size_t cols = 0;
    std::vector<float> z_values; // row-major, size = rows * cols
    Vec2d origin{0.0, 0.0};     // XY of first probe point (mm)
    Vec2d spacing{0.0, 0.0};    // XY step between probe points (mm)
    float z_min = 0.f;
    float z_max = 0.f;
    Status status = Status::Empty;
    std::string error_message;

    bool is_valid() const { return status == Status::Loaded && rows > 1 && cols > 1 && z_values.size() == rows * cols; }

    float get(size_t row, size_t col) const { return z_values[row * cols + col]; }

    void recompute_range()
    {
        if (z_values.empty()) {
            z_min = z_max = 0.f;
            return;
        }
        z_min = *std::min_element(z_values.begin(), z_values.end());
        z_max = *std::max_element(z_values.begin(), z_values.end());
    }

    float mean() const
    {
        if (z_values.empty()) return 0.f;
        return std::accumulate(z_values.begin(), z_values.end(), 0.f) / float(z_values.size());
    }

    float std_dev() const
    {
        if (z_values.size() < 2) return 0.f;
        float m = mean();
        float sum_sq = 0.f;
        for (float z : z_values)
            sum_sq += (z - m) * (z - m);
        return std::sqrt(sum_sq / float(z_values.size() - 1));
    }

    // Generate a mock 7x7 mesh for development/testing.
    // Simulates a gentle bowl shape typical of a slightly warped bed.
    // bed_min/bed_max define the printable area in mm.
    static BedMeshData create_mock(const Vec2d& bed_min, const Vec2d& bed_max);

    // Load a mesh from a whitespace/tab-separated CSV file as emitted by
    // Prusa Buddy firmware's `M420 V1 T1` "Bed Topography Report for CSV:" block.
    // The CSV's first row corresponds to Y_max (top of the bed topography report),
    // so rows are reversed on load so row 0 = probe_min.y (front of bed).
    // probe_min/probe_max define the XY extent of the probe grid in mm.
    // Returns a mesh with status=Loaded on success, status=Error otherwise.
    static BedMeshData load_from_csv(const std::string& path,
                                     const Vec2d& probe_min,
                                     const Vec2d& probe_max);

    // Parse a sequence of lines (e.g. the response to `M420 V1 T1` over serial)
    // and extract the mesh. Looks for a "Bed Topography Report for CSV:" header
    // and reads the consecutive numeric rows that follow. Same row-reversal and
    // validity checks as load_from_csv.
    static BedMeshData parse_m420_output(const std::vector<std::string>& lines,
                                         const Vec2d& probe_min,
                                         const Vec2d& probe_max);

    // Write this mesh to a tab-separated CSV compatible with load_from_csv
    // and with Buddy firmware's "Bed Topography Report for CSV" format.
    // Row 0 in the file corresponds to Y_max (bed back), matching the
    // on-the-wire convention — load_from_csv will reverse on reload.
    // Returns empty string on success, or a human-readable error message.
    std::string save_to_csv(const std::string& path) const;

    // Element-wise subtraction for compare mode. Both meshes must have the
    // same rows/cols (and both be Loaded); on mismatch, returns a mesh with
    // status=Error. XY origin/spacing are copied from *this.
    BedMeshData subtract(const BedMeshData& rhs) const;

    // Fit a plane Z = a*X + b*Y + c to the mesh in least-squares sense. The
    // result describes the bed's overall tilt: tilt_x / tilt_y are the slope
    // components converted to arc-minutes for human readability; rms is the
    // root-mean-square residual after removing the plane (so, the "warp
    // beyond tilt" — what a leveling routine cannot compensate for).
    struct PlaneFit
    {
        float tilt_x_arcmin{ 0.f }; // positive = front→back uphill
        float tilt_y_arcmin{ 0.f }; // positive = left→right uphill
        float rms_after{ 0.f };     // residual RMS in mm
        float offset_z{ 0.f };      // plane Z at mesh center, mm
    };
    PlaneFit fit_plane() const;

    // Max absolute deviation from the plane fit (i.e. the worst point the
    // first-layer compensation still can't fix). Used for the quality grade.
    float max_deviation_from_plane() const;

    // Simple traffic-light assessment of the mesh relative to a user-visible
    // threshold (default 0.15 mm, Prusa's rough first-layer tolerance).
    enum class Quality : unsigned char { Excellent, Good, Marginal, Bad };
    Quality quality_grade(float threshold_mm = 0.15f) const;

    // Bilinear sample at fractional grid coordinates. s ∈ [0, cols-1],
    // t ∈ [0, rows-1]; values outside clamp to the edge. At integer (s,t)
    // this returns get(t, s) exactly. Used by the rendering tessellator
    // to generate a finer mesh without modifying the underlying data.
    // Returns 0.f on an invalid mesh.
    float sample_bilinear(double s, double t) const;
};

// Map a Z value to a heatmap color via a diverging ramp centered at z_ref.
// Full saturation is reached at whichever side of z_ref is more extreme
// (i.e. the mapping is symmetric about z_ref in color space, and clamped at
// max(|z_min - z_ref|, |z_max - z_ref|)).
ColorRGBA z_deviation_to_color(float z, float z_min, float z_max, float z_ref = 0.f);

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_BedMeshData_hpp_
