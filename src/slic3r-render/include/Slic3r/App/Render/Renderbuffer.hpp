#pragma once

#include <vector>
#include "WithInternal.hpp"
#include "Slic3r/Domain/PixelFormat.hpp"

namespace Slic3r::App::Render {

class Renderbuffer : public WithInternal
{
public:
    Renderbuffer(Domain::PixelFormat format);
    ~Renderbuffer() override;

    Domain::PixelFormat format() const { return m_format; }

protected:
    Domain::PixelFormat m_format;
};

using RenderbufferPtr = std::unique_ptr<Renderbuffer>;
using RenderbufferPtrs = std::vector<RenderbufferPtr>;

} // namespace Slic3r::App::Render
