#pragma once

#include <vector>

namespace Slic3r::Domain {

struct Size
{
    enum class ScaleMode
    {
        IgnoreAspectRatio,
        KeepAspectRatio, // Fit Inside
    };

    Size() = default;
    Size(int width, int height);
    Size(const Size& size);
    Size& operator=(const Size& size);

    int width  = 0;
    int height = 0;

    bool operator==(const Size& other) const;
    bool operator!=(const Size& other) const;

    void scale(const Size& scale_to, ScaleMode mode = ScaleMode::KeepAspectRatio);
    Size scaled(const Size& scale_to, ScaleMode mode = ScaleMode::KeepAspectRatio) const;
    //! \return space taken by this size (width * height)
    int space() const;
};

using Sizes = std::vector<Size>;

} // namespace Slic3r::Domain
