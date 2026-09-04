#include "Slic3r/App/Scene/BedError.hpp"

#include <imgui/imgui.h>

#include <algorithm>

namespace Slic3r::App::Scene {

bool BedError::add_bed_instance(const Domain::SlicingId& id)
{
    if (!contains(id)) {
        m_bed_instances.push_back(id);
        return true;
    }
    return false;
}

bool BedError::remove_bed_instance(const Domain::SlicingId& id)
{
    auto it = std::find(m_bed_instances.begin(), m_bed_instances.end(), id);
    if (it != m_bed_instances.end()) {
        m_bed_instances.erase(it);
        return true;
    }
    return false;
}

bool BedError::contains(const Domain::SlicingId& id) const
{
    return std::find(m_bed_instances.begin(), m_bed_instances.end(), id) != m_bed_instances.end();
}

#if ENABLE_DEBUG_BED_ERROR
void render_imgui_debug_bed_error(const BedError& bed_error)
{
    ImGui::SetNextWindowCollapsed(true, ImGuiCond_Once);
    if (ImGui::Begin("Bed error debug", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const std::vector<Domain::SlicingId>& bed_instances = bed_error.bed_instances();
        if (bed_instances.empty())
            ImGui::Text("None");
        else {
            if (ImGui::BeginTable("Bed instances", 2, ImGuiTableFlags_Borders)) {
                for (const auto& b : bed_instances) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("Bed instance");
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%zu, %zu", b.project_id, b.bed_instance_id);
                }
                ImGui::EndTable();
            }
        }
    }
    ImGui::End();
}
#endif // ENABLE_DEBUG_BED_ERROR

} // namespace Slic3r::App::Scene

