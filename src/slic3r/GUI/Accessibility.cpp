///|/ Copyright (c) Prusa Research 2023 Dawid Pieper @dawidpieper
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/

#include <wx/string.h>

#include "Accessibility.hpp"

namespace Slic3r { namespace GUI {

		wxString Accessibility::GetLastLabelString() {return Accessibility::sLastLabel;}

		void Accessibility::SetNextLabelString(wxString labelString) {
			Accessibility::sLastLabel = labelString.Clone();
			Accessibility::bLabelAvailable = true;
		}

		void Accessibility::ClearLabelString() {
			Accessibility::sLastLabel = "";
			Accessibility::bLabelAvailable = false;
		}

		bool Accessibility::IsLabelAvailable() {return Accessibility::bLabelAvailable;}

	} // GUI
} // Slic3r
