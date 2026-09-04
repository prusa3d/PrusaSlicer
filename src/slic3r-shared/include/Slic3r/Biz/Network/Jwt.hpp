#pragma once

#include <string>

namespace Slic3r::Biz::Network::Jwt {

bool verify_exp(const std::string& token);
int get_exp_seconds(const std::string& token);

}