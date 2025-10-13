#include "DirectoriesUtils.hpp"
#include "libslic3r/libslic3r.h"

#include <boost/filesystem/path.hpp>
#include <boost/log/trivial.hpp>

#if defined(_WIN32)

#include <shlobj.h>

static std::string GetDataDir()
{
    HRESULT hr = E_FAIL;

    std::wstring buffer;
    buffer.resize(MAX_PATH);

    hr = ::SHGetFolderPathW
    (
        NULL,               // parent window, not used
        CSIDL_APPDATA,
        NULL,               // access token (current user)
        SHGFP_TYPE_CURRENT, // current path, not just default value
        (LPWSTR)buffer.data()
    );

    if (hr == E_FAIL)
    {
        // directory doesn't exist, maybe we can get its default value?
        hr = ::SHGetFolderPathW
        (
            NULL,
            CSIDL_APPDATA,
            NULL,
            SHGFP_TYPE_DEFAULT,
            (LPWSTR)buffer.data()
        );
    }

    for (int i=0; i< MAX_PATH; i++)
        if (buffer.data()[i] == '\0') {
            buffer.resize(i);
            break;
        }

    return  boost::nowide::narrow(buffer);
}

#elif defined(__linux__)

#include <stdlib.h>
#include <pwd.h>

std::optional<std::string> get_env(std::string_view key) {
    const char* result{getenv(key.data())};
    if(result == nullptr) {
        return std::nullopt;
    }
    return std::string{result};
}

namespace {
std::optional<boost::filesystem::path> get_home_dir(const std::string& subfolder) {
    if (auto result{get_env("HOME")}) {
        return *result + subfolder;
    } else {
        std::optional<std::string> user_name{get_env("USER")};
        if (!user_name) {
            user_name = get_env("LOGNAME");
        }
        struct passwd* who{
            user_name ?
            getpwnam(user_name->data()) :
            (struct passwd*)NULL
        };
        // make sure the user exists!
        if (!who) {
            who = getpwuid(getuid());
        }
        if (who) {
            return std::string{who->pw_dir} + subfolder;
        }
    }
    return std::nullopt;
}
}

namespace Slic3r {
std::optional<boost::filesystem::path> get_home_config_dir() {
    return get_home_dir("/.config");
}

std::optional<boost::filesystem::path> get_home_local_dir() {
    return get_home_dir("/.local");
}
}

std::string GetDataDir()
{
    if (auto result{get_env("XDG_CONFIG_HOME")}) {
        return *result;
    } else if (auto result{Slic3r::get_home_config_dir()}) {
        return result->string();
    }

    BOOST_LOG_TRIVIAL(error) << "GetDataDir() > unsupported file layout";

    return {};
}

#endif

namespace Slic3r {

std::string get_default_datadir()
{
    const std::string config_dir = GetDataDir();
    std::string data_dir = (boost::filesystem::path(config_dir) / SLIC3R_APP_FULL_NAME).make_preferred().string();
    return data_dir;
}

} // namespace Slic3r
