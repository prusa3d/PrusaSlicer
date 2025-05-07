#include "UserAccount.hpp"

#include "UserAccountUtils.hpp"
#include "format.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"

#include "libslic3r/Utils.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/PresetBundle.hpp"

#include <boost/regex.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/log/trivial.hpp>

#include "InstanceCheck.hpp"
#include "GUI_App.hpp"

#include <wx/stdpaths.h>

namespace pt = boost::property_tree;

namespace Slic3r {
namespace GUI {

UserAccount::UserAccount(wxEvtHandler* evt_handler, AppConfig* app_config, const std::string& instance_hash)
    : m_communication(std::make_unique<UserAccountCommunication>(evt_handler, app_config))
    , m_instance_hash(instance_hash)
{
}

UserAccount::~UserAccount()
{}

void UserAccount::set_username(const std::string& username, bool store)
{
    m_username = username;
    m_communication->set_username(username, store);
}

void UserAccount::clear()
{
    m_username = {};
    m_account_user_data.clear();
    m_printer_map.clear();
    m_communication->do_clear();
}

void UserAccount::set_remember_session(bool remember)
{
    m_communication->set_remember_session(remember);
}
void UserAccount::toggle_remember_session()
{
    m_communication->set_remember_session(!m_communication->get_remember_session());
}
bool UserAccount::get_remember_session()
{
    return m_communication->get_remember_session();
}

bool UserAccount::is_logged() const
{
    return m_communication->is_logged();
}
void UserAccount::do_login()
{
    m_communication->do_login();
}
void UserAccount::do_logout()
{
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__;
    m_communication->do_logout();
    Slic3r::GUI::wxGetApp().other_instance_message_handler()->multicast_message("STORE_READ"); 
}

std::string UserAccount::get_access_token()
{
    return m_communication->get_access_token();
}

std::string UserAccount::get_shared_session_key()
{
    return m_communication->get_shared_session_key();
}

boost::filesystem::path UserAccount::get_avatar_path(bool logged) const
{
    if (logged) {
        const std::string filename = "prusaslicer-avatar-" + m_instance_hash + m_avatar_extension;
        return into_path(wxStandardPaths::Get().GetTempDir()) / filename;
    } else {
        return  boost::filesystem::path(resources_dir()) / "icons" / "user.svg";
    }
}

void UserAccount::enqueue_connect_printer_models_action()
{
    m_communication->enqueue_connect_printer_models_action();
}
void UserAccount::enqueue_connect_status_action()
{
    m_communication->enqueue_connect_status_action();
}
void UserAccount::enqueue_avatar_old_action()
{
    m_communication->enqueue_avatar_old_action(m_account_user_data["avatar"]);
}
void UserAccount::enqueue_avatar_new_action(const std::string& url)
{
    m_communication->enqueue_avatar_new_action(url);
}
void UserAccount::enqueue_printer_data_action(const std::string& uuid)
{
    m_communication->enqueue_printer_data_action(uuid);
}
void UserAccount::request_refresh()
{
    m_communication->request_refresh();
}

bool UserAccount::on_login_code_recieved(const std::string& url_message)
{
    m_communication->on_login_code_recieved(url_message);
    return true;
}

bool UserAccount::on_user_id_success(const std::string data, std::string& out_username, bool after_token_success)
{
    boost::property_tree::ptree ptree;
    try {
        std::stringstream ss(data);
        boost::property_tree::read_json(ss, ptree);
    }
    catch (const std::exception&) {
        BOOST_LOG_TRIVIAL(error) << "UserIDUserAction Could not parse server response.";
        return false;
    }
    m_account_user_data.clear();
    for (const auto& section : ptree) {
        const auto opt = ptree.get_optional<std::string>(section.first);
        if (opt) {
            //BOOST_LOG_TRIVIAL(debug) << static_cast<std::string>(section.first) << "    " << *opt;
            m_account_user_data[section.first] = *opt;
        }
       
    }
    if (m_account_user_data.find("public_username") == m_account_user_data.end()) {
        BOOST_LOG_TRIVIAL(error) << "User ID message from PrusaAuth did not contain public_username. Login failed. Message data: " << data;
        return false;
    }
    std::string public_username = m_account_user_data["public_username"];
    set_username(public_username, after_token_success);
    out_username = public_username;
    // enqueue GET with avatar url

    if (m_account_user_data.find("avatar_small") != m_account_user_data.end()) {
        const boost::filesystem::path server_file(m_account_user_data["avatar_small"]);
        m_avatar_extension = server_file.extension().string();
        enqueue_avatar_new_action(m_account_user_data["avatar_small"]);
    } else if (m_account_user_data.find("avatar_large") != m_account_user_data.end()) {
        const boost::filesystem::path server_file(m_account_user_data["avatar_large"]);
        m_avatar_extension = server_file.extension().string();
        enqueue_avatar_new_action(m_account_user_data["avatar_large"]);
    } else if (m_account_user_data.find("avatar") != m_account_user_data.end()) {
        const boost::filesystem::path server_file(m_account_user_data["avatar"]);
        m_avatar_extension = server_file.extension().string();
        enqueue_avatar_old_action();
    }
    else {
        BOOST_LOG_TRIVIAL(error) << "User ID message from PrusaAuth did not contain avatar.";
    }
    // update printers list
    enqueue_connect_printer_models_action();
    return true;
}

void UserAccount::on_communication_fail()
{
    m_fail_counter++;
    if (m_fail_counter > 5) // there is no deeper reason why 5
    {
        m_communication->enqueue_test_connection();
        m_fail_counter = 0;
    }
}

void UserAccount::on_race_lost()
{
    m_communication->on_race_lost();
}

bool UserAccount::on_connect_printers_success(const std::string& data, AppConfig* app_config, bool& out_printers_changed)
{
    BOOST_LOG_TRIVIAL(trace) << "Prusa Connect printers message: " << data;
    pt::ptree ptree;
    try {
        std::stringstream ss(data);
        pt::read_json(ss, ptree);
    }
    catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "Could not parse prusaconnect message. " << e.what();
        return false;
    }
    // tree format: 
    /*
    [{
        "printer_uuid": "972d2ce7-0967-4555-bff2-330c7fa0a4e1",
            "printer_state" : "IDLE"
    }, {
        "printer_uuid": "15d160fd-c7e4-4b5a-9748-f0e531c7e0f5",
        "printer_state" : "OFFLINE"
    }]
    */
    ConnectPrinterStateMap new_printer_map;
    for (const auto& printer_tree : ptree) {
        const auto printer_uuid = printer_tree.second.get_optional<std::string>("printer_uuid");
        if (!printer_uuid) {
            continue;
        }
        const auto printer_state = printer_tree.second.get_optional<std::string>("printer_state");
        if (!printer_state) {
            continue;
        }
        ConnectPrinterState state;
        if (auto pair = printer_state_table.find(*printer_state); pair != printer_state_table.end()) {
            state = pair->second;
        } else {
            assert(false); // On this assert, printer_state_table needs to be updated with *state_opt and correct ConnectPrinterState
            continue;
        }
        if (m_printer_uuid_map.find(*printer_uuid) == m_printer_uuid_map.end()) {
            BOOST_LOG_TRIVIAL(trace) << "Missing printer model for printer uuid: " << *printer_uuid;
            continue;
        }
        
        std::string printer_name = m_printer_uuid_map[*printer_uuid];

        if (new_printer_map.find(printer_name) == new_printer_map.end()) {
            new_printer_map[printer_name].reserve(static_cast<size_t>(ConnectPrinterState::CONNECT_PRINTER_STATE_COUNT));
            for (size_t i = 0; i < static_cast<size_t>(ConnectPrinterState::CONNECT_PRINTER_STATE_COUNT); i++) {
                new_printer_map[printer_name].push_back(0);
            }
        }
        new_printer_map[printer_name][static_cast<size_t>(state)] += 1;
        
    }

    // compare new and old printer map and update old map into new
    out_printers_changed = false;
    for (const auto& it : new_printer_map) {
        if (m_printer_map.find(it.first) == m_printer_map.end()) {
            // printer is not in old map, add it by copying data from new map
            out_printers_changed = true;
            m_printer_map[it.first].reserve(static_cast<size_t>(ConnectPrinterState::CONNECT_PRINTER_STATE_COUNT));
            for (size_t i = 0; i < static_cast<size_t>(ConnectPrinterState::CONNECT_PRINTER_STATE_COUNT); i++) {
                m_printer_map[it.first].push_back(new_printer_map[it.first][i]);
            }
        } else {
            // printer is in old map, check state by state
            for (size_t i = 0; i < static_cast<size_t>(ConnectPrinterState::CONNECT_PRINTER_STATE_COUNT); i++) {
                if (m_printer_map[it.first][i] != new_printer_map[it.first][i])  {
                    out_printers_changed = true;
                    m_printer_map[it.first][i] = new_printer_map[it.first][i];
                }
            }
        }
    }
    return true;
}

bool UserAccount::on_connect_uiid_map_success(const std::string& data, AppConfig* app_config, bool& out_printers_changed)
{
    m_printer_uuid_map.clear();
    pt::ptree ptree;
    try {
        std::stringstream ss(data);
        pt::read_json(ss, ptree);
    }
    catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "Could not parse prusaconnect message. " << e.what();
        return false;
    }
    pt::ptree printers_ptree;
    if (auto it = ptree.find("printers"); it != ptree.not_found()) {
        printers_ptree = it->second;
    } else {
        BOOST_LOG_TRIVIAL(error) << "Could not parse prusaconnect message. \"printers\" subtree is missing.";
        return false;
    }

    for (const auto& printer_sub : printers_ptree) {
        const pt::ptree &printer_ptree = printer_sub.second;
        const auto printer_uuid = printer_ptree.get_optional<std::string>("uuid");
        if (!printer_uuid) {
            continue;
        }
        const auto printer_model = printer_ptree.get_optional<std::string>("printer_model");
        if (!printer_model) {
            continue;
        }

        std::map<std::string, std::vector<std::string>> config_options_to_match; 
        UserAccountUtils::fill_config_options_from_json(printer_ptree, config_options_to_match);
        const Preset* printer_preset = UserAccountUtils::find_preset_by_nozzle_and_options(wxGetApp().preset_bundle->printers, *printer_model, config_options_to_match);
        if (printer_preset) {
            // Preset can have repo prefix
            std::string trimmed_name = printer_preset->name;
            const PresetWithVendorProfile& printer_with_vendor = wxGetApp().preset_bundle->printers.get_preset_with_vendor_profile(*printer_preset);
            trimmed_name = printer_preset->trim_vendor_repo_prefix(trimmed_name, printer_with_vendor.vendor);
            m_printer_uuid_map[*printer_uuid] = trimmed_name;
        } else {
            BOOST_LOG_TRIVIAL(trace) << "Failed to find preset for printer model: " << *printer_model;
        }
    }
    m_communication->on_uuid_map_success();
    return on_connect_printers_success(data, app_config, out_printers_changed);
}

std::string UserAccount::get_current_printer_uuid_from_connect(const std::string &selected_printer_id) const {
    if (m_current_printer_data_json_from_connect.empty() || m_current_printer_uuid_from_connect.empty()) {
        return {};
    }

    pt::ptree ptree;
    try {
        std::stringstream ss(m_current_printer_data_json_from_connect);
        pt::read_json(ss, ptree);
    }
    catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "Could not parse Printer data from Connect. " << e.what();
        return {};
    }

    std::string data_uuid = UserAccountUtils::get_keyword_from_json(ptree, "", "uuid");
    assert(data_uuid == m_current_printer_uuid_from_connect);

    //std::string model_name = parse_tree_for_param(ptree, "printer_model");
    std::vector<std::string> compatible_printers;
    UserAccountUtils::fill_supported_printer_models_from_json(ptree, compatible_printers);
    if (compatible_printers.empty()) {
        return {};
    }
    
    return std::find(compatible_printers.begin(), compatible_printers.end(), selected_printer_id) == compatible_printers.end() ? "" : m_current_printer_uuid_from_connect;
}

}} // namespace slic3r::GUI