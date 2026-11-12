#pragma once

#include "Slic3r/App/Yoga/ListView.hpp"
#include "Slic3r/App/Yoga/CircleBorderedButton.hpp"
#include "Slic3r/App/Config/ToolToFeatureExtruderButton.hpp"

#include "Slic3r/Biz/PrintToolItem.hpp"
#include "Slic3r/Biz/IObservableList.hpp"
#include "Slic3r/Biz/IListObserver.hpp"
#include "Slic3r/Biz/Preset/PresetInteractor.hpp"

#include <functional>

namespace Slic3r::Biz {
class ProjectInteractor;
class IConfigBoxSetter;
} // namespace Slic3r::Biz

namespace Slic3r::App {

class ToolToFeatureDialogHeader :
    public Yoga::Item,
    public Biz::IListObserver<Biz::PrintToolItem>
{
public:
    ToolToFeatureDialogHeader(
        Biz::ProjectInteractor& project_interactor,
        Biz::IConfigBoxSetter& cb_setter
    );
    ~ToolToFeatureDialogHeader() override;

    /**
     * @brief The list of options whose checked-button state this header reflects, and
     * whose items are all set to the clicked extruder id when a button is clicked.
     */
    void set_source_list(std::weak_ptr<Biz::IObservableList<Biz::PrintToolItem>> source_list);

    /**
     * @name Biz::IListObserver<Biz::PrintToolItem>
     * @{
     */
    void on_updated(const Biz::IndexRange& index_range) override;
    /** @} */

private:
    using ExtruderButtonListViewFactory = Yoga::ViewFactory<
        ToolToFeatureExtruderButton,
        Biz::Preset::PresetItemObservableList,
        std::function<void(int)>,
        Biz::ProjectInteractor&>;
    using ExtruderButtonListView = Yoga::ListView<
        ToolToFeatureExtruderButton,
        Biz::Preset::PresetItemObservableList,
        ExtruderButtonListViewFactory>;

    void update_checked_buttons();
    void on_button_clicked(int extruder_id);

    Biz::IConfigBoxSetter& m_cb_setter;

    Biz::WeakerPointer<Biz::IObservableList<Biz::PrintToolItem>> m_source_list;

    Yoga::CircleBorderedButton* m_auto_button{nullptr};
    ExtruderButtonListView* m_list_view{nullptr};
};

} // namespace Slic3r::App
