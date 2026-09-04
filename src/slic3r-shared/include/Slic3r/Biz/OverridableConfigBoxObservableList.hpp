#pragma once

#include "Slic3r/Biz/OverrideItem.hpp"
#include "Slic3r/Biz/IObservableList.hpp"

namespace Slic3r::Domain {
struct ConfigValue;
} // namespace Slic3r::Domain

namespace Slic3r::Biz {

class OverridableConfigBoxObservableList : public IObservableList<OverrideItem>
{
public:
    void
    set_config_box(Domain::ConfigBox* config_box, const Domain::ConfigBox* original_config_box);

    void set_value(const std::string_view key, const Domain::ConfigValue& value);

    void set_override(const std::string& key, bool enable);

    std::pair<const Domain::ConfigValue*, std::optional<bool>> find(const std::string& name) const;

    const OverrideItem& at(size_t index) const override;
    size_t size() const override;

    bool is_dirty(const std::string& key) const;
    bool is_dirty() const;
    void set_from_original_value(const std::string& key);

private:
    using Items = std::vector<Biz::OverrideItem>;

    Domain::ConfigBox* m_config_box{nullptr};
    Items m_items;
};

} // namespace Slic3r::Biz
