#pragma once

#include "Slic3r/Biz/ConfigBoxObservableList.hpp"
#include <set>

namespace Slic3r::Biz {

/**
 * @brief The ConfigBoxInteractor class acts as Wrapper around ConfigBox
 * and provides IObservableList functionality.
 * @warning All set_value has to go through this Interactor, otherwise
 * there is a high chance of introducing desync.
 */
class ConfigBoxInteractor
{
public:
    class SetAccessor
    {
    public:
        void set_source(
            std::weak_ptr<ConfigBoxObservableList> config_box_list
        );

        void set_value(const std::string& key, const Domain::ConfigValue& value);

        void set_override(const std::string& key, bool enable);

        void set_config_box(
            Domain::ConfigBox* config_box,
            const Domain::ConfigBox* original_config_box = nullptr
        );

        void set_from_original_value(const std::string& key);

    private:
        std::weak_ptr<ConfigBoxObservableList> m_config_box_list;
    };

    ConfigBoxInteractor();
    explicit ConfigBoxInteractor(SetAccessor& set_accessor);

    const Domain::ConfigValue* find(const std::string& name) const;

    std::weak_ptr<ConfigBoxObservableList> config_box_list();

    std::weak_ptr<const ConfigBoxObservableList> config_box_list() const;

private:
    UnsharedPointer<ConfigBoxObservableList> m_config_box_list;
};

} // namespace Slic3r::Biz
