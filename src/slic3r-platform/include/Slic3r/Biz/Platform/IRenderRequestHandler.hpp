#pragma once

namespace Slic3r::Biz::Platform {

class IRenderRequestHandler {
public:
    virtual ~IRenderRequestHandler() = default;

    virtual void request_render() = 0;
};

} // namespace Slic3r::Biz::Platform
