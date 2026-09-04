#pragma once

#include <Slic3r/Domain/Config.hpp>

#include <Slic3r/Biz/Platform/ListenerScope.hpp>
#include "Slic3r/Biz/IObservableList.hpp"
#include "Slic3r/Biz/Preset/PresetInteractor.hpp"

namespace Slic3r::Biz {
class ConfigBoxInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::App {

class SearchObservableList :
    public Biz::IObservableList<Domain::ConfigItem>,
    public Biz::Preset::IPresetChangedListener
{
public:
    explicit SearchObservableList(Biz::Preset::PresetInteractor& preset_interactor);

    const Domain::ConfigItem& at(size_t index) const override;
    size_t size() const override;

    const std::string& search_text() const;
    void set_search_text(const std::string& search_text);

    void on_preset_selection_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id,
        Biz::Preset::PresetItemType type
    ) override;
    void on_config_container_selection_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id
    ) override;

private:
    void invalidate_source_items();
    void refresh_search();
    int score_item(const Domain::ConfigItem* item);

private:
    Biz::Preset::PresetInteractor& m_preset_interactor;

    Biz::ListenerScope<
        Biz::Preset::IPresetChangedListener,
        Biz::Preset::PresetInteractor,
        SearchObservableList>
        m_preset_changed_listener_scope;

    std::string m_search_text;
    std::string m_search_text_cleaned;

    struct ItemStrings
    {
        std::string label;
        std::string category;
        std::string tooltip;
        std::string option_group;
    };

    using ItemStringsMap = std::unordered_map<std::string, ItemStrings>;
    ItemStringsMap m_item_strings;
    std::vector<const Domain::ConfigItem*> m_found_items;
    std::vector<const Domain::ConfigItem*> m_source_items;
};

} // namespace Slic3r::App
