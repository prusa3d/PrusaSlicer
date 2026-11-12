#include "Slic3r/Biz/Platform/SecretStoreLinux.hpp"

#include "Slic3r/Log.hpp"
#include "Slic3r/Assert.hpp"
#include "Slic3r/Directories.hpp"

#include "Slic3r/Utils.hpp" // ScopeGuard

#include <boost/filesystem.hpp> 
#include <boost/nowide/fstream.hpp>
#include <boost/algorithm/string.hpp>
#include <fmt/format.h>
#include <fcntl.h>

namespace Slic3r::Biz::Platform {

namespace {
bool concurrent_write_file(const std::string& secret_to_store, const boost::filesystem::path& filename)
{
    // Open the file
    int fd = open(filename.string().c_str(), O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
    if (fd == -1) {
        SPDLOG_ERROR("Unable to open store file {}: {}", filename.string() , strerror(errno));
        return false;
    }
    // Close the file when the guard dies
    Slic3r::ScopeGuard sg_fd([fd]() { 
        close(fd); 
    });

    // Configure the lock
    struct flock lock;
    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_WRLCK;  // Write lock
    lock.l_whence = SEEK_SET;  // Lock from the start of the file
    lock.l_start = 0;
    lock.l_len = 0;  // 0 means lock the entire file

    // Try to acquire the lock
    SPDLOG_INFO("Waiting to acquire lock on file: {}", filename.string());
    if (fcntl(fd, F_SETLKW, &lock) == -1) {
        SPDLOG_ERROR("Unable to acquire lock: {}", strerror(errno));
        return false;
    }
    SPDLOG_INFO("Lock acquired on file: {}", filename.string());
    
    Slic3r::ScopeGuard sg_lock([&lock, fd, filename]() {
        // Release the lock when guard dies.
        lock.l_type = F_UNLCK;  // Unlock the file
       if (fcntl(fd, F_SETLK, &lock) == -1) {
            SPDLOG_ERROR("Unable to release lock ({}): ", filename.string(), strerror(errno));
        } else {
            SPDLOG_INFO("Lock released on file: {}", filename.string());
        }
    });

    // Write content to the file
    if (write(fd, secret_to_store.c_str(), strlen(secret_to_store.c_str())) == -1) {
        SPDLOG_ERROR("Unable to write to file: {}", strerror(errno));
        return false;
    }
    return true;
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
    
    if (!concurrent_write_file(data, target)) {
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