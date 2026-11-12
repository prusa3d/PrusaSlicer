#include "Slic3r/App/Preview/ExtrudersSequence.hpp"

namespace Slic3r::App::Preview {

bool ExtrudersSequence::operator == (const ExtrudersSequence& other) const
{
    return other.is_mm_intervals == this->is_mm_intervals &&
           other.interval_by_mm == this->interval_by_mm &&
           other.interval_by_layers == this->interval_by_layers &&
           other.random_sequence == this->random_sequence &&
           other.color_repetition == this->color_repetition &&
           other.extruders == this->extruders;
}

bool ExtrudersSequence::operator != (const ExtrudersSequence& other) const
{
    return !operator==(other);
}

void ExtrudersSequence::add_extruder(size_t pos, size_t extruder_id)
{
    extruders.insert(extruders.begin() + pos + 1, extruder_id);
}

void ExtrudersSequence::delete_extruder(size_t pos)
{            
    if (extruders.size() == 1)
        return;// last item can't be deleted
    extruders.erase(extruders.begin() + pos);
}

void ExtrudersSequence::init(size_t extruders_count)
{
    extruders.clear();
    for (size_t extruder = 0; extruder < extruders_count; extruder++) {
        extruders.push_back(extruder);
    }
}

} // namespace Slic3r::App::Preview
