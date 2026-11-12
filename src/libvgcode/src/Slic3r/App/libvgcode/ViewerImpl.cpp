#include "ViewerImpl.hpp"
#include "Utils.hpp"
#include "ObjExport.hpp"
#include "Slic3r/App/libvgcode/ViewerInputData.hpp"
#include "Slic3r/App/libvgcode/GCodeNodeTag.hpp"

#include <Slic3r/Biz/libpgcode/Utils.hpp>
#include <Slic3r/App/Render/GL/commonGL.hpp>
#include <Slic3r/App/Render/Device.hpp>
#include <Slic3r/App/Render/Context.hpp>
#include <Slic3r/App/Render/TextureManager.hpp>
#include <Slic3r/App/Render/TextureBufferManager.hpp>
#include <Slic3r/App/Render/Material.hpp>
#include <Slic3r/App/Scene/NodeBuilder.hpp>
#include <Slic3r/App/Scene/Scene.hpp>
#include "Slic3r/App/Scene/InstancedMeshRenderNodeComponent.hpp"

#include <map>
#include <assert.h>
#include <stdexcept>
#include <cstdio>
#include <string>
#include <algorithm>
#include <cmath>
#include <numeric>

using namespace Slic3r::Biz::libpgcode;
using Slic3r::Domain::Vec3f;
using Slic3r::Domain::GCodeExtrusionRole;

namespace Slic3r::App::libvgcode {

static constexpr int POSITION_TEX_ID = 0;
static constexpr int HEIGHT_WIDTH_ANGLE_TEX_ID = 1;
static constexpr int COLOR_TEX_ID = 2;
static constexpr int ENABLED_SEGMENTS_TEX_ID = 3;
static constexpr int ENABLED_OPTIONS_TEX_ID = 3;

template<class T, class O = T>
using IntegerOnly = std::enable_if_t<std::is_integral<T>::value, O>;

// Rounding up.
// 1.5 is rounded to 2
// 1.49 is rounded to 1
// 0.5 is rounded to 1,
// 0.49 is rounded to 0
// -0.5 is rounded to 0,
// -0.51 is rounded to -1,
// -1.5 is rounded to -1.
// -1.51 is rounded to -2.
// If input is not a valid float (it is infinity NaN or if it does not fit)
// the float to int conversion produces a max int on Intel and +-max int on ARM.
template<typename I>
inline IntegerOnly<I, I> fast_round_up(double a)
{
    // Why does Java Math.round(0.49999999999999994) return 1?
    // https://stackoverflow.com/questions/9902968/why-does-math-round0-49999999999999994-return-1
    return a == 0.49999999999999994 ? I(0) : I(floor(a + 0.5));
}

// Round to a bin with minimum two digits resolution.
// Equivalent to conversion to string with sprintf(buf, "%.2g", value) and conversion back to float, but faster.
static float round_to_bin(const float value)
{
//    assert(value >= 0);
    constexpr float const scale[5]     = { 100.f,  1000.f,  10000.f,  100000.f,  1000000.f };
    constexpr float const invscale[5]  = { 0.01f,  0.001f,  0.0001f,  0.00001f,  0.000001f };
    constexpr float const threshold[5] = { 0.095f, 0.0095f, 0.00095f, 0.000095f, 0.0000095f };
    // Scaling factor, pointer to the tables above.
    int                   i = 0;
    // While the scaling factor is not yet large enough to get two integer digits after scaling and rounding:
    for (; value < threshold[i] && i < 4; ++i);
    // At least on MSVC std::round() calls a complex function, which is pretty expensive.
    // our fast_round_up is much cheaper and it could be inlined.
//    return std::round(value * scale[i]) * invscale[i];
    double a = value * scale[i];
    assert(std::abs(a) < double(std::numeric_limits<int64_t>::max()));
    return fast_round_up<int64_t>(a) * invscale[i];
}

static const std::array<ColorRGB, GCODE_EXTRUSION_ROLES_COUNT> DEFAULT_EXTRUSION_ROLES_COLORS = { {
    { 0.90f, 0.70f, 0.70f }, // None
    { 1.00f, 0.90f, 0.30f }, // Perimeter
    { 1.00f, 0.49f, 0.22f }, // ExternalPerimeter
    { 0.12f, 0.12f, 1.00f }, // OverhangPerimeter
    { 0.69f, 0.19f, 0.16f }, // InternalInfill
    { 0.59f, 0.33f, 0.80f }, // SolidInfill
    { 0.94f, 0.25f, 0.25f }, // TopSolidInfill
    { 1.00f, 0.55f, 0.41f }, // Ironing
    { 0.30f, 0.50f, 0.73f }, // BridgeInfill
    { 1.00f, 1.00f, 1.00f }, // GapFill
    { 0.00f, 0.53f, 0.43f }, // Skirt
    { 0.00f, 1.00f, 0.00f }, // SupportMaterial
    { 0.00f, 0.50f, 0.00f }, // SupportMaterialInterface
    { 0.70f, 0.89f, 0.67f }, // WipeTower
    { 0.37f, 0.82f, 0.58f }  // Custom
}};

static const std::array<ColorRGB, OPTION_TYPES_COUNT> DEFAULT_OPTIONS_COLORS{{
    { 0.22f, 0.28f, 0.61f }, // Travels
    { 1.00f, 1.00f, 0.00f }, // Wipes
    { 0.80f, 0.13f, 0.84f }, // Retractions
    { 0.29f, 0.68f, 0.81f }, // Unretractions
    { 0.90f, 0.90f, 0.90f }, // Seams
    { 0.76f, 0.75f, 0.39f }, // ToolChanges
    { 0.86f, 0.58f, 0.55f }, // ColorChanges
    { 0.32f, 0.94f, 0.51f }, // PausePrints
    { 0.89f, 0.82f, 0.26f }  // CustomGCodes
}};

#if !USE_TEXTURE_BUFFER
static std::pair<size_t, size_t> width_height(size_t count, size_t max_texture_size)
{
    std::pair<size_t, size_t> ret;
    ret.first = std::min(count, max_texture_size);
    size_t rows_count = count / ret.first;
    if (count > rows_count * ret.first)
        ++rows_count;
    ret.second = std::min(rows_count, max_texture_size);
    return ret;
}

void ViewerImpl::TextureData::init(Render::Device* device, size_t vertices_count)
{
    if (vertices_count == 0)
        return;

    m_device = device;
    m_max_texture_size = m_device->context().max_texture_size();
    m_width = std::min(vertices_count, m_max_texture_size);
    size_t rows_count = vertices_count / m_width;
    if (vertices_count > rows_count * m_width)
        ++rows_count;
    m_height = std::min(rows_count, m_max_texture_size);
    m_count = rows_count / m_height;
    if (rows_count > m_count * m_height)
        ++m_count;

    m_tex_ids = std::vector<Textures>(m_count);
}

void ViewerImpl::TextureData::set_positions(const std::vector<Vec4f>& positions)
{
    if (m_count == 0)
        return;

    if (positions.empty())
        return;

    size_t tex_capacity = max_texture_capacity();
    size_t remaining = positions.size();
    for (size_t i = 0; i < m_count; ++i) {
        auto [w, h] = width_height(std::min(remaining, tex_capacity), m_max_texture_size);
        size_t offset = i * tex_capacity;

        assert(m_device != nullptr);
        Render::Texture* tex =
            m_device->context().texture_manager()
                .create_empty(format("libvgcode_positions_%d", i), Render::PixelFormat::RGBA32F, w, h);

        tex->set_filtering(Render::TextureMinFilter::Nearest, Render::TextureMagFilter::Nearest);
        if (remaining >= tex_capacity){
            tex->set_data(Render::PixelFormat::RGBA32F, 0, w, h, &positions[offset]);
            m_tex_ids[i].positions.second = w * h;
        }
        else {
            // the last row is only partially fitted with data, send it separately
            tex->set_data(Render::PixelFormat::RGBA32F, 0, w, h, nullptr);
            tex->set_sub_data(Render::PixelFormat::RGBA32F, 0, 0, 0, w, h - 1, &positions[offset]);
            tex->set_sub_data(Render::PixelFormat::RGBA32F, 0, 0, h - 1, remaining % w, 1, &positions[offset + w * (h - 1)]);
            m_tex_ids[i].positions.second = w * (h - 1) + remaining % w;
        }
        m_tex_ids[i].positions.first = tex;
        remaining = (remaining > tex_capacity) ? remaining - tex_capacity: 0;
    }
}

void ViewerImpl::TextureData::set_heights_widths_angles(const std::vector<Vec4f>& heights_widths_angles)
{
    if (m_count == 0)
        return;

    if (heights_widths_angles.empty())
        return;

    size_t tex_capacity = max_texture_capacity();
    size_t remaining = heights_widths_angles.size();
    for (size_t i = 0; i < m_count; ++i) {
        auto [w, h] = width_height(std::min(remaining, tex_capacity), m_max_texture_size);
        size_t offset = i * tex_capacity;

        assert(m_device != nullptr);
        Render::Texture* tex =
            m_device->context().texture_manager()
                .create_empty(format("libvgcode_hwas_%d", i), Render::PixelFormat::RGBA32F, w, h);

        tex->set_filtering(Render::TextureMinFilter::Nearest, Render::TextureMagFilter::Nearest);
        if (remaining >= tex_capacity){
            tex->set_data(Render::PixelFormat::RGBA32F, 0, w, h, &heights_widths_angles[offset]);
            m_tex_ids[i].heights_widths_angles.second = w * h;
        }
        else {
            // the last row is only partially fitted with data, send it separately
            tex->set_data(Render::PixelFormat::RGBA32F, 0, w, h, nullptr);
            tex->set_sub_data(Render::PixelFormat::RGBA32F, 0, 0, 0, w, h - 1, &heights_widths_angles[offset]);
            tex->set_sub_data(Render::PixelFormat::RGBA32F, 0, 0, h - 1, remaining % w, 1, &heights_widths_angles[offset + w * (h - 1)]);
            m_tex_ids[i].heights_widths_angles.second = w * (h - 1) + remaining % w;
        }
        m_tex_ids[i].heights_widths_angles.first = tex;
        remaining = (remaining > tex_capacity) ? remaining - tex_capacity : 0;
    }
}

void ViewerImpl::TextureData::set_colors(const std::vector<float>& colors)
{
    if (m_count == 0)
        return;

    if (colors.empty())
        return;

    size_t tex_capacity = max_texture_capacity();
    size_t remaining = colors.size();
    for (size_t i = 0; i < m_count; ++i) {
        auto [w, h] = width_height(std::min(remaining, tex_capacity), m_max_texture_size);
        size_t offset = i * tex_capacity;

        assert(m_device != nullptr);
        Render::Texture* tex =
            m_device->context().texture_manager()
                .create_empty(format("libvgcode_colors_%d", i), Render::PixelFormat::R32F, w, h);

        tex->set_filtering(Render::TextureMinFilter::Nearest, Render::TextureMagFilter::Nearest);
        if (remaining >= tex_capacity){
            tex->set_data(Render::PixelFormat::R32F, 0, w, h, &colors[offset]);
            m_tex_ids[i].colors.second = w * h;
        }
        else {
            // the last row is only partially fitted with data, send it separately
            tex->set_data(Render::PixelFormat::R32F, 0, w, h, nullptr);
            tex->set_sub_data(Render::PixelFormat::R32F, 0, 0, 0, w, h - 1, &colors[offset]);
            tex->set_sub_data(Render::PixelFormat::R32F, 0, 0, h - 1, remaining % w, 1, &colors[offset + w * (h - 1)]);
            m_tex_ids[i].colors.second = w * (h - 1) + remaining % w;
        }
        m_tex_ids[i].colors.first = tex;
        remaining = (remaining > tex_capacity) ? remaining - tex_capacity : 0;
    }
}

void ViewerImpl::TextureData::set_enabled_segments(const std::vector<uint32_t>& enabled_segments)
{
    if (m_count == 0)
        return;

    if (enabled_segments.empty())
        return;

    size_t tex_capacity = max_texture_capacity();
    size_t curr_tex_id = 0;
    std::vector<uint32_t> curr_segments;
    for (size_t i = 0; i < enabled_segments.size(); ++i) {
        uint32_t seg = enabled_segments[i];
        bool new_tex = size_t(seg) > (curr_tex_id + 1) * tex_capacity;
        if (!new_tex)
            curr_segments.push_back(seg - uint32_t(curr_tex_id * tex_capacity));
        if (i + 1 == enabled_segments.size() || new_tex) {
            auto [w, h] = width_height(curr_segments.size(), m_max_texture_size);

            assert(m_device != nullptr);
            Render::Texture* tex =
                m_device->context().texture_manager()
                    .create_empty(format("libvgcode_segments_%d", i), Render::PixelFormat::R32UI, w, h);

            tex->set_filtering(Render::TextureMinFilter::Nearest, Render::TextureMagFilter::Nearest);
            if (curr_segments.size() >= tex_capacity) {
                tex->set_data(Render::PixelFormat::R32UI, 0, w, h, curr_segments.data());
                m_tex_ids[curr_tex_id].enabled_segments.second = w * h;
            }
            else {
                tex->set_data(Render::PixelFormat::R32UI, 0, w, h, nullptr);
                if (h == 1) {
                    tex->set_data(Render::PixelFormat::R32UI, 0, w, 1, curr_segments.data());
                    m_tex_ids[curr_tex_id].enabled_segments.second = w;
                }
                else {
                    // the last row is only partially fitted with data, send it separately
                    tex->set_sub_data(Render::PixelFormat::R32UI, 0, 0, 0, w, h - 1, curr_segments.data());
                    tex->set_sub_data(Render::PixelFormat::R32UI, 0, 0, h - 1, curr_segments.size() % w, 1, &curr_segments[w * (h - 1)]);
                    m_tex_ids[curr_tex_id].enabled_segments.second = w * (h - 1) + curr_segments.size() % w;
                }
            }
            m_tex_ids[curr_tex_id].enabled_segments.first = tex;
            if (new_tex) {
                curr_segments.clear();
                ++curr_tex_id;
                curr_segments.push_back(seg - uint32_t(curr_tex_id * tex_capacity));
            }
        }
    }
}

void ViewerImpl::TextureData::set_enabled_options(const std::vector<uint32_t>& enabled_options)
{
    if (m_count == 0)
        return;

    if (enabled_options.empty())
        return;

    size_t tex_capacity = max_texture_capacity();
    size_t curr_tex_id = 0;
    std::vector<uint32_t> curr_options;
    for (size_t i = 0; i < enabled_options.size(); ++i) {
        uint32_t opt = enabled_options[i];
        bool new_tex = size_t(opt) > (curr_tex_id + 1) * tex_capacity;
        if (!new_tex)
            curr_options.push_back(opt - uint32_t(curr_tex_id * tex_capacity));
        if (i + 1 == enabled_options.size() || new_tex) {
            auto [w, h] = width_height(curr_options.size(), m_max_texture_size);

            assert(m_device != nullptr);
            Render::Texture* tex =
                m_device->context().texture_manager()
                    .create_empty(format("libvgcode_options_%d", i), Render::PixelFormat::R32UI, w, h);

            tex->set_filtering(Render::TextureMinFilter::Nearest, Render::TextureMagFilter::Nearest);
            if (curr_options.size() >= tex_capacity){
                tex->set_data(Render::PixelFormat::R32UI, 0, w, h, curr_options.data());
                m_tex_ids[curr_tex_id].enabled_options.second = w * h;
            }
            else {
                tex->set_data(Render::PixelFormat::R32UI, 0, w, h, nullptr);
                if (h == 1) {
                    tex->set_data(Render::PixelFormat::R32UI, 0, w, 1, curr_options.data());
                    m_tex_ids[curr_tex_id].enabled_options.second = w;
                }
                else {
                    // the last row is only partially fitted with data, send it separately
                    tex->set_sub_data(Render::PixelFormat::R32UI, 0, 0, 0, w, h - 1, curr_options.data());
                    tex->set_sub_data(Render::PixelFormat::R32UI, 0, 0, h - 1, curr_options.size() % w, 1, &curr_options[w * (h - 1)]);
                    m_tex_ids[curr_tex_id].enabled_options.second = w * (h - 1) + curr_options.size() % w;
                }
            }
            m_tex_ids[curr_tex_id].enabled_options.first = tex;
            if (new_tex) {
                curr_options.clear();
                ++curr_tex_id;
                curr_options.push_back(opt - uint32_t(curr_tex_id * tex_capacity));
            }
        }
    }
}

void ViewerImpl::TextureData::reset()
{
    m_tex_ids.clear();

    m_width = 0;
    m_height = 0;
    m_count = 0;
}

std::pair<Render::Texture*, size_t> ViewerImpl::TextureData::positions_tex(size_t id) const
{
    assert(id < m_tex_ids.size());
    return m_tex_ids[id].positions;
}

std::pair<Render::Texture*, size_t> ViewerImpl::TextureData::heights_widths_angles_tex(size_t id) const
{
    assert(id < m_tex_ids.size());
    return m_tex_ids[id].heights_widths_angles;
}

std::pair<Render::Texture*, size_t> ViewerImpl::TextureData::colors_tex(size_t id) const
{
    assert(id < m_tex_ids.size());
    return m_tex_ids[id].colors;
}

std::pair<Render::Texture*, size_t> ViewerImpl::TextureData::enabled_segments_tex(size_t id) const
{
    assert(id < m_tex_ids.size());
    return m_tex_ids[id].enabled_segments;
}

std::pair<Render::Texture*, size_t> ViewerImpl::TextureData::enabled_options_tex(size_t id) const
{
    assert(id < m_tex_ids.size());
    return m_tex_ids[id].enabled_options;
}
#endif //!USE_TEXTURE_BUFFER

ViewerImpl::ViewerImpl()
{
    reset_default_extrusion_roles_colors();
    reset_default_options_colors();
}

void ViewerImpl::init(Render::Device& device, Scene::Scene& scene, Scene::GeometryDataFactory& data_factory)
{
    if (m_initialized)
        return;

    m_device = &device;
    m_scene = &scene;
    Scene::NodeBuilder builder{ *m_scene };
    builder.set_debug_name("gcode_main");
    builder.set_tag(GCodeNodeTag{ GCodeElementType::Undefined });

    builder.child([&](Scene::NodeBuilder& bldr) {
        m_cog_marker.init(*m_device, bldr, data_factory);
    });
    builder.child([&](Scene::NodeBuilder& bldr) {
        m_tool_marker.init(*m_device, bldr, data_factory);
    });
    builder.child([&](Scene::NodeBuilder& bldr) {
        m_segment_template.init(*m_device, bldr);
    });
    builder.child([&](Scene::NodeBuilder& bldr) {
        m_option_template.init(*m_device, bldr, data_factory);
    });

    auto main_node = builder.build();
    m_scene->add_child(main_node.release(), &m_scene->root());

    m_initialized = true;
}

void ViewerImpl::reset()
{
    m_layers.reset();
    m_view_range.reset();
    m_extrusion_roles.reset();
    m_options.clear();
    m_used_extruders.reset();
    m_total_time = {};
    m_time_modes.clear();
    m_options_times.clear();
    m_gcode_events.clear();
    m_vertices.clear();
    m_vertices_colors.clear();
    m_valid_lines_bitset.clear();
    m_cog_marker.reset();

#if USE_TEXTURE_BUFFER
    if (m_positions_buffer != nullptr) m_positions_buffer->set_data(nullptr, 1, Render::BufferUsage::StaticDraw);
    if (m_heights_widths_angles_buffer != nullptr) m_heights_widths_angles_buffer->set_data(nullptr, 1, Render::BufferUsage::StaticDraw);
    if (m_colors_buffer != nullptr) m_colors_buffer->set_data(nullptr, 1, Render::BufferUsage::StaticDraw);
    if (m_enabled_segments_buffer != nullptr) m_enabled_segments_buffer->set_data(nullptr, 1, Render::BufferUsage::StaticDraw);
    if (m_enabled_options_buffer != nullptr) m_enabled_options_buffer->set_data(nullptr, 1, Render::BufferUsage::StaticDraw);
    m_ranges_settings = std::nullopt;
#else
    m_texture_data.reset();
#endif // USE_TEXTURE_BUFFER

    m_enabled_segments_count = 0;
    m_enabled_options_count = 0;

    set_view_type(ViewType::FeatureType);
}

static void extract_pos_and_or_hwa(const MoveVertices& vertices, float travels_radius, float wipes_radius, BitSet<>& valid_lines_bitset,
    std::vector<Vec4f>* positions = nullptr, std::vector<Vec4f>* heights_widths_angles = nullptr, bool update_bitset = false) {
    if (positions == nullptr && heights_widths_angles == nullptr)
        return;
    if (vertices.empty())
        return;
    if (travels_radius <= 0.0f || wipes_radius <= 0.0f)
        return;

    if (positions != nullptr)
        positions->reserve(vertices.size());
    if (heights_widths_angles != nullptr)
        heights_widths_angles->reserve(vertices.size());
    for (size_t i = 0; i < vertices.size(); ++i) {
        const MoveVertex& v = vertices[i];
        MoveType move_type = v.type;
        bool prev_line_valid = i > 0 && valid_lines_bitset[i - 1];
        Vec3f prev_line = prev_line_valid ? v.position - vertices[i - 1].position : (Vec3f)Vec3f::Zero();
        bool this_line_valid = i + 1 < vertices.size() &&
                               vertices[i + 1].position != v.position &&
                               vertices[i + 1].type == move_type &&
                               move_type != MoveType::Seam;
        Vec3f this_line = this_line_valid ? vertices[i + 1].position - v.position : (Vec3f)Vec3f::Zero();

        if (this_line_valid) {
            // there is a valid path between point i and i+1.
        }
        else {
            // the connection is invalid, there should be no line rendered, ever
            if (update_bitset)
                valid_lines_bitset.reset(i);
        }
        
        if (positions != nullptr) {
            // the last component is a dummy float to comply with GL_RGBA32F format
            Vec4f position = { v.position.x(), v.position.y(), v.position.z(), 0.0f };
            if (move_type == MoveType::Extrude)
                // push down extrusion vertices by half height to render them at the right z
                position.z() -= 0.5f * v.height;
            positions->emplace_back(position);
        }

        if (heights_widths_angles != nullptr) {
            float height = 0.0f;
            float width = 0.0f;
            if (v.is_travel()) {
                height = travels_radius;
                width  = travels_radius;
            }
            else if (v.is_wipe()) {
                height = wipes_radius;
                width  = wipes_radius;
            }
            else {
                height = v.height;
                width = v.width;
            }
            // the last component is a dummy float to comply with GL_RGBA32F format
            heights_widths_angles->push_back({ height, width,
                std::atan2(prev_line.x() * this_line.y() - prev_line.y() * this_line.x(), prev_line.dot(this_line)), 0.0f});
        }
    }
}

void ViewerImpl::load(ViewerInputData&& gcode_data)
{
    if (!m_initialized)
        return;

    if (gcode_data.vertices.empty())
        return;

    reset();

    m_vertices = std::move(gcode_data.vertices);
    m_tool_colors = std::move(gcode_data.tools_colors);
    m_color_print_colors = std::move(gcode_data.color_print_colors);
    m_gcode_events = std::move(gcode_data.gcode_events);
    m_vertices_colors.resize(m_vertices.size());
    m_settings.spiral_vase_enabled = gcode_data.spiral_vase_enabled;
    m_extruders_count = gcode_data.extruders_count;

    for (const auto& [role, values] : gcode_data.used_filament_by_roles) {
        m_extrusion_roles.add(role, values);
    }

    for (const auto& [role, values] : gcode_data.used_filament_by_extruders) {
        m_used_extruders.add(role, values);
    }

    for (size_t i = 0; i < m_vertices.size(); ++i) {
        const MoveVertex& v = m_vertices[i];
        OptionType option_type = move_type_to_option(v.type);

        m_layers.update(v, uint32_t(i));

        for (size_t j = 0; j < TIME_MODES_COUNT; ++j) {
            m_total_time[j] += v.time[j];
            auto it = std::find_if(m_options_times.begin(), m_options_times.end(),
                [option_type](const std::pair<OptionType, Times>& item) {
                  return option_type == item.first; });
            if (it == m_options_times.end() && option_type != OptionType::COUNT) {
                m_options_times.emplace_back() = { option_type, {} };
                it = std::prev(m_options_times.end());
            }
            if (it != m_options_times.end())
                it->second[j] += v.time[j];
        }

        if (option_type != OptionType::COUNT)
            m_options.emplace_back(option_type);

        if (v.type == MoveType::Extrude) {
            m_extrusion_roles.update(v.extrusion_role, v.time);
            m_used_extruders.update(v.extruder_id, { v.extruder_id, v.cp_color_id, v.layer_id, m_total_time });
        }

        if (i > 0) {
            // updates calculation for center of gravity
            if (v.type == MoveType::Extrude &&
                v.extrusion_role != GCodeExtrusionRole::Skirt &&
                v.extrusion_role != GCodeExtrusionRole::SupportMaterial &&
                v.extrusion_role != GCodeExtrusionRole::SupportMaterialInterface &&
                v.extrusion_role != GCodeExtrusionRole::WipeTower &&
                v.extrusion_role != GCodeExtrusionRole::Custom) {
                m_cog_marker.update(0.5f * (v.position + m_vertices[i - 1].position), v.mass);
            }
        }
    }

    for (size_t i = 0; i < TIME_MODES_COUNT; ++i) {
        if (m_total_time[i] > 0.0f)
            m_time_modes.push_back(TimeMode(i));
    }

    if (!m_layers.empty())
        m_layers.set_view_range(0, uint32_t(m_layers.count()) - 1);

    std::sort(m_options.begin(), m_options.end());
    m_options.erase(std::unique(m_options.begin(), m_options.end()), m_options.end());
    m_options.shrink_to_fit();

    // reset segments visibility bitset
    m_valid_lines_bitset = BitSet<>(m_vertices.size());
    m_valid_lines_bitset.setAll();

    if (m_settings.time_mode != TimeMode::Normal && m_total_time[size_t(m_settings.time_mode)] == 0.0f)
        m_settings.time_mode = TimeMode::Normal;

    // buffers to send to gpu
    // the last component is a dummy float to comply with GL_RGBA32F format
    std::vector<Vec4f> positions;
    std::vector<Vec4f> heights_widths_angles;
    positions.reserve(m_vertices.size());
    heights_widths_angles.reserve(m_vertices.size());
    extract_pos_and_or_hwa(m_vertices, m_travels_radius, m_wipes_radius, m_valid_lines_bitset, &positions, &heights_widths_angles, true);

    if (!positions.empty()) {
#if USE_TEXTURE_BUFFER
        // create and fill positions buffer
        m_positions_buffer = m_device->context().texture_buffer_manager().get_or_create_empty("gcode_positions", Render::PixelFormat::RGBA32F);
        m_positions_buffer->set_data(positions.data(), positions.size() * sizeof(Vec4f), Render::BufferUsage::StaticDraw);

        // create and fill height, width and angles buffer
        m_heights_widths_angles_buffer = m_device->context().texture_buffer_manager().get_or_create_empty("gcode_heights_widths_angles", Render::PixelFormat::RGBA32F);
        m_heights_widths_angles_buffer->set_data(heights_widths_angles.data(), heights_widths_angles.size() * sizeof(Vec4f), Render::BufferUsage::DynamicDraw);

        // create (but do not fill) colors buffer (data is set in update_colors())
        m_colors_buffer = m_device->context().texture_buffer_manager().get_or_create_empty("gcode_colors", Render::PixelFormat::R32F);

        // create (but do not fill) enabled segments buffer (data is set in update_enabled_entities())
        m_enabled_segments_buffer = m_device->context().texture_buffer_manager().get_or_create_empty("gcode_enabled_segments", Render::PixelFormat::R32UI);

        // create (but do not fill) enabled options buffer (data is set in update_enabled_entities())
        m_enabled_options_buffer = m_device->context().texture_buffer_manager().get_or_create_empty("gcode_enabled_options", Render::PixelFormat::R32UI);
#else
        m_texture_data.init(m_device, positions.size());
        // create and fill position textures
        m_texture_data.set_positions(positions);
        // create and fill height, width and angle textures
        m_texture_data.set_heights_widths_angles(heights_widths_angles);
#endif // USE_TEXTURE_BUFFER
    }

    update_view_full_range();
    m_view_range.set_visible(m_view_range.enabled());
    update_enabled_entities();
    update_colors();
}

void ViewerImpl::load_as_sla(const std::vector<float>& layers_zs, const std::vector<float>& layers_times)
{
    if (!m_initialized)
        return;

    if (layers_zs.empty() || layers_times.empty())
        return;

    reset();

    assert(layers_zs.size() == layers_times.size());

    for (size_t i = 0; i < layers_zs.size(); ++i) {
        m_layers.update_as_sla(layers_zs[i], layers_times[i]);
    }

    if (!m_layers.empty())
        m_layers.set_view_range(0, uint32_t(m_layers.count()) - 1);
}

void ViewerImpl::update_enabled_entities()
{
    if (m_vertices.empty())
        return;

    std::vector<uint32_t> enabled_segments;
    std::vector<uint32_t> enabled_options;
    Interval range = m_view_range.visible();

    // when top layer only visualization is enabled, we need to render
    // all the toolpaths in the other layers as grayed, so extend the range
    // to contain them
    if (m_settings.top_layer_only_view_range)
        range[0] = m_view_range.full()[0];

    // to show the options at the current tool marker position we need to extend the range by one extra step
    if (m_vertices[range[1]].is_option() && range[1] < uint32_t(m_vertices.size()) - 1)
        ++range[1];

    if (m_settings.spiral_vase_enabled) {
        // when spiral vase mode is enabled and only one layer is shown, extend the range by one step
        const Interval& layers_range = m_layers.view_range();
        if (layers_range[0] > 0 && layers_range[0] == layers_range[1])
            --range[0];
    }

    for (size_t i = range[0]; i < range[1]; ++i) {
        const MoveVertex& v = m_vertices[i];

        if (!m_valid_lines_bitset[i] && !v.is_option())
            continue;
        if (v.is_travel()) {
            if (!m_settings.options_visibility[size_t(OptionType::Travels)])
                continue;
        }
        else if (v.is_wipe()) {
            if (!m_settings.options_visibility[size_t(OptionType::Wipes)])
                continue;
        }
        else if (v.is_option()) {
            if (!m_settings.options_visibility[size_t(move_type_to_option(v.type))])
                continue;
        }
        else if (v.is_extrusion()) {
            if (!m_settings.extrusion_roles_visibility[size_t(v.extrusion_role)])
                continue;
        }
        else
            continue;

        if (v.is_option())
            enabled_options.push_back(uint32_t(i));
        else
            enabled_segments.push_back(uint32_t(i));
    }

    m_enabled_segments_count = enabled_segments.size();
    m_enabled_options_count = enabled_options.size();

#if !USE_TEXTURE_BUFFER
    m_texture_data.set_enabled_segments(enabled_segments);
    m_texture_data.set_enabled_options(enabled_options);
#else
    // update buffer for enabled segments
    assert(m_enabled_segments_buffer != nullptr);
    if (!enabled_segments.empty())
        m_enabled_segments_buffer->set_data(enabled_segments.data(), enabled_segments.size() * sizeof(uint32_t), Render::BufferUsage::StaticDraw);
    else
        // size = 1 is used here because passing 0 let the call glBufferData() to keep the current content
        // of the buffer without cleaaring it
        m_enabled_segments_buffer->set_data(nullptr, 1, Render::BufferUsage::StaticDraw);

    // update gpu buffer for enabled options
    assert(m_enabled_options_buffer != nullptr);
    if (!enabled_options.empty())
        m_enabled_options_buffer->set_data(enabled_options.data(), enabled_options.size() * sizeof(uint32_t), Render::BufferUsage::StaticDraw);
    else
        // size = 1 is used here because passing 0 let the call glBufferData() to keep the current content
        // of the buffer without cleaaring it
        m_enabled_options_buffer->set_data(nullptr, 1, Render::BufferUsage::StaticDraw);
#endif // !USE_TEXTURE_BUFFER

    m_settings.update_enabled_entities = false;
}

static float encoded_color(const ColorRGB& color) {
    int r = int(color.r_uchar());
    int g = int(color.g_uchar());
    int b = int(color.b_uchar());
    int i_color = r << 16 | g << 8 | b;
    return float(i_color);
}

void ViewerImpl::update_colors_texture()
{
#if USE_TEXTURE_BUFFER
    if (m_colors_buffer == nullptr)
        return;
#endif // USE_TEXTURE_BUFFER

    size_t top_layer_id = m_settings.top_layer_only_view_range ? m_layers.view_range()[1] : 0;
    bool color_top_layer_only = m_view_range.full()[1] != m_view_range.visible()[1];

    // Based on current settings and slider position, we might want to render some
    // vertices as dark grey. Use either that or the normal color (from the cache).
    std::vector<float> colors(m_vertices_colors.size());
    assert(colors.size() == m_vertices.size() && m_vertices_colors.size() == m_vertices.size());
    for (size_t i = 0; i < m_vertices.size(); ++i) {
        colors[i] = (color_top_layer_only && m_vertices[i].layer_id < top_layer_id &&
            (!m_settings.spiral_vase_enabled || i != m_view_range.enabled()[0])) ?
            encoded_color(DUMMY_COLOR) : m_vertices_colors[i];
    }
#if USE_TEXTURE_BUFFER
    m_colors_buffer->set_data(colors.data(), colors.size() * sizeof(float), Render::BufferUsage::StaticDraw);
#else
    if (!colors.empty())
        // update gpu buffer for colors
        m_texture_data.set_colors(colors);
#endif // USE_TEXTURE_BUFFER
}

void ViewerImpl::update_colors()
{
    if (m_used_extruders.extruders_count() > 0) {
        // ensure that the number of defined tool colors matches the max id of the used extruders
        size_t max_used_extruder_id = 1 + size_t(m_used_extruders.extruder_max_id());
        size_t tool_colors_size = m_tool_colors.size();
        if (m_tool_colors.size() < max_used_extruder_id) {
            for (size_t i = 0; i < max_used_extruder_id - tool_colors_size; ++i) {
                m_tool_colors.emplace_back(DUMMY_COLOR);
            }
        }
    }

    update_color_ranges();

    // Recalculate "normal" colors of all the vertices for current view settings.
    // If some part of the preview should be rendered in dark grey, it is taken
    // care of in update_colors_texture. That is to avoid the need to recalculate
    // the "normal" color on every slider move.
    for (size_t i = 0; i < m_vertices.size(); ++i){
        m_vertices_colors[i] = encoded_color(vertex_color(m_vertices[i]));
    }

    update_colors_texture();
    m_settings.update_colors = false;
}

void ViewerImpl::render()
{
    render_tool_marker();
    render_cog_marker();

    if (m_layers.empty())
        return;

    if (m_settings.update_view_full_range)
        update_view_full_range();

    if (m_settings.update_enabled_entities)
        update_enabled_entities();

    if (m_settings.update_colors)
        update_colors();

    render_segments(m_scene->camera().position().cast<float>());
    render_options();
}

#if ENABLE_RENDER_TO_TEXTURE
std::vector<uint8_t> ViewerImpl::render_to_texture(uint16_t width, uint16_t height, const Transform3f& view_matrix,
    const Transform3f& projection_matrix, const ColorRGBA& background_color)
{
    std::vector<uint8_t> pixels;
    if (width == 0 || height == 0)
        return pixels;

    pixels.resize(4 * width * height, '\0');

    GLint max_samples = 0;
    glsafe(glGetIntegerv(GL_MAX_SAMPLES, &max_samples));
    const GLsizei num_samples = max_samples / 2;

    bool old_multisample = false;
    if (num_samples > 1) {
        old_multisample = glIsEnabled(GL_MULTISAMPLE);
        glcheck();
        if (!old_multisample)
            glsafe(glEnable(GL_MULTISAMPLE));
    }

#ifndef NDEBUG
    GLint old_draw_framebuffer = 0;
    glsafe(glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &old_draw_framebuffer));
    GLint old_read_framebuffer = 0;
    glsafe(glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &old_read_framebuffer));
    assert(old_draw_framebuffer == 0 && old_read_framebuffer == 0);
#endif // NDEBUG

    GLuint render_fbo = 0;
    glsafe(glGenFramebuffers(1, &render_fbo));
    glsafe(glBindFramebuffer(GL_FRAMEBUFFER, render_fbo));

    GLuint render_tex_buffer = 0;
    // use renderbuffer instead of texture to avoid the need to use glTexImage2DMultisample which is available only since OpenGL 3.2
    glsafe(glGenRenderbuffers(1, &render_tex_buffer));
    glsafe(glBindRenderbuffer(GL_RENDERBUFFER, render_tex_buffer));
    glsafe(glRenderbufferStorageMultisample(GL_RENDERBUFFER, num_samples, GL_RGBA8, width, height));
    glsafe(glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, render_tex_buffer));

    GLuint render_depth;
    glsafe(glGenRenderbuffers(1, &render_depth));
    glsafe(glBindRenderbuffer(GL_RENDERBUFFER, render_depth));
    glsafe(glRenderbufferStorageMultisample(GL_RENDERBUFFER, num_samples, GL_DEPTH_COMPONENT24, width, height));

    glsafe(glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, render_depth));

    GLenum drawBufs[] = { GL_COLOR_ATTACHMENT0 };
    glsafe(glDrawBuffers(1, drawBufs));

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
        glViewport(0, 0, size_t(width), size_t(height));
        glClearColor(background_color.r(), background_color.g(), background_color.b(), background_color.a());
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);

        if (m_settings.update_view_full_range)
            update_view_full_range();

        if (m_settings.update_enabled_entities)
            update_enabled_entities();

        if (m_settings.update_colors)
            update_colors();

        Vec3f camera_position = view_matrix.inverse().translation();
        render_segments(view_matrix, projection_matrix, camera_position);

        GLuint resolve_fbo;
        glsafe(glGenFramebuffers(1, &resolve_fbo));
        glsafe(glBindFramebuffer(GL_FRAMEBUFFER, resolve_fbo));

        GLuint resolve_tex;
        glsafe(glGenTextures(1, &resolve_tex));
        glsafe(glBindTexture(GL_TEXTURE_2D, resolve_tex));
        glsafe(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr));
        glsafe(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
        glsafe(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
        glsafe(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, resolve_tex, 0));

        glsafe(glDrawBuffers(1, drawBufs));

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
            glsafe(glBindFramebuffer(GL_READ_FRAMEBUFFER, render_fbo));
            glsafe(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolve_fbo));
            glsafe(glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_LINEAR));

            glsafe(glBindFramebuffer(GL_READ_FRAMEBUFFER, resolve_fbo));
            glsafe(glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, (void*)pixels.data()));
        }

        glsafe(glDeleteTextures(1, &resolve_tex));
        glsafe(glDeleteFramebuffers(1, &resolve_fbo));
    }

    glsafe(glBindFramebuffer(GL_FRAMEBUFFER, 0));
    if (render_depth != 0)
        glsafe(glDeleteRenderbuffers(1, &render_depth));
    if (render_tex_buffer != 0)
        glsafe(glDeleteRenderbuffers(1, &render_tex_buffer));
    if (render_fbo != 0)
        glsafe(glDeleteFramebuffers(1, &render_fbo));

    if (num_samples > 1 && !old_multisample)
        glsafe(glDisable(GL_MULTISAMPLE));

    return pixels;
}
#endif // ENABLE_RENDER_TO_TEXTURE

void ViewerImpl::set_view_type(ViewType type)
{
    m_settings.view_type = type;
    m_settings.update_colors = true;
}

void ViewerImpl::set_time_mode(TimeMode mode)
{
    m_settings.time_mode = mode;
    m_settings.update_colors = true;
}

void ViewerImpl::set_layers_range(Interval::value_type min, Interval::value_type max)
{
    min = std::clamp<Interval::value_type>(min, 0, m_layers.count() - 1);
    max = std::clamp<Interval::value_type>(max, 0, m_layers.count() - 1);
    m_layers.set_view_range(min, max);
    // force immediate update of the full range
    update_view_full_range();
    m_view_range.set_visible(m_view_range.enabled());
    m_settings.update_enabled_entities = true;
    //m_settings.update_colors = true;
    update_colors_texture();
}

void ViewerImpl::toggle_top_layer_only_view_range()
{
    m_settings.top_layer_only_view_range = !m_settings.top_layer_only_view_range;
    update_view_full_range();
    m_view_range.set_visible(m_view_range.enabled());
    m_settings.update_enabled_entities = true;
    //m_settings.update_colors = true;
    update_colors_texture();
}

BoundingBoxf3 ViewerImpl::bounding_box(const MoveTypes& types) const
{
    BoundingBoxf3 ret;
    for (const MoveVertex& v : m_vertices) {
        if (std::find(types.begin(), types.end(), v.type) != types.end())
            ret.merge(v.position.cast<double>());
    }
    return ret;
}

BoundingBoxf3 ViewerImpl::extrusion_bounding_box(const GCodeExtrusionRoles& roles) const
{
    BoundingBoxf3 ret;
    for (const MoveVertex& v : m_vertices) {
        if (v.is_extrusion() && std::find(roles.begin(), roles.end(), v.extrusion_role) != roles.end())
            ret.merge(v.position.cast<double>());
    }
    return ret;
}

bool ViewerImpl::is_option_visible(OptionType type) const
{
    return m_settings.options_visibility[size_t(type)];
}

void ViewerImpl::toggle_option_visibility(OptionType type)
{
    if (type != OptionType::CenterOfGravity && type != OptionType::ToolMarker) {
        auto opt_it = std::find(m_options.begin(), m_options.end(), type);
        if (opt_it == m_options.end())
            return;
    }

    m_settings.options_visibility[size_t(type)] = ! m_settings.options_visibility[size_t(type)];
    Interval old_enabled_range = m_view_range.enabled();
    update_view_full_range();
    const Interval& new_enabled_range = m_view_range.enabled();
    if (old_enabled_range != new_enabled_range) {
        const Interval& visible_range = m_view_range.visible();
        if (old_enabled_range == visible_range)
            m_view_range.set_visible(new_enabled_range);
        else if (m_settings.top_layer_only_view_range && new_enabled_range[0] < visible_range[0])
            m_view_range.set_visible(new_enabled_range[0], visible_range[1]);
    }
    m_settings.update_enabled_entities = true;
    m_settings.update_colors = true;

    if (type == OptionType::CenterOfGravity) {
        Scene::Node* node = m_scene->root().query_first([](const Scene::Node* n) {
            const GCodeNodeTag* tag = n->tag_of_type<GCodeNodeTag>();
            return tag != nullptr && tag->type == GCodeElementType::CogMarker;
        }, true);
        assert(node != nullptr);
        node->set_enabled(m_settings.options_visibility[size_t(type)]);
    }

    if (type == OptionType::ToolMarker) {
        Scene::Node* node = m_scene->root().query_first([](const Scene::Node* n) {
            const GCodeNodeTag* tag = n->tag_of_type<GCodeNodeTag>();
            return tag != nullptr && tag->type == GCodeElementType::ToolMarker;
        }, true);
        assert(node != nullptr);
        node->set_enabled(m_settings.options_visibility[size_t(type)]);
    }
}

bool ViewerImpl::is_extrusion_role_visible(GCodeExtrusionRole role) const
{
    return m_settings.extrusion_roles_visibility[size_t(role)];
}

void ViewerImpl::toggle_extrusion_role_visibility(GCodeExtrusionRole role)
{
    m_settings.extrusion_roles_visibility[size_t(role)] = ! m_settings.extrusion_roles_visibility[size_t(role)];
    Interval old_enabled_range = m_view_range.enabled();
    update_view_full_range();
    const Interval& new_enabled_range = m_view_range.enabled();
    if (old_enabled_range != new_enabled_range) {
        const Interval& visible_range = m_view_range.visible();
        if (old_enabled_range == visible_range)
            m_view_range.set_visible(new_enabled_range);
        else if (m_settings.top_layer_only_view_range && new_enabled_range[0] < visible_range[0])
            m_view_range.set_visible(new_enabled_range[0], visible_range[1]);
    }
    m_settings.update_enabled_entities = true;
    m_settings.update_colors = true;
}

void ViewerImpl::set_view_visible_range(Interval::value_type min, Interval::value_type max)
{
    // force update of the full range, to avoid clamping the visible range with full old values
    // when calling m_view_range.set_visible()
    update_view_full_range();
    m_view_range.set_visible(min, max);
    update_enabled_entities();
    //m_settings.update_colors = true;
    update_colors_texture();

    if (is_option_visible(OptionType::ToolMarker)){
        Scene::Node* node = m_scene->root().query_first([](const Scene::Node* n)->bool {
            const GCodeNodeTag* tag = n->tag_of_type<GCodeNodeTag>();
            return tag != nullptr && tag->type == GCodeElementType::ToolMarker;
        }, true);

        assert(node != nullptr);
        node->set_enabled(m_view_range.visible()[1] != m_view_range.enabled()[1]);
    }
}

void ViewerImpl::set_lights(const Lights& lights)
{
    m_lights.clear();
    size_t num_lights = std::min(lights.size(), MAX_NUM_LIGHTS);
    m_lights.reserve(num_lights);
    for (size_t i = 0; i < num_lights; ++i) {
        Light light = lights[i];
        light.direction = light.direction.normalized();
        light.ambient = std::max(light.ambient, 0.0f);
        light.diffuse = std::max(light.diffuse, 0.0f);
        light.specular = std::max(light.specular, 0.0f);
        light.shininess = std::max(light.shininess, 0.0f);

        // specular and shininess cannot be both zero, see: https://registry.khronos.org/OpenGL-Refpages/gl4/html/pow.xhtml
        if (light.specular == 0.0f && light.shininess == 0.0f)
            light.shininess = 0.001f;

        m_lights.emplace_back(light);
    }
}

static const Lights DEFAULT_LIGHTS = {
    { LightReferenceSystem::Eye, { -0.4574957f, 0.4574957f, 0.7624929f }, 0.45f, 0.48f, 0.075f, 20.0f },
    { LightReferenceSystem::Eye, { 0.70014f, 0.140028f, 0.70014f }, 0.0f, 0.18f, 0.0f, 0.0f }
};

const Lights& ViewerImpl::default_lights() const
{
    return DEFAULT_LIGHTS;
}

float ViewerImpl::estimated_time_at(size_t id) const
{
    return std::accumulate(m_vertices.begin(), m_vertices.begin() + id + 1, 0.0f,
        [this](float a, const MoveVertex& v) { return a + v.time[size_t(m_settings.time_mode)]; });
}

size_t ViewerImpl::visible_extrusion_roles_count() const
{
    return visible_extrusion_roles().size();
}

GCodeExtrusionRoles ViewerImpl::visible_extrusion_roles() const
{
    GCodeExtrusionRoles ret;
    GCodeExtrusionRoles roles = extrusion_roles();
    for (GCodeExtrusionRole role : roles) {
        if (is_extrusion_role_visible(role))
            ret.emplace_back(role);
    }
    return ret;
}

size_t ViewerImpl::visible_options_count() const
{
    return visible_options().size();
}

OptionTypes ViewerImpl::visible_options() const
{
    OptionTypes ret;
    for (OptionType option : options()) {
        if (is_option_visible(option))
            ret.emplace_back(option);
    }
    return ret;
}

float ViewerImpl::option_estimated_time(OptionType type) const
{
    auto it = std::find_if(m_options_times.begin(), m_options_times.end(),
      [type](const std::pair<OptionType, Times>& item) {
        return type == item.first; });

    return (it == m_options_times.end()) ? 0.0f : it->second[size_t(m_settings.time_mode)];
}

ColorRGB ViewerImpl::vertex_color(const MoveVertex& v) const
{
    if (v.type == MoveType::Noop)
        return DUMMY_COLOR;

    if ((v.is_wipe() && (m_settings.view_type != ViewType::Speed && m_settings.view_type != ViewType::ActualSpeed)) || v.is_option())
        return option_color(move_type_to_option(v.type));

    switch (m_settings.view_type)
    {
    case ViewType::FeatureType:
    {
        return v.is_travel() ? option_color(move_type_to_option(v.type)) : extrusion_role_color(v.extrusion_role);
    }
    case ViewType::Height:
    {
        return v.is_travel() ? option_color(move_type_to_option(v.type)) : m_height_range.color_at(v.height);
    }
    case ViewType::Width:
    {
        return v.is_travel() ? option_color(move_type_to_option(v.type)) : m_width_range.color_at(v.width);
    }
    case ViewType::Speed:
    {
        return m_speed_range.color_at(v.feedrate);
    }
    case ViewType::ActualSpeed:
    {
        return m_actual_speed_range.color_at(v.actual_feedrate);
    }
    case ViewType::FanSpeed:
    {
        return v.is_travel() ? option_color(move_type_to_option(v.type)) : m_fan_speed_range.color_at(v.fan_speed);
    }
    case ViewType::Temperature:
    {
        return v.is_travel() ? option_color(move_type_to_option(v.type)) : m_temperature_range.color_at(v.temperature);
    }
    case ViewType::VolumetricFlowRate:
    {
        return v.is_travel() ? option_color(move_type_to_option(v.type)) : m_volumetric_rate_range.color_at(v.volumetric_rate());
    }
    case ViewType::ActualVolumetricFlowRate:
    {
        return v.is_travel() ? option_color(move_type_to_option(v.type)) : m_actual_volumetric_rate_range.color_at(v.actual_volumetric_rate());
    }
    case ViewType::LayerTimeLinear:
    {
        return v.is_travel() ? option_color(move_type_to_option(v.type)) :
            m_layer_time_range[0].color_at(m_layers.layer_time(m_settings.time_mode, size_t(v.layer_id)));
    }
    case ViewType::LayerTimeLogarithmic:
    {
        return v.is_travel() ? option_color(move_type_to_option(v.type)) :
            m_layer_time_range[1].color_at(m_layers.layer_time(m_settings.time_mode, size_t(v.layer_id)));
    }
    case ViewType::Tool:
    {
        assert(size_t(v.extruder_id) < m_tool_colors.size());
        return m_tool_colors[v.extruder_id];
    }
    case ViewType::ColorPrint:
    {
        return m_layers.layer_contains_colorprint_options(size_t(v.layer_id)) ? DUMMY_COLOR :
            m_color_print_colors[size_t(v.cp_color_id) % m_color_print_colors.size()];
    }
    default: { break; }
    }

    return DUMMY_COLOR;
}

void ViewerImpl::set_tool_colors(const Palette& colors)
{
    m_tool_colors = colors;
    m_settings.update_colors = true;
}

void ViewerImpl::set_color_print_colors(const Palette& colors)
{
    m_color_print_colors = colors;
    m_settings.update_colors = true;
}

const ColorRGB& ViewerImpl::extrusion_role_color(GCodeExtrusionRole role) const
{
    return m_extrusion_roles_colors[size_t(role)];
}

void ViewerImpl::set_extrusion_role_color(GCodeExtrusionRole role, const ColorRGB& color)
{
    m_extrusion_roles_colors[size_t(role)] = color;
    m_settings.update_colors = true;
}

void ViewerImpl::reset_default_extrusion_roles_colors()
{
    m_extrusion_roles_colors = DEFAULT_EXTRUSION_ROLES_COLORS;
}

const ColorRGB& ViewerImpl::option_color(OptionType type) const
{
    return m_options_colors[size_t(type)];
}

void ViewerImpl::set_option_color(OptionType type, const ColorRGB& color)
{
    m_options_colors[size_t(type)] = color;
    m_settings.update_colors = true;
}

void ViewerImpl::reset_default_options_colors()
{
    m_options_colors = DEFAULT_OPTIONS_COLORS;
}

const ColorRange& ViewerImpl::color_range(ViewType type) const
{
    switch (type)
    {
    case ViewType::Height:                   { return m_height_range; }
    case ViewType::Width:                    { return m_width_range; }
    case ViewType::Speed:                    { return m_speed_range; }
    case ViewType::ActualSpeed:              { return m_actual_speed_range; }
    case ViewType::FanSpeed:                 { return m_fan_speed_range; }
    case ViewType::Temperature:              { return m_temperature_range; }
    case ViewType::VolumetricFlowRate:       { return m_volumetric_rate_range; }
    case ViewType::ActualVolumetricFlowRate: { return m_actual_volumetric_rate_range; }
    case ViewType::LayerTimeLinear:          { return m_layer_time_range[0]; }
    case ViewType::LayerTimeLogarithmic:     { return m_layer_time_range[1]; }
    default:                                 { return ColorRange::DUMMY_COLOR_RANGE; }
    }
}

void ViewerImpl::set_color_range_palette(ViewType type, const Palette& palette)
{
    switch (type)
    {
    case ViewType::Height:                   { m_height_range.set_palette(palette);          break; }
    case ViewType::Width:                    { m_width_range.set_palette(palette);           break; }
    case ViewType::Speed:                    { m_speed_range.set_palette(palette);           break; }
    case ViewType::ActualSpeed:              { m_actual_speed_range.set_palette(palette);    break; }
    case ViewType::FanSpeed:                 { m_fan_speed_range.set_palette(palette);       break; }
    case ViewType::Temperature:              { m_temperature_range.set_palette(palette);     break; }
    case ViewType::VolumetricFlowRate:       { m_volumetric_rate_range.set_palette(palette); break; }
    case ViewType::ActualVolumetricFlowRate: { m_actual_volumetric_rate_range.set_palette(palette); break; }
    case ViewType::LayerTimeLinear:          { m_layer_time_range[0].set_palette(palette);   break; }
    case ViewType::LayerTimeLogarithmic:     { m_layer_time_range[1].set_palette(palette);   break; }
    default:                                 { break; }
    }
    m_settings.update_colors = true;
}

void ViewerImpl::set_travels_radius(float radius)
{
    m_travels_radius = std::clamp(radius, MIN_TRAVELS_RADIUS_MM, MAX_TRAVELS_RADIUS_MM);
    update_heights_widths();
}

void ViewerImpl::set_wipes_radius(float radius)
{
    m_wipes_radius = std::clamp(radius, MIN_WIPES_RADIUS_MM, MAX_WIPES_RADIUS_MM);
    update_heights_widths();
}

static bool is_visible(const MoveVertex& v, const Settings& settings)
{
    const OptionType option_type = move_type_to_option(v.type);
    try
    {
        return (option_type == OptionType::COUNT) ?
            (v.type == MoveType::Extrude) ? settings.extrusion_roles_visibility[size_t(v.extrusion_role)] : false :
            settings.options_visibility[size_t(option_type)];
    }
    catch (...)
    {
        return false;
    }
}

BoundingBoxf3 ViewerImpl::tool_marker_bounding_box() const
{
    BoundingBoxf3 ret = m_tool_marker.bounding_box();
    const Vec3f& position = current_vertex().position;
    Vec3f offset = { position.x(), position.y(), position.z() + m_tool_marker.offset_z()};
    ret.merge(offset.cast<double>());
    return ret;
}

bool ViewerImpl::export_toolpaths_to_obj(FILE& obj_file, FILE& mtl_file, const ObjExportParams& params) const
{
    return libvgcode::export_toolpaths_to_obj(obj_file, mtl_file, params, *this);
}

void ViewerImpl::update_view_full_range()
{
    const Interval& layers_range = m_layers.view_range();
    bool travels_visible = m_settings.options_visibility[size_t(OptionType::Travels)];
    bool wipes_visible   = m_settings.options_visibility[size_t(OptionType::Wipes)];

    auto first_it = m_vertices.begin();
    while (first_it != m_vertices.end() &&
           (first_it->layer_id < layers_range[0] || !is_visible(*first_it, m_settings))) {
        ++first_it;
    }

    // If the first vertex is an extrusion, add an extra step to properly detect the first segment
    if (first_it != m_vertices.begin() && first_it != m_vertices.end() && first_it->type == MoveType::Extrude)
        --first_it;

    if (first_it == m_vertices.end())
        m_view_range.set_full(Range());
    else {
        if (travels_visible || wipes_visible) {
            // if the global range starts with a travel/wipe move, extend it to the travel/wipe start
            while (first_it != m_vertices.begin() &&
                   ((travels_visible && first_it->is_travel()) ||
                    (wipes_visible && first_it->is_wipe()))) {
                --first_it;
            }
        }

        auto last_it = first_it;
        while (last_it != m_vertices.end() && last_it->layer_id <= layers_range[1]) {
            ++last_it;
        }
        if (last_it != first_it)
            --last_it;

        // remove disabled trailing options, if any 
        auto rev_first_it = std::make_reverse_iterator(first_it);
        if (rev_first_it != m_vertices.rbegin())
            --rev_first_it;
        auto rev_last_it = std::make_reverse_iterator(last_it);
        if (rev_last_it != m_vertices.rbegin())
            --rev_last_it;

        bool reduced = false;
        while (rev_last_it != rev_first_it && !is_visible(*rev_last_it, m_settings)) {
            ++rev_last_it;
            reduced = true;
        }

        if (reduced && rev_last_it != m_vertices.rend())
            last_it = rev_last_it.base() - 1;

        if (travels_visible || wipes_visible) {
            // if the global range ends with a travel/wipe move, extend it to the travel/wipe end
            while (last_it != m_vertices.end() && last_it + 1 != m_vertices.end() &&
                   ((travels_visible && last_it->is_travel() && (last_it + 1)->is_travel()) ||
                    (wipes_visible && last_it->is_wipe() && (last_it + 1)->is_wipe()))) {
                  ++last_it;
            }
        }

        if (first_it != last_it)
            m_view_range.set_full(std::distance(m_vertices.begin(), first_it), std::distance(m_vertices.begin(), last_it));
        else
            m_view_range.set_full(Range());

        if (m_settings.top_layer_only_view_range) {
            const Interval& full_range = m_view_range.full();
            auto top_first_it = m_vertices.begin() + full_range[0];
            bool shortened = false;
            while (top_first_it != m_vertices.end() && (top_first_it->layer_id < layers_range[1] || !is_visible(*top_first_it, m_settings))) {
                ++top_first_it;
                shortened = true;
            }
            if (shortened)
                --top_first_it;

            // when spiral vase mode is enabled and only one layer is shown, extend the range by one step
            if (m_settings.spiral_vase_enabled && layers_range[0] > 0 && layers_range[0] == layers_range[1])
                --top_first_it;
            m_view_range.set_enabled(std::distance(m_vertices.begin(), top_first_it), full_range[1]);
        }
        else
            m_view_range.set_enabled(m_view_range.full());
    }

    m_settings.update_view_full_range = false;
}

void ViewerImpl::update_color_ranges()
{
    // Color ranges do not need to be recalculated that often. If the following settings are the same
    // as last time, the current ranges are still valid. The recalculation is quite expensive.
    if (m_ranges_settings.has_value() &&
        m_settings.extrusion_roles_visibility == m_ranges_settings->extrusion_roles_visibility &&
        m_settings.options_visibility == m_ranges_settings->options_visibility)
        return;

    m_width_range.reset();
    m_height_range.reset();
    m_speed_range.reset();
    m_actual_speed_range.reset();
    m_fan_speed_range.reset();
    m_temperature_range.reset();
    m_volumetric_rate_range.reset();
    m_actual_volumetric_rate_range.reset();
    m_layer_time_range[0].reset(); // ColorRange::EType::Linear
    m_layer_time_range[1].reset(); // ColorRange::EType::Logarithmic

    for (size_t i = 0; i < m_vertices.size(); i++) {
        const MoveVertex& v = m_vertices[i];
        if (v.is_extrusion()) {
            m_height_range.update(round_to_bin(v.height));
            if (!v.is_custom_gcode() || m_settings.extrusion_roles_visibility[size_t(GCodeExtrusionRole::Custom)]) {
                m_width_range.update(round_to_bin(v.width));
                m_volumetric_rate_range.update(round_to_bin(v.volumetric_rate()));
                m_actual_volumetric_rate_range.update(round_to_bin(v.actual_volumetric_rate()));
            }
            m_fan_speed_range.update(round_to_bin(v.fan_speed));
            m_temperature_range.update(round_to_bin(v.temperature));
        }
        if ((v.is_travel() && m_settings.options_visibility[size_t(OptionType::Travels)]) ||
            (v.is_wipe() && m_settings.options_visibility[size_t(OptionType::Wipes)]) ||
             v.is_extrusion()) {
            m_speed_range.update(v.feedrate);
            m_actual_speed_range.update(v.actual_feedrate);
        }
    }

    std::vector<float> times = m_layers.times(m_settings.time_mode);
    for (size_t i = 0; i < m_layer_time_range.size(); ++i) {
        for (float t : times) {
            m_layer_time_range[i].update(t);
        }
    }

    m_ranges_settings = m_settings;
}

void ViewerImpl::update_heights_widths()
{
#if !USE_TEXTURE_BUFFER
    std::vector<Vec4f> heights_widths_angles;
    heights_widths_angles.reserve(m_vertices.size());
    extract_pos_and_or_hwa(m_vertices, m_travels_radius, m_wipes_radius, m_valid_lines_bitset, nullptr, &heights_widths_angles);
    m_texture_data.set_heights_widths_angles(heights_widths_angles);
#else
    if (m_heights_widths_angles_buffer == nullptr)
        return;

    m_device->bind_buffer(*m_heights_widths_angles_buffer);
    Vec4f* buffer = (Vec4f*)m_device->map_buffer(*m_heights_widths_angles_buffer, Render::BufferAccess::WriteOnly);

    for (size_t i = 0; i < m_vertices.size(); ++i) {
        const MoveVertex& v = m_vertices[i];
        if (v.is_travel()) {
            buffer[i][0] = m_travels_radius;
            buffer[i][1] = m_travels_radius;
        }
        else if (v.is_wipe()) {
            buffer[i][0] = m_wipes_radius;
            buffer[i][1] = m_wipes_radius;
        }
    }

    m_device->unmap_buffer(*m_heights_widths_angles_buffer);
#endif // !USE_TEXTURE_BUFFER
}

static void add_lights_to_material(Render::Material& material, const Lights& lights)
{
    material.set_uniform("num_lights", int(lights.size()));
    for (size_t i = 0; i < lights.size(); ++i) {
        const Light& l = lights[i];
        material
            .set_uniform(format("lights[%d].system", i), int(l.system))
            .set_uniform(format("lights[%d].direction", i), l.direction)
            .set_uniform(format("lights[%d].ambient", i), l.ambient)
            .set_uniform(format("lights[%d].diffuse", i), l.diffuse)
            .set_uniform(format("lights[%d].specular", i), l.specular)
            .set_uniform(format("lights[%d].shininess", i), l.shininess);
    }
}

void ViewerImpl::render_segments(const Vec3f& camera_position)
{
    Scene::Node* node = m_scene->root().query_first([](const Scene::Node* n)->bool {
        const GCodeNodeTag* tag = n->tag_of_type<GCodeNodeTag>();
        return tag != nullptr && tag->type == GCodeElementType::Toolpaths;
    }, true);

    assert(node != nullptr);
    node->set_enabled(m_enabled_segments_count > 0);

    if (m_enabled_segments_count == 0)
        return;

    Render::Material material{};
    material
        .set_shader(m_device->context().shader_manager().shader("segments"));
    add_lights_to_material(material, m_lights);

#if USE_TEXTURE_BUFFER
    material
        .set_uniform("position_tex", POSITION_TEX_ID)
        .set_uniform("height_width_angle_tex", HEIGHT_WIDTH_ANGLE_TEX_ID)
        .set_uniform("color_tex", COLOR_TEX_ID)
        .set_uniform("segment_index_tex", ENABLED_SEGMENTS_TEX_ID)
        .set_uniform("camera_position", camera_position)
        .set_texture_buffer(POSITION_TEX_ID, m_positions_buffer)
        .set_texture_buffer(HEIGHT_WIDTH_ANGLE_TEX_ID, m_heights_widths_angles_buffer)
        .set_texture_buffer(COLOR_TEX_ID, m_colors_buffer)
        .set_texture_buffer(ENABLED_SEGMENTS_TEX_ID, m_enabled_segments_buffer);

    node->set_material_override(material);
    Scene::InstancedMeshRenderNodeComponent* r_comp = dynamic_cast<Scene::InstancedMeshRenderNodeComponent*>(node->render_component());
    r_comp->set_instances_count(m_enabled_segments_count);
#else
    for (size_t i = 0; i < m_texture_data.count(); ++i) {
        auto [es_tex, count] = m_texture_data.enabled_segments_tex(i);
        if (count == 0)
            continue;
        
        material
            .set_uniform("position_tex", POSITION_TEX_ID)
            .set_uniform("height_width_angle_tex", HEIGHT_WIDTH_ANGLE_TEX_ID)
            .set_uniform("color_tex", COLOR_TEX_ID)
            .set_uniform("segment_index_tex", ENABLED_SEGMENTS_TEX_ID)
            .set_uniform("camera_position", camera_position)
            .set_texture(POSITION_TEX_ID, m_texture_data.positions_tex(i).first)
            .set_texture(HEIGHT_WIDTH_ANGLE_TEX_ID, m_texture_data.heights_widths_angles_tex(i).first)
            .set_texture(COLOR_TEX_ID, m_texture_data.colors_tex(i).first)
            .set_texture(ENABLED_SEGMENTS_TEX_ID, es_tex);
        
        node->set_material_override(material);
        Scene::InstancedMeshRenderNodeComponent* r_comp = dynamic_cast<Scene::InstancedMeshRenderNodeComponent*>(node->render_component());
        r_comp->set_instances_count(count);
    }
#endif // USE_TEXTURE_BUFFER
}

void ViewerImpl::render_options()
{
    Scene::Node* node = m_scene->root().query_first([](const Scene::Node* n)->bool {
        const GCodeNodeTag* tag = n->tag_of_type<GCodeNodeTag>();
        return tag != nullptr && tag->type == GCodeElementType::Options;
    }, true);

    assert(node != nullptr);
    node->set_enabled(m_enabled_options_count > 0);

    if (m_enabled_options_count == 0)
        return;

    Render::Material material;
    material
        .set_shader(m_device->context().shader_manager().shader("options"));
    add_lights_to_material(material, m_lights);

#if USE_TEXTURE_BUFFER
    material
        .set_uniform("position_tex", POSITION_TEX_ID)
        .set_uniform("height_width_angle_tex", HEIGHT_WIDTH_ANGLE_TEX_ID)
        .set_uniform("color_tex", COLOR_TEX_ID)
        .set_uniform("segment_index_tex", ENABLED_OPTIONS_TEX_ID)
        .set_texture_buffer(POSITION_TEX_ID, m_positions_buffer)
        .set_texture_buffer(HEIGHT_WIDTH_ANGLE_TEX_ID, m_heights_widths_angles_buffer)
        .set_texture_buffer(COLOR_TEX_ID, m_colors_buffer)
        .set_texture_buffer(ENABLED_OPTIONS_TEX_ID, m_enabled_options_buffer);

    node->set_material_override(material);
    Scene::InstancedMeshRenderNodeComponent* r_comp = dynamic_cast<Scene::InstancedMeshRenderNodeComponent*>(node->render_component());
    r_comp->set_instances_count(m_enabled_options_count);
#else
    for (size_t i = 0; i < m_texture_data.count(); ++i) {
        auto [eo_tex, count] = m_texture_data.enabled_options_tex(i);
        if (count == 0)
            continue;
        
        material
            .set_uniform("position_tex", POSITION_TEX_ID)
            .set_uniform("height_width_angle_tex", HEIGHT_WIDTH_ANGLE_TEX_ID)
            .set_uniform("color_tex", COLOR_TEX_ID)
            .set_uniform("segment_index_tex", ENABLED_OPTIONS_TEX_ID)
            .set_texture(POSITION_TEX_ID, m_texture_data.positions_tex(i).first)
            .set_texture(HEIGHT_WIDTH_ANGLE_TEX_ID, m_texture_data.heights_widths_angles_tex(i).first)
            .set_texture(COLOR_TEX_ID, m_texture_data.colors_tex(i).first)
            .set_texture(ENABLED_OPTIONS_TEX_ID, eo_tex);
        
        node->set_material_override(material);
        Scene::InstancedMeshRenderNodeComponent* r_comp = dynamic_cast<Scene::InstancedMeshRenderNodeComponent*>(node->render_component());
        r_comp->set_instances_count(count);
    }
#endif // USE_TEXTURE_BUFFER
}

void ViewerImpl::render_cog_marker()
{
    Scene::Node* node = m_scene->root().query_first([](const Scene::Node* n)->bool {
        const GCodeNodeTag* tag = n->tag_of_type<GCodeNodeTag>();
        return tag != nullptr && tag->type == GCodeElementType::CogMarker;
    }, true);

    assert(node != nullptr);
    bool enabled = m_cog_marker.total_mass() > 0.0f && m_settings.options_visibility[size_t(OptionType::CenterOfGravity)];
    node->set_enabled(enabled);
    if (!enabled)
        return;

    Render::Material material;
    material
        .set_shader(m_device->context().shader_manager().shader("cog_marker"))
        .set_uniform("world_origin", m_cog_marker.position())
        .set_uniform("scale_factor", m_cog_marker.scale_factor());
    add_lights_to_material(material, m_lights);
    node->set_material_override(material);
}

void ViewerImpl::render_tool_marker()
{
    Scene::Node* node = m_scene->root().query_first([](const Scene::Node* n)->bool {
        const GCodeNodeTag* tag = n->tag_of_type<GCodeNodeTag>();
        return tag != nullptr && tag->type == GCodeElementType::ToolMarker;
    }, true);

    assert(node != nullptr);
    bool enabled = m_tool_marker.enabled() && m_settings.options_visibility[size_t(OptionType::ToolMarker)];
    node->set_enabled(enabled);
    if (!enabled)
        return;

    Vec3f origin = get_current_vertex().position + m_tool_marker.offset_z() * Vec3f::UnitZ();
    ColorRGBA color = to_rgba(m_tool_marker.color(), m_tool_marker.alpha());

    Render::Material material;
    material
        .set_shader(m_device->context().shader_manager().shader("tool_marker"))
        .set_uniform("world_origin", origin)
        .set_uniform("scale_factor", m_tool_marker.scale_factor())
        .set_uniform("color_base", color)
        .set_transparent(color.is_transparent());
    add_lights_to_material(material, m_lights);
    node->set_material_override(material);
}

} // namespace Slic3r::App::libvgcode
