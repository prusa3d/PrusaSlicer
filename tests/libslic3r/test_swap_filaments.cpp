#include <catch2/catch_test_macros.hpp>

#include "libslic3r/Config.hpp"
#include "libslic3r/CustomGCode.hpp"

#include <algorithm>
#include <vector>

using namespace Slic3r;

// Helper: perform the same wipe tower matrix row+column swap that Plater::swap_filaments() does.
static void swap_wipe_matrix(std::vector<double>& matrix, size_t n, size_t idx_a, size_t idx_b)
{
    if (idx_a == idx_b || n * n != matrix.size() || idx_a >= n || idx_b >= n)
        return;
    // Swap rows
    for (size_t col = 0; col < n; ++col)
        std::swap(matrix[idx_a * n + col], matrix[idx_b * n + col]);
    // Swap columns
    for (size_t row = 0; row < n; ++row)
        std::swap(matrix[row * n + idx_a], matrix[row * n + idx_b]);
}

// Helper: perform the same custom G-code extruder swap that Plater::swap_filaments() does.
static void swap_custom_gcode_extruders(std::vector<CustomGCode::Item>& gcodes, int ext_a, int ext_b)
{
    for (auto& item : gcodes) {
        if (item.extruder == ext_a)
            item.extruder = ext_b;
        else if (item.extruder == ext_b)
            item.extruder = ext_a;
    }
}

TEST_CASE("Wipe tower matrix 3x3 swap", "[SwapFilaments]") {
    // 3x3 matrix:
    // row0: 1 2 3
    // row1: 4 5 6
    // row2: 7 8 9
    std::vector<double> matrix = {1,2,3, 4,5,6, 7,8,9};
    swap_wipe_matrix(matrix, 3, 0, 2);

    // After swapping rows 0<->2:
    // row0: 7 8 9
    // row1: 4 5 6
    // row2: 1 2 3
    // Then swapping cols 0<->2:
    // row0: 9 8 7
    // row1: 6 5 4
    // row2: 3 2 1
    std::vector<double> expected = {9,8,7, 6,5,4, 3,2,1};
    CHECK(matrix == expected);
}

TEST_CASE("Wipe tower matrix swap - same index is identity", "[SwapFilaments]") {
    std::vector<double> matrix = {1,2,3, 4,5,6, 7,8,9};
    std::vector<double> original = matrix;
    swap_wipe_matrix(matrix, 3, 1, 1);
    CHECK(matrix == original);
}

TEST_CASE("Wipe tower matrix 2x2 swap", "[SwapFilaments]") {
    // 2x2 matrix:
    // row0:  0 10
    // row1: 20  0
    std::vector<double> matrix = {0,10, 20,0};
    swap_wipe_matrix(matrix, 2, 0, 1);

    // After swap rows 0<->1:
    // row0: 20  0
    // row1:  0 10
    // Then swap cols 0<->1:
    // row0:  0 20
    // row1: 10  0
    std::vector<double> expected = {0,20, 10,0};
    CHECK(matrix == expected);
}

TEST_CASE("Custom G-code extruder swap", "[SwapFilaments]") {
    std::vector<CustomGCode::Item> gcodes = {
        {1.0, CustomGCode::ColorChange, 1, "#FF0000", ""},
        {2.0, CustomGCode::ToolChange,  2, "",        ""},
        {3.0, CustomGCode::ColorChange, 3, "#0000FF", ""},
    };

    // Swap extruders 1<->2 (1-based, as in Plater::swap_filaments)
    swap_custom_gcode_extruders(gcodes, 1, 2);

    CHECK(gcodes[0].extruder == 2);
    CHECK(gcodes[1].extruder == 1);
    CHECK(gcodes[2].extruder == 3);
}

TEST_CASE("Custom G-code extruder swap - no matching extruders", "[SwapFilaments]") {
    std::vector<CustomGCode::Item> gcodes = {
        {1.0, CustomGCode::ColorChange, 3, "", ""},
        {2.0, CustomGCode::ToolChange,  4, "", ""},
        {3.0, CustomGCode::ColorChange, 5, "", ""},
    };

    swap_custom_gcode_extruders(gcodes, 1, 2);

    CHECK(gcodes[0].extruder == 3);
    CHECK(gcodes[1].extruder == 4);
    CHECK(gcodes[2].extruder == 5);
}

TEST_CASE("Extruder color swap via ConfigOptionStrings", "[SwapFilaments]") {
    ConfigOptionStrings colors;
    colors.values = {"#FF0000", "#00FF00", "#0000FF"};

    std::swap(colors.values[0], colors.values[1]);

    CHECK(colors.values[0] == "#00FF00");
    CHECK(colors.values[1] == "#FF0000");
    CHECK(colors.values[2] == "#0000FF");
}
