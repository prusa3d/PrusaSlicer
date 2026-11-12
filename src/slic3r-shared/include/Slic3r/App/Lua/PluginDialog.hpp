#pragma once
#include <functional>
#include <map>
#include <memory>
#include <string>

#include "Slic3r/App/Lua/Plugin.hpp"
#include "Slic3r/App/Yoga/Dialog.hpp"
#include "Slic3r/Domain/TextConfiguration.hpp"

namespace Slic3r::App::Lua {

namespace Details {

class IParamControl
{
public:
    virtual ~IParamControl() = default;
    virtual PluginParamValue value() const = 0;
    virtual Yoga::Item& emplace_control(Yoga::Item& parent) = 0;
};

using IParamControlPtr = std::unique_ptr<IParamControl>;
using IParamControlMap = std::map<std::string, IParamControlPtr>;

}

class PluginDialog : public Yoga::Dialog
{
public:
    using ProcessFunction =
        std::function<void(const PluginMeta& meta, const PluginParamValueMap& param_values)>;
    PluginDialog(
        ProcessFunction process_function,
        const Domain::FontList& fonts,
        const std::string& default_font_name
    );

    void show_plugin(const PluginMeta& plugin_meta, const PluginParamValueMap& param_values);

private:
    void emplace_string_param(const PluginParamDef& param, std::optional<PluginParamValue> default_value);
    void emplace_float_param(const PluginParamDef& param, std::optional<PluginParamValue> default_value);
    void emplace_int_param(const PluginParamDef& param, std::optional<PluginParamValue> default_value);
    void emplace_bool_param(const PluginParamDef& param, std::optional<PluginParamValue> default_value);
    void emplace_combobox_param(
        const PluginParamDef& param,
        const std::vector<std::string>& allowed_values,
        const std::optional<PluginParamValue>& default_value
    );
    void emplace_combobox_param(
        const PluginParamDef& param,
        const std::vector<Domain::FontDescriptor>& allowed_values,
        const std::optional<PluginParamValue>& default_value
    );

    Yoga::Item& emplace_row(const char* label=nullptr);
    void style_control(Yoga::Item& ctrl);

    void collect_values(PluginParamValueMap& param_values) const;

private:
    std::optional<PluginMeta> m_meta;
    Details::IParamControlMap m_param_controls;
    ProcessFunction m_process_function;
    Domain::FontList m_fonts;
    std::string m_default_font_name;
};

}
