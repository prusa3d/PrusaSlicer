#include "Slic3r/App/Config/CategoryPageTransformer.hpp"
#include "Slic3r/App/Config/CategoryUtils.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

namespace Slic3r::App {

CategoryPageTransformer::CategoryPageTransformer()
{
    set_transform_fn(
        [this](const Biz::ConfigItemContext& data, size_t index)
        {
            const Domain::ConfigItemDef::Category category = data.config_item->def().category;

            Domain::PrinterTechnology pt = m_project_interactor ?
                m_project_interactor->selected_config_container().print_technology() :
                Domain::PrinterTechnology::FFF;
            Render::Icon icon            = CategoryUtils::category_render_icon(category, pt);

            return PageEntry{
                Biz::_u8(Domain::ConfigItemDef::translate_category(category, pt)),
                icon
            };
        }
    );
}

void CategoryPageTransformer::set_project_interactor(Biz::ProjectInteractor* project_interactor)
{
    m_project_interactor = project_interactor;
}

} // namespace Slic3r::App
