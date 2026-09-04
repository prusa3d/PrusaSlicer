#include "Slic3r/App/OverrideCategoryButton.hpp"

#include "Slic3r/Domain/Config.hpp"

#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

OverrideCategoryButton::OverrideCategoryButton(
    size_t index,
    const Biz::OverrideItem& data,
    Biz::ProjectInteractor& project_interactor,
    SelectCategoryFn& select_category_fn
) :
    Biz::DataObserver<Biz::OverrideItem>(index, data),
    LayoutButton("", Render::Icon::ChevronRight),
    m_project_interactor(project_interactor)
{
    set_content_direction(YGDirectionRTL);
    set_content_justify_content(YGJustifyFlexEnd);
    set_expand_label(true);
    set_flex_shrink(0);

    on_data_update();

    callbacks().action = [&]() {
        select_category_fn(m_state->config_item->def().category);
    };
}

void OverrideCategoryButton::on_data_update()
{
    set_label(m_state->name);

    set_label(
        Biz::_u8(Domain::ConfigItemDef::translate_category(
            m_state->config_item->def().category,
            m_project_interactor.selected_config_container().print_technology()
        ))
    );
}

} // namespace Slic3r::App
