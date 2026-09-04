#include <functional>
#include "Slic3r/Domain/Preset/HwConfig.hpp"
#include "Slic3r/Assert.hpp"
#include "Slic3r/Log.hpp"

#include <charconv>
#include <ranges>

namespace Slic3r::Domain::Preset {

tl::expected<Address, std::string> to_address(const std::string& input)
{
    Address address;
    for (const auto& slot : std::views::split(input, ".")) {
        int slot_v;
        auto result =
            std::from_chars(std::to_address(slot.begin()), std::to_address(slot.end()), slot_v, 10);
        if (result.ec != std::errc()) {
            return tl::unexpected{"Failed to parse a number!"};
        }
        address.push_back(slot_v);
    }

    if (address.empty()) {
        return tl::unexpected{"No numbers were parsed!"};
    }

    return address;
}

FeatureValueMap build_features(const FeatureDefs& feature_defs)
{
    FeatureValueMap result;
    for (const auto& [name, def] : feature_defs)
        result[name] = def.default_value;
    return result;
}

void override_features(FeatureValueMap& dest, const FeatureDefs& overrides)
{
    for (const auto& [k, v] : overrides)
        dest[k] = v.default_value;
}

void override_features(FeatureValueMap& dest, const FeatureValueMap& overrides)
{
    for (const auto& [k, v] : overrides)
        dest[k] = v;
}

template <typename T>
void override_value(std::optional<T>& dest, const std::optional<T>& override)
{
    if (override.has_value())
        dest = override.value();
}

void override_visual(VisualRepresentation& dest, const VisualRepresentation& overrides)
{
    override_value(dest.bed_model, overrides.bed_model);
    override_value(dest.bed_texture, overrides.bed_texture);
    override_value(dest.thumbnail, overrides.thumbnail);
}

void fill_missing_features_with_default(FeatureValueMap& dest, const FeatureDefs& def)
{
    for (const auto& [k, v] : def) {
        if (dest.contains(k))
            continue;
        dest[k] = v.default_value;
    }
}

void remove_features_with_default(FeatureValueMap& features, const FeatureDefs& def_features)
{
    for (auto it = features.begin(); it != features.end();) {
        auto def_it = def_features.find(it->first);
        if (def_it->second.default_value == it->second)
            it = features.erase(it);
        else
            ++it;
    }
}

namespace {
template <typename T>
const T* find_element_by_id(
    const VendorData& vendor_data,
    const std::string& id,
    const std::function<const std::map<std::string, T>&(const HwPrinterTechnologyDefs&)>& element_container_getter
)
{
    for (const auto& [_, pt_defs] : vendor_data.defs) {
        const auto& elements = element_container_getter(pt_defs);
        if (const auto it = elements.find(id); it != elements.end())
            return &it->second;
    }
    return nullptr;
}
} // namespace

const HwPrinterConfigDef* VendorData::find_printer_config_def_by_id(const std::string& id) const
{
    return find_element_by_id<HwPrinterConfigDef>(
        *this,
        id,
        [](const HwPrinterTechnologyDefs& pt_defs) -> const auto& { return pt_defs.printers; }
    );
}

const HwPrinterConfigDef* VendorData::find_printer_config_def_by_legacy_printer_model(const std::string& id) const
{
    for (const auto& tech_defs : defs | std::views::values) {
        const auto printer_defs = tech_defs.printers | std::views::values;
        auto it = std::ranges::find_if(
           printer_defs,
           [&](const HwPrinterConfigDef& pc)
           { return std::ranges::find(pc.legacy_printer_model, id) != pc.legacy_printer_model.end(); }
       );
        if (it != printer_defs.end())
            return &*it;
    }
    return nullptr;
}

const HwToolConfigDef* VendorData::find_tool_config_def_by_id(const std::string& id) const
{
    return find_element_by_id<HwToolConfigDef>(
        *this,
        id,
        [](const HwPrinterTechnologyDefs& pt_defs) -> const auto& { return pt_defs.tools; }
    );
}

const HwFeederConfigDef* VendorData::find_feeder_config_def_by_id(const std::string& id) const
{
    return find_element_by_id<HwFeederConfigDef>(
        *this,
        id,
        [](const HwPrinterTechnologyDefs& pt_defs) -> const auto& { return pt_defs.feeders; }
    );
}

const HwSheetConfigDef* VendorData::find_sheet_config_def_by_id(const std::string& id) const
{
    return find_element_by_id<HwSheetConfigDef>(
        *this,
        id,
        [](const HwPrinterTechnologyDefs& pt_defs) -> const auto& { return pt_defs.sheets; }
    );
}

const HwPrinterConfigTemplate* VendorData::find_printer_config_template_by_id(const std::string& id) const
{
    auto it = std::find_if(printer_configs.begin(), printer_configs.end(), [&](const auto& pc) {
        return pc.id == id;
    });
    return it == printer_configs.end() ? nullptr : &*it;
}

const HwPrinterConfigTemplate* VendorData::find_printer_config_template_by_legacy_printer_model(const std::string& id) const
{
    auto it = std::ranges::find_if(
        printer_configs,
        [&](const HwPrinterConfigTemplate& pc)
        { return std::ranges::find(pc.legacy_printer_model, id) != pc.legacy_printer_model.end(); }
    );
    return it == printer_configs.end() ? nullptr : &*it;
}

MaterialIterator::MaterialIterator(const HwPrinterConfig& config) :
    m_config(config),
    m_current_address({0})
{
    if (!m_config.feeders.empty())
        descent_to_material_slot();
}

void MaterialIterator::descent_to_material_slot()
{
    ASSERT(!m_current_address.empty());
    const HwFeederConfig* feeder = nullptr;
    do {
        const auto it = m_config.feeders.find(m_current_address);
        m_current_address.push_back(0);
        feeder = it == m_config.feeders.end() ? nullptr : &it->second;
    } while (feeder != nullptr);
    m_current_address.pop_back();
}

MaterialIterator& MaterialIterator::operator++()
{
    if (m_current_address.back() + 1 < current_slot_count()) {
        m_current_address.back()++;
        descent_to_material_slot();
    } else {
        // move up till the next free slot is available
        while (!m_current_address.empty() && m_current_address.back() + 1 >= current_slot_count())
            m_current_address.pop_back();
        // if there is still valid address
        if (!m_current_address.empty()) {
            // increment address
            m_current_address.back()++;
            // move down to first children till material slot reached
            descent_to_material_slot();
        }
    }
    return *this;
}

size_t MaterialIterator::current_slot_count() const
{
    size_t n = m_current_address.size();
    if (n == 0)
        return 0;
    if (n == 1)
        return m_config.tool_count;
    auto it = m_config.feeders.find({m_current_address.begin(), m_current_address.begin() + (n - 1)});
    ASSERT(it != m_config.feeders.end());
    return it->second.slot_count;
}

void fill_missing_features_with_default(HwPrinterConfig& printer_config, const VendorData& vendor_data)
{
    const auto* def = vendor_data.find_printer_config_def_by_id(printer_config.printer_id);
    ASSERT(def != nullptr, printer_config.printer_id);
    fill_missing_features_with_default(printer_config.features, def->features);

    for (auto& tool_config : printer_config.tools) {
        const auto* tool_def = vendor_data.find_tool_config_def_by_id(tool_config.id);
        ASSERT(tool_def != nullptr, tool_config.id);
        fill_missing_features_with_default(tool_config.features, tool_def->features);
    }

    for (auto& [slot, feeder_config] : printer_config.feeders) {
        const auto* feeder_def = vendor_data.find_feeder_config_def_by_id(feeder_config.id);
        ASSERT(feeder_def != nullptr, feeder_config.id);
        fill_missing_features_with_default(feeder_config.features, feeder_def->features);
    }
}

HwPrinterConfig remove_features_with_default(
    const HwPrinterConfig& printer_config,
    const VendorData& vendor_data
)
{
    HwPrinterConfig ret = printer_config;

    const auto* def = vendor_data.find_printer_config_def_by_id(printer_config.printer_id);
    ASSERT(def != nullptr, printer_config.printer_id);
    remove_features_with_default(ret.features, def->features);

    for (auto& tool_config : ret.tools) {
        const auto* tool_def = vendor_data.find_tool_config_def_by_id(tool_config.id);
        ASSERT(tool_def != nullptr, tool_config.id);
        remove_features_with_default(tool_config.features, tool_def->features);
    }

    for (auto& [slot, feeder_config] : ret.feeders) {
        const auto* feeder_def = vendor_data.find_feeder_config_def_by_id(feeder_config.id);
        ASSERT(feeder_def != nullptr, feeder_config.id);
        remove_features_with_default(feeder_config.features, feeder_def->features);
    }

    return ret;
}

std::string suggest_name(const HwPrinterConfig& cfg, const VendorData& vendor_data, bool brief)
{
    auto* printer = vendor_data.find_printer_config_def_by_id(cfg.printer_id);
    ASSERT(printer != nullptr, cfg.printer_id);
    std::stringstream ss;
    ss << printer->name;
    if (cfg.technology == PrinterTechnology::FFF) {
        if (cfg.tool_count > 1)
            ss << " " << int(cfg.tool_count) << "T";
        bool first = true;
        for (size_t i = 0; i < cfg.tools.size(); ++i) {
            const auto& t = cfg.tools[i];
            auto* tool = vendor_data.find_tool_config_def_by_id(t.id);
            auto feeder_it = cfg.feeders.find(Address{static_cast<uint8_t>(i)});
            ASSERT(tool != nullptr, t.id);
            if (first) {
                first = false;
                ss << " ";
            } else if (!brief){
                ss << ", ";
            }

            if (feeder_it != cfg.feeders.end()) {
                auto* feeder = vendor_data.find_feeder_config_def_by_id(feeder_it->second.id);
                ASSERT(feeder != nullptr, feeder_it->second.id);
                ss << feeder->name;
                if (!brief)
                    ss << " ";
            }
            if (!brief) {
                ss << tool->name;
            }
        }
    }

    return ss.str();
}

size_t HwPrinterConfig::material_slot_count() const
{
    size_t count = tool_count;
    for (const auto& feeder : feeders | std::views::values) {
        ASSERT(feeder.slot_count >= 1);
        count += feeder.slot_count - 1;
    }
    return count;
}



std::string HwPrinterConfig::relative_path_to_assets() const
{
    return "/presets/" + repo_id + "/" + vendor_id + "/assets/";
}


bool HwPrinterConfig::has_same_values(const HwPrinterConfig& other) const
{
#ifndef NDEBUG
    auto feature_val_to_string = []<typename T>(const T& val) -> std::string {
        if constexpr (std::is_same_v<T, std::string>) {
            return val;
        } else if constexpr (std::is_same_v<T, std::nullptr_t>) {
            return "(null)";
        } else if constexpr (!std::is_same_v<T, JsonObject> && !std::is_same_v<T, JsonArray> ) {
            return std::to_string(val);
        } else {
            return "...";
        }
    };

    constexpr size_t N = 10;
    if (name.substr(0, N) == other.name.substr(0, N)) {
        SPDLOG_INFO("HwPrinterConfig::has_same_values id: {}, name: {}", id, name);
        if (printer_id != other.printer_id) {
            SPDLOG_INFO("printer_id mismatched {} != {}", printer_id, other.printer_id);
        }
        if (vendor_id != other.vendor_id) {
            SPDLOG_INFO("vendor_id mismatched {} != {}", vendor_id, other.vendor_id);
        }
        if (repo_id != other.repo_id) {
            SPDLOG_INFO("repo_id mismatched {} != {}", repo_id, other.repo_id);
        }
        if (repo_version != other.repo_version) {
            SPDLOG_INFO("repo_version mismatched {} != {}", repo_version, other.repo_version);
        }
        if (name != other.name) {
            SPDLOG_INFO("name mismatched {} != {}", name, other.name);
        }
        if (short_name != other.short_name) {
            SPDLOG_INFO("short_name mismatched {} != {}", short_name, other.short_name);
        }
        if (technology != other.technology) {
            SPDLOG_INFO("technology mismatched {} != {}", int(technology), int(other.technology));
        }
        if (model != other.model) {
            SPDLOG_INFO(
                "model mismatched {}/{} != {}/{}",
                model.base_model,
                model.model,
                other.model.base_model,
                other.model.model
            );
        }
        if (tool_count != other.tool_count) {
            SPDLOG_INFO("tool_count mismatched {} != {}", tool_count, other.tool_count);
        }
        if (features != other.features) {
            SPDLOG_INFO("features mismatched |{}| != |{}|", features.size(), other.features.size());
            for (const auto& [k, v] : features) {
                auto oit = other.features.find(k);
                std::optional<std::string> other_val;
                if (oit != other.features.end()) {
                    auto val = oit->second;
                    if (val == v) {
                        continue;
                    }
                    other_val = std::visit(feature_val_to_string, val);
                }

                SPDLOG_INFO(
                    "  {}: {} != {}",
                    k,
                    std::visit(feature_val_to_string, v),
                    other_val.has_value() ? *other_val : "ø"
                );
            }
            for (const auto& [k, v] : other.features) {
                if (features.contains(k) ) {
                    continue;
                }

                SPDLOG_INFO("  {}: ø != {}", k, std::visit(feature_val_to_string, v));
            }
        }
        if (visual != other.visual) {
            SPDLOG_INFO("visual mismatched");
        }
        if (tools != other.tools) {
            SPDLOG_INFO("tools mismatched");
        }
        if (feeders != other.feeders) {
            SPDLOG_INFO("feeders mismatched |{}| != |{}|", feeders.size(), other.feeders.size());
        }
        if (sheet != other.sheet) {
            SPDLOG_INFO("sheet mismatched");
        }
    }
#endif // NDEBUG

    // Intentionally omitted fields as changing these don't change the preset logic:
    // - name, short_name
    // - material: this is only 3MF/GCode metadata unified storage about materials
    //   (i.e. won't change preset logic either)
    return printer_id == other.printer_id
        && vendor_id == other.vendor_id
        && repo_id == other.repo_id
        && repo_version == other.repo_version
        && technology == other.technology
        && model == other.model
        && tool_count == other.tool_count
        && features == other.features
        && visual == other.visual
        && tools == other.tools
        && feeders == other.feeders
        && sheet == other.sheet;
}

MaterialIterator
MaterialIterator::from_slot_index(const HwPrinterConfig& hw_config, size_t slot_index)
{
    MaterialIterator it{hw_config};
    for (size_t current_slot_idx = 0; current_slot_idx < slot_index; ++current_slot_idx) {
        ++it;
        ASSERT(it.is_valid());
    }
    return it;
}

} // namespace Slic3r::Domain::Preset
