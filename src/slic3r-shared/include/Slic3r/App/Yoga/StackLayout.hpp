#pragma once

#include "Slic3r/App/Yoga/Item.hpp"

namespace Slic3r::App::Yoga {

/**
 * @brief The StackLayout class is a stacking type layout.
 * Stack layout can have multiple children, but only one of them
 * is visible at any time.
 */
class StackLayout : public Item {
public:

    StackLayout();

    void insert(ObjectPtr child, size_t index) override;
    ObjectPtr remove(Object *child) override;

    size_t current_index() const;
    void set_current_index(size_t current_index);
    void set_current_item(Item* item);

private:
    size_t m_current_index = 0;
};

}
