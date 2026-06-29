///|/ meshseal integration into PrusaSlicer.
///|/ Released under AGPLv3+ (PrusaSlicer); meshseal itself is MIT.
///|/
#ifndef slic3r_GUI_Utils_FixModelByMeshseal_hpp_
#define slic3r_GUI_Utils_FixModelByMeshseal_hpp_

#include <string>

class wxProgressDialog;
class wxString;

namespace Slic3r {

class ModelObject;

#ifdef HAS_MESHSEAL

// Cross-platform mesh repair backend backed by libmeshseal
// (https://github.com/jkavalik/meshseal).
//
// Mirrors the public surface of FixModelByWin10.hpp so the GUI can pick
// between backends at the call site. Difference vs the WinSDK backend:
// - Cross-platform (Windows, macOS, Linux).
// - In-memory ITS <-> meshseal::Mesh conversion (no temp 3MF round-trip).
// - Single worker thread; progress + cooperative cancellation via the
//   meshseal RepairOptions::on_progress callback.

extern bool is_meshseal_available();

// Returns FALSE if fixing was canceled by the user.
// `fix_result` is empty on success; on failure contains an error message.
extern bool fix_model_by_meshseal_gui(ModelObject&                model_object,
                                      int                         volume_idx,
                                      wxProgressDialog&           progress_dialog,
                                      const wxString&             msg_header,
                                      std::string&                fix_result);

#else /* HAS_MESHSEAL */

inline bool is_meshseal_available() { return false; }
inline bool fix_model_by_meshseal_gui(ModelObject&,
                                      int,
                                      wxProgressDialog&,
                                      const wxString&,
                                      std::string&) { return false; }

#endif /* HAS_MESHSEAL */

} // namespace Slic3r

#endif /* slic3r_GUI_Utils_FixModelByMeshseal_hpp_ */
