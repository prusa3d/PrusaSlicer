#include "libslic3r/SlicingStatus.hpp"
#include <boost/algorithm/string/join.hpp>
#include <span>

namespace Slic3r::Biz::Slicing {

std::string join(const std::vector<std::string>& texts, const std::string& delimiter)
{
    if (texts.empty()) {
        return {};
    }

    std::string result{texts.front()};
    for (const std::string& text : std::span{texts}.subspan(1)) {
        result += delimiter;
        result += text;
    }
    return result;
}

std::ostream& operator<<(std::ostream& output, const Error& error)
{
    output << "(";
    output << int(error.code);
    output << ", [";
    output << join(error.item_keys, ", ");
    output << "])";
    return output;
}

std::ostream& operator<<(std::ostream& output, const Progress& progress)
{
    output << "(";
    output << progress.progress.value;
    output << "%, ";
    output << int(progress.progress_info);
    output << ")";
    return output;
}

std::ostream& operator<<(std::ostream& output, const StatusCode& status_code)
{
    switch (status_code) {
    case StatusCode::Empty:
        return output << "Empty";
    case StatusCode::Updating:
        return output << "Updating";
    case StatusCode::Running:
        return output << "Running";
    case StatusCode::Finished:
        return output << "Finished";
    case StatusCode::Modified:
        return output << "Modified";
    case StatusCode::Stopping:
        return output << "Stopping";
    case StatusCode::Removed:
        return output << "Removed";
    case StatusCode::InvalidData:
        return output << "Invalid data";
    default:
        return output << "Unknown";
    }
}

std::ostream& operator<<(std::ostream& output, const StatusUpdate& status)
{
    output << "{code: ";
    if (status.code) {
        output << *status.code;
    }
    output << ", clear_errors: " << status.clear_errors << ", ";
    output << ", errors_to_append: [";

    std::vector<std::string> error_codes;
    for (const Biz::Slicing::Error& error : status.errors_to_append) {
        error_codes.push_back(std::to_string(static_cast<int>(error.code)));
    }

    output << boost::algorithm::join(error_codes, ",");
    output << "], clear_warnings: " << status.clear_warnings << ", ";
    output << "warnings_to_append: [";

    std::vector<std::string> warning_codes;
    for (const Biz::Slicing::Warning& warning : status.warnings_to_append) {
        warning_codes.push_back(std::to_string(static_cast<int>(warning.code)));
    }

    output << boost::algorithm::join(warning_codes, ",");
    output << "], clear_progress: ";
    output << status.clear_progress;
    output << ", progress: ";
    if (status.progress) {
        output << *status.progress;
    }
    output << "}";
    return output;
}
} // namespace Slic3r::Biz::Slicing
