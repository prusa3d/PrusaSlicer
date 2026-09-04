#include "Slic3r/Assert.hpp"
#include "Slic3r/Log.hpp"

namespace Slic3r {

using namespace libassert;

namespace {


[[noreturn]]
void assert_failure_handler(const assertion_info& info) {

    enable_virtual_terminal_processing_if_needed(); // for terminal colors on windows
    std::string message = info.to_string();
    SPDLOG_ERROR(message);

    switch(info.type) {
    case assert_type::assertion:
    case assert_type::debug_assertion:
    case assert_type::assumption:
    case assert_type::panic:
    case assert_type::unreachable:
        (void)fflush(stderr);
        std::abort();
        // Breaking here as debug CRT allows aborts to be ignored, if someone wants to make a debug build of
        // this library
        break;
    default:
        LIBASSERT_PRIMITIVE_PANIC("Unknown assertion type in assertion failure handler");
    }
}

}


void init_assert()
{
    set_failure_handler(assert_failure_handler);
}

}
