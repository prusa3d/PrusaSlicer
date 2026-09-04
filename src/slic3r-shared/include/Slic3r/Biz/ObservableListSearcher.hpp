#pragma once

#include "Slic3r/Biz/IObservableList.hpp"

#include <vector>
#include <sstream>
#include <algorithm>
#include <numeric>

namespace Slic3r::Biz {

template <class Data>
class ObservableListSearcher : public Biz::IListObserver<Data>, public IObservableList<Data>
{
    struct ScoreItem
    {
        const Data* data{nullptr};
        int score{0};
    };

    inline std::string string_to_lower(std::string_view input)
    {
        std::string output;
        output.reserve(input.size());
        std::transform(
            input.cbegin(),
            input.cend(),
            std::back_inserter(output),
            [](const char ch) { return std::tolower(ch); }
        );

        return output;
    }

    inline void ltrim(std::string& string)
    {
        string.erase(
            string.begin(),
            std::find_if(
                string.begin(),
                string.end(),
                [](unsigned char ch) { return !std::isspace(ch); }
            )
        );
    }

    // Trim from the end (in place)
    inline void rtrim(std::string& string)
    {
        string.erase(
            std::find_if(
                string.rbegin(),
                string.rend(),
                [](unsigned char ch) { return !std::isspace(ch); }
            ).base(),
            string.end()
        );
    }

    inline std::string process_string(const std::string& string)
    {
        std::string lowered = string_to_lower(string);
        ltrim(lowered);
        rtrim(lowered);
        return lowered;
    }

    inline std::vector<std::string> split_words(const std::string& input)
    {
        std::istringstream iss(input);
        std::vector<std::string> words;
        std::string word;

        while (iss >> word) {
            words.push_back(word);
        }

        return words;
    }

public:
    // Returns int scoring value of an item. Higher value -> higher match
    using ScoreFn = std::function<
        int(std::function<const std::string&(const std::string&)> process_string,
            const Data& item,
            const std::string& search_text)>;

    ObservableListSearcher(ScoreFn score_fn) : m_score_fn(score_fn) {}

    virtual ~ObservableListSearcher()
    {
        if (m_source_model.is_valid()) {
            m_source_model->template remove_listener<Biz::IListObserver<Data>>(this);
        }
    }

    const Data& at(size_t index) const override
    {
        return *m_found_items.at(index);
    }

    size_t size() const override
    {
        return m_found_items.size();
    }

    const std::string& search_text() const
    {
        return m_search_text;
    }

    void set_search_text(const std::string& search_text)
    {
        if (m_search_text != search_text) {
            m_search_text = search_text;

            m_search_text_cleaned = split_words(search_text);
            for (std::string& word : m_search_text_cleaned) {
                word = string_to_lower(word);
            }

            search();
        }
    }

    void set_score_fn(ScoreFn score_fn)
    {
        m_score_fn = score_fn;
        search();
    }

    const std::string& processed_string(const std::string& string)
    {
        std::unordered_map<std::string, std::string>::iterator string_it =
            m_data_strings.find(string);
        if (string_it == m_data_strings.end()) {
            m_data_strings[string] = process_string(string);
            return m_data_strings.at(string);
        } else {
            return string_it->second;
        }
    }

    void on_inserted(const Data& data, size_t index) override
    {
        search();
    }

    void on_removed(const IndexRange& index_range) override
    {
        search();
    }

    void on_updated(const IndexRange& index_range) override
    {
        search();
    }

    void on_reset() override
    {
        search();
    }

    void on_moved(size_t from, size_t to) override
    {
        search();
    }

    int max_found_items() const
    {
        return m_max_found_items;
    }

    void set_max_found_items(int max_found_items)
    {
        if (m_max_found_items != max_found_items) {
            m_max_found_items = max_found_items;
            search();
        }
    }

    void search()
    {
        // If we do not have model just clear up everything and early return
        if (!m_source_model.is_valid()) {
            if (!m_found_items.empty()) {
                this->template invoke_listeners<Biz::IListObserver<Data>>(
                    [&](Biz::IListObserver<Data>* l) { l->on_will_be_reset(); }
                );
                m_found_items.clear();
                this->template invoke_listeners<Biz::IListObserver<Data>>(
                    [&](Biz::IListObserver<Data>* l) { l->on_reset(); }
                );
            }
            return;
        }

        auto cmp = [](const ScoreItem& a, const ScoreItem& b) { return a.score > b.score; };

        std::vector<const Data*> found_items;

        if (m_search_text_cleaned.empty()) {
            found_items.resize(m_source_model->size());
            for (size_t i = 0; i < m_source_model->size(); ++i) {
                found_items[i] = &m_source_model->at(i);
            }
        } else {
            std::list<ScoreItem> scored_items;
            for (size_t i = 0; i < m_source_model->size(); ++i) {
                const Data* item = &m_source_model->at(i);
                int score        = std::accumulate(
                    m_search_text_cleaned.cbegin(),
                    m_search_text_cleaned.cend(),
                    0,
                    [&](int sum, const std::string& search_text)
                    {
                        return sum
                            + m_score_fn(
                                   [this](const std::string& input) -> const std::string&
                                   { return processed_string(input); },
                                   *item,
                                   search_text
                            );
                    }
                );
                if (!score) {
                    continue;
                }
                if (int(scored_items.size()) < m_max_found_items) {
                    ScoreItem score_item{item, score};
                    auto pos = std::ranges::lower_bound(scored_items, score_item, cmp);
                    scored_items.insert(pos, score_item);
                } else if (score > scored_items.back().score) {
                    scored_items.pop_back();
                    ScoreItem score_item{item, score};
                    auto pos = std::ranges::lower_bound(scored_items, score_item, cmp);
                    scored_items.insert(pos, score_item);
                }
            }

            found_items.resize(scored_items.size());
            for (size_t i = 0; i < found_items.size(); ++i) {
                found_items[i] = scored_items.begin()->data;
                scored_items.erase(scored_items.cbegin());
            }
        }

        if (m_found_items != found_items) {
            this->template invoke_listeners<Biz::IListObserver<Data>>(
                [&](Biz::IListObserver<Data>* l) { l->on_will_be_reset(); }
            );
            m_found_items = found_items;
            this->template invoke_listeners<Biz::IListObserver<Data>>(
                [&](Biz::IListObserver<Data>* l) { l->on_reset(); }
            );
        }
    }

    IObservableList<Data>* source_model() const
    {
        return m_source_model;
    }

    void set_source_model(IObservableList<Data>* source_model)
    {
        set_source_model(WeakerPointer<IObservableList<Data>>{source_model});
    }

    template <
        typename Derived,
        typename = std::enable_if_t<std::is_base_of_v<IObservableList<Data>, Derived>>>
    void set_source_model(const std::weak_ptr<Derived>& source_model)
    {
        set_source_model(
            WeakerPointer<IObservableList<Data>>{
                std::static_pointer_cast<IObservableList<Data>>(source_model.lock())
            }
        );
    }

    void set_source_model(const WeakerPointer<IObservableList<Data>>& source_model)
    {
        if (m_source_model.get() != source_model.get()) {
            if (m_source_model.is_valid()) {
                m_source_model->template remove_listener<Biz::IListObserver<Data>>(this);
            }

            m_source_model = source_model;
            if (m_source_model.get()) {
                m_source_model->template add_listener<Biz::IListObserver<Data>>(this);
            }

            on_reset();
        }
    }

private:
    using Item = std::pair<const Data*, int>;

    std::unordered_map<std::string, std::string> m_data_strings;

    std::string m_search_text;
    std::vector<std::string> m_search_text_cleaned;

    int m_max_found_items{10};

    ScoreFn m_score_fn;

    WeakerPointer<IObservableList<Data>> m_source_model{nullptr};
    std::vector<const Data*> m_found_items;
};

} // namespace Slic3r::Biz
