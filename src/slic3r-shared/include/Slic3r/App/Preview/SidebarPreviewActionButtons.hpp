#pragma once

#include "Slic3r/App/SidebarActionButtons.hpp"
#include "Slic3r/Biz/PhysicalPrinter/IPhysicalPrinterChangedListener.hpp"

namespace Slic3r::App::Yoga {
class LayoutButton;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App::Preview {

class SidebarPreviewActionButtons :
    public SidebarActionButtons,
    public Biz::IStatusCacheChangedListener,
    public Biz::ISelectedBedInstancesChangedListener,
    public Biz::RemovableDrive::IRemovableDriveStatusListener
{
public:
    SidebarPreviewActionButtons(Navigator* render_module_navigator);
    ~SidebarPreviewActionButtons();

    void on_init(Biz::ProjectInteractor* project_interactor) override;

    void on_selected_bed_instances_changed(
        Domain::SelectionId project_id,
        const Biz::Scene::BedSelection& bed_selection
    ) override;
    void on_status_cache_status_code_changed(const Domain::SlicingId slicing_id) override;

    void on_removable_drive_status_changed(
        const boost::filesystem::path&,
        Biz::RemovableDrive::RemovableDriveStatus
    ) override;

protected:
    void update_buttons() override;

private:
    Yoga::LayoutButton* m_primary_button{nullptr};
    Yoga::LayoutButton* m_navigation_button{nullptr};

    std::unique_ptr<Yoga::LayoutButton> get_primary_button();
};
} // namespace Slic3r::App::Preview