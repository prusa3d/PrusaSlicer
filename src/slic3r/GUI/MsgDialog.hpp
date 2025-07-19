///|/ Copyright (c) Prusa Research 2018 - 2022 Oleksandra Iushchenko @YuSanka, Lukáš Matěna @lukasmatena, David Kocík @kocikdav, Lukáš Hejl @hejllukas, Vojtěch Bubník @bubnikv, Vojtěch Král @vojtechkral
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_MsgDialog_hpp_
#define slic3r_MsgDialog_hpp_

#include <string>
#include <unordered_map>

#include <wx/dialog.h>
#include <wx/font.h>
#include <wx/bitmap.h>
#include <wx/msgdlg.h>
#include <wx/richmsgdlg.h>
#include <wx/textctrl.h>
#include <wx/statline.h>

class wxBoxSizer;
class CheckBox;
class wxStaticBitmap;

namespace Slic3r {

namespace GUI {

using t_link_clicked = std::function<void(const std::string&)>;

struct HtmlContent
{
	wxString        msg{ wxEmptyString };
	bool            is_monospaced_font{ false };
	bool            is_marked_msg{ false };
	t_link_clicked	on_link_clicked{ nullptr };
};

// A message / query dialog with a bitmap on the left and any content on the right
// with buttons underneath.
struct MsgDialog : wxDialog
{
	MsgDialog(MsgDialog &&) = delete;
	MsgDialog(const MsgDialog &) = delete;
	MsgDialog &operator=(MsgDialog &&) = delete;
	MsgDialog &operator=(const MsgDialog &) = delete;
	virtual ~MsgDialog() = default;

	void SetButtonLabel(wxWindowID btn_id, const wxString& label, bool set_focus = false);

protected:
	enum {
		CONTENT_WIDTH = 70,//50,
		CONTENT_MAX_HEIGHT = 60,
		BORDER = 30,
		VERT_SPACING = 15,
		HORIZ_SPACING = 5,
	};

	MsgDialog(wxWindow *parent, const wxString &title, const wxString &headline, long style = wxOK, wxBitmap bitmap = wxNullBitmap);
	// returns pointer to created button
	wxButton* add_button(wxWindowID btn_id, bool set_focus = false, const wxString& label = wxString());
	// returns pointer to found button or NULL
	wxButton* get_button(wxWindowID btn_id);
	void apply_style(long style);
	void finalize();

	wxFont boldfont;
	wxBoxSizer *content_sizer;
	wxBoxSizer *btn_sizer;
	wxStaticBitmap *logo;
};


// Generic error dialog, used for displaying exceptions
class ErrorDialog : public MsgDialog
{
public:
	// If monospaced_font is true, the error message is displayed using html <code><pre></pre></code> tags,
	// so that the code formatting will be preserved. This is useful for reporting errors from the placeholder parser.
	ErrorDialog(wxWindow *parent, const wxString &msg, bool courier_font);
	ErrorDialog(wxWindow *parent, const wxString &msg, const t_link_clicked& on_link_clicked);
	ErrorDialog(ErrorDialog &&) = delete;
	ErrorDialog(const ErrorDialog &) = delete;
	ErrorDialog &operator=(ErrorDialog &&) = delete;
	ErrorDialog &operator=(const ErrorDialog &) = delete;
	virtual ~ErrorDialog() = default;

private:
	void create(const HtmlContent& content, int icon_width);

	HtmlContent m_content;
};


// Generic warning dialog, used for displaying exceptions
class WarningDialog : public MsgDialog
{
public:
	WarningDialog(	wxWindow *parent,
		            const wxString& message,
		            const wxString& caption = wxEmptyString,
		            long style = wxOK);
	WarningDialog(WarningDialog&&) = delete;
	WarningDialog(const WarningDialog&) = delete;
	WarningDialog &operator=(WarningDialog&&) = delete;
	WarningDialog &operator=(const WarningDialog&) = delete;
	virtual ~WarningDialog() = default;
};

wxString get_wraped_wxString(const wxString& text_in, size_t line_len = 80);


// Generic rich message dialog, used intead of wxRichMessageDialog
class RichMessageDialogBase : public MsgDialog
{

// Using CheckBox causes some weird sizer-related issues on Linux and macOS. To get around the problem before
// we find a better fix, we will fallback to wxCheckBox in this dialog. This makes little difference for most dialogs,
// We currently only use this class as a base for HtmlCapableRichMessageDialog on Linux and macOS. The normal
// RichMessageDialog is just an alias for wxRichMessageDialog on these platforms.
#ifdef _WIN32
	CheckBox*   m_checkBox{ nullptr };
#else
	wxCheckBox* m_checkBox{ nullptr };
#endif

	wxString	m_checkBoxText;
	bool		m_checkBoxValue{ false };

public:
	// NOTE! Don't change a signature of contsrucor. It have to  be tha same as for wxRichMessageDialog
	RichMessageDialogBase(wxWindow* parent, const wxString& message, const wxString& caption = wxEmptyString, long style = wxOK);
	RichMessageDialogBase(wxWindow* parent, const HtmlContent& content, const wxString& caption = wxEmptyString, long style = wxOK);
	RichMessageDialogBase(RichMessageDialogBase&&)                 = delete;
	RichMessageDialogBase(const RichMessageDialogBase&)            = delete;
	RichMessageDialogBase &operator=(RichMessageDialogBase&&)      = delete;
	RichMessageDialogBase &operator=(const RichMessageDialogBase&) = delete;
	virtual ~RichMessageDialogBase()                                = default;

	int  ShowModal() override;

	void ShowCheckBox(const wxString& checkBoxText, bool checked = false)
	{
		m_checkBoxText = checkBoxText;
		m_checkBoxValue = checked;
	}

	wxString	GetCheckBoxText()	const { return m_checkBoxText; }
	bool		IsCheckBoxChecked() const { return m_checkBoxValue; }

// This part o fcode isported from the "wx\msgdlg.h"
	using wxMD = wxMessageDialogBase;
	// customization of the message box buttons
	virtual bool SetYesNoLabels(const wxMD::ButtonLabel& yes, const wxMD::ButtonLabel& no)
	{
		DoSetCustomLabel(m_yes, yes, wxID_YES);
		DoSetCustomLabel(m_no, no, wxID_NO);
		return true;
	}

	virtual bool SetYesNoCancelLabels(const wxMD::ButtonLabel& yes,
		const wxMD::ButtonLabel& no,
		const wxMD::ButtonLabel& cancel)
	{
		DoSetCustomLabel(m_yes, yes, wxID_YES);
		DoSetCustomLabel(m_no, no, wxID_NO);
		DoSetCustomLabel(m_cancel, cancel, wxID_CANCEL);
		return true;
	}

	virtual bool SetOKLabel(const wxMD::ButtonLabel& ok)
	{
		DoSetCustomLabel(m_ok, ok, wxID_OK);
		return true;
}

	virtual bool SetOKCancelLabels(const wxMD::ButtonLabel& ok,
		const wxMD::ButtonLabel& cancel)
	{
		DoSetCustomLabel(m_ok, ok, wxID_OK);
		DoSetCustomLabel(m_cancel, cancel, wxID_CANCEL);
		return true;
	}

	virtual bool SetHelpLabel(const wxMD::ButtonLabel& help)
	{
		DoSetCustomLabel(m_help, help, wxID_HELP);
		return true;
	}
	// test if any custom labels were set
	bool HasCustomLabels() const
	{
		return !(m_ok.empty() && m_cancel.empty() && m_help.empty() &&
			m_yes.empty() && m_no.empty());
	}

	// these functions return the label to be used for the button which is
	// either a custom label explicitly set by the user or the default label,
	// i.e. they always return a valid string
	wxString GetYesLabel() const
	{
		return m_yes.empty() ? GetDefaultYesLabel() : m_yes;
	}
	wxString GetNoLabel() const
	{
		return m_no.empty() ? GetDefaultNoLabel() : m_no;
	}
	wxString GetOKLabel() const
	{
		return m_ok.empty() ? GetDefaultOKLabel() : m_ok;
	}
	wxString GetCancelLabel() const
	{
		return m_cancel.empty() ? GetDefaultCancelLabel() : m_cancel;
	}
	wxString GetHelpLabel() const
	{
		return m_help.empty() ? GetDefaultHelpLabel() : m_help;
	}

protected:
	// this function is called by our public SetXXXLabels() and should assign
	// the value to var with possibly some transformation (e.g. Cocoa version
	// currently uses this to remove any accelerators from the button strings
	// while GTK+ one handles stock items specifically here)
	void DoSetCustomLabel(wxString& var, const wxMD::ButtonLabel& label, wxWindowID btn_id)
	{
		var = label.GetAsString();
		SetButtonLabel(btn_id, var);
	}

	// these functions return the custom label or empty string and should be
	// used only in specific circumstances such as creating the buttons with
	// these labels (in which case it makes sense to only use a custom label if
	// it was really given and fall back on stock label otherwise), use the
	// Get{Yes,No,OK,Cancel}Label() methods above otherwise
	const wxString& GetCustomYesLabel() const { return m_yes; }
	const wxString& GetCustomNoLabel() const { return m_no; }
	const wxString& GetCustomOKLabel() const { return m_ok; }
	const wxString& GetCustomHelpLabel() const { return m_help; }
	const wxString& GetCustomCancelLabel() const { return m_cancel; }

private:
	// these functions may be overridden to provide different defaults for the
	// default button labels (this is used by wxGTK)
	virtual wxString GetDefaultYesLabel() const { return wxGetTranslation("Yes"); }
	virtual wxString GetDefaultNoLabel() const { return wxGetTranslation("No"); }
	virtual wxString GetDefaultOKLabel() const { return wxGetTranslation("OK"); }
	virtual wxString GetDefaultCancelLabel() const { return wxGetTranslation("Cancel"); }
	virtual wxString GetDefaultHelpLabel() const { return wxGetTranslation("Help"); }

	// labels for the buttons, initially empty meaning that the defaults should
	// be used, use GetYes/No/OK/CancelLabel() to access them
	wxString m_yes,
		m_no,
		m_ok,
		m_cancel,
		m_help;

	HtmlContent m_content;
};


#ifdef _WIN32
// Generic static line, used intead of wxStaticLine
class StaticLine: public wxTextCtrl
{
public:
	StaticLine( wxWindow* parent,
				wxWindowID id = wxID_ANY,
				const wxPoint& pos = wxDefaultPosition,
				const wxSize& size = wxDefaultSize,
				long style = wxLI_HORIZONTAL,
				const wxString& name = wxString::FromAscii(wxTextCtrlNameStr))
	: wxTextCtrl(parent, id, wxEmptyString, pos, size!=wxDefaultSize ? size : (style == wxLI_HORIZONTAL ? wxSize(10, 1) : wxSize(1, 10)), wxSIMPLE_BORDER, wxDefaultValidator, name)
	{
		this->Enable(false);
	}
	~StaticLine() {}
};

// Generic message dialog, used intead of wxMessageDialog
class MessageDialog : public MsgDialog
{
public:
	// NOTE! Don't change a signature of contsrucor. It have to  be tha same as for wxMessageDialog
    MessageDialog(wxWindow *parent,
		            const wxString& message,
		            const wxString& caption = wxEmptyString,
		            long style = wxOK);
    MessageDialog(MessageDialog &&)                 = delete;
    MessageDialog(const MessageDialog &)            = delete;
    MessageDialog &operator=(MessageDialog &&)      = delete;
    MessageDialog &operator=(const MessageDialog &) = delete;
    virtual ~MessageDialog()                            = default;
};

using RichMessageDialog = RichMessageDialogBase; 

#else
// just a wrapper for wxStaticLine to use the same code on all platforms
class StaticLine : public wxStaticLine
{
public:
	StaticLine(wxWindow* parent,
		wxWindowID id = wxID_ANY,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize,
		long style = wxLI_HORIZONTAL,
		const wxString& name = wxString::FromAscii(wxStaticLineNameStr))
		: wxStaticLine(parent, id, pos, size, style, name) {}
	~StaticLine() {}
};
// just a wrapper to wxMessageBox to use the same code on all platforms
class MessageDialog : public wxMessageDialog
{
public:
	MessageDialog(wxWindow* parent,
		const wxString& message,
		const wxString& caption = wxEmptyString,
		long style = wxOK)
    : wxMessageDialog(parent, get_wraped_wxString(message), caption, style) {}
	~MessageDialog() {}
};

// just a wrapper to wxRichMessageBox to use the same code on all platforms
class RichMessageDialog : public wxRichMessageDialog
{
public:
	RichMessageDialog(wxWindow* parent,
		const wxString& message,
		const wxString& caption = wxEmptyString,
		long style = wxOK)
    : wxRichMessageDialog(parent, get_wraped_wxString(message), caption, style) {
		this->SetEscapeId(wxID_CANCEL);
	}
	~RichMessageDialog() {}
};
#endif

class HtmlCapableRichMessageDialog : public RichMessageDialogBase
{
public:
	HtmlCapableRichMessageDialog(wxWindow *parent, const wxString &msg, const wxString& caption, long style, const std::function<void(const std::string &)> &on_link_clicked);
    ~HtmlCapableRichMessageDialog() {}

private:
    HtmlContent m_content;
};

// Generic info dialog, used for displaying exceptions
class InfoDialog : public MsgDialog
{
public:
	InfoDialog(wxWindow *parent, const wxString &title, const wxString &msg, bool is_marked = false, long style = wxOK| wxICON_INFORMATION);
	InfoDialog(InfoDialog&&) = delete;
	InfoDialog(const InfoDialog&) = delete;
	InfoDialog&operator=(InfoDialog&&) = delete;
	InfoDialog&operator=(const InfoDialog&) = delete;
	virtual ~InfoDialog() = default;

	void set_caption(const wxString& caption) { this->SetTitle(caption); }

private:
	wxString msg;
};


}
}

#endif
