#pragma once

namespace Slic3r::App {

class ILanguageChangedListener
{
public:
    virtual ~ILanguageChangedListener() = default;
    virtual void on_language_changed() = 0;
};

} // Slic3r::Biz

