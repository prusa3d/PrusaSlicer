#pragma once

#include <vector>
#include <cstddef>

namespace Slic3r::App::Preview {

struct ExtrudersSequence
{
    bool is_mm_intervals{ true };
    float interval_by_mm{ 3.0f };
    int interval_by_layers{ 10 };
    bool random_sequence{ false };
    bool color_repetition{ false };
    std::vector<size_t> extruders;

    bool operator == (const ExtrudersSequence& other) const;
    bool operator != (const ExtrudersSequence& other) const;

    void add_extruder(size_t pos, size_t extruder_id = 0);
    void delete_extruder(size_t pos);
    void init(size_t extruders_count);
};

} // namespace Slic3r::App::Preview
