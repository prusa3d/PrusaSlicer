#pragma once

#include <functional>
#include <memory>
#include <optional>

#include "Slic3r/Biz/Crypto/Types.hpp"

namespace Slic3r::Biz::Crypto {
class IContentProvider
{
public:
    constexpr static size_t DEFAULT_BUFFER_SIZE = 256 * 1024;

    using BytesView    = BytesView;
    using FileList     = std::vector<std::string>;
    using StreamKernel = std::function<void(BytesView window)>;

private:
    template <typename T = Bytes>
    std::optional<T> file_as(const std::string& file_path);

public:

    virtual ~IContentProvider() = default;

    virtual FileList list_files() = 0;

    virtual bool
    file_stream(const std::string& file_path, const StreamKernel& kernel, size_t buffer_size) = 0;

    virtual const std::string& path() const = 0;

    bool file_stream(const std::string& file_path, const StreamKernel& kernel)
    {
        return file_stream(file_path, kernel, DEFAULT_BUFFER_SIZE);
    }

    template <Updatable U>
    bool file_stream(const std::string& file_path, U& u, size_t buffer_size)
    {
        return file_stream(file_path, [&u](BytesView bytes) { u.update(bytes); }, buffer_size);
    }

    template <Updatable U>
    bool file_stream(const std::string& file_path, U& u)
    {
        return file_stream(file_path, u, DEFAULT_BUFFER_SIZE);
    }

    auto file_as_text(const std::string& file_path)
    {
        return file_as<std::string>(file_path);
    }

    auto file_as_bytes(const std::string& file_path)
    {
        return file_as<Bytes>(file_path);
    }
};

template <typename T>
std::optional<T> IContentProvider::file_as(const std::string& file_path)
{
    Reader<T> reader;
    if (!file_stream(file_path, reader)) {
        return std::nullopt;
    }
    return reader.data;
}

using IContentProviderPtr = std::unique_ptr<IContentProvider>;

IContentProviderPtr create_directory_source(const std::string& dir_path);
IContentProviderPtr create_zip_source(const std::string& zip_path);

} // namespace Slic3r::Biz::Crypto
