#pragma once

#include <Slic3r/App/Platform/StdMainThreadDispatcher.hpp>

namespace Slic3r::App::Platform::WX {

class WXMainThreadDispatcher : public StdMainThreadDispatcher
{
private:
    void on_queue_insert() override;

};

}
