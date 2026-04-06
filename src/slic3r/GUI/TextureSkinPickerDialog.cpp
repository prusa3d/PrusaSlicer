///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "TextureSkinPickerDialog.hpp"

#include <wx/bitmap.h>
#include <wx/dcmemory.h>
#include <wx/filedlg.h>
#include <wx/image.h>
#include <wx/imaglist.h>
#include <wx/listctrl.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/settings.h>

#include "I18N.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "libslic3r/Utils.hpp"

namespace Slic3r::GUI {

namespace TextureFeature = Slic3r::Feature::TextureSkin;

static constexpr int THUMB_PX = 120;

static wxBitmap make_custom_tile_bitmap(int px)
{
    wxBitmap bmp(px, px);
    wxMemoryDC dc(bmp);
    dc.SetBackground(wxBrush(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE)));
    dc.Clear();
    dc.SetPen(wxPen(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNSHADOW), 2, wxPENSTYLE_LONG_DASH));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawRectangle(4, 4, px - 8, px - 8);
    dc.SetTextForeground(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNTEXT));
    wxFont font = dc.GetFont();
    font.SetPointSize(font.GetPointSize() + 4);
    font.MakeBold();
    dc.SetFont(font);
    const wxString label = "+";
    wxCoord tw = 0, th = 0;
    dc.GetTextExtent(label, &tw, &th);
    dc.DrawText(label, (px - tw) / 2, (px - th) / 2 - 8);
    font.SetPointSize(font.GetPointSize() - 4);
    font.SetWeight(wxFONTWEIGHT_NORMAL);
    dc.SetFont(font);
    const wxString sub = _("Custom…");
    dc.GetTextExtent(sub, &tw, &th);
    dc.DrawText(sub, (px - tw) / 2, (px / 2) + 8);
    dc.SelectObject(wxNullBitmap);
    return bmp;
}

static wxBitmap load_pattern_bitmap(TextureFeature::Pattern p, int px)
{
    const char* fname = TextureFeature::pattern_filename(p);
    if (!fname || !*fname) return wxBitmap(px, px);
    const std::string path = Slic3r::resources_dir() + "/textures/texture_skin/" + fname;
    wxImage image;
    if (!image.CanRead(from_u8(path)) || !image.LoadFile(from_u8(path), wxBITMAP_TYPE_PNG) ||
        image.GetWidth() == 0 || image.GetHeight() == 0) {
        return wxBitmap(px, px);
    }
    image.Rescale(px, px, wxIMAGE_QUALITY_BILINEAR);
    return wxBitmap(std::move(image));
}

TextureSkinPickerDialog::TextureSkinPickerDialog(wxWindow* parent, Pattern initial, const std::string& initial_custom_path)
    : DPIDialog(parent, wxID_ANY, _L("Pick texture"), wxDefaultPosition, wxDefaultSize,
                wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    , m_pattern(initial)
    , m_custom_path(initial_custom_path)
{
#ifndef _WIN32
    SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
#endif
    SetFont(wxGetApp().normal_font());

    auto* label_top = new wxStaticText(this, wxID_ANY, _L("Select a built-in pattern or pick a custom image") + ":");

    m_list_ctrl = new wxListCtrl(this, wxID_ANY, wxDefaultPosition,
                                 wxSize(60 * wxGetApp().em_unit(), 44 * wxGetApp().em_unit()),
                                 wxLC_ICON | wxSIMPLE_BORDER);
    m_list_ctrl->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent& event) {
        accept_selection(event.GetIndex());
    });
#ifdef _WIN32
    this->Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
        event.Skip();
        if (m_list_ctrl) m_list_ctrl->Arrange();
    });
#endif

    populate_list();

    auto* buttons = CreateStdDialogButtonSizer(wxOK | wxCANCEL);
    wxGetApp().SetWindowVariantForButton(buttons->GetAffirmativeButton());
    wxGetApp().SetWindowVariantForButton(buttons->GetCancelButton());
    buttons->GetAffirmativeButton()->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        long sel = m_list_ctrl->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
        if (sel >= 0) accept_selection(sel);
        else EndModal(wxID_CANCEL);
    });

    auto* top_sizer = new wxBoxSizer(wxVERTICAL);
    const int border = wxGetApp().em_unit();
    top_sizer->Add(label_top, 0, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, border);
    top_sizer->Add(m_list_ctrl, 1, wxEXPAND | wxALL, border);
    top_sizer->Add(buttons, 0, wxEXPAND | wxALL, border);
    SetSizer(top_sizer);
    top_sizer->SetSizeHints(this);

    wxGetApp().UpdateDlgDarkUI(this);
    CenterOnScreen();
}

TextureSkinPickerDialog::~TextureSkinPickerDialog()
{
    // wxListCtrl::SetImageList does not take ownership.
    if (m_image_list) delete m_image_list;
}

void TextureSkinPickerDialog::populate_list()
{
    m_image_list = new wxImageList(THUMB_PX, THUMB_PX);

    long initial_index = 0;
    for (size_t i = 0; i < TextureFeature::PatternCount; ++i) {
        const auto p = static_cast<Pattern>(i);
        wxBitmap bmp = (p == Pattern::Custom) ? make_custom_tile_bitmap(THUMB_PX)
                                              : load_pattern_bitmap(p, THUMB_PX);
        m_image_list->Add(bmp);

        wxString label = (p == Pattern::Custom) ? _L("Custom…")
                                                : wxString::FromUTF8(TextureFeature::pattern_display_name(p));
        long idx = m_list_ctrl->InsertItem(long(i), label, long(i));
        if (p == m_pattern) initial_index = idx;
    }

    m_list_ctrl->SetImageList(m_image_list, wxIMAGE_LIST_NORMAL);
    if (initial_index >= 0) {
        m_list_ctrl->SetItemState(initial_index, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
                                                   wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
        m_list_ctrl->EnsureVisible(initial_index);
    }
}

void TextureSkinPickerDialog::accept_selection(long list_index)
{
    if (list_index < 0 || list_index >= long(TextureFeature::PatternCount)) {
        EndModal(wxID_CANCEL);
        return;
    }
    const auto picked = static_cast<Pattern>(list_index);
    if (picked == Pattern::Custom) {
        const std::string path = prompt_custom_file();
        if (path.empty()) return; // leave dialog open
        m_pattern = Pattern::Custom;
        m_custom_path = path;
    } else {
        m_pattern = picked;
    }
    EndModal(wxID_OK);
}

std::string TextureSkinPickerDialog::prompt_custom_file()
{
    wxFileDialog dlg(this, _L("Choose a texture image"),
                     m_custom_path.empty() ? wxString() : wxString::FromUTF8(m_custom_path),
                     wxString(), "Image files (*.png;*.jpg;*.jpeg)|*.png;*.jpg;*.jpeg|PNG files (*.png)|*.png|JPEG files (*.jpg;*.jpeg)|*.jpg;*.jpeg",
                     wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dlg.ShowModal() != wxID_OK) return {};
    return into_u8(dlg.GetPath());
}

} // namespace Slic3r::GUI
