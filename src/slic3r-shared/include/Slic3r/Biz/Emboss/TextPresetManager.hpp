#ifndef slic3r_EmbossTextPresetManager_hpp_
#define slic3r_EmbossTextPresetManager_hpp_

#include <memory>
#include <optional>
#include <string>
#include <functional>

#include "Slic3r/Domain/FontFile.hpp"
#include "Slic3r/Biz/Emboss/Emboss.hpp"
#include "Slic3r/Biz/Emboss/IFontManager.hpp"
#include "Slic3r/Biz/ProjectScoped.hpp"
#include "Slic3r/Domain/BoundingBox.hpp"
#include "Slic3r/Domain/TextConfiguration.hpp"
#include "Slic3r/Domain/EmbossShape.hpp"

namespace Slic3r::Biz::Emboss {
/**
@brief Manage Emboss text preset(style of the embossing)
Cache current state of preset
*/
class TextPresetManager
{
public:
    /**
    @param font_manager Accessor to font file data via Domain::FontDescriptor
    @param cache_path File path for store cache with current user Emboss presets.
        @note data_dir() + "/emboss_presets.cereal"
    @param project_interactor For initialize ProjectScoped data only
    */
    TextPresetManager(
        IFontManager& font_manager,
        const std::string& cache_path,
        Biz::ProjectInteractor& project_interactor
    );

    /**
    @brief Load font presets from file,
    Also select actual activ font
    */
    void init();

    /**
    @brief Write font list into AppConfig
    @param use_modification When true cache state will be used for store
    @param store_active_index When treu also store current activ index
    @return True on succes otherwise False.
    */
    bool store_presets(bool use_modification = true, bool store_active_index = true);

    void save_preset_as();
    void rename_preset();
    bool delete_preset();

    /**
    @brief Discard changes in current preset
    When no activ preset use last used OR first loadable
    */
    void discard_preset_changes();

    /**
    @brief load some valid preset
    */
    void load_valid_preset();

    /**
    @brief Change current preset
    When font can't load, roll back current preset
    @param preset_index New preset index(from m_presets range)
    @return True on succes. False on fail load font
    */
    bool load_preset(size_t preset_index);
    // load font preset not stored in list
    struct Preset;
    bool load_preset(const Preset& preset);

    // setter for font
    void set_font(const Domain::FontDescriptor& font_descriptor);

    // getters for private data
    const Preset* get_stored_preset() const;

    const Preset& get_preset() const
    {
        return m_proj_preset_cache.selected().preset;
    }

    Preset& get_preset()
    {
        return m_proj_preset_cache.selected().preset;
    }
    
    // For fill select box in dialog, used together with get_preset_index() 
    std::vector<std::string> get_presets_names() const;
    int get_preset_index() const
    {
        size_t res = m_proj_preset_cache.selected().preset_index
            .value_or(m_data.presets.size()); // index or out of range
        return static_cast<int>(res);
    }

    const Domain::FontProp& get_font_prop() const
    {
        return get_preset().emboss_style.prop;
    }

    Domain::FontProp& get_font_prop()
    {
        return get_preset().emboss_style.prop;
    }

    FontFileWithCache& get_font_file_with_cache();

    bool has_collections() const
    {
        const FontFileWithCache& ff = m_proj_preset_cache.selected().font_file;
        return ff.has_value()
            && ff.font_file->infos.size() > 1;
    }

    // True when activ style has same name as some of stored style
    bool exist_stored_style() const
    {
        return m_proj_preset_cache.selected().preset_index.has_value();
    }

    bool is_unique_style_name(const std::string& name) const;

    // access to all managed font styles
    const std::vector<Preset>& get_presets() const;

    /**
    @brief All connected with one style
    keep temporary data and caches for style
    */
    struct Preset
    {
        Domain::EmbossStyle emboss_style;

        // Define how to emboss shape
        Domain::EmbossProjection projection;

        // distance from surface point
        // used for move over model surface
        // When not set value is zero and is not stored
        std::optional<float> distance; // [in mm]

        // Angle of rotation around emboss direction (Z axis)
        // It is calculate on the fly from volume world transformation
        // only TextPresetManager keep actual value for comparision with style
        // When not set value is zero and is not stored
        std::optional<float> angle; // [in radians] form -Pi to Pi

        bool operator==(const Preset& other) const
        {
            return emboss_style == other.emboss_style
                && projection == other.projection
                && Domain::is_approx(distance, other.distance)
                && Domain::is_approx(angle, other.angle);
        }
    };

    using Presets = std::vector<Preset>;
    struct PresetsObj {
        Presets presets;
        size_t current_index;
    };
private:
    IFontManager& m_font_manager;
    std::string m_cache_path;

    /**
    @brief Cache data from style to reduce amount of:
    1) loading font from file
    2) Keep loaded(and modified by style) glyphs from font
    */
    struct PresetCache
    {
        // share font file data with emboss job thread
        FontFileWithCache font_file = {};

        // actual used font item
        Preset preset = {};

        // index into m_presets
        std::optional<size_t> preset_index;
    };

    Biz::ProjectScoped<PresetCache> m_proj_preset_cache;

    // Privat member
    PresetsObj m_data;
};

} // namespace Slic3r::Biz::Emboss

#endif // slic3r_EmbossTextPresetManager_hpp_
