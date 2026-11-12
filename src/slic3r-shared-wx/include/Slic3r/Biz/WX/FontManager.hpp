#pragma once
#include "Slic3r/Biz/Emboss/IFontManager.hpp"
#include <optional>
#include <memory>

#include <wx/string.h>
#include <boost/filesystem.hpp>

namespace Slic3r::Biz::WX {

/// <summary>
/// Use Wx library to enumerate OS installed fonts
/// and acceess to file data by wxFont
/// </summary>
class FontManager : public Emboss::IFontManager
{
public:
    /// <summary>
    /// Constructs a FontManager object using the specified data directory.
    /// </summary>
    /// <param name="data_dir">Directory path for store Cache(installed openable fonts)</param>
    FontManager(const std::string& data_dir);

    /// <summary>
    /// Invalidate font list cache and read new one from OS
    /// Filtrate already founded bad fonts(+vertical fonts)
    /// </summary>
    /// <returns>Os installed valid fonts</returns>
    const Domain::FontList& get_fonts() override;

    /// <summary>
    /// Create new wx font and get font file data from wxFont
    /// </summary>
    /// <param name="descriptor">Contain serialized string for font</param>
    /// <returns>Font file on succcess otherwise nullptr</returns>
    std::unique_ptr<const Domain::FontFile> open(const Domain::FontDescriptor& descriptor) override;

    /// <summary>
    /// Create wxFont from the descriptor and than convert the wxFont back to the current used descriptor.
    /// </summary>
    /// <param name="descriptor">Input descriptor, different language operating system, size, ...</param>
    /// <returns>One from the enumerated descriptors(font list)</returns>
    Descriptor get_current_descriptor(const Domain::FontDescriptor& other_descriptor) override;

    /// <summary>
    /// Retrieves the current font descriptor type.
    /// Distiquish Win/Lin/Mac
    /// </summary>
    /// <returns>The current font descriptor type as a value of Domain::FontDescriptor::Type.</returns>
    Domain::FontDescriptor::Type get_current_type() const override;

    /// <summary>
    /// Create descriptors from static defined wxFont for curren
    /// * wxNORMAL_FONT
    /// * wxSMALL_FONT
    /// * wxITALIC_FONT
    /// * wxSWISS_FONT
    /// * from wxFONTFAMILY_MODERN
    /// </summary>
    /// <returns>List of favorits font descriptors in current OS</returns>
    Domain::FontList create_favorit() override;
private:
    // data of can_load() faces
    Domain::FontList m_openable;
    std::vector<wxString> m_valid; // same size as m_openable

    // Sorter set of Non valid face names in OS
    std::vector<wxString> m_bad;

    std::optional<size_t> m_hash;

    boost::filesystem::path m_cache_path;

    std::string m_data_dir;
};
} // namespace Slic3r::Biz::WX
