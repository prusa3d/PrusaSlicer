#pragma once

#include "Slic3r/App/Browser/AbstractBrowserLogic.hpp"

#include <string>
#include <utility>
#include <vector>

namespace Slic3r::App::Browser {

class AbstractUploadBrowserLogic : public AbstractBrowserLogic {
public:
    AbstractUploadBrowserLogic(
        const std::string& url,
        std::vector<std::string>&& message_handler_names,
        const std::string& loading_html = "other_loading",
        const std::string& error_html   = "other_error",
        const std::string& title        = "title"
    ) : 
        AbstractBrowserLogic(url, std::move(message_handler_names), loading_html, error_html, title)
    {}

    virtual ~AbstractUploadBrowserLogic() = default;

    std::string result_data() const { return m_result; }
    bool success() const { return m_success; }

protected:
    bool m_success {false};
    std::string m_result;
};

} // namespace Slic3r::App::Browser