#ifndef slic3r_GUI_ExtraRenderers_hpp_
#define slic3r_GUI_ExtraRenderers_hpp_

#include <functional>

#include <wx/dataview.h>

namespace Slic3r::App::WX {

// ----------------------------------------------------------------------------
// DataViewBitmapText: helper class used by BitmapTextRenderer
// ----------------------------------------------------------------------------

class DataViewBitmapText : public wxObject
{
public:
    DataViewBitmapText( const wxString &text = wxEmptyString,
                        const wxBitmap& bmp = wxNullBitmap) :
        m_text(text),
        m_bmp(bmp)
    { }

    DataViewBitmapText(const DataViewBitmapText &other)
        : wxObject(),
        m_text(other.m_text),
        m_bmp(other.m_bmp)
    { }

    void            SetText(const wxString &text)   { m_text = text; }
    wxString        GetText() const                 { return m_text; }
    void            SetBitmap(const wxBitmap &bmp)  { m_bmp = bmp; }
    const wxBitmap& GetBitmap() const               { return m_bmp; }

    bool IsSameAs(const DataViewBitmapText& other) const {
        return m_text == other.m_text && m_bmp.IsSameAs(other.m_bmp);
    }

    bool operator==(const DataViewBitmapText& other) const { return IsSameAs(other); }

    bool operator!=(const DataViewBitmapText& other) const { return !IsSameAs(other); }

private:
    wxString    m_text;
    wxBitmap    m_bmp;

    wxDECLARE_DYNAMIC_CLASS(DataViewBitmapText);
};
DECLARE_VARIANT_OBJECT(DataViewBitmapText)

// ----------------------------------------------------------------------------
// BitmapTextRenderer
// ----------------------------------------------------------------------------

// Lightweight markup parser.
// Supported tags: <b>, <i>, <span color="...">
// Nesting is supported but malformed markup is ignored.
class MarkupText
{
public:
    ~MarkupText() = default;

    void SetMarkup(const wxString& text);
    wxSize Measure(wxDC& dc) const;
    void Render(wxDC& dc, const wxRect& rect, wxEllipsizeMode ellipsize) const;
    wxString GetPlainText() const;

    struct Token
    {
        explicit Token(const wxString& text, bool bold, bool italic, const wxColour& color);

        wxString text;
        wxColour color;
        wxFont font;
    };

private:
    std::vector<Token> m_tokens;
};

#if ENABLE_NONCUSTOM_DATA_VIEW_RENDERING
class BitmapTextRenderer : public wxDataViewRenderer
#else
class BitmapTextRenderer : public wxDataViewCustomRenderer
#endif //ENABLE_NONCUSTOM_DATA_VIEW_RENDERING
{
public:
    BitmapTextRenderer(bool use_markup = false,
        wxDataViewCellMode mode =
#ifdef __WXOSX__
        wxDATAVIEW_CELL_INERT
#else
        wxDATAVIEW_CELL_EDITABLE
#endif

        , int align = wxDVR_DEFAULT_ALIGNMENT
#if ENABLE_NONCUSTOM_DATA_VIEW_RENDERING
    );
#else
    ) :
    wxDataViewCustomRenderer(wxT("DataViewBitmapText"), mode, align)
    {
        EnableMarkup(use_markup);
    }
#endif //ENABLE_NONCUSTOM_DATA_VIEW_RENDERING

    ~BitmapTextRenderer();

    void    EnableMarkup(bool enable = true);

    bool    SetValue(const wxVariant& value) override;
    bool    GetValue(wxVariant& value) const override;
#if ENABLE_NONCUSTOM_DATA_VIEW_RENDERING && wxUSE_ACCESSIBILITY
    wxString    GetAccessibleDescription() const override;
#endif // wxUSE_ACCESSIBILITY && ENABLE_NONCUSTOM_DATA_VIEW_RENDERING

    bool    Render(wxRect cell, wxDC* dc, int state) override;
    wxSize  GetSize() const override;
    bool    HasEditorCtrl() const override
    {
#ifdef __WXOSX__
        return false;
#else
        return true;
#endif
    }
    wxWindow*   CreateEditorCtrl(wxWindow* parent, wxRect labelRect, const wxVariant& value) override;
    bool        GetValueFromEditorCtrl(wxWindow* ctrl, wxVariant& value) override;
    bool        WasCanceled() const { return m_was_unusable_symbol; }

    void        set_can_create_editor_ctrl_function(std::function<bool()> can_create_fn) { m_can_create_editor_ctrl = can_create_fn; }

private:
    DataViewBitmapText      m_value;
    bool                    m_was_unusable_symbol       { false };
    std::function<bool()>   m_can_create_editor_ctrl    { nullptr };

    std::unique_ptr<MarkupText> m_markupText;
};


// ----------------------------------------------------------------------------
// BitmapChoiceRenderer
// ----------------------------------------------------------------------------

class BitmapChoiceRenderer : public wxDataViewCustomRenderer
{
public:
    BitmapChoiceRenderer(wxDataViewCellMode mode =
#ifdef __WXOSX__
        wxDATAVIEW_CELL_INERT
#else
        wxDATAVIEW_CELL_EDITABLE
#endif
        , int align = wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL
    ) : wxDataViewCustomRenderer(wxT("DataViewBitmapText"), mode, align) {}

    bool    SetValue(const wxVariant& value) override;
    bool    GetValue(wxVariant& value) const override;

    bool    Render(wxRect cell, wxDC* dc, int state) override;
    wxSize  GetSize() const override;

    bool        HasEditorCtrl() const override { return true; }
    wxWindow*   CreateEditorCtrl(wxWindow* parent, wxRect labelRect, const wxVariant& value) override;
    bool        GetValueFromEditorCtrl(wxWindow* ctrl, wxVariant& value) override;

    void        set_can_create_editor_ctrl_function(std::function<bool()> can_create_fn) { m_can_create_editor_ctrl = can_create_fn; }
    void        set_default_extruder_idx(std::function<int()> default_extruder_idx_fn)   { m_get_default_extruder_idx = default_extruder_idx_fn; }

private:
    DataViewBitmapText      m_value;
    std::function<bool()>   m_can_create_editor_ctrl  { nullptr };
    std::function<int()>    m_get_default_extruder_idx{ nullptr };
};



// ----------------------------------------------------------------------------
// TextRenderer
// ----------------------------------------------------------------------------

class TextRenderer : public wxDataViewCustomRenderer
{
public:
    TextRenderer(wxDataViewCellMode mode = wxDATAVIEW_CELL_INERT
        , int align = wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL
    ) : wxDataViewCustomRenderer(wxT("string"), mode, align) {}

    bool    SetValue(const wxVariant& value) override;
    bool    GetValue(wxVariant& value) const override;

    bool    Render(wxRect cell, wxDC* dc, int state) override;
    wxSize  GetSize() const override;

    bool    HasEditorCtrl() const override { return false; }

private:
    wxString    m_value;
};

}

#endif // slic3r_GUI_ExtraRenderers_hpp_
