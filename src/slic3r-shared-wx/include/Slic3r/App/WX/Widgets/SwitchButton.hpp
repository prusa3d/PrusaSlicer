#pragma once

#include "BitmapToggleButton.hpp"

#include "../Scalable.hpp"
#include "StateColor.hpp"

namespace Slic3r::App::WX::Widgets {

class SwitchButton : public BitmapToggleButton
{
public:
	SwitchButton(wxWindow * parent = NULL, const wxString& name = wxEmptyString, wxWindowID id = wxID_ANY);

public:
	void SetLabels(wxString const & lbl_on, wxString const & lbl_off);

	void SetTextColor(StateColor const &color);

	void SetTrackColor(StateColor const &color);

	void SetThumbColor(StateColor const &color);

	void SetValue(bool value) override;

	void Rescale();

	void SysColorChange();

private:
	void update() override;

private:
	ScalableBitmap m_on;
	ScalableBitmap m_off;

	wxString labels[2];
	StateColor   text_color;
	StateColor   track_color;
	StateColor   thumb_color;
};

}