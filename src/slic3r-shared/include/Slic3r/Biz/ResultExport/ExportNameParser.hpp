#include "Slic3r/Biz/Parser/IO.hpp"
#include "Slic3r/Biz/FDMResultCache.hpp"
#include "Slic3r/Biz/SLAResultCache.hpp"

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::Biz::ExportNameParser {

enum class Technology
{
    Fdm,
    Sla
};

struct ExportNameData
{
    Technology technology;
    std::string filename;
    std::string preferred_extension;
};
    
/**
 * Generates export filename data based on the active printer technology.
 * @throw Slic3r::PlaceholderParserError If the output filename format string is invalid.
 */
ExportNameData parse_export_name(const Biz::ProjectInteractor& project_interactor);

/**
 * Generates export filename data without parsing output filename format.
 * Used when parsing fails.
 */
ExportNameData error_state_export_name(const Biz::ProjectInteractor& project_interactor);


} // namespace Slic3r::Biz::ExportNameParser
