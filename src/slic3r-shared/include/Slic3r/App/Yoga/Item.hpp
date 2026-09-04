#pragma once

#include "Slic3r/App/Yoga/Object.hpp"

#include <memory>
#include <optional>
#include <variant>

namespace Slic3r::App::Render {
class ImguiRender;
class Texture;
using TexturePtr = std::shared_ptr<Texture>;
} // namespace Slic3r::App::Render

namespace Slic3r::App::Yoga {

class Item;
using ItemPtr = std::unique_ptr<Item>;

class Popup;
class RootItem;

/**
 * @brief The Item class is an Object Item in a tree that can be styled using Yoga
 *
 * When sizing elements keep in mind Pixel values are not physical pixels but
 * logical, Yoga::Item are expected to be scaled by outside layer (e.g. WxRenderCanvas)
 * and the scale is then passed into Yoga tree using Yoga::SizeInfo.
 * Moreover we allow user to set Root Font Size and therefore using rem units
 * for margins/paddings and gaps would be much more suited than px.
 */
class Item : public Object
{
public:
    struct Callbacks
    {
        std::function<void()> size_changed{
            nullptr
        }; ///< Do not use, you probably want to override on_resized instead!
    };

    Item();
    ~Item() override;

    Callbacks& item_callbacks();

    void resize(const SizeInfo& size_info) override;

    /**
     * @warning invisible Items (and their object) are not rendered
     */
    void render(const Vec2f& pos, const Vec2f& size) override;

    /**
     * @warning invisible Items (and their object) are not styled
     */
    void style_node() override;

    /**
     * @return Yoga node
     */
    YGNodeRef node() const;

    /**
     * @return true if this Item is contained in Yoga::Window
     * @warning recursive solution
     */
    virtual bool is_in_window() const;

    bool enabled() const;
    void set_enabled(bool enabled);

    float width() const;
    float height() const;
    /**
     * @note z layer only works between siblings
     */
    float z() const;
    /**
     * @return position that is relative to Item parent
     */
    float left() const;
    /**
     * @return position that is relative to Item parent
     */
    float right() const;
    /**
     * @return position that is relative to Item parent
     */
    float top() const;
    /**
     * @return position that is relative to Item parent
     */
    float bottom() const;
    const Unit& min_width() const;
    const Unit& min_heigth() const;
    const Unit& max_width() const;
    const Unit& max_height() const;
    bool is_visible() const;
    bool is_self_visible() const;
    const Unit& flex_grow() const;
    const Unit& flex_shrink() const;
    YGDirection direction() const;
    float aspect_ratio() const;
    YGPositionType position_type() const;
    bool debug_border() const;
    YGJustify justify_content() const;
    YGAlign align_items() const;
    YGAlign align_content() const;
    const EvaluatedMargins& margin() const;
    const EvaluatedPaddings& padding() const;
    const Unit& gap() const;
    Orientation orientation() const;
    YGWrap flex_wrap() const;
    bool is_node_dirty() const;

    void set_self_align(YGAlign align);
    void set_margin(const Margins& margin);
    void set_padding(const Paddings& padding);
    void set_min_width(const Unit& min_width);
    void set_min_height(const Unit& min_height);
    void set_max_width(const Unit& max_width);
    void set_max_height(const Unit& max_height);
    void set_visible(bool visible);
    void set_flex_grow(const Unit& flex_grow);
    void set_flex_shrink(const Unit& flex_shrink);
    void set_direction(YGDirection direction);
    void set_aspect_ratio(float aspect_ratio);
    void set_position_type(YGPositionType position_type);
    void set_align_items(YGAlign align_items);
    void set_orientation(Orientation orientation);
    void set_gap(const Unit& gap);
    void set_justify_content(YGJustify justify_content);
    void set_align_content(YGAlign align_content);
    void set_width(const Unit& width);
    void set_height(const Unit& height);
    void set_width_percent(float width_percent);
    void set_height_percent(float height_percent);
    void set_left(const Unit& left);
    void set_right(const Unit& right);
    void set_top(const Unit& top);
    void set_bottom(const Unit& bottom);
    void set_flex_wrap(YGWrap wrap);
    /**
     * @note z layer only works between siblings
     */
    void set_z(float z);

    void insert(ObjectPtr child, size_t index) override;
    ObjectPtr remove(Object* child) override;

    /**
     * @brief remove_later removes item only after render traversal
     * @param child to remove
     */
    void remove_later(Item* child);
    /**
     * @brief move_later moves this item only after render traversal
     * @param target to move this item
     * @param index to move this item
     */
    void move_later(Item* target, size_t index);

    /**
     * @deprecated Please use Item::get_item(size_t index)
     */
    const std::vector<Item*>& items() const;
    Item* get_item(size_t index) const;

    static void set_imgui_render(Render::ImguiRender* imgui_render);

    void set_debug_border(bool show_debug_border);

    Vec2f get_global_pos() const;

    void check_resized();

    /**
     * @warning could return null even if parented! (parent is just not Item)
     */
    Item* parent_item() const;

#ifdef DEBUG
    void render_debug_overlay(ImDrawList* draw_list) const;
#endif

protected:
    static ImVec2 to_im(const Vec2f& val);
    static Vec2f from_im(const ImVec2& val);
    static bool is_node_visible(YGNodeRef node);

    virtual Vec2f get_item_size();

    virtual void enabled_updated_internal();
    virtual void visible_updated_internal();

    virtual void on_resized();

    void update_children_render_order();

    void render_item_begin(const Vec2f& pos, const Vec2f& size);
    void render_item_end(const Vec2f& pos, const Vec2f& size);
    void render_node(const Vec2f& pos, Item* child);
    void render_debug(const Vec2f& pos, const Vec2f& size);

    /**
     * @brief render_image - replacement for ImGui::Image
     */
    void render_image(
        const Render::TexturePtr& texture,
        const ImVec2& image_size,
        const ImVec2& uv0      = ImVec2(0, 0),
        const ImVec2& uv1      = ImVec2(1, 1),
        const ImVec4& bg_col   = ImVec4(0, 0, 0, 0),
        const ImVec4& tint_col = ImVec4(1, 1, 1, 1),
        float rounding         = 0
    );

    /**
     * Invalidates the initial calculation of the minimal item size, forcing it to be recalculated.
     *
     * @warning Use this function carefully and only when you're sure it's necessary.
     * It should be called only when the content has changed in a way that might affect the item's size.
     */
    void invalidate_min_size_calculation();

protected:
    // Todo: Cleanup this when slic3r-yoga will be introduced
    static Render::ImguiRender* m_imgui_render;

    std::vector<Item*> m_children_render_order;

    float m_last_width  = 0;
    float m_last_height = 0;

#ifdef DEBUG
    static Item* m_debug_item;
#endif

private:
    void update_enabled();
    void update_visible();

    virtual void size_info_changed(const SizeInfo& info_size);

private:
    using YogaSize = std::variant<EvaluatedUnit, float>;

    YGNodeRef m_node = nullptr;

    Callbacks m_callbacks;

    bool m_min_size_calculated = false;
    bool m_debug_border        = false;

    // Set attributes
    YGAlign m_self_align             = YGAlign::YGAlignAuto;
    YGAlign m_align_items            = YGAlign::YGAlignStretch;
    YGAlign m_align_content          = YGAlign::YGAlignFlexStart;
    YGJustify m_justify_content      = YGJustify::YGJustifyFlexStart;
    YGPositionType m_position_type   = YGPositionType::YGPositionTypeRelative;
    Orientation m_orientation        = Orientation::Horizontal;
    YGFlexDirection m_flex_direction = YGFlexDirectionRow;
    YGDirection m_direction          = YGDirectionLTR;

    float m_aspect_ratio = YGUndefined;

    float m_z = 0;

    // Evaluated units
    std::optional<EvaluatedUnit> m_left;
    std::optional<EvaluatedUnit> m_right;
    std::optional<EvaluatedUnit> m_top;
    std::optional<EvaluatedUnit> m_bottom;
    std::optional<YogaSize> m_width;
    std::optional<YogaSize> m_height;
    EvaluatedUnit m_min_width;
    EvaluatedUnit m_max_width{YGUndefined};
    EvaluatedUnit m_min_height;
    EvaluatedUnit m_max_height{YGUndefined};
    EvaluatedUnit m_flex_grow;
    EvaluatedUnit m_flex_shrink{1};
    EvaluatedUnit m_gap;
    EvaluatedMargins m_margins;
    EvaluatedPaddings m_paddings;

    /**
     * @warning SPARSE field, objects which are not items are in this vector as well
     */
    std::vector<Item*> m_children_items;
    Item* m_parent_item = nullptr;
    bool m_has_items    = false;
    bool m_enabled      = true;
    bool m_visible      = true;
};

/**
 * The Passthrough class is a handy container for "passing through"
 * changing ownership of primarily of ItemPtr. It utilizes load and unload methods,
 * unloanding still keeps set m_raw pointer so even though the ownership
 * was already transfered a Passthrough instance still provides pointer
 * to the once owned instance.
 */
template <class T>
class Passthrough
{
public:
    Passthrough() {}

    Passthrough(std::unique_ptr<T>&& unique)
    {
        reset(std::move(unique));
    }

    T* operator->() const
    {
        return m_raw;
    }

    void reset(std::unique_ptr<T> unique)
    {
        m_raw    = unique.get();
        m_unique = std::move(unique);
    }

    std::unique_ptr<T> release()
    {
        ASSERT(m_unique, "releasing empty unique_ptr");
        return std::move(m_unique);
    }

    T* get() const
    {
        return m_raw;
    }

private:
    std::unique_ptr<T> m_unique;
    T* m_raw = nullptr;
};

template <class T>
std::unique_ptr<T> unique_dynamic_cast(ItemPtr item)
{
    return std::unique_ptr<T>(dynamic_cast<T*>(item.release()));
}

} // namespace Slic3r::App::Yoga
