#include "Slic3r/Biz/Format/ResultLoad3mf.hpp"
#include <magic_enum/magic_enum.hpp>


template <> struct magic_enum::customize::enum_range<Slic3r::Read3mfIssueType> {
    static constexpr int max = 473; // 473 unique value to be full text searchable when exceed
};

namespace Slic3r {



void Read3mfIssues::add_issue(const Read3mfIssue& issue)
{
    auto it = std::find_if(issues.begin(), issues.end(), [&issue](const auto& is) { return is.type == issue.type; });
    if (it == issues.end())
        issues.emplace_back(issue);
    else
        it->sources.insert(it->sources.end(), issue.sources.begin(), issue.sources.end());
}

bool Read3mfIssues::has_issue(Read3mfIssueType type) const
{
    return std::find_if(issues.begin(), issues.end(),
        [&type](const auto& is) { return is.type == type; }
    ) != issues.end();
}

Read3mfIssue::Read3mfIssue(
        Read3mfIssueType type,
        std::optional<std::string> msg, 
        std::optional<std::string> source_str,
        std::optional<int> source_line
    )
{
    this->type = type;
    this->msg = (msg.has_value() ? *msg : std::string(magic_enum::enum_name(type)));

    if (source_str.has_value() || source_line.has_value()) {
        this->sources.emplace_back();
        this->sources.back().first = (source_str.has_value() ? *source_str : "");
        this->sources.back().second = (source_line.has_value() ? *source_line : -1);
    }
}

} // namespace Slic3r
