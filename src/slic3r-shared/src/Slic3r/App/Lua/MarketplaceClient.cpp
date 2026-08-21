#include "Slic3r/App/Lua/MarketplaceClient.hpp"

#include "Slic3r/Directories.hpp"
#include "Slic3r/Log.hpp"
#include "Slic3r/Biz/CerealUtils.hpp"
#include "Slic3r/Biz/Network/IHttp.hpp"
#include "Slic3r/Biz/Platform/PlatformServices.hpp"
#include "Slic3r/Biz/Platform/JobManager/JobManager.hpp"

#include <boost/filesystem/operations.hpp>
#include <boost/nowide/fstream.hpp>
#include <fmt/format.h>

namespace Slic3r::App::Lua {

namespace fs = boost::filesystem;

BundleRequestConfig::LoadResult BundleRequestConfig::load_from_json(const nlohmann::json& j)
{
    BundleRequestConfig ret;

    try {
        j.at("pluginId").get_to(ret.plugin_id);
        j.at("version").get_to(ret.version);
        j.at("downloadUrl").get_to(ret.download_url);
        j.at("verifyUrl").get_to(ret.verify_url);
        j.at("apiOrigin").get_to(ret.api_origin);
    } catch (nlohmann::json::exception& e) {
        return tl::unexpected{fmt::format("Error parsing JSON: {}", e.what())};
    }

    return ret;
}

void MarketplaceClient::download_bundle(const BundleRequestConfig& config)
{
    perform_verify_step(config);
}

namespace {
auto make_retry_fn(Biz::JThread::StopToken& stop_token)
{
    return [&](Biz::Network::IHttp::Retry r, bool& cancel)
    {
        if (stop_token.stop_requested())
            cancel = true;
    };
}

/**
 * HttpCurl reports HTTP >= 400 with an empty `error` and the payload in `body`, and
 * transport failures the other way round. Without this, a 404 arrives as an empty
 * message. Note curl follows redirects, so the status can come from the storage
 * backend the download URL redirects to, not from the marketplace API.
 */
std::string http_error_message(const std::string& body, const std::string& error, unsigned status)
{
    constexpr std::size_t max_body = 300;
    const std::string detail = body.size() > max_body ? body.substr(0, max_body) + "..." : body;

    if (!error.empty()) {
        return status != 0 ? fmt::format("{} (HTTP {})", error, status) : error;
    }
    if (status != 0) {
        return detail.empty() ? fmt::format("HTTP {}", status)
                              : fmt::format("HTTP {}: {}", status, detail);
    }
    return detail.empty() ? std::string{"request failed for an unknown reason"} : detail;
}

tl::expected<void, std::string> save_file(const fs::path& dest, std::string_view data)
{
    try {
        auto dest_dir = dest.parent_path();
        boost::system::error_code ec;
        if (!fs::exists(dest_dir, ec)) {
            fs::create_directories(dest_dir, ec);
            if (ec.failed()) {
                return tl::unexpected{ec.what()};
            }
        }

        boost::nowide::ofstream f{dest, std::ios::out | std::ios::binary};
        if (!f.good()) {
            return tl::unexpected{fmt::format("Cannot write file {}", dest.string())};
        }
        f.write(data.data(), data.size());
        f.close();
    } catch (std::exception& e) {
        return tl::unexpected{e.what()};
    }

    return {};
}

}

void MarketplaceClient::perform_verify_step(const BundleRequestConfig& config)
{
    using VerifyResult = tl::expected<VerifiedBundleData, std::string>;

    auto job = [config](Biz::JThread::StopToken stop_token) -> VerifyResult
    {
        auto retry_fn = make_retry_fn(stop_token);
        auto http_ptr = Biz::Network::IHttp::
            create(Biz::Network::IHttp::RequestMethod::Get, config.verify_url, retry_fn);
        auto& http = *http_ptr;

        VerifyResult result = tl::unexpected{std::string{"verify request did not complete"}};

        http.on_complete([&](const std::string& body, unsigned status)
        {
            try {
                nlohmann::json j = nlohmann::json::parse(body);
                if (!j.value("ok", false)) {
                    result = tl::unexpected{fmt::format(
                        "bundle signature was refused: {}",
                        j.value("reason", std::string{"no reason given"})
                    )};
                    return;
                }

                VerifiedBundleData data;
                j.at("assetId").get_to(data.plugin_id);
                j.at("version").get_to(data.version);
                j.at("publicKey").get_to(data.public_key);
                result = std::move(data);
            } catch (std::exception& e) {
                result = tl::unexpected{fmt::format("cannot parse verify response: {}", e.what())};
            }
        })
        .on_error([&](const std::string& body, const std::string& error, unsigned status)
        {
            result = tl::unexpected{http_error_message(body, error, status)};
        });

        http.perform_sync();

        return result;
    };

    auto& job_manager = Biz::Platform::PlatformServices::instance().job_manager();
    job_manager.create_job("plugin-verify", std::move(job))
        .on_result(
            [config, this](VerifyResult result)
            {
                if (!result.has_value()) {
                    report_error(config.plugin_id, result.error());
                    return;
                }
                invoke_listeners<IMarketplaceClientListener>(
                    [&](IMarketplaceClientListener* l) { l->on_bundle_verified(result.value()); }
                );
                perform_download_step(config);
            }
        )
        .on_exception(
            [config, this](const std::exception_ptr& eptr)
            {
                try {
                    std::rethrow_exception(eptr);
                } catch (std::exception& e) {
                    report_error(config.plugin_id, e.what());
                }
            }
        )
        .start();
}

void MarketplaceClient::perform_download_step(const BundleRequestConfig& config)
{
    using DownloadResult = tl::expected<std::string, std::string>;

    auto job = [config, this](Biz::JThread::StopToken stop_token) -> DownloadResult
    {
        auto retry_fn = make_retry_fn(stop_token);
        auto http_ptr = Biz::Network::IHttp::
            create(Biz::Network::IHttp::RequestMethod::Get, config.download_url, retry_fn);
        auto& http = *http_ptr;

        DownloadResult result = tl::unexpected{std::string{"download request did not complete"}};

        http.on_complete([&](const std::string& body, unsigned status)
        {
            if (body.empty()) {
                result = tl::unexpected{fmt::format("downloaded bundle is empty (HTTP {})", status)};
                return;
            }

            auto dest_path =
                download_dir() / fmt::format("{}@{}.zip", config.plugin_id, config.version);
            if (auto saved = save_file(dest_path, body); !saved.has_value()) {
                result = tl::unexpected{saved.error()};
                return;
            }

            result = dest_path.string();
        })
        .on_error([&](const std::string& body, const std::string& error, unsigned status)
        {
            result = tl::unexpected{http_error_message(body, error, status)};
        });

        http.perform_sync();

        return result;
    };

    auto& job_manager = Biz::Platform::PlatformServices::instance().job_manager();
    job_manager.create_job("plugin-download", std::move(job))
        .on_result(
            [config, this](DownloadResult result)
            {
                if (!result.has_value()) {
                    report_error(config.plugin_id, result.error());
                    return;
                }
                invoke_listeners<IMarketplaceClientListener>(
                    [&](IMarketplaceClientListener* l)
                    {
                        l->on_bundle_downloaded(config.plugin_id, config.version, result.value());
                    }
                );
            }
        )
        .on_exception(
            [config, this](const std::exception_ptr& eptr)
            {
                try {
                    std::rethrow_exception(eptr);
                } catch (std::exception& e) {
                    report_error(config.plugin_id, e.what());
                }
            }
        )
        .start();
}

void MarketplaceClient::report_error(const std::string& plugin_id, const std::string& message)
{
    // Only ever called from job on_result / on_exception, i.e. on the main thread.
    SPDLOG_ERROR("Plugin Marketplace: {} failed: {}", plugin_id, message);
    invoke_listeners<IMarketplaceClientListener>(
        [&](IMarketplaceClientListener* l) { l->on_download_error(plugin_id, message); }
    );
}

fs::path MarketplaceClient::download_dir() const
{
    return fs::path{data_dir()} / "download";
}

fs::path MarketplaceClient::keys_dir() const
{
    return fs::path{data_dir()} / "authorized_authors";
}

} // namespace Slic3r::App::Lua
