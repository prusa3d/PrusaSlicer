#include "Slic3r/App/Config/PercentSliderElement.hpp"

#include "Slic3r/App/Yoga/Slider.hpp"
#include "Slic3r/App/Yoga/Text.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"

#include <cmath>
#include <utility>

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

namespace {

/// Percent formatted without a trailing ".0", so whole values read as "20 %".
std::string format_percent(double value)
{
    const double rounded = std::round(value * 10.0) / 10.0;
    std::string text = std::to_string(rounded);
    text.erase(text.find_last_not_of('0') + 1);
    if (!text.empty() && text.back() == '.')
        text.pop_back();
    return text + " %";
}

} // namespace

PercentSliderElement::PercentSliderElement(
    const ConfigFormContext& context,
    std::string key,
    const double step
) :
    ConfigFormElement(context), m_key(std::move(key))
{
    set_object_name("PercentSliderElement");
    set_orientation(Orientation::Vertical);
    set_gap(5);

    const Domain::ConfigItem* item = config_item(m_key);
    if (item == nullptr || !item->holds_alternative<Domain::Percentage>())
        return;

    const Domain::ConfigItemDef& def = item->def();
    m_label = emplace_back<Text>(Biz::_u8(def.label), Render::ImguiFontType::Bold);

    auto* row = emplace_back<Item>();
    row->set_orientation(Orientation::Horizontal);
    row->set_gap(10);
    row->set_align_items(YGAlignCenter);

    // A percentage without declared bounds still has natural ones.
    const double begin = def.min.value_or(0.0);
    const double end   = def.max.value_or(100.0);

    m_slider = row->emplace_back<Slider>(begin, end, step <= 0.0 ? 1.0 : step);
    m_slider->set_flex_grow(1);

    m_readout = row->emplace_back<Text>(format_percent(begin));
    m_readout->set_width(60);

    m_slider->callbacks().value_changed = [this](double value)
    {
        update_readout(value);
        // set_value() fires this too, so a refresh would otherwise write
        // straight back to the config it just read.
        if (m_applying_from_config)
            return;
        set_percent(m_key, value);
    };

    refresh_from_config();
}

void PercentSliderElement::update_readout(double value)
{
    if (m_readout != nullptr)
        m_readout->set_text(format_percent(value));
}

void PercentSliderElement::refresh_from_config()
{
    if (m_slider == nullptr)
        return;

    const double current = percent_of(m_key);
    if (m_shown_value.has_value() && std::abs(*m_shown_value - current) < 1e-6)
        return;
    m_shown_value = current;

    m_applying_from_config = true;
    m_slider->set_value(current);
    m_applying_from_config = false;
    // The slider snaps to its step, so read back rather than echoing the input.
    update_readout(m_slider->value());
}

void PercentSliderElement::render(const Yoga::Vec2f& pos, const Yoga::Vec2f& size)
{
    refresh_from_config();
    Item::render(pos, size);
}

} // namespace Slic3r::App
