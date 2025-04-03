///|/ Copyright (c) Prusa Research 2023 Dawid Pieper @dawidpieper
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef SLIC3R_GUI_ACCESSIBILITY_HPP
#define SLIC3R_GUI_ACCESSIBILITY_HPP

#include <wx/string.h>

namespace Slic3r { namespace GUI {

	class Accessibility {
		
		public:
		static wxString GetLastLabelString();
		static void SetNextLabelString(wxString labelString);
		static void ClearLabelString();
		static bool IsLabelAvailable();
		
		private:
		inline static wxString sLastLabel = "";
		inline static bool bLabelAvailable = false;
		
	};

	} // GUI
} // Slic3r

#endif /* SLIC3R_GUI_ACCESSIBILITY_HPP */
