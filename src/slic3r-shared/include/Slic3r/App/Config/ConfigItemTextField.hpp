#pragma once

#include "Slic3r/App/Config/ConfigItemControl.hpp"
#include "Slic3r/App/Yoga/InputTextField.hpp"
#include "Slic3r/App/Yoga/Validator.hpp"

namespace Slic3r::Biz {
class IConfigBoxSetter;
} // namespace Slic3r::Biz

namespace Slic3r::App {

class ConfigItemTextField : public ConfigItemControl, public Yoga::InputTextField
{
public:
    ConfigItemTextField(
        size_t index,
        const Domain::ConfigItem& data,
        Biz::IConfigBoxSetter& cb_setter,
        std::vector<size_t> cbi_index
    );

protected:
    void on_data_update() override;
    void update_value(const Domain::ConfigValue& value);

private:
    Yoga::Passthrough<Yoga::DoubleValidator> m_double_validator;
    const Domain::ConfigItem* m_last_item{nullptr};
    std::optional<bool> m_is_multiline{std::nullopt};
    std::optional<bool> m_is_full_width{std::nullopt};
};

} // namespace Slic3r::App
