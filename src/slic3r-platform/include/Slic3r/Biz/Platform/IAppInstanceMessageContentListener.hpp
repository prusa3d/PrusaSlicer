#pragma once

#include <vector>
#include <string>
#include <boost/filesystem.hpp>

namespace Slic3r::Biz::Platform {

class IAppInstanceMessageContentListener
{
public:
    virtual ~IAppInstanceMessageContentListener() = default;

    virtual void on_open_models(std::vector<boost::filesystem::path> message) {}

    virtual void on_download_models(std::vector<std::string> message) {}

    virtual void on_read_token_store_message() {}

    virtual void on_login_data(const std::string& message) {}

    virtual void on_app_go_front() {}

    virtual void on_backup_id_requested() {}

    virtual void on_backup_id_provided(const std::string& id) {}
};

} // namespace Slic3r::Biz::Platform
