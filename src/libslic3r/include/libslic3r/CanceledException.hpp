#pragma once

#include <exception>

namespace Slic3r::Biz::Slicing {

class CanceledException : public std::exception
{
public:
    const char* what() const throw()
    {
        return "Background processing has been canceled";
    }
};

} // namespace Slic3r::Biz::Slicing
