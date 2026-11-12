#pragma once

namespace Slic3r::Biz::Preset {

/// Notified whenever the macro overrides of the selected preset were recomputed.
class IMacroOverridesChangedListener
{
public:
    virtual ~IMacroOverridesChangedListener() = default;

    virtual void on_macro_overrides_changed() = 0;
};

} // namespace Slic3r::Biz::Preset
