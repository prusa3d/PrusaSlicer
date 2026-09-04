#pragma once

namespace Slic3r::App {

class IMenuUpdatedListener
{
public:
    virtual ~IMenuUpdatedListener() = default;
    virtual void on_menu_updated()  = 0;
};

} // namespace Slic3r::App
