#include "Jwt.hpp"

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/beast/core/detail/base64.hpp> // IWYU pragma: keep
#include <boost/optional/optional.hpp>
#include <ctime>
#include <sstream>
#include <tuple>
#include <algorithm>

namespace Slic3r::Utils {

bool verify_exp(const std::string& token)
{
    size_t payload_start = token.find('.');
    if (payload_start == std::string::npos)
        return false;
    payload_start += 1; // payload starts after dot

    const size_t payload_end = token.find('.', payload_start);
    if (payload_end == std::string::npos)
        return false;

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
        return false;

    namespace pt = boost::property_tree;

    pt::ptree payload;
    std::istringstream iss(json);
    pt::json_parser::read_json(iss, payload);

    auto exp_opt = payload.get_optional<double>("exp");
    if (!exp_opt)
        return false;

    auto now = time(nullptr);
    return exp_opt.get() > now;
}

}
