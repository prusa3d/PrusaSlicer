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

class IMarketplaceClientListener
{
public:
    virtual ~IMarketplaceClientListener() = default;

    virtual void on_download_error(const std::string& plugin_id, const std::string& message) = 0;
    virtual void on_bundle_verified(const VerifiedBundleData& data) = 0;
    virtual void on_bundle_downloaded(const std::string& plugin_id, const std::string& version, const std::string& zip_path) = 0;
};

class MarketplaceClient : public WithListeners<IMarketplaceClientListener> {
public:
    void download_bundle(const BundleRequestConfig& config);

private:
    void perform_verify_step(const BundleRequestConfig& config);
    void perform_download_step(const BundleRequestConfig& config);
    void report_error(const std::string& plugin_id, const std::string& message);

    boost::filesystem::path download_dir() const;
    boost::filesystem::path keys_dir() const;
};
}