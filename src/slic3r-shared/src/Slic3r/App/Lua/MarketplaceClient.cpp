#include "Slic3r/App/Lua/MarketplaceClient.hpp"

#include "Slic3r/Directories.hpp"
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

    auto job = [config = config, this](Biz::JThread::StopToken stop_token)
    {
        auto retry_fn = make_retry_fn(stop_token);
        auto http_ptr = Biz::Network::IHttp::
            create(Biz::Network::IHttp::RequestMethod::Get, config.verify_url, retry_fn);
        auto& http = *http_ptr;
        std::optional<VerifiedBundleData> data;
        http.on_complete([&](const std::string& body, unsigned status)
        {
            try {
                nlohmann::json j = nlohmann::json::parse(body);
                bool ok;
                std::string asset_id;
                std::string version;
                std::string public_key;
                std::string author_id;
                j.at("ok").get_to(ok);
                j.at("assetId").get_to(asset_id);
                j.at("version").get_to(version);
                j.at("publicKey").get_to(public_key);
                j.at("authorId").get_to(author_id);

                data = {.plugin_id = asset_id, .version = version, .public_key = public_key};
            } catch (std::exception& e) {
                report_error(config.plugin_id, e.what());
            }
        });

        http_ptr->perform_sync();

        return data;
    };

    auto& job_manager = Biz::Platform::PlatformServices::instance().job_manager();
    job_manager.create_job("plugin-verify", std::move(job))
        .on_result(
            [config = config, this](auto data)
            {
                if (data.has_value()) {
                    invoke_listeners<IMarketplaceClientListener>(
                        [&](IMarketplaceClientListener* l) { l->on_bundle_verified(data.value()); }
                    );
                    perform_download_step(config);
                }
            }
        )
        .on_exception(
            [this, &config](const std::exception_ptr& eptr)
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
    auto job = [config = config, this](Biz::JThread::StopToken stop_token)
    {
        std::optional<std::string> download_path;
        auto retry_fn = make_retry_fn(stop_token);
        auto http_ptr = Biz::Network::IHttp::
            create(Biz::Network::IHttp::RequestMethod::Get, config.download_url, retry_fn);
        auto& http = *http_ptr;
        http // setup handlers
            .on_complete(
                [this, &config, &download_path](const std::string& body, unsigned status)
                {
                    auto dest_path =
                        download_dir() / fmt::format("{}@{}.zip", config.plugin_id, config.version);
                    if (auto result = save_file(dest_path, body); !result.has_value()) {
                        report_error(config.plugin_id, result.error());
                        return;
                    }

                    download_path = dest_path.string();
                }
            )
            .on_error(
                [this, &config](const std::string& body, const std::string& error, unsigned status)
                {
                    Biz::Platform::PlatformServices::instance()
                        .main_thread_dispatcher()
                        .dispatch_on_main_thread(
                            [=, this] { report_error(config.plugin_id, error); }
                        );
                }
            );

        http.perform_sync();

        return download_path;
    };

    auto& job_manager = Biz::Platform::PlatformServices::instance().job_manager();
    job_manager.create_job("plugin-download", std::move(job))
        .on_result([config = config, this](const auto& download_path)
        {
            if (download_path.has_value()) {
                invoke_listeners<IMarketplaceClientListener>(
                    [&download_path, &config](IMarketplaceClientListener* l)
                    {
                        l->on_bundle_downloaded(
                            config.plugin_id,
                            config.version,
                            download_path.value()
                        );
                    }
                );
                perform_download_step(config);
            }
        })
        .on_exception(
            [this, &config](const std::exception_ptr& eptr)
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
