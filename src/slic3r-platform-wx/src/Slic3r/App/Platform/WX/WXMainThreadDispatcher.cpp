#include "Slic3r/App/Platform/WX/WXMainThreadDispatcher.hpp"

#include <wx/wx.h>

namespace Slic3r::App::Platform::WX {

	void WXMainThreadDispatcher::on_queue_insert() { wxWakeUpIdle(); }
}
