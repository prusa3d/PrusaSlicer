#pragma once
#include "Slic3r/App/Yoga/Dialog.hpp"
#include "Slic3r/App/Yoga/ListView.hpp"
#include "Slic3r/App/Config/ExtruderIdButtonGroup.hpp"
#include "Slic3r/App/Config/ToolToFeatureDialogHeader.hpp"

#include "Slic3r/Biz/DataObserver.hpp"
#include "Slic3r/Biz/PrintToolItem.hpp"
#include "Slic3r/Biz/ObservableListSortFilter.hpp"

namespace Slic3r::Biz {
class ProjectInteractor;
class IConfigBoxSetter;
} // namespace Slic3r::Biz

namespace Slic3r::App {

class Navigator;

class ExtruderOptionRow : public Biz::DataObserver<Biz::PrintToolItem>, public Yoga::Item
{
public:
    ExtruderOptionRow(
        size_t index,
        const Biz::PrintToolItem& data,
        Biz::IConfigBoxSetter& cb_setter,
        Biz::ProjectInteractor& project_interactor
    );

protected:
    void on_data_update() override;

private:
    Biz::IConfigBoxSetter& m_cb_setter;

    Yoga::Text* m_name{nullptr};
    ExtruderIdButtonGroup* m_value{nullptr};
};

class ToolToFeatureDialog : public Yoga::Dialog
{
public:
    ToolToFeatureDialog(Biz::ProjectInteractor& project_interactor, Navigator& navigator);

protected:
    void close_action() override;

private:
    using ExtruderAssignmentFilter = Biz::ObservableListSortFilter<Biz::PrintToolItem>;

    using ExtruderOptionListViewFactory = Yoga::ViewFactory<
        ExtruderOptionRow,
        Biz::PrintToolItem,
        Biz::IConfigBoxSetter&,
        Biz::ProjectInteractor&>;
    using ExtruderOptionListView =
        Yoga::ListView<ExtruderOptionRow, Biz::PrintToolItem, ExtruderOptionListViewFactory>;

    Biz::ProjectInteractor& m_project_interactor;
    Navigator& m_navigator;

    ToolToFeatureDialogHeader* m_header{nullptr};

    Biz::UnsharedPointer<ExtruderAssignmentFilter> m_extruder_assignment_filter;
    ExtruderOptionListView* m_extruder_assignment_list_view{nullptr};
};

} // namespace Slic3r::App
