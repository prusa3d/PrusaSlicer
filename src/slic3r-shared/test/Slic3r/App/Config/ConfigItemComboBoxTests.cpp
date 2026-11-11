#include <catch2/catch_test_macros.hpp>

#include <Slic3r/App/Config/ConfigItemComboBox.hpp>
#include <Slic3r/Biz/IConfigBoxSetter.hpp>
#include <Slic3r/Domain/ConfigDefsFDM.hpp>

#include "../Yoga/YogaComponentFixture.hpp"

using namespace Slic3r;
using namespace Slic3r::Domain;

namespace {

/** @brief Hosts a ConfigItemComboBox of a FDM config item and applies the values it sets back to the item. */
struct ConfigItemComboBoxFixture : public YogaComponentFixture, public Biz::IConfigBoxSetter
{
    void make_combo(const std::string& name)
    {
        const auto& defs = get_defs_fdm().defs();
        const auto def   = std::ranges::find(defs, name, &ConfigItemDef::name);
        REQUIRE(def != defs.end());
        item.emplace(*def, def->location);
        combo =
            window->emplace_back<App::ConfigItemComboBox>(0, *item, *this, std::vector<size_t>{0});
    }

    template <typename T>
    void set_value(const T& value)
    {
        item->set(value);
        combo->set_state(*item);
        set_count = 0;
    }

    void wheel(float y)
    {
        hover(combo, 0.25f);
        mouse_wheel(y);
    }

    void click_text()
    {
        render();
        const App::Yoga::Vec2f pos = combo->get_global_pos();
        simulate_click(pos.x() + combo->width() * 0.25f, pos.y() + combo->height() * 0.5f);
    }

    void type(const char* text)
    {
        click_text();
        type_text(text);
        key_tap(ImGuiKey_Enter);
    }

    const ConfigValue* get_override_original_value(const ConfigItem&, size_t) const override
    {
        return nullptr;
    }

    void
    set_item_value(const ConfigItem&, const ConfigValue& value, const std::vector<size_t>&) override
    {
        ++set_count;
        value.visit([&](const auto& typed) { item->set(typed); });
        if (notify) {
            combo->set_state(*item);
        }
    }

    void set_item_override(const ConfigItem&, bool, size_t) override {}

    std::optional<ConfigItem> item;
    App::ConfigItemComboBox* combo = nullptr;
    int set_count                  = 0;
    bool notify                    = true;
};

} // namespace

TEST_CASE_METHOD(ConfigItemComboBoxFixture, "ConfigItemComboBox: mouse wheel steps a percentage")
{
    make_combo("fill_density");
    REQUIRE(combo->current_index() == 4);

    wheel(1.f);
    REQUIRE(item->get<Percentage>().value == 25.);

    set_value(Percentage{7.});
    REQUIRE(combo->current_index() == -1);
    wheel(1.f);
    REQUIRE(item->get<Percentage>().value == 10.);
    REQUIRE(combo->current_index() == 2);

    set_value(Percentage{7.});
    wheel(-1.f);
    REQUIRE(item->get<Percentage>().value == 5.);
}

TEST_CASE_METHOD(ConfigItemComboBoxFixture, "ConfigItemComboBox: mouse wheel steps an int")
{
    make_combo("support_material_bottom_interface_layers");
    REQUIRE(combo->current_index() == 0);

    wheel(-1.f);
    REQUIRE(set_count == 0);

    wheel(1.f);
    REQUIRE(item->get<int>() == 0);

    set_value(7);
    wheel(1.f);
    REQUIRE(set_count == 0);

    wheel(-1.f);
    REQUIRE(item->get<int>() == 3);
}

TEST_CASE_METHOD(
    ConfigItemComboBoxFixture,
    "ConfigItemComboBox: mouse wheel steps a float but not a percentage of FloatOrPercentage"
)
{
    make_combo("infill_anchor");
    REQUIRE(item->get<FloatOrPercentage>().is_percentage());

    wheel(1.f);
    wheel(-1.f);
    REQUIRE(set_count == 0);

    set_value(FloatOrPercentage{3.});
    wheel(1.f);
    REQUIRE(item->get<FloatOrPercentage>() == FloatOrPercentage{5.});
}

TEST_CASE_METHOD(ConfigItemComboBoxFixture, "ConfigItemComboBox: mouse wheel steps a listed string")
{
    make_combo("filament_type");
    REQUIRE(combo->current_index() == 0);

    wheel(1.f);
    REQUIRE(item->get<std::string>() == "PET");

    set_value(std::string("Custom"));
    REQUIRE(combo->current_index() == -1);
    wheel(1.f);
    wheel(-1.f);
    REQUIRE(set_count == 0);
}

TEST_CASE_METHOD(
    ConfigItemComboBoxFixture,
    "ConfigItemComboBox: mouse wheel on Mixed selects the last item"
)
{
    make_combo("fill_density");
    combo->set_mixed(true);
    combo->set_state(*item);

    wheel(-1.f);
    REQUIRE(item->get<Percentage>().value == 100.);
}

TEST_CASE_METHOD(
    ConfigItemComboBoxFixture,
    "ConfigItemComboBox: shows the choice text for a listed value and the number otherwise"
)
{
    make_combo("support_material_contact_distance");
    REQUIRE(combo->current_label() == "0.2 (detachable)");

    type("0.15");
    REQUIRE(item->get<double>() == 0.15);
    REQUIRE(combo->current_label() == "0.15 mm");

    type("0.1");
    REQUIRE(item->get<double>() == 0.1);
    REQUIRE(combo->current_label() == "0.1 (detachable)");

    wheel(1.f);
    REQUIRE(combo->current_label() == "0.2 (detachable)");
}

TEST_CASE_METHOD(
    ConfigItemComboBoxFixture,
    "ConfigItemComboBox: shows the choice text without a data update"
)
{
    make_combo("support_material_contact_distance");
    notify = false;

    type("0.2");
    REQUIRE(combo->current_label() == "0.2 (detachable)");

    type("0.1");
    REQUIRE(combo->current_label() == "0.1 (detachable)");

    type("0.15");
    REQUIRE(combo->current_label() == "0.15 mm");
}

TEST_CASE_METHOD(
    ConfigItemComboBoxFixture,
    "ConfigItemComboBox: focusing the text edits the number and keeps the choice text"
)
{
    make_combo("support_material_contact_distance");

    click_text();
    REQUIRE(combo->current_label() == "0.2");

    key_tap(ImGuiKey_Escape);
    REQUIRE(combo->current_label() == "0.2 (detachable)");
    REQUIRE(set_count == 0);

    click_text();
    type_text("0.15");
    key_tap(ImGuiKey_Escape);
    REQUIRE(combo->current_label() == "0.2 (detachable)");
    REQUIRE(set_count == 0);
}
