///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_TextureSkinPickerDialog_hpp_
#define slic3r_TextureSkinPickerDialog_hpp_

#include <string>

#include "GUI_Utils.hpp"
#include "libslic3r/Feature/TextureSkin/TexturePatterns.hpp"

class wxListCtrl;
class wxImageList;
class wxListEvent;

namespace Slic3r::GUI {

// Modal thumbnail-grid picker for Texture Skin built-in patterns, plus a
// "Custom…" tile that opens a file chooser for a user PNG.
class TextureSkinPickerDialog : public DPIDialog
{
public:
    using Pattern = Slic3r::Feature::TextureSkin::Pattern;

    TextureSkinPickerDialog(wxWindow* parent, Pattern initial, const std::string& initial_custom_path);
    ~TextureSkinPickerDialog() override;

    Pattern            get_pattern() const          { return m_pattern; }
    const std::string& get_custom_image_path() const { return m_custom_path; }

protected:
    void on_dpi_changed(const wxRect& /*suggested_rect*/) override {}
    void on_sys_color_changed() override {}

private:
    void        populate_list();
    void        accept_selection(long list_index);
    std::string prompt_custom_file();

    wxListCtrl*  m_list_ctrl  { nullptr };
    wxImageList* m_image_list { nullptr };

    Pattern      m_pattern      { Pattern::Knurling };
    std::string  m_custom_path;
};

} // namespace Slic3r::GUI

#endif // slic3r_TextureSkinPickerDialog_hpp_
