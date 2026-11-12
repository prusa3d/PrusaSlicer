#pragma once

#include "Slic3r/Domain/SlicingId.hpp"

namespace Slic3r::Biz::Slicing {

class ISlicingListener
{
public:
    virtual ~ISlicingListener() = default;
};

struct IBackgroundProcessStateListener : ISlicingListener
{
    virtual void on_background_process_state_changed(const Domain::SlicingId) = 0;
};

} // namespace Slic3r::Biz::Slicing
