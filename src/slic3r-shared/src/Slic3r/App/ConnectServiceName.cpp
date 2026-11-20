#include "Slic3r/App/ConnectServiceName.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/Network/ServiceConfig.hpp"

namespace Slic3r::App {

std::string connect_service_name()
{
    if (Biz::Network::ServiceConfig::instance().is_connect_local()) {
        // TRN The self-hosted Prusa Connect Local server, wherever the Connect service is named.
        return Biz::_u8L("Prusa Connect on-premise");
    }
    return Biz::_u8L("Prusa Connect");
}

std::string account_service_name()
{
    if (Biz::Network::ServiceConfig::instance().is_connect_local()) {
        return connect_service_name();
    }
    return Biz::_u8L("Prusa Account");
}

} // namespace Slic3r::App
