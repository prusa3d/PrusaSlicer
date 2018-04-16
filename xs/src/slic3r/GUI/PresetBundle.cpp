//#undef NDEBUG
#include <cassert>

#include "PresetBundle.hpp"
#include "BitmapCache.hpp"

#include <fstream>
#include <boost/filesystem.hpp>
#include <boost/algorithm/clamp.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include <boost/nowide/cenv.hpp>
#include <boost/nowide/cstdio.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/locale.hpp>
#include <boost/log/trivial.hpp>

#include <wx/dcmemory.h>
#include <wx/image.h>
#include <wx/choice.h>
#include <wx/bmpcbox.h>
#include <wx/wupdlock.h>

#include "../../libslic3r/libslic3r.h"
#include "../../libslic3r/PlaceholderParser.hpp"
#include "../../libslic3r/Utils.hpp"

// Store the print/filament/printer presets into a "presets" subdirectory of the Slic3rPE config dir.
// This breaks compatibility with the upstream Slic3r if the --datadir is used to switch between the two versions.
// #define SLIC3R_PROFILE_USE_PRESETS_SUBDIR

namespace Slic3r {

static std::vector<std::string> s_project_options {
    "wiping_volumes_extruders",
    "wiping_volumes_matrix"
};

PresetBundle::PresetBundle() :
    prints(Preset::TYPE_PRINT, Preset::print_options()), 
    filaments(Preset::TYPE_FILAMENT, Preset::filament_options()), 
    printers(Preset::TYPE_PRINTER, Preset::printer_options()),
    m_bitmapCompatible(new wxBitmap),
    m_bitmapIncompatible(new wxBitmap),
    m_bitmapLock(new wxBitmap),
    m_bitmapLockOpen(new wxBitmap),
    m_bitmapCache(new GUI::BitmapCache)
{
    if (wxImage::FindHandler(wxBITMAP_TYPE_PNG) == nullptr)
        wxImage::AddHandler(new wxPNGHandler);

    // Create the ID config keys, as they are not part of the Static print config classes.
    this->prints.preset(0).config.opt_string("print_settings_id", true);
    this->filaments.preset(0).config.opt_string("filament_settings_id", true);
    this->printers.preset(0).config.opt_string("print_settings_id", true);
    // Create the "compatible printers" keys, as they are not part of the Static print config classes.
    this->filaments.preset(0).config.optptr("compatible_printers", true);
    this->filaments.preset(0).config.optptr("compatible_printers_condition", true);
    this->prints.preset(0).config.optptr("compatible_printers", true);
    this->prints.preset(0).config.optptr("compatible_printers_condition", true);
    // Create the "inherits" keys.
    this->prints.preset(0).config.optptr("inherits", true);
    this->filaments.preset(0).config.optptr("inherits", true);
    this->printers.preset(0).config.optptr("inherits", true);
    // Create the "printer_vendor", "printer_model" and "printer_variant" keys.
    this->printers.preset(0).config.optptr("printer_vendor", true);
    this->printers.preset(0).config.optptr("printer_model", true);
    this->printers.preset(0).config.optptr("printer_variant", true);

    this->prints   .load_bitmap_default("cog.png");
    this->filaments.load_bitmap_default("spool.png");
    this->printers .load_bitmap_default("printer_empty.png");
    this->load_compatible_bitmaps();

    this->project_config.apply_only(FullPrintConfig::defaults(), s_project_options);
}

PresetBundle::~PresetBundle()
{
	assert(m_bitmapCompatible != nullptr);
	assert(m_bitmapIncompatible != nullptr);
    assert(m_bitmapLock != nullptr);
    assert(m_bitmapLockOpen != nullptr);
	delete m_bitmapCompatible;
	m_bitmapCompatible = nullptr;
    delete m_bitmapIncompatible;
	m_bitmapIncompatible = nullptr;
    delete m_bitmapLock;
    m_bitmapLock = nullptr;
    delete m_bitmapLockOpen;
    m_bitmapLockOpen = nullptr;
    delete m_bitmapCache;
    m_bitmapCache = nullptr;
}

void PresetBundle::reset(bool delete_files)
{
    // Clear the existing presets, delete their respective files.
    this->prints   .reset(delete_files);
    this->filaments.reset(delete_files);
    this->printers .reset(delete_files);
    this->filament_presets.clear();
    this->filament_presets.emplace_back(this->filaments.get_selected_preset().name);
}

void PresetBundle::setup_directories()
{
    boost::filesystem::path data_dir = boost::filesystem::path(Slic3r::data_dir());
    std::initializer_list<boost::filesystem::path> paths = { 
        data_dir,
		data_dir / "vendor",
#ifdef SLIC3R_PROFILE_USE_PRESETS_SUBDIR
        // Store the print/filament/printer presets into a "presets" directory.
        data_dir / "presets", 
        data_dir / "presets" / "print", 
        data_dir / "presets" / "filament", 
        data_dir / "presets" / "printer" 
#else
        // Store the print/filament/printer presets at the same location as the upstream Slic3r.
        data_dir / "print", 
        data_dir / "filament", 
        data_dir / "printer" 
#endif
    };
    for (const boost::filesystem::path &path : paths) {
		boost::filesystem::path subdir = path;
        subdir.make_preferred();
        if (! boost::filesystem::is_directory(subdir) && 
            ! boost::filesystem::create_directory(subdir))
            throw std::runtime_error(std::string("Slic3r was unable to create its data directory at ") + subdir.string());
    }
}

void PresetBundle::load_presets(const AppConfig &config)
{
    // First load the vendor specific system presets.
    std::string errors_cummulative = this->load_system_presets();

    const std::string dir_user_presets = data_dir()
#ifdef SLIC3R_PROFILE_USE_PRESETS_SUBDIR
        // Store the print/filament/printer presets into a "presets" directory.
        + "/presets"
#else
        // Store the print/filament/printer presets at the same location as the upstream Slic3r.
#endif
        ;
    try {
        this->prints.load_presets(dir_user_presets, "print");
    } catch (const std::runtime_error &err) {
        errors_cummulative += err.what();
    }
    try {
        this->filaments.load_presets(dir_user_presets, "filament");
    } catch (const std::runtime_error &err) {
        errors_cummulative += err.what();
    }
    try {
        this->printers.load_presets(dir_user_presets, "printer");
    } catch (const std::runtime_error &err) {
        errors_cummulative += err.what();
    }
    this->update_multi_material_filament_presets();
    this->update_compatible_with_printer(false);
    if (! errors_cummulative.empty())
        throw std::runtime_error(errors_cummulative);

    this->load_selections(config);
}

// Load system presets into this PresetBundle.
// For each vendor, there will be a single PresetBundle loaded.
std::string PresetBundle::load_system_presets()
{
    // Here the vendor specific read only Config Bundles are stored.
    boost::filesystem::path dir = (boost::filesystem::path(data_dir()) / "vendor").make_preferred();
    std::string errors_cummulative;
    for (auto &dir_entry : boost::filesystem::directory_iterator(dir))
        if (boost::filesystem::is_regular_file(dir_entry.status()) && boost::algorithm::iends_with(dir_entry.path().filename().string(), ".ini")) {
            std::string name = dir_entry.path().filename().string();
            // Remove the .ini suffix.
            name.erase(name.size() - 4);
            try {
                // Load the config bundle, flatten it.
                this->load_configbundle(dir_entry.path().string(), LOAD_CFGBNDLE_SYSTEM);
            } catch (const std::runtime_error &err) {
                errors_cummulative += err.what();
                errors_cummulative += "\n";
            }
        }
    return errors_cummulative;
}

static inline std::string remove_ini_suffix(const std::string &name)
{
    std::string out = name;
    if (boost::iends_with(out, ".ini"))
        out.erase(out.end() - 4, out.end());
    return out;
}

// Set the "enabled" flag for printer vendors, printer models and printer variants
// based on the user configuration.
// If the "vendor" section is missing, enable all models and variants of the particular vendor.
void PresetBundle::load_installed_printers(const AppConfig &config)
{
    // m_storage
}

// Load selections (current print, current filaments, current printer) from config.ini
// This is done just once on application start up.
void PresetBundle::load_selections(const AppConfig &config)
{
    prints.select_preset_by_name(remove_ini_suffix(config.get("presets", "print")), true);
    filaments.select_preset_by_name(remove_ini_suffix(config.get("presets", "filament")), true);
    printers.select_preset_by_name(remove_ini_suffix(config.get("presets", "printer")), true);
    auto   *nozzle_diameter = dynamic_cast<const ConfigOptionFloats*>(printers.get_selected_preset().config.option("nozzle_diameter"));
    size_t  num_extruders   = nozzle_diameter->values.size();   
    this->set_filament_preset(0, filaments.get_selected_preset().name);
    for (unsigned int i = 1; i < (unsigned int)num_extruders; ++ i) {
        char name[64];
        sprintf(name, "filament_%d", i);
        if (! config.has("presets", name))
            break;
        this->set_filament_preset(i, remove_ini_suffix(config.get("presets", name)));
    }
    // Update visibility of presets based on their compatibility with the active printer.
    // Always try to select a compatible print and filament preset to the current printer preset,
    // as the application may have been closed with an active "external" preset, which does not
    // exist.
    this->update_compatible_with_printer(true);
}

// Export selections (current print, current filaments, current printer) into config.ini
void PresetBundle::export_selections(AppConfig &config)
{
    assert(filament_presets.size() >= 1);
    assert(filament_presets.size() > 1 || filaments.get_selected_preset().name == filament_presets.front());
    config.clear_section("presets");
    config.set("presets", "print",    prints.get_selected_preset().name);
    config.set("presets", "filament", filament_presets.front());
	for (int i = 1; i < filament_presets.size(); ++i) {
        char name[64];
        sprintf(name, "filament_%d", i);
        config.set("presets", name, filament_presets[i]);
    }
    config.set("presets", "printer",  printers.get_selected_preset().name);
}

void PresetBundle::export_selections(PlaceholderParser &pp)
{
    assert(filament_presets.size() >= 1);
    assert(filament_presets.size() > 1 || filaments.get_selected_preset().name == filament_presets.front());
    pp.set("print_preset",    prints.get_selected_preset().name);
    pp.set("filament_preset", filament_presets);
    pp.set("printer_preset",  printers.get_selected_preset().name);
}

bool PresetBundle::load_compatible_bitmaps()
{
    const std::string path_bitmap_compatible   = "flag-green-icon.png";
    const std::string path_bitmap_incompatible = "flag-red-icon.png";
    const std::string path_bitmap_lock         = "sys_lock.png";//"lock.png";
	const std::string path_bitmap_lock_open    = "sys_unlock.png";//"lock_open.png";
    bool loaded_compatible   = m_bitmapCompatible  ->LoadFile(
        wxString::FromUTF8(Slic3r::var(path_bitmap_compatible).c_str()), wxBITMAP_TYPE_PNG);
    bool loaded_incompatible = m_bitmapIncompatible->LoadFile(
        wxString::FromUTF8(Slic3r::var(path_bitmap_incompatible).c_str()), wxBITMAP_TYPE_PNG);
    bool loaded_lock = m_bitmapLock->LoadFile(
        wxString::FromUTF8(Slic3r::var(path_bitmap_lock).c_str()), wxBITMAP_TYPE_PNG);
    bool loaded_lock_open = m_bitmapLockOpen->LoadFile(
        wxString::FromUTF8(Slic3r::var(path_bitmap_lock_open).c_str()), wxBITMAP_TYPE_PNG);
    if (loaded_compatible) {
        prints   .set_bitmap_compatible(m_bitmapCompatible);
        filaments.set_bitmap_compatible(m_bitmapCompatible);
//        printers .set_bitmap_compatible(m_bitmapCompatible);
    }
    if (loaded_incompatible) {
        prints   .set_bitmap_incompatible(m_bitmapIncompatible);
        filaments.set_bitmap_incompatible(m_bitmapIncompatible);
//        printers .set_bitmap_incompatible(m_bitmapIncompatible);        
    }
    if (loaded_lock) {
        prints   .set_bitmap_lock(m_bitmapLock);
        filaments.set_bitmap_lock(m_bitmapLock);
        printers .set_bitmap_lock(m_bitmapLock);
    }
    if (loaded_lock_open) {
        prints   .set_bitmap_lock_open(m_bitmapLock);
        filaments.set_bitmap_lock_open(m_bitmapLock);
        printers .set_bitmap_lock_open(m_bitmapLock);
    }
    return loaded_compatible && loaded_incompatible && loaded_lock && loaded_lock_open;
}

DynamicPrintConfig PresetBundle::full_config() const
{    
    DynamicPrintConfig out;
    out.apply(FullPrintConfig());
    out.apply(this->prints.get_edited_preset().config);
    out.apply(this->printers.get_edited_preset().config);
    out.apply(this->project_config);

    auto   *nozzle_diameter = dynamic_cast<const ConfigOptionFloats*>(out.option("nozzle_diameter"));
    size_t  num_extruders   = nozzle_diameter->values.size();

    if (num_extruders <= 1) {
        out.apply(this->filaments.get_edited_preset().config);
    } else {
        // Retrieve filament presets and build a single config object for them.
        // First collect the filament configurations based on the user selection of this->filament_presets.
        std::vector<const DynamicPrintConfig*> filament_configs;
        for (const std::string &filament_preset_name : this->filament_presets)
            filament_configs.emplace_back(&this->filaments.find_preset(filament_preset_name, true)->config);
		while (filament_configs.size() < num_extruders)
            filament_configs.emplace_back(&this->filaments.first_visible().config);
        // Option values to set a ConfigOptionVector from.
        std::vector<const ConfigOption*> filament_opts(num_extruders, nullptr);
        // loop through options and apply them to the resulting config.
        for (const t_config_option_key &key : this->filaments.default_preset().config.keys()) {
			if (key == "compatible_printers")
				continue;
            // Get a destination option.
            ConfigOption *opt_dst = out.option(key, false);
            if (opt_dst->is_scalar()) {
                // Get an option, do not create if it does not exist.
                const ConfigOption *opt_src = filament_configs.front()->option(key);
                if (opt_src != nullptr)
                    opt_dst->set(opt_src);
            } else {
                // Setting a vector value from all filament_configs.
                for (size_t i = 0; i < filament_opts.size(); ++ i)
                    filament_opts[i] = filament_configs[i]->option(key);
                static_cast<ConfigOptionVectorBase*>(opt_dst)->set(filament_opts);
            }
        }
    }

    out.erase("compatible_printers");
    
    static const char *keys[] = { "perimeter", "infill", "solid_infill", "support_material", "support_material_interface" };
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++ i) {
        std::string key = std::string(keys[i]) + "_extruder";
        auto *opt = dynamic_cast<ConfigOptionInt*>(out.option(key, false));
        assert(opt != nullptr);
        opt->value = boost::algorithm::clamp<int>(opt->value, 0, int(num_extruders));
    }

    return out;
}

// Load an external config file containing the print, filament and printer presets.
// Instead of a config file, a G-code may be loaded containing the full set of parameters.
// In the future the configuration will likely be read from an AMF file as well.
// If the file is loaded successfully, its print / filament / printer profiles will be activated.
void PresetBundle::load_config_file(const std::string &path)
{
	if (boost::iends_with(path, ".gcode") || boost::iends_with(path, ".g")) {
		DynamicPrintConfig config;
		config.apply(FullPrintConfig::defaults());
        config.load_from_gcode_file(path);
        Preset::normalize(config);
		load_config_file_config(path, true, std::move(config));
		return;
	}

    // 1) Try to load the config file into a boost property tree.
    boost::property_tree::ptree tree;
    try {
        boost::nowide::ifstream ifs(path);
        boost::property_tree::read_ini(ifs, tree);
    } catch (const std::ifstream::failure &err) {
        throw std::runtime_error(std::string("The config file cannot be loaded: ") + path + "\n\tReason: " + err.what());
    } catch (const std::runtime_error &err) {
        throw std::runtime_error(std::string("Failed loading the preset file: ") + path + "\n\tReason: " + err.what());
    }

    // 2) Continue based on the type of the configuration file.
    ConfigFileType config_file_type = guess_config_file_type(tree);
    switch (config_file_type) {
    case CONFIG_FILE_TYPE_UNKNOWN:
        throw std::runtime_error(std::string("Unknown configuration file type: ") + path);   
    case CONFIG_FILE_TYPE_APP_CONFIG:
        throw std::runtime_error(std::string("Invalid configuration file: ") + path + ". This is an application config file.");
	case CONFIG_FILE_TYPE_CONFIG:
	{
		// Initialize a config from full defaults.
		DynamicPrintConfig config;
		config.apply(FullPrintConfig::defaults());
		config.load(tree);
		Preset::normalize(config);
		load_config_file_config(path, true, std::move(config));
		break;
	}
    case CONFIG_FILE_TYPE_CONFIG_BUNDLE:
		load_config_file_config_bundle(path, tree);
        break;
    }
}

void PresetBundle::load_config_string(const char* str, const char* source_filename)
{
    if (str != nullptr)
    {
        DynamicPrintConfig config;
        config.apply(FullPrintConfig::defaults());
        config.load_from_gcode_string(str);
        Preset::normalize(config);
        load_config_file_config((source_filename == nullptr) ? "" : source_filename, true, std::move(config));
    }
}

// Load a config file from a boost property_tree. This is a private method called from load_config_file.
void PresetBundle::load_config_file_config(const std::string &name_or_path, bool is_external, DynamicPrintConfig &&config)
{
    // The "compatible_printers" field should not have been exported into a config.ini or a G-code anyway, 
    // but some of the alpha versions of Slic3r did.
    {
        ConfigOption *opt_compatible = config.optptr("compatible_printers");
        if (opt_compatible != nullptr) {
            assert(opt_compatible->type() == coStrings);
            if (opt_compatible->type() == coStrings)
                static_cast<ConfigOptionStrings*>(opt_compatible)->values.clear();
        }
    }

    // 1) Create a name from the file name.
    // Keep the suffix (.ini, .gcode, .amf, .3mf etc) to differentiate it from the normal profiles.
    std::string name = is_external ? boost::filesystem::path(name_or_path).filename().string() : name_or_path;

    // 2) If the loading succeeded, split and load the config into print / filament / printer settings.
    // First load the print and printer presets.
    for (size_t i_group = 0; i_group < 2; ++ i_group) {
        PresetCollection &presets = (i_group == 0) ? this->prints : this->printers;
		Preset &preset = presets.load_preset(is_external ? name_or_path : presets.path_from_name(name), name, config);
        if (is_external)
            preset.is_external = true;
        else
            preset.save();
    }

    // 3) Now load the filaments. If there are multiple filament presets, split them and load them.
    auto   *nozzle_diameter   = dynamic_cast<const ConfigOptionFloats*>(config.option("nozzle_diameter"));
    auto   *filament_diameter = dynamic_cast<const ConfigOptionFloats*>(config.option("filament_diameter"));
    size_t  num_extruders     = std::min(nozzle_diameter->values.size(), filament_diameter->values.size());
    if (num_extruders <= 1) {
        Preset &preset = this->filaments.load_preset(
			is_external ? name_or_path : this->filaments.path_from_name(name), name, config);
        if (is_external)
            preset.is_external = true;
        else
            preset.save();
        this->filament_presets.clear();
        this->filament_presets.emplace_back(name);
    } else {
        // Split the filament presets, load each of them separately.
        std::vector<DynamicPrintConfig> configs(num_extruders, this->filaments.default_preset().config);
        // loop through options and scatter them into configs.
        for (const t_config_option_key &key : this->filaments.default_preset().config.keys()) {
            const ConfigOption *other_opt = config.option(key);
            if (other_opt == nullptr)
                continue;
            if (other_opt->is_scalar()) {
                for (size_t i = 0; i < configs.size(); ++ i)
                    configs[i].option(key, false)->set(other_opt);
            } else if (key != "compatible_printers") {
                for (size_t i = 0; i < configs.size(); ++ i)
                    static_cast<ConfigOptionVectorBase*>(configs[i].option(key, false))->set_at(other_opt, 0, i);
            }
        }
        // Load the configs into this->filaments and make them active.
        this->filament_presets.clear();
        for (size_t i = 0; i < configs.size(); ++ i) {
            char suffix[64];
            if (i == 0)
                suffix[0] = 0;
            else
                sprintf(suffix, " (%d)", i);
            std::string new_name = name + suffix;
            // Load all filament presets, but only select the first one in the preset dialog.
            Preset &preset = this->filaments.load_preset(
				is_external ? name_or_path : this->filaments.path_from_name(new_name),
                new_name, std::move(configs[i]), i == 0);
            if (is_external)
                preset.is_external = true;
            else
                preset.save();
            this->filament_presets.emplace_back(new_name);
        }
    }

    // 4) Load the project config values (the per extruder wipe matrix etc).
    this->project_config.apply_only(config, s_project_options);

    this->update_compatible_with_printer(false);
}

// Load the active configuration of a config bundle from a boost property_tree. This is a private method called from load_config_file.
void PresetBundle::load_config_file_config_bundle(const std::string &path, const boost::property_tree::ptree &tree)
{
    // 1) Load the config bundle into a temp data.
    PresetBundle tmp_bundle;
    // Load the config bundle, don't save the loaded presets to user profile directory.
    tmp_bundle.load_configbundle(path, 0);
    std::string bundle_name = std::string(" - ") + boost::filesystem::path(path).filename().string();

    // 2) Extract active configs from the config bundle, copy them and activate them in this bundle.
    auto load_one = [this, &path, &bundle_name](PresetCollection &collection_dst, PresetCollection &collection_src, const std::string &preset_name_src, bool activate) -> std::string {
        Preset *preset_src = collection_src.find_preset(preset_name_src, false);
        Preset *preset_dst = collection_dst.find_preset(preset_name_src, false);
        assert(preset_src != nullptr);
        std::string preset_name_dst;
        if (preset_dst != nullptr && preset_dst->is_default) {
            // No need to copy a default preset, it always exists in collection_dst.
            if (activate)
                collection_dst.select_preset(0);
            return preset_name_src;
        } else if (preset_dst != nullptr && preset_src->config == preset_dst->config) {
            // Don't save as the config exists in the current bundle and its content is the same.
            return preset_name_src;
        } else {
            // Generate a new unique name.
            preset_name_dst = preset_name_src + bundle_name;
            Preset *preset_dup = nullptr;
            for (size_t i = 1; (preset_dup = collection_dst.find_preset(preset_name_dst, false)) != nullptr; ++ i) {
                if (preset_src->config == preset_dup->config)
                    // The preset has been already copied into collection_dst.
                    return preset_name_dst;
                // Try to generate another name.
                char buf[64];
                sprintf(buf, " (%d)", i);
                preset_name_dst = preset_name_src + buf + bundle_name;
            }
        }
        assert(! preset_name_dst.empty());
        // Save preset_src->config into collection_dst under preset_name_dst.
        // The "compatible_printers" field should not have been exported into a config.ini or a G-code anyway, 
        // but some of the alpha versions of Slic3r did.
        ConfigOption *opt_compatible = preset_src->config.optptr("compatible_printers");
        if (opt_compatible != nullptr) {
            assert(opt_compatible->type() == coStrings);
            if (opt_compatible->type() == coStrings)
                static_cast<ConfigOptionStrings*>(opt_compatible)->values.clear();
        }
        collection_dst.load_preset(path, preset_name_dst, std::move(preset_src->config), activate).is_external = true;
        return preset_name_dst;
    };
    load_one(this->prints,    tmp_bundle.prints,    tmp_bundle.prints   .get_selected_preset().name, true);
    load_one(this->filaments, tmp_bundle.filaments, tmp_bundle.filaments.get_selected_preset().name, true);
    load_one(this->printers,  tmp_bundle.printers,  tmp_bundle.printers .get_selected_preset().name, true);
    this->update_multi_material_filament_presets();
    for (size_t i = 1; i < std::min(tmp_bundle.filament_presets.size(), this->filament_presets.size()); ++ i)
        this->filament_presets[i] = load_one(this->filaments, tmp_bundle.filaments, tmp_bundle.filament_presets[i], false);

    this->update_compatible_with_printer(false);
}

// Process the Config Bundle loaded as a Boost property tree.
// For each print, filament and printer preset (group defined by group_name), apply the inherited presets.
// The presets starting with '*' are considered non-terminal and they are
// removed through the flattening process by this function.
// This function will never fail, but it will produce error messages through boost::log.
static void flatten_configbundle_hierarchy(boost::property_tree::ptree &tree, const std::string &group_name)
{
    namespace pt = boost::property_tree;

    typedef std::pair<pt::ptree::key_type, pt::ptree> ptree_child_type;

    // 1) For the group given by group_name, initialize the presets.
    struct Prst {
        Prst(const std::string &name, pt::ptree *node) : name(name), node(node) {}
        // Name of this preset. If the name starts with '*', it is an intermediate preset,
        // which will not make it into the result.
        const std::string           name;
        // Link to the source boost property tree node, owned by tree.
        pt::ptree                  *node;
        // Link to the presets, from which this preset inherits.
        std::vector<Prst*>          inherits;
        // Link to the presets, for which this preset is a direct parent.
        std::vector<Prst*>          parent_of;
        // When running the Kahn's Topological sorting algorithm, this counter is decreased from inherits.size() to zero.
        // A cycle is indicated, if the number does not drop to zero after the Kahn's algorithm finishes.
        size_t                      num_incoming_edges_left = 0;
        // Sorting by the name, to be used when inserted into std::set.
        bool operator==(const Prst &rhs) const { return this->name == rhs.name; }
        bool operator< (const Prst &rhs) const { return this->name < rhs.name; }
    };
    // Find the presets, store them into a std::map, addressed by their names.
    std::set<Prst> presets;
    std::string group_name_preset = group_name + ":";
    for (auto &section : tree)
        if (boost::starts_with(section.first, group_name_preset) && section.first.size() > group_name_preset.size())
            presets.emplace(section.first.substr(group_name_preset.size()), &section.second);
    // Fill in the "inherits" and "parent_of" members, report invalid inheritance fields.
    for (const Prst &prst : presets) {
        // Parse the list of comma separated values, possibly enclosed in quotes.
        std::vector<std::string> inherits_names;
        if (Slic3r::unescape_strings_cstyle(prst.node->get<std::string>("inherits", ""), inherits_names)) {
            // Resolve the inheritance by name.
            std::vector<Prst*> &inherits_nodes = const_cast<Prst&>(prst).inherits;
            for (const std::string &node_name : inherits_names) {
                auto it = presets.find(Prst(node_name, nullptr));
                if (it == presets.end())
                    BOOST_LOG_TRIVIAL(error) << "flatten_configbundle_hierarchy: The preset " << prst.name << " inherits an unknown preset \"" << node_name << "\"";
                else {
                    inherits_nodes.emplace_back(const_cast<Prst*>(&(*it)));
                    inherits_nodes.back()->parent_of.emplace_back(const_cast<Prst*>(&prst));
                }
            }
        } else {
            BOOST_LOG_TRIVIAL(error) << "flatten_configbundle_hierarchy: The preset " << prst.name << " has an invalid \"inherits\" field";
        }
        // Remove the "inherits" key, it has no meaning outside the config bundle.
        const_cast<pt::ptree*>(prst.node)->erase("inherits");
    }

    // 2) Create a linear ordering for the directed acyclic graph of preset inheritance.
    // https://en.wikipedia.org/wiki/Topological_sorting
    // Kahn's algorithm.
    std::vector<Prst*> sorted;
    {
        // Initialize S with the set of all nodes with no incoming edge.
        std::deque<Prst*> S;
        for (const Prst &prst : presets)
            if (prst.inherits.empty())
                S.emplace_back(const_cast<Prst*>(&prst));
            else
                const_cast<Prst*>(&prst)->num_incoming_edges_left = prst.inherits.size();
        while (! S.empty()) {
            Prst *n = S.front();
            S.pop_front();
            sorted.emplace_back(n);
            for (Prst *m : n->parent_of) {
                assert(m->num_incoming_edges_left > 0);
                if (-- m->num_incoming_edges_left == 0) {
                    // We have visited all parents of m.
                    S.emplace_back(m);
                }
            }
        }
        if (sorted.size() < presets.size()) {
            for (const Prst &prst : presets)
                if (prst.num_incoming_edges_left)
                    BOOST_LOG_TRIVIAL(error) << "flatten_configbundle_hierarchy: The preset " << prst.name << " has cyclic dependencies";
        }
    }

    // Apply the dependencies in their topological ordering.
    for (Prst *prst : sorted) {
        // Merge the preset nodes in their order of application.
        // Iterate in a reverse order, so the last change will be placed first in merged.
        for (auto it_inherits = prst->inherits.rbegin(); it_inherits != prst->inherits.rend(); ++ it_inherits)
            for (auto it = (*it_inherits)->node->begin(); it != (*it_inherits)->node->end(); ++ it)
                if (prst->node->find(it->first) == prst->node->not_found())
                    prst->node->add_child(it->first, it->second);
    }

    // Remove the "internal" presets from the ptree. These presets are marked with '*'.
    group_name_preset += '*';
    for (auto it_section = tree.begin(); it_section != tree.end(); ) {
        if (boost::starts_with(it_section->first, group_name_preset) && it_section->first.size() > group_name_preset.size())
            // Remove the "internal" preset from the ptree.
            it_section = tree.erase(it_section);
        else
            // Keep the preset.
            ++ it_section;
    }
}

static void flatten_configbundle_hierarchy(boost::property_tree::ptree &tree)
{
    flatten_configbundle_hierarchy(tree, "print");
    flatten_configbundle_hierarchy(tree, "filament");
    flatten_configbundle_hierarchy(tree, "printer");
}

static void load_vendor_profile(const boost::property_tree::ptree &tree, VendorProfile &vendor_profile)
{
    const std::string printer_model_key = "printer_model:";
    for (auto &section : tree)
        if (section.first == "vendor") {
            // Load the names of the active presets.
            for (auto &kvp : section.second) {
                if (kvp.first == "name")
                    vendor_profile.name = kvp.second.data();
                else if (kvp.first == "id")
                    vendor_profile.id = kvp.second.data();
                else if (kvp.first == "config_version")
                    vendor_profile.config_version = kvp.second.data();
                else if (kvp.first == "config_update_url")
                    vendor_profile.config_update_url = kvp.second.data();
            }
        } else if (boost::starts_with(section.first, printer_model_key)) {
            VendorProfile::PrinterModel model;
            model.name = section.first.substr(printer_model_key.size());
            section.second.get<std::string>("variants", "");
            std::vector<std::string> variants;
            if (Slic3r::unescape_strings_cstyle(section.second.get<std::string>("variants", ""), variants)) {
                for (const std::string &variant_name : variants) {
                    if (model.variant(variant_name) == nullptr)
                        model.variants.emplace_back(VendorProfile::PrinterVariant(variant_name));
                }
            } else {
                // Log error?
            }
            if (! model.name.empty() && ! model.variants.empty())
                vendor_profile.models.insert(model);
        }
}

// Load a config bundle file, into presets and store the loaded presets into separate files
// of the local configuration directory.
void PresetBundle::install_vendor_configbundle(const std::string &src_path0)
{
    boost::filesystem::path src_path(src_path0);
    boost::filesystem::copy_file(src_path, (boost::filesystem::path(data_dir()) / "vendor" / src_path.filename()).make_preferred(), boost::filesystem::copy_option::overwrite_if_exists);
}

// Load a config bundle file, into presets and store the loaded presets into separate files
// of the local configuration directory.
size_t PresetBundle::load_configbundle(const std::string &path, unsigned int flags)
{
    if (flags & (LOAD_CFGBNDLE_RESET_USER_PROFILE | LOAD_CFGBNDLE_SYSTEM))
        // Reset this bundle, delete user profile files if LOAD_CFGBNDLE_SAVE.
        this->reset(flags & LOAD_CFGBNDLE_SAVE);

    // 1) Read the complete config file into a boost::property_tree.
    namespace pt = boost::property_tree;
    pt::ptree tree;
    boost::nowide::ifstream ifs(path);
    pt::read_ini(ifs, tree);
    // Flatten the config bundle by applying the inheritance rules. Internal profiles (with names starting with '*') are removed.
    flatten_configbundle_hierarchy(tree);

    const VendorProfile *vendor_profile = nullptr;
    if (flags & LOAD_CFGBNDLE_SYSTEM) {
        VendorProfile vp;
        load_vendor_profile(tree, vp);
        if (vp.name.empty())
            throw std::runtime_error(std::string("Vendor Config Bundle is not valid: Missing vendor name key."));
        if (vp.num_variants() == 0)
            return 0;
        vendor_profile = &(*this->vendors.insert(vp).first);
    }

    // 2) Parse the property_tree, extract the active preset names and the profiles, save them into local config files.
    std::vector<std::string> loaded_prints;
    std::vector<std::string> loaded_filaments;
    std::vector<std::string> loaded_printers;
    std::string              active_print;
    std::vector<std::string> active_filaments;
    std::string              active_printer;
    size_t                   presets_loaded = 0;
    for (const auto &section : tree) {
        PresetCollection         *presets = nullptr;
        std::vector<std::string> *loaded  = nullptr;
        std::string               preset_name;
        if (boost::starts_with(section.first, "print:")) {
            presets = &this->prints;
            loaded  = &loaded_prints;
            preset_name = section.first.substr(6);
        } else if (boost::starts_with(section.first, "filament:")) {
            presets = &this->filaments;
            loaded  = &loaded_filaments;
            preset_name = section.first.substr(9);
        } else if (boost::starts_with(section.first, "printer:")) {
            presets = &this->printers;
            loaded  = &loaded_printers;
            preset_name = section.first.substr(8);
        } else if (section.first == "presets") {
            // Load the names of the active presets.
            for (auto &kvp : section.second) {
                if (kvp.first == "print") {
                    active_print = kvp.second.data();
                } else if (boost::starts_with(kvp.first, "filament")) {
                    int idx = 0;
                    if (kvp.first == "filament" || sscanf(kvp.first.c_str(), "filament_%d", &idx) == 1) {
                        if (int(active_filaments.size()) <= idx)
                            active_filaments.resize(idx + 1, std::string());
                        active_filaments[idx] = kvp.second.data();
                    }
                } else if (kvp.first == "printer") {
                    active_printer = kvp.second.data();
                }
            }
        } else if (section.first == "settings") {
            // Load the settings.
            for (auto &kvp : section.second) {
                if (kvp.first == "autocenter") {
                }
            }
        } else
            // Ignore an unknown section.
            continue;
        if (presets != nullptr) {
            // Load the print, filament or printer preset.
            DynamicPrintConfig config(presets->default_preset().config);
            for (auto &kvp : section.second)
                config.set_deserialize(kvp.first, kvp.second.data());
            Preset::normalize(config);
            if ((flags & LOAD_CFGBNDLE_SYSTEM) && presets == &printers) {
                // Filter out printer presets, which are not mentioned in the vendor profile.
                // These presets are considered not installed.
                auto printer_model   = config.opt_string("printer_model");
                if (printer_model.empty()) {
                    BOOST_LOG_TRIVIAL(error) << "Error in a Vendor Config Bundle \"" << path << "\": The printer preset \"" << 
                        section.first << "\" defines no printer model, it will be ignored.";
                    continue;
                }
                auto printer_variant = config.opt_string("printer_variant");
                if (printer_variant.empty()) {
                    BOOST_LOG_TRIVIAL(error) << "Error in a Vendor Config Bundle \"" << path << "\": The printer preset \"" << 
                        section.first << "\" defines no printer variant, it will be ignored.";
                    continue;
                }
                auto it_model = vendor_profile->models.find(VendorProfile::PrinterModel(printer_model));
                if (it_model == vendor_profile->models.end()) {
                    BOOST_LOG_TRIVIAL(error) << "Error in a Vendor Config Bundle \"" << path << "\": The printer preset \"" << 
                        section.first << "\" defines invalid printer model \"" << printer_model << "\", it will be ignored.";
                    continue;
                }
                auto it_variant = it_model->variant(printer_variant);
                if (it_variant == nullptr) {
                    BOOST_LOG_TRIVIAL(error) << "Error in a Vendor Config Bundle \"" << path << "\": The printer preset \"" << 
                        section.first << "\" defines invalid printer variant \"" << printer_variant << "\", it will be ignored.";
                    continue;
                }
                const Preset *preset_existing = presets->find_preset(section.first, false);
                if (preset_existing != nullptr) {
                    BOOST_LOG_TRIVIAL(error) << "Error in a Vendor Config Bundle \"" << path << "\": The printer preset \"" << 
                        section.first << "\" has already been loaded from another Confing Bundle.";
                    continue;
                }
            }
            // Decide a full path to this .ini file.
            auto file_name = boost::algorithm::iends_with(preset_name, ".ini") ? preset_name : preset_name + ".ini";
            auto file_path = (boost::filesystem::path(data_dir()) 
#ifdef SLIC3R_PROFILE_USE_PRESETS_SUBDIR
                // Store the print/filament/printer presets into a "presets" directory.
                / "presets" 
#else
                // Store the print/filament/printer presets at the same location as the upstream Slic3r.
#endif
                / presets->name() / file_name).make_preferred();
            // Load the preset into the list of presets, save it to disk.
            Preset &loaded = presets->load_preset(file_path.string(), preset_name, std::move(config), false);
            if (flags & LOAD_CFGBNDLE_SAVE)
                loaded.save();
            if (flags & LOAD_CFGBNDLE_SYSTEM) {
                loaded.is_system = true;
                loaded.vendor = vendor_profile;
            }
            ++ presets_loaded;
        }
    }

    // 3) Activate the presets.
    if ((flags & LOAD_CFGBNDLE_SYSTEM) == 0) {
        if (! active_print.empty()) 
            prints.select_preset_by_name(active_print, true);
        if (! active_printer.empty())
            printers.select_preset_by_name(active_printer, true);
        // Activate the first filament preset.
        if (! active_filaments.empty() && ! active_filaments.front().empty())
            filaments.select_preset_by_name(active_filaments.front(), true);
        this->update_multi_material_filament_presets();
        for (size_t i = 0; i < std::min(this->filament_presets.size(), active_filaments.size()); ++ i)
            this->filament_presets[i] = filaments.find_preset(active_filaments[i], true)->name;
        this->update_compatible_with_printer(false);
    }

    return presets_loaded;
}

void PresetBundle::update_multi_material_filament_presets()
{
    // Verify and select the filament presets.
    auto   *nozzle_diameter = static_cast<const ConfigOptionFloats*>(printers.get_edited_preset().config.option("nozzle_diameter"));
    size_t  num_extruders   = nozzle_diameter->values.size();
    // Verify validity of the current filament presets.
    for (size_t i = 0; i < std::min(this->filament_presets.size(), num_extruders); ++ i)
        this->filament_presets[i] = this->filaments.find_preset(this->filament_presets[i], true)->name;
    // Append the rest of filament presets.
//    if (this->filament_presets.size() < num_extruders)
        this->filament_presets.resize(num_extruders, this->filament_presets.empty() ? this->filaments.first_visible().name : this->filament_presets.back());


    // Now verify if wiping_volumes_matrix has proper size (it is used to deduce number of extruders in wipe tower generator):
    std::vector<double> old_matrix = this->project_config.option<ConfigOptionFloats>("wiping_volumes_matrix")->values;
    size_t old_number_of_extruders = int(sqrt(old_matrix.size())+EPSILON);
    if (num_extruders != old_number_of_extruders) {
            // First verify if purging volumes presets for each extruder matches number of extruders
            std::vector<double>& extruders = this->project_config.option<ConfigOptionFloats>("wiping_volumes_extruders")->values;
            while (extruders.size() < 2*num_extruders) {
                extruders.push_back(extruders.size()>1 ? extruders[0] : 50.);  // copy the values from the first extruder
                extruders.push_back(extruders.size()>1 ? extruders[1] : 50.);
            }
            while (extruders.size() > 2*num_extruders) {
                extruders.pop_back();
                extruders.pop_back();
            }

        std::vector<double> new_matrix;
        for (unsigned int i=0;i<num_extruders;++i)
            for (unsigned int j=0;j<num_extruders;++j) {
                // append the value for this pair from the old matrix (if it's there):
                if (i<old_number_of_extruders && j<old_number_of_extruders)
                    new_matrix.push_back(old_matrix[i*old_number_of_extruders + j]);
                else
                    new_matrix.push_back( i==j ? 0. : extruders[2*i]+extruders[2*j+1]); // so it matches new extruder volumes
            }
		this->project_config.option<ConfigOptionFloats>("wiping_volumes_matrix")->values = new_matrix;
    }
}

void PresetBundle::update_compatible_with_printer(bool select_other_if_incompatible)
{
    this->prints.update_compatible_with_printer(this->printers.get_edited_preset(), select_other_if_incompatible);
    this->filaments.update_compatible_with_printer(this->printers.get_edited_preset(), select_other_if_incompatible);
    if (select_other_if_incompatible) {
        // Verify validity of the current filament presets.
        for (std::string &filament_name : this->filament_presets) {
            Preset *preset = this->filaments.find_preset(filament_name, false);
            if (preset == nullptr || ! preset->is_compatible)
                filament_name = this->filaments.first_compatible().name;
        }
    }
}

void PresetBundle::export_configbundle(const std::string &path) //, const DynamicPrintConfig &settings
{
    boost::nowide::ofstream c;
    c.open(path, std::ios::out | std::ios::trunc);

    // Put a comment at the first line including the time stamp and Slic3r version.
    c << "# " << Slic3r::header_slic3r_generated() << std::endl;

    // Export the print, filament and printer profiles.
    for (size_t i_group = 0; i_group < 3; ++ i_group) {
        const PresetCollection &presets = (i_group == 0) ? this->prints : (i_group == 1) ? this->filaments : this->printers;
        for (const Preset &preset : presets()) {
            if (preset.is_default || preset.is_external)
                // Only export the common presets, not external files or the default preset.
                continue;
            c << std::endl << "[" << presets.name() << ":" << preset.name << "]" << std::endl;
            for (const std::string &opt_key : preset.config.keys())
                c << opt_key << " = " << preset.config.serialize(opt_key) << std::endl;
        }
    }

    // Export the names of the active presets.
    c << std::endl << "[presets]" << std::endl;
    c << "print = " << this->prints.get_selected_preset().name << std::endl;
    c << "printer = " << this->printers.get_selected_preset().name << std::endl;
    for (size_t i = 0; i < this->filament_presets.size(); ++ i) {
        char suffix[64];
        if (i > 0)
            sprintf(suffix, "_%d", i);
        else
            suffix[0] = 0;
        c << "filament" << suffix << " = " << this->filament_presets[i] << std::endl;
    }

#if 0
    // Export the following setting values from the provided setting repository.
    static const char *settings_keys[] = { "autocenter" };
    c << "[settings]" << std::endl;
    for (size_t i = 0; i < sizeof(settings_keys) / sizeof(settings_keys[0]); ++ i)
        c << settings_keys[i] << " = " << settings.serialize(settings_keys[i]) << std::endl;
#endif

    c.close();
}

// Set the filament preset name. As the name could come from the UI selection box, 
// an optional "(modified)" suffix will be removed from the filament name.
void PresetBundle::set_filament_preset(size_t idx, const std::string &name)
{
    if (idx >= filament_presets.size())
        filament_presets.resize(idx + 1, filaments.default_preset().name);
    filament_presets[idx] = Preset::remove_suffix_modified(name);
}

static inline int hex_digit_to_int(const char c)
{
    return 
        (c >= '0' && c <= '9') ? int(c - '0') : 
        (c >= 'A' && c <= 'F') ? int(c - 'A') + 10 :
        (c >= 'a' && c <= 'f') ? int(c - 'a') + 10 : -1;
}

bool PresetBundle::parse_color(const std::string &scolor, unsigned char *rgb_out)
{
    rgb_out[0] = rgb_out[1] = rgb_out[2] = 0;
    if (scolor.size() != 7 || scolor.front() != '#')
        return false;
    const char *c = scolor.data() + 1;
    for (size_t i = 0; i < 3; ++ i) {
        int digit1 = hex_digit_to_int(*c ++);
        int digit2 = hex_digit_to_int(*c ++);
        if (digit1 == -1 || digit2 == -1)
            return false;
        rgb_out[i] = (unsigned char)(digit1 * 16 + digit2);
    }
    return true;
}

void PresetBundle::update_platter_filament_ui(unsigned int idx_extruder, wxBitmapComboBox *ui)
{
    if (ui == nullptr)
        return;

    unsigned char rgb[3];
    std::string extruder_color = this->printers.get_edited_preset().config.opt_string("extruder_colour", idx_extruder);
    if (! parse_color(extruder_color, rgb))
        // Extruder color is not defined.
        extruder_color.clear();

    // Fill in the list from scratch.
    ui->Freeze();
    ui->Clear();
    const Preset *selected_preset = this->filaments.find_preset(this->filament_presets[idx_extruder]);
    // Show wide icons if the currently selected preset is not compatible with the current printer,
    // and draw a red flag in front of the selected preset.
    bool          wide_icons      = selected_preset != nullptr && ! selected_preset->is_compatible && m_bitmapIncompatible != nullptr;
    assert(selected_preset != nullptr);
	std::map<wxString, wxBitmap> nonsys_presets;
	wxString selected_str = "";
    for (int i = this->filaments().front().is_visible ? 0 : 1; i < int(this->filaments().size()); ++ i) {
        const Preset &preset    = this->filaments.preset(i);
        bool          selected  = this->filament_presets[idx_extruder] == preset.name;
		if (! preset.is_visible || (! preset.is_compatible && ! selected))
			continue;
		// Assign an extruder color to the selected item if the extruder color is defined.
		std::string   filament_rgb = preset.config.opt_string("filament_colour", 0);
		std::string   extruder_rgb = (selected && !extruder_color.empty()) ? extruder_color : filament_rgb;
        bool          single_bar   = filament_rgb == extruder_rgb;
        std::string   bitmap_key   = single_bar ? filament_rgb : filament_rgb + extruder_rgb;
        // If the filament preset is not compatible and there is a "red flag" icon loaded, show it left
        // to the filament color image.
        if (wide_icons)
            bitmap_key += preset.is_compatible ? ",cmpt" : ",ncmpt";
        bitmap_key += (preset.is_system || preset.is_default) ? ",syst" : ",nsyst";
        if (preset.is_dirty)
            bitmap_key += ",drty";
        wxBitmap     *bitmap       = m_bitmapCache->find(bitmap_key);
        if (bitmap == nullptr) {
            // Create the bitmap with color bars.
            std::vector<wxBitmap> bmps;
            if (wide_icons)
                // Paint a red flag for incompatible presets.
                bmps.emplace_back(preset.is_compatible ? m_bitmapCache->mkclear(16, 16) : *m_bitmapIncompatible);
            // Paint the color bars.
            parse_color(filament_rgb, rgb);
            bmps.emplace_back(m_bitmapCache->mksolid(single_bar ? 24 : 16, 16, rgb));
            if (! single_bar) {
                parse_color(extruder_rgb, rgb);
                bmps.emplace_back(m_bitmapCache->mksolid(8,  16, rgb));
            }
            // Paint a lock at the system presets.
            bmps.emplace_back(m_bitmapCache->mkclear(4, 16));
            bmps.emplace_back((preset.is_system || preset.is_default) ? 
                (preset.is_dirty ? *m_bitmapLockOpen : *m_bitmapLock) : m_bitmapCache->mkclear(16, 16));
            bitmap = m_bitmapCache->insert(bitmap_key, bmps);
		}
// 		ui->Append(wxString::FromUTF8((preset.name + (preset.is_dirty ? Preset::suffix_modified() : "")).c_str()), (bitmap == 0) ? wxNullBitmap : *bitmap);
//         if (selected)
//             ui->SetSelection(ui->GetCount() - 1);

		if (preset.is_default || preset.is_system){
			ui->Append(wxString::FromUTF8((preset.name + (preset.is_dirty ? Preset::suffix_modified() : "")).c_str()), 
				(bitmap == 0) ? wxNullBitmap : *bitmap);
			if (selected)
				ui->SetSelection(ui->GetCount() - 1);
		}
		else
		{
			nonsys_presets.emplace(wxString::FromUTF8((preset.name + (preset.is_dirty ? Preset::suffix_modified() : "")).c_str()), 
				(bitmap == 0) ? wxNullBitmap : *bitmap);
			if (selected)
				selected_str = wxString::FromUTF8((preset.name + (preset.is_dirty ? Preset::suffix_modified() : "")).c_str());
		}
		if (preset.is_default)
			ui->Append("------------------------------------", wxNullBitmap);
    }

	if (!nonsys_presets.empty())
	{
		ui->Append("------------------------------------", wxNullBitmap);
		for (std::map<wxString, wxBitmap>::iterator it = nonsys_presets.begin(); it != nonsys_presets.end(); ++it) {
			ui->Append(it->first, it->second);
			if (it->first == selected_str)
				ui->SetSelection(ui->GetCount() - 1);
		}
	}
    ui->Thaw();
}

void PresetBundle::set_default_suppressed(bool default_suppressed)
{
    prints.set_default_suppressed(default_suppressed);
    filaments.set_default_suppressed(default_suppressed);
    printers.set_default_suppressed(default_suppressed);
}

} // namespace Slic3r
