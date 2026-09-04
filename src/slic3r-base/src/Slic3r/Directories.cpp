#include "Slic3r/Directories.hpp"

#include "Slic3r/Log.hpp"
#include "Slic3r/Version.hpp"
#include "Slic3r/Semver.hpp"

#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/nowide/convert.hpp>
#include "Slic3r/Assert.hpp"

#include <fstream> 
#include <regex>

#if defined(_WIN32)

#include <shlobj.h>
#include <comdef.h>

#elif defined(__linux__)

#include <stdlib.h>
#include <pwd.h>
#include <unistd.h>

#elif defined(__APPLE__)

#include "Slic3r/DirectoriesMacUtils.hpp"
#include <sys/types.h>
#include <unistd.h>
#include <pwd.h>

#endif

namespace fs = boost::filesystem;

namespace Slic3r {

static std::string g_var_dir;

void set_var_dir(const std::string& dir)
{
    g_var_dir = dir;
}

const std::string& var_dir()
{
    return g_var_dir;
}

std::string var(const std::string& file_name)
{
    auto file = (boost::filesystem::path(g_var_dir) / file_name).make_preferred();
    return file.string();
}

static std::string g_resources_dir;

void set_resources_dir(const std::string& dir)
{
    g_resources_dir = dir;
}

const std::string& resources_dir()
{
    return g_resources_dir;
}

static std::string g_local_dir;

void set_local_dir(const std::string& dir)
{
    g_local_dir = dir;
}

const std::string& localization_dir()
{
    return g_local_dir;
}

static std::string g_sys_shapes_dir;

void set_sys_shapes_dir(const std::string& dir)
{
    g_sys_shapes_dir = dir;
}

const std::string& sys_shapes_dir()
{
    return g_sys_shapes_dir;
}

static std::string g_custom_gcodes_dir;

void set_custom_gcodes_dir(const std::string& dir)
{
    g_custom_gcodes_dir = dir;
}

const std::string& custom_gcodes_dir()
{
    return g_custom_gcodes_dir;
}

static std::string g_data_dir;

void set_data_dir(const std::string& dir)
{
    g_data_dir = dir;
}

const std::string& data_dir()
{
    return g_data_dir;
}

std::string custom_shapes_dir()
{
    return (boost::filesystem::path(g_data_dir) / "shapes").string();
}

#if defined(_WIN32)

static std::string get_platform_data_dir()
{
    HRESULT hr = E_FAIL;

    std::wstring buffer;
    buffer.resize(MAX_PATH);

    hr = ::SHGetFolderPathW(
        NULL, // parent window, not used
        CSIDL_APPDATA,
        NULL, // access token (current user)
        SHGFP_TYPE_CURRENT, // current path, not just default value
        (LPWSTR) buffer.data()
    );

    if (hr == E_FAIL) {
        // directory doesn't exist, maybe we can get its default value?
        hr = ::SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_DEFAULT, (LPWSTR) buffer.data());
    }

    for (int i = 0; i < MAX_PATH; i++)
        if (buffer.data()[i] == '\0') {
            buffer.resize(i);
            break;
        }

    return boost::nowide::narrow(buffer);
}

#elif defined(__linux__)

std::optional<std::string> get_env(std::string_view key)
{
    const char* result{getenv(key.data())};
    if (result == nullptr) {
        return std::nullopt;
    }
    return std::string{result};
}

namespace {
std::optional<boost::filesystem::path> get_home_dir(const std::string& subfolder)
{
    if (auto result{get_env("HOME")}) {
        return *result + subfolder;
    } else {
        std::optional<std::string> user_name{get_env("USER")};
        if (!user_name) {
            user_name = get_env("LOGNAME");
        }
        struct passwd* who{user_name ? getpwnam(user_name->data()) : (struct passwd*) NULL};
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
} // namespace

namespace Slic3r {
std::optional<boost::filesystem::path> get_home_config_dir()
{
    return get_home_dir("/.config");
}

std::optional<boost::filesystem::path> get_home_local_dir()
{
    return get_home_dir("/.local");
}
} // namespace Slic3r

std::string get_platform_data_dir()
{
    if (auto result{get_env("XDG_CONFIG_HOME")}) {
        return *result;
    } else if (auto result{Slic3r::get_home_config_dir()}) {
        return result->string();
    }

    SPDLOG_ERROR("get_platform_data_dir() > unsupported file layout");

    return {};
}

#elif defined(__EMSCRIPTEN__)
static std::string get_platform_data_dir()
{
    // Emscripten TODO: provide better data dir path
    std::string dir;
    return dir;
}
#endif

std::string get_default_datadir()
{
    Semver semver(::Slic3r::VERSION);
    bool is_alpha = semver.prerelease() && std::string{semver.prerelease()}.find("alpha") != std::string::npos;
    bool is_beta = semver.prerelease() && std::string{semver.prerelease()}.find("beta") != std::string::npos;

    auto datadir_name = std::string(::Slic3r::APP_NAME) + std::to_string(semver.maj());
    if (is_alpha || is_beta)
        datadir_name += "-dev";

    const std::string config_dir = get_platform_data_dir();
    std::string data_dir = (boost::filesystem::path(config_dir) / datadir_name).make_preferred().string();
    return data_dir;
}

static std::string g_cache_dir;

void set_cache_dir(const std::string& path) {
    g_cache_dir = path;
}

const std::string& cache_dir() {
    ASSERT(!g_cache_dir.empty());
    return g_cache_dir;
}

#if defined(__linux__)
static fs::path get_xdg_cache_home() {
    const char* cache_path_raw{::getenv("XDG_CACHE_HOME")};
    if (cache_path_raw != nullptr) {
        return fs::path{cache_path_raw};
    }
    const char* home_raw{::getenv("HOME")};
    if (home_raw != nullptr) {
        return fs::path{home_raw} / ".cache";
    }
    return fs::path{".cache"};
}

std::string get_default_cachedir() {
    const fs::path cache_path{get_xdg_cache_home()};
    return (cache_path / "prusa-slicer").string();
}
#elif defined(__APPLE__)
std::string get_default_cachedir() {
    return get_platform_cache_dir();
}

#else
std::string get_default_cachedir() {
    return data_dir();
}
#endif

#ifdef _WIN32
boost::filesystem::path system_downloads_dir()
{
    PWSTR path = NULL;
    HRESULT hr = SHGetKnownFolderPath(FOLDERID_Downloads, 0, NULL, &path);
    if (SUCCEEDED(hr)) {
        boost::filesystem::path result(path);
        CoTaskMemFree(path);
        return result;
    }

    boost::system::error_code ec;
    const wchar_t* user_profile = _wgetenv(L"USERPROFILE");
    if (user_profile) {
        boost::filesystem::path fallback = boost::filesystem::path(user_profile) / "Downloads";
        if (fs::exists(fallback, ec) && fs::is_directory(fallback, ec)) {
            return fallback;
        }
    }
    SPDLOG_ERROR("Failed to resolve downloads path.");
    return {};
}

#elif  __APPLE__
boost::filesystem::path system_downloads_dir()
{
    std::string home_path;
    const char* env_home = std::getenv("HOME");
    if (env_home) {
        home_path = env_home;
    } else {
        // MacOS is POSIX compliant; use thread-safe getpwuid_r
        long bufsize = sysconf(_SC_GETPW_R_SIZE_MAX);
        if (bufsize == -1) bufsize = 16384; 

        std::vector<char> buf(bufsize);
        passwd pwd, *result = nullptr;

        if (getpwuid_r(getuid(), &pwd, buf.data(), buf.size(), &result) == 0 && result) {
            home_path = result->pw_dir;
        }
    }
    if (home_path.empty()) {
        SPDLOG_ERROR("Failed to resolve home directory.");
        return {};
    }
    boost::system::error_code ec;
    fs::path root(home_path);
    // MacOS Specific: "Downloads" is standard.
    fs::path downloads = root / "Downloads";
    if (fs::exists(downloads, ec) && fs::is_directory(downloads, ec)) {
        return downloads;
    }
    SPDLOG_ERROR("Failed to resolve downloads path.");
    return {};
}

#else

static fs::path get_home_directory() {
    const char* env_home = std::getenv("HOME");
    if (env_home && *env_home != '\0') {
        return fs::path(env_home);
    }

    long bufsize = sysconf(_SC_GETPW_R_SIZE_MAX);
    if (bufsize == -1) bufsize = 16384;

    std::vector<char> buf(bufsize);
    passwd pwd;
    passwd* result = nullptr;

    if (getpwuid_r(getuid(), &pwd, buf.data(), buf.size(), &result) == 0 && result) {
        return fs::path(result->pw_dir);
    }
    return {};
}

boost::filesystem::path system_downloads_dir() {
    if (const char* env_dl = std::getenv("XDG_DOWNLOAD_DIR")) {
        fs::path p(env_dl);
        if (p.is_absolute()) return p;
    }

    const fs::path home = get_home_directory();
    if (home.empty()) {
        SPDLOG_ERROR("Failed to resolve home directory.");
        return {};
    }

    fs::path config_file;
    if (const char* env_config = std::getenv("XDG_CONFIG_HOME")) {
        config_file = fs::path(env_config) / "user-dirs.dirs";
    } else {
        config_file = home / ".config" / "user-dirs.dirs";
    }

    boost::system::error_code ec;
    if (fs::exists(config_file, ec) && fs::is_regular_file(config_file, ec)) {
        std::ifstream file(config_file.string());
        std::string line;

        static const std::regex re(R"rx(^\s*XDG_DOWNLOAD_DIR\s*=\s*"([^"]*)")rx");
        std::smatch match;

        while (std::getline(file, line)) {
            // Skip comments
            if (line.empty() || line[0] == '#') continue;

            if (std::regex_search(line, match, re) && match.size() > 1) {
                std::string value = match.str(1);

                static const std::string home_var = "$HOME";                
                if (value == home_var) {
                    return home;
                } else if (value.rfind(home_var + "/", 0) == 0) {
                    return home / value.substr(6); 
                } else if (value.rfind("/", 0) == 0) {
                    return fs::path(value);
                } else {
                    return home / value;
                }
            }
        }
    }

    return home / "Downloads";
}

#endif

static boost::filesystem::path temp_dir_override;

boost::filesystem::path temp_dir()
{
    if (temp_dir_override.empty())
    {
        return boost::filesystem::temp_directory_path();
    }
    return temp_dir_override;
}

void set_temp_dir(const boost::filesystem::path& path)
{
    temp_dir_override = path;
}

} // namespace Slic3r::Biz
