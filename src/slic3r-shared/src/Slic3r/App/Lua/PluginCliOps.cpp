#include "Slic3r/App/Lua/PluginCliOps.hpp"

#include "Slic3r/Directories.hpp"
#include "Slic3r/Log.hpp"
#include "Slic3r/Biz/Algorithms/MiniZWrapper.hpp"
#include "Slic3r/Biz/Crypto/ContentProvider.hpp"
#include "Slic3r/Biz/Crypto/Sign.hpp"
#include "Slic3r/App/Console/ConsoleInput.hpp"
#include "Slic3r/App/Lua/PluginBundle.hpp"
#include "Slic3r/App/Lua/PluginCliOpsLicenses.hpp"
#include "Slic3r/Biz/Platform/Termination.hpp"

#include <iostream>
#include <algorithm>
#include <cctype>
#include <string_view>

#include <boost/nowide/cstdio.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/directory.hpp>
#include <boost/nowide/convert.hpp>
#include <nlohmann/json.hpp>

#ifdef _WIN32
#include <windows.h>
#include <aclapi.h>
#include <vector>
#include <string>
namespace {
bool restrict_to_current_user(const std::wstring& path)
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return false;

    DWORD len = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &len);   // query size
    std::vector<BYTE> buf(len);
    BOOL ok = GetTokenInformation(token, TokenUser, buf.data(), len, &len);
    CloseHandle(token);
    if (!ok) return false;

    PSID user_sid = reinterpret_cast<TOKEN_USER*>(buf.data())->User.Sid;

    EXPLICIT_ACCESS_W ea{};
    ea.grfAccessPermissions = FILE_ALL_ACCESS;
    ea.grfAccessMode        = SET_ACCESS;
    ea.grfInheritance       = NO_INHERITANCE;
    ea.Trustee.TrusteeForm  = TRUSTEE_IS_SID;
    ea.Trustee.TrusteeType  = TRUSTEE_IS_USER;
    ea.Trustee.ptstrName    = reinterpret_cast<LPWSTR>(user_sid);

    PACL acl = nullptr;
    if (SetEntriesInAclW(1, &ea, nullptr, &acl) != ERROR_SUCCESS)
        return false;

    DWORD rc = SetNamedSecurityInfoW(
        const_cast<LPWSTR>(path.c_str()),
        SE_FILE_OBJECT,
        DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
        nullptr, nullptr, acl, nullptr);

    LocalFree(acl);
    return rc == ERROR_SUCCESS;
}
} // namespace
#endif
namespace Slic3r::App::Lua {

namespace fs = boost::filesystem;

namespace {
bool write_file(std::string_view path, std::string_view data)
{
    FILE* fp = boost::nowide::fopen(path.data(), "wb");
    if (fp == nullptr) {
        return false;
    }
    auto written = fwrite(data.data(), 1, data.size(), fp);
    fclose(fp);
    return written == data.size();
}

using VarMap = std::map<std::string, std::string>;

// Expands "{{varName}}" placeholders in `source_template` using `vars`.
//
// Behaviour:
//   * A placeholder whose name is not in `vars` is copied through verbatim
//     (including the braces), so nothing is silently lost.
//   * An unterminated "{{" is copied through verbatim.
//   * If another "{{" appears before the closing "}}", the outer one is treated
//     as literal text and scanning restarts at the inner one, so "{{{{x}}}}"
//     yields "{{" + value_of_x + "}}".
//   * Expansion is single-pass: substituted values are never rescanned, so a
// value containing "{{...}}" cannot trigger further expansion.
std::string
expand_template(std::string_view source_template, const VarMap& vars)
{
    constexpr std::string_view OPEN_TAG  = "{{";
    constexpr std::string_view CLOSE_TAG = "}}";
    constexpr auto npos = std::string_view::npos;

    std::string out;
    out.reserve(source_template.size());

    std::size_t pos = 0;  // start of the not-yet-copied tail
    while (pos < source_template.size())
    {
        const std::size_t start = source_template.find(OPEN_TAG, pos);
        if (start == npos)
            break;

        const std::size_t name_begin = start + OPEN_TAG.size();

        const std::size_t end = source_template.find(CLOSE_TAG, name_begin);
        if (end == npos)
            break;  // unterminated placeholder: copy the rest as-is

        // A nested "{{" before the "}}" wins; treat the outer one as literal.
        const std::size_t next_open = source_template.find(OPEN_TAG, name_begin);
        if (next_open != npos && next_open < end)
        {
            out.append(source_template.substr(pos, next_open - pos));
            pos = next_open;  // strictly greater than pos, so we always advance
            continue;
        }

        out.append(source_template.substr(pos, start - pos));

        const std::string_view name =
            source_template.substr(name_begin, end - name_begin);

        // std::map<std::string, std::string> uses std::less<std::string>, which
        // is not transparent, so the key has to be materialised. Declaring the
        // map as std::map<std::string, std::string, std::less<>> would let this
        // be `vars.find(name)` with no allocation.
        if (const auto it = vars.find(std::string(name)); it != vars.end())
            out.append(it->second);
        else
            out.append(source_template.substr(start, end + CLOSE_TAG.size() - start));

        pos = end + CLOSE_TAG.size();
    }

    out.append(source_template.substr(pos));
    return out;
}

bool expand_template_file(
    const std::string& template_file_path,
    const std::string& dest_file_path,
    const VarMap& vars
)
{
    try {
        auto template_src = Biz::Crypto::file_as_text(template_file_path);
        auto buf = expand_template(template_src, vars);
        return write_file(dest_file_path, buf);

    } catch (Biz::Crypto::CryptoException& e) {
        SPDLOG_ERROR("Template expansion failed: {}", e.what());
        return false;
    }
}

bool looks_like_reverse_domain_id(std::string_view input)
{
    constexpr std::size_t min_labels = 3;

    auto is_valid_label = [](std::string_view label) {
        if (label.empty() || label.size() > 63)
            return false;
        if (!std::isalpha(static_cast<unsigned char>(label.front())))
            return false;       // must start with a letter
        if (label.back() == '-')
            return false;       // must not end with a hyphen
        return std::ranges::all_of(label, [](char c) {
            return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_';
        });
    };

    if (input.empty() || input.size() > 255)
        return false;

    std::size_t labels = 0;
    while (true) {
        const auto dot = input.find('.');
        if (!is_valid_label(input.substr(0, dot)))
            return false;
        ++labels;

        if (dot == std::string_view::npos)
            break;

        input.remove_prefix(dot + 1);
        if (input.empty())
            return false;       // trailing dot
    }

    return labels >= min_labels;
}

void console_output(std::string_view msg)
{
    std::cout << msg << std::endl;
}

void console_error(std::string_view msg)
{
    std::cerr << msg << std::endl;
}

[[noreturn]]
void fail_error(std::string_view msg)
{
    console_error(msg);
    Biz::Platform::close();
    std::exit(127);
}

[[nodiscard]] std::string json_escape(std::string_view in)
{
    std::string s = nlohmann::json(in).dump();
    return s.substr(1, s.size() - 2);
}

bool is_valid_name(std::string_view name, std::string_view extra_allowed = "")
{
    return name != "." && name != ".." && std::ranges::all_of(
        name,
        [&extra_allowed](char ch)
        {
            return ('a' <= ch && ch <= 'z')
                || ('0' <= ch && ch <= '9')
                || ('A' <= ch && ch <= 'Z')
                || ch == '-'
                || ch == '.'
                || ch == '_'
                || extra_allowed.find(ch) != std::string::npos;
        }
    );
}

bool is_valid_path(const std::string& p)
{
    auto path = fs::path{p};
    return std::ranges::
        all_of(path, [](const auto& p) -> bool { return is_valid_name(p.string(), " "); });
}

Console::Validation validate_id(std::string_view v)
{
    if (v.empty()) {
        return {Console::ValidationStatus::Error, "ID must be not empty"};
    }
    if (!is_valid_name(v)) {
        return {
            Console::ValidationStatus::Error,
            "Only following characters are allowed: [a-zA-Z0-9.-_]+"
        };
    }
    if (!looks_like_reverse_domain_id(v)) {
        return {
            Console::ValidationStatus::Warning,
            "ID should be in reverse DNS like form (e.g. com.example.awesome.plugin-bundle)"
        };
    }
    return { Console::ValidationStatus::OK };
};

Console::Validation validate_str(std::string_view v)
{
    if (v.empty()) {
        return {Console::ValidationStatus::Error, "Field must be not empty"};
    }
    return { Console::ValidationStatus::OK };
};

Console::Validation validate_author(std::string_view v)
{
    if (v.empty()) {
        return {Console::ValidationStatus::Error, "Field must be not empty"};
    }
    if (!is_valid_name(v)) {
        return {
            Console::ValidationStatus::Error,
            "Author name has to conform to [a-zA-Z0-9.-_]+"
        };
    }
    return { Console::ValidationStatus::OK };
};

Console::Validation validate_license(std::string_view v)
{
    if (v.empty()) {
        return {Console::ValidationStatus::Error, "License must be not empty"};
    }
    if (auto it = std::ranges::find(KNOWN_LICENSES, std::string{v}); it == KNOWN_LICENSES.end()) {
        return {
            Console::ValidationStatus::Warning,
            "Unknown license, it should be SPDX license identifier, see: https://spdx.github.io/license-list-data/"
        };
    }
    return { Console::ValidationStatus::OK };
};



} // namespace


void plugin_init(PluginInitActionParams& params)
{
    auto check_params_value = [force = params.force](std::string_view field, std::string_view val, const std::function<Console::Validation(std::string_view v)>& validator)
    {
        auto v = validator(val);
        if (v.status == Console::ValidationStatus::Error) {
            fail_error(fmt::format("Invalid {}: {}", field, v.message));
        }
        if (v.status == Console::ValidationStatus::Warning && !force) {
            fail_error(fmt::format("Invalid {}: {} Use --force to force the value", field, v.message));
        }
    };

    try {
        if (!params.id.has_value()) {
            params.id = Console::get_input("Plugin Bundle ID: ", "com.example.my-plugin", validate_id, {});
        } else {
            check_params_value("id", params.id.value(), validate_id);
        }

        if (!params.name.has_value()) {
            params.name = Console::get_input("Name: ", "My Plugin", validate_str, {});
        } else {
            check_params_value("name", params.name.value(), validate_str);
        }

        if (!params.author.has_value()) {
            params.author = Console::get_input("Author: ", "the.author", validate_author, {});
        } else {
            check_params_value("author", params.author.value(), validate_author);
        }

        if (!params.license.has_value()) {
            params.license = Console::get_input("License: ", "BSD-3-Clause", validate_license, KNOWN_LICENSES);
        } else {
            check_params_value("license", params.license.value(), validate_license);
        }

        auto base_dir = fs::path{resources_dir()} / "lua_template" / "project.plugin";
        auto dest_dir = fs::path{params.dest_path} / params.id.value();
        if (fs::exists(dest_dir)) {
            bool handled = false;
            if (fs::is_directory(dest_dir) && params.force) {
                console_output(fmt::format("Removing directory {}", dest_dir.string()));
                fs::remove_all(dest_dir);
                handled = true;
            }

            if (!handled) {
                fail_error(
                    fmt::format(
                        "Directory or file {} already exists, cannot create plugin bundle directory. "
                        "Use --force flag to remove this directory.",
                        dest_dir.string()
                    )
                );
            }
        }
        fs::create_directories(dest_dir);
        console_output(fmt::format("Created directory {}", dest_dir.string()));

        auto script_path_src  = base_dir / "hello.lua";
        auto script_path_dest = dest_dir / "hello.lua";
        fs::copy_file(script_path_src, script_path_dest);
        console_output(fmt::format("Created file {}", script_path_dest.string()));

        auto manifest_path_src  = (base_dir / META_FILENAME).string();
        auto manifest_path_dest = (dest_dir / META_FILENAME).string();
        const auto expanded = expand_template_file(
            manifest_path_src,
            manifest_path_dest,
            // Clang formatting this part drives me crazy
            // Like in what parallel universe is single space indent is easy to read
            // clang-format off
            {
                {"id", json_escape(params.id.value())},
                {"name", json_escape(params.name.value())},
                {"license", json_escape(params.license.value())},
                {"description", json_escape(params.description.value_or("A plugin description"))},
                {"author", json_escape(params.author.value())}
            }
            // clang-format on
        );
        if (!expanded) {
            throw std::runtime_error("Expanding template failed");
        }
        console_output(fmt::format("Created file {}", manifest_path_dest));

    } catch (std::exception& e) {
        fail_error(e.what());
    }
}

void plugin_keygen(const PluginKeygenActionParams& params)
{
    try {
        auto keypair = Biz::Crypto::KeyPair::generate(params.key_size);

        auto pk = keypair.save_private_key_pem();
        if (pk.empty()) {
            fail_error("Generating key PEM failed");
        }
        if (!write_file(params.priv_key_file, pk.view())) {
            fail_error(fmt::format("Cannot write file {}", params.priv_key_file));
        }
        console_output(fmt::format("Private key written to {}", params.priv_key_file));

        auto p = fs::path{params.priv_key_file};
        boost::system::error_code ec;

#if _WIN32
        if (!restrict_to_current_user(p.wstring())) {
            fail_error(fmt::format("Cannot set file permissions to {}", p.string()));
        }
#else
        fs::permissions(p, fs::owner_read | fs::owner_write, ec);
        if (ec.failed()) {
            fail_error(fmt::format("Cannot set file permissions to {}", p.string()));
        }
#endif
        auto pubk = keypair.save_public_key_pem();
        if (pubk.empty()) {
            fail_error("Generating key PEM failed");
        }
        if (!write_file(params.pub_key_file, pubk)) {
            fail_error(fmt::format("Cannot write file {}", params.pub_key_file));
        }
        console_output(fmt::format("Public key written to {}", params.pub_key_file));
    } catch (std::exception& e) {
        fail_error(e.what());
    }
}

void plugin_sign(const PluginSignActionParams& params)
{
    const fs::path bundle_dir{params.bundle_path};
    std::string bundle_id;

    auto check_params_value = [&params](std::string_view field, std::string_view val, const std::function<Console::Validation(std::string_view v)>& validator)
    {
        auto v = validator(val);

        if (v.status == Console::ValidationStatus::Error) {
            fail_error(
                fmt::
                    format("{}/manifest.json: Invalid {}: {}", params.bundle_path, field, v.message)
            );
        }

        if (v.status == Console::ValidationStatus::Warning && !params.force) {
            fail_error(
                fmt::format(
                    "{}/manifest.json: Invalid {}: {} Use --force to force the value",
                    params.bundle_path,
                    field,
                    v.message
                )
            );
        }
    };

    try {
        // validate
        {
            auto content_provider = Biz::Crypto::create_directory_source(params.bundle_path);
            PluginBundle plugin_bundle{std::move(content_provider)};
            auto result = plugin_bundle.load_meta();
            if (!result.has_value()) {
                fail_error(result.error());
            }

            const auto& meta = plugin_bundle.meta();
            bundle_id = meta.id;

            check_params_value("id", meta.id, validate_id);
            check_params_value("name", meta.name, validate_str);
            check_params_value("author", meta.author, validate_author);
            check_params_value("license", meta.license, validate_license);

            console_output(fmt::format("{}/manifest.json is valid", params.bundle_path));
        }

        const std::set<std::string> whitelisted{CHECKSUM_FILENAME, SIGN_FILENAME};

        auto content_provider = Biz::Crypto::create_directory_source(params.bundle_path);

        auto file_list = content_provider->list_files();
        std::string out;
        out.reserve(file_list.size() * 60);
        for (const auto& f : file_list) {
            if (whitelisted.contains(f)) {
                continue;
            }
            auto hash_builder = Biz::Crypto::create_hash_builder(Biz::Crypto::HashType::SHA_256);
            if (!content_provider->file_stream(f, *hash_builder)) {
                fail_error(fmt::format("Cannot read {} ", f));
            }
            out.append(hash_builder->finalize().hex_string());
            out.append("  ");
            out.append(f);
            out.append("\n");
        }

        auto checksum_path = (bundle_dir / CHECKSUM_FILENAME).string();
        if (!write_file(checksum_path, out)) {
            fail_error(fmt::format("Cannot write {}", checksum_path));
        }
        console_output(fmt::format("Files checksum written to {}", checksum_path));

        auto priv_key_bytes = Biz::Crypto::file_as_bytes(params.private_key_path);
        auto key = Biz::Crypto::KeyPair::load_priv_pem(priv_key_bytes);
        auto sign_builder = Biz::Crypto::create_signature_builder(Biz::Crypto::HashType::SHA_256, key);
        sign_builder->update(Biz::Crypto::as_bytes_view(out));
        auto sign = sign_builder->finalize();
        auto sign_path = (bundle_dir / SIGN_FILENAME).string();
        if (!write_file(
                sign_path,
                std::string{sign.bytes().data(), sign.bytes().data() + sign.bytes().size()}
            ))
        {
            fail_error(fmt::format("Cannot write {}", sign_path));
        }
        console_output(fmt::format("Files checksum signature written to {}", sign_path));

        // write zip
        Biz::Algorithms::MZ_Archive zip;
        auto zip_file_name = fmt::format("{}.zip", bundle_id);
        auto zip_file_path = fs::path{zip_file_name};
        if (fs::exists(zip_file_path)) {
            fs::remove(zip_file_path);
        }
        if (!Biz::Algorithms::open_zip_writer(&zip.arch, zip_file_name)) {
            fail_error(fmt::format("Cannot open file {} for writing", zip_file_name));
        }

        // refresh file list
        file_list = content_provider->list_files();
        for (const auto& file_path : file_list) {
            if (!is_valid_path(file_path)) {
                fail_error(
                    fmt::format(
                        "Cannot zip file {}, it is not a valid file. Only allowed file names conforms to [a-zA-Z0-9.-_ ]+",
                        file_path
                    )
                );
            }
            auto src_path = (bundle_dir / file_path).string();
            if (!mz_zip_writer_add_file(
                    &zip.arch,
                    file_path.c_str(),
                    src_path.c_str(),
                    nullptr,
                    0,
                    MZ_BEST_COMPRESSION
                ))
            {
                fail_error(fmt::format("Cannot write into zip file {}", zip_file_name));
            }
        }
        if (!mz_zip_writer_finalize_archive(&zip.arch)) {
            fail_error(fmt::format("Cannot write into zip file {}", zip_file_name));
        }
        if (!Biz::Algorithms::close_zip_writer(&zip.arch)) {
            fail_error(fmt::format("Cannot write into zip file {}", zip_file_name));
        }

        console_output(fmt::format("File {}.zip written", bundle_id));
    } catch (std::exception& e) {
        fail_error(e.what());
    }
}

} // namespace Slic3r::App::Lua
