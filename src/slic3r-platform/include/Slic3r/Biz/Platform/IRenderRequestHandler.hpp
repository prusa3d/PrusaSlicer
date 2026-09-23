#pragma once

namespace Slic3r::Biz::Platform {

class IRenderRequestHandler {
public:
    virtual ~IRenderRequestHandler() = default;

    virtual void request_render() = 0;
};

class NullRenderRequestHandler final : public IRenderRequestHandler
{
public:
    void request_render() override {}
};

} // namespace Slic3r::Biz::Platform
