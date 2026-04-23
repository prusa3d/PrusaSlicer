#ifndef LIBSLIC3R_SLA_WORKFLOWS_HPP
#define LIBSLIC3R_SLA_WORKFLOWS_HPP

#include <string>
#include <vector>
#include <utility>
#include <memory>
#include <thread>
#include <functional>
#include <optional>

#include "libslic3r/Semver.hpp"
#include "nlohmann/json_fwd.hpp"

namespace Slic3r {
namespace sla {

struct Workflow
{
    std::string uuid;
    std::string name;
    bool is_default = false;
};

class WorkflowManager
{
    std::unique_ptr<nlohmann::json> m_data;
    std::thread m_thread;

public:
    explicit WorkflowManager();
    ~WorkflowManager();

    void fetch_workflows_file_online_in_background(std::function<std::optional<std::string>(const std::string&)>, std::function<void(Semver)>);
    std::vector<Workflow> workflows_for_material_sorted(const std::string &material_uuid) const;
};

} // namespace sla
} // namespace Slic3r

#endif // LIBSLIC3R_SLA_WORKFLOWS_HPP
