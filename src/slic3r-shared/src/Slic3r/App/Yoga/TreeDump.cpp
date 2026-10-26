#include "Slic3r/App/Yoga/TreeDump.hpp"

#include "Slic3r/App/Yoga/AbstractButton.hpp"
#include "Slic3r/App/Yoga/CollapsibleWindow.hpp"
#include "Slic3r/App/Yoga/ComboBox.hpp"
#include "Slic3r/App/Yoga/ContextPopup.hpp"
#include "Slic3r/App/Yoga/Dialog.hpp"
#include "Slic3r/App/Yoga/InputText.hpp"
#include "Slic3r/App/Yoga/InputTextField.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/Menu.hpp"
#include "Slic3r/App/Yoga/MenuItem.hpp"
#include "Slic3r/App/Yoga/Popup.hpp"
#include "Slic3r/App/Yoga/RadioButton.hpp"
#include "Slic3r/App/Yoga/RectangleButton.hpp"
#include "Slic3r/App/Yoga/RootItem.hpp"
#include "Slic3r/App/Yoga/ScrollArea.hpp"
#include "Slic3r/App/Yoga/SegmentedControl.hpp"
#include "Slic3r/App/Yoga/Slider.hpp"
#include "Slic3r/App/Yoga/SliderWithInput.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/ToggleButton.hpp"
#include "Slic3r/App/Yoga/Tooltip.hpp"
#include "Slic3r/App/Yoga/Window.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <string_view>
#include <utility>

namespace Slic3r::App::Yoga {

namespace {

constexpr int api_version = 1;

template <class T>
bool is_type(const Object& object)
{
    return dynamic_cast<const T*>(&object) != nullptr;
}

using TypeCheck = bool (*)(const Object&);

constexpr std::array<std::pair<std::string_view, TypeCheck>, 24> known_types{{
    {"Object", is_type<Object>},
    {"Item", is_type<Item>},
    {"RootItem", is_type<RootItem>},
    {"Text", is_type<Text>},
    {"InputText", is_type<InputText>},
    {"InputTextField", is_type<InputTextField>},
    {"AbstractButton", is_type<AbstractButton>},
    {"RectangleButton", is_type<RectangleButton>},
    {"LayoutButton", is_type<LayoutButton>},
    {"MenuItem", is_type<MenuItem>},
    {"ToggleButton", is_type<ToggleButton>},
    {"RadioButton", is_type<RadioButton>},
    {"ComboBox", is_type<ComboBox>},
    {"Window", is_type<Window>},
    {"ScrollArea", is_type<ScrollArea>},
    {"Popup", is_type<Popup>},
    {"Dialog", is_type<Dialog>},
    {"Tooltip", is_type<Tooltip>},
    {"ContextPopup", is_type<ContextPopup>},
    {"Menu", is_type<Menu>},
    {"Slider", is_type<Slider>},
    {"SliderWithInput", is_type<SliderWithInput>},
    {"SegmentedControl", is_type<SegmentedControl>},
    {"CollapsibleWindow", is_type<CollapsibleWindow>},
}};

std::string class_name(const std::string& id)
{
    const size_t underscore = id.rfind('_');
    if (underscore == std::string::npos || underscore + 1 == id.size()) {
        return id;
    }
    const bool digits = std::all_of(
        id.begin() + underscore + 1,
        id.end(),
        [](unsigned char c) { return std::isdigit(c); }
    );
    return digits ? id.substr(0, underscore) : id;
}

bool is_object_visible(const Object& object)
{
    if (const auto* popup = dynamic_cast<const Popup*>(&object)) {
        return popup->opened();
    }
    if (const auto* context_popup = dynamic_cast<const ContextPopup*>(&object);
        context_popup && !context_popup->last_frame_opened())
    {
        return false;
    }
    if (const auto* item = dynamic_cast<const Item*>(&object)) {
        return item->is_visible();
    }
    return true;
}

int selected_segment(const SegmentedControl& segmented)
{
    const std::vector<Object*> children = segmented.objects();
    for (size_t index = 0; index < children.size(); ++index) {
        const auto* button = dynamic_cast<const AbstractButton*>(children[index]);
        if (button != nullptr && button->checked()) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

nlohmann::json dump_props(const Object& object)
{
    nlohmann::json props = nlohmann::json::object();

    if (const auto* text = dynamic_cast<const Text*>(&object)) {
        props["text"] = text->text();
    }
    if (const auto* input = dynamic_cast<const InputText*>(&object)) {
        props["text"]    = input->text();
        props["hint"]    = input->hint();
        props["focused"] = input->has_focus();
        props["active"]  = input->active();
    }
    if (const auto* field = dynamic_cast<const InputTextField*>(&object)) {
        props["text"] = field->text();
    }
    if (const auto* button = dynamic_cast<const AbstractButton*>(&object)) {
        props["checkable"] = button->checkable();
        props["checked"]   = button->checked();
        props["hovered"]   = button->hovered();
        props["pressed"]   = button->pressed();
    }
    if (const auto* button = dynamic_cast<const LayoutButton*>(&object)) {
        props["label"] = button->label();
    }
    if (const auto* button = dynamic_cast<const ToggleButton*>(&object)) {
        props["label"] = button->get_label();
    }
    if (const auto* button = dynamic_cast<const RadioButton*>(&object)) {
        props["label"] = button->get_label();
    }
    if (const auto* combo = dynamic_cast<const ComboBox*>(&object)) {
        props["items"]         = combo->items();
        props["current_index"] = combo->current_index();
        props["current_label"] = combo->current_label();
        props["editable"]      = combo->editable();
    }
    if (const auto* window = dynamic_cast<const Window*>(&object)) {
        props["modal"] = window->is_modal();
    }
    if (const auto* popup = dynamic_cast<const Popup*>(&object)) {
        props["opened"] = popup->opened();
    }
    if (const auto* context_popup = dynamic_cast<const ContextPopup*>(&object)) {
        props["opened"] = context_popup->last_frame_opened();
    }
    if (const auto* tooltip = dynamic_cast<const Tooltip*>(&object)) {
        props["text"] = tooltip->text();
    }
    if (const auto* dialog = dynamic_cast<const Dialog*>(&object)) {
        props["current_tab_index"] = dialog->current_tab_index();
        props["closable"]          = dialog->closable();
    }
    if (const auto* slider = dynamic_cast<const Slider*>(&object)) {
        props["value"] = slider->value();
    }
    if (const auto* slider = dynamic_cast<const SliderWithInput*>(&object)) {
        props["value"] = slider->value();
    }
    if (const auto* segmented = dynamic_cast<const SegmentedControl*>(&object)) {
        props["selected_index"] = selected_segment(*segmented);
    }
    if (const auto* collapsible = dynamic_cast<const CollapsibleWindow*>(&object)) {
        props["label"]     = collapsible->label();
        props["collapsed"] = collapsible->collapsed();
    }

    return props;
}

int root_frame_of(const Object& object)
{
    for (const Object* current = &object; current != nullptr; current = current->parent()) {
        if (const auto* root = dynamic_cast<const RootItem*>(current)) {
            return root->last_rendered_frame();
        }
    }
    return -1;
}

nlohmann::json dump_object(const Object& object, bool include_hidden, int root_frame)
{
    const bool visible = is_object_visible(object);
    if (!visible && !include_hidden) {
        return nullptr;
    }

    nlohmann::json types = nlohmann::json::array();
    for (const auto& [name, check] : known_types) {
        if (check(object)) {
            types.push_back(name);
        }
    }

    const auto* item = dynamic_cast<const Item*>(&object);

    nlohmann::json rect = nullptr;
    bool rendered       = false;
    if (item != nullptr) {
        const Vec2f pos  = item->last_render_pos();
        const Vec2f size = item->last_render_size();
        if (!std::isnan(pos.x())
            && !std::isnan(pos.y())
            && !std::isnan(size.x())
            && !std::isnan(size.y()))
        {
            rect = nlohmann::json::array({pos.x(), pos.y(), size.x(), size.y()});
        }
        rendered = root_frame >= 0 && item->last_render_frame() == root_frame;
    }

    nlohmann::json children = nlohmann::json::array();
    for (const Object* child : object.objects()) {
        nlohmann::json node = dump_object(*child, include_hidden, root_frame);
        if (!node.is_null()) {
            children.push_back(std::move(node));
        }
    }

    return nlohmann::json{
        {"id", object.object_name()},
        {"class", class_name(object.object_name())},
        {"types", std::move(types)},
        {"visible", visible},
        {"enabled", item == nullptr || item->enabled()},
        {"rendered", rendered},
        {"rect", std::move(rect)},
        {"props", dump_props(object)},
        {"children", std::move(children)},
    };
}

} // namespace

nlohmann::json dump_node(const Object& object, bool include_hidden)
{
    return dump_object(object, include_hidden, root_frame_of(object));
}

nlohmann::json dump_tree(const RootItem& root, bool include_hidden)
{
    const int frame = root.last_rendered_frame();

    nlohmann::json roots = nlohmann::json::array();
    if (frame >= 0) {
        nlohmann::json node = dump_object(root, include_hidden, frame);
        if (!node.is_null()) {
            roots.push_back(std::move(node));
        }
    }

    return nlohmann::json{
        {"version", api_version},
        {"frame", frame},
        {"include_hidden", include_hidden},
        {"roots", std::move(roots)},
    };
}

} // namespace Slic3r::App::Yoga
