#include "Slic3r/Biz/Crypto/Hash.hpp"
#include "Slic3r/Assert.hpp"

#include <ranges>

#include <fmt/format.h>
#include <fmt/ranges.h>

namespace Slic3r::Biz::Crypto {

std::string Hash::hex_string(std::string_view sep) const
{
    return fmt::to_string(
        fmt::join(
            m_bytes
                | std::views::transform(
                    [](uint8_t b) -> std::string { return fmt::format("{:02x}", b); }
                ),
            sep
        )
    );
}

size_t Hash::byte_size(HashType type)
{
    switch (type) {
    case HashType::SHA_1:
        return 20;
    case HashType::SHA_256:
        return 32;
    case HashType::SHA_512:
        return 64;
    default:
        ASSERT(false);
        return 0;
    }
}

Hash compute_hash(BytesView bytes_view, HashType type)
{
    auto builder = create_hash_builder(type);
    ASSERT(builder != nullptr);
    builder->update(bytes_view);
    return builder->finalize();
}

Hash compute_file_hash(const std::string& file_path, HashType type)
{
    auto builder = create_hash_builder(type);
    ASSERT(builder != nullptr);
    if (!file_stream(file_path, *builder)) {
        return Hash{type, {}};
    }
    return builder->finalize();
}

} // namespace Slic3r::Biz::Crypto
