#include <catch2/catch_test_macros.hpp>

#include "ImGuiFixture.hpp"

#include <Slic3r/App/Yoga/ComboBoxListViewSelection.hpp>
#include <Slic3r/App/Yoga/ListView.hpp>
#include <Slic3r/App/Yoga/RootItem.hpp>
#include <Slic3r/Biz/BatchObservableList.hpp>
#include <Slic3r/Biz/DataObserver.hpp>
#include <Slic3r/Biz/ObservableListWithSelection.hpp>

using namespace Slic3r;
using namespace Slic3r::App::Yoga;

struct DummyData
{
    std::string name;
};

namespace {
void compare_combo_items(const ComboBox& combo, const std::vector<std::string>& expected)
{
    REQUIRE(combo.items().size() == expected.size());
    for (size_t i = 0; i < expected.size(); ++i) {
        REQUIRE(combo.items()[i] == expected[i]);
    }
}
} // namespace

struct ProfileData
{
    std::shared_ptr<Biz::ObservableListWithSelection<DummyData>> options;
};

class ProfileComboItem :
    public ComboBoxListViewSelection<DummyData>,
    public Biz::DataObserver<ProfileData>
{
public:
    ProfileComboItem(size_t index, const ProfileData& data)
        : Biz::DataObserver<ProfileData>(index, data)
    {
        set_get_name_fn([](const DummyData* d) { return d->name; });
        rebind();
    }

protected:
    void on_data_update() override { rebind(); }

private:
    void rebind() { set_source_list(m_state->options.get()); }
};

TEST_CASE_METHOD(ImGuiFixture, "ComboBoxListViewSelection binds items and selection")
{
    Biz::ObservableListWithSelection<DummyData> selList;
    ComboBoxListViewSelection<DummyData> combo;
    combo.set_get_name_fn([](const DummyData* d) { return d->name; });

    selList.items().set_items({{"a"}, {"b"}, {"c"}});
    selList.set_selected_index(1);
    combo.set_source_list(&selList);

    REQUIRE(combo.items().size() == 3);
    compare_combo_items(combo, {"a", "b", "c"});
    REQUIRE(combo.current_index() == 1);
}

TEST_CASE_METHOD(ImGuiFixture, "ComboBoxListViewSelection initial selection synced")
{
    Biz::ObservableListWithSelection<DummyData> selList;
    ComboBoxListViewSelection<DummyData> combo;
    combo.set_get_name_fn([](const DummyData* d) { return d->name; });

    selList.items().set_items({{"a"}, {"b"}, {"c"}});
    selList.set_selected_index(2);
    combo.set_source_list(&selList);

    REQUIRE(combo.current_index() == static_cast<int>(selList.selected_index()));
}

TEST_CASE_METHOD(
    ImGuiFixture,
    "ComboBoxListViewSelection on_list_selection_changed updates current_index"
)
{
    Biz::ObservableListWithSelection<DummyData> selList;
    ComboBoxListViewSelection<DummyData> combo;
    combo.set_get_name_fn([](const DummyData* d) { return d->name; });

    selList.items().set_items({{"a"}, {"b"}, {"c"}});
    selList.set_selected_index(0);
    combo.set_source_list(&selList);

    selList.set_selected_index(2);
    REQUIRE(combo.current_index() == 2);
}

TEST_CASE_METHOD(ImGuiFixture, "ComboBoxListViewSelection items updated after set_items")
{
    Biz::ObservableListWithSelection<DummyData> selList;
    ComboBoxListViewSelection<DummyData> combo;
    combo.set_get_name_fn([](const DummyData* d) { return d->name; });

    selList.items().set_items({{"a"}, {"b"}});
    selList.set_selected_index(0);
    combo.set_source_list(&selList);

    selList.items().set_items({{"a"}, {"b"}, {"c"}});

    REQUIRE(combo.items().size() == 3);
    REQUIRE(combo.items()[2] == "c");
}

TEST_CASE_METHOD(ImGuiFixture, "ComboBoxListViewSelection items reduced after set_items")
{
    Biz::ObservableListWithSelection<DummyData> selList;
    ComboBoxListViewSelection<DummyData> combo;
    combo.set_get_name_fn([](const DummyData* d) { return d->name; });

    selList.items().set_items({{"a"}, {"b"}, {"c"}});
    selList.set_selected_index(0);
    combo.set_source_list(&selList);

    selList.items().set_items({{"a"}, {"c"}});

    compare_combo_items(combo, {"a", "c"});
}

// SPE-3551: set_source_list with pre-populated list must correctly populate m_items.
// Before the fix, calling set_source_list on a list that already had items resulted in
// m_items being empty because on_reset() was not forced after listener registration.
TEST_CASE_METHOD(
    ImGuiFixture,
    "ComboBoxListViewSelection SPE-3551 set_source_list with pre-populated list"
)
{
    Biz::ObservableListWithSelection<DummyData> selList;
    ComboBoxListViewSelection<DummyData> combo;
    combo.set_get_name_fn([](const DummyData* d) { return d->name; });

    selList.items().set_items({{"a"}, {"b"}, {"c"}});
    selList.set_selected_index(1);

    combo.set_source_list(&selList);

    REQUIRE(combo.items().size() == 3);
    compare_combo_items(combo, {"a", "b", "c"});
    REQUIRE(combo.current_index() == 1);
}

TEST_CASE_METHOD(
    ImGuiFixture,
    "ComboBoxListViewSelection SPE-3551 rebind to second pre-populated list"
)
{
    Biz::ObservableListWithSelection<DummyData> selList1;
    Biz::ObservableListWithSelection<DummyData> selList2;
    ComboBoxListViewSelection<DummyData> combo;
    combo.set_get_name_fn([](const DummyData* d) { return d->name; });

    selList1.items().set_items({{"x"}});
    selList1.set_selected_index(0);
    combo.set_source_list(&selList1);
    REQUIRE(combo.items().size() == 1);

    selList2.items().set_items({{"a"}, {"b"}, {"c"}});
    selList2.set_selected_index(2);
    combo.set_source_list(&selList2);

    REQUIRE(combo.items().size() == 3);
    compare_combo_items(combo, {"a", "b", "c"});
    REQUIRE(combo.current_index() == 2);
}

TEST_CASE_METHOD(ImGuiFixture, "ComboBoxListViewSelection set_source_list nullptr clears items")
{
    Biz::ObservableListWithSelection<DummyData> selList;
    ComboBoxListViewSelection<DummyData> combo;
    combo.set_get_name_fn([](const DummyData* d) { return d->name; });

    selList.items().set_items({{"a"}, {"b"}, {"c"}});
    selList.set_selected_index(0);
    combo.set_source_list(&selList);
    REQUIRE(combo.items().size() == 3);

    combo.set_source_list(nullptr);
    REQUIRE(combo.items().empty());
}

// ListView calls set_source_list on an already- populated ObservableListWithSelection — fixed by SPE-3551.
TEST_CASE_METHOD(
    ImGuiFixture,
    "ComboBoxListViewSelection ListView of BatchObservableList of ObservableListWithSelection"
)
{
    Biz::BatchObservableList<ProfileData> profiles;

    {
        RootItem tree;
        auto* list_view = tree.emplace_back<ListView<ProfileComboItem, ProfileData>>();
        list_view->set_source_list(&profiles);

        // First load: opts_a is pre-populated before set_items fires on_reset.
        // ListView creates a ProfileComboItem and calls set_source_list(opts_a)
        auto opts_a = std::make_shared<Biz::ObservableListWithSelection<DummyData>>();
        opts_a->items().set_items({{"x"}});
        opts_a->set_selected_index(0);
        profiles.set_items({{opts_a}});

        REQUIRE(list_view->list_item_count() == 1);
        compare_combo_items(*list_view->item_at(0), {"x"});
        REQUIRE(list_view->item_at(0)->current_index() == 0);

        // Second load: opts_b is pre-populated when on_reset fires.
        // ListView reuses the existing item and calls on_data_update -> set_source_list(opts_b).
        auto opts_b = std::make_shared<Biz::ObservableListWithSelection<DummyData>>();
        opts_b->items().set_items({{"a"}, {"b"}, {"c"}});
        opts_b->set_selected_index(1);
        profiles.set_items({{opts_b}});

        compare_combo_items(*list_view->item_at(0), {"a", "b", "c"});
        REQUIRE(list_view->item_at(0)->current_index() == 1);
    }
}
