#pragma once

#include <boost/optional/optional.hpp>

#include <string>

namespace Slic3r::Biz::Network::Jwt {

bool verify_exp(const std::string& token);

/**
 * @brief The expiration (exp claim, unix time) of the token, none when it has none.
 */
boost::optional<double> get_exp(const std::string& token);

int get_exp_seconds(const std::string& token);

}