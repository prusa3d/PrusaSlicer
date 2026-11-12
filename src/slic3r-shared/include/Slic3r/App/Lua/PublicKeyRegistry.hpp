#pragma once

#include <string_view>
#include <utility>

#include <boost/filesystem/path.hpp>

#include <Slic3r/Biz/Crypto/Sign.hpp>

namespace Slic3r::App::Lua {

class PublicKeyRegistry
{
public:
    explicit PublicKeyRegistry(boost::filesystem::path dir_path) : m_dir_path(std::move(dir_path)) {}

    void store_key(std::string_view key_id, std::string_view public_key_pem);
    Biz::Crypto::KeyPair load_key(std::string_view key_id);

private:
    /** @brief Path of the key file; throws CryptoException if it leaves the key directory. */
    boost::filesystem::path key_path(std::string_view key_id) const;

    boost::filesystem::path m_dir_path;
};

} // namespace Slic3r::App::Lua
