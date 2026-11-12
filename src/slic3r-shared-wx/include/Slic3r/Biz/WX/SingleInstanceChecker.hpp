#pragma once

#include "Slic3r/Biz/Platform/ISingleInstanceChecker.hpp"

#include <wx/wx.h>
#include <wx/snglinst.h>
#include <string>
#include <memory>

namespace Slic3r::Biz::WX {
class SingleInstanceChecker : public Platform::ISingleInstanceChecker 
{
public:
	SingleInstanceChecker(const std::string& name, const std::string& path);

   bool is_another_running() override;
   /**
    * This function is important for lockfile solution. Solution based on WX doesnt need it. (So it needs to return true)
    */
   bool is_primary_instance() override { return true; }

private:
    wxSingleInstanceChecker m_wxchecker;
};
}