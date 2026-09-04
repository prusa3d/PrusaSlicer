#pragma once

#include "Slic3r/App/Config/ConfigItemControl.hpp"
#include "Slic3r/App/Yoga/InputTextWithSpin.hpp"

namespace Slic3r::Biz {
class IConfigBoxSetter;
} // namespace Slic3r::Biz

namespace Slic3r::App::Yoga {
class IntValidator;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App {

class ConfigItemSpinBox : public ConfigItemControl, public Yoga::InputTextWithSpin
{
public:
    ConfigItemSpinBox(
        size_t index,
        const Domain::ConfigItem& data,
        Biz::IConfigBoxSetter& cb_setter,
        std::vector<size_t> cbi_index
    );

    int value() const;

protected:
    void on_data_update() override;

    void update_value(const Domain::ConfigValue& value);

private:
    Yoga::IntValidator* m_value_validator{nullptr};
};

} // namespace Slic3r::App
