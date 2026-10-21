#pragma once

#include "Slic3r/Biz/Platform/WithListeners.hpp"
#include "Slic3r/Domain/Types.hpp"

namespace Slic3r::Domain {
class Project;
} // namespace Slic3r::Domain

namespace Slic3r::Biz::Scene {
struct ObjectSelection;
} // namespace Slic3r::Biz::Scene

namespace Slic3r::App {

enum class ContextMenuType
{
    None,
    Scene, //???
    Bed,
    Object,
    MultiObjects,
    SvgOrText,
    Volume
};

class IShowContextMenuListener
{
public:
    virtual ~IShowContextMenuListener()                                                   = default;
    virtual void on_show_context_menu(ContextMenuType type, Domain::Vec2f mouse_position) = 0;
};

/**
 * @brief Source of context menu requests (e.g. the 3D scene or the object list). The menus
 * themselves are owned and shown by the IShowContextMenuListener.
 */
class ContextMenuRequester : public WithListeners<IShowContextMenuListener>
{
public:
    /**
     * @brief Asks the listeners to show the menu of @p type at @p mouse_position
     * (ContextMenuType::None closes all menus).
     */
    void request_context_menu(ContextMenuType type, Domain::Vec2f mouse_position);
};

/**
 * @return The context menu matching the object @p selection, ContextMenuType::None for an empty
 * selection.
 */
ContextMenuType
context_menu_type(const Biz::Scene::ObjectSelection& selection, const Domain::Project& project);

} // namespace Slic3r::App
