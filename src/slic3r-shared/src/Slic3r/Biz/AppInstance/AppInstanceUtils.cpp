#include "Slic3r/Biz/AppInstance/AppInstanceUtils.hpp"

#include <string_view>

#ifdef WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <atomic>
#include <random>
#endif

namespace Slic3r::Biz::AppInstance {
unsigned get_current_pid()
{
#ifdef WIN32
    return GetCurrentProcessId();
#elif __APPLE__
    return ::getpid();
#else
    // On flatpak getpid() might return same number for each concurent instances.
    static std::atomic<unsigned> instance_uuid{0};
    if (instance_uuid == 0) {
        unsigned generated_value;
        {
            // Use a thread-local random engine
            thread_local std::random_device rd;
            thread_local std::mt19937 generator(rd());
            std::uniform_int_distribution<unsigned> distribution;
            generated_value = distribution(generator);
        }
        unsigned expected = 0;
        // Atomically initialize the instance_uuid if it has not been set
        instance_uuid.compare_exchange_strong(expected, generated_value);
    }
    return instance_uuid.load();
#endif
}

std::string redact_app_urls(const std::string& text)
{
    static constexpr std::string_view scheme     = "prusaslicer://";
    static constexpr std::string_view terminator = "\"\\ \t\r\n";

    std::string result;
    size_t      pos = 0;
    while (true) {
        const size_t url_start = text.find(scheme, pos);
        if (url_start == std::string::npos) {
            result.append(text, pos);
            return result;
        }
        size_t url_end = text.find_first_of(terminator, url_start);
        if (url_end == std::string::npos) {
            url_end = text.size();
        }
        const size_t query = text.find('?', url_start);
        if (query == std::string::npos || query >= url_end) {
            result.append(text, pos, url_end - pos);
        } else {
            result.append(text, pos, query + 1 - pos);
            result.append("<redacted>");
        }
        pos = url_end;
    }
}
} // namespace Slic3r::Biz::AppInstance
