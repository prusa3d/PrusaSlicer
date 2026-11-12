#include "Slic3r/Biz/HwConfigMatchers.hpp"

namespace Slic3r::Biz {

bool match_feeder(
    const Domain::Preset::Address& address,
    const nlohmann::json& json,
    const Domain::Preset::HwFeederConfigs& feeders)
{
    const std::string type{json.value("type", "")};
    const std::string model{json.value("model", "")};
    const std::string base_model{json.value("base_model", "")};
    const uint8_t num_slots{json.value("num_slots", static_cast<uint8_t>(0))};

    if (type.empty() || model.empty() || base_model.empty() || num_slots == 0) {
        return false;
    }

    const auto it{std::ranges::find_if(
        feeders,
        [&](const auto& pair)
        {
            const Domain::Preset::Address& feeder_address{pair.first};
            const Domain::Preset::HwFeederConfig feeder{pair.second};
            return feeder_address == address
                // TODO: Connect now does not send correct models. Uncomment this, once it does.
                // && model == feeder.model.model
                && base_model == feeder.model.base_model
                && num_slots == feeder.slot_count;
        })};

    return it != feeders.end();
}

} // namespace Slic3r::Biz
