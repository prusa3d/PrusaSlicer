#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <Slic3r/App/Yoga/ComboBox.hpp>
#include <Slic3r/App/Yoga/InputText.hpp>
#include <Slic3r/App/Yoga/Popup.hpp>
#include <Slic3r/App/Yoga/RectangleButton.hpp>
#include <Slic3r/App/Yoga/Text.hpp>
#include <Slic3r/App/Yoga/TreeDump.hpp>
#include <Slic3r/App/Yoga/Validator.hpp>

#include "YogaComponentFixture.hpp"

#include <set>
#include <string>

using namespace Slic3r::App::Yoga;
using Catch::Matchers::WithinAbs;

namespace {

struct DumpPopup : public Popup
{
    DumpPopup()
    {
        auto win = std::make_unique<Window>("DumpPopup");
        win->set_width(80.f);
        win->set_height(60.f);
        win->set_flags(
            ImGuiWindowFlags_NoDecoration
            | ImGuiWindowFlags_NoInputs
            | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoBringToFrontOnFocus
        );
        set_content_item(std::move(win));
    }
};

const nlohmann::json* find_node(const nlohmann::json& node, const std::string& id)
{
    if (node["id"] == id) {
        return &node;
    }
    for (const nlohmann::json& child : node["children"]) {
        if (const nlohmann::json* found = find_node(child, id)) {
            return found;
        }
    }
    return nullptr;
}

const nlohmann::json* find_in_tree(const nlohmann::json& tree, const std::string& id)
{
    for (const nlohmann::json& root : tree["roots"]) {
        if (const nlohmann::json* found = find_node(root, id)) {
            return found;
        }
    }
    return nullptr;
}

bool any_hidden(const nlohmann::json& node)
{
    if (node["visible"] == false) {
        return true;
    }
    for (const nlohmann::json& child : node["children"]) {
        if (any_hidden(child)) {
            return true;
        }
    }
    return false;
}

struct TreeDumpFixture : public YogaComponentFixture
{
    TreeDumpFixture()
    {
        text = detached.emplace_back<Text>("Hello dump");

        button = window->emplace_back<RectangleButton>();
        button->set_position_type(YGPositionTypeAbsolute);
        button->set_left(100.f);
        button->set_top(50.f);
        button->set_width(40.f);
        button->set_height(30.f);

        input = window->emplace_back<InputText>();
        input->set_text("42");

        combo =
            window->emplace_back<ComboBox>(std::initializer_list<std::string>{"First", "Second"});
        combo->set_current_index(1);

        hidden_box = window->emplace_back<Item>();
        hidden_box->emplace_back<Text>("Hidden child");
        hidden_box->set_visible(false);

        popup = window->emplace_back<DumpPopup>();
        popup->attach_to_item(button);

        render();
    }

    Item detached;

    Text* text              = nullptr;
    RectangleButton* button = nullptr;
    InputText* input        = nullptr;
    ComboBox* combo         = nullptr;
    Item* hidden_box        = nullptr;
    DumpPopup* popup        = nullptr;
};

} // namespace

TEST_CASE_METHOD(TreeDumpFixture, "TreeDump: tree envelope and root", "[treedump]")
{
    const nlohmann::json tree = dump_tree(root);
    REQUIRE(tree["version"] == 1);
    REQUIRE(tree["frame"] == root.last_rendered_frame());
    REQUIRE(tree["include_hidden"] == false);
    REQUIRE(tree["roots"].size() == 1);
    REQUIRE(tree["roots"][0]["id"] == root.object_name());
}

TEST_CASE_METHOD(TreeDumpFixture, "TreeDump: unrendered root has no roots", "[treedump]")
{
    RootItem unrendered;
    const nlohmann::json tree = dump_tree(unrendered);
    REQUIRE(tree["frame"] == -1);
    REQUIRE(tree["roots"].empty());
}

TEST_CASE_METHOD(TreeDumpFixture, "TreeDump: node has exactly the contract keys", "[treedump]")
{
    const nlohmann::json node = dump_node(*button);
    std::set<std::string> keys;
    for (const auto& [key, value] : node.items()) {
        keys.insert(key);
    }
    REQUIRE(
        keys
        == std::set<std::string>{
            "id",
            "class",
            "types",
            "visible",
            "enabled",
            "rendered",
            "rect",
            "props",
            "children"
        }
    );
}

TEST_CASE_METHOD(TreeDumpFixture, "TreeDump: id and class split", "[treedump]")
{
    const nlohmann::json node = dump_node(*button);
    REQUIRE(node["id"] == button->object_name());
    REQUIRE(button->object_name().starts_with("RectangleButton_"));
    REQUIRE(node["class"] == "RectangleButton");
}

TEST_CASE_METHOD(TreeDumpFixture, "TreeDump: types are ordered from base to derived", "[treedump]")
{
    REQUIRE(
        dump_node(*button)["types"]
        == nlohmann::json::array({"Object", "Item", "AbstractButton", "RectangleButton"})
    );
    REQUIRE(dump_node(*text)["types"] == nlohmann::json::array({"Object", "Item", "Text"}));
    REQUIRE(dump_node(*window)["types"] == nlohmann::json::array({"Object", "Item", "Window"}));
    REQUIRE(dump_node(*popup, true)["types"] == nlohmann::json::array({"Object", "Popup"}));
    REQUIRE(dump_node(root)["types"] == nlohmann::json::array({"Object", "Item", "RootItem"}));
}

TEST_CASE_METHOD(TreeDumpFixture, "TreeDump: props per type", "[treedump]")
{
    REQUIRE(dump_node(*text)["props"]["text"] == "Hello dump");

    const nlohmann::json input_props = dump_node(*input)["props"];
    REQUIRE(input_props["text"] == "42");
    REQUIRE(input_props.contains("hint"));
    REQUIRE(input_props["focused"] == false);
    REQUIRE(input_props.contains("active"));

    const nlohmann::json button_props = dump_node(*button)["props"];
    REQUIRE(button_props["checkable"] == false);
    REQUIRE(button_props["checked"] == false);
    REQUIRE(button_props["hovered"] == false);
    REQUIRE(button_props["pressed"] == false);
    REQUIRE_FALSE(button_props.contains("label"));

    const nlohmann::json combo_props = dump_node(*combo)["props"];
    REQUIRE(combo_props["items"] == nlohmann::json::array({"First", "Second"}));
    REQUIRE(combo_props["current_index"] == 1);
    REQUIRE(combo_props["current_label"] == "Second");
    REQUIRE(combo_props["editable"] == false);

    REQUIRE(dump_node(*window)["props"]["modal"] == false);
}

TEST_CASE_METHOD(
    TreeDumpFixture,
    "TreeDump: rendered flag and rect follow the layout",
    "[treedump]"
)
{
    const nlohmann::json node = dump_node(*button);
    REQUIRE(node["rendered"] == true);
    REQUIRE(node["visible"] == true);
    REQUIRE(node["enabled"] == true);
    REQUIRE_THAT(node["rect"][0].get<float>(), WithinAbs(100.f, 0.01f));
    REQUIRE_THAT(node["rect"][1].get<float>(), WithinAbs(50.f, 0.01f));
    REQUIRE_THAT(node["rect"][2].get<float>(), WithinAbs(40.f, 0.01f));
    REQUIRE_THAT(node["rect"][3].get<float>(), WithinAbs(30.f, 0.01f));

    const nlohmann::json popup_node = dump_node(*popup, true);
    REQUIRE(popup_node["rendered"] == false);
    REQUIRE(popup_node["rect"].is_null());

    button->set_enabled(false);
    REQUIRE(dump_node(*button)["enabled"] == false);
}

TEST_CASE_METHOD(
    TreeDumpFixture,
    "TreeDump: hidden subtrees and closed popups are pruned by default",
    "[treedump]"
)
{
    const nlohmann::json tree = dump_tree(root);
    REQUIRE(find_in_tree(tree, hidden_box->object_name()) == nullptr);
    REQUIRE(find_in_tree(tree, popup->object_name()) == nullptr);
    REQUIRE(find_in_tree(tree, popup->content_item()->object_name()) == nullptr);
    for (const nlohmann::json& root_node : tree["roots"]) {
        REQUIRE_FALSE(any_hidden(root_node));
    }
    REQUIRE(dump_node(*hidden_box).is_null());
}

TEST_CASE_METHOD(
    TreeDumpFixture,
    "TreeDump: include_hidden emits hidden objects with visible false",
    "[treedump]"
)
{
    const nlohmann::json tree = dump_tree(root, true);
    REQUIRE(tree["include_hidden"] == true);

    const nlohmann::json* hidden = find_in_tree(tree, hidden_box->object_name());
    REQUIRE(hidden != nullptr);
    REQUIRE((*hidden)["visible"] == false);
    REQUIRE((*hidden)["rendered"] == false);
    REQUIRE((*hidden)["children"].size() == 1);
    REQUIRE((*hidden)["children"][0]["visible"] == false);

    const nlohmann::json* closed_popup = find_in_tree(tree, popup->object_name());
    REQUIRE(closed_popup != nullptr);
    REQUIRE((*closed_popup)["visible"] == false);
    REQUIRE((*closed_popup)["props"]["opened"] == false);

    REQUIRE(tree.dump().size() > dump_tree(root).dump().size());
}

TEST_CASE_METHOD(
    TreeDumpFixture,
    "TreeDump: opened popup appears in the default dump",
    "[treedump]"
)
{
    popup->open();
    render();

    const nlohmann::json tree        = dump_tree(root);
    const nlohmann::json* popup_node = find_in_tree(tree, popup->object_name());
    REQUIRE(popup_node != nullptr);
    REQUIRE((*popup_node)["visible"] == true);
    REQUIRE((*popup_node)["props"]["opened"] == true);
    REQUIRE((*popup_node)["types"] == nlohmann::json::array({"Object", "Popup"}));

    const nlohmann::json* content = find_in_tree(tree, popup->content_item()->object_name());
    REQUIRE(content != nullptr);
    REQUIRE((*content)["rendered"] == true);
    REQUIRE((*content)["rect"].is_array());
}
