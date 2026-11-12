#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>

#include "Slic3r/Biz/ObservableListSortFilter.hpp"
#include "Slic3r/Biz/ObservableList.hpp"

using namespace Slic3r;
using namespace Slic3r::Biz;

struct SortFilterFixture
{
    SortFilterFixture() :
        source_model(std::make_shared<ObservableList<std::string>>()),
        sort_filter_model(std::make_shared<ObservableListSortFilter<std::string>>()),
        sort_filter_model_end(std::make_shared<ObservableListSortFilter<std::string>>())
    {
        sort_filter_model->set_source_model(source_model.get());
        sort_filter_model_end->set_source_model(sort_filter_model.get());
    }

    void set_filter_fn()
    {
        sort_filter_model->set_filter_fn(
            [](const std::string& str) -> bool { return !str.starts_with('_'); }
        );
    }

    void set_sort_fn()
    {
        sort_filter_model->set_sort_fn(
            [](const std::string& lhs, const std::string& rhs) -> int { return lhs < rhs; }
        );
    }

    void set_group_fn()
    {
        sort_filter_model->set_group_by_fn(
            [](const std::string& item, std::unordered_set<std::string>& seen_keys) -> bool
            {
                const std::string first_leter = {item[0]};
                if (seen_keys.contains(first_leter)) {
                    return true;
                } else {
                    seen_keys.insert(first_leter);
                    return false;
                }
            }
        );
    }

    void sort_filter_matches(std::initializer_list<std::string> initializer_list) const
    {
        REQUIRE(initializer_list.size() == sort_filter_model->size());

        REQUIRE(sort_filter_model->size() == sort_filter_model_end->size());

        size_t index = 0;
        for (const std::string& str : initializer_list) {
            REQUIRE(str == sort_filter_model->at(index));
            REQUIRE(str == sort_filter_model_end->at(index));
            index++;
        }
    }

    UnsharedPointer<ObservableList<std::string>> source_model;
    UnsharedPointer<ObservableListSortFilter<std::string>> sort_filter_model;

private:
    // We are using two filter models to simulate behavior of ListView,
    // essentially sort_filter_model and sort_filter_model_end needs to have same content
    UnsharedPointer<ObservableListSortFilter<std::string>> sort_filter_model_end;
};

TEST_CASE_METHOD(
    SortFilterFixture,
    "[ObservableListSortFilter] no filter, no sort, no group, reset"
)
{
    source_model->reset({"a", "b", "c"});

    sort_filter_matches({"a", "b", "c"});
}

TEST_CASE_METHOD(
    SortFilterFixture,
    "[ObservableListSortFilter] basic filter, no sort, no group, reset"
)
{
    set_filter_fn();

    source_model->reset({"a", "_b", "_c", "b", "c"});

    sort_filter_matches({"a", "b", "c"});
}

TEST_CASE_METHOD(
    SortFilterFixture,
    "[ObservableListSortFilter] no filter, basic sort, no group, reset"
)
{
    set_sort_fn();

    source_model->reset({"c", "b", "d", "a"});

    sort_filter_matches({"a", "b", "c", "d"});
}

TEST_CASE_METHOD(
    SortFilterFixture,
    "[ObservableListSortFilter] no filter, no sort, basic group, reset"
)
{
    set_group_fn();

    source_model->reset({"ca", "ab", "cb", "ac", "ad", "cf"});

    sort_filter_matches({"ca", "ab"});
}

TEST_CASE_METHOD(
    SortFilterFixture,
    "[ObservableListSortFilter] basic filter, basic sort, basic group, reset"
)
{
    set_filter_fn();

    set_sort_fn();

    set_group_fn();

    source_model->reset({"_invalid", "cc", "ca", "bb", "_a", "dg", "_x", "_ea", "da", "ba", "aa", "_foobar", "eb"});

    sort_filter_matches({"aa", "bb", "cc", "dg", "eb"});
}

TEST_CASE_METHOD(
    SortFilterFixture,
    "[ObservableListSortFilter] no filter, no sort, no group, insert"
)
{
    source_model->append("a");
    source_model->append("b");
    source_model->append("c");

    sort_filter_matches({"a", "b", "c"});
}

TEST_CASE_METHOD(
    SortFilterFixture,
    "[ObservableListSortFilter] basic filter, no sort, no group, insert"
)
{
    set_filter_fn();

    source_model->append("a");
    source_model->append("_b");
    source_model->append("b");
    source_model->append("_c");
    source_model->append("c");
    source_model->append("_a");

    sort_filter_matches({"a", "b", "c"});
}

TEST_CASE_METHOD(
    SortFilterFixture,
    "[ObservableListSortFilter] no filter, basic sort, no group, insert"
)
{
    set_sort_fn();

    source_model->append("b");
    source_model->append("a");
    source_model->append("c");

    sort_filter_matches({"a", "b", "c"});
}

TEST_CASE_METHOD(
    SortFilterFixture,
    "[ObservableListSortFilter] no filter, no sort, basic group, insert"
    )
{
    set_group_fn();

    source_model->append("ba");
    source_model->append("ad");
    source_model->append("ch");
    source_model->append("aa");
    source_model->append("ca");


    sort_filter_matches({"ba", "ad", "ch"});
}

TEST_CASE_METHOD(
    SortFilterFixture,
    "[ObservableListSortFilter] basic filter, basic sort, basic group, insert"
    )
{
    set_filter_fn();
    set_sort_fn();
    set_group_fn();

    source_model->append("ba");
    source_model->append("_ad");
    source_model->append("ch");
    source_model->append("aa");
    source_model->append("_ca");


    sort_filter_matches({"aa", "ba", "ch"});
}

TEST_CASE_METHOD(
    SortFilterFixture,
    "[ObservableListSortFilter] no filter, no sort, no group, remove"
    )
{
    source_model->reset({"a", "b", "c", "d"});

    source_model->remove({0, 1});

    sort_filter_matches({"c", "d"});
}

TEST_CASE_METHOD(
    SortFilterFixture,
    "[ObservableListSortFilter] basic filter, no sort, no group, remove"
    )
{
    set_filter_fn();

    source_model->reset({"a", "_b", "_c", "b", "c"});

    source_model->remove({0, 1});

    sort_filter_matches({"b", "c"});
}

TEST_CASE_METHOD(
    SortFilterFixture,
    "[ObservableListSortFilter] no filter, basic sort, no group, remove"
    )
{
    set_sort_fn();

    source_model->reset({"c", "b", "d", "a"});

    source_model->remove({0, 1});

    sort_filter_matches({"a", "d"});
}

TEST_CASE_METHOD(
    SortFilterFixture,
    "[ObservableListSortFilter] no filter, no sort, basic group, remove"
    )
{
    set_group_fn();

    source_model->reset({"ca", "ab", "cb", "ac", "ad", "cf"});

    source_model->remove({0, 1});

    sort_filter_matches({"cb", "ac"});
}

TEST_CASE_METHOD(
    SortFilterFixture,
    "[ObservableListSortFilter] basic filter, basic sort, basic group, remove"
    )
{
    set_filter_fn();

    set_sort_fn();

    set_group_fn();

    source_model->reset({"_invalid", "cc", "ca", "bb", "_a", "dg", "_x", "_ea", "da", "ba", "aa", "_foobar", "eb"});

    source_model->remove({0, 4});

    sort_filter_matches({"aa", "ba", "dg", "eb"});
}

TEST_CASE_METHOD(
    SortFilterFixture,
    "[ObservableListSortFilter] basic filter, no sort, no group, data_changed"
    )
{
    set_filter_fn();

    source_model->reset({"a", "_b", "_c", "b", "c"});

    source_model->set("bb", 2);

    sort_filter_matches({"a", "bb", "b", "c"});
}

TEST_CASE_METHOD(
    SortFilterFixture,
    "[ObservableListSortFilter] no filter, basic sort, no group, data_changed"
    )
{
    set_sort_fn();

    source_model->reset({"c", "b", "d", "a"});

    source_model->set("0b", 2);

    sort_filter_matches({"0b", "a", "b", "c"});
}

TEST_CASE_METHOD(
    SortFilterFixture,
    "[ObservableListSortFilter] no filter, no sort, basic group, data_changed"
    )
{
    set_group_fn();

    source_model->reset({"ca", "ab", "cb", "ac", "ad", "cf"});

    source_model->set("da", 0);

    sort_filter_matches({"da", "ab", "cb"});
}

TEST_CASE_METHOD(
    SortFilterFixture,
    "[ObservableListSortFilter] basic filter, basic sort, basic group, data_changed"
    )
{
    set_filter_fn();

    set_sort_fn();

    set_group_fn();

    source_model->reset({"_invalid", "cc", "ca", "bb", "_a", "dg", "_x", "_ea", "da", "ba", "aa", "_foobar", "eb"});

    source_model->set("cd", 1);

    sort_filter_matches({"aa", "bb", "cd", "dg", "eb"});
}
