#include "Slic3r/Biz/Emboss/TextPresetManager.hpp"
#include "Slic3r/Biz/CerealUtils.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/App/IDialogManager.hpp"
#include <Slic3r/App/AppServices.hpp> // singleton for dialog
#include <fast_float.h>
#include <Slic3r/Log.hpp>

using namespace Slic3r;

/// <summary>
/// For store/load emboss style to/from file
/// </summary>
namespace {
constexpr std::uint32_t STYLE_OBJ_VERSION = 1;
using PresetsObj                           = Biz::Emboss::TextPresetManager::PresetsObj;
void store_styles_obj(const std::string& path, /*const */ PresetsObj& data);
bool load_styles_obj(const std::string& path, PresetsObj& data);
using Presets = Biz::Emboss::TextPresetManager::Presets;
void make_unique_name(const Presets& presets, std::string& name);
Presets create_default_styles(Biz::Emboss::IFontManager&);
} // namespace

namespace Slic3r::Biz::Emboss {
TextPresetManager::TextPresetManager(
    IFontManager& font_manager,
    const ::std::string& cache_path,
    Biz::ProjectInteractor& project_interactor
) :
    m_font_manager(font_manager),
    m_cache_path(cache_path),
    m_proj_preset_cache(project_interactor)
{}

void TextPresetManager::init()
{
    if (!m_data.presets.empty())
        return; // already initialized

    if (!load_styles_obj(m_cache_path, m_data)) {
        // No styles loaded from ini file so use default
        m_data.presets        = create_default_styles(m_font_manager);
        m_data.current_index = 0;
    }

    // Different monitor(scale factor) creates different wxFont descriptor
    // (3rd serialized param on the windows is font size in pixels -> pixelSize)
    for (Preset& preset : m_data.presets) {
        Domain::FontDescriptor &fd = preset.emboss_style.descriptor;
        auto current_fd = m_font_manager.get_current_descriptor(fd);
        if(current_fd.has_value())
            fd.path = current_fd->path;
    }

    // find valid font item
    if (load_preset(m_data.current_index))
        return; // style is loaded

    // Try to fix that style can't be loaded
    m_data.presets.erase(m_data.presets.begin() + m_data.current_index);
    load_valid_preset();
}

bool TextPresetManager::store_presets(bool use_modification, bool store_active_index)
{
    if (m_data.presets.empty())
        return false;
    PresetCache& cache = m_proj_preset_cache.selected();
    if (use_modification) {
        if (exist_stored_style()) {
            // update stored item
            m_data.presets[*cache.preset_index] = cache.preset;
        } else {
            // add new into stored list
            Domain::EmbossStyle& style = cache.preset.emboss_style;
            ::make_unique_name(m_data.presets, style.descriptor.name);
            cache.preset_index = m_data.presets.size();
            m_data.presets.emplace_back(Preset{.emboss_style=style});
        }
    }
    if (store_active_index && exist_stored_style()) {
        m_data.current_index = *cache.preset_index;
    }
    store_styles_obj(m_cache_path, m_data);
    return true;
}

void TextPresetManager::save_preset_as() {
    auto& dlg_manager = App::AppServices::instance().dialog_manager();
    App::IDialogManager::YesNoCallback callback;
    auto save_preset_as_fn = [this, &dlg_manager, &callback]()
    {
        std::string name = dlg_manager.show_input_dialog(
            _u8L("New preset name") + ':',
            _u8L("Type unique preset name to save current settings"),
            get_preset().emboss_style.descriptor.name
        );
        if (name.empty())
            return;
        if (!is_unique_style_name(name)) {
            dlg_manager.show_yesno_dialog(
                _u8L("Preset name is not unique"),
                _u8L("Preset already exists, would you like to try a new name?"),
                callback
            );
        } else {
            PresetCache& cache         = m_proj_preset_cache.selected();
            Domain::EmbossStyle& style = cache.preset.emboss_style;
            style.descriptor.name      = name;
            ::make_unique_name(m_data.presets, style.descriptor.name);
            cache.preset_index = m_data.presets.size();
            m_data.presets.emplace_back(Preset{.emboss_style = style});
            m_data.current_index = *cache.preset_index;
            store_presets();
        }
    };
    callback = [&](bool yes) {if (yes) save_preset_as_fn(); };
    save_preset_as_fn();
}

void TextPresetManager::rename_preset()
{
    auto& dlg_manager = App::AppServices::instance().dialog_manager();
    App::IDialogManager::YesNoCallback callback;
    auto rename_preset_fn = [this, &dlg_manager, &callback]()
    {
        std::string name = dlg_manager.show_input_dialog(
            _u8L("Rename preset") + ':',
            _u8L("Type unique preset name to save current settings"),
            get_preset().emboss_style.descriptor.name
        );
        if (name.empty())
            return;
        if (!is_unique_style_name(name)) {
            dlg_manager.show_yesno_dialog(
                _u8L("Preset name is not unique"),
                _u8L("Preset already exists, would you like to try a new name?"),
                callback
            );
        } else {
            PresetCache& cache                        = m_proj_preset_cache.selected();
            cache.preset.emboss_style.descriptor.name = name;
            if (!exist_stored_style())
                return;

            m_data.presets[*cache.preset_index].emboss_style.descriptor.name = name;

            // rename in all projects
            for (Domain::SelectionId project_id : m_proj_preset_cache.get_project_ids()) {
                PresetCache& cache_ = m_proj_preset_cache.project(project_id);
                if (cache_.preset_index != cache.preset_index)
                    continue;
                cache_.preset.emboss_style.descriptor.name = name;
            }
            store_presets();
        }
    };
    callback = [&](bool yes) {if (yes) rename_preset_fn(); };
    rename_preset_fn();
}

bool TextPresetManager::delete_preset()
{
    /// <summary>
    /// Remove preset
    /// Fix selected index when index is under m_font_selected
    /// </summary>
    /// <param name="index">Index of preset to be removed</param>
    auto erase = [this](size_t index) {
        if (index >= m_data.presets.size())
            return;

        // fix selected index in all projects data        
        for (Domain::SelectionId project_id: m_proj_preset_cache.get_project_ids()) {
            PresetCache &cache = m_proj_preset_cache.project(project_id);
            if (!cache.preset_index.has_value())
                continue; // not selected

            size_t& i = *cache.preset_index;
            if (index < i) {
                --i;
            } else if (index == i) {
                cache.preset_index.reset();
            }
        }
        m_data.presets.erase(m_data.presets.begin() + index);
    };

    std::string style_name = get_preset().emboss_style.descriptor.name; // copy
    size_t next_style_index = std::numeric_limits<size_t>::max();
    bool exist_change = false;
    while (true) {
        // NOTE: can't use previous loaded activ index -> erase could change index
        size_t active_index = get_preset_index();
        next_style_index = (active_index > 0) ? active_index - 1 :
            active_index + 1;

        if (next_style_index >= get_presets().size()) {
            //MessageDialog msg(plater, _L("Can't remove the last existing preset."), dialog_title, wxICON_ERROR | wxOK);
            //msg.ShowModal();
            break;
        }

        // IMPROVE: add function can_load?
        // clean unactivable styles
        if (!load_preset(next_style_index)) {
            erase(next_style_index);
            exist_change = true;
            continue;
        }

        //wxString message = GUI::format_wxstr(_L("Are you sure you want to permanently remove the \"%1%\" preset?"), style_name);
        //MessageDialog msg(plater, message, dialog_title, wxICON_WARNING | wxYES | wxNO);
        //if (msg.ShowModal() == wxID_YES) {
            // delete preset
        erase(active_index);
        exist_change = true;
        //process();
    //}
    //else {
    //    // load back preset
    //    load_preset(active_index);
    //}
        break;
    }
    if (exist_change) {
        store_presets(false);
        return true;
    }
    return false;
}

void TextPresetManager::discard_preset_changes()
{
    if (exist_stored_style()) {
        if (load_preset(*m_proj_preset_cache.selected().preset_index))
            return; // correct reload style
    } else {
        if (load_preset(m_data.current_index))
            return; // correct load last used style
    }

    // try to save situation by load some font
    load_valid_preset();
}

void TextPresetManager::load_valid_preset()
{
    // iterate over all known styles
    while (!m_data.presets.empty()) {
        if (load_preset(0))
            return;
        // can't load so erase it from list
        m_data.presets.erase(m_data.presets.begin());
    }

    // no one style is loadable
    // set up default font list
    m_data.presets        = create_default_styles(m_font_manager);
    m_data.current_index = 0;

    // iterate over default styles
    // There have to be option to use build in font
    while (!m_data.presets.empty()) {
        if (load_preset(0))
            return;
        // can't load so erase it from list
        m_data.presets.erase(m_data.presets.begin());
    }

    // This OS doesn't have TTF as default font,
    // find some loadable font out of default list
    assert(false);
}

bool TextPresetManager::load_preset(size_t style_index)
{
    if (style_index >= m_data.presets.size())
        return false;
    if (!load_preset(m_data.presets[style_index]))
        return false;
    m_proj_preset_cache.selected().preset_index = style_index;
    m_data.current_index      = style_index;
    return true;
}

bool TextPresetManager::load_preset(const Preset& style)
{
    const Domain::FontDescriptor descriptor = style.emboss_style.descriptor;
    std::unique_ptr<const Domain::FontFile> font_ptr = 
        m_font_manager.open(descriptor);
    if (font_ptr == nullptr)
        return false;

    PresetCache& cache = m_proj_preset_cache.selected();
    //cache.font_file = FontFileWithCache(descriptor, std::move(font_ptr));
    cache.preset       = style; // copy
    cache.preset_index = std::numeric_limits<size_t>::max();
    return true;
}

bool TextPresetManager::is_unique_style_name(const std::string& name) const
{
    for (const TextPresetManager::Preset& style : m_data.presets)
        if (style.emboss_style.descriptor.name == name)
            return false;
    return true;
}

const TextPresetManager::Preset* TextPresetManager::get_stored_preset() const
{
    const std::optional<size_t> preset_index = m_proj_preset_cache.selected().preset_index;
    if (!preset_index.has_value() || *preset_index >= m_data.presets.size())
        return nullptr;
    return &m_data.presets[*preset_index];
}

FontFileWithCache& TextPresetManager::get_font_file_with_cache()
{
    PresetCache& cache = m_proj_preset_cache.selected();
    FontFileWithCache& ff = cache.font_file;
    const Domain::FontDescriptor& fd = cache.preset.emboss_style.descriptor;
    if (ff.has_value() && (ff.descriptor.path == fd.path)) {
        return ff; // use cache
    }
    // create new cache
    ff = FontFileWithCache(fd, m_font_manager.open(fd));
    return ff;
}

void TextPresetManager::set_font(const Domain::FontDescriptor& font_descriptor)
{
    PresetCache& cache = m_proj_preset_cache.selected();
    cache.font_file.font_file = nullptr; // discard cache
    Domain::FontDescriptor& cache_descriptor = cache.preset.emboss_style.descriptor;
    if (cache_descriptor.type != font_descriptor.type) {
        // discard style name(FontDescriptor::name)
        cache_descriptor = font_descriptor;
    } else {
        cache_descriptor.path = font_descriptor.path;
    }
}

const TextPresetManager::Presets& TextPresetManager::get_presets() const
{
    return m_data.presets;
}

std::vector<std::string> TextPresetManager::get_presets_names() const 
{
    std::vector<std::string> names;
    names.reserve(m_data.presets.size());
    for (const Biz::Emboss::TextPresetManager::Preset& style : m_data.presets)
        names.push_back(style.emboss_style.descriptor.name);
    return names;
}
} // namespace Slic3r::Biz::Emboss

#include <fstream>
// cache font list by cereal
#include <cereal/cereal.hpp>
#include <cereal/types/vector.hpp>
#include <cereal/types/optional.hpp>
#include <cereal/archives/binary.hpp>

namespace cereal {
template <class Archive>
void serialize(Archive& ar, Biz::Emboss::TextPresetManager::Preset& s)
{
    // ignore truncated_name and image(which are created on demand)
    ar((Domain::EmbossStyle&) s, s.projection, s.distance, s.angle);
}

template <class Archive>
void serialize(Archive& ar, Biz::Emboss::TextPresetManager::PresetsObj& data, const std::uint32_t version)
{
    // When performing a load, the version associated with the class
    // is whatever it was when that data was originally serialized
    // When we save, we'll use the version that is defined in the macro
    if (version != ::STYLE_OBJ_VERSION)
        return;
    ar(data.presets, data.current_index);
}
} // namespace cereal

// StylesSerializable
namespace {
void store_styles_obj(const std::string& path, PresetsObj& data)
{
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        SPDLOG_ERROR("Text Preset can't be stored. Failed to open cache file {} for writing.", path);
        return;
    }
    cereal::BinaryOutputArchive archive(file);
    try {
        archive(data);
    } catch (const std::exception& ex) {
        SPDLOG_ERROR("Failed to store file - {}: {}", path, ex.what());
    }
}

bool load_styles_obj(const std::string& path, PresetsObj& data)
{
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) // Cache File not found (not created yet)
        return false;
    cereal::BinaryInputArchive archive(file);
    try {
        archive(data);
    } catch (const std::exception& ex) {
        SPDLOG_ERROR("Failed to read from file - {}: {}", path, ex.what());
        return false;
    }
    return true;
}

void make_unique_name(const Biz::Emboss::TextPresetManager::Presets& presets, std::string& name)
{
    auto is_unique = [&presets](const std::string& name) {
        for (const Biz::Emboss::TextPresetManager::Preset& it : presets)
            if (it.emboss_style.descriptor.name == name)
                return false;
        return true;
    };

    // Preset name can't be empty so default name is set
    if (name.empty())
        name = "Text style";

    // When name is already unique, nothing need to be changed
    if (is_unique(name))
        return;

    // when there is previous version of style name only find number
    const char* prefix = " (";
    const char suffix  = ')';
    auto pos           = name.find_last_of(prefix);
    if (name.c_str()[name.size() - 1] == suffix && pos != std::string::npos) {
        // short name by ord number
        name = name.substr(0, pos);
    }

    int order = 1; // start with value 2 to represents same font name
    std::string new_name;
    do {
        new_name = name + prefix + std::to_string(++order) + suffix;
    } while (!is_unique(new_name));
    name = new_name;
}

Presets create_default_styles(Biz::Emboss::IFontManager& font_manager)
{
    Presets presets;
    Domain::FontList favorits = font_manager.create_favorit();
    for (Domain::FontDescriptor& favorit : favorits) {
        ::make_unique_name(presets, favorit.name);
        presets.push_back(
            Biz::Emboss::TextPresetManager::Preset{.emboss_style = Domain::EmbossStyle{.descriptor = favorit}}
        );
    }
    return presets;
}

} // namespace

CEREAL_CLASS_VERSION(Slic3r::Biz::Emboss::TextPresetManager::PresetsObj, ::STYLE_OBJ_VERSION); // register class version
