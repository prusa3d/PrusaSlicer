#pragma once

#include <string>
#include <vector>

#include "Slic3r/Biz/Crypto/Hash.hpp"
#include "Slic3r/Biz/Crypto/ContentProvider.hpp"

namespace Slic3r::Biz::Crypto {

struct FileChecksum
{
    Hash hash;
    std::string path;
};

struct ChecksumVerifyOpts
{
    using StringList = std::vector<std::string>;

    bool strict{true};
    StringList whitelist;
};

struct DirChecksum
{
    using FileChecksums = std::vector<FileChecksum>;
    FileChecksums entries;

    bool verify(IContentProvider& content_provider, const ChecksumVerifyOpts& opts = {}) const;

    static DirChecksum load_from_file(IContentProvider& content_provider, const std::string& path, HashType hash_type);
};

} // namespace Slic3r::Biz::Crypto
