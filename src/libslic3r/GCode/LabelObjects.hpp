#ifndef slic3r_GCode_LabelObjects_hpp_
#define slic3r_GCode_LabelObjects_hpp_

#include <string>
#include <vector>

#include "libslic3r/Print.hpp"

namespace Slic3r {

enum GCodeFlavor : unsigned char;
enum class LabelObjectsStyle;
struct PrintInstance;
class Print;
class GCodeWriter;

namespace GCode {

class LabelObjects
{
public:
    void init(const SpanOfConstPtrs<PrintObject>& objects, LabelObjectsStyle label_object_style, GCodeFlavor gcode_flavor);
    std::string all_objects_header() const;
    std::string all_objects_header_singleline_json() const;

    bool update(const PrintInstance *instance);

    std::string maybe_start_instance(GCodeWriter& writer);

    std::string maybe_stop_instance();

    std::string maybe_change_instance(GCodeWriter& writer);

    bool has_active_instance();

private:
    struct LabelData
    {
        const PrintInstance* pi;
        std::string name;
        std::string center;
        std::string polygon;
        int unique_id;
    };

    enum class IncludeName {
        No,
        Yes
    };

    std::string start_object(const PrintInstance& print_instance, IncludeName include_name) const;
    std::string stop_object(const PrintInstance& print_instance) const;

    const PrintInstance* current_instance{nullptr};
    const PrintInstance* last_operation_instance{nullptr};

    LabelObjectsStyle m_label_objects_style;
    GCodeFlavor       m_flavor;
    std::vector<LabelData> m_label_data;
};
} // namespace GCode
} // namespace Slic3r

#endif // slic3r_GCode_LabelObjects_hpp_
