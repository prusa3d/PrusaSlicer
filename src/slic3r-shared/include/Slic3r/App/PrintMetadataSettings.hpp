#pragma once

#include "Slic3r/Biz/DataObserver.hpp"
#include "Slic3r/Biz/Preset/PresetInteractor.hpp"

#include "Slic3r/App/Yoga/Item.hpp"

namespace Slic3r::Biz::Preset {
class PresetInteractor;
} // namespace Slic3r::Biz::Preset

namespace Slic3r::App::Yoga {
class InputTextField;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App {

class PrintMetadataSettings :
    public Biz::DataObserver<Biz::Preset::ToolConfigItemObservableList>,
    public Biz::Preset::IPresetChangedListener,
    public Yoga::Item
{
public:
    PrintMetadataSettings(
        size_t index,
        const Biz::Preset::ToolConfigItemObservableList& data,
        Biz::Preset::PresetInteractor& preset_interactor
    );

    void on_preset_selection_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id,
        Biz::Preset::PresetItemType type
    ) override;

protected:
    void on_data_update() override;
    void on_index_update() override;

    void add_new_row(const std::string& label, Yoga::ItemPtr control);

    void update_contents();

private:
    Biz::ListenerScope<
        Biz::Preset::IPresetChangedListener,
        Biz::Preset::PresetInteractor,
        PrintMetadataSettings>
        m_preset_changed_listener_scope;

    Biz::Preset::PresetInteractor& m_preset_interactor;

    Yoga::InputTextField* m_input_id{nullptr};
    Yoga::InputTextField* m_input_name{nullptr};
    // Yoga::InputTextField* m_input_expression{nullptr};
};

} // namespace Slic3r::App
