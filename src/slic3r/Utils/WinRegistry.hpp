///|/ Copyright (c) Prusa Research 2022 Enrico Turri @enricoturri1966
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_Utils_WinRegistry_hpp_
#define slic3r_Utils_WinRegistry_hpp_

#ifdef _WIN32

#include <string>

namespace Slic3r {

// Creates a Windows registry key for the files with the given 'extension' and associates them to the application 'prog_id'.
// If 'set_as_default' is true, the application 'prog_id' is set ad default application for the file type 'extension'.
// The file type registration implementation is based on code taken from:
// https://stackoverflow.com/questions/20245262/c-program-needs-an-file-association
// The set as default implementation is based on code taken from:
// https://hg.mozilla.org/mozilla-central/rev/e928b3e95a6c3b7257d0ba475fc2303bfbad1874
// https://hg.mozilla.org/releases/mozilla-release/diff/7e775ce432b599c6daf7ac379aa42f1e9b3b33ed/browser/components/shell/WindowsUserChoice.cpp
void associate_file_type(const std::wstring& extension, const std::wstring& prog_id, const std::wstring& prog_desc, bool set_as_default);

} // namespace Slic3r

#endif // _WIN32

#endif // slic3r_Utils_WinRegistry_hpp_
