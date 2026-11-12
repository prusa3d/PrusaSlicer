#pragma once

#include <string>

namespace Slic3r::App::Render {
class IResourceResolver {
public:

    IResourceResolver() = default;
    virtual ~IResourceResolver() = default;
    IResourceResolver(const IResourceResolver& resolver) = delete;
    IResourceResolver& operator=(const IResourceResolver& resolver) = delete;

    /**
     * Resolves relative_filepath into absolute one.
     * @param relative_filepath of the file to be resolved
     * @return either absolute filepath or empty resolved string
     */
    virtual std::string resolve(const std::string& relative_filepath) = 0;
};

}
