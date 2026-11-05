#pragma once

#include <optional>
#include <string>

namespace Slic3r::App::Yoga {

struct DialogGeometry
{
    float left{};
    float top{};
    float width{};
    float height{};
    bool maximized{false};

    bool operator==(const DialogGeometry&) const = default;
};

/**
 * @brief Persistent storage of pinned Dialog geometry keyed by dialog object name
 */
class IDialogGeometryStore
{
public:
    virtual ~IDialogGeometryStore() = default;

    virtual std::optional<DialogGeometry> dialog_geometry(const std::string& name) const = 0;
    virtual void set_dialog_geometry(const std::string& name, const DialogGeometry& geometry) = 0;
    virtual void remove_dialog_geometry(const std::string& name) = 0;
};

} // namespace Slic3r::App::Yoga
