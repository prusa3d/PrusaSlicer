#include "Slic3r/App/Browser/BrowserLogicConnectSelect.hpp"

#include "Slic3r/App/ResultExport/ExportPathSelect.hpp"
#include "Slic3r/Biz/Network/ServiceConfig.hpp"
#include <Slic3r/Biz/Platform/PlatformServices.hpp>
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/Preset/PresetInteractor.hpp"
#include "Slic3r/Biz/Connect/ConnectMessageHandler.hpp"

#include "Slic3r/Assert.hpp"
#include <nlohmann/json.hpp>

namespace Slic3r::App::Browser {

BrowserLogicConnectSelect::BrowserLogicConnectSelect(Biz::ProjectInteractor& project_interactor) :
    AbstractUploadBrowserLogic(
        Biz::Network::ServiceConfig::instance().connect_select_printer_url(),
        {"_prusaSlicer"},
        "connect_loading",
        "connect_error",
        "Select printer"
    ),
    AbstractConnectRequestHandler(project_interactor)
{}

std::vector<BrowserLogicCommand> BrowserLogicConnectSelect::on_script_message_webview_event(const std::string& message) 
{
     return handle_message(message);
}

std::vector<BrowserLogicCommand> BrowserLogicConnectSelect::on_show_webview_event(bool show)
{
    return {};
}

std::vector<BrowserLogicCommand> BrowserLogicConnectSelect::on_connect_action_select_printer(const std::string& message_data)
{
    ASSERT(false, "SELECT_PRINTER request is not defined for Connect Select");
    return {};
}

std::vector<BrowserLogicCommand> BrowserLogicConnectSelect::on_connect_action_print(const std::string& message_data)
{
    m_success = true;
    m_result = message_data; 
    return {{BrowserLogicCommandType::EndModalOK, {}}};
}

namespace {
struct PrinterData
{
    std::string legacy_model;
    std::vector<double> nozzle_diameters;
    std::vector<bool> filament_abrasive;
    std::vector<bool> high_flow;
    std::vector<std::string> filament_type;
};

PrinterData get_printer_data(const Biz::Preset::PresetInteractor& preset_interactor)
{
    PrinterData res;
    const auto& hw_config = preset_interactor.current_printer_config();
    const auto& selected_preset = preset_interactor.selected_printer_preset();
    
    res.legacy_model = hw_config.legacy_printer_model.value_or("");

    size_t slot_count = hw_config.material_slot_count();
    res.nozzle_diameters.reserve(slot_count);
    res.high_flow.reserve(slot_count);
    res.filament_abrasive.reserve(slot_count);
    res.filament_type.reserve(slot_count);

    size_t slot_idx = 0;
    for (auto it = Slic3r::Domain::Preset::MaterialIterator(hw_config); it.is_valid(); ++it, ++slot_idx) {
        res.nozzle_diameters.push_back(
            Slic3r::Domain::Preset::get_feature<double>(it.tool_config().features, "nozzle_diameter").value_or(0.4)
        );

        res.high_flow.push_back(
            Slic3r::Domain::Preset::get_feature<bool>(it.tool_config().features, "nozzle_high_flow").value_or(false)
        );

        bool abrasive = false;
        std::string f_type = "";

        if (slot_idx < selected_preset.materials.size()) {
            auto [evaluated_mat, is_runtime] = preset_interactor.get_material_preset(
                hw_config.id,
                selected_preset.printer.id,
                selected_preset.print.id,
                slot_idx,
                selected_preset.materials[slot_idx].id
            );
            
            const auto& features = evaluated_mat.get().features;
            
            if (auto tags_it = features.find("$.tags"); tags_it != features.end()) {
                using JsonVector = std::vector<Slic3r::Domain::JsonValue>;
                if (const auto* tags_vec = std::get_if<JsonVector>(&tags_it->second)) {
                    for (const auto& tag_val : *tags_vec) {
                        if (const auto* tag_str = std::get_if<std::string>(&tag_val)) {
                            if (*tag_str == "abrasive") {
                                abrasive = true;
                                break;
                            }
                        }
                    }
                }
            }

            if (auto type_it = features.find("$.type"); type_it != features.end()) {
                if (const auto* type_str = std::get_if<std::string>(&type_it->second)) {
                    f_type = *type_str;
                }
            }
        }
        res.filament_abrasive.push_back(abrasive);
        res.filament_type.push_back(f_type);
    }
    
    return res;
}

PrinterData get_printer_data_from_cbi(const Biz::Preset::PresetInteractor& preset_interactor)
{
    PrinterData res;
    const auto& hw_config = preset_interactor.current_printer_config();
    
    res.legacy_model = hw_config.legacy_printer_model.value_or("");

    size_t slot_count = hw_config.material_slot_count();
    res.nozzle_diameters.reserve(slot_count);
    res.high_flow.reserve(slot_count);
    res.filament_abrasive.reserve(slot_count);
    res.filament_type.reserve(slot_count);

    const auto& material_cbis = preset_interactor.material_cbi_list();

    size_t slot_idx = 0;
    for (auto it = Slic3r::Domain::Preset::MaterialIterator(hw_config); it.is_valid(); ++it, ++slot_idx) {
        res.nozzle_diameters.push_back(
            Slic3r::Domain::Preset::get_feature<double>(it.tool_config().features, "nozzle_diameter").value_or(0.4)
        );

        res.high_flow.push_back(
            Slic3r::Domain::Preset::get_feature<bool>(it.tool_config().features, "nozzle_high_flow").value_or(false)
        );

        bool abrasive = false;
        std::string f_type;

        if (slot_idx < material_cbis.size()) {
            const auto& cbi = material_cbis.at(slot_idx);

            if (const Domain::ConfigValue* val = cbi.find("filament_abrasive")) {
                if (val->holds_alternative<bool>()) {
                    abrasive = val->get<bool>();
                } else if (val->holds_alternative<std::vector<bool>>()) {
                    auto v = val->get<std::vector<bool>>();
                    if (!v.empty()) abrasive = v.front();
                }
            }

            if (const Domain::ConfigValue* val = cbi.find("filament_type")) {
                if (val->holds_alternative<std::string>()) {
                    f_type = val->get<std::string>();
                } else if (val->holds_alternative<std::vector<std::string>>()) {
                    auto v = val->get<std::vector<std::string>>();
                    if (!v.empty()) f_type = v.front();
                }
            }
        }

        res.filament_abrasive.push_back(abrasive);
        res.filament_type.push_back(f_type);
    }
    
    return res;
}
}

std::vector<BrowserLogicCommand> BrowserLogicConnectSelect::on_connect_action_webapp_ready(const std::string& message_data)
{
    return request_compatible_printers_FFF();
}

std::vector<BrowserLogicCommand> BrowserLogicConnectSelect::request_compatible_printers_FFF()
{
    /*
    {
        "printerUuid": "",
        "printerModel": "MK4IS",
        "nozzle_diameter": [0.4],
        "material": ["PLA"],
        "filename": "Shape-Box_0.4n_0.2mm_PLA_MK4IS_20m.bgcode",
        "filament_abrasive": [false],
        "high_flow": [false],
        "multiple_beds": false
    }

    {
        "printerUuid": "",
        "printerModel": "XL5IS",
        "nozzle_diameter": [0.4, 0.4, 0.4, 0.4, 0.4],
        "material": ["PLA", "PLA", "PLA", "PLA", "PLA"],
        "filename": "Shape-Box_0.4n_0.2mm_{printing_filament_types}_XLIS_{print_time}.bgcode",
        "filament_abrasive": [false, false, false, false, false],
        "high_flow": [false, false, false, false, false],
        "multiple_beds": false
    }
    */

    const auto filename_data = ExportPathSelect::get_export_name_data(m_project_interactor);
    const std::string uuid = m_project_interactor.connect_message_handler().uuid_for_upload();

    // We have implemented 2 methods how to obtain data
    // get_printer_data looks into evaluated presets and should work with data from material database
    // get_printer_data_from_cbi looks into cbi preset
    // In future get_printer_data should be better approach but now (when Connect FE expects same data as in 2.9+)
    // get_printer_data_from_cbi seems to return better results.

    //const PrinterData p_data = get_printer_data(m_project_interactor.preset_interactor());
    const PrinterData p_data = get_printer_data_from_cbi(m_project_interactor.preset_interactor());
    nlohmann::json payload = {
        {"printerUuid", uuid},
        {"filename", filename_data.filename},
        {"printerModel", p_data.legacy_model},
        {"nozzle_diameter", p_data.nozzle_diameters},
        {"filament_abrasive", p_data.filament_abrasive},
        {"high_flow", p_data.high_flow},
        {"material", p_data.filament_type},
        {"multiple_beds", false} // TODO when we have multiple bed export
    };
    
    std::string placeholder_script = fmt::format(
        "window._prusaConnect_v2.requestCompatiblePrinter({})", 
        payload.dump()
    );

    return {{BrowserLogicCommandType::RunScript, placeholder_script}};    
}

std::vector<BrowserLogicCommand> BrowserLogicConnectSelect::request_compatible_printers_SLA()
{
    // TODO
    return {};
}

std::vector<BrowserLogicCommand> BrowserLogicConnectSelect::on_webview_reload_event(const std::string& message_data)
{
    return {{BrowserLogicCommandType::LoadURL, m_url}};
}

std::vector<BrowserLogicCommand> BrowserLogicConnectSelect::on_connect_action_close_dialog(const std::string& message_data)
{
    return {{BrowserLogicCommandType::EndModalOK, {}}};
}

std::vector<BrowserLogicCommand> BrowserLogicConnectSelect::on_connect_action_log_in_in_browser(const std::string& data)
{
    return {};
}

} // namespace Slic3r::App::Browser 