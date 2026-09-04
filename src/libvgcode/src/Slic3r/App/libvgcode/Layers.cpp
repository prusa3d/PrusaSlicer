#include "Slic3r/App/libvgcode/Layers.hpp"

#include <assert.h>
#include <algorithm>

using namespace Slic3r::Biz::libpgcode;

namespace Slic3r::App::libvgcode {

static bool is_colorprint_option(const MoveVertex& v)
{
    return v.type == MoveType::PausePrint || v.type == MoveType::CustomGCode;
}

void Layers::update(const MoveVertex& vertex, uint32_t vertex_id)
{
    if (m_items.empty() || vertex.layer_id == m_items.size()) {
        // this code assumes that gcode paths are sent sequentially, one layer after the other
        assert(vertex.layer_id == uint32_t(m_items.size()));
        Item& item = m_items.emplace_back(Item());
        if (vertex.type == MoveType::Extrude && vertex.extrusion_role != Domain::GCodeExtrusionRole::Custom)
            item.z = vertex.position[2];
        item.range.set(vertex_id, vertex_id);
        item.times = vertex.time;
        item.contains_colorprint_options |= is_colorprint_option(vertex);
    }
    else {
        Item& item = m_items.back();
        if (vertex.type == MoveType::Extrude && vertex.extrusion_role != Domain::GCodeExtrusionRole::Custom && item.z != vertex.position[2])
            item.z = vertex.position[2];
        item.range.set_max(vertex_id);
        for (size_t i = 0; i < TIME_MODES_COUNT; ++i) {
            item.times[i] += vertex.time[i];
        }
        item.contains_colorprint_options |= is_colorprint_option(vertex);
    }
}

void Layers::update_as_sla(float z, float time)
{
    Item& item = m_items.emplace_back(Item());
    item.z = z;
    item.times[0] = time;
    item.times[1] = time;
}

void Layers::reset()
{
    m_items.clear();
    m_view_range.reset();
}

std::vector<float> Layers::times(TimeMode mode) const
{
    std::vector<float> ret;
    if (mode < TimeMode::COUNT) {
        for (const Item& item : m_items) {
            ret.emplace_back(item.times[size_t(mode)]);
        }
    }
    return ret;
}

std::vector<float> Layers::zs() const
{
    std::vector<float> ret;
    ret.reserve(m_items.size());
    for (const Item& item : m_items) {
        ret.emplace_back(item.z);
    }
    return ret;
}

size_t Layers::layer_id_at(float z) const
{
    auto iter = std::upper_bound(m_items.begin(), m_items.end(), z, [](float z, const Item& item) { return item.z < z; });
    return std::distance(m_items.begin(), iter);
}

} // namespace Slic3r::App::libvgcode
