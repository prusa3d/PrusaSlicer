#pragma once

#include "Slic3r/App/Yoga/AbstractButton.hpp"
#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/Domain/Types.hpp"

#include <functional>
#include <string>

namespace Slic3r::App::ColorMix {

/**
 * @brief Triangle mixer of a three-filament blend.
 *
 * Each corner has the color of one filament. Dragging the handle sets how much of each one is
 * used, in steps of five percent.
 */
class BarycentricRatioPicker : public Yoga::AbstractButton
{
public:
    struct Callbacks
    {
        std::function<void()> weights_changed{nullptr};
    };

    BarycentricRatioPicker();

    Callbacks& callbacks();

    void set_colors(const ImColor& color_0, const ImColor& color_1, const ImColor& color_2);

    /**
     * @brief Weights of the three components, normalized to sum to one.
     */
    void set_weights(double weight_0, double weight_1, double weight_2);

    void set_vertex_labels(
        const std::string& label_0,
        const std::string& label_1,
        const std::string& label_2
    );

    [[nodiscard]] double weight_0() const
    {
        return m_weight_0;
    }

    [[nodiscard]] double weight_1() const
    {
        return m_weight_1;
    }

    [[nodiscard]] double weight_2() const
    {
        return m_weight_2;
    }

    void render(const Yoga::Vec2f& pos, const Yoga::Vec2f& size) override;

private:
    void size_info_changed(const Yoga::SizeInfo& info_size) override;

    void vertices(
        const Yoga::Vec2f& size,
        Domain::Vec2d& v0,
        Domain::Vec2d& v1,
        Domain::Vec2d& v2
    ) const;
    void normalize_and_assign(double weight_0, double weight_1, double weight_2);
    bool update_from_point(const Yoga::Vec2f& size, const Domain::Vec2d& p);

    ImColor m_color_0{0xC0, 0xC0, 0xC0};
    ImColor m_color_1{0xC0, 0xC0, 0xC0};
    ImColor m_color_2{0xC0, 0xC0, 0xC0};
    double m_weight_0{1.0 / 3.0};
    double m_weight_1{1.0 / 3.0};
    double m_weight_2{1.0 / 3.0};
    std::string m_label_0{"1"};
    std::string m_label_1{"2"};
    std::string m_label_2{"3"};
    bool m_dragging{false};

    Yoga::EvaluatedUnit m_triangle_margin;
    Yoga::EvaluatedUnit m_badge_radius;
    Yoga::EvaluatedUnit m_badge_border_thickness;
    Yoga::EvaluatedUnit m_handle_radius;
    Yoga::EvaluatedUnit m_outline_thickness;

    Callbacks m_callbacks;
};

} // namespace Slic3r::App::ColorMix
