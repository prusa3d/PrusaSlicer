#pragma once

#include <string>

namespace Slic3r::Biz::Connect {

class IConnectHandlerListener 
{
public:
    virtual ~IConnectHandlerListener() = default;
    virtual void on_connect_handler_error(const std::string& msg) {}
};
} // namespace Slic3r::Biz::Connect