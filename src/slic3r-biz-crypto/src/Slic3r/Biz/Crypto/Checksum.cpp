#include "Slic3r/Biz/Crypto/Checksum.hpp"

#include "Slic3r/Log.hpp"

#include <ranges>
#include <set>

#include <boost/filesystem/directory.hpp>
#include <boost/filesystem/operations.hpp>
#include <fmt/format.h>

namespace Slic3r::Biz::Crypto {

bool DirChecksum::verify(IContentProvider& content_provider, const ChecksumVerifyOpts& opts) const
{
    for (const auto& entry : entries) {
        std::unique_ptr<IHashBuilder> hash_builder = create_hash_builder(HashType::SHA_256);
        if (!content_provider.file_stream(entry.path, *hash_builder)
            || hash_builder->finalize() != entry.hash)
        {
            return false;
        }
    }

    if (opts.strict) {
        using StringSet = std::set<std::string>;

        StringSet whitelist{opts.whitelist.begin(), opts.whitelist.end()};
        StringSet verified;
        std::ranges::copy(
            entries | std::views::transform([](const auto& e) -> const auto& { return e.path; }),
            std::inserter(verified, verified.end())
        );

        auto files = content_provider.list_files();

        for (const auto& path : files) {

            bool entry_verified = verified.contains(path);
            bool entry_whitelisted = whitelist.contains(path);

            if (!entry_verified && !entry_whitelisted) {
                return false;
            }
        }
    }

    return true;
}

DirChecksum DirChecksum::load_from_file(
    IContentProvider& content_provider,
    const std::string& path,
    HashType hash_type
)
{
    DirChecksum ret;

    auto content = content_provider.file_as_text(path);
    if (!content.has_value()) {
        throw CryptoException(fmt::format("Failed loading file {}", path));
    }

    std::ranges::copy(
        content.value()
            | std::views::split('\n')
            | std::views::transform(
                [&path, hash_type](auto line) -> FileChecksum
                {
                    auto is_space             = [](unsigned char c) { return std::isspace(c); };
                    auto is_space_or_asterisk = [](unsigned char c)
                    { return c == '*' || std::isspace(c); };
                    auto check_not_line_end = [&](auto it)
                    {
                        if (it == line.end()) {
                            throw CryptoException(fmt::format("Failed loading file {}", path));
                        }
                    };

                    auto token1_start = std::ranges::find_if_not(line, is_space);
                    if (token1_start == line.end()) {
                        return {{hash_type, {}}};
                    }

                    auto token1_end = std::ranges::find_if(token1_start, line.end(), is_space);
                    check_not_line_end(token1_end);

                    std::string_view token1{token1_start, token1_end};
                    if (token1.size() != 2 * Hash::byte_size(hash_type)) {
                        throw CryptoException(fmt::format("Failed loading file {}", path));
                    }

                    auto token2_start =
                        std::ranges::find_if_not(token1_end, line.end(), is_space_or_asterisk);
                    check_not_line_end(token2_start);

                    auto token2_end = std::ranges::find_if(token2_start, line.end(), is_space);

                    return {
                        Hash{hash_type, bytes_from_hex(token1)},
                        std::string{token2_start, token2_end}
                    };
                }
            )
            | std::views::filter([](const auto& e) { return !e.hash.bytes().empty(); }),
        std::back_inserter(ret.entries)
    );

    return ret;
}

} // namespace Slic3r::Biz::Crypto
