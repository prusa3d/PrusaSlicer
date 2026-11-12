#ifndef slic3r_GCode_PostProcessor_hpp_
#define slic3r_GCode_PostProcessor_hpp_

#include "libslic3r/ConfigViews.hpp"
#include "libslic3r/Extruder.hpp"
#include "libslic3r/libslic3r.h"
#include "libslic3r/PrintBase.hpp"
#include "Slic3r/Biz/libpgcode/ProcessorResult.hpp"

#include <functional>

namespace Slic3r::Biz::libpgcode {
struct ProcessorResult;
struct PostProcessorConfig;
} // namespace Slic3r::Biz::libpgcode

namespace Slic3r {
namespace GCode {

typedef std::function<void(Biz::Slicing::Warning)> WarningCallback;

extern Slic3r::Biz::libpgcode::ProcessorResult post_process(
    const Slic3r::Biz::libpgcode::PostProcessorConfig& config,
    Slic3r::Biz::libpgcode::ProcessorResult&& result, 
    const std::vector<Extruder>& extruders,
    const Domain::ExtraPrintStatistics& extra_print_statistics,
    WarningCallback warning_callback
);

} // namespace GCode

// Run post processing script / scripts if defined.
// Returns true if a post-processing script was executed.
// Returns false if no post-processing script was defined.
// Throws an exception on error.
// host is one of "File", "PrusaLink", "Repetier", "SL1Host", "OctoPrint", "FlashAir", "Duet", "AstroBox" ...
// If make_copy, then a temp file will be created for src_path by adding a ".pp" suffix and src_path will be updated.
// In that case the caller is responsible to delete the temp file created.
// output_name is the final name of the G-code on SD card or when uploaded to PrusaLink or OctoPrint.
// If uploading to PrusaLink or OctoPrint, then the file will be renamed to output_name first on the target host.
// The post-processing script may change the output_name.
extern bool run_post_process_scripts(std::string &src_path, bool make_copy, const std::string &host, std::string &output_name, const PrintConfigView &config);

inline bool run_post_process_scripts(std::string &src_path, const PrintConfigView &config)
{
	std::string src_path_name = src_path;
	return run_post_process_scripts(src_path, false, "File", src_path_name, config);
}

} // namespace Slic3r

#endif /* slic3r_GCode_PostProcessor_hpp_ */
