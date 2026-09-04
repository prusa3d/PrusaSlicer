#include "Slic3r/App/Preview/SlaViewerWrapper.hpp"

#include "Slic3r/App/Imgui/ImguiExtension.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/libpgcode/Utils.hpp"

#include "Slic3r/Domain/Constants.hpp"

#include <iostream>

using namespace Slic3r::Biz::libpgcode;
using namespace Slic3r::App::libvgcode;
using namespace Slic3r::Biz;

namespace Slic3r::App::Preview {

SlaViewerWrapper::~SlaViewerWrapper() = default;

bool SlaViewerWrapper::init(Render::Device& device, Scene::Scene& scene, Scene::GeometryDataFactory& data_factory)
{
    try {
        m_viewer.init(device, scene, data_factory);
        return true;
    }
    catch (const std::exception& e) {
        std::cout << e.what();
        return false;
    }
}

void SlaViewerWrapper::render_scene()
{
    m_viewer.render();
}

void SlaViewerWrapper::render_imgui() 
{
}

bool SlaViewerWrapper::set_settings(const ViewerWrapperBaseSettings& settings)
{
    m_settings = settings;

    try {
        m_slider_layers = Yoga::Passthrough(std::make_unique<DoubleSliderForLayers>());
        set_layers_slider_base_flags(m_settings.layers_slider_base_flags);

        m_slider_layers->set_draw_mode(true, false);
        // set layers slider callbacks
        m_slider_layers->set_on_thumb_move_callback(std::bind(&SlaViewerWrapper::on_slider_layers_scroll_changed, this));
        m_slider_layers->set_request_extra_frames_callback(m_settings.layers_slider_base_callbacks.request_extra_frames);
        m_slider_layers->set_app_config_changed_callback(m_settings.layers_slider_base_callbacks.app_config_changed);

        return true;
    }
    catch (const std::exception& e) {
        std::cout << e.what();
        return false;
    }
}

void SlaViewerWrapper::reset()
{
    m_viewer.reset();
}

void SlaViewerWrapper::load_from_result(const Biz::Slicing::SLAResult& result, const Scene::Transform& bed_transform)
{
    m_loading = true;

    m_viewer.load(result, bed_transform);
    update_slider_layers();

    m_loading = false;
}

void SlaViewerWrapper::load_from_object(const Biz::Slicing::Sla::Object& object, const Scene::Transform& bed_transform)
{
    m_viewer.load_object(object, bed_transform);
}

void SlaViewerWrapper::reset_result()
{
    m_viewer.reset_layers();
}

void SlaViewerWrapper::reset_object(const Domain::ObjectID& object_id)
{
    m_viewer.reset_object(object_id);
}

// void SlaViewerWrapper::render_legend(Render::ImguiRender* imgui_render)
// {
//     static std::string msg = _u8L("No data available");

//     if (!has_data()) {
//         ImVec2 msg_size = ImGui::CalcTextSize(msg.c_str());
//         ImVec2 available_size = ImGui::GetContentRegionAvail();
//         if (msg_size.x < available_size.x && msg_size.y < available_size.y) {
//             ImVec2 pos = ImGui::GetCurrentWindow()->DC.CursorPos + (available_size - msg_size) * 0.5f;
//             ImGui::RenderText(pos, msg.c_str());
//         }
//     }
//     else {
//         // ToDo Render SLA legend
//     }
// }

void SlaViewerWrapper::set_layers_range(Interval::value_type min, Interval::value_type max)
{
    m_viewer.set_layers_range(min, max);
}

void SlaViewerWrapper::update_slider_layers()
{
    // !!! Code duplication

    // Save the initial slider span.
    float z_low = m_slider_layers->lower_value();
    float z_high = m_slider_layers->higher_value();
    bool was_empty = m_slider_layers->max_pos() == 0;

    std::vector<float> layers_zs = m_viewer.layers_zs();

    bool force_sliders_full_range = was_empty || layers_zs.empty() || std::abs(layers_zs.back() - m_slider_layers->max_value()) > Domain::EPSILON;
    bool snap_to_min = force_sliders_full_range || m_slider_layers->is_lower_at_min();
    bool snap_to_max = force_sliders_full_range || m_slider_layers->is_higher_at_max();

    int max_pos = layers_zs.empty() ? 0 : int(layers_zs.size()) - 1;

    int idx_low = 0;
    int idx_high = max_pos;
    if (!layers_zs.empty()) {
        if (!snap_to_min) {
            int idx_new = DoubleSliderForLayers::find_close_layer_idx(layers_zs, z_low, float(Domain::EPSILON));
            if (idx_new != -1)
                idx_low = idx_new;
        }
        if (!snap_to_max) {
            int idx_new = DoubleSliderForLayers::find_close_layer_idx(layers_zs, z_high, float(Domain::EPSILON));
            if (idx_new != -1)
                idx_high = idx_new;
        }
    }

    m_slider_layers->set_slider_values(std::move(layers_zs));
    m_slider_layers->force_ruler_update();
    assert(m_slider_layers->min_pos() == 0);
    m_slider_layers->freeze();
    m_slider_layers->set_max_pos(max_pos);
    m_slider_layers->set_selection_span(idx_low, idx_high);

    if (!m_data.keep_layers_times)
        m_slider_layers->set_layers_times(m_viewer.layers_estimated_times(), m_viewer.estimated_time());

    m_slider_layers->thaw();
}

void SlaViewerWrapper::on_slider_layers_scroll_changed()
{
    if (m_slider_layers->is_visible()) {
        set_layers_range(uint32_t(m_slider_layers->lower_pos()), uint32_t(m_slider_layers->higher_pos()));
        if (m_settings.layers_slider_base_callbacks.on_thumb_move != nullptr)
            m_settings.layers_slider_base_callbacks.on_thumb_move();
    }
}

} // namespace Slic3r::App::Preview
