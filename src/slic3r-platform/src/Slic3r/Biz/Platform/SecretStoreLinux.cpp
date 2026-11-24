#include "Slic3r/Biz/Platform/SecretStoreLinux.hpp"

#include "Slic3r/Log.hpp"
#include "Slic3r/Assert.hpp"
#include "Slic3r/Directories.hpp"

#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/algorithm/string.hpp>
#include <fmt/format.h>
#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>

namespace Slic3r::Biz::Platform {

namespace {
bool write_all(int fd, const std::string& data)
{
    size_t written = 0;
    while (written < data.size()) {
        const ssize_t result = write(fd, data.data() + written, data.size() - written);
        if (result == -1) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        written += static_cast<size_t>(result);
    }
    return true;
}

bool replace_file(const std::string& content, const boost::filesystem::path& filename)
{
    const std::string temporary = fmt::format("{}.{}.tmp", filename.string(), getpid());
    const int fd = open(
        temporary.c_str(),
        O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC,
        S_IRUSR | S_IWUSR
    );
    if (fd == -1) {
        SPDLOG_ERROR("Unable to open store file {}: {}", temporary, strerror(errno));
        return false;
    }

    bool replaced = write_all(fd, content);
    if (!replaced) {
        SPDLOG_ERROR("Unable to write to file {}: {}", temporary, strerror(errno));
    } else if (fsync(fd) == -1) {
        SPDLOG_WARN("Unable to flush {}: {}", temporary, strerror(errno));
    }
    if (close(fd) == -1 && replaced) {
        SPDLOG_ERROR("Unable to close {}: {}", temporary, strerror(errno));
        replaced = false;
    }
    if (replaced && rename(temporary.c_str(), filename.string().c_str()) == -1) {
        SPDLOG_ERROR("Unable to replace {}: {}", filename.string(), strerror(errno));
        replaced = false;
    }
    if (!replaced) {
        unlink(temporary.c_str());
    }
    return replaced;
}
} // namespace

bool SecretStoreLinux::save_secret(const std::string& opt, const std::string& usr, const std::string& psswd)
{
    boost::filesystem::path target(boost::filesystem::path(Slic3r::data_dir()) / "shared_runtime" / (opt + ".dat"));
	ASSERT(usr.find('\n') == std::string::npos);
    std::string data = fmt::format("usr: {}\npsswd: {}", usr, psswd);

    static const auto perms = boost::filesystem::owner_read | boost::filesystem::owner_write;   // aka 600

    boost::system::error_code ec;
    boost::filesystem::permissions(target, perms, ec);
    if (ec)
        SPDLOG_INFO("UserAccount: boost::filesystem::permissions before write error message (this could be irrelevant message based on file system): {}", ec.message());
    ec.clear();

    if (!replace_file(data, target)) {
        SPDLOG_ERROR("Failed to store secret.");
        return false;
    }

    boost::filesystem::permissions(target, perms, ec);
    if (ec)
        SPDLOG_INFO("UserAccount: boost::filesystem::permissions after write error message (this could be irrelevant message based on file system): {}", ec.message());
    return true;
}


bool SecretStoreLinux::load_secret(const std::string& opt, std::string& usr, std::string& psswd)
{
    boost::filesystem::path source(boost::filesystem::path(Slic3r::data_dir()) / "shared_runtime" / (opt + ".dat"));
    boost::system::error_code ec;
    if (!boost::filesystem::exists(source, ec) || ec) {
        SPDLOG_ERROR("UserAccount: Failed to read token - no datafile found on path {}", source.string());
        return false;
    }

    boost::nowide::ifstream stream(source.generic_string(), std::ios::in | std::ios::binary);
    if (!stream) {
        SPDLOG_ERROR("UserAccount: Failed to read tokens from {}", source.string());
        return false;
    }
    std::string label;
    std::getline(stream, label, ' ');
    std::getline(stream, usr);
    std::getline(stream, label, ' ');
    std::getline(stream, psswd, '\0');
    stream.close();
    return true;
}

}