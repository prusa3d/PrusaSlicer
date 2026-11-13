#pragma once

#include "Slic3r/App/Yoga/ListView.hpp"
#include "Slic3r/App/Yoga/Rectangle.hpp"
#include "Slic3r/Biz/ObservableList.hpp"

#include <string>
#include <vector>

namespace Slic3r::Domain {
struct FullPrintStatistics;
} // namespace Slic3r::Domain

namespace Slic3r::Domain::SLA {
struct PrintStatistics;
} // namespace Slic3r::Domain::SLA

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::App {

namespace Yoga {
class Text;
class ToggleButton;
class AbstractButton;
class ScrollArea;
} // namespace Yoga

namespace ObjectList {

/**
 * @brief Panel with the estimates (used material, printing time, ...) of the sliced selected bed,
 * in a summary or a detail view.
 */
class BedSlicedInfo : public Yoga::Rectangle
{
public:
    explicit BedSlicedInfo(Biz::ProjectInteractor& project_interactor);

    /**
     * @brief Shows the estimates of the selected bed, hides the panel if the bed isn't sliced.
     */
    void update();

private:
    /**
     * @brief Row showing a used material amount. The weight (or volume for SLA) and the length
     * have separate columns, aligned with the per extruder table. The length is empty where it
     * doesn't apply (resin volume, spool weight).
     */
    struct MaterialRow
    {
        Yoga::Item* row{nullptr};
        Yoga::Text* weight{nullptr};
        Yoga::Text* length{nullptr};
    };

    /**
     * @brief Data of one row of the per extruder table in the detail view, the values are
     * already formatted.
     */
    struct ExtruderRowData
    {
        ImColor color;
        size_t extruder_index{0}; ///< 1-based, shown in the badge.
        std::string used;
        std::string length;
        std::string with_spool;
        bool with_spool_visible{false};
    };

    /**
     * @brief One row of the per extruder table, shows the ExtruderRowData.
     */
    class ExtruderRowView;
    using ExtruderListView = Yoga::ListView<ExtruderRowView, ExtruderRowData>;

    static void set_used_material(MaterialRow& material_row, float weight_g, float length_mm);

    void update_fdm(
        const Domain::FullPrintStatistics& print_statistics,
        const std::vector<std::string>& tool_colors
    );
    void update_sla(const Domain::SLA::PrintStatistics& print_statistics);

    /**
     * @brief Fills the per extruder table, one row for each used tool.
     * @param with_spool_visible Shows the "with spool" column of the rows.
     */
    void update_extruder_rows(
        const Domain::FullPrintStatistics& print_statistics,
        const std::vector<size_t>& used_tools,
        const std::vector<std::string>& tool_colors,
        const std::vector<float>& spool_weights,
        bool with_spool_visible
    );

    Biz::ProjectInteractor& m_project_interactor;

    Yoga::ToggleButton* m_detail_view_toggle{nullptr};
    /// Everything below the header, scrolls when the panel doesn't get enough height.
    Yoga::ScrollArea* m_content{nullptr};

    MaterialRow m_used_material_total;
    MaterialRow m_used_material_for_objects;
    MaterialRow m_used_material_for_wipe_tower;
    MaterialRow m_used_material_for_flush;
    MaterialRow m_used_material_for_supports;
    MaterialRow m_used_material_with_spool;

    Yoga::Item* m_per_extruder_header_row{nullptr};
    Yoga::AbstractButton* m_per_extruder_with_spool_header{nullptr};
    Biz::UnsharedPointer<Biz::ObservableList<ExtruderRowData>> m_extruder_rows;
    ExtruderListView* m_per_extruder_table{nullptr};

    Yoga::Text* m_material_cost{nullptr};
    Yoga::Text* m_first_layer_time{nullptr};
    Yoga::Text* m_estimated_time{nullptr};
    Yoga::Text* m_estimated_stealth_time{nullptr};
    Yoga::Text* m_toolchanges{nullptr};

    Yoga::Item* m_material_cost_row{nullptr};
    Yoga::Item* m_first_layer_time_row{nullptr};
    Yoga::Item* m_estimated_stealth_time_row{nullptr};
    Yoga::Item* m_toolchanges_row{nullptr};
};

} // namespace ObjectList
} // namespace Slic3r::App
