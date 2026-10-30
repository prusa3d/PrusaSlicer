#include <catch2/catch_test_macros.hpp>

#include "Slic3r/Biz/ObservableListSearcher.hpp"
#include "Slic3r/Biz/ObservableList.hpp"

using namespace Slic3r;
using namespace Slic3r::Biz;

// Score fn: returns 1 if search_text is a substring of item, else 0
static int substring_score(std::function<const std::string&(const std::string&)>,
                           const std::string& item,
                           const std::string& search_text)
{
    return item.find(search_text) != std::string::npos ? 1 : 0;
}

struct SearcherFixture
{
    SearcherFixture() :
        source_model(std::make_shared<ObservableList<std::string>>()),
        searcher(std::make_shared<ObservableListSearcher<std::string>>(substring_score))
    {
        searcher->set_source_model(source_model.get());
    }

    void searcher_matches(std::initializer_list<std::string> expected) const
    {
        REQUIRE(searcher->size() == expected.size());
        size_t i = 0;
        for (const std::string& s : expected) {
            REQUIRE(searcher->at(i++) == s);
        }
    }

    UnsharedPointer<ObservableList<std::string>> source_model;
    UnsharedPointer<ObservableListSearcher<std::string>> searcher;
};

TEST_CASE_METHOD(SearcherFixture, "[ObservableListSearcher] empty search text returns all items")
{
    source_model->reset({"apple", "banana", "cherry"});

    searcher_matches({"apple", "banana", "cherry"});
}

TEST_CASE_METHOD(SearcherFixture, "[ObservableListSearcher] single-word search filters and orders by score")
{
    // Exact match scores 2, substring match scores 1 — gives deterministic ordering
    searcher->set_score_fn(
        [](std::function<const std::string&(const std::string&)>,
           const std::string& item,
           const std::string& search_text) -> int
        {
            if (item == search_text)
                return 2;
            if (item.find(search_text) != std::string::npos)
                return 1;
            return 0;
        });

    source_model->reset({"ap", "banana", "apricot", "cherry"});
    searcher->set_search_text("ap");

    REQUIRE(searcher->size() == 2);
    REQUIRE(searcher->at(0) == "ap");       // exact match, score 2
    REQUIRE(searcher->at(1) == "apricot");  // substring match, score 1
}

TEST_CASE_METHOD(SearcherFixture, "[ObservableListSearcher] multi-word search sums scores across words")
{
    // Score fn counts substring occurrences; item scoring higher for more matches
    searcher->set_score_fn(
        [](std::function<const std::string&(const std::string&)>,
           const std::string& item,
           const std::string& word) -> int
        {
            int count  = 0;
            size_t pos = 0;
            while ((pos = item.find(word, pos)) != std::string::npos) {
                ++count;
                ++pos;
            }
            return count;
        });

    source_model->reset({"aab", "ab", "c"});
    // "a b": "aab" scores 2+1=3, "ab" scores 1+1=2, "c" scores 0
    searcher->set_search_text("a b");

    REQUIRE(searcher->size() == 2);
    REQUIRE(searcher->at(0) == "aab");
    REQUIRE(searcher->at(1) == "ab");
}

TEST_CASE_METHOD(SearcherFixture, "[ObservableListSearcher] max_found_items caps result count")
{
    searcher->set_max_found_items(2);
    source_model->reset({"ax", "bx", "cx", "dx"});
    searcher->set_search_text("x");

    REQUIRE(searcher->size() == 2);
}

TEST_CASE_METHOD(SearcherFixture, "[ObservableListSearcher] source update re-triggers search")
{
    source_model->reset({"apple", "banana"});
    searcher->set_search_text("ap");

    REQUIRE(searcher->size() == 1);

    source_model->append("apricot");

    REQUIRE(searcher->size() == 2);
}

TEST_CASE_METHOD(SearcherFixture, "[ObservableListSearcher] search is case-insensitive and trims whitespace")
{
    source_model->reset({"Apple", "BANANA", "cherry"});
    // Normalised search text "apple" should match "Apple" via lowercase comparison
    searcher->set_score_fn(
        [](std::function<const std::string&(const std::string&)>,
           const std::string& item,
           const std::string& search_text) -> int
        {
            std::string lower;
            lower.reserve(item.size());
            std::transform(item.begin(), item.end(), std::back_inserter(lower), ::tolower);
            return lower.find(search_text) != std::string::npos ? 1 : 0;
        });

    searcher->set_search_text("  Apple  ");

    searcher_matches({"Apple"});
}

namespace {
struct UpdateRecorder : IListObserver<std::string>
{
    void on_updated(const IndexRange& index_range) override
    {
        updated.push_back(index_range.from);
    }

    void on_reset() override
    {
        ++reset_count;
    }

    std::vector<size_t> updated;
    int reset_count{0};
};
} // namespace

TEST_CASE_METHOD(SearcherFixture, "[ObservableListSearcher] in-place source update is forwarded")
{
    source_model->reset({"apple", "banana", "apricot"});
    UpdateRecorder recorder;
    searcher->add_listener<IListObserver<std::string>>(&recorder);

    SECTION("empty search text")
    {
        source_model->set("banana split", 1);

        REQUIRE(recorder.reset_count == 0);
        REQUIRE(recorder.updated == std::vector<size_t>{1});
        searcher_matches({"apple", "banana split", "apricot"});
    }

    SECTION("item kept by search text")
    {
        searcher->set_search_text("ap");
        recorder.reset_count = 0;

        source_model->set("apricots", 2);

        REQUIRE(recorder.reset_count == 0);
        REQUIRE(searcher->size() == 2);
        REQUIRE(recorder.updated.size() == 1);
        REQUIRE(searcher->at(recorder.updated.front()) == "apricots");
    }

    SECTION("item filtered out by search text")
    {
        searcher->set_search_text("ap");
        recorder.reset_count = 0;

        source_model->set("banana split", 1);

        REQUIRE(recorder.reset_count == 0);
        REQUIRE(recorder.updated.empty());
    }

    searcher->remove_listener<IListObserver<std::string>>(&recorder);
}
