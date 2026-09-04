#include "Slic3r/Biz/Crypto/Types.hpp"
#include "Slic3r/Log.hpp"
#include <ranges>

#include <boost/nowide/cstdio.hpp>

namespace Slic3r::Biz::Crypto {


bool file_stream(const std::string& file_path, const StreamKernel& kernel, size_t buffer_size)
{
    Bytes buf;

    auto* fp = boost::nowide::fopen(file_path.c_str(), "rb");
    if (!fp) {
        return false;
    }
    buf.resize(buffer_size);

    bool success = true;

    while (!feof(fp)) {
        if (const auto read = fread(buf.data(), 1, buf.size(), fp); read > 0) {
            kernel(BytesView{buf.data(), read});
        } else {
            success = false;
            break;
        }
    }

    fclose(fp);
    return success;
}

namespace {
std::optional<uint8_t> char_to_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    SPDLOG_ERROR("Invalid hex character '{}'",c);
    return std::nullopt;
}
}

Bytes bytes_from_hex(std::string_view hex_string) {
    if (hex_string.length() % 2 != 0) {
        return {};
    }

    Bytes output;
    output.reserve(hex_string.length() / 2);

    for (size_t i = 0; i < hex_string.length(); i += 2) {
        auto high = char_to_nibble(hex_string[i]);
        if (!high.has_value()) {
            return {};
        }
        auto low  = char_to_nibble(hex_string[i + 1]);
        if (!low.has_value()) {
            return {};
        }

        output.push_back((high.value() << 4) | low.value());
    }

    return output;
}

Bytes file_as_bytes(const std::string& file_path)
{
    Bytes dest;
    const bool success = file_stream(file_path, [&dest](BytesView buf)
    {
        const auto offset = dest.size();
        dest.resize(offset + buf.size_bytes());
        std::memcpy(dest.data() + offset, buf.data(), buf.size());
    });
    if (!success) {
        throw CryptoException(fmt::format("Cannot load file {}", file_path));
    }

    return dest;
}

std::string file_as_text(const std::string& file_path)
{
    auto bytes = file_as_bytes(file_path);
    return {bytes.begin(), bytes.end()};
}

} // namespace Slic3r::Biz::Crypto
