#pragma once

#include "Slic3r/App/Config/ConfigFormElement.hpp"
#include "Slic3r/App/Yoga/ButtonGroup.hpp"

#include <optional>
#include <set>
#include <string>

namespace Slic3r::App::Yoga {
class Item;
class RadioButton;
class Text;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App {

class ConfigFormElementRegistry;
class ConfigItemControl;

/**
 * @brief Presents travel avoidance as the single choice it actually is.
 *
 * The config holds two booleans and a length:
 *
 * - avoid_crossing_perimeters
 * - avoid_crossing_curled_overhangs
 * - avoid_crossing_perimeters_max_detour
 *
 * The two booleans are mutually exclusive — 2.x disabled each when the other
 * was set — and the length only applies to the first. Rendered one row per key
 * that reads as three independent settings, two of which can be switched on
 * together to produce a combination the slicer later refuses.
 *
 * So they are rendered as one radio group over three strategies, with the
 * detour length shown only for the strategy it belongs to. Nothing about
 * storage changes: this writes the same two booleans, and the length keeps its
 * own standard control, so profiles and the backend are untouched.
 */
class TravelAvoidanceElement : public ConfigFormElement
{
public:
    explicit TravelAvoidanceElement(const ConfigFormContext& context);

    void refresh_from_config() override;

    void render(const Yoga::Vec2f& pos, const Yoga::Vec2f& size) override;

    /// Keys this element renders, so they get no default row.
    static const std::set<std::string>& claimed_keys();

private:
    enum class Strategy
    {
        Straight,        ///< Neither boolean set: travel goes where it likes.
        AroundPerimeters,
        AroundCurledOverhangs
    };

    Strategy strategy_from_config() const;
    void apply_strategy(Strategy strategy);
    void update_detour_visibility(Strategy strategy);

    Yoga::RadioButton* m_straight{nullptr};
    Yoga::RadioButton* m_around_perimeters{nullptr};
    Yoga::RadioButton* m_around_curled{nullptr};
    Yoga::ButtonGroup m_strategy_group;

    Yoga::Item* m_detour_row{nullptr};
    Yoga::Text* m_detour_label{nullptr};
    ConfigItemControl* m_detour_control{nullptr};

    /// Last strategy pushed into the buttons, to avoid rebuilding every frame.
    std::optional<Strategy> m_shown_strategy;

    /// True while pushing config values into the buttons, so the resulting
    /// checked_changed callbacks do not write those values straight back.
    bool m_applying_from_config{false};
};

/// Register this element with the given registry.
void register_travel_avoidance_element(ConfigFormElementRegistry& registry);

} // namespace Slic3r::App
