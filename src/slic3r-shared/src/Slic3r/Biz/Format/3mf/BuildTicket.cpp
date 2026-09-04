#include "BuildTicket.hpp"
#include <Slic3r/Log.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/string_generator.hpp>
#include <boost/spirit/include/qi_int.hpp> // parse int
#include "pugixml.hpp"
#include <string>
#include <vector>
#include <map>
#include <optional>

#include "libslic3r/Model.hpp" // ModelInstance
#include "libslic3r/ProfilesSharingUtils.hpp" // load_fullconfig
#include "libslic3r/PrintConfig.hpp" // DynamicPrintConfig + ConfigSubstitutionContext

using namespace Slic3r;

namespace {

/// <summary>
/// Keep additional slice configuration for each object
/// </summary>
struct BuildTicket
{
    struct Property
    {
        std::string name;
        // std::string type;
        std::string value;
    };
    using Properties = std::vector<Property>;
    using Overrides = std::map<boost::uuids::uuid, Properties>;

    // Global document settings
    Properties default_properties;
    Overrides overrides;

    /// <summary>
    /// getter on property value
    /// </summary>
    /// <param name="name">Name of parameter in build ticket</param>
    /// <param name="uuid">When not set return default values</param>
    /// <returns>Value for name pair</returns>
    const std::string &get(const std::string &name, const boost::uuids::uuid &uuid) const;
    const std::string &get(const std::string &name) const;
};

/// <summary>
/// Read build ticket from file data and copy string values to private variable
/// </summary>
/// <param name="data">Do NOT take ownership, it is up to caller to destroy data</param>
/// <param name="data_size">size of data</param>
/// <returns>Slice configuration called Build Ticket, when it is possible create from data,
/// otherwise no value optional</returns>
std::optional<BuildTicket> create_build_ticket(void *data, size_t data_size);

BuildTicket::Property get_property(const pugi::xml_node &prop)
{
    pugi::xml_attribute name_attr = prop.attribute("Name");
    if (name_attr.empty())
        SPDLOG_WARN("Property 'Name' is empty. (value='{}')", prop.text().as_string());
    // pugi::xml_attribute type_attr = prop.attribute("Type"); // "string"
    if (prop.text().empty())
        SPDLOG_WARN("Property(name='{}') doesn't have value", name_attr.as_string());

    return BuildTicket::Property{name_attr.as_string(), prop.text().as_string()};
}

BuildTicket::Properties get_properties(const pugi::xml_node &props_parent)
{
    if (props_parent.empty())
        SPDLOG_WARN("Propertis parent is empty");
    BuildTicket::Properties properties; // result
    for (const pugi::xml_node &prop : props_parent.children("Property"))
        properties.push_back(get_property(prop));
    if (properties.empty())
        SPDLOG_WARN("Propertis are empty");
    return properties;
}

const BuildTicket::Property *get_property(const BuildTicket::Properties &props, const std::string &name) {
    for (const BuildTicket::Property &prop: props)
        if (prop.name == name)
            return &prop;    
    return nullptr;
}

std::optional<BuildTicket> create_build_ticket(void *data, size_t data_size)
{
    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_buffer_inplace(data, data_size);
    if (result.status != pugi::xml_parse_status::status_ok) {
        SPDLOG_ERROR("Pugi can't load xml from given data for BuildTicket: {}", result.description());
        return {};
    }
    pugi::xml_node root = doc.child("MtlsBuildTicket");
    if (root.empty()) {
        SPDLOG_ERROR("There is missing root tag \"MtlsBuildTicket\" OR it is empty");
        return {};
    }
    if (std::strcmp(root.attribute("xmlns").value(), "http://schemas.materialise.com/build_processing/2016/04") != 0)
        SPDLOG_WARN("Attribute \"xmlns\" is not \"http://schemas.materialise.com/build_processing/2016/04\"");

    pugi::xml_node defs = root.child("Defaults");
    // pugi::xml_attribute type_attr = defs.attribute("type"); // "build"
    BuildTicket bt;
    bt.default_properties = get_properties(defs);
    if (bt.default_properties.empty()) {
        SPDLOG_ERROR("There is no Defaults properties");
        return {};
    }

    // read overrides
    pugi::xml_node part_overrides = root.child("PartOverrides");
    boost::uuids::string_generator gen;
    for (const pugi::xml_node &instance : part_overrides.children("Instance")) {
        pugi::xml_attribute uuid_attr = instance.attribute("uuid");
        if (uuid_attr.empty()) {
            SPDLOG_WARN("UUID is required attribute for Instance");
            continue;
        }
        // pugi::xml_attribute type_attr = instance.attribute("type"); // "builditem"
        std::string uuid_str(uuid_attr.as_string());
        boost::uuids::uuid uuid = gen(uuid_str);
        auto it = bt.overrides.find(uuid);
        if (it != bt.overrides.end())
            SPDLOG_WARN("UUID('{}') already exists and its property will be overrided", uuid_attr.as_string());
        bt.overrides[uuid] = get_properties(instance);
    }
    return bt;
}

const std::string &BuildTicket::get(const std::string &name, const boost::uuids::uuid &uuid) const {
    if (auto it = overrides.find(uuid);
        it != overrides.end()){
        const Property* res = get_property(it->second, name);
        if (res != nullptr) // found object override
            return res->value;
    }
    // no object specific so try return default value
    return get(name);
}

static const std::string no_value = "";
const std::string& BuildTicket::get(const std::string &name) const {
    const Property *res = get_property(default_properties, name);
    if (res == nullptr) { // not known property name
        SPDLOG_WARN("Property with name \"{}\" is not defined", name);
        assert(false);
        return no_value;
    }
    return res->value;
}

using UuidToInstance = std::map<boost::uuids::uuid, const ModelInstance*>;
UuidToInstance create_uuid_to_instance(const format_3MF::CT_Items &items, const InstanceMap &instances) {
    UuidToInstance result;
    assert(items.size() == instances.size());
    for (const format_3MF::CT_Item &item: items){
        if (item.uuid.is_nil())
            continue;
        size_t index = &item - &items.front();
        if (index >= instances.size())
            continue;
        const ModelInstance *instance = instances[index];
        if (instance == nullptr)
            continue;

        // check unique uuid in items
        assert(result.find(item.uuid) == result.end());

        result[item.uuid] = instance;
    }
    return result;
}

const std::string BT_PRINT_PROFILE_NAME               = "print_profile";
const std::string BT_FILAMENT_PROFILE_NAME            = "filament_profile";
const std::string BT_FILAMENT_PROFILE_NAME_PREFIX     = "filament_profile_";
const std::string BT_PRINTER_PROFILE_NAME             = "printer_profile";
const std::string BT_EXTRUDER_NUMBER_NAME             = "extruder_number";
const std::string BT_EXTRUDER_NUMBER_FOR_SUPPORT_NAME = "extruder_support_number";
const std::string BT_EXTRUDER_NUMBER_VALUE_PREFIX     = "Extruder ";
const std::string BT_OBJECT_NAME                      = "object_name";
const std::string BT_GENERATE_SUPPORT_NAME            = "generate_support";

// apply build ticket to model by pointer to instance
void apply(const BuildTicket         &build_ticket,
           const UuidToInstance      &uuid_to_instance,
           DynamicPrintConfig        &config,
           ConfigSubstitutionContext &config_substitutions)
{
    auto get_postfix_number = [](const std::string &prefix, const std::string &value,
                                 bool is_zero_based = false) -> int {
        int postfix_number;
        if (value.size() <= prefix.size() ||
            !boost::spirit::qi::parse(&value[prefix.size()], &value.back()+1,
            boost::spirit::qi::int_, postfix_number)) {
            SPDLOG_WARN("Postfix number should have value=\"{}{{N}}\", where {{N}} is number. Can't parse value: \"{}\" soo zero is used", prefix, value);
            return 0;
        }
        if (postfix_number <= 0) {
            SPDLOG_WARN("Should be positive number. Can't parse value: \"{}\" as positive number", value);
            return 0;
        }

        // in configuration index of extruder starts from zero
        // but in build ticket starts from 1
        if (is_zero_based)
            postfix_number -= 1;

        // TODO: Check 'postfix_number' is smaller than count of extruders
        return postfix_number;
    };
    auto get_extruder_number = [get_postfix_number]
    (const std::string& value, bool is_zero_based = false)->std::string{
        return std::to_string(get_postfix_number(BT_EXTRUDER_NUMBER_VALUE_PREFIX, value, is_zero_based)); };
    auto get_filament_number = [get_postfix_number](const std::string &value) {
        return get_postfix_number(BT_FILAMENT_PROFILE_NAME_PREFIX, value, /* is_zero_based */true); };
    auto get_bool_value = [](const std::string &value) -> std::string {
        if (value == "true") return "1";
        if (value == "false") return "0";

        SPDLOG_WARN("Can't parse bool value from: \"{}\" soo false is used", value);
        return "0";
    };

    struct CfgNames {
        std::string print;
        std::vector<std::string> filaments;
        std::string printer;
    }cfg_names;

    for (const BuildTicket::Property &prop : build_ticket.default_properties) {
        // check uniquity of property name
        if (prop.name == BT_PRINT_PROFILE_NAME) {
            cfg_names.print = prop.value;
        } else if (boost::starts_with(prop.name, BT_FILAMENT_PROFILE_NAME_PREFIX)) {
            int filament_profile_number = get_filament_number(prop.name);
            int i = static_cast<int>(cfg_names.filaments.size());
            if (i <= filament_profile_number) {
                // extend vector of filaments by current profile
                for (; i <= filament_profile_number; ++i)
                    cfg_names.filaments.push_back(prop.value);
            } else {
                cfg_names.filaments[filament_profile_number] = prop.value;
            }
        } else if (prop.name == BT_FILAMENT_PROFILE_NAME) {
            if (cfg_names.filaments.empty()) {
                cfg_names.filaments.push_back(prop.value);
            } else {
                SPDLOG_WARN("Mixed definition of filament for single and multiple tool printer");
                cfg_names.filaments.front() = prop.value; // set as first filament
            }
        } else if (prop.name == BT_PRINTER_PROFILE_NAME) {
            cfg_names.printer = prop.value;
        }
    }
    assert(!cfg_names.printer.empty());    
    assert(!cfg_names.print.empty());
    assert(!cfg_names.filaments.empty());
    // Load configs by names
    std::string error = load_full_print_config(cfg_names.print, cfg_names.filaments, cfg_names.printer, config, PrinterTechnology::ptUnknown);
    if (!error.empty())
        throw boost::filesystem::filesystem_error(error, {});

    std::set<std::string> contain_name;
    for (const BuildTicket::Property &prop : build_ticket.default_properties) {
        // check uniquity of property name
        if (contain_name.find(prop.name) != contain_name.end()) {
            SPDLOG_WARN("Do not support second use of same property Name: {} with value \"{}\"", prop.name, prop.value);
            continue;
        }
        contain_name.insert(prop.name);

        if (prop.name == BT_PRINT_PROFILE_NAME ||
            prop.name == BT_PRINTER_PROFILE_NAME ||
            prop.name == BT_FILAMENT_PROFILE_NAME ||
            boost::starts_with(prop.name, BT_FILAMENT_PROFILE_NAME_PREFIX))
            // already processed
            continue;

        if (prop.name == BT_GENERATE_SUPPORT_NAME) {
            config.set_deserialize("support_material", get_bool_value(prop.value), config_substitutions);
        } else if (prop.name == BT_EXTRUDER_NUMBER_NAME) {
            std::string extruder_number = get_extruder_number(prop.value);
            config.set_deserialize("infill_extruder", extruder_number, config_substitutions);
            config.set_deserialize("solid_infill_extruder", extruder_number, config_substitutions);
            config.set_deserialize("perimeter_extruder", extruder_number, config_substitutions);
        } else if (prop.name == BT_EXTRUDER_NUMBER_FOR_SUPPORT_NAME) {
            config.set_deserialize("support_material_extruder", get_extruder_number(prop.value, true), config_substitutions);
        } else {
            SPDLOG_WARN("Unsupported Build Ticket name: {} with value \"{}\"", prop.name, prop.value);
        }
    }
    
    for (const auto& [uuid, properties]: build_ticket.overrides){
        auto it = uuid_to_instance.find(uuid);
        if (it == uuid_to_instance.end()) {
            SPDLOG_WARN("Build ticket contain unused object");
            continue;
        }
        const ModelInstance* mi = it->second;
        ModelObject* mo_ptr = mi->get_object();
        if (mo_ptr->instances.size() > 1) {
            // divide object instances into separated objects
            auto it = std::find(mo_ptr->instances.begin(), mo_ptr->instances.end(), mi);
            size_t mi_index = it - mo_ptr->instances.begin();
            ModelObject *model_object = mo_ptr->get_model()->add_object(*mo_ptr); // duplicate

            // delete instances except modified one
            for (int inst_idx = static_cast<int>(model_object->instances.size()) - 1; inst_idx >= 0; inst_idx--) {
                if (inst_idx == static_cast<int>(mi_index))
                    continue;
                model_object->delete_instance(inst_idx);
            }
            // remove instance from original object
            mo_ptr->delete_instance(mi_index);

            // change pointer
            mo_ptr = model_object;
        }
        ModelObject &mo = *mo_ptr;        

        std::set<std::string> contain_name;
        for (const BuildTicket::Property &prop : properties) {
            // check uniquity of property name
            if (contain_name.find(prop.name) != contain_name.end()) {
                //BOOST_LOG_TRIVIAL(warning) << "Do not support second use of same property for object(uuid=" << uuid << ") Name: " << prop.name << " with value \"" << prop.value << "\"";
                continue;
            }
            contain_name.insert(prop.name);

            if (prop.name == BT_OBJECT_NAME) {
                // TODO: Check 'prop.value' special character
                mo.name = prop.value;
            } else if (prop.name == BT_EXTRUDER_NUMBER_NAME) {
                mo.config.set_deserialize("extruder", get_extruder_number(prop.value), config_substitutions);
            } else if (prop.name == BT_GENERATE_SUPPORT_NAME) {
                mo.config.set_deserialize("support_material", get_bool_value(prop.value), config_substitutions);
            } else {
                SPDLOG_WARN("Unsupported Build Ticket override name: {} with value \"{}\"", prop.name, prop.value);
            }
        }
    }
}

}

namespace Slic3r {
void process_build_ticket(
    mz_zip_archive &archive,
    const mz_zip_archive_file_stat &stat,
    const format_3MF::CT_Items &items,
    const InstanceMap &instance_map,
    DynamicPrintConfig &config,
    ConfigSubstitutionContext &config_substitutions
) {
    std::unique_ptr<char[]> file_data(new char[stat.m_uncomp_size]);
    if (file_data == nullptr) {
        SPDLOG_ERROR("Can not allocate memmory for build ticket");
        return;
    }

    if (mz_zip_reader_extract_to_mem(&archive, stat.m_file_index, file_data.get(), stat.m_uncomp_size, 0) != MZ_TRUE) {
        SPDLOG_ERROR("Can't extract build ticket file to memory");
        return;
    }

    std::optional<BuildTicket> build_ticket =
        create_build_ticket(file_data.get(), stat.m_uncomp_size);
    if (!build_ticket.has_value())
        return;
    UuidToInstance uuid_to_instance = create_uuid_to_instance(items, instance_map);
    ::apply(*build_ticket, uuid_to_instance, config, config_substitutions);
}
} // namespace Slic3r