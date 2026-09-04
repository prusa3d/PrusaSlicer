#include "Slic3r/Biz/UserAccount/UserAccountCodeChallengeGenerator.hpp"

#include "Slic3r/Biz/SHA256.hpp"
#include "Slic3r/Log.hpp"

#include <string>
#include <random>
#include <algorithm>
#include <stdexcept>
#include <boost/beast/core/detail/base64.hpp>

namespace Slic3r::Biz::UserAccount {

std::string UserAccountCodeChallengeGenerator::generate_challenge(const std::string& verifier) const
{
    std::string code_challenge;
    try
    {
        code_challenge = sha256(verifier);
        code_challenge = base64_encode(code_challenge);
    }
    catch (const std::exception& e)
    {
        SPDLOG_ERROR("Code Challenge Generator failed: {}", e.what());
    }
    assert(!code_challenge.empty());
    return code_challenge;
    
}

std::string UserAccountCodeChallengeGenerator::generate_verifier() const
{
    size_t length = 40;
    std::string code_verifier = generate_code_verifier(length);
    assert(code_verifier.size() == length);
    return code_verifier;
}
std::string UserAccountCodeChallengeGenerator::base64_encode(const std::string& input) const
{
    std::string output;
    output.resize(boost::beast::detail::base64::encoded_size(input.size()));
    boost::beast::detail::base64::encode(&output[0], input.data(), input.size());
    // save encode - replace + and / with - and _
    std::replace(output.begin(), output.end(), '+', '-');
    std::replace(output.begin(), output.end(), '/', '_');
    // remove last '=' sign 
    while (output.back() == '=')
        output.pop_back();
    return output;
}

std::string UserAccountCodeChallengeGenerator::generate_code_verifier(size_t length) const
{
    const std::string                   chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    std::random_device                  rd;
    std::mt19937                        gen(rd());
    std::uniform_int_distribution<int>  distribution(0, chars.size() - 1);
    std::string                         code_verifier;
    for (size_t i = 0; i < length; ++i) {
        code_verifier += chars[distribution(gen)];
    }
    return code_verifier;
}

} //namespace Slic3r::Biz::UserAccount