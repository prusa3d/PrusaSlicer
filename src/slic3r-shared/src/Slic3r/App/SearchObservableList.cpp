#include "Slic3r/App/SearchObservableList.hpp"

#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/App/AppConfigInteractor.hpp"

#include "Slic3r/Biz/Preset/PresetInteractor.hpp"
#include "Slic3r/Biz/PrintToolConfigObservableList.hpp"
#include "Slic3r/Biz/OverridableConfigBoxObservableList.hpp"
#include <Slic3r/Biz/I18N/I18N.hpp>

namespace {
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

} // namespace

namespace Slic3r::App {

SearchObservableList::SearchObservableList(Biz::Preset::PresetInteractor& preset_interactor) :
    m_preset_interactor(preset_interactor),
    m_preset_changed_listener_scope(preset_interactor, *this)
{
    invalidate_source_items();
}

const Domain::ConfigItem& SearchObservableList::at(size_t index) const
{
    return *m_found_items.at(index);
}

size_t SearchObservableList::size() const
{
    return m_found_items.size();
}

const std::string& SearchObservableList::search_text() const
{
    return m_search_text;
}

void SearchObservableList::set_search_text(const std::string& search_text)
{
    if (m_search_text != search_text) {
        m_search_text         = search_text;
        m_search_text_cleaned = string_to_lower(m_search_text);
        ltrim(m_search_text_cleaned);
        rtrim(m_search_text_cleaned);
        refresh_search();
    }
}

void SearchObservableList::on_preset_selection_changed(
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id,
    Biz::Preset::PresetItemType type
)
{
    invalidate_source_items();
}

void SearchObservableList::on_config_container_selection_changed(
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id
)
{
    invalidate_source_items();
}

void SearchObservableList::invalidate_source_items()
{
    m_source_items.clear();

    auto extract_cbol = [this](std::shared_ptr<const Biz::ConfigBoxObservableList> cbol)
    {
        const size_t cbol_size = cbol->size();
        m_source_items.reserve(m_source_items.size() + cbol_size);

        for (size_t index = 0; index < cbol_size; ++index) {
            const Domain::ConfigItem* item = cbol->at(index).config_item;

            if (item->def().category == Domain::ConfigItemDef::Category::Hidden) {
                continue;
            }
            m_source_items.push_back(item);
        }
    };
    auto extract_printtool_cbol =
        [this](std::shared_ptr<const Biz::PrintToolConfigObservableList> cbol)
    {
        const size_t cbol_size = cbol->size();
        m_source_items.reserve(m_source_items.size() + cbol_size);

        for (size_t index = 0; index < cbol_size; ++index) {
            const Biz::PrintToolItem& item = cbol->at(index);

            if (item.print_item->def().category == Domain::ConfigItemDef::Category::Hidden) {
                continue;
            }

            m_source_items.push_back(item.print_item);
            // Ignore Tool overrides
        }
    };

    auto extract_overridable_cbol =
        [this](std::shared_ptr<const Biz::OverridableConfigBoxObservableList> cbol)
    {
        const size_t cbol_size = cbol->size();
        m_source_items.reserve(m_source_items.size() + cbol_size);

        for (size_t index = 0; index < cbol_size; ++index) {
            const Biz::OverrideItem* item = &cbol->at(index);

            if (item->config_item->def().category == Domain::ConfigItemDef::Category::Hidden) {
                continue;
            }
            m_source_items.push_back(item->config_item);
        }
    };

    extract_cbol(m_preset_interactor.printer_cbi().config_box_list().lock());

    extract_printtool_cbol(m_preset_interactor.print_tool_cbi().observable_list().lock());

    // Only extract first cbol encountered in tools and materials
    if (m_preset_interactor.material_cbi_list().size()) {
        std::shared_ptr<const Biz::OverridableConfigBoxObservableList> material_cbi_list =
            m_preset_interactor.material_cbi_list().at(0).config_box_overridable_list().lock();
        if (material_cbi_list) {
            extract_overridable_cbol(material_cbi_list);
        }
    }

    extract_cbol(
        App::AppServices::instance()
            .app_config_interactor()
            .app_config_cbi()
            .config_box_list()
            .lock()
    );

    refresh_search();
}

struct ScoreItem
{
    const Domain::ConfigItem* config_item{nullptr};
    int score{0};
};

void SearchObservableList::refresh_search()
{
    auto cmp = [](const ScoreItem& a, const ScoreItem& b) { return a.score > b.score; };
    constexpr size_t MaxFoundItems = 10;

    std::list<ScoreItem> scored_items;

    if (!m_search_text_cleaned.empty()) {
        for (const Domain::ConfigItem* item : std::as_const(m_source_items)) {
            const int score = score_item(item);
            if (!score) {
                continue;
            }
            if (scored_items.size() < MaxFoundItems) {
                ScoreItem score_item{item, score};
                std::list<ScoreItem>::iterator pos =
                    std::ranges::lower_bound(scored_items, score_item, cmp);
                scored_items.insert(pos, score_item);
            } else if (score > scored_items.back().score) {
                scored_items.pop_back();
                ScoreItem score_item{item, score};
                std::list<ScoreItem>::iterator pos =
                    std::ranges::lower_bound(scored_items, score_item, cmp);
                scored_items.insert(pos, score_item);
            }
        }
    }

    std::vector<const Domain::ConfigItem*> found_items(scored_items.size());
    for (size_t i = 0; i < found_items.size(); ++i) {
        found_items[i] = scored_items.begin()->config_item;
        scored_items.erase(scored_items.cbegin());
    }

    if (m_found_items != found_items) {
        invoke_listeners<Biz::IListObserver<Domain::ConfigItem>>(
            [&](Biz::IListObserver<Domain::ConfigItem>* l) { l->on_will_be_reset(); }
        );
        m_found_items = found_items;
        invoke_listeners<Biz::IListObserver<Domain::ConfigItem>>(
            [&](Biz::IListObserver<Domain::ConfigItem>* l) { l->on_reset(); }
        );
    }
}

int SearchObservableList::score_item(const Domain::ConfigItem* item)
{
    const std::string& name           = item->name();
    ItemStringsMap::const_iterator it = m_item_strings.find(name);
    if (it == m_item_strings.cend()) {
        m_item_strings.emplace(
            std::make_pair(
                name,
                ItemStrings{
                    string_to_lower(Biz::_u8(item->def().label)),
                    string_to_lower(
                        Biz::_u8(
                            Domain::ConfigItemDef::translate_category(
                                item->def().category,
                                Domain::PrinterTechnology::FFF
                            )
                        )
                    ),
                    string_to_lower(Biz::_u8(item->def().tooltip)),
                    string_to_lower(
                        Biz::_u8(
                            Domain::ConfigItemDef::translate_option_group(item->def().option_group)
                        )
                    )
                }
            )
        );
        it = m_item_strings.find(name);
    }

    ASSERT(it != m_item_strings.cend());

    int score = 0;

    if (!name.empty() && name.find(m_search_text_cleaned) != std::string::npos) {
        score += 15;
    }

    if (!it->second.label.empty()
        && it->second.label.find(m_search_text_cleaned) != std::string::npos)
    {
        score += 15;
    }

    if (!it->second.option_group.empty()
        && it->second.option_group.find(m_search_text_cleaned) != std::string::npos)
    {
        score += 7;
    }

    if (!it->second.category.empty()
        && it->second.category.find(m_search_text_cleaned) != std::string::npos)
    {
        score += 5;
    }

    if (!it->second.tooltip.empty()
        && it->second.tooltip.find(m_search_text_cleaned) != std::string::npos)
    {
        score += 3;
    }

    return score;
}

} // namespace Slic3r::App
