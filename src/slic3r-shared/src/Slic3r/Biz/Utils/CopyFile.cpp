#include "Slic3r/Biz/Utils/CopyFile.hpp"

#include "Slic3r/Platform.hpp"

#include <string>
#include <vector>
#include <algorithm>
#include <fstream>
#include <cstring>
#include <cerrno>

#include <boost/filesystem.hpp>
#include <boost/system/error_code.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/nowide/cstdio.hpp>

#include <Slic3r/Log.hpp>

#ifdef WIN32
#include <windows.h>
#include <psapi.h>
#else
#include <unistd.h>
#include <sys/types.h>
#ifdef __linux__
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/sendfile.h>
#endif
#endif

namespace Slic3r::Biz::Utils {

#ifdef _WIN32
// The following helpers are borrowed from the LLVM project https://github.com/llvm
namespace WindowsSupport {
template <typename HandleTraits>
class ScopedHandle
{
    typedef typename HandleTraits::handle_type handle_type;
    handle_type Handle;
    ScopedHandle(const ScopedHandle& other)   = delete;
    void operator=(const ScopedHandle& other) = delete;

public:
    ScopedHandle() : Handle(HandleTraits::GetInvalid()) {}

    explicit ScopedHandle(handle_type h) : Handle(h) {}

    ~ScopedHandle()
    {
        if (HandleTraits::IsValid(Handle))
            HandleTraits::Close(Handle);
    }

    handle_type take()
    {
        handle_type t = Handle;
        Handle        = HandleTraits::GetInvalid();
        return t;
    }

    ScopedHandle& operator=(handle_type h)
    {
        if (HandleTraits::IsValid(Handle))
            HandleTraits::Close(Handle);
        Handle = h;
        return *this;
    }

    // True if Handle is valid.
    explicit operator bool() const
    {
        return HandleTraits::IsValid(Handle) ? true : false;
    }

    operator handle_type() const
    {
        return Handle;
    }
};

struct CommonHandleTraits
{
    typedef HANDLE handle_type;

    static handle_type GetInvalid()
    {
        return INVALID_HANDLE_VALUE;
    }

    static void Close(handle_type h)
    {
        ::CloseHandle(h);
    }

    static bool IsValid(handle_type h)
    {
        return h != GetInvalid();
    }
};

typedef ScopedHandle<CommonHandleTraits> ScopedFileHandle;

std::error_code map_windows_error(unsigned windows_error_code)
{
#define MAP_ERR_TO_COND(x, y) case x: return std::make_error_code(std::errc::y)
    switch (windows_error_code) {
        MAP_ERR_TO_COND(ERROR_ACCESS_DENIED, permission_denied);
        MAP_ERR_TO_COND(ERROR_ALREADY_EXISTS, file_exists);
        MAP_ERR_TO_COND(ERROR_BAD_UNIT, no_such_device);
        MAP_ERR_TO_COND(ERROR_BUFFER_OVERFLOW, filename_too_long);
        MAP_ERR_TO_COND(ERROR_BUSY, device_or_resource_busy);
        MAP_ERR_TO_COND(ERROR_BUSY_DRIVE, device_or_resource_busy);
        MAP_ERR_TO_COND(ERROR_CANNOT_MAKE, permission_denied);
        MAP_ERR_TO_COND(ERROR_CANTOPEN, io_error);
        MAP_ERR_TO_COND(ERROR_CANTREAD, io_error);
        MAP_ERR_TO_COND(ERROR_CANTWRITE, io_error);
        MAP_ERR_TO_COND(ERROR_CURRENT_DIRECTORY, permission_denied);
        MAP_ERR_TO_COND(ERROR_DEV_NOT_EXIST, no_such_device);
        MAP_ERR_TO_COND(ERROR_DEVICE_IN_USE, device_or_resource_busy);
        MAP_ERR_TO_COND(ERROR_DIR_NOT_EMPTY, directory_not_empty);
        MAP_ERR_TO_COND(ERROR_DIRECTORY, invalid_argument);
        MAP_ERR_TO_COND(ERROR_DISK_FULL, no_space_on_device);
        MAP_ERR_TO_COND(ERROR_FILE_EXISTS, file_exists);
        MAP_ERR_TO_COND(ERROR_FILE_NOT_FOUND, no_such_file_or_directory);
        MAP_ERR_TO_COND(ERROR_HANDLE_DISK_FULL, no_space_on_device);
        MAP_ERR_TO_COND(ERROR_INVALID_ACCESS, permission_denied);
        MAP_ERR_TO_COND(ERROR_INVALID_DRIVE, no_such_device);
        MAP_ERR_TO_COND(ERROR_INVALID_FUNCTION, function_not_supported);
        MAP_ERR_TO_COND(ERROR_INVALID_HANDLE, invalid_argument);
        MAP_ERR_TO_COND(ERROR_INVALID_NAME, invalid_argument);
        MAP_ERR_TO_COND(ERROR_LOCK_VIOLATION, no_lock_available);
        MAP_ERR_TO_COND(ERROR_LOCKED, no_lock_available);
        MAP_ERR_TO_COND(ERROR_NEGATIVE_SEEK, invalid_argument);
        MAP_ERR_TO_COND(ERROR_NOACCESS, permission_denied);
        MAP_ERR_TO_COND(ERROR_NOT_ENOUGH_MEMORY, not_enough_memory);
        MAP_ERR_TO_COND(ERROR_NOT_READY, resource_unavailable_try_again);
        MAP_ERR_TO_COND(ERROR_OPEN_FAILED, io_error);
        MAP_ERR_TO_COND(ERROR_OPEN_FILES, device_or_resource_busy);
        MAP_ERR_TO_COND(ERROR_OUTOFMEMORY, not_enough_memory);
        MAP_ERR_TO_COND(ERROR_PATH_NOT_FOUND, no_such_file_or_directory);
        MAP_ERR_TO_COND(ERROR_BAD_NETPATH, no_such_file_or_directory);
        MAP_ERR_TO_COND(ERROR_READ_FAULT, io_error);
        MAP_ERR_TO_COND(ERROR_RETRY, resource_unavailable_try_again);
        MAP_ERR_TO_COND(ERROR_SEEK, io_error);
        MAP_ERR_TO_COND(ERROR_SHARING_VIOLATION, permission_denied);
        MAP_ERR_TO_COND(ERROR_TOO_MANY_OPEN_FILES, too_many_files_open);
        MAP_ERR_TO_COND(ERROR_WRITE_FAULT, io_error);
        MAP_ERR_TO_COND(ERROR_WRITE_PROTECT, permission_denied);
        MAP_ERR_TO_COND(WSAEACCES, permission_denied);
        MAP_ERR_TO_COND(WSAEBADF, bad_file_descriptor);
        MAP_ERR_TO_COND(WSAEFAULT, bad_address);
        MAP_ERR_TO_COND(WSAEINTR, interrupted);
        MAP_ERR_TO_COND(WSAEINVAL, invalid_argument);
        MAP_ERR_TO_COND(WSAEMFILE, too_many_files_open);
        MAP_ERR_TO_COND(WSAENAMETOOLONG, filename_too_long);
    default:
        return std::error_code(windows_error_code, std::system_category());
    }
#undef MAP_ERR_TO_COND
}

static std::error_code rename_internal(HANDLE from_handle, const std::wstring& wide_to, bool replace_if_exists)
{
    std::vector<char> rename_info_buf(
        sizeof(FILE_RENAME_INFO) - sizeof(wchar_t) + (wide_to.size() * sizeof(wchar_t))
    );
    FILE_RENAME_INFO& rename_info = *reinterpret_cast<FILE_RENAME_INFO*>(rename_info_buf.data());
    rename_info.ReplaceIfExists   = replace_if_exists;
    rename_info.RootDirectory     = 0;
    rename_info.FileNameLength    = DWORD(wide_to.size() * sizeof(wchar_t));
    std::copy(wide_to.begin(), wide_to.end(), &rename_info.FileName[0]);

    ::SetLastError(ERROR_SUCCESS);
    if (!::SetFileInformationByHandle(
            from_handle,
            FileRenameInfo,
            &rename_info,
            (DWORD) rename_info_buf.size()
        ))
    {
        unsigned Error = GetLastError();
        if (Error == ERROR_SUCCESS)
            Error = ERROR_CALL_NOT_IMPLEMENTED; // Wine doesn't always set error code.
        return map_windows_error(Error);
    }

    return std::error_code();
}

static std::error_code real_path_from_handle(HANDLE H, std::wstring& buffer)
{
    buffer.resize(MAX_PATH + 1);
    DWORD CountChars = ::GetFinalPathNameByHandleW(
        H,
        (LPWSTR) buffer.data(),
        (DWORD) buffer.size() - 1,
        FILE_NAME_NORMALIZED
    );
    if (CountChars > buffer.size()) {
        // The buffer wasn't big enough, try again.  In this case the return value *does* indicate the size of the null terminator.
        buffer.resize((size_t) CountChars);
        CountChars = ::GetFinalPathNameByHandleW(
            H,
            (LPWSTR) buffer.data(),
            (DWORD) buffer.size() - 1,
            FILE_NAME_NORMALIZED
        );
    }
    if (CountChars == 0)
        return map_windows_error(GetLastError());
    buffer.resize(CountChars);
    return std::error_code();
}

std::error_code rename(const std::string& from, const std::string& to)
{
    // Convert to utf-16.
    std::wstring wide_from = boost::nowide::widen(from);
    std::wstring wide_to   = boost::nowide::widen(to);

    ScopedFileHandle from_handle;
    // Retry this a few times to defeat badly behaved file system scanners.
    for (unsigned retry = 0; retry != 200; ++retry) {
        if (retry != 0)
            ::Sleep(10);
        from_handle = ::CreateFileW(
            (LPWSTR) wide_from.data(),
            GENERIC_READ | DELETE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            NULL
        );
        if (from_handle)
            break;
    }
    if (!from_handle)
        return map_windows_error(GetLastError());

    // We normally expect this loop to succeed after a few iterations. If it
    // requires more than 200 tries, it's more likely that the failures are due to
    // a true error, so stop trying.
    for (unsigned retry = 0; retry != 200; ++retry) {
        auto errcode = rename_internal(from_handle, wide_to, true);

        if (errcode == std::error_code(ERROR_CALL_NOT_IMPLEMENTED, std::system_category())) {
            // Wine doesn't support SetFileInformationByHandle in rename_internal.
            // Fall back to MoveFileEx.
            if (std::error_code errcode2 = real_path_from_handle(from_handle, wide_from))
                return errcode2;
            if (::MoveFileExW(
                    (LPCWSTR) wide_from.data(),
                    (LPCWSTR) wide_to.data(),
                    MOVEFILE_REPLACE_EXISTING
                ))
                return std::error_code();
            return map_windows_error(GetLastError());
        }

        if (!errcode || errcode != std::errc::permission_denied)
            return errcode;

        // The destination file probably exists and is currently open in another
        // process, either because the file was opened without FILE_SHARE_DELETE or
        // it is mapped into memory (e.g. using MemoryBuffer). Rename it in order to
        // move it out of the way of the source file. Use FILE_FLAG_DELETE_ON_CLOSE
        // to arrange for the destination file to be deleted when the other process
        // closes it.
        ScopedFileHandle to_handle(
            ::CreateFileW(
                (LPCWSTR) wide_to.data(),
                GENERIC_READ | DELETE,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                NULL,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE,
                NULL
            )
        );
        if (!to_handle) {
            auto errcode = map_windows_error(GetLastError());
            // Another process might have raced with us and moved the existing file
            // out of the way before we had a chance to open it. If that happens, try
            // to rename the source file again.
            if (errcode == std::errc::no_such_file_or_directory)
                continue;
            return errcode;
        }

        BY_HANDLE_FILE_INFORMATION FI;
        if (!::GetFileInformationByHandle(to_handle, &FI))
            return map_windows_error(GetLastError());

        // Try to find a unique new name for the destination file.
        for (unsigned unique_id = 0; unique_id != 200; ++unique_id) {
            std::wstring tmp_filename = wide_to + L".tmp" + std::to_wstring(unique_id);
            std::error_code errcode   = rename_internal(to_handle, tmp_filename, false);
            if (errcode) {
                if (errcode == std::make_error_code(std::errc::file_exists)
                    || errcode == std::make_error_code(std::errc::permission_denied))
                {
                    // Again, another process might have raced with us and moved the file
                    // before we could move it. Check whether this is the case, as it
                    // might have caused the permission denied error. If that was the
                    // case, we don't need to move it ourselves.
                    ScopedFileHandle to_handle2(
                        ::CreateFileW(
                            (LPCWSTR) wide_to.data(),
                            0,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            NULL,
                            OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL,
                            NULL
                        )
                    );
                    if (!to_handle2) {
                        auto errcode = map_windows_error(GetLastError());
                        if (errcode == std::errc::no_such_file_or_directory)
                            break;
                        return errcode;
                    }
                    BY_HANDLE_FILE_INFORMATION FI2;
                    if (!::GetFileInformationByHandle(to_handle2, &FI2))
                        return map_windows_error(GetLastError());
                    if (FI.nFileIndexHigh != FI2.nFileIndexHigh
                        || FI.nFileIndexLow != FI2.nFileIndexLow
                        || FI.dwVolumeSerialNumber != FI2.dwVolumeSerialNumber)
                        break;
                    continue;
                }
                return errcode;
            }
            break;
        }

        // Okay, the old destination file has probably been moved out of the way at
        // this point, so try to rename the source file again. Still, another
        // process might have raced with us to create and open the destination
        // file, so we need to keep doing this until we succeed.
    }

    // The most likely root cause.
    return std::make_error_code(std::errc::permission_denied);
}
} // namespace WindowsSupport
#endif /* _WIN32 */

namespace {
#ifdef __linux__
// Copied from boost::filesystem.
// Called by copy_file_linux() in case linux sendfile() API is not supported.
int copy_file_linux_read_write(int infile, int outfile, uintmax_t file_size)
{
    std::vector<char> buf(
        // Prefer the buffer to be larger than the file size so that we don't have
        // to perform an extra read if the file fits in the buffer exactly.
        std::clamp<size_t>(
            file_size + (file_size < ~static_cast<uintmax_t>(0u)),
            // Min and max buffer sizes are selected to minimize the overhead from system calls.
            // The values are picked based on coreutils cp(1) benchmarking data described here:
            // https://github.com/coreutils/coreutils/blob/d1b0257077c0b0f0ee25087efd46270345d1dd1f/src/ioblksize.h#L23-L72
            8u * 1'024u,
            256u * 1'024u
        ),
        0
    );

#if defined(POSIX_FADV_SEQUENTIAL)
    ::posix_fadvise(infile, 0, 0, POSIX_FADV_SEQUENTIAL);
#endif

    // Don't use file size to limit the amount of data to copy since some filesystems, like procfs or sysfs,
    // provide files with generated content and indicate that their size is zero or 4096. Just copy as much data
    // as we can read from the input file.
    while (true) {
        ssize_t sz_read = ::read(infile, buf.data(), buf.size());
        if (sz_read == 0)
            break;
        if (sz_read < 0) {
            int err = errno;
            if (err == EINTR)
                continue;
            return err;
        }
        // Allow for partial writes - see Advanced Unix Programming (2nd Ed.),
        // Marc Rochkind, Addison-Wesley, 2004, page 94
        for (ssize_t sz_wrote = 0; sz_wrote < sz_read;) {
            ssize_t sz = ::write(
                outfile,
                buf.data() + sz_wrote,
                static_cast<std::size_t>(sz_read - sz_wrote)
            );
            if (sz < 0) {
                int err = errno;
                if (err == EINTR)
                    continue;
                return err;
            }
            sz_wrote += sz;
        }
    }
    return 0;
}

// Copied from boost::filesystem, to support copying a file to a weird filesystem, which does not support changing file attributes,
// for example ChromeOS Linux integration or FlashAIR WebDAV.
// Copied and simplified from boost::filesystem::detail::copy_file() with option = overwrite_if_exists and with just the Linux path kept,
// and only features supported by Linux 3.10 (on our build server with CentOS 7) are kept, namely sendfile with ranges and statx() are not supported.
bool copy_file_linux(
    const boost::filesystem::path& from,
    const boost::filesystem::path& to,
    boost::system::error_code& ec
)
{
    using namespace boost::filesystem;

    struct fd_wrapper
    {
        int fd{-1};
        fd_wrapper() = default;

        explicit fd_wrapper(int fd) throw() : fd(fd) {}

        ~fd_wrapper() throw()
        {
            if (fd >= 0)
                ::close(fd);
        }
    };

    ec.clear();
    int err = 0;

    // Note: Declare fd_wrappers here so that errno is not clobbered by close() that may be called in fd_wrapper destructors
    fd_wrapper infile, outfile;

    while (true) {
        infile.fd = ::open(from.c_str(), O_RDONLY | O_CLOEXEC);
        if (infile.fd < 0) {
            err = errno;
            if (err == EINTR)
                continue;
fail:
            ec.assign(err, boost::system::system_category());
            return false;
        }
        break;
    }

    struct ::stat from_stat;
    if (::fstat(infile.fd, &from_stat) != 0) {
fail_errno:
        err = errno;
        goto fail;
    }

    const mode_t from_mode = from_stat.st_mode;
    if (!S_ISREG(from_mode)) {
        err = ENOSYS;
        goto fail;
    }

    // Enable writing for the newly created files. Having write permission set is important e.g. for NFS,
    // which checks the file permission on the server, even if the client's file descriptor supports writing.
    mode_t to_mode = from_mode | S_IWUSR;
    int oflag      = O_WRONLY | O_CLOEXEC | O_CREAT | O_TRUNC;

    while (true) {
        outfile.fd = ::open(to.c_str(), oflag, to_mode);
        if (outfile.fd < 0) {
            err = errno;
            if (err == EINTR)
                continue;
            goto fail;
        }
        break;
    }

    struct ::stat to_stat;
    if (::fstat(outfile.fd, &to_stat) != 0)
        goto fail_errno;

    to_mode = to_stat.st_mode;
    if (!S_ISREG(to_mode)) {
        err = ENOSYS;
        goto fail;
    }

    if (from_stat.st_dev == to_stat.st_dev && from_stat.st_ino == to_stat.st_ino) {
        err = EEXIST;
        goto fail;
    }

    //! copy_file implementation that uses sendfile loop. Requires sendfile to support file descriptors.
    // FIXME Vojtech: This is a copy loop valid for Linux 2.6.33 and newer.
    // copy_file_data_copy_file_range() supports cross-filesystem copying since 5.3, but Vojtech did not want to polute this
    // function with that, we don't think the performance gain is worth it for the types of files we are copying,
    // and our build server based on CentOS 7 with Linux 3.10 does not support that anyways.
    {
        // sendfile will not send more than this amount of data in one call
        constexpr std::size_t max_send_size = 0x7ffff000u;
        uintmax_t offset                    = 0u;
        while (off_t(offset) < from_stat.st_size) {
            uintmax_t size_left      = from_stat.st_size - offset;
            std::size_t size_to_copy = max_send_size;
            if (size_left < static_cast<uintmax_t>(max_send_size))
                size_to_copy = static_cast<std::size_t>(size_left);
            ssize_t sz = ::sendfile(outfile.fd, infile.fd, nullptr, size_to_copy);
            if (sz < 0) {
                err = errno;
                if (offset == 0u) {
                    // sendfile may fail with EINVAL if the underlying filesystem does not support it.
                    // See https://patchwork.kernel.org/project/linux-nfs/patch/20190411183418.4510-1-olga.kornievskaia@gmail.com/
                    // https://bugzilla.redhat.com/show_bug.cgi?id=1783554.
                    // https://github.com/boostorg/filesystem/commit/4b9052f1e0b2acf625e8247582f44acdcc78a4ce
                    if (err == EINVAL || err == EOPNOTSUPP) {
                        err = copy_file_linux_read_write(infile.fd, outfile.fd, from_stat.st_size);
                        if (err < 0)
                            goto fail;
                        // Succeeded.
                        break;
                    }
                }
                if (err == EINTR)
                    continue;
                if (err == 0)
                    break;
                goto fail; // err already contains the error code
            }
            offset += sz;
        }
    }

    // If we created a new file with an explicitly added S_IWUSR permission,
    // we may need to update its mode bits to match the source file.
    if (to_mode != from_mode && ::fchmod(outfile.fd, from_mode) != 0) {
        if (platform_flavor() == PlatformFlavor::LinuxOnChromium) {
            // Ignore that. 9p filesystem does not allow fmod().
            SPDLOG_ERROR("copy_file_linux() failed to fchmod() the output file \"{}\" to {}: {} "
                "This may be expected when writing to a 9p filesystem.", to.string(), from_mode, ec.message());
        } else {
            // Generic linux. Write out an error to console. At least we may get some feedback.
            SPDLOG_ERROR("copy_file_linux() failed to fchmod() the output file \"{}\" to {}: {}",
                to.string(), from_mode, ec.message());


        }
    }

    // Note: Use fsync/fdatasync followed by close to avoid dealing with the possibility of close failing with EINTR.
    // Even if close fails, including with EINTR, most operating systems (presumably, except HP-UX) will close the
    // file descriptor upon its return. This means that if an error happens later, when the OS flushes data to the
    // underlying media, this error will go unnoticed and we have no way to receive it from close. Calling fsync/fdatasync
    // ensures that all data have been written, and even if close fails for some unfathomable reason, we don't really
    // care at that point.
    err = ::fdatasync(outfile.fd);
    if (err != 0)
        goto fail_errno;

    return true;
}
#endif // __linux__

// Copy a file, adjust the access attributes, so that the target is writable.
CopyFileResult copy_file_inner(const std::string& from, const std::string& to, std::string& error_message)
{
    const boost::filesystem::path source(from);
    const boost::filesystem::path target(to);
    static const auto perms = boost::filesystem::owner_read
        | boost::filesystem::owner_write
        | boost::filesystem::group_read
        | boost::filesystem::others_read; // aka 644

    // Make sure the file has correct permission both before and after we copy over it.
    // NOTE: error_code variants are used here to supress expception throwing.
    // Error code of permission() calls is ignored on purpose - if they fail,
    // the copy_file() function will fail appropriately and we don't want the permission()
    // calls to cause needless failures on permissionless filesystems (ie. FATs on SD cards etc.)
    // or when the target file doesn't exist.
    boost::system::error_code ec;
    boost::filesystem::permissions(target, perms, ec);
    if (ec && boost::filesystem::exists(target))
        SPDLOG_DEBUG(
            "boost::filesystem::permisions before copy error message (this could be irrelevant message based on file system): {}",
            ec.message()
        );
    // BOOST_LOG_TRIVIAL(debug) << "boost::filesystem::permisions before copy error message (this could be irrelevant message based on file system): " << ec.message();
    ec.clear();
#ifdef __linux__
    // We want to allow copying files on Linux to succeed even if changing the file attributes fails.
    // That may happen when copying on some exotic file system, for example Linux on Chrome.
    copy_file_linux(source, target, ec);
#else // __linux__
    boost::filesystem::copy_file(source, target, boost::filesystem::copy_options::overwrite_existing, ec);
#endif // __linux__
    if (ec) {
        error_message = ec.message();
        return FailCopyFile;
    }
    ec.clear();
    boost::filesystem::permissions(target, perms, ec);
    if (ec)
        SPDLOG_DEBUG(
            "boost::filesystem::permisions after copy error message (this could be irrelevant message based on file system): {}",
            ec.message()
        );
    // BOOST_LOG_TRIVIAL(debug) << "boost::filesystem::permisions after copy error message (this could be irrelevant message based on file system): " << ec.message();
    return Success;
}
} // namespace

CopyFileResult copy_file(
    const std::string& from,
    const std::string& to,
    std::string& error_message,
    const bool with_check
)
{
    std::string to_temp    = to + ".tmp";

    // make sure the temporary target file doesn't exist yet
    size_t idx = 0;
    while (boost::filesystem::exists(to_temp)) {
        to_temp = to + "." + std::to_string(idx++) + ".tmp";
    }

    CopyFileResult ret_val = copy_file_inner(from, to_temp, error_message);
    if (ret_val == Success) {
        if (with_check)
            ret_val = check_copy(from, to_temp);

        if (ret_val == 0 && Utils::rename_file(to_temp, to))
            ret_val = FailRenaming;
    }
    return ret_val;
}

CopyFileResult check_copy(const std::string& origin, const std::string& copy)
{
    boost::nowide::ifstream f1(origin, std::ifstream::in | std::ifstream::binary | std::ifstream::ate);
    boost::nowide::ifstream f2(copy, std::ifstream::in | std::ifstream::binary | std::ifstream::ate);

    if (f1.fail())
        return FailCheckOriginNotOpened;
    if (f2.fail())
        return FailCheckTargetNotOpened;

    std::streampos fsize = f1.tellg();
    if (fsize != f2.tellg())
        return FailFilesDifferent;

    f1.seekg(0, std::ifstream::beg);
    f2.seekg(0, std::ifstream::beg);

    // Compare by reading 8 MiB buffers one at a time.
    size_t buffer_size = 8 * 1'024 * 1'024;
    std::vector<char> buffer_origin(buffer_size, 0);
    std::vector<char> buffer_copy(buffer_size, 0);
    do {
        f1.read(buffer_origin.data(), buffer_size);
        f2.read(buffer_copy.data(), buffer_size);
        std::streampos origin_cnt = f1.gcount();
        std::streampos copy_cnt   = f2.gcount();
        if (origin_cnt != copy_cnt
            || (origin_cnt > 0
                && std::memcmp(buffer_origin.data(), buffer_copy.data(), origin_cnt) != 0))
            // Files are different.
            return FailFilesDifferent;
        fsize -= origin_cnt;
    } while (f1.good() && f2.good());

    // All data has been read and compared equal.
    return (f1.eof() && f2.eof() && fsize == 0) ? Success : FailFilesDifferent;
}

std::error_code rename_file(const std::string& from, const std::string& to)
{
    // borrowed from LVVM lib/Support/Windows/Path.inc
#ifdef _WIN32
    return WindowsSupport::rename(from, to);
#else
    boost::nowide::remove(to.c_str());
    return std::make_error_code(
        static_cast<std::errc>(boost::nowide::rename(from.c_str(), to.c_str()))
    );
#endif
}
} // namespace Slic3r::Biz::Utils
