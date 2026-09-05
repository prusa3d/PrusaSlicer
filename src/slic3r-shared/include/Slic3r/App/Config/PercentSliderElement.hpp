#pragma once

#include "Slic3r/App/Config/ConfigFormElement.hpp"

#include <optional>
#include <string>

namespace Slic3r::App::Yoga {
class Slider;
class Text;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App {

/**
 * @brief Renders one bounded percentage as a slider with a live readout.
 *
 * Density, overlap and flow settings are bounded quantities the user tunes by
 * feel, and a text field says nothing about where a value sits in its range or
 * which way is "more". A slider shows both, and the readout keeps the exact
 * number visible so precision is not lost to the gesture.
 *
 * The range comes from the definition's own min and max, so this works for any
 * percentage setting that declares them.
 */
class PercentSliderElement : public ConfigFormElement
{
public:
    /**
     * @param key The percentage setting to render.
     * @param step Slider granularity, in percent.
     */
    PercentSliderElement(const ConfigFormContext& context, std::string key, double step = 1.0);

    void refresh_from_config() override;

    void render(const Yoga::Vec2f& pos, const Yoga::Vec2f& size) override;

private:
    void update_readout(double value);

    std::string m_key;

    Yoga::Text* m_label{nullptr};
    Yoga::Slider* m_slider{nullptr};
    Yoga::Text* m_readout{nullptr};

    std::optional<double> m_shown_value;
    bool m_applying_from_config{false};
};

} // namespace Slic3r::App
