#pragma once

#include <string_view>
#include <utility>

#include <boost/filesystem/path.hpp>

#include <Slic3r/Biz/Crypto/Sign.hpp>

namespace Slic3r::App::Lua {

class AuthorRegistry
{
public:
    explicit AuthorRegistry(boost::filesystem::path dir_path) : m_dir_path(std::move(dir_path)) {}

    void store_author_key(std::string_view author_id, std::string_view public_key_pem);
    Biz::Crypto::KeyPair load_author_key(std::string_view author_id);

private:
    boost::filesystem::path m_dir_path;
};

} // namespace Slic3r::App::Lua
