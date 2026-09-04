#pragma once

#include <string>

namespace Slic3r::Biz::UserAccount {
class UserAccountCodeChallengeGenerator
{
public:
    UserAccountCodeChallengeGenerator() = default;
    ~UserAccountCodeChallengeGenerator() = default;

    std::string generate_challenge(const std::string& verifier) const;
    std::string generate_verifier() const;
private:
    std::string generate_code_verifier(size_t length) const;
    std::string base64_encode(const std::string& input) const;
};
}