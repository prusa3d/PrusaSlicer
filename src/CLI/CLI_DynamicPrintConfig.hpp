#pragma once

#include "libslic3r/Config.hpp"

namespace Slic3r::CLI
{
enum class Type
{
    Input,
    Overrides,
    Transformations,
    Misc,
    Actions,
    Undef
};

class CLI_DynamicPrintConfig : public DynamicPrintConfig
{
public:
    CLI_DynamicPrintConfig() {}
    CLI_DynamicPrintConfig(Type type, const ConfigDef* config_def) : 
        m_type (type),
        m_config_def (config_def) {}
    CLI_DynamicPrintConfig(const CLI_DynamicPrintConfig& other) : 
        DynamicPrintConfig(other),
        m_type (other.type()),
        m_config_def (other.def()) {}

    // Overrides ConfigBase::def(). Static configuration definition. Any value stored into this ConfigBase shall have its definition here.
    const ConfigDef* def() const override { return m_config_def; }

    // Verify whether the opt_key has not been obsoleted or renamed.
    // Both opt_key and value may be modified by handle_legacy().
    // If the opt_key is no more valid in this version of Slic3r, opt_key is cleared by handle_legacy().
    // handle_legacy() is called internally by set_deserialize().
    void            handle_legacy(t_config_option_key& opt_key, std::string& value) const override {
        if (m_type == Type::Overrides)
            DynamicPrintConfig::handle_legacy(opt_key, value);
    }

    Type            type() const { return m_type; }

private:
    Type                m_type          { Type::Undef };
    const ConfigDef*    m_config_def    { nullptr };
};
}
