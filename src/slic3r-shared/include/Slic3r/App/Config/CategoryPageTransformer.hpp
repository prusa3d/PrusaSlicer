#pragma once

#include "Slic3r/Biz/ObservableListTransformer.hpp"
#include <Slic3r/Biz/ConfigItemContext.hpp>
#include "Slic3r/App/PageEntryButton.hpp"

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::App {

class CategoryPageTransformer :
    public Biz::ObservableListTransformer<Biz::ConfigItemContext, PageEntry>
{
public:
    CategoryPageTransformer();
    void set_project_interactor(Biz::ProjectInteractor* project_interactor);

private:
    Biz::ProjectInteractor* m_project_interactor{nullptr};
};

} // namespace Slic3r::App
