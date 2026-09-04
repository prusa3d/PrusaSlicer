#include "Slic3r/App/Lua/PluginBundle.hpp"
#include "Slic3r/Biz/Crypto//Checksum.hpp"
#include <Slic3r/Assert.hpp>

#include <ranges>

#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/nowide/cstdio.hpp>
#include <nlohmann/json.hpp>
#include <tl/expected.hpp>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

namespace fs = boost::filesystem;

namespace Slic3r::App::Lua {

template <typename T>
using Result = tl::expected<T, std::string>;

namespace {
const std::map<PluginApiType, std::string> PLUGIN_API_TYPE_NAMES = {
    {PluginApiType::Project, "project.plugin"}
};
}


std::string to_string(PluginApiType pat)
{
    auto it = PLUGIN_API_TYPE_NAMES.find(pat);
    ASSERT(it != PLUGIN_API_TYPE_NAMES.end());
    return it->second;
}


tl::expected<PluginApiType, std::string> parse_plugin_api_type(std::string_view s)
{
    auto r = PLUGIN_API_TYPE_NAMES | std::views::values;
    auto it = std::ranges::find(r, s);
    if (it == r.end()) {
        return tl::unexpected{fmt::format("Cannot parse Plugin API type from string \"{}\"", s)};
    }
    return it.base()->first;
}

Result<PluginBundleMeta> load_meta_from_text(std::string_view json_str)
{
    try {
        nlohmann::json json = nlohmann::json::parse(json_str);

        auto get = [&json]<typename T>(std::string_view name, T& ret, bool required=true) -> std::optional<std::string>
        {
            auto it = json.find(name);
            if (it == json.end()) {
                return required ? std::make_optional(fmt::format("Missing required field '{}'", name)) :
                                  std::nullopt;
            }

            if constexpr (std::is_same_v<T, Semver> || std::is_same_v<T, std::optional<Semver>>) {
                std::string v = it->get<std::string>();
                auto parsed = Semver::parse(v);
                if (!parsed.has_value()) {
                    return fmt::format("Cannot parse version from string \"{}\"", v);
                }
                ret = parsed.value();
                return std::nullopt;
            } else {
                if constexpr (std::is_same_v<T, std::string>) {
                    if (!it->is_string()) {
                        return fmt::format(
                            "Field '{}' has unexpected type. Expecting string value.",
                            name
                        );
                    }
                }

                ret = it->get<T>();
                return std::nullopt;
            }
        };

        auto parse_api_version_map = [&get]() -> Result<PluginBundleMeta::ApiVersionMap>
        {
            PluginBundleMeta::ApiVersionMap ret;
            std::map<std::string, std::string> raw_data;
            if (auto err = get("required_apis", raw_data, true); err.has_value()) {
                return tl::unexpected(err.value());
            }
            for (const auto& [raw_api, raw_ver] : raw_data) {
                auto api = parse_plugin_api_type(raw_api);
                if (!api.has_value()) {
                    return tl::unexpected{api.error()};
                }
                auto ver = Semver::parse(raw_ver);
                if (!ver.has_value()) {
                    return tl::unexpected(
                        fmt::format("Cannot parse version from string \"{}\"", raw_ver)
                    );
                }
                ret.emplace(api.value(), ver.value());
            }
            return ret;
        };

        PluginBundleMeta meta;
#define SAFE_GET_(field, req)\
if (auto err = get(#field, meta.field, req); err.has_value()) { \
return tl::unexpected{err.value()}; \
}
#define SAFE_GET(field) SAFE_GET_(field, true)
#define SAFE_GET_OPT(field) SAFE_GET_(field, false)

        SAFE_GET(id)
        SAFE_GET(name)
        SAFE_GET(version);
        SAFE_GET(author)
        SAFE_GET(license)
        SAFE_GET_OPT(description)
        SAFE_GET_OPT(category)
        SAFE_GET(min_slicer_version)
        SAFE_GET_OPT(max_slicer_version)
        SAFE_GET_OPT(repo)
        SAFE_GET_OPT(web)

        auto req_apis = parse_api_version_map();
        if (!req_apis.has_value()) {
            return tl::unexpected{req_apis.error()};
        }
        meta.required_apis = req_apis.value();

#undef SAFE_GET_
#undef SAFE_GET
#undef SAFE_GET_OPT
#undef SAFE_GET_DEST

        return meta;
    } catch (nlohmann::json::exception& e) {
        return tl::unexpected{fmt::format("Parsing JSON failed: {}", e.what())};
    }
}

PluginInstallResult PluginBundle::load_meta()
{
    auto content_opt = m_content_provider->file_as_text(META_FILENAME);
    if (!content_opt.has_value()) {
        return tl::unexpected{fmt::format("Cannot load {}", META_FILENAME)};
    }
    auto meta_load_result = load_meta_from_text(content_opt.value());
    if (!meta_load_result.has_value()) {
        return tl::unexpected{
            fmt::format("Cannot parse {}: {}", META_FILENAME, meta_load_result.error())
        };
    }
    m_meta = meta_load_result.value();
    return {};
}

bool PluginBundle::verify(const Biz::Crypto::KeyPair& author_pub_key) const
{
    try {
        auto sign_data = m_content_provider->file_as_bytes(SIGN_FILENAME);
        if (!sign_data.has_value()) {
            SPDLOG_ERROR("Cannot load file {}", SIGN_FILENAME);
            return false;
        }
        auto s = Biz::Crypto::Signature{sign_data.value()};

        auto sign_verifier = Biz::Crypto::create_signature_verifier(HASH_TYPE, author_pub_key);
        ASSERT(sign_verifier != nullptr);
        if (!m_content_provider->file_stream(CHECKSUM_FILENAME, *sign_verifier)) {
            SPDLOG_ERROR("Cannot load file {}", CHECKSUM_FILENAME);
            return false;
        }
        auto verified = sign_verifier->finalize(s);
        if (!verified) {
            SPDLOG_ERROR("Invalid signature for given public key");
            return false;
        }

        const auto checksum = Biz::Crypto::DirChecksum::load_from_file(
            *m_content_provider,
            CHECKSUM_FILENAME,
            HASH_TYPE
        );
        Biz::Crypto::ChecksumVerifyOpts opts{
            .strict    = true,
            .whitelist = {CHECKSUM_FILENAME, SIGN_FILENAME}
        };
        if (!checksum.verify(*m_content_provider, opts)) {
            SPDLOG_ERROR("Checksum doesn't match");
            return false;
        }

        return true;
    } catch (Biz::Crypto::CryptoException& e) {
        SPDLOG_ERROR("Error while loading {}: {}", CHECKSUM_FILENAME, e.what());
        return false;
    }
}

PluginInstallResult PluginBundle::install(const std::string& dest_base_dir)
{
    const fs::path dest_path{dest_base_dir};

    struct FileWriter
    {
        FILE* fp{nullptr};
        const char* file_path;

        explicit FileWriter(const char* file_path): file_path(file_path)
        {
            fp = boost::nowide::fopen(file_path, "wb");
        }

        ~FileWriter()
        {
            if (fp) {
                fclose(fp);
                fp = nullptr;
            }
        }

        bool ok() const { return fp != nullptr; }

        void update(Biz::Crypto::BytesView bytes)
        {
            if (fp) {
                const auto n = bytes.size_bytes();
                const auto written = fwrite(bytes.data(), 1, n, fp);
                if (written != n) {
                    throw std::logic_error(fmt::format("Cannot write file {}", file_path));
                }
            }
        }
    };

    const auto files = m_content_provider->list_files();
    for (const auto& file : files) {
        const auto p = dest_path / file;

        if (!is_path_in_sandbox(dest_path, p)) {
            SPDLOG_ERROR("Malformed file path: {}, won't be copied", file);
            continue;
        }
        const auto parent = p.parent_path();

        boost::system::error_code ec;
        const bool create_dir = !fs::is_directory(parent, ec) || ec;

        if (create_dir) {
            ec.clear();
            fs::create_directories(parent, ec);

            if (ec.failed()) {
                return tl::unexpected{ec.message()};
            }
        }

        try
        {
            const auto p_str = p.string();
            FileWriter writer(p_str.c_str());
            if (!writer.ok()) {
                return tl::unexpected{fmt::format("Cannot write file {}", p_str)};
            }
            m_content_provider->file_stream(file, writer);
        }
        catch (std::exception& e) {
            return tl::unexpected{e.what()};
        }
    }

    return {};
}

PluginBundle::Plugins PluginBundle::load_plugins()
{
    Plugins plugins;

    const auto files = m_content_provider->list_files();
    const std::string id_prefix = m_meta.id + ".";
    for (const auto& f : files) {
        if (f.ends_with(".lua")) {
            Biz::Lua::LuaEngine lua;
            auto entry_path = fs::path{m_content_provider->path()} / f;
            sol::protected_function_result ret;
            try {
                ret = lua.run_file(entry_path.string());
            } catch (std::exception& e) {
                SPDLOG_ERROR("Failed loading plugin: {}", e.what());
            }
            if (!ret.valid()) {
                sol::error err = ret;
                SPDLOG_ERROR("Failed loading plugin {}: {}", entry_path.string(), err.what());
            }
            if (auto result = Plugin::parse(lua, id_prefix, entry_path.string())) {
                auto&& plugin = result.value();

                plugins.emplace_back(std::move(plugin));
            } else {
                SPDLOG_INFO(
                    "Parsing metadata of plugin {} unsuccessful: {}\n"
                    "This may not be an error if the .lua file is not a plugin but shared module.",
                    entry_path.string(),
                    result.error()
                );
            }

        }
    }
    return plugins;
}

bool PluginBundle::is_plugin_bundle_dir(const std::string& path)
{
    return fs::exists(fs::path{path} / META_FILENAME);
}

} // namespace Slic3r::App::Lua
