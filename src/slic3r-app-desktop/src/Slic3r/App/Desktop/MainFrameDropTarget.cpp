#include "MainFrameDropTarget.hpp"

#include <boost/filesystem/path.hpp>
#include "Slic3r/App/Navigator.hpp"
#include <Slic3r/App/WX/StringConversions.hpp>

#include "Slic3r/Biz/ProjectInteractor.hpp"
#include <Slic3r/Biz/FileLoadingLogic.hpp>

namespace Slic3r::App::Desktop {

MainFrameDropTarget::MainFrameDropTarget(
    Biz::ProjectInteractor& project_interactor,
    Navigator& navigator,
    std::function<bool()> can_accept
) :
    m_project_interactor(project_interactor),
    m_navigator(navigator),
    m_can_accept(std::move(can_accept))
{}

bool MainFrameDropTarget::OnDropFiles(wxCoord /*x*/, wxCoord /*y*/, const wxArrayString& filenames)
{
    if (!m_can_accept())
        return false;

    std::vector<boost::filesystem::path> paths;
    paths.reserve(filenames.size());
    for (const wxString& fn : filenames) {
        boost::filesystem::path p{WX::into_u8(fn)};
        if (Biz::FileLoadingLogic::is_supported_file(p.string()))
            paths.push_back(std::move(p));
    }

    if (paths.empty())
        return false;

    if (paths.size() == 1 && Biz::FileLoadingLogic::is_project_file(paths.front().string()))
        m_project_interactor.load_project(paths.front());
    else {
        m_project_interactor.load_models_to_project(std::move(paths));
        m_navigator.navigate_to_module_type(App::Render::ModuleType::Plater);
    }

    return true;
}

} // namespace Slic3r::App::Desktop
