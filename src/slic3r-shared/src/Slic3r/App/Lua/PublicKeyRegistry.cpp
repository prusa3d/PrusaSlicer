#include "Slic3r/App/Lua/PublicKeyRegistry.hpp"
#include "Slic3r/App/Lua/Plugin.hpp"
#include "Slic3r/Assert.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

#include <boost/filesystem/path.hpp>
#include <boost/nowide/cstdio.hpp>
#include <fmt/format.h>

namespace Slic3r::App::Lua {

namespace fs = boost::filesystem;

fs::path PublicKeyRegistry::key_path(std::string_view key_id) const
{
    auto path = m_dir_path / (std::string{key_id} + ".pem");
    if (!is_path_in_sandbox(m_dir_path, path)) {
        throw Biz::Crypto::CryptoException(
            // TRN {} is a public key identifier
            fmt::format(fmt::runtime(Biz::_u8L("Invalid key id: {}")), key_id)
        );
    }
    return path;
}

void PublicKeyRegistry::store_key(std::string_view key_id, std::string_view public_key_pem)
{
    auto path = key_path(key_id);

    std::string path_str = path.string();
    FILE* fp = boost::nowide::fopen(path_str.c_str(), "wb");
    if (fp == nullptr) {
        throw Biz::Crypto::CryptoException(
            // TRN {} is a path to file the writing to failed
            fmt::format(fmt::runtime(Biz::_u8L("Cannot store file: {}")), path_str)
        );
    }
    fwrite(public_key_pem.data(), public_key_pem.size(), 1, fp);
    fclose(fp);
}

Biz::Crypto::KeyPair PublicKeyRegistry::load_key(std::string_view key_id)
{
    auto path = key_path(key_id);

    auto bytes = Biz::Crypto::file_as_bytes(path.string());
    return Biz::Crypto::KeyPair::load_pub_pem(bytes);
}

} // namespace Slic3r::App::Lua
