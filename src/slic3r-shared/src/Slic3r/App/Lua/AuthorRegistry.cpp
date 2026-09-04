#include "Slic3r/App/Lua/AuthorRegistry.hpp"
#include "Slic3r/Assert.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

#include <boost/filesystem/path.hpp>
#include <boost/nowide/cstdio.hpp>
#include <fmt/format.h>

namespace Slic3r::App::Lua {

namespace fs = boost::filesystem;

void AuthorRegistry::store_author_key(std::string_view author_id, std::string_view public_key_pem)
{
    auto path = m_dir_path / (std::string{author_id} + ".pem");

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

Biz::Crypto::KeyPair AuthorRegistry::load_author_key(std::string_view author_id)
{
    auto path = m_dir_path / (std::string{author_id} + ".pem");

    auto bytes = Biz::Crypto::file_as_bytes(path.string());
    return Biz::Crypto::KeyPair::load_pub_pem(bytes);
}

} // namespace Slic3r::App::Lua
