#include "Slic3r/App/Config/ToolToFeatureDialogHeader.hpp"

#include <Slic3r/Domain/Config.hpp>

#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/Namespace.hpp"

#include "Slic3r/Biz/IConfigBoxSetter.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

namespace Slic3r::App {

using namespace Yoga;

namespace {
Unit buttons_gap = 10_fpx;
} // namespace

ToolToFeatureDialogHeader::ToolToFeatureDialogHeader(
    Biz::ProjectInteractor& project_interactor,
    Biz::IConfigBoxSetter& cb_setter
) :
    m_cb_setter(cb_setter)
{
    set_object_name("ExtruderIdButtonGroup");
    set_orientation(Orientation::Horizontal);
    set_padding({0, 0, 0, 8_fpx});
    set_gap(5_fpx);

    Text* label = emplace_back<Text>(Biz::_u8L("Extruders"));
    label->set_font_type(Render::ImguiFontType::Bold);
    label->set_flex_grow(1);

    m_auto_button = emplace_back<CircleBorderedButton>();
    m_auto_button->set_icon(Render::Icon::Robot);
    m_auto_button->set_icon_tint(m_theme->color_imgui(Platform::Color::AccentTertiary));
    m_auto_button->set_tooltip(
        Biz::_u8L(
            "Extruder auto-selection\n"
            "(use the current extruder to minimize tool changes)"
        )
    );
    m_auto_button->callbacks().action = [this]()
    {
        on_button_clicked(0);
        m_auto_button->set_checked(true);
    };

    m_list_view = emplace_back<ExtruderButtonListView>(ExtruderButtonListViewFactory{
        [this](int extruder_id) { on_button_clicked(extruder_id); },
        project_interactor
    });
    m_list_view->set_orientation(Orientation::Horizontal);
    m_list_view->set_gap(buttons_gap);
    m_list_view->set_source_list(&project_interactor.preset_interactor().material_presets());
}

ToolToFeatureDialogHeader::~ToolToFeatureDialogHeader()
{
    if (m_source_list.is_valid()) {
        m_source_list->remove_listener<Biz::IListObserver<Biz::PrintToolItem>>(this);
    }
}

void ToolToFeatureDialogHeader::set_source_list(
    std::weak_ptr<Biz::IObservableList<Biz::PrintToolItem>> source_list
)
{
    if (m_source_list == source_list) {
        return;
    }

    if (m_source_list.is_valid()) {
        m_source_list->remove_listener<Biz::IListObserver<Biz::PrintToolItem>>(this);
    }

    m_source_list = source_list;

    if (m_source_list.is_valid()) {
        m_source_list->add_listener<Biz::IListObserver<Biz::PrintToolItem>>(this);
    }

    update_checked_buttons();
}

void ToolToFeatureDialogHeader::on_updated(const Biz::IndexRange& index_range)
{
    update_checked_buttons();
}

void ToolToFeatureDialogHeader::update_checked_buttons()
{
    if (!m_source_list.is_valid()) {
        m_auto_button->set_checked(false);
        for (size_t i = 0; i < m_list_view->list_item_count(); ++i) {
            m_list_view->item_at(i)->set_checked(false);
        }
        return;
    }

    auto is_used = [this](int id) -> bool
    {
        for (size_t i = 0; i < m_source_list->size(); ++i) {
            if (m_source_list->at(i).value.first.get<int>() == id) {
                return true;
            }
        }
        return false;
    };

    m_auto_button->set_checked(is_used(0));
    for (size_t i = 0; i < m_list_view->list_item_count(); ++i) {
        ToolToFeatureExtruderButton* button = m_list_view->item_at(i);
        button->set_checked(is_used(button->extruder_id()));
    }
}

void ToolToFeatureDialogHeader::on_button_clicked(int extruder_id)
{
    if (!m_source_list.is_valid()) {
        return;
    }

    for (size_t i = 0; i < m_source_list->size(); ++i) {
        const Biz::PrintToolItem& item     = m_source_list->at(i);
        std::optional<int> min_extruder_id = std::nullopt;
        if (item.print_item->def().min && extruder_id < item.print_item->def().min.value())
            min_extruder_id = static_cast<int>(item.print_item->def().min.value());
        m_cb_setter.set_item_value(
            *item.print_item,
            Domain::ConfigValue(min_extruder_id.value_or(extruder_id))
        );
    }
}

} // namespace Slic3r::App
