#pragma once

#include "Slic3r/Domain/BedRef.hpp"

namespace Slic3r::Biz {

class ISlicingInputChangedListener {
public:
    virtual ~ISlicingInputChangedListener() = default;
    virtual void on_slicing_input_changed(const Domain::BedRef& bed_instance) = 0;
    virtual void on_slicing_input_removed(const Domain::BedRef& bed_instance) = 0;
};

}
