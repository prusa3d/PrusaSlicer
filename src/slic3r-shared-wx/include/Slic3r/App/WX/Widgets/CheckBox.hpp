#pragma once

#include "BitmapToggleButton.hpp"
#include "../Scalable.hpp"

namespace Slic3r::App::WX::Widgets {

class CheckBox : public BitmapToggleButton
{
public:
	CheckBox(wxWindow* parent = NULL, const wxString& name = wxEmptyString);

public:
	void SetValue(bool value) override;
    void Update() override;
    bool Enable(bool enable = true) override;

	void Rescale();

protected:
#ifdef __WXMSW__
    virtual State GetNormalState() const wxOVERRIDE;
#endif
    
#ifdef __WXOSX__
    virtual wxBitmap DoGetBitmap(State which) const wxOVERRIDE;
    
    void updateBitmap(wxEvent & evt);
    
    bool m_disable = false;
    bool m_hover = false;
    bool m_focus = false;
#endif
    
private:
	void update() override;

    ScalableBitmap  m_on;
    ScalableBitmap  m_off;
    ScalableBitmap  m_on_disabled;
    ScalableBitmap  m_off_disabled;
    ScalableBitmap  m_on_focused;
    ScalableBitmap  m_off_focused;
};

} //namespace Slic3r::App::WX
