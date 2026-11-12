#pragma once

//	 Class inherited from AbstractConfigManipulation to show warnings using wxWidgets

#include "Slic3r/Biz/Preset/AbstractConfigManipulation.hpp"

class wxWindow;

namespace Slic3r::Domain {
    struct ConfigBox;
}

namespace Slic3r::App::WX {

class ConfigManipulation : public Biz::Preset::AbstractConfigManipulation
{    
    wxWindow* m_msg_dlg_parent{ nullptr };

public:
    ConfigManipulation(std::function<void()> load_config,
        std::function<void(const std::string&, bool toggle, int opt_index)> cb_toggle_field,
        std::function<void(const std::string&, const boost::any&)>  cb_value_change,
        Domain::ConfigBox* local_config = nullptr) :
        Biz::Preset::AbstractConfigManipulation(load_config, cb_toggle_field, cb_value_change, local_config) {}
    ConfigManipulation() {}

    void set_message_dialog_parent(wxWindow* parent) { m_msg_dlg_parent = parent; }

    // just warn user about some configuration conflict(s) and inform about changes
    void warn_user(const std::string& title, const std::string& message) override;

    // warn user about some configuration conflict(s) and ask him about possible changes
    // return true, if user answer is "yes"
    bool ask_user(const std::string& title, const std::string& question) override;

};

}