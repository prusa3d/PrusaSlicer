#include "Slic3r/App/Config/TravelAvoidanceElement.hpp"

#include "Slic3r/App/Config/ConfigFormElementRegistry.hpp"
#include "Slic3r/App/Config/ConfigItemControl.hpp"
#include "Slic3r/App/Yoga/RadioButton.hpp"
#include "Slic3r/App/Yoga/Text.hpp"

#include "Slic3r/Biz/ConfigBoxInteractor.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

namespace {

constexpr const char* AVOID_PERIMETERS      = "avoid_crossing_perimeters";
constexpr const char* AVOID_CURLED          = "avoid_crossing_curled_overhangs";
constexpr const char* MAX_DETOUR            = "avoid_crossing_perimeters_max_detour";

} // namespace

const std::set<std::string>& TravelAvoidanceElement::claimed_keys()
{
    static const std::set<std::string> keys{AVOID_PERIMETERS, AVOID_CURLED, MAX_DETOUR};
    return keys;
}

TravelAvoidanceElement::TravelAvoidanceElement(const ConfigFormContext& context) :
    ConfigFormElement(context)
{
    set_object_name("TravelAvoidanceElement");
    set_orientation(Orientation::Vertical);
    set_gap(5);

    // No title of its own: the subcategory already renders the option group's
    // heading above this, and repeating it would read as two sections.
    m_straight = emplace_back<RadioButton>(
        Biz::_u8L("Straight"),
        Biz::_u8L("Travel moves take the direct route.")
    );
    m_around_perimeters = emplace_back<RadioButton>(
        Biz::_u8L("Around perimeters"),
        Biz::_u8L(
            "Detour around perimeters so the nozzle crosses them as little as possible. "
            "Mostly useful with Bowden extruders, which suffer from oozing. Slows down both "
            "the print and the G-code generation."
        )
    );
    m_around_curled = emplace_back<RadioButton>(
        Biz::_u8L("Around curled overhangs (experimental)"),
        Biz::_u8L(
            "Detour around areas where the filament may have curled up, which mostly happens "
            "on steeper rounded overhangs and can otherwise crash the nozzle. Slows down both "
            "the print and the G-code generation."
        )
    );

    m_strategy_group.set_buttons({m_straight, m_around_perimeters, m_around_curled});
    m_strategy_group.set_always_checked(true);
    m_strategy_group.callbacks().checked_changed =
        [this](AbstractButton* current, AbstractButton*)
    {
        // set_checked() fires this too, so a refresh from the config would
        // otherwise write straight back to the config it just read.
        if (m_applying_from_config)
            return;
        if (current == m_straight)
            apply_strategy(Strategy::Straight);
        else if (current == m_around_perimeters)
            apply_strategy(Strategy::AroundPerimeters);
        else if (current == m_around_curled)
            apply_strategy(Strategy::AroundCurledOverhangs);
    };

    // The detour length keeps its standard control, so it inherits the unit,
    // the min/max and the formatting from its own definition rather than having
    // them restated here.
    const Domain::ConfigItem* max_detour = config_item(MAX_DETOUR);
    if (max_detour != nullptr && context.setter != nullptr) {
        m_detour_row = emplace_back<Item>();
        m_detour_row->set_orientation(Orientation::Horizontal);
        m_detour_row->set_gap(5);
        m_detour_row->set_align_items(YGAlignCenter);
        m_detour_row->set_padding(Paddings(0.f, 0.f, 20.f, 0.f));

        m_detour_label = m_detour_row->emplace_back<Text>(Biz::_u8L("Max detour length"));
        m_detour_label->set_width(150);

        m_detour_control = ConfigItemControl::config_item_control_factory(
            m_detour_row,
            1,
            0,
            *max_detour,
            *context.setter,
            {context.cbi_index}
        );
        if (auto* input = dynamic_cast<Item*>(m_detour_control)) {
            input->set_width(150_fpx);
            input->set_max_width(200_fpx);
        }
    }

    refresh_from_config();
}

TravelAvoidanceElement::Strategy TravelAvoidanceElement::strategy_from_config() const
{
    // Both booleans set is not a state the UI can produce, but a profile written
    // before this control existed can carry it. Perimeters wins, matching what
    // the slicer does with the pair.
    if (flag_of(AVOID_PERIMETERS))
        return Strategy::AroundPerimeters;
    if (flag_of(AVOID_CURLED))
        return Strategy::AroundCurledOverhangs;
    return Strategy::Straight;
}

void TravelAvoidanceElement::apply_strategy(const Strategy strategy)
{
    // Write both booleans on every change. Setting only the chosen one would
    // leave the other set when arriving from a profile that had both.
    set_flag(AVOID_PERIMETERS, strategy == Strategy::AroundPerimeters);
    set_flag(AVOID_CURLED, strategy == Strategy::AroundCurledOverhangs);
    update_detour_visibility(strategy);
}

void TravelAvoidanceElement::update_detour_visibility(const Strategy strategy)
{
    if (m_detour_row != nullptr)
        m_detour_row->set_visible(strategy == Strategy::AroundPerimeters);
}

void TravelAvoidanceElement::refresh_from_config()
{
    const Strategy strategy = strategy_from_config();
    if (m_shown_strategy == strategy)
        return;
    m_shown_strategy = strategy;

    m_applying_from_config = true;
    m_straight->set_checked(strategy == Strategy::Straight);
    m_around_perimeters->set_checked(strategy == Strategy::AroundPerimeters);
    m_around_curled->set_checked(strategy == Strategy::AroundCurledOverhangs);
    m_applying_from_config = false;

    update_detour_visibility(strategy);

    if (m_detour_control != nullptr) {
        if (const Domain::ConfigItem* max_detour = config_item(MAX_DETOUR))
            m_detour_control->set_state(*max_detour);
    }
}

void TravelAvoidanceElement::render(const Yoga::Vec2f& pos, const Yoga::Vec2f& size)
{
    // Re-read every frame, like the rest of this UI: the settings can change
    // from an undo, a preset switch or a write elsewhere, and none of those
    // notify this element. refresh_from_config() returns immediately unless the
    // chosen strategy actually differs.
    refresh_from_config();
    Item::render(pos, size);
}

void register_travel_avoidance_element(ConfigFormElementRegistry& registry)
{
    registry.register_element(ConfigFormElementRegistry::Entry{
        .category     = Domain::ConfigItemDef::Category::Print_MotionDynamics,
        .option_group = Domain::ConfigItemDef::OptionGroup::Print_MotionDynamics_TravelAvoidance,
        .claimed_keys = TravelAvoidanceElement::claimed_keys(),
        .factory =
            [](const ConfigFormContext& context)
        { return std::make_unique<TravelAvoidanceElement>(context); }
    });
}

} // namespace Slic3r::App
