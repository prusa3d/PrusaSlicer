#pragma once

#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/Biz/DataObserver.hpp"
#include "Slic3r/Biz/Preset/PresetInteractor.hpp"
#include "Slic3r/App/Yoga/ComboBoxListViewSelection.hpp"

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::App::Yoga {
class ButtonGroup;
class LayoutButton;
class ButtonGroup;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App {

class PrintSettingsDialog;

class SidebarToolHeadRow :
    public Biz::DataObserver<Biz::Preset::PresetItemObservableList>,
    public Yoga::Item
{
public:
    explicit SidebarToolHeadRow(
        size_t index,
        const Biz::Preset::PresetItemObservableList& data,
        Biz::ProjectInteractor& project_interactor
    );

    void on_view_will_be_reset() override;
    Yoga::ComboBoxListViewSelection<Biz::Preset::PresetItem>* combo_box();

protected:
    void on_data_update() override;

private:
    Biz::ProjectInteractor& m_project_interactor;
    Yoga::ComboBoxListViewSelection<Biz::Preset::PresetItem>* m_combo_box{nullptr};
    Biz::Preset::PresetItemObservableList* m_last_preset_item_observable_list{nullptr};

    int m_last_selected_index{-1};
};

} // namespace Slic3r::App
