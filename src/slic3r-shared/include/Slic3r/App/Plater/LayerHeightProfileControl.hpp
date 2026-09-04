#pragma once

#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/Domain/LayerHeightProfile.hpp"

#include <optional>
#include <vector>

namespace Slic3r::App::Plater {

struct HeightRangeEntry
{
    double min_z{0.};
    double max_z{0.};
    double layer_height{0.};
};

using HeightRangeEntries = std::vector<HeightRangeEntry>;

class LayerHeightProfileControl : public Yoga::Item
{
public:
    LayerHeightProfileControl();

    void set_object_max_z(float object_max_z);
    void set_min_layer_height(float min_layer_height);
    void set_max_layer_height(float max_layer_height);
    void set_default_layer_height(float default_layer_height);

    void set_layer_height_profile(const Domain::ZHeightPairs& layer_height_profile);
    void set_height_ranges(const HeightRangeEntries& height_ranges);
    void set_selected_height_range(size_t range_index);
    void set_hovered_height_range(size_t range_index);
    void set_external_hovered_height_range(size_t range_index);

    void reset_selected_height_range();
    void reset_hovered_height_range();
    void reset_external_hovered_height_range();

    void update_height_range(
        size_t range_index,
        double min_z,
        double max_z,
        std::optional<double> layer_height = std::nullopt
    );

protected:
    float object_max_z() const;
    float min_layer_height_value() const;
    float max_layer_height_value() const;
    const HeightRangeEntries& height_ranges() const;
    std::optional<size_t> selected_range_index() const;
    std::optional<size_t> hovered_range_index() const;
    bool is_external_hover() const;
    void update_overlaps();

    float project_layer_height(float layer_height, float out_range_min, float out_range_max) const;
    float project_layer_z(float layer_z, float out_range_min, float out_range_max) const;
    float project_mouse_y_to_z(
        float mouse_y,
        const Domain::Vec2f& pos,
        const Domain::Vec2f& size,
        float object_max_z
    ) const;

    void render_baseline(const Domain::Vec2f& pos, const Domain::Vec2f& size) const;
    void render_layer_height_profile(const Domain::Vec2f& pos, const Domain::Vec2f& size) const;
    void render_height_ranges(const Domain::Vec2f& pos, const Domain::Vec2f& size) const;

    /**
     * @brief Returns the index of the height range under the given screen Y coordinate, if any.
     *
     * @param mouse_y Screen-space Y coordinate (e.g. ImGui mouse position).
     * @param pos Top-left corner of the profile control area in screen space.
     * @param size Width and height of the profile control area.
     */
    std::optional<size_t>
    pick_height_range(float mouse_y, const Domain::Vec2f& pos, const Domain::Vec2f& size) const;

private:
    float m_object_max_z         = 0.f;
    float m_min_layer_height     = 0.f;
    float m_max_layer_height     = 0.f;
    float m_default_layer_height = 0.f;

    Domain::ZHeightPairs m_layer_height_profile;

    HeightRangeEntries m_height_ranges;
    std::vector<std::pair<float, float>> m_overlaps;
    std::optional<size_t> m_selected_range_index;
    std::optional<size_t> m_hovered_range_index;
    bool m_external_hover = false;

    ImColor m_layer_height_profile_color;
    ImColor m_layer_height_baseline_color;
    ImColor m_height_range_color_even;
    ImColor m_height_range_color_odd;
    ImColor m_height_range_color_selected;
    ImColor m_height_range_color_hovered;
    ImColor m_height_range_color_overlap_fill;
    ImColor m_height_range_color_overlap_border;
};

} // namespace Slic3r::App::Plater
