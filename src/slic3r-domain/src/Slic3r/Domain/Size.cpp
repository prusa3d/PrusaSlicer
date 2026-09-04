#include "Slic3r/Domain/Size.hpp"

namespace Slic3r::Domain {
void Size::scale(const Size& scale_to, ScaleMode mode)
{
    switch (mode) {
    case ScaleMode::IgnoreAspectRatio:
        width  = scale_to.width;
        height = scale_to.height;
        break;
    case ScaleMode::KeepAspectRatio: {
        auto scaled_width = scale_to.height * width / height;
        bool use_height     = scaled_width <= scale_to.width;

        if (use_height) {
            width  = scaled_width;
            height = scale_to.height;
        } else {
            height = scale_to.width * height / width;
            width  = scale_to.width;
        }
    } break;
    }
}

Size Size::scaled(const Size& scale_to, ScaleMode mode) const
{
    Size scaled = *this;
    scaled.scale(scale_to, mode);
    return scaled;
}

Size::Size(int w, int h) : width(w), height(h) {}

Size::Size(const Size& size) : Size(size.width, size.height) {}

Size& Size::operator=(const Size& size)
{
    width  = size.width;
    height = size.height;

    return *this;
}

bool Size::operator==(const Size& other) const
{
    return width == other.width && height == other.height;
}

bool Size::operator!=(const Size& other) const
{
    return !(*this == other);
}

int Size::space() const
{
    return width * height;
}
} // namespace Slic3r::Domain
