#include "Jwt.hpp"

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/beast/core/detail/base64.hpp> // IWYU pragma: keep
#include <boost/optional/optional.hpp>
#include <ctime>
#include <sstream>
#include <tuple>
#include <algorithm>
#include <chrono>

namespace Slic3r::Utils {

namespace {
boost::optional<double> get_exp(const std::string& token)
{
    size_t payload_start = token.find('.');
    if (payload_start == std::string::npos)
        return boost::none;
    payload_start += 1; // payload starts after dot

    const size_t payload_end = token.find('.', payload_start);
    if (payload_end == std::string::npos)
        return boost::none;

    size_t encoded_length = payload_end - payload_start;
    size_t decoded_length = boost::beast::detail::base64::decoded_size(encoded_length);

    auto json_b64 = token.substr(payload_start, encoded_length);
    std::replace(json_b64.begin(), json_b64.end(), '-', '+');
    std::replace(json_b64.begin(), json_b64.end(), '_', '/');

    size_t padding = encoded_length % 4;
    encoded_length += padding;
    while (padding--) json_b64 += '=';


    std::string json;
    json.resize(decoded_length + 2);
    size_t read_bytes, written_bytes;
    std::tie(written_bytes, read_bytes) = boost::beast::detail::base64::decode(json.data(), json_b64.data(), json_b64.length());
    json.resize(written_bytes);
    if (written_bytes == 0)
        return boost::none;

    namespace pt = boost::property_tree;

    pt::ptree payload;
    std::istringstream iss(json);
    pt::json_parser::read_json(iss, payload);

    return payload.get_optional<double>("exp");
}
}

int get_exp_seconds(const std::string& token)
{
    auto exp_opt = get_exp(token);
    if (!exp_opt)
        return 0;
    auto now = std::chrono::system_clock::now();
    auto now_in_seconds = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    double remaining_time = *exp_opt - now_in_seconds;
    return (int)remaining_time;
}

bool verify_exp(const std::string& token)
{
    auto exp_opt = get_exp(token);
    if (!exp_opt)
        return false;
    auto now = time(nullptr);
    return exp_opt.get() > now;
}

}
