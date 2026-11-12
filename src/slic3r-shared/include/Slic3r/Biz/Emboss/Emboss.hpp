#ifndef slic3r_Emboss_hpp_
#define slic3r_Emboss_hpp_

#include <admesh/stl.h> // indexed_triangle_set
#include <stdint.h>
#include <vector>
#include <optional>
#include <memory>
#include <functional>
#include <map>
#include <string>
#include <utility>

#include "Slic3r/Domain/FontFile.hpp"
#include "Slic3r/Domain/ExPolygon.hpp" // also Polygon and Points
#include "Slic3r/Domain/EmbossShape.hpp" // ExPolygonsWithIds, HealedExpolygons
#include "Slic3r/Domain/BoundingBox.hpp"
#include "Slic3r/Domain/ModelObject.hpp"
#include "Slic3r/Domain/ModelVolume.hpp"
#include "Slic3r/Domain/TextConfiguration.hpp"
#include "Slic3r/Biz/Algorithms/Projection.hpp"

/**
@brief class with only static function add ability to engraved OR raised
text OR polygons onto model surface
*/
namespace Slic3r::Biz::Emboss {
// Limit direction of up vector on model between side and top surface
static const double UP_LIMIT = 0.9; // absolut size of z of the normalized vector [in range 0 - 1]
static const float UNION_DELTA           = 50.0f; // [approx in nano meters depends on volume scale]
static const unsigned UNION_MAX_ITERATIN = 10; // [count]
/**
@brief Collect fonts registred inside OS
@return OS registred TTF font files(full path) with names
*/
Domain::EmbossStyles get_font_list();
#ifdef _WIN32
Domain::EmbossStyles get_font_list_by_register();
Domain::EmbossStyles get_font_list_by_enumeration();
Domain::EmbossStyles get_font_list_by_folder();
#endif

/**
@brief OS dependent function to get location of font by its name descriptor
@param font_face_name Unique identificator for font
@return File path to font when found
*/
std::optional<std::wstring> get_font_path(const std::wstring& font_face_name);

// description of one letter
struct Glyph
{
    // NOTE: shape is scaled by SHAPE_SCALE
    // to be able store points without floating points
    Domain::ExPolygons shape;

    // values are in font points
    int advance_width = 0, left_side_bearing = 0;
};

// cache for glyph by unicode
using Glyphs = std::map<int, Glyph>;

/**
@brief Add caching for shape of glyphs
*/
struct FontFileWithCache
{
    Domain::FontDescriptor descriptor;

    // Pointer on data of the font file
    std::shared_ptr<const Domain::FontFile> font_file;

    // Cache for glyph shape
    // IMPORTANT: accessible only in plater job thread !!!
    // main thread only clear cache by set to another shared_ptr
    std::shared_ptr<Emboss::Glyphs> cache;

    FontFileWithCache() : font_file(nullptr), cache(nullptr) {}

    explicit FontFileWithCache(
        const Domain::FontDescriptor& descriptor,
        std::unique_ptr<const Domain::FontFile> font_file) :
        descriptor(descriptor),
        font_file(std::move(font_file)),
        cache(std::make_shared<Emboss::Glyphs>())
    {}

    bool has_value() const
    {
        return font_file != nullptr && cache != nullptr;
    }
};

/**
@brief Load font file into buffer
@param file_path Location of .ttf or .ttc font file
@return Font object when loaded.
*/
std::unique_ptr<Domain::FontFile> create_font_file(const char* file_path);
// data = raw file data
std::unique_ptr<Domain::FontFile> create_font_file_from_data(
    std::unique_ptr<std::vector<unsigned char>> data
);
#ifdef _WIN32
// fix for unknown pointer HFONT is replaced with "void *"
void* can_load(void* hfont);
std::unique_ptr<Domain::FontFile> create_font_file(void* hfont);
#endif // _WIN32

/**
@brief convert letter into polygons
@param font Define fonts
@param font_index Index of font in collection
@param letter One character defined by unicode codepoint
@param flatness Precision of lettter outline curve in conversion to lines
@return inner polygon cw(outer ccw)
*/
std::optional<Glyph> letter2glyph(
    const Domain::FontFile& font,
    unsigned int font_index,
    int letter,
    float flatness
);

/**
@brief Convert text into polygons
@param font Define fonts + cache, which could extend
@param text Characters to convert
@param font_prop User defined property of the font
@param was_canceled Way to interupt processing
@return Inner polygon cw(outer ccw)
*/
Domain::HealedExPolygons text2shapes(
    FontFileWithCache& font,
    const char* text,
    const Domain::FontProp& font_prop,
    const std::function<bool()>& was_canceled = []() { return false; }
);
Domain::ExPolygonsWithIds text2vshapes(
    FontFileWithCache& font,
    const std::wstring& text,
    const Domain::FontProp& font_prop,
    const std::function<bool()>& was_canceled = []() { return false; }
);

const unsigned ENTER_UNICODE = static_cast<unsigned>('\n');
/// Sum of character '\n'
unsigned get_count_lines(const std::wstring& ws);
unsigned get_count_lines(const std::string& text);
unsigned get_count_lines(const Domain::ExPolygonsWithIds& shape);

/**
@brief Use data from font property to modify transformation
@param angle Z-rotation as angle to Y axis
@param distance Z-move as surface distance
@param transformation In / Out transformation to modify by property
*/
void apply_transformation(
    const std::optional<float>& angle,
    const std::optional<float>& distance,
    Domain::Transform3d& transformation
);

/**
@brief Read information from naming table of font file
search for italic (or oblique), bold italic (or bold oblique)
@param font Selector of font
@param font_index Index of font in collection
@return True when the font description contains italic/obligue otherwise False
*/
bool is_italic(const Domain::FontFile& font, unsigned int font_index);

/**
@brief Create unique character set from string with filtered from text with only character from font
@param text Source vector of glyphs
@param font Font descriptor
@param font_index Define font in collection
@param exist_unknown True when text contain glyph unknown in font
@return Unique set of character from text contained in font
*/
std::string create_range_text(
    const std::string& text,
    const Domain::FontFile& font,
    unsigned int font_index,
    bool* exist_unknown = nullptr
);

/**
@brief Calculate scale for glyph shape convert from shape points to mm
@param fp Property of font
@param ff Font data
@return Conversion to mm
*/
double get_text_shape_scale(const Domain::FontProp& fp, const Domain::FontFile& ff);

/**
@brief getter of font info by collection defined in prop
@param font Contain infos about all fonts(collections) in file
@param prop Index of collection
@return Ascent, descent, line gap
*/
const Domain::FontFile::Info& get_font_info(const Domain::FontFile& font, const Domain::FontProp& prop);

/**
@brief Read from font file and properties height of line with spacing
@param font Infos for collections
@param prop Collection index + Additional line gap
@return Line height with spacing in scaled font points (same as ExPolygons)
*/
int get_line_height(const Domain::FontFile& font, const Domain::FontProp& prop);

/**
@brief Calculate Vertical align
@param align Top | Center | Bottom
@param count_lines 
@return Return align Y offset in mm
*/
double get_align_y_offset_in_mm(
    Domain::FontProp::VerticalAlign align,
    unsigned count_lines,
    const Domain::FontFile& ff,
    const Domain::FontProp& fp
);

/**
@brief Create triangle model for text
@param shape2d text or image
@param projection Define transformation from 2d to 3d(orientation, position, scale, ...)
@return Projected shape into space
*/
indexed_triangle_set polygons2model(
    const Domain::ExPolygons& shape2d,
    const Algorithms::IProjection& projection
);

/**
@brief Suggest wanted up vector of embossed text by emboss direction
@param normal Normalized vector of emboss direction in world
@param up_limit Is compared with normal.z to suggest up direction
@return Wanted up vector
*/
Domain::Vec3d suggest_up(const Domain::Vec3d normal, double up_limit = UP_LIMIT);

/**
@brief By transformation calculate angle between suggested and actual up vector
@param tr Transformation of embossed volume in world
@param up_limit Is compared with normal.z to suggest up direction
@return Rotation of suggested up-vector[in rad] in the range [-Pi, Pi], When rotation is not zero
*/
std::optional<float> calc_up(const Domain::Transform3d& tr, double up_limit = UP_LIMIT);

/**
@brief Create transformation for emboss text object to lay on surface point
@param position Position of surface point
@param normal Normal of surface point
@param up_limit Is compared with normal.z to suggest up direction
@return Transformation onto surface point
*/
Domain::Transform3d create_transformation_onto_surface(
    const Domain::Vec3d& position,
    const Domain::Vec3d& normal,
    double up_limit = UP_LIMIT
);

class ProjectZ : public Algorithms::IProjection
{
public:
    explicit ProjectZ(double depth) : m_depth(depth) {}

    // Inherited via IProject
    std::pair<Domain::Vec3d, Domain::Vec3d> create_front_back(const Domain::Point& p) const override;
    Domain::Vec3d project(const Domain::Vec3d& point) const override;
    std::optional<Domain::Vec2d> unproject(const Domain::Vec3d& p, double* depth = nullptr) const override;
    double m_depth;
};

class ProjectScale : public Algorithms::IProjection
{
    std::unique_ptr<Algorithms::IProjection> core;
    double m_scale;

public:
    ProjectScale(std::unique_ptr<Algorithms::IProjection> core, double scale) :
        core(std::move(core)),
        m_scale(scale)
    {}

    // Inherited via IProject
    std::pair<Domain::Vec3d, Domain::Vec3d> create_front_back(const Domain::Point& p) const override
    {
        auto res = core->create_front_back(p);
        return std::make_pair(res.first * m_scale, res.second * m_scale);
    }

    Domain::Vec3d project(const Domain::Vec3d& point) const override
    {
        return core->project(point);
    }

    std::optional<Domain::Vec2d> unproject(const Domain::Vec3d& p, double* depth = nullptr) const override
    {
        auto res = core->unproject(p / m_scale, depth);
        if (depth != nullptr)
            *depth *= m_scale;
        return res;
    }
};

class ProjectTransform : public Algorithms::IProjection
{
    std::unique_ptr<Algorithms::IProjection> m_core;
    Domain::Transform3d m_tr;
    Domain::Transform3d m_tr_inv;
    double z_scale;

public:
    ProjectTransform(std::unique_ptr<Algorithms::IProjection> core, const Domain::Transform3d& tr) :
        m_core(std::move(core)),
        m_tr(tr)
    {
        m_tr_inv = m_tr.inverse();
        z_scale  = (m_tr.linear() * Domain::Vec3d::UnitZ()).norm();
    }

    // Inherited via IProject
    std::pair<Domain::Vec3d, Domain::Vec3d> create_front_back(const Domain::Point& p) const override
    {
        auto [front, back] = m_core->create_front_back(p);
        return std::make_pair(m_tr * front, m_tr * back);
    }

    Domain::Vec3d project(const Domain::Vec3d& point) const override
    {
        return m_core->project(point);
    }

    std::optional<Domain::Vec2d> unproject(const Domain::Vec3d& p, double* depth = nullptr) const override
    {
        auto res = m_core->unproject(m_tr_inv * p, depth);
        if (depth != nullptr)
            *depth *= z_scale;
        return res;
    }
};

class OrthoProject3d : public Algorithms::IProject3d
{
    // size and direction of emboss for ortho projection
    Domain::Vec3d m_direction;

public:
    OrthoProject3d(Domain::Vec3d direction) : m_direction(direction) {}

    Domain::Vec3d project(const Domain::Vec3d& point) const override
    {
        return point + m_direction;
    }
};

class OrthoProject : public Algorithms::IProjection
{
    Domain::Transform3d m_matrix;
    // size and direction of emboss for ortho projection
    Domain::Vec3d m_direction;
    Domain::Transform3d m_matrix_inv;

public:
    OrthoProject(Domain::Transform3d matrix, Domain::Vec3d direction) :
        m_matrix(matrix),
        m_direction(direction),
        m_matrix_inv(matrix.inverse())
    {}

    // Inherited via IProject
    std::pair<Domain::Vec3d, Domain::Vec3d> create_front_back(const Domain::Point& p) const override;
    Domain::Vec3d project(const Domain::Vec3d& point) const override;
    std::optional<Domain::Vec2d> unproject(const Domain::Vec3d& p, double* depth = nullptr) const override;
};

/**
@brief Define point laying on polygon
keep index of polygon line and point coordinate
*/
struct PolygonPoint
{
    // index of line inside of polygon
    // 0 .. from point polygon[0] to polygon[1]
    size_t index;

    // Point, which lay on line defined by index
    Domain::Point point;
};

using PolygonPoints = std::vector<PolygonPoint>;

/**
@brief Define polygon for draw letters
*/
struct TextLine
{
    // slice of object
    Domain::Polygon polygon;

    // point laying on polygon closest to zero
    PolygonPoint start;

    // offset of text line in volume mm
    float y;
};

using TextLines = std::vector<TextLine>;

/**
@brief Sample slice polygon by bounding boxes centers
slice start point has shape_center_x coor
@param slice Polygon and start point[Slic3r scaled milimeters]
@param bbs Bounding boxes of letter on one line[in font scales]
@param scale Scale for bbs (after multiply bb is in milimeters)
@return Sampled polygon by bounding boxes
*/
PolygonPoints sample_slice(const TextLine& slice, const Domain::BoundingBoxes2crd& bbs, double scale);

/**
@brief Calculate angle for polygon point
@param distance Distance for found normal in point
@param polygon_point Select point on polygon
@param polygon Polygon know neighbor of point
@return angle(atan2) of normal in polygon point
*/
double calculate_angle(int32_t distance, PolygonPoint polygon_point, const Domain::Polygon& polygon);
std::vector<double> calculate_angles(
    const Domain::BoundingBoxes2crd& glyph_sizes,
    const PolygonPoints& polygon_points,
    const Domain::Polygon& polygon
);

// delta .. safe offset before union (use as boolean close)
// NOTE: remove unprintable spaces between neighbor curves (made by linearization of curve)
Domain::ExPolygons union_with_delta(Domain::EmbossShape& shape, float delta, unsigned max_heal_iteration);

enum class ReadShapeResult {
    file_inaccessible,
    nsvg_issue,
    no_shape, // svg do not conatain path
    cant_heal, // double point and self intersection
    success // no issue -> successfull
};
ReadShapeResult read_shape_from_file(Domain::EmbossShape& shape,
    const std::optional<double>& volume_scale_x,
    const std::optional<double>& volume_scale_y);
std::string to_string(ReadShapeResult issue, const std::string& file_path);

/**
 *  @brief  Check whether transformation matrix contains odd number of mirroring.
 *          @note In code is sometime function named is_left_handed
 *  @param  transform - Transformation to check
 *  @retval           - Is positive determinant
 */
bool has_reflection(const Domain::Transform3d& transform);

/**
 *  @brief  select volume parts (Neccessary for per glyph slicing)
 *  @param  mo - object to slice(Part+Negative+Modifiers)
 *  @retval    - List of parts
 */
Domain::ModelVolumePtrs prepare_volumes_to_slice(const Domain::ModelObject& mo);
Domain::ModelVolumePtrs prepare_volumes_to_slice(const Domain::ModelVolume& mv);

} // namespace Slic3r::Biz::Emboss
#endif // slic3r_Emboss_hpp_
