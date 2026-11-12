#pragma once

#include "Slic3r/Domain/Config.hpp"

#include "Slic3r/Biz/DataObserver.hpp"

namespace Slic3r::Biz {
class IConfigBoxSetter;
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::App::Yoga {
class Item;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App {

class ConfigItemControl : public Biz::DataObserver<Domain::ConfigItem>
{
public:
    explicit ConfigItemControl(
        size_t index,
        const Domain::ConfigItem& data,
        Biz::IConfigBoxSetter& cb_setter,
        std::vector<size_t> cbi_index
    );

    static ConfigItemControl* config_item_control_factory(
        Yoga::Item* container,
        size_t child_index,
        size_t data_index,
        const Domain::ConfigItem& item,
        Biz::IConfigBoxSetter& cb_setter,
        std::vector<size_t> cbi_index
    );

    bool mixed() const;
    void set_mixed(bool mixed);

    std::optional<bool> overriden() const;
    void set_overriden(std::optional<bool> overriden);

    int location_index() const;
    void set_location_index(int location_index);

    static void set_project_interactor(Biz::ProjectInteractor* project_interactor);

protected:
    std::optional<std::string> default_value() const;

    std::string tooltip_text() const;

    Biz::ProjectInteractor* project_interactor() const;

    void set_item_value(const Domain::ConfigValue& value);

protected:
    Biz::IConfigBoxSetter& m_cbi_container;
    std::vector<size_t> m_cbi_index;

private:
    static Biz::ProjectInteractor* m_project_interactor;

    bool m_mixed         = false;
    int m_location_index = 0;
    std::optional<bool> m_overriden;
};

} // namespace Slic3r::App
