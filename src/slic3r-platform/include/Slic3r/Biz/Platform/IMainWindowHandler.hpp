#pragma once

namespace Slic3r::Biz::Platform {

/**
 * @brief Interface for handling platform-specific window operations
 */
class IMainWindowHandler
{
public:
    virtual ~IMainWindowHandler() = default;

    virtual bool has_fullscreen() const  = 0;
    virtual bool is_fullscreen() const   = 0;
    virtual void set_fullscreen(bool on) = 0;

    virtual void close_application() = 0;
};

} // namespace Slic3r::Biz::Platform
