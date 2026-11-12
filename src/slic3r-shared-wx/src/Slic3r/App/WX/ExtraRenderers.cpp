#include "Slic3r/App/WX/ExtraRenderers.hpp"
#include "Slic3r/App/WX/WidgetsConfig.hpp"
#include "Slic3r/App/WX/BitmapGetters.hpp"
#include "Slic3r/App/WX/StringConversions.hpp"
#include "Slic3r/App/WX/Widgets/BitmapComboBox.hpp"
#include "Slic3r/App/WX/I18N.hpp"

#include <wx/dc.h>
#include <wx/dcclient.h>
#include <wx/bmpcbox.h>

namespace Slic3r::App::WX {

//-----------------------------------------------------------------------------
// DataViewBitmapText
//-----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(DataViewBitmapText, wxObject)

IMPLEMENT_VARIANT_OBJECT(DataViewBitmapText)

static wxSize get_size(const wxBitmap& icon)
{
#ifdef __WIN32__
    return icon.GetSize();
#else
    return icon.GetScaledSize();
#endif
}

// ---------------------------------------------------------
// BitmapTextRenderer
// ---------------------------------------------------------

MarkupText::Token::Token(const wxString& text, bool bold, bool italic, const wxColour& color) :
    text(text),
    color(color)
{
    font = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
    if (bold) {
        font.MakeBold();
    }
    if (italic) {
        font.MakeItalic();
    }
}

static wxColour ParseSpanColor(const wxString& tag)
{
    static const wxString marker = from_u8("color=");

    wxString lower = tag.Lower();

    int pos = lower.Find(marker);
    if (pos == wxNOT_FOUND)
        return wxNullColour;

    wxChar quote = lower[pos + 6];
    if (quote != '"' && quote != '\'')
        return wxNullColour;

    int start = pos + 7;
    int end   = lower.find(quote, start);
    if (end == wxNOT_FOUND)
        return wxNullColour;

    wxString value = lower.Mid(start, end - start);
    wxColour col;
    if (col.Set(value))
        return col;

    return wxNullColour;
}

static std::vector<MarkupText::Token> ParseMarkup(const wxString& input)
{
    static const wxString bold_beg   = from_u8("b");
    static const wxString bold_end   = from_u8("/b");
    static const wxString italic_beg = from_u8("i");
    static const wxString italic_end = from_u8("/i");
    static const wxString span_beg   = from_u8("span");
    static const wxString span_end   = from_u8("/span");

    std::vector<MarkupText::Token> tokens;

    bool bold      = false;
    bool italic    = false;
    wxColour color = wxNullColour;

    wxString buffer;
    bool inTag = false;
    wxString tag;

    auto flush = [&]()
    {
        if (!buffer.empty()) {
            tokens.push_back(MarkupText::Token(buffer, bold, italic, color));
            buffer.clear();
        }
    };

    for (wxChar c : input) {
        if (c == '<') {
            flush();
            inTag = true;
            tag.clear();
            continue;
        }

        if (c == '>' && inTag) {
            inTag      = false;
            wxString t = tag.Lower();

            if (t == bold_beg)
                bold = true;
            else if (t == bold_end)
                bold = false;
            else if (t == italic_beg)
                italic = true;
            else if (t == italic_end)
                italic = false;
            else if (t.StartsWith(span_beg))
                color = ParseSpanColor(tag);
            else if (t == span_end)
                color = wxNullColour;

            continue;
        }

        if (inTag) {
            tag += c;
            continue;
        }

        buffer += c;
    }

    flush();
    return tokens;
}

void MarkupText::SetMarkup(const wxString& text)
{
    m_tokens = ParseMarkup(text);
}

wxSize MarkupText::Measure(wxDC& dc) const
{
    wxSize size{0, 0};

    for (const auto& token : m_tokens) {
        dc.SetFont(token.font);
        wxSize ext = dc.GetTextExtent(token.text);
        size.x += ext.x;
        size.y = std::max(size.y, ext.y);
    }
    return size;
}

void MarkupText::Render(wxDC& dc, const wxRect& rect, wxEllipsizeMode ellipsize) const
{
    // NOTE: Ellipsizing is intentionally not supported for markup text

    int x = rect.x;
    int y = rect.y;

    for (const auto& token : m_tokens) {
        dc.SetFont(token.font);

        wxColour old = dc.GetTextForeground();
        if (token.color.IsOk())
            dc.SetTextForeground(token.color);

        wxString text = token.text;
        if (ellipsize != wxELLIPSIZE_NONE)
            text = wxControl::Ellipsize(
                text, dc, ellipsize, rect.width - (x - rect.x)
            );

        dc.DrawText(text, x, y);
        dc.SetTextForeground(old);

        x += dc.GetTextExtent(text).x;
    }
}

wxString MarkupText::GetPlainText() const
{
    wxString out;
    for (const auto& token : m_tokens)
        out += token.text;
    return out;
}

#if ENABLE_NONCUSTOM_DATA_VIEW_RENDERING
BitmapTextRenderer::BitmapTextRenderer(wxDataViewCellMode mode /*= wxDATAVIEW_CELL_EDITABLE*/, 
                                                 int align /*= wxDVR_DEFAULT_ALIGNMENT*/): 
wxDataViewRenderer(wxT("PrusaDataViewBitmapText"), mode, align)
{
    SetMode(mode);
    SetAlignment(align);
}
#endif // ENABLE_NONCUSTOM_DATA_VIEW_RENDERING

BitmapTextRenderer::~BitmapTextRenderer() {}

void BitmapTextRenderer::EnableMarkup(bool enable)
{
    if (enable && !m_markupText) {
        m_markupText = std::make_unique<MarkupText>();
    }
}

bool BitmapTextRenderer::SetValue(const wxVariant &value)
{
    m_value << value;

    if (m_markupText)
        m_markupText->SetMarkup(m_value.GetText());

    return true;
}

bool BitmapTextRenderer::GetValue(wxVariant& WXUNUSED(value)) const
{
    return false;
}

#if ENABLE_NONCUSTOM_DATA_VIEW_RENDERING && wxUSE_ACCESSIBILITY
wxString BitmapTextRenderer::GetAccessibleDescription() const
{
    return m_markupText ? m_markupText->GetPlainText() : m_value.GetText();
}
#endif // wxUSE_ACCESSIBILITY && ENABLE_NONCUSTOM_DATA_VIEW_RENDERING

bool BitmapTextRenderer::Render(wxRect rect, wxDC *dc, int state)
{
    int xoffset = 0;

    const wxBitmap& icon = m_value.GetBitmap();
    if (icon.IsOk())
    {
        wxSize icon_sz = get_size(icon);
        dc->DrawBitmap(icon, rect.x, rect.y + (rect.height - icon_sz.y) / 2);
        xoffset = icon_sz.x + 4;
    }

    if (m_markupText) {
        rect.x += xoffset;
        rect.width -= xoffset;
        m_markupText->Render(*dc, rect, wxELLIPSIZE_MIDDLE);
    }

    return true;
}

wxSize BitmapTextRenderer::GetSize() const
{
    if (!m_value.GetText().empty())
    {
        wxSize size;
        wxDataViewCtrl* const view = GetView();
        wxClientDC dc(view);
        if (GetAttr().HasFont())
            dc.SetFont(GetAttr().GetEffectiveFont(view->GetFont()));
        else
            dc.SetFont(view->GetFont());

        if (m_markupText)
            size = m_markupText->Measure(dc);
        else
            size = dc.GetTextExtent(m_value.GetText());

        int lines = m_value.GetText().Freq('\n') + 1;
        size.SetHeight(size.GetHeight() * lines);

        if (m_value.GetBitmap().IsOk())
            size.x += m_value.GetBitmap().GetWidth() + 4;
        return size;
    }
    return wxSize(80, 20);
}


wxWindow* BitmapTextRenderer::CreateEditorCtrl(wxWindow* parent, wxRect labelRect, const wxVariant& value)
{
    if (m_can_create_editor_ctrl && !m_can_create_editor_ctrl())
        return nullptr;

    DataViewBitmapText data;
    data << value;

    m_was_unusable_symbol = false;

    wxPoint position = labelRect.GetPosition();
    if (data.GetBitmap().IsOk()) {
        const int bmp_width = data.GetBitmap().GetWidth();
        position.x += bmp_width;
        labelRect.SetWidth(labelRect.GetWidth() - bmp_width);
    }

#ifdef __WXMSW__
    // Case when from some reason we try to create next EditorCtrl till old one was not deleted
    if (auto children = parent->GetChildren(); children.GetCount() > 0)
        for (auto child : children)
            if (dynamic_cast<wxTextCtrl*>(child)) {
                parent->RemoveChild(child);
                child->Destroy();
                break;
            }
#endif // __WXMSW__

    wxTextCtrl* text_editor = new wxTextCtrl(parent, wxID_ANY, data.GetText(),
                                             position, labelRect.GetSize(), wxTE_PROCESS_ENTER);
    text_editor->SetInsertionPointEnd();
    text_editor->SelectAll();

    return text_editor;
}

bool BitmapTextRenderer::GetValueFromEditorCtrl(wxWindow* ctrl, wxVariant& value)
{
    wxTextCtrl* text_editor = wxDynamicCast(ctrl, wxTextCtrl);
    if (!text_editor || text_editor->GetValue().IsEmpty())
        return false;

    m_was_unusable_symbol = has_illegal_characters(text_editor->GetValue());
    if (m_was_unusable_symbol)
        return false;

    // The icon can't be edited so get its old value and reuse it.
    wxVariant valueOld;
    GetView()->GetModel()->GetValue(valueOld, m_item, /*colName*/0); 
    
    DataViewBitmapText bmpText;
    bmpText << valueOld;

    // But replace the text with the value entered by user.
    bmpText.SetText(text_editor->GetValue());

    value << bmpText;
    return true;
}

// ----------------------------------------------------------------------------
// BitmapChoiceRenderer
// ----------------------------------------------------------------------------

bool BitmapChoiceRenderer::SetValue(const wxVariant& value)
{
    m_value << value;
    return true;
}

bool BitmapChoiceRenderer::GetValue(wxVariant& value) const 
{
    value << m_value;
    return true;
}

bool BitmapChoiceRenderer::Render(wxRect rect, wxDC* dc, int state)
{
    int xoffset = 0;

    const wxBitmap& icon = m_value.GetBitmap();
    if (icon.IsOk())
    {
        wxSize icon_sz = get_size(icon);

        dc->DrawBitmap(icon, rect.x, rect.y + (rect.height - icon_sz.GetHeight()) / 2);
        xoffset = icon_sz.GetWidth() + 4;

        if (rect.height==0)
          rect.height= icon_sz.GetHeight();
    }

#ifdef _WIN32
    // workaround for Windows DarkMode : Don't respect to the state & wxDATAVIEW_CELL_SELECTED to avoid update of the text color
    RenderText(m_value.GetText(), xoffset, rect, dc, state & wxDATAVIEW_CELL_SELECTED ? 0 : state);
#else
    RenderText(m_value.GetText(), xoffset, rect, dc, state);
#endif

    return true;
}

wxSize BitmapChoiceRenderer::GetSize() const
{
    wxSize sz = GetTextExtent(m_value.GetText());

    if (m_value.GetBitmap().IsOk())
        sz.x += m_value.GetBitmap().GetWidth() + 4;

    return sz;
}


wxWindow* BitmapChoiceRenderer::CreateEditorCtrl(wxWindow* parent, wxRect labelRect, const wxVariant& value)
{
    if (m_can_create_editor_ctrl && !m_can_create_editor_ctrl())
        return nullptr;

    std::vector<wxBitmapBundle*> icons = get_extruder_color_icons();
    if (icons.empty())
        return nullptr;

    DataViewBitmapText data;
    data << value;

#ifdef _WIN32
    Widgets::BitmapComboBox* c_editor = new Widgets::BitmapComboBox(parent, wxID_ANY, wxEmptyString,
#else
    auto c_editor = new wxBitmapComboBox(parent, wxID_ANY, wxEmptyString,
#endif
        labelRect.GetTopLeft(), wxSize(labelRect.GetWidth(), -1), 
        0, nullptr , wxCB_READONLY);

    int def_id = m_get_default_extruder_idx ? m_get_default_extruder_idx() : 0;
    c_editor->Append(_L("default"), def_id < 0 ? wxNullBitmap : *icons[def_id]);
    for (size_t i = 0; i < icons.size(); i++)
        c_editor->Append(wxString::Format(from_u8("%d"), i+1), *icons[i]);

    c_editor->SetSelection(atoi(data.GetText().ToUTF8()));

    
#ifdef __linux__
    c_editor->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent& evt) {
        // to avoid event propagation to other sidebar items
        evt.StopPropagation();
        // FinishEditing grabs new selection and triggers config update. We better call
        // it explicitly, automatic update on KILL_FOCUS didn't work on Linux.
        this->FinishEditing();
    });
#else
    // to avoid event propagation to other sidebar items
    c_editor->Bind(wxEVT_COMBOBOX, [](wxCommandEvent& evt) { evt.StopPropagation(); });
#endif

    return c_editor;
}

bool BitmapChoiceRenderer::GetValueFromEditorCtrl(wxWindow* ctrl, wxVariant& value)
{
#ifdef _WIN32
    Widgets::BitmapComboBox* c = static_cast<Widgets::BitmapComboBox*>(ctrl);
#else
    wxBitmapComboBox* c = static_cast<wxBitmapComboBox*>(ctrl);
#endif
    int selection = c->GetSelection();
    if (selection < 0)
        return false;
   
    DataViewBitmapText bmpText;

    bmpText.SetText(c->GetString(selection));
    bmpText.SetBitmap(c->GetItemBitmap(selection));

    value << bmpText;
    return true;
}


// ----------------------------------------------------------------------------
// TextRenderer
// ----------------------------------------------------------------------------

bool TextRenderer::SetValue(const wxVariant& value)
{
    m_value = value.GetString();
    return true;
}

bool TextRenderer::GetValue(wxVariant& value) const
{
    return false;
}

bool TextRenderer::Render(wxRect rect, wxDC* dc, int state)
{
#ifdef _WIN32
    // workaround for Windows DarkMode : Don't respect to the state & wxDATAVIEW_CELL_SELECTED to avoid update of the text color
    RenderText(m_value, 0, rect, dc, state & wxDATAVIEW_CELL_SELECTED ? 0 : state);
#else
    RenderText(m_value, 0, rect, dc, state);
#endif

    return true;
}

wxSize TextRenderer::GetSize() const
{
    return GetTextExtent(m_value);
}

}
