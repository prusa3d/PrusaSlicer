#include <catch2/catch_test_macros.hpp>

#include "ImGuiFixture.hpp"

#include <Slic3r/App/Yoga/ComboBoxListView.hpp>
#include <Slic3r/Biz/ObservableList.hpp>

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

TEST_CASE_METHOD(ImGuiFixture, "ComboBoxListView after ObservableList reset")
{
    Biz::ObservableList<DummyData> list;
    ComboBoxListView<DummyData> combo;
    combo.set_get_name_fn([](const DummyData* d) { return d->name; });

    list.reset({{"a"}, {"b"}, {"c"}});
    combo.set_source_list(&list);

    compare_combo_items(combo, {"a", "b", "c"});
}

TEST_CASE_METHOD(ImGuiFixture, "ComboBoxListView before ObservableList reset")
{
    Biz::ObservableList<DummyData> list;
    ComboBoxListView<DummyData> combo;
    combo.set_get_name_fn([](const DummyData* d) { return d->name; });

    combo.set_source_list(&list);
    list.reset({{"x"}, {"y"}});

    compare_combo_items(combo, {"x", "y"});
}

TEST_CASE_METHOD(ImGuiFixture, "ComboBoxListView empty source list")
{
    Biz::ObservableList<DummyData> list;
    ComboBoxListView<DummyData> combo;
    combo.set_get_name_fn([](const DummyData* d) { return d->name; });

    combo.set_source_list(&list);

    REQUIRE(combo.items().empty());
}

TEST_CASE_METHOD(ImGuiFixture, "ComboBoxListView ObservableList::append")
{
    Biz::ObservableList<DummyData> list;
    ComboBoxListView<DummyData> combo;
    combo.set_get_name_fn([](const DummyData* d) { return d->name; });

    combo.set_source_list(&list);
    list.append({"d"});
    list.append({"e"});

    compare_combo_items(combo, {"d", "e"});
}

TEST_CASE_METHOD(ImGuiFixture, "ComboBoxListView ObservableList::insert at front")
{
    Biz::ObservableList<DummyData> list;
    ComboBoxListView<DummyData> combo;
    combo.set_get_name_fn([](const DummyData* d) { return d->name; });

    list.reset({{"b"}, {"c"}});
    combo.set_source_list(&list);
    list.insert({"a"}, 0);

    compare_combo_items(combo, {"a", "b", "c"});
}

TEST_CASE_METHOD(ImGuiFixture, "ComboBoxListView ObservableList::insert at middle")
{
    Biz::ObservableList<DummyData> list;
    ComboBoxListView<DummyData> combo;
    combo.set_get_name_fn([](const DummyData* d) { return d->name; });

    list.reset({{"a"}, {"c"}});
    combo.set_source_list(&list);
    list.insert({"b"}, 1);

    compare_combo_items(combo, {"a", "b", "c"});
}

TEST_CASE_METHOD(ImGuiFixture, "ComboBoxListView ObservableList::remove single")
{
    Biz::ObservableList<DummyData> list;
    ComboBoxListView<DummyData> combo;
    combo.set_get_name_fn([](const DummyData* d) { return d->name; });

    list.reset({{"a"}, {"b"}, {"c"}});
    combo.set_source_list(&list);
    list.remove({1});

    compare_combo_items(combo, {"a", "c"});
}

TEST_CASE_METHOD(ImGuiFixture, "ComboBoxListView ObservableList::remove range")
{
    Biz::ObservableList<DummyData> list;
    ComboBoxListView<DummyData> combo;
    combo.set_get_name_fn([](const DummyData* d) { return d->name; });

    list.reset({{"a"}, {"b"}, {"c"}, {"d"}});
    combo.set_source_list(&list);
    list.remove({1, 2});

    compare_combo_items(combo, {"a", "d"});
}

TEST_CASE_METHOD(ImGuiFixture, "ComboBoxListView ObservableList::set updates label")
{
    Biz::ObservableList<DummyData> list;
    ComboBoxListView<DummyData> combo;
    combo.set_get_name_fn([](const DummyData* d) { return d->name; });

    list.reset({{"a"}, {"b"}, {"c"}});
    combo.set_source_list(&list);
    list.set({"Z"}, 1);

    REQUIRE(combo.items().size() == 3);
    REQUIRE(combo.items()[1] == "Z");
}

TEST_CASE_METHOD(ImGuiFixture, "ComboBoxListView ObservableList::reset replaces items")
{
    Biz::ObservableList<DummyData> list;
    ComboBoxListView<DummyData> combo;
    combo.set_get_name_fn([](const DummyData* d) { return d->name; });

    combo.set_source_list(&list);
    list.reset({{"a"}, {"b"}});
    list.reset({{"x"}, {"y"}, {"z"}});

    compare_combo_items(combo, {"x", "y", "z"});
}

TEST_CASE_METHOD(ImGuiFixture, "ComboBoxListView ObservableList::move preserves size")
{
    Biz::ObservableList<DummyData> list;
    ComboBoxListView<DummyData> combo;
    combo.set_get_name_fn([](const DummyData* d) { return d->name; });

    list.reset({{"a"}, {"b"}, {"c"}});
    combo.set_source_list(&list);

    REQUIRE(combo.items().size() == 3);

    list.move(0, 2);

    REQUIRE(combo.items().size() == 3);
}

TEST_CASE_METHOD(ImGuiFixture, "ComboBoxListView switch source list")
{
    Biz::ObservableList<DummyData> list1;
    Biz::ObservableList<DummyData> list2;
    ComboBoxListView<DummyData> combo;
    combo.set_get_name_fn([](const DummyData* d) { return d->name; });

    list1.reset({{"a"}, {"b"}});
    combo.set_source_list(&list1);
    REQUIRE(combo.items().size() == 2);

    list2.reset({{"x"}, {"y"}, {"z"}});
    combo.set_source_list(&list2);
    compare_combo_items(combo, {"x", "y", "z"});

    list1.append({"c"});
    REQUIRE(combo.items().size() == 3);
}
