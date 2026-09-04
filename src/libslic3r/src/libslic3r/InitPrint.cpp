#include "libslic3r/InitPrint.hpp"
#include "libslic3r/PrintBase.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/SLAPrint.hpp"

namespace Slic3r::Biz::Slicing {

using Slic3r::Print;

std::unique_ptr<IPrint> init_print(const Domain::PrinterTechnology& printer_technology,
                                   IProcessCallbacks& callbacks,
                                   const Domain::SlicingId id)
{
    std::unique_ptr<PrintBase> print;
    std::reference_wrapper<IProcessCallbacks> callbacks_ref{callbacks};
    switch (printer_technology) {
    case Domain::PrinterTechnology::FFF: {
        Print::OnFdmResult on_fdm_result = [callbacks_ref, id](FDMResult&& result)
        { callbacks_ref.get().on_fdm_result(std::move(result), id); };
        Print::OnWipeTowerGeometry on_wipe_tower_geometry =
            [callbacks_ref, id](OptWipeTowerGeometry&& geometry)
        { callbacks_ref.get().on_wipe_tower_geometry(std::move(geometry), id); };
        Print::OnExtruderCandidates on_extruder_candidates =
            [callbacks_ref, id](std::vector<unsigned> extruder_candidates)
        { callbacks_ref.get().on_extruder_candidates(std::move(extruder_candidates), id); };
        Print::OnGeneratedSupportPoints on_generated_support_points =
            [callbacks_ref, id](GeneratedSupportPointsSnapshot&& support_points)
        { callbacks_ref.get().on_generated_support_points(std::move(support_points), id); };
        print = std::make_unique<Print>(
            on_fdm_result,
            on_wipe_tower_geometry,
            on_extruder_candidates,
            on_generated_support_points
        );
        break;
    }
    case Domain::PrinterTechnology::SLA: {
        SLAPrint::OnSlaResult on_sla_result = [callbacks_ref, id](SLAResult&& result)
        { callbacks_ref.get().on_sla_result(id, std::move(result)); };
        SLAPrint::OnSlaObject on_sla_object = [callbacks_ref, id](const Sla::Object& object)
        {
            auto object_copy = object;
            callbacks_ref.get().on_sla_object(id, std::move(object_copy));
        };
        print = std::make_unique<SLAPrint>(on_sla_result, on_sla_object);
        break;
    }
    default:
        UNREACHABLE("Only FFF and SLA are viable options!");
    }
    callbacks_ref.get().on_wipe_tower_geometry(std::nullopt, id);
    return print;
}
} // namespace Slic3r::Biz::Slicing
