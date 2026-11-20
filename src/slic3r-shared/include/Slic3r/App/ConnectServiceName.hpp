#pragma once

#include <string>

namespace Slic3r::App {

/**
 * @brief The translated name of the Connect service the app uses: "Prusa Connect" or, with a
 * Connect Local server, "Prusa Connect on-premise". For texts naming the service.
 */
std::string connect_service_name();

/**
 * @brief The translated name of the account the app signs in to: "Prusa Account" or, with a
 * Connect Local server, "Prusa Connect on-premise".
 */
std::string account_service_name();

} // namespace Slic3r::App
