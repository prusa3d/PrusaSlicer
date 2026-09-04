#pragma once

#include <Slic3r/Biz/ConfigItemContext.hpp>
#include <Slic3r/Biz/IObservableList.hpp>

namespace Slic3r::Biz {

class ConfigBoxObservableList : public IObservableList<ConfigItemContext>
{
public:
    void set_config_box(
        Domain::ConfigBox* observable_config_box,
        const Domain::ConfigBox* original_config_box
    );

    const ConfigItemContext& at(size_t index) const override;

    size_t size() const override;

    void set_value(const std::string_view key, const Domain::ConfigValue& value);

    const Domain::ConfigValue* find(const std::string& name) const;

    bool is_dirty(const std::string& key) const;
    bool is_dirty() const;

    void set_from_original_value(const std::string& key);

private:
    Domain::ConfigBox* m_config_box{nullptr};

    using Items = std::vector<ConfigItemContext>;
    Items m_items;
};

} // namespace Slic3r::Biz
