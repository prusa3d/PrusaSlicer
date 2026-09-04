#pragma once

#include "Slic3r/App/Yoga/Dialog.hpp"
#include "Slic3r/Biz/StatusCache.hpp"
#include "Slic3r/Biz/ISelectedBedInstanceChangedListener.hpp"
#include "Slic3r/Biz/ISelectedProjectChangedListener.hpp"

namespace Slic3r::Biz {
class ProjectInteractor;
}

namespace Slic3r::App {

class Navigator;

namespace Yoga {
class Text;
}

class InvalidDataError;

class InvalidDataDialog :
    public Yoga::Dialog,
    public Biz::IStatusCacheChangedListener,
    public Biz::ISelectedBedInstancesChangedListener,
    public Biz::ISelectedProjectChangedListener
{
public:
    InvalidDataDialog(Biz::ProjectInteractor& project_interactor, Navigator& navigator);
    ~InvalidDataDialog();

    void on_status_cache_status_code_changed(const Domain::SlicingId id) override;
    void on_status_cache_errors_changed(const Domain::SlicingId id) override;
    void on_selected_bed_instances_changed(
        Domain::SelectionId project_id,
        const Biz::Scene::BedSelection& bed_selection
    ) override;
    void on_selected_project_changed(size_t index) override;

protected:
    void close_action() override;

private:
    void reload();

private:
    Biz::ProjectInteractor& m_project_interactor;
    Navigator& m_navigator;
    Yoga::Item* m_errors;
};

} // namespace Slic3r::App
