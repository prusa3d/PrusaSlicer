#pragma once

#include <functional>

#include <wx/dnd.h>

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::App {
class Navigator;
} // namespace Slic3r::App

namespace Slic3r::App::Desktop {

/**
 * wxWidgets adapter that receives OS file drop events and routes them to the
 * appropriate ProjectInteractor method:
 *   - single project file (.3mf) → load_project()
 *   - everything else (model files)   → load_models_to_project()
 * Unsupported file types are silently filtered out.
 *
 * @param can_accept Predicate called on each drop; returning false ignores the
 *                   drop entirely (e.g. when a non-slicing tab is active).
 */
class MainFrameDropTarget : public wxFileDropTarget
{
public:
    MainFrameDropTarget(
        Biz::ProjectInteractor& project_interactor,
        Navigator& navigator,
        std::function<bool()> can_accept
    );
    bool OnDropFiles(wxCoord x, wxCoord y, const wxArrayString& filenames) override;

private:
    Biz::ProjectInteractor& m_project_interactor;
    Navigator& m_navigator;
    std::function<bool()> m_can_accept;
};

} // namespace Slic3r::App::Desktop
