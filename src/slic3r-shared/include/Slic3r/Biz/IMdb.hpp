#pragma once

#include <optional>
#include <string>

namespace Slic3r::Biz {

/**
 * @brief Interface for querying the Material Database for per-material colors.
 *
 * The real implementation is not yet available. Use NullMdb as a stub.
 */
class IMdb
{
public:
    virtual ~IMdb() = default;

    /**
     * @brief Look up a color for the given material UUID.
     * @param uuid Material UUID as provided by the MDB.
     * @return A hex color string (e.g. "#FF8000") if the material is known,
     *         or std::nullopt if the UUID is not found.
     */
    virtual std::optional<std::string> get_color(const std::string& uuid) const = 0;
};

/**
 * @brief Stub MDB implementation that always returns nullopt.
 * Used until the real MDB integration is available.
 */
class NullMdb : public IMdb
{
public:
    std::optional<std::string> get_color(const std::string& /*uuid*/) const override
    {
        return std::nullopt;
    }
};

} // namespace Slic3r::Biz
