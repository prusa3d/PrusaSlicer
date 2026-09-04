#pragma once

#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/App/Yoga/ComboBoxListViewSelection.hpp"
#include "Slic3r/Biz/DataObserver.hpp"
#include "Slic3r/Biz/Preset/PresetInteractor.hpp"
#include "Slic3r/App/Yoga/Validator.hpp"

namespace Slic3r::App {

class PrinterNozzleRow :
    public Biz::DataObserver<Biz::Preset::ToolConfigItemObservableList>,
    public Yoga::Item
{
public:
    struct Callbacks
    {
        std::function<void(bool)> validation_updated{nullptr};
    };

    PrinterNozzleRow(
        size_t index,
        const Biz::Preset::ToolConfigItemObservableList& data,
        Biz::Preset::PresetInteractor& preset_interactor,
        const std::function<void(bool)>& validation_updated = nullptr
    );

    Callbacks& callbacks();

    void on_data_update() override;
    void on_index_update() override;
    void on_view_will_be_reset() override;

private:
    using ComboBoxTools = Yoga::ComboBoxListViewSelection<Domain::Preset::HwToolConfigDef>;

    Biz::Preset::PresetInteractor& m_preset_interactor;
    Yoga::Text* m_text_index   = nullptr;
    ComboBoxTools* m_combo_box = nullptr;
    Callbacks m_callbacks;
};

} // namespace Slic3r::App
