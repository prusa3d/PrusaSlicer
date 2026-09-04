#pragma once

#include "Slic3r/App/Yoga/LayoutButton.hpp"

#include "Slic3r/Biz/DataObserver.hpp"
#include "Slic3r/Biz/Preset/PresetInteractor.hpp"
#include "Slic3r/Biz/Preset/IPresetChangedListener.hpp"
#include "Slic3r/Biz/Platform/ListenerScope.hpp"
#include "Slic3r/Biz/IColorsChangedListener.hpp"
#include "Slic3r/Biz/ProjectSettingsInteractor.hpp"

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::App {
class MaterialSettingsDialog;
} // namespace Slic3r::App

namespace Slic3r::App::Yoga {

class Text;
class ColorPickerButton;
class ButtonGroup;
class LayoutButton;
class AbstractButton;

class MaterialSettingsButton :
    public RectangleButton,
    public Biz::DataObserver<Biz::Preset::PresetItemObservableList>,
    public Biz::IListSelectionChangedListener,
    public Biz::Preset::IPresetChangedListener,
    public Biz::IColorsChangedListener
{
public:
    using FnIndexClicked = std::function<void(size_t)>;
    MaterialSettingsButton(
        size_t index,
        const Biz::Preset::PresetItemObservableList& state,
        std::weak_ptr<ButtonGroup> button_group,
        FnIndexClicked on_cog_clicked,
        FnIndexClicked on_nozzle_clicked,
        Biz::ProjectInteractor& project_interactor
    );
    ~MaterialSettingsButton();

    void on_list_selection_changed(Domain::SelectionId new_selection) override;

    void on_hw_item_selection_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id,
        Biz::Preset::HwItemType type
    ) override;

    void on_colors_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id,
        const std::vector<Domain::ColorRGB>& colors
    ) override;

    void on_view_will_be_removed() override;

    void on_preset_value_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id,
        const Domain::ConfigItem& item
    ) override;
    void on_preset_bundles_loaded() override;

protected:
    void on_data_update() override;
    void checked_updated_internal() override;
    void hovered_updated_internal() override;

    void set_color(const ImColor& color);
    void set_nozzle(const std::string& nozzle);
    void set_material_name(const std::string& name, bool is_modified);
    void refresh_label_color();

private:
    void update_cog_visibility();

private:
    Biz::ListenerScope<
        Biz::IColorsChangedListener,
        Biz::ProjectSettingsInteractor,
        MaterialSettingsButton>
        m_colors_changed_listener_scope;

    ColorPickerButton* m_color_marker{nullptr};
    Text* m_material_name{nullptr};
    LayoutButton* m_cog_btn{nullptr};
    LayoutButton* m_nozzle_btn{nullptr};
    std::weak_ptr<ButtonGroup> m_button_group;
    FnIndexClicked m_on_cog_clicked;
    FnIndexClicked m_on_nozzle_clicked;
    Biz::ProjectInteractor& m_project_interactor;

    std::string m_material_preset_name{};
};

} // namespace Slic3r::App::Yoga
