#include "Slic3r/Biz/PhysicalPrinter/PhysicalPrinterJson.hpp"

#include "Slic3r/Biz/Config/HwConfigJson.hpp"
#include "Slic3r/Domain/ConfigPhysical.hpp"
#include "Slic3r/Assert.hpp"

#include <nlohmann/json.hpp>
#include <variant>

namespace Slic3r::Biz::PhysicalPrinter {

namespace {

using Domain::PrintHostAuthType;
using Domain::PrintHostType;

// Serialized enum strings must match the legacy ConfigBox schema
// (see the removed physical_printer_init_fn in ConfigPhysical.cpp).
std::string host_type_to_serialized(PrintHostType type)
{
    switch (type) {
    case Domain::PrusaLink:        return "prusalink";
    case Domain::PrusaLinkStorage: return "prusalink_storage";
    case Domain::SL1Host:          return "sl1";
    case Domain::OctoPrint:        return "octoprint";
    case Domain::Moonraker:        return "moonraker";
    case Domain::Duet:             return "duet";
    case Domain::FlashAir:         return "flashair";
    case Domain::AstroBox:         return "astrobox";
    case Domain::Repetier:         return "repetier";
    case Domain::MKS:              return "mks";
    }
    return "prusalink";
}

PrintHostType host_type_from_serialized(const std::string& s)
{
    if (s == "prusalink")         return Domain::PrusaLink;
    if (s == "prusalink_storage") return Domain::PrusaLinkStorage;
    if (s == "sl1")               return Domain::SL1Host;
    if (s == "octoprint")         return Domain::OctoPrint;
    if (s == "moonraker")         return Domain::Moonraker;
    if (s == "duet")              return Domain::Duet;
    if (s == "flashair")          return Domain::FlashAir;
    if (s == "astrobox")          return Domain::AstroBox;
    if (s == "repetier")          return Domain::Repetier;
    if (s == "mks")               return Domain::MKS;
    return Domain::PrusaLink;
}

std::string auth_type_to_serialized(PrintHostAuthType type)
{
    switch (type) {
    case PrintHostAuthType::None:   return "none";
    case PrintHostAuthType::ApiKey: return "key";
    case PrintHostAuthType::Digest: return "user";
    }
    return "key";
}

PrintHostAuthType auth_type_from_serialized(const std::string& s)
{
    if (s == "none") return PrintHostAuthType::None;
    if (s == "key")  return PrintHostAuthType::ApiKey;
    if (s == "user") return PrintHostAuthType::Digest;
    return PrintHostAuthType::ApiKey;
}

std::string get_str(const nlohmann::ordered_json& j, const char* key)
{
    auto it = j.find(key);
    if (it != j.end() && it->is_string())
        return it->get<std::string>();
    return {};
}

bool get_bool(const nlohmann::ordered_json& j, const char* key, bool def)
{
    auto it = j.find(key);
    if (it != j.end() && it->is_boolean())
        return it->get<bool>();
    return def;
}

} // namespace

nlohmann::ordered_json printer_to_json(const PhysicalPrinterConfig& printer)
{
    const PrinterUpload* up = std::get_if<PrinterUpload>(&printer.payload);
    ASSERT(up); // Only host-upload printers are persisted.

    nlohmann::ordered_json j;
    j["physical_printer_user_given_name"]    = printer.name;
    j["physical_printer_host"]               = printer.host;
    j["physical_printer_host_type"]          = host_type_to_serialized(up->type);
    j["physical_printer_authorization_type"] = auth_type_to_serialized(up->auth_type);
    j["physical_printer_api_key"]            = up->api_key;
    j["physical_printer_user"]               = up->username;
    j["physical_printer_password"]           = up->password;
    j["physical_printer_port"]               = up->port;
    j["physical_printer_ca_file"]            = up->ca_file;
    j["physical_printer_ssl_ignore_revoke"]  = up->ssl_revoke_best_effort;
    j["physical_printer_uuid"]               = printer.uuid;
    // Kept for compatibility with the legacy schema (unused).
    j["physical_printer_preset_model"]       = std::string{};
    j["physical_printer_preset_base_model"]  = std::string{};
    j["hw_config"]                           = printer.hw_config;
    return j;
}

tl::expected<PhysicalPrinterConfig, std::string>
printer_from_json(const nlohmann::ordered_json& json)
{
    if (!json.is_object())
        return tl::unexpected{std::string{"Physical printer json is not an object"}};

    if (!json.contains("hw_config"))
        return tl::unexpected{std::string{"Physical printer json is missing hw_config"}};

    auto hw_config = Biz::Config::load_hw_config(json.at("hw_config"));
    if (!hw_config)
        return tl::unexpected{"Invalid hw_config: " + hw_config.error()};

    PrinterUpload up;
    up.type       = host_type_from_serialized(get_str(json, "physical_printer_host_type"));
    up.auth_type  = auth_type_from_serialized(get_str(json, "physical_printer_authorization_type"));
    up.api_key    = get_str(json, "physical_printer_api_key");
    up.username   = get_str(json, "physical_printer_user");
    up.password   = get_str(json, "physical_printer_password");
    up.port       = get_str(json, "physical_printer_port");
    up.ca_file    = get_str(json, "physical_printer_ca_file");
    up.ssl_revoke_best_effort = get_bool(json, "physical_printer_ssl_ignore_revoke", false);

    PhysicalPrinterConfig printer;
    printer.payload   = std::move(up);
    printer.host      = get_str(json, "physical_printer_host");
    printer.name      = get_str(json, "physical_printer_user_given_name");
    printer.uuid      = get_str(json, "physical_printer_uuid");
    printer.hw_config = std::move(hw_config.value());
    return printer;
}

} // namespace Slic3r::Biz::PhysicalPrinter
