#pragma once

#include "Slic3r/App/Render/ImguiTypes.hpp"
#include "Slic3r/App/Yoga/Circle.hpp"
#include "Slic3r/App/Yoga/ContextPopup.hpp"
#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/App/Yoga/RectangleButton.hpp"
#include "Slic3r/Biz/Preset/IPresetChangedListener.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Domain/SelectionId.hpp"
#include "Slic3r/Biz/IColorsChangedListener.hpp"
#include "Slic3r/Biz/IVirtualExtrudersChangedListener.hpp"

namespace Slic3r::App::Yoga {

class MulticolorCircle;

class ColorMenuItem : public Yoga::RectangleButton
{
public:
    ColorMenuItem(
        const std::string& label,
        const std::vector<Domain::ColorRGBA>& colors,
        bool selectable,
        bool dropdown_indicator = false,
        bool hollow = false,
        std::optional<std::string> index = std::nullopt
    );

    void set_entry(
        const std::string& label,
        const std::vector<Domain::ColorRGBA>& colors,
        bool hollow = false,
        std::optional<std::string> index = std::nullopt
    );

    void render(const Yoga::Vec2f& pos, const Yoga::Vec2f& size) override;

private:
    MulticolorCircle* m_swatch{nullptr};
    Text* m_text{nullptr};
    std::string m_label;
    bool m_dropdown_indicator{false};
};

class ColorDropdown :
    public Item,
    public Biz::IColorsChangedListener,
    public Biz::Preset::IPresetChangedListener,
    public Biz::IVirtualExtrudersChangedListener
{
public:
    ColorDropdown(
        Biz::ProjectInteractor& project_interactor,
        bool with_default,
        bool with_numbers,
        bool with_virtual = true
    );
    ~ColorDropdown();

    void set_current_index(std::optional<std::size_t> index);

    std::size_t current_index() const;

    /**
     * @brief Extruder id of the item at the given position.
     *
     * @param index Position of the item in the popup.
     * @return 1-based extruder id, physical slot or virtual extruder.
     */
    int extruder_id_at(std::size_t index) const;

    /**
     * @brief Position of the item with the given extruder id.
     *
     * @param extruder_id 1-based extruder id, physical slot or virtual extruder.
     * @return Position of the item in the popup, or std::nullopt when no item carries the id.
     */
    std::optional<std::size_t> index_of_extruder_id(int extruder_id) const;

    void style_node() override;

    std::function<void(std::size_t index)> on_color_selected{[](std::size_t index) {}};

    void on_colors_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id,
        const std::vector<Domain::ColorRGB>& colors
    ) override;

    void on_preset_selection_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id,
        Biz::Preset::PresetItemType type
    ) override;

    void on_virtual_extruders_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id
    ) override;

    void on_config_container_selection_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id
    ) override;

protected:
    void reload();
private:
    void reload_items();
    void reload_default_colors();
    void rebuild_popup_items();
    std::string get_default_text();
    void update_trigger_label();
    void set_current_index_internal(std::size_t index);
    void set_items(const std::vector<std::pair<Domain::ColorRGBA, std::string>>& material_colors);

    std::optional<int> selected_item_extruder_id() const;

    /**
     * @brief Appends the item color of the given extruder to the default item colors.
     *
     * @param extruder_id 1-based extruder id, physical slot or virtual extruder. An id without
     *                    an item (e.g. a slot of another printer) contributes nothing.
     */
    void append_default_color_of_extruder(int extruder_id);

    Biz::ProjectInteractor& m_project_interactor;
    bool m_with_default{};
    bool m_with_numbers{};
    bool m_with_virtual{};
    std::vector<std::pair<Domain::ColorRGBA, std::string>> m_material_colors;
    std::optional<std::size_t> m_current_index;
    ColorMenuItem* m_trigger{nullptr};
    ContextPopup* m_popup{nullptr};
    std::vector<ColorMenuItem*> m_popup_items;
    std::vector<Domain::ColorRGBA> m_default_colors;
    std::vector<int> m_extruder_ids;
};

} // namespace Slic3r::App::Yoga
