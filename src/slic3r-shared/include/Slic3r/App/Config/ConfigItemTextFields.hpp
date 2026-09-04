#pragma once

#include "Slic3r/App/Config/ConfigItemControl.hpp"
#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/App/Yoga/Validator.hpp"

namespace Slic3r::Biz {
class IConfigBoxSetter;
} // namespace Slic3r::Biz

namespace Slic3r::App::Yoga {
class InputTextField;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App {

class ConfigItemTextFields : public ConfigItemControl, public Yoga::Item
{
public:
    ConfigItemTextFields(
        size_t index,
        const Domain::ConfigItem& data,
        Biz::IConfigBoxSetter& cb_setter,
        std::vector<size_t> cbi_index
    );

protected:
    void on_data_update() override;

private:
    void reconstruct_fields();
    void update_values();
    void send_data();

private:
    struct Field
    {
        Yoga::InputTextField* textfield{nullptr};
        Yoga::Passthrough<Yoga::DoubleValidator> double_validator;
    };

    std::vector<Field> m_fields;
};

} // namespace Slic3r::App
