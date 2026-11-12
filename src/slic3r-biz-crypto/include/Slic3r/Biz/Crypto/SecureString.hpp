#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace Slic3r::Biz::Crypto {

// Guaranteed-not-elided memory wipe. Defined per-platform in SecureString.cpp.
void secure_wipe(void* data, std::size_t size) noexcept;

class SecureString
{
public:
    SecureString() = default;

    // Takes ownership. The source is wiped and cleared, so no copy survives
    // even when SSO kicks in and the move degrades to a byte copy.
    explicit SecureString(std::string&& str) noexcept;

    // Builds in place with an exact reservation - no reallocation, so no
    // intermediate buffer is ever freed unwiped.
    SecureString(const char* data, std::size_t size);

    ~SecureString()
    {
        dispose_string(m_str);
    }

    SecureString(SecureString&& other) noexcept;
    SecureString& operator=(SecureString&& other) noexcept;

    SecureString(const SecureString&)            = delete;
    SecureString& operator=(const SecureString&) = delete;

    // string_view rather than const std::string&: a caller writing
    // `auto s = key.str();` would silently make an unprotected heap copy.
    std::string_view view() const& noexcept
    {
        return m_str;
    }

    std::string_view view() const&& = delete; // no views into a temporary

    const char* data() const noexcept
    {
        return m_str.data();
    }

    std::size_t size() const noexcept
    {
        return m_str.size();
    }

    bool empty() const noexcept
    {
        return m_str.empty();
    }

    void clear() noexcept
    {
        dispose_string(m_str);
    }

    std::string to_unprotected_string() const
    {
        return std::string{m_str.data(), m_str.size()};
    }

private:
    static void dispose_string(std::string& str) noexcept;

private:
    std::string m_str;
};

} // namespace Slic3r::Biz::Crypto
