#include "Slic3r/Biz/ConnectPrinter/ConnectFavoriteMaterials.hpp"

#include "Slic3r/Log.hpp"

#include <algorithm>
#include <nlohmann/json.hpp>

namespace Slic3r::Biz::ConnectPrinter {

namespace {
constexpr const char* FAVORITE_MATERIALS_QUERY =
    "{ warehouse { favoriteMaterials { material { uuid } } } }";
} // namespace

std::string favorite_materials_request_body()
{
    return nlohmann::json{{"query", FAVORITE_MATERIALS_QUERY}}.dump();
}

std::optional<std::vector<std::string>> parse_favorite_materials(const std::string& body)
{
    nlohmann::json json;
    try {
        json = nlohmann::json::parse(body);
    } catch (const nlohmann::json::parse_error& e) {
        SPDLOG_ERROR("Could not parse Connect favorite materials. {}", e.what());
        return std::nullopt;
    }

    if (!json.is_object()) {
        SPDLOG_ERROR("Unexpected format of Connect favorite materials.");
        return std::nullopt;
    }

    // GraphQL reports failures with HTTP 200 and an "errors" array.
    if (auto it = json.find("errors"); it != json.end() && it->is_array() && !it->empty()) {
        SPDLOG_ERROR("Connect favorite materials query failed: {}", it->dump());
        return std::nullopt;
    }

    auto data = json.find("data");
    if (data == json.end() || !data->is_object()) {
        SPDLOG_ERROR("Unexpected format of Connect favorite materials.");
        return std::nullopt;
    }
    auto warehouse = data->find("warehouse");
    if (warehouse == data->end() || !warehouse->is_object()) {
        SPDLOG_ERROR("Unexpected format of Connect favorite materials.");
        return std::nullopt;
    }

    std::vector<std::string> uuids;
    auto favorites = warehouse->find("favoriteMaterials");
    if (favorites == warehouse->end() || !favorites->is_array()) {
        return uuids;
    }

    for (const nlohmann::json& favorite : *favorites) {
        if (!favorite.is_object()) {
            continue;
        }
        auto material = favorite.find("material");
        if (material == favorite.end() || !material->is_object()) {
            continue;
        }
        auto uuid = material->find("uuid");
        if (uuid == material->end() || !uuid->is_string()) {
            continue;
        }
        std::string value = uuid->get<std::string>();
        if (!value.empty() && std::ranges::find(uuids, value) == uuids.end()) {
            uuids.push_back(std::move(value));
        }
    }
    return uuids;
}

} // namespace Slic3r::Biz::ConnectPrinter
