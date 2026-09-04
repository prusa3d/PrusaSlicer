#pragma once

#include <memory>
#include "Slic3r/Biz/libpgcode/ProcessorResult.hpp"
#include "libslic3r/GeneratedSupportPoints.hpp"
#include "libslic3r/IPrint.hpp"
#include "libslic3r/WipeTowerGeometry.hpp"
#include "libslic3r/SLAResult.hpp"

namespace Slic3r::Biz::Slicing {

using FDMResult = libpgcode::ProcessorResult;

class IProcessCallbacks {
public:
    virtual void on_fdm_result(FDMResult &&, Domain::SlicingId) = 0;
    virtual void on_sla_result(const Domain::SlicingId&, SLAResult&&) = 0;
    virtual void on_sla_object(const Domain::SlicingId&, Sla::Object&&) = 0;
    virtual void on_status(const StatusUpdate, Domain::SlicingId) = 0;
    virtual void on_exception(std::exception_ptr exception, Domain::SlicingId) = 0;
    virtual void on_wipe_tower_geometry(OptWipeTowerGeometry&&, Domain::SlicingId) = 0;
    virtual void on_extruder_candidates(std::vector<unsigned>&& extruder_candidates, Domain::SlicingId) = 0;
    virtual void
    on_generated_support_points(GeneratedSupportPointsSnapshot&&, Domain::SlicingId) = 0;
    virtual StatusCode get_status(const Domain::SlicingId) const = 0;
    virtual ~IProcessCallbacks() = default;
};

std::unique_ptr<IPrint> init_print(const Domain::PrinterTechnology& printer_technology,
                                   IProcessCallbacks& callbacks,
                                   const Domain::SlicingId id);
} // namespace Slic3r::Biz::Slicing
