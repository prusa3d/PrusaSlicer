#pragma once

#include <string>

#include <tl/expected.hpp>
#include <nlohmann/json.hpp>
#include <boost/filesystem/path.hpp>

#include "Slic3r/Biz/Platform/WithListeners.hpp"

namespace Slic3r::App::Lua {

struct BundleRequestConfig
{
    std::string plugin_id;
    std::string version;
    std::string download_url;
    std::string verify_url;
    std::string api_origin;

    using LoadResult = tl::expected<BundleRequestConfig, std::string>;
    static LoadResult load_from_json(const nlohmann::json& j);
};

struct VerifiedBundleData
{
    std::string plugin_id;
    std::string version;
    std::string public_key;
};

/**
 * @brief Listner to MarketplaceClient events.
 */
class IMarketplaceClientListener
{
public:
    virtual ~IMarketplaceClientListener() = default;

    /**
     * @brief Download of plugin fail notification
     * @param plugin_id An ID of plugin being downloaded
     * @param message Description of the problem
     */
    virtual void on_download_error(const std::string& plugin_id, const std::string& message) = 0;

    /**
     * @brief Notification about verification step got data
     * @param data Verification data obtained from marketplace server
     * @return True if the verification was processed successfully, otherwise false
     */
    virtual bool on_bundle_verified(const VerifiedBundleData& data) = 0;

    /**
     * @brief Notification that plugin bundle was downloaded successfully, and is ready for installation.
     * @param plugin_id An ID of downloaded plugin bundle
     * @param version A version of downloaded plugin bundle
     * @param zip_path Path to the downloaded plugin bundle.
     */
    virtual void on_bundle_downloaded(
        const std::string& plugin_id,
        const std::string& version,
        const std::string& zip_path
    ) = 0;
};

/**
 * @brief Client API for Marketplace server.
 */
class MarketplaceClient : public WithListeners<IMarketplaceClientListener> {
public:
    /**
     * @brief Initiate download and install workflow.
     * @param config Description of what should be installed
     */
    void download_bundle(const BundleRequestConfig& config);

private:
    void perform_verify_step(const BundleRequestConfig& config);
    void perform_download_step(const BundleRequestConfig& config);
    void report_error(const std::string& plugin_id, const std::string& message);

    boost::filesystem::path download_dir() const;
    boost::filesystem::path keys_dir() const;
};
}