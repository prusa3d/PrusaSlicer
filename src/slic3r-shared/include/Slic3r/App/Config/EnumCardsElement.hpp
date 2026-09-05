#pragma once

#include "Slic3r/App/Config/ConfigFormElement.hpp"
#include "Slic3r/App/Yoga/ButtonGroup.hpp"

#include <optional>
#include <string>
#include <vector>

namespace Slic3r::App::Yoga {
class RadioButton;
class Text;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App {

/**
 * @brief Renders one enum setting as a list of cards instead of a dropdown.
 *
 * A dropdown shows one option and hides the rest, which is the wrong shape for
 * a choice the user makes by comparing the alternatives — infill pattern, seam
 * position, support style. Laying the options out means the choice can be read
 * without opening anything.
 *
 * Generic on purpose: the options, their labels and the setting's own label all
 * come from the definition, so this works for any enum setting and registering
 * it costs one line. Only worth it where there are few enough options to lay
 * out; a dropdown is still right for a long list.
 */
class EnumCardsElement : public ConfigFormElement
{
public:
    /**
     * @param key The enum setting to render.
     * @param columns How many cards per row. One reads as a list, more as a grid.
     */
    EnumCardsElement(const ConfigFormContext& context, std::string key, size_t columns = 1);

    void refresh_from_config() override;

    void render(const Yoga::Vec2f& pos, const Yoga::Vec2f& size) override;

private:
    std::string m_key;

    Yoga::Text* m_label{nullptr};
    Yoga::ButtonGroup m_group;
    /// Cards in definition order, paired with the enum value each selects.
    std::vector<std::pair<int, Yoga::RadioButton*>> m_cards;

    std::optional<int> m_shown_value;
    bool m_applying_from_config{false};
};

} // namespace Slic3r::App
