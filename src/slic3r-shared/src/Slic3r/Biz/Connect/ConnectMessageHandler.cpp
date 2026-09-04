#include "Slic3r/Biz/Connect/ConnectMessageHandler.hpp"

#include "Slic3r/Biz/PhysicalPrinter/PhysicalPrinterInteractor.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/UserAccount/UserAccountInteractor.hpp"

#include "Slic3r/Log.hpp"
#include "nlohmann/json.hpp"
#include "jthread/JThread.hpp"

namespace Slic3r::Biz::Connect {

ConnectMessageHandler::ConnectMessageHandler(
        Platform::IMainThreadDispatcher& dispatcher,
        Preset::PresetInteractor& preset_interactor,
        UserAccount::UserAccountInteractor& user_account_interactor,
    PhysicalPrinter::PhysicalPrinterInteractor& physical_printer_interactor
    ) :
    m_dispatcher(dispatcher),
    m_preset_interactor(preset_interactor),
    m_user_account_interactor(user_account_interactor),
    m_physical_printer_interactor(physical_printer_interactor)
{}

void ConnectMessageHandler::dispatch_error(std::string msg)
{
    m_dispatcher.dispatch_on_main_thread([this, msg = std::move(msg)]() {
        this->invoke_listeners<IConnectHandlerListener>([&msg](auto* listener) {
            listener->on_connect_handler_error(msg);
        });
    });
}

namespace {
std::string extract_printer_json_by_uuid(const std::string& message_json, const std::string& uuid)
{
    try {
        auto j = nlohmann::json::parse(message_json);
        if (j.contains("printers") && j["printers"].is_array()) {
            for (const auto& printer : j["printers"]) {
                if (printer.value("uuid", "") == uuid) {
                    return printer.dump();
                }
            }
        }
    } catch (const nlohmann::json::exception& e) {
        SPDLOG_ERROR("Failed to parse Connect printers JSON: {}", e.what());
    }
    return {};
}
} // namespace

void ConnectMessageHandler::handle_select_printer_message(
    const std::string& message_json
)
{
    std::string uuid;
    try {
        nlohmann::json j = nlohmann::json::parse(message_json);
        j.at("uuid").get_to(uuid);
    } catch (const nlohmann::json::exception& e) {
        SPDLOG_ERROR("JSON parsing error: {}", e.what());
        dispatch_error("Invalid payload: Failed to parse Connect message JSON.");
    }
    if (uuid.empty()) {
        SPDLOG_ERROR("Failed to select printer from Connect: Failed to parse Connect message.");
        dispatch_error("Failed to select printer from Connect: Missing UUID.");
        return;
    }

    auto succ_fn = [this, uuid](const std::string& body)
    {
        std::string printer_json = extract_printer_json_by_uuid(body, uuid);

        if (printer_json.empty()) {
            SPDLOG_WARN("Printer with UUID {} not found in Connect message.", uuid);
            dispatch_error("Printer with requested UUID not found in Connect response.");
            return;
        }

        m_dispatcher.dispatch_on_main_thread([this, printer_json = std::move(printer_json)]() {
            this->do_select_printer_from_connect(printer_json);
        });
    };


    auto fail_fn = [this](const std::string& error_msg)
    {
        this->dispatch_error(error_msg);
    };

    fetch_printer_data_async(
        Network::ServiceConfig::instance().connect_printer_list_url(),
        m_user_account_interactor.access_token(), 
        std::move(succ_fn),
        std::move(fail_fn)
    );
}

void ConnectMessageHandler::fetch_printer_data_async(
    const std::string& url,
    const std::string& access_token,
    std::function<void(const std::string&)> success_fn,
    std::function<void(const std::string&)> fail_fn,
    std::function<void(Network::IHttp::Retry, bool&)> retry_fn,
    std::function<void(Network::IHttp::Progress, bool&)> progress_fn
) const
{
    std::thread(
        [url,
         access_token,
         success_fn  = std::move(success_fn),
         fail_fn     = std::move(fail_fn),
         retry_fn    = std::move(retry_fn),
         progress_fn = std::move(progress_fn)]() mutable
        {
            std::unique_ptr<Network::IHttp> http =
                Network::IHttp::create(Network::IHttp::RequestMethod::Get, url, retry_fn);

            if (!access_token.empty()) {
                http->header("Authorization", "Bearer " + access_token);
            }

            http->on_complete(
                [success_fn = std::move(success_fn)](std::string body, unsigned status)
                {
                    if (success_fn) {
                        success_fn(body);
                    }
                });

            http->on_error(
                [fail_fn = std::move(fail_fn)](std::string body, std::string error, unsigned status)
                {
                    SPDLOG_ERROR("Connect HTTP request failed. Status: {}, Error: {}",
                                 status,
                                 error);
                    if (fail_fn) {
                        fail_fn("Connect HTTP request failed: " + error);
                    }
                });
            http->on_progress(
                [progress_fn = std::move(progress_fn)](Network::IHttp::Progress progress,
                                                       bool& cancel)
                {
                    if (progress_fn) {
                        progress_fn(progress, cancel);
                    }
                });

            http->perform_sync(Network::HttpRetryOpt::default_retry());
        })
        .detach();
}

namespace {

bool
compare_feature(const Domain::Preset::FeatureValue& hw_feature, const nlohmann::json& json_feature)
{
    return std::visit(
        [&json_feature](const auto& val) -> bool
        {
            using T = std::decay_t<decltype(val)>;

            if constexpr (std::is_same_v<T, double>) {
                if (!json_feature.is_number()) {
                    return false;
                }
                constexpr double epsilon = 1e-6; 
                return std::abs(json_feature.get<double>() - val) <= epsilon;
            } else if constexpr (std::is_same_v<T, bool>) {
                return json_feature.is_boolean() && json_feature.get<bool>() == val;
            } else if constexpr (std::is_same_v<T, std::string>) {
                return json_feature.is_string() && json_feature.get<std::string>() == val;
            } else if constexpr (std::is_same_v<T, std::nullptr_t>) {
                return json_feature.is_null();
            } else if constexpr (std::is_same_v<T, Domain::JsonArray>) {
                if (!json_feature.is_array() || json_feature.size() != val.size()) {
                    return false;
                }
                for (size_t i = 0; i < val.size(); ++i) {
                    if (!compare_feature(val[i], json_feature[i])) {
                        return false;
                    }
                }
                return true;
            } else if constexpr (std::is_same_v<T, Domain::JsonObject>) {
                if (!json_feature.is_object() || json_feature.size() != val.size()) {
                    return false;
                }
                for (const auto& [key, inner_val] : val) {
                    if (!json_feature.contains(key)
                        || !compare_feature(inner_val, json_feature[key])) {
                        return false;
                    }
                }
                return true;
            }

            return false;
        },
        static_cast<const Domain::JsonVariant&>(hw_feature)
    );
}

const Domain::Preset::HwPrinterConfig*
find_matching_printer_config(const auto& printer_configs_view, const nlohmann::json& j)
{
    std::string j_model        = j.value("model", "");
    std::string j_base_model   = j.value("base_model", "");
    uint8_t j_tool_count       = j.value("tool_count", static_cast<uint8_t>(0));
    size_t j_tools_array_size  = j.contains("tools") ? j["tools"].size() : 0;

    for (const auto& item : printer_configs_view) {
        const auto& hw_config = item.first.get();
        if (hw_config.model.model == j_model
            && hw_config.model.base_model == j_base_model
            && hw_config.tool_count == j_tool_count
            && hw_config.material_slot_count() == j_tools_array_size)
        {
            return &hw_config;
        }
    }
    return nullptr;
}

const Preset::PresetItem* find_matching_preset_item(
    const auto& printer_presets,
    const Domain::Preset::HwPrinterConfig* hw_config
)
{
    ASSERT(hw_config);

    for (size_t i = 0; i < printer_presets.size(); i++) {
        const Preset::PresetItem& item = printer_presets.at(i);

        if (item.hw_printer_config_id == hw_config->id
            && item.origin == Domain::Preset::PresetOrigin::System)
        {
            return &item;
        }
    }

    return nullptr;
}
} // namespace

void ConnectMessageHandler::select_printer_tools_from_connect(const nlohmann::json& j)
{
    if (!j.contains("tools") || !j["tools"].is_object()) {
        return;
    }

    const Domain::Preset::FeatureDefs* vendor_tool_features =
        m_preset_interactor.get_vendor_tool_feature_defs();

    for (const auto& [tool_key, tool_json] : j["tools"].items()) {
        const auto address = Domain::Preset::to_address(tool_key);

        if (!address) {
            SPDLOG_WARN(
                "Connect tool sync: Address could not be parsed from '{}': {}",
                tool_key,
                address.error()
            );
            continue;
        }

        // Nested addresses (e.g. "2.0") are feeder/material slots handled elsewhere, not an error.
        if (address->size() != 1) {
            continue;
        }

        const size_t tool_idx = address->front();

        if (!tool_json.contains("features") || tool_idx >= m_preset_interactor.tool_items().size()) {
            continue;
        }

        const auto& tool_list       = m_preset_interactor.tool_items().at(tool_idx);
        const auto& available_tools = tool_list.items();

        std::string matching_tool_id;

        for (size_t i = 0; i < available_tools.size(); ++i) {
            const Domain::Preset::HwToolConfigDef& tool_def = available_tools.at(i);
            bool matches                                    = true;

            for (const auto& [feature_key, json_feature_val] : tool_json["features"].items()) {
                const Domain::Preset::FeatureValue* expected_value = nullptr;

                if (auto it = tool_def.features.find(feature_key); it != tool_def.features.end()) {
                    expected_value = &it->second.default_value;
                } else if (vendor_tool_features) {
                    if (auto vendor_it = vendor_tool_features->find(feature_key);
                        vendor_it != vendor_tool_features->end())
                    {
                        expected_value = &vendor_it->second.default_value;
                    }
                }

                if (!expected_value) {
                    continue;
                }

                if (!compare_feature(*expected_value, json_feature_val)) {
                    matches = false;
                    break;
                }
            }

            if (matches) {
                matching_tool_id = tool_def.id;
                break;
            }
        }

        if (!matching_tool_id.empty()) {
            m_preset_interactor.select_printer_tool_item(tool_idx, matching_tool_id);
        } else {
            SPDLOG_WARN("Connect tool sync: No matching tool definition found for tool index {}", tool_idx);
        }
    }
}

void ConnectMessageHandler::select_printer_materials_from_connect(const nlohmann::json& j)
{
    if (!j.contains("tools") || !j["tools"].is_object()) {
        return;
    }

    const auto& tools_json = j["tools"];
    size_t max_slots = m_preset_interactor.material_presets().size();

    const auto& selected_preset = m_preset_interactor.selected_printer_preset();
    const std::string& hw_config_id = selected_preset.hw_config.id;
    const std::string& printer_preset_id = selected_preset.printer.id;
    const std::string& print_preset_id = selected_preset.print.id;

    for (size_t slot_idx = 0; slot_idx < max_slots; ++slot_idx) {
        std::string tool_key = std::to_string(slot_idx);
        
        if (!tools_json.contains(tool_key)) {
            continue;
        }

        const auto& tool_json = tools_json[tool_key];
        std::string target_type;

        if (tool_json.contains("material_package_instance") && tool_json["material_package_instance"].is_object()) {
            const auto& mpi = tool_json["material_package_instance"];
            if (mpi.contains("package") && mpi["package"].is_object()) {
                const auto& pkg = mpi["package"];
                if (pkg.contains("material") && pkg["material"].is_object()) {
                    const auto& mat = pkg["material"];
                    if (mat.contains("type") && mat["type"].is_string()) {
                        target_type = mat["type"].get<std::string>();
                    }
                }
            }
        }

        if (target_type.empty()) {
            continue;
        }

        const auto& material_list       = m_preset_interactor.material_presets().at(slot_idx);
        const auto& available_materials = material_list.items();
        std::string matching_material_id;

        std::string best_prusament_id;
        size_t best_prusament_len = std::string::npos;
        std::string best_generic_id;
        size_t best_generic_len = std::string::npos;
        std::string first_match_id;

        for (size_t i = 0; i < available_materials.size(); ++i) {
            const Preset::PresetItem& preset = available_materials.at(i);
            
            for (const auto& [mat_preset, is_runtime] : m_preset_interactor.get_material_presets(hw_config_id, printer_preset_id, print_preset_id, slot_idx)) {
                if (is_runtime) {
                    continue;
                }
                
                if (mat_preset.get().id != preset.id) {
                    continue;
                }

                auto it = mat_preset.get().config_box().find("filament_type");
                if (it.item && it.item->template get<std::string>() == target_type) {
                    if (first_match_id.empty()) {
                        first_match_id = preset.id;
                    }

                    if (preset.name.starts_with("Prusament")) {
                        if (preset.name.length() < best_prusament_len) {
                            best_prusament_len = preset.name.length();
                            best_prusament_id = preset.id;
                        }
                    } 
                    else if (preset.name.starts_with("Generic")) {
                        if (preset.name.length() < best_generic_len) {
                            best_generic_len = preset.name.length();
                            best_generic_id = preset.id;
                        }
                    }
                }
            } 
        }

        if (!best_prusament_id.empty()) {
            matching_material_id = best_prusament_id;
        } else if (!best_generic_id.empty()) {
            matching_material_id = best_generic_id;
        } else {
            matching_material_id = first_match_id;
        }

        if (!matching_material_id.empty()) {
            m_preset_interactor.select_material_preset(slot_idx, matching_material_id);
        } else {
            SPDLOG_WARN("Connect material sync: No matching material preset found for slot {} (Target type: {})", slot_idx, target_type);
        }
    }
}

void ConnectMessageHandler::do_select_printer_from_connect(
    const std::string& printer_json
)
{
    nlohmann::json parsed_printer_json;
    try {
        parsed_printer_json = nlohmann::json::parse(printer_json);
    } catch (const nlohmann::json::parse_error& e) {
        SPDLOG_ERROR("JSON parsing error: {}", e.what());
        dispatch_error("Failed to parse specific printer JSON.");
        return;
    }

    const auto& printer_configs_view = m_preset_interactor.get_printer_configs();
    
    const Domain::Preset::HwPrinterConfig* config =
        find_matching_printer_config(printer_configs_view, parsed_printer_json);

    if (!config) {
        SPDLOG_INFO("Failed to select printer preset from Connect. No matching hw config.");
        dispatch_error("No matching hardware configuration found locally.");
        return;
    }

    const Preset::PresetItem* item =
        find_matching_preset_item(m_preset_interactor.printer_presets().items(), config);

    if (!item) {
        SPDLOG_INFO("Failed to select printer preset from Connect. No matching printer preset.");
        dispatch_error("No matching system printer preset found.");
        return;
    }

    m_preset_interactor.select_printer_preset(config->id, item->id, true);
    m_physical_printer_interactor.select_connect_upload(false);
    select_printer_tools_from_connect(parsed_printer_json);
    select_printer_materials_from_connect(parsed_printer_json);

    m_last_printer_json = printer_json;
}

std::string ConnectMessageHandler::uuid_for_upload()
{
    if (m_last_printer_json.empty()) {
        return {};
    }

    try {
        nlohmann::json j = nlohmann::json::parse(m_last_printer_json);
        const auto& current_config = m_preset_interactor.current_printer_config();

        std::string j_model        = j.value("model", "");
        std::string j_base_model   = j.value("base_model", "");
        uint8_t j_tool_count       = j.value("tool_count", static_cast<uint8_t>(0));
        size_t j_tools_array_size  = j.contains("tools") ? j["tools"].size() : 0;

        if (current_config.model.model == j_model
            && current_config.model.base_model == j_base_model
            && current_config.tool_count == j_tool_count
            && current_config.material_slot_count() == j_tools_array_size)
        {
            return j.value("uuid", "");
        }
    } catch (const nlohmann::json::exception& e) {
        SPDLOG_ERROR("Failed to parse printer json: {}", e.what());
    }

    return {};
}

} // namespace Slic3r::Biz::Connect
