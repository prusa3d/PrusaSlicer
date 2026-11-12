#pragma once

#include "Slic3r/Biz/Scene/SceneInteractor.hpp"
#include "Slic3r/Domain/Config.hpp"

#include "Slic3r/Biz/IObservableList.hpp"
#include "Slic3r/Biz/OverrideItem.hpp"

namespace Slic3r::Biz {

class ObjectSettingsObservableList : public Biz::IObservableList<OverrideItem>
{
public:
    ObjectSettingsObservableList(Scene::SceneInteractor& scene_interactor);

    virtual const OverrideItem& at(size_t index) const;
    virtual size_t size() const;

    void set_sources(const std::vector<Domain::ConfigBox*>& sources);

    void set_object_settings_source(const Domain::ConfigBox* object_settings_source);

    void set_value(const std::string& key, const Domain::ConfigValue& value);

    void set_override(const std::string& key, bool enabled);

    const Domain::ConfigValue* find_object_value(const std::string& key, size_t index = 0);

private:
    OverrideItem* find_item(const std::string& name);
    void update_overriden(OverrideItem* item);

private:
    Scene::SceneInteractor& m_scene_interactor;

    using OverrideItemPtr = std::unique_ptr<OverrideItem>;
    using ItemMap         = std::unordered_map<std::string, size_t>;
    std::vector<Domain::ConfigBox*> m_sources;

    // Source ConfigBox used to get object settings when ObjectSettingsObservableList
    // is used for volume settings overrides.
    const Domain::ConfigBox* m_object_settings_source{nullptr};

    std::unordered_map<OverrideItem*, std::set<Domain::ConfigBox*>> m_item_sources;
    std::vector<OverrideItemPtr> m_items;
    ItemMap m_item_index; ///< <item name, index of item>
};

} // namespace Slic3r::Biz
