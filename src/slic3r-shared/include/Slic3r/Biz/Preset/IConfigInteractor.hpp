#pragma once

#include <functional>
#include <boost/any.hpp>

namespace Slic3r {
class DynamicPrintConfig;
}

namespace Slic3r::Biz::Preset {

/**
 * Universal interface the GUI components use to manipulate an element backed
 * by a DynamicPrintConfig (like Preset or ModelConfig).
 */
class IConfigInteractor
{
public:
    virtual ~IConfigInteractor() = default;

    /**
     * Get actual config (read only)
     * @return config
     */
    virtual const DynamicPrintConfig& config() const = 0;

    /**
     * Set one specific key
     * @param name
     * @param value
     * @param opt_index
     */
    virtual void set_config_value(
        const std::string& name, const boost::any& value, int opt_index
    ) = 0;
    virtual void set_config_num_extruders(size_t num_extruders) = 0;
    virtual void set_config(const DynamicPrintConfig& config) = 0;

    using ModifyFunc = std::function<void(DynamicPrintConfig&)>;
    virtual void modify_config(ModifyFunc mod_fn) = 0;
};

}
