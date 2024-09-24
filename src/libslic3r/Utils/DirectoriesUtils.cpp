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

static std::string GetDataDir()
{
    std::string dir;

    char* ptr;
    if ((ptr = getenv("XDG_CONFIG_HOME")))
        dir = std::string(ptr);
    else {
        if ((ptr = getenv("HOME")))
            dir = std::string(ptr);
        else {
            struct passwd* who = (struct passwd*)NULL;
            if ((ptr = getenv("USER")) || (ptr = getenv("LOGNAME")))
                who = getpwnam(ptr);
            // make sure the user exists!
            if (!who)
                who = getpwuid(getuid());

            dir = std::string(who ? who->pw_dir : 0);
        }
        if (! dir.empty())
            dir += "/.config";
    }

    if (dir.empty())
        BOOST_LOG_TRIVIAL(error) << "GetDataDir() > unsupported file layout";

    return dir;
}

#endif

namespace Slic3r {

std::string get_default_datadir()
{
    const std::string config_dir = GetDataDir();
    const std::string data_dir = (boost::filesystem::path(config_dir) / SLIC3R_APP_FULL_NAME).make_preferred().string();
    return data_dir;
}

} // namespace Slic3r
