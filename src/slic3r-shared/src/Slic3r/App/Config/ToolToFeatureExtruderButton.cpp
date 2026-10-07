#include "Slic3r/App/Config/ToolToFeatureExtruderButton.hpp"

#include "Slic3r/Biz/ProjectInteractor.hpp"

namespace Slic3r::App {

using namespace Yoga;

ToolToFeatureExtruderButton::ToolToFeatureExtruderButton(
    size_t index,
    const Biz::Preset::PresetItemObservableList& data,
    std::function<void(int extruder_id)> on_clicked,
    Biz::ProjectInteractor& project_interactor
) :
    Biz::DataObserver<Biz::Preset::PresetItemObservableList>(index, data),
    m_project_interactor(project_interactor),
    m_colors_changed_listener_scope(project_interactor.project_settings_interactor(), *this)
{
    m_icon = emplace_back<ExtruderIcon>();
    m_icon->set_extruder_index(extruder_id());
    m_icon->set_width(20_fpx);

    update_color(project_interactor.project_settings_interactor().get_colors(
        project_interactor.selected_project_id(),
        project_interactor.selected_config_container_id()
    ));

    callbacks().action = [this, on_clicked]()
    {
        if (on_clicked) {
            on_clicked(extruder_id());
        }
        this->set_checked(true);
    };
}

void ToolToFeatureExtruderButton::on_colors_changed(
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id,
    const std::vector<Domain::ColorRGB>& colors
)
{
    if (project_id != m_project_interactor.selected_project_id()
        || config_container_id != m_project_interactor.selected_config_container_id())
    {
        return;
    }

    update_color(colors);
}

void ToolToFeatureExtruderButton::update_color(const std::vector<Domain::ColorRGB>& colors)
{
    if (m_index >= colors.size()) {
        return;
    }

    const Domain::ColorRGB& color = colors[m_index];
    m_icon->set_extruder_color({color.r(), color.g(), color.b()});
}

} // namespace Slic3r::App
