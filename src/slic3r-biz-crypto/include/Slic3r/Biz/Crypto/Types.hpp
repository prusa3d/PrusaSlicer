#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <stdexcept>
#include <vector>

namespace Slic3r::Biz::Crypto {

using Bytes = std::vector<uint8_t>;
using BytesView = std::span<const uint8_t>;

Bytes bytes_from_hex(std::string_view hex_string);

inline BytesView as_bytes_view(std::string_view s)
{
    return std::span{reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

template <typename T>
BytesView as_bytes_view(const std::vector<T>& v)
{
    return std::span{reinterpret_cast<const uint8_t*>(v.data()), v.size() * sizeof(T)};
}

constexpr size_t DEFAULT_BUFFER_SIZE = 256 * 1024;

Bytes file_as_bytes(const std::string& file_path);
std::string file_as_text(const std::string& file_path);

template <typename T>
concept Updatable = requires(T obj, BytesView bytes) {
    { obj.update(bytes) } -> std::same_as<void>;
};

template <typename BufferT = Bytes>
struct Reader
{
    BufferT data;

    void update(BytesView bytes)
    {
        data.insert(data.end(), bytes.begin(), bytes.end());
    }
};

using StringReader = Reader<std::string>;

using StreamKernel = std::function<void(BytesView bytes)>;
bool file_stream(const std::string& file_path, const StreamKernel& kernel, size_t buf_size = DEFAULT_BUFFER_SIZE);

template <Updatable U>
bool file_stream(const std::string& file_path, U& kernel, size_t buf_size = DEFAULT_BUFFER_SIZE)
{
    return file_stream(file_path, [&kernel](BytesView bytes) { kernel.update(bytes); }, buf_size);
}


struct CryptoException : std::runtime_error
{
    explicit CryptoException(const std::string& basic_string) : runtime_error(basic_string) {}
};

} // namespace Slic3r::Biz::Crypto
