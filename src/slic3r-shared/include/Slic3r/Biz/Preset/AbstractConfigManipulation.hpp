#pragma once

/*	 Interface for validation config options
 *	 and update (enable/disable) IU components
 *	 
 *	 Used for config validation for global config (Print Settings Tab)
 *	 and local config (overrides options on sidebar)
 * */

#include <functional>
#include <boost/any.hpp>
#include "Slic3r/Biz/Preset/IConfigInteractor.hpp"

namespace Slic3r {
    class DynamicPrintConfig;
}

namespace Slic3r::Domain {
    struct ConfigBox;
}

namespace Slic3r::Biz::Preset {

class AbstractConfigManipulation
{
    bool                is_msg_dlg_already_exist{ false };

    bool                m_is_initialized_support_material_overhangs_queried{ false };
    bool                m_support_material_overhangs_queried{ false };

    // function to loading of changed configuration 
    std::function<void()>                                       load_config = nullptr;
    std::function<void (const std::string&, bool toggle, int opt_index)>    cb_toggle_field = nullptr;
    // callback to propagation of changed value, if needed 
    std::function<void(const std::string&, const boost::any&)>  cb_value_change = nullptr;
    Domain::ConfigBox* local_config = nullptr;

public:
    AbstractConfigManipulation(std::function<void()> load_config,
        std::function<void(const std::string&, bool toggle, int opt_index)> cb_toggle_field,
        std::function<void(const std::string&, const boost::any&)>  cb_value_change,
        Domain::ConfigBox* local_config = nullptr) :
        load_config(load_config),
        cb_toggle_field(cb_toggle_field),
        cb_value_change(cb_value_change),
        local_config(local_config) {}
    AbstractConfigManipulation() {}

    ~AbstractConfigManipulation() {
        load_config = nullptr;
        cb_toggle_field = nullptr;
        cb_value_change = nullptr;
    }

    void    apply(IConfigInteractor& config_interactor, const DynamicPrintConfig* new_config);
    void    toggle_field(const std::string& field_key, const bool toggle, int opt_index = -1);

    // FFF print
    void    update_print_fff_config(IConfigInteractor& config_interactor,
                                    const DynamicPrintConfig* initial_config = nullptr, // configuration from the selected preset
                                    const bool is_global_config = false);         // function is used from PrinterTab of from the Object/Part settings
    void    toggle_print_fff_options(IConfigInteractor& config);

    // SLA print
    void    toggle_print_sla_options(IConfigInteractor& config_interactor);

    bool    is_initialized_support_material_overhangs_queried() { return m_is_initialized_support_material_overhangs_queried; }
    void    initialize_support_material_overhangs_queried(bool queried)
    {
        m_is_initialized_support_material_overhangs_queried = true;
        m_support_material_overhangs_queried = queried;
    }

    // just warn user about some configuration conflict(s) and inform about changes
    virtual void warn_user(const std::string& title, const std::string& message) = 0;
    // warn user about some configuration conflict(s) and ask him about possible changes
    // return true, if user answer is "yes"
    virtual bool ask_user(const std::string& title, const std::string& question) = 0;

private:
    void    apply(DynamicPrintConfig* config, const DynamicPrintConfig* new_config);

    // FFF print
    void    update_print_fff_config(DynamicPrintConfig* config,
                                 const DynamicPrintConfig* initial_config = nullptr, // configuration from the selected preset
                                 const bool is_global_config = false);         // function is used from PrinterTab of from the Object/Part settings
    void    toggle_print_fff_options(DynamicPrintConfig* config);

    // SLA print
    void    toggle_print_sla_options(DynamicPrintConfig* config);

};

}