#include "Slic3r/App/Config/EnumCardsElement.hpp"

#include "Slic3r/App/Yoga/RadioButton.hpp"
#include "Slic3r/App/Yoga/Text.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"

#include <utility>

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

EnumCardsElement::EnumCardsElement(
    const ConfigFormContext& context,
    std::string key,
    const size_t columns
) :
    ConfigFormElement(context), m_key(std::move(key))
{
    set_object_name("EnumCardsElement");
    set_orientation(Orientation::Vertical);
    set_gap(5);

    const Domain::ConfigItem* item = config_item(m_key);
    if (item == nullptr || !item->holds_alternative<Domain::EnumWrapper>())
        return;

    const Domain::ConfigItemDef& def = item->def();
    m_label = emplace_back<Text>(Biz::_u8(def.label), Render::ImguiFontType::Bold);

    const auto translate = [&def](const std::string& s)
    { return def.i18n_context.empty() ? Biz::_u8(s) : Biz::_ctx_u8(s, def.i18n_context); };

    // Options come from the value's own definitions, so this stays correct as
    // settings gain or lose choices without touching this file.
    const Domain::EnumValueDefs& values = item->value().get<Domain::EnumWrapper>().def();

    Item* row = nullptr;
    const size_t per_row = columns == 0 ? 1 : columns;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i % per_row == 0) {
            row = emplace_back<Item>();
            row->set_orientation(Orientation::Horizontal);
            row->set_gap(10);
        }
        const Domain::EnumValueDef& value = values[i];
        auto* card = row->emplace_back<RadioButton>(translate(value.str_ui), Biz::_u8(def.tooltip));
        card->set_flex_grow(1);
        m_cards.emplace_back(value.enum_value, card);
        m_group.insert_button(card);
    }
    m_group.set_always_checked(true);

    m_group.callbacks().checked_changed = [this](AbstractButton* current, AbstractButton*)
    {
        // set_checked() fires this too, so a refresh would otherwise write
        // straight back to the config it just read.
        if (m_applying_from_config)
            return;
        for (const auto& [value, card] : m_cards) {
            if (card == current) {
                set_enum(m_key, value);
                return;
            }
        }
    };

    refresh_from_config();
}

void EnumCardsElement::refresh_from_config()
{
    if (m_cards.empty())
        return;

    const int current = enum_of(m_key);
    if (m_shown_value == current)
        return;
    m_shown_value = current;

    m_applying_from_config = true;
    for (const auto& [value, card] : m_cards)
        card->set_checked(value == current);
    m_applying_from_config = false;
}

void EnumCardsElement::render(const Yoga::Vec2f& pos, const Yoga::Vec2f& size)
{
    // Re-read every frame: an undo, a preset switch or a write elsewhere does
    // not notify this element, and the check is a comparison unless it changed.
    refresh_from_config();
    Item::render(pos, size);
}

} // namespace Slic3r::App
