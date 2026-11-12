///Vojtěch Král @vojtechkral

#pragma once

#include <boost/optional.hpp>
#include <semver.h>
#include <string>
#include <ostream>

namespace Slic3r {

class Semver
{
public:
    struct Major
    {
        const int i;
        Major(int i) : i(i) {}
    };
    struct Minor
    {
        const int i;
        Minor(int i) : i(i) {}
    };
    struct Patch
    {
        const int i;
        Patch(int i) : i(i) {}
    };

    Semver();

    Semver(
        int major,
        int minor,
        int patch,
        const boost::optional<const std::string&>& metadata,
        const boost::optional<const std::string&>& prerelease
    );

    Semver(
        int major, int minor, int patch, const char* metadata = nullptr, const char* prerelease = nullptr
    );

    Semver(const std::string& str);

    static boost::optional<Semver> parse(const std::string& str);

    static const Semver zero();

    static const Semver inf();

    static const Semver invalid();

    Semver(Semver&& other) noexcept;
    Semver(const Semver& other);

    Semver& operator=(Semver&& other) noexcept;

    Semver& operator=(const Semver& other);

    ~Semver();

    // const accessors
    int maj() const;
    int min() const;
    int patch() const;
    const char* prerelease() const;
    const char* metadata() const;

    // Setters
    void set_maj(int maj);
    void set_min(int min);
    void set_patch(int patch);
    void set_metadata(const boost::optional<const std::string&>& meta);

    void set_metadata(const char* meta);

    void set_prerelease(const boost::optional<const std::string&>& pre);

    void set_prerelease(const char* pre);

    // Comparison
    bool operator<(const Semver& b) const;
    bool operator<=(const Semver& b) const;
    bool operator==(const Semver& b) const;
    bool operator!=(const Semver& b) const;
    bool operator>=(const Semver& b) const;
    bool operator>(const Semver& b) const;
    // We're using '&' instead of the '~' operator here as '~' is unary-only:
    // Satisfies patch if Major and minor are equal.
    bool operator&(const Semver& b) const;
    bool operator^(const Semver& b) const;
    bool in_range(const Semver& low, const Semver& high) const;
    bool valid() const;

    // Conversion
    std::string to_string() const;

    // Arithmetics
    Semver& operator+=(const Major& b);
    Semver& operator+=(const Minor& b);
    Semver& operator+=(const Patch& b);
    Semver& operator-=(const Major& b);
    Semver& operator-=(const Minor& b);
    Semver& operator-=(const Patch& b);
    Semver operator+(const Major& b) const;
    Semver operator+(const Minor& b) const;
    Semver operator+(const Patch& b) const;
    Semver operator-(const Major& b) const;
    Semver operator-(const Minor& b) const;
    Semver operator-(const Patch& b) const;

    // Stream output
    friend std::ostream& operator<<(std::ostream& os, const Semver& self);

private:
    semver_t ver;

    Semver(semver_t ver);

    static semver_t semver_zero();
    static char* strdup(const std::string& str);
};

} // namespace Slic3r
