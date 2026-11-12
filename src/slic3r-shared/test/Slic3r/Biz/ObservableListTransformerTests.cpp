#include <catch2/catch_test_macros.hpp>

#include "Slic3r/Biz/ObservableListTransformer.hpp"
#include "Slic3r/Biz/ObservableListSortFilter.hpp"
#include "Slic3r/Biz/ObservableList.hpp"

using namespace Slic3r;
using namespace Slic3r::Biz;

struct TransformerFixture
{
    TransformerFixture() :
        source_model(std::make_shared<ObservableList<int>>()),
        transformer(std::make_shared<ObservableListTransformer<int, std::string>>())
    {
        transformer->set_transform_fn([](const int& v, size_t) { return std::to_string(v); });
        transformer->set_source_model(source_model.get());
    }

    void transformer_matches(std::initializer_list<std::string> expected) const
    {
        REQUIRE(transformer->size() == expected.size());
        size_t i = 0;
        for (const std::string& s : expected) {
            REQUIRE(transformer->at(i++) == s);
        }
    }

    UnsharedPointer<ObservableList<int>> source_model;
    UnsharedPointer<ObservableListTransformer<int, std::string>> transformer;
};

TEST_CASE_METHOD(TransformerFixture, "[ObservableListTransformer] reset transforms all items")
{
    source_model->reset({1, 2, 3});

    transformer_matches({"1", "2", "3"});
}

TEST_CASE_METHOD(TransformerFixture, "[ObservableListTransformer] insert transforms and forwards item")
{
    source_model->reset({1, 3});
    source_model->insert(2, 1);

    transformer_matches({"1", "2", "3"});
}

TEST_CASE_METHOD(TransformerFixture, "[ObservableListTransformer] remove erases from transformed list")
{
    source_model->reset({1, 2, 3, 4});
    source_model->remove({1, 2});

    transformer_matches({"1", "4"});
}

TEST_CASE_METHOD(TransformerFixture, "[ObservableListTransformer] move reorders transformed list")
{
    source_model->reset({1, 2, 3});
    source_model->move(0, 2);

    transformer_matches({"2", "3", "1"});
}

TEST_CASE_METHOD(TransformerFixture, "[ObservableListTransformer] no transform fn set — insert is no-op")
{
    UnsharedPointer<ObservableListTransformer<int, std::string>> t =
        std::make_shared<ObservableListTransformer<int, std::string>>();
    t->set_source_model(source_model.get());

    source_model->append(42);

    REQUIRE(t->size() == 0);
}

TEST_CASE_METHOD(TransformerFixture, "[ObservableListTransformer] chained with SortFilter")
{
    UnsharedPointer<ObservableListSortFilter<std::string>> sort_filter =
        std::make_shared<ObservableListSortFilter<std::string>>();
    sort_filter->set_source_model(transformer.get());
    sort_filter->set_sort_fn([](const std::string& a, const std::string& b) { return a < b; });

    source_model->reset({3, 1, 2});

    REQUIRE(sort_filter->size() == 3);
    REQUIRE(sort_filter->at(0) == "1");
    REQUIRE(sort_filter->at(1) == "2");
    REQUIRE(sort_filter->at(2) == "3");
}
