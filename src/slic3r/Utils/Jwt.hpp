#pragma once

#include <string>

namespace Slic3r::Utils {

bool verify_exp(const std::string& token);
int get_exp_seconds(const std::string& token);

}
