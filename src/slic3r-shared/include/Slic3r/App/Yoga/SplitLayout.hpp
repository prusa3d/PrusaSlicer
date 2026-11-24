#pragma once

#include "Slic3r/App/Yoga/Item.hpp"

#include <optional>
#include <set>

namespace Slic3r::App::Yoga {

class SeparatorButton;
class SeparatorWindow;

class SplitLayout : public Item
{
public:
    SplitLayout();

    void insert(ObjectPtr child, size_t index) override;
    void append(ObjectPtr child) override;
    ObjectPtr remove(Object* child) override;

    void set_orientation(Orientation orientation);

    void set_flex_child(Item* child, bool flex);
    void set_visible_child(Item* child, bool visible);
    void set_separator_enable(size_t index, bool enable);
    /**
     * @brief Limits how far the separator extends along the cross axis (height for horizontal
     * orientation, width for vertical), measured from the start of the content area.
     * By default the separator spans the whole SplitLayout. Pass std::nullopt to reset.
     */
    void set_separator_max_extent(size_t index, std::optional<float> extent);

    void invalidate();

protected:
    void on_resized() override;

private:
    void on_separator_moved(SeparatorButton* button, float requested_pos);

private:
    struct SeparatorItem
    {
        SeparatorButton* button{nullptr};
        SeparatorWindow* window{nullptr};

        Item* item() const;
    };

    std::vector<SeparatorItem> m_separators;
    std::vector<Item*> m_content_items;
    bool m_needs_windows{false};

    std::set<Item*> m_flex_children;
};

} // namespace Slic3r::App::Yoga
