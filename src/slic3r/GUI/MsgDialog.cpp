///|/ Copyright (c) Prusa Research 2018 - 2023 Oleksandra Iushchenko @YuSanka, Lukáš Matěna @lukasmatena, Vojtěch Bubník @bubnikv, Enrico Turri @enricoturri1966, David Kocík @kocikdav, Lukáš Hejl @hejllukas, Vojtěch Král @vojtechkral
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "MsgDialog.hpp"

#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <wx/statbmp.h>
#include <wx/scrolwin.h>
#include <wx/clipbrd.h>
#include <wx/checkbox.h>
#include <wx/html/htmlwin.h>

#include <boost/algorithm/string/replace.hpp>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Utils.hpp"
#include "libslic3r/Color.hpp"
#include "GUI.hpp"
#include "format.hpp"
#include "I18N.hpp"
#include "ConfigWizard.hpp"
#include "wxExtensions.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "GUI_App.hpp"

#include "Widgets/CheckBox.hpp"

namespace Slic3r {
namespace GUI {

MsgDialog::MsgDialog(wxWindow *parent, const wxString &title, const wxString &headline, long style, wxBitmap bitmap)
	: wxDialog(parent ? parent : dynamic_cast<wxWindow*>(wxGetApp().mainframe), wxID_ANY, title, wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
	, boldfont(wxGetApp().normal_font())
	, content_sizer(new wxBoxSizer(wxVERTICAL))
	, btn_sizer(new wxBoxSizer(wxHORIZONTAL))
{
#ifdef __APPLE__
    this->SetBackgroundColour(wxGetApp().get_window_default_clr());
#endif
	boldfont.SetWeight(wxFONTWEIGHT_BOLD);

    this->SetFont(wxGetApp().normal_font());
    this->CenterOnParent();

    auto *main_sizer = new wxBoxSizer(wxVERTICAL);
	auto *topsizer = new wxBoxSizer(wxHORIZONTAL);
	auto *rightsizer = new wxBoxSizer(wxVERTICAL);

	auto *headtext = new wxStaticText(this, wxID_ANY, headline);
	headtext->SetFont(boldfont);
    headtext->Wrap(CONTENT_WIDTH*wxGetApp().em_unit());
	rightsizer->Add(headtext);
	rightsizer->AddSpacer(VERT_SPACING);

	rightsizer->Add(content_sizer, 1, wxEXPAND);
    btn_sizer->AddStretchSpacer();

	logo = new wxStaticBitmap(this, wxID_ANY, bitmap.IsOk() ? bitmap : wxNullBitmap);

	topsizer->Add(logo, 0, wxALL, BORDER);
	topsizer->Add(rightsizer, 1, wxTOP | wxBOTTOM | wxRIGHT | wxEXPAND, BORDER);

    main_sizer->Add(topsizer, 1, wxEXPAND);
    main_sizer->Add(new StaticLine(this), 0, wxEXPAND | wxLEFT | wxRIGHT, HORIZ_SPACING);
    main_sizer->Add(btn_sizer, 0, wxALL | wxEXPAND, VERT_SPACING);

    apply_style(style);

	SetSizerAndFit(main_sizer);
}

void MsgDialog::SetButtonLabel(wxWindowID btn_id, const wxString& label, bool set_focus/* = false*/) 
{
    if (wxButton* btn = get_button(btn_id)) {
        btn->SetLabel(label);
        if (set_focus)
            btn->SetFocus();
    }
}

wxButton* MsgDialog::add_button(wxWindowID btn_id, bool set_focus /*= false*/, const wxString& label/* = wxString()*/)
{
    wxButton* btn = new wxButton(this, btn_id, label);
    wxGetApp().SetWindowVariantForButton(btn);
    if (set_focus) {
        btn->SetFocus();
        // For non-MSW platforms SetFocus is not enought to use it as default, when the dialog is closed by ENTER
        // We have to set this button as the (permanently) default one in its dialog
        // See https://twitter.com/ZMelmed/status/1472678454168539146
        btn->SetDefault();
    }
    btn_sizer->Add(btn, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, HORIZ_SPACING);
    btn->Bind(wxEVT_BUTTON, [this, btn_id](wxCommandEvent&) { this->EndModal(btn_id); });
    return btn;
};

wxButton* MsgDialog::get_button(wxWindowID btn_id){
    return static_cast<wxButton*>(FindWindowById(btn_id, this));
}

void MsgDialog::apply_style(long style)
{
    if (style & wxOK)       add_button(wxID_OK, true);
    if (style & wxYES)      add_button(wxID_YES,   !(style & wxNO_DEFAULT));
    if (style & wxNO)       add_button(wxID_NO,     (style & wxNO_DEFAULT));
    if (style & wxCANCEL)   add_button(wxID_CANCEL, (style & wxCANCEL_DEFAULT));

    std::string icon_name = style & wxICON_WARNING        ? "exclamation" :
                            style & wxICON_INFORMATION    ? "info"        :
                            style & wxICON_QUESTION       ? "question"    : "PrusaSlicer";
    logo->SetBitmap(*get_bmp_bundle(icon_name, 64));
}

void MsgDialog::finalize()
{
    wxGetApp().UpdateDlgDarkUI(this);
    Fit();
    this->CenterOnParent();
}

// Text shown as HTML, so that mouse selection and Ctrl-V to copy will work.
static void add_msg_content(MsgDialog* parent, wxBoxSizer* content_sizer, const HtmlContent& content)
{
    wxHtmlWindow* html = new wxHtmlWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxHW_SCROLLBAR_AUTO);

    // count lines in the message
    int msg_lines = 0;
    if (!content.is_monospaced_font) {
        int line_len = 55;// count of symbols in one line
        int start_line = 0;
        for (auto i = content.msg.begin(); i != content.msg.end(); ++i) {
            if (*i == '\n') {
                int cur_line_len = i - content.msg.begin() - start_line;
                start_line = i - content.msg.begin();
                if (cur_line_len == 0 || line_len > cur_line_len)
                    msg_lines++;
                else
                    msg_lines += std::lround((double)(cur_line_len) / line_len);
            }
        }
        msg_lines++;
    }

    wxFont      font = wxGetApp().normal_font();//wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
    wxFont      monospace = wxGetApp().code_font();
    wxColour    text_clr = wxGetApp().get_label_clr_default();
    auto        text_clr_str = encode_color(ColorRGB(text_clr.Red(), text_clr.Green(), text_clr.Blue()));
    auto        bgr_clr_str = wxGetApp().get_html_bg_color(parent);
    const int   font_size = font.GetPointSize();
    int         size[] = { font_size, font_size, font_size, font_size, font_size, font_size, font_size };
    html->SetFonts(font.GetFaceName(), monospace.GetFaceName(), size);
    html->SetBorders(2);

    // calculate html page size from text
    wxSize page_size;
    int em = wxGetApp().em_unit();
    if (!wxGetApp().mainframe) {
        // If mainframe is nullptr, it means that GUI_App::on_init_inner() isn't completed 
        // (We just show information dialog about configuration version now)
        // And as a result the em_unit value wasn't created yet
        // So, calculate it from the scale factor of Dialog
#if defined(__WXGTK__)
        // Linux specific issue : get_dpi_for_window(this) still doesn't responce to the Display's scale in new wxWidgets(3.1.3).
        // So, initialize default width_unit according to the width of the one symbol ("m") of the currently active font of this window.
        em = std::max<size_t>(10, parent->GetTextExtent("m").x - 1);
#else
        double scale_factor = (double)get_dpi_for_window(parent) / (double)DPI_DEFAULT;
        em = std::max<size_t>(10, 10.0f * scale_factor);
#endif // __WXGTK__
    }

    // if message containes the table
    if (content.msg.Contains("<tr>")) {
        int lines = content.msg.Freq('\n') + 1;
        int pos = 0;
        while (pos < (int)content.msg.Len() && pos != wxNOT_FOUND) {
            pos = content.msg.find("<tr>", pos + 1);
            lines += 2;
        }
        int page_height = std::min(int(font.GetPixelSize().y+2) * lines, 68 * em);
        page_size = wxSize(68 * em, page_height);
    }
    else {
        wxClientDC dc(parent);
        wxSize msg_sz = dc.GetMultiLineTextExtent(content.msg);
        page_size = wxSize(std::min(msg_sz.GetX() + 2 * em, 68 * em),
                           std::min(msg_sz.GetY() + 2 * em, 68 * em));
    }
    html->SetMinSize(page_size);

    std::string msg_escaped = xml_escape(into_u8(content.msg), content.is_marked_msg || content.on_link_clicked);
    boost::replace_all(msg_escaped, "\r\n", "<br>");
    boost::replace_all(msg_escaped, "\n", "<br>");
    if (content.is_monospaced_font)
        // Code formatting will be preserved. This is useful for reporting errors from the placeholder parser.
        msg_escaped = std::string("<pre><code>") + msg_escaped + "</code></pre>";
    html->SetPage(format_wxstr("<html>"
                                    "<body bgcolor=%1% link=%2%>"
                                        "<font color=%2%>"
                                            "%3%"
                                        "</font>"
                                    "</body>"
                               "</html>", 
                    bgr_clr_str, text_clr_str, from_u8(msg_escaped)));

    html->Bind(wxEVT_HTML_LINK_CLICKED, [parent, &content](wxHtmlLinkEvent& event) {
        if (content.on_link_clicked) {
            parent->EndModal(wxID_CLOSE);
            content.on_link_clicked(into_u8(event.GetLinkInfo().GetHref()));
        }
        else
            wxGetApp().open_browser_with_warning_dialog(event.GetLinkInfo().GetHref(), parent, false);
        event.Skip(false);
    });

    content_sizer->Add(html, 1, wxEXPAND);
    wxGetApp().UpdateDarkUI(html);
}

// ErrorDialog

void ErrorDialog::create(const HtmlContent& content, int icon_width)
{
    add_msg_content(this, content_sizer, content);

    // Use a small bitmap with monospaced font, as the error text will not be wrapped.
    logo->SetBitmap(*get_bmp_bundle("PrusaSlicer_192px_grayscale.png", icon_width));

    SetMaxSize(wxSize(-1, CONTENT_MAX_HEIGHT*wxGetApp().em_unit()));

    finalize();
}

ErrorDialog::ErrorDialog(wxWindow *parent, const wxString &msg, bool monospaced_font)
    : MsgDialog(parent, wxString::Format(_L("%s error"), SLIC3R_APP_NAME), 
                        wxString::Format(_L("%s has encountered an error"), SLIC3R_APP_NAME), wxOK)
    , m_content(HtmlContent{ msg, monospaced_font, true })
{
    create(m_content, monospaced_font ? 48 : 84);
}

ErrorDialog::ErrorDialog(wxWindow *parent, const wxString &msg, const t_link_clicked& on_link_clicked)
    : MsgDialog(parent, wxString::Format(_L("%s error"), SLIC3R_APP_NAME), 
                        wxString::Format(_L("%s has encountered an error"), SLIC3R_APP_NAME), wxOK)
    , m_content(HtmlContent{ msg, false, true, on_link_clicked })
{
    create(m_content, 84);
}


HtmlCapableRichMessageDialog::HtmlCapableRichMessageDialog(wxWindow                                       *parent,
                                                           const wxString                                 &msg,
                                                           const wxString                                 &caption,
                                                           long                                           style,
                                                           const std::function<void(const std::string &)> &on_link_clicked)
    : RichMessageDialogBase(parent, HtmlContent{msg, false, true, on_link_clicked}, caption, style)
{}


// WarningDialog

WarningDialog::WarningDialog(wxWindow *parent,
                             const wxString& message,
                             const wxString& caption/* = wxEmptyString*/,
                             long style/* = wxOK*/)
    : MsgDialog(parent, caption.IsEmpty() ? wxString::Format(_L("%s warning"), SLIC3R_APP_NAME) : caption, 
                        wxString::Format(_L("%s has a warning")+":", SLIC3R_APP_NAME), style)
{
    add_msg_content(this, content_sizer, HtmlContent{ message });
    finalize();
}

#ifdef _WIN32
// MessageDialog

MessageDialog::MessageDialog(wxWindow* parent,
    const wxString& message,
    const wxString& caption/* = wxEmptyString*/,
    long style/* = wxOK*/)
    : MsgDialog(parent, caption.IsEmpty() ? wxString::Format(_L("%s info"), SLIC3R_APP_NAME) : caption, wxEmptyString, style)
{
    add_msg_content(this, content_sizer, HtmlContent{ get_wraped_wxString(message) });
    finalize();
}
#endif


// RichMessageDialogBase

RichMessageDialogBase::RichMessageDialogBase(wxWindow* parent,
    const wxString& message,
    const wxString& caption/* = wxEmptyString*/,
    long style/* = wxOK*/)
    : RichMessageDialogBase(parent, HtmlContent{get_wraped_wxString(message)}, caption, style)
{}

RichMessageDialogBase::RichMessageDialogBase(wxWindow* parent, const HtmlContent& content, const wxString& caption, long style)
    : MsgDialog(parent, caption.IsEmpty() ? wxString::Format(_L("%s info"), SLIC3R_APP_NAME) : caption, wxEmptyString, style)
{
    m_content = content; // We need a copy for the on_link_clicked lambda.
    add_msg_content(this, content_sizer, m_content);

#ifdef _WIN32 // See comment in the header where m_checkBox is defined.
    m_checkBox = new ::CheckBox(this, m_checkBoxText);
#else
    m_checkBox = new wxCheckBox(this, wxID_ANY, m_checkBoxText);
#endif

    wxGetApp().UpdateDarkUI(m_checkBox);
    m_checkBox->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { m_checkBoxValue = m_checkBox->GetValue(); });

    btn_sizer->Insert(0, m_checkBox, wxALIGN_CENTER_VERTICAL);

    finalize();    
}


int RichMessageDialogBase::ShowModal()
{
    if (m_checkBoxText.IsEmpty())
        m_checkBox->Hide();
    else {
        m_checkBox->SetLabelText(m_checkBoxText);
        m_checkBox->Update();
    }
    Layout();

    return wxDialog::ShowModal();
}


// InfoDialog

InfoDialog::InfoDialog(wxWindow* parent, const wxString &title, const wxString& msg, bool is_marked_msg/* = false*/, long style/* = wxOK | wxICON_INFORMATION*/)
	: MsgDialog(parent, wxString::Format(_L("%s information"), SLIC3R_APP_NAME), title, style)
	, msg(msg)
{
    add_msg_content(this, content_sizer, HtmlContent{ msg, false, is_marked_msg });
    finalize();
}

wxString get_wraped_wxString(const wxString& in, size_t line_len /*=80*/)
{
    wxString out;

    for (size_t i = 0; i < in.size();) {
        // Overwrite the character (space or newline) starting at ibreak?
        bool   overwrite = false;
        // UTF8 representation of wxString.
        // Where to break the line, index of character at the start of a UTF-8 sequence.
        size_t ibreak    = size_t(-1);
        // Overwrite the character at ibreak (it is a whitespace) or not?
        size_t j = i;
        for (size_t cnt = 0; j < in.size();) {
            if (bool newline = in[j] == '\n'; in[j] == ' ' || in[j] == '\t' || newline) {
                // Overwrite the whitespace.
                ibreak    = j ++;
                overwrite = true;
                if (newline)
                    break;
            } else if (in[j] == '/'
#ifdef _WIN32
                 || in[j] == '\\'
#endif // _WIN32
                 ) {
                // Insert after the slash.
                ibreak    = ++ j;
                overwrite = false;
            } else
                j += get_utf8_sequence_length(in.c_str() + j, in.size() - j);
            if (++ cnt == line_len) {
                if (ibreak == size_t(-1)) {
                    ibreak    = j;
                    overwrite = false;
                }
                break;
            }
        }
        if (j == in.size()) {
            out.append(in.begin() + i, in.end());
            break;
        }
        assert(ibreak != size_t(-1));
        out.append(in.begin() + i, in.begin() + ibreak);
        out.append('\n');
        i = ibreak;
        if (overwrite)
            ++ i;
    }

    return out;
}

}
}
