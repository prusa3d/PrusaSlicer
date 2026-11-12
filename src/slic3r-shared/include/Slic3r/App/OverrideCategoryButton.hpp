#pragma once

#include "Slic3r/Domain/ConfigDef.hpp"

#include "Slic3r/Biz/DataObserver.hpp"
#include "Slic3r/Biz/OverrideItem.hpp"

#include "Slic3r/App/Yoga/LayoutButton.hpp"

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::App {

class OverrideCategoryButton : public Biz::DataObserver<Biz::OverrideItem>, public Yoga::LayoutButton
{
public:
    using SelectCategoryFn = std::function<void(Domain::ConfigItemDef::Category category)>;

    explicit OverrideCategoryButton(
        size_t index,
        const Biz::OverrideItem& data,
        Biz::ProjectInteractor& project_interactor,
        SelectCategoryFn& select_category_fn
    );

protected:
    void on_data_update() override;

private:
    Biz::ProjectInteractor& m_project_interactor;
};

} // namespace Slic3r::App
