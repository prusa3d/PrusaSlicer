///|/ Copyright (c) Prusa Research 2024 Filip Sykala @Jony01
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/

#include "SupportPointGenerator.hpp"

#include "libslic3r/Execution/ExecutionTBB.hpp" // parallel preparation of data for sampling
#include "libslic3r/Execution/Execution.hpp"
#include "libslic3r/KDTreeIndirect.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/AABBTreeLines.hpp" // closest point to layer part
#include "libslic3r/AABBMesh.hpp" // move_on_mesh_surface Should be in another file
// SupportIslands
#include "libslic3r/SLA/SupportIslands/UniformSupportIsland.hpp"
#include "libslic3r/SLA/SupportIslands/SampleConfigFactory.hpp"

using namespace Slic3r;
using namespace Slic3r::sla;

namespace {
#ifndef NDEBUG
bool exist_point_in_distance(const Vec3f &p, float distance, const LayerSupportPoints &pts) {
    float distance_sq = sqr(distance);
    return std::any_of(pts.begin(), pts.end(), [&p, distance_sq](const LayerSupportPoint &sp) {
        return (sp.pos - p).squaredNorm() < distance_sq; });
}
#endif // NDEBUG

/// <summary>
/// Struct to store support points in KD tree to fast search for nearest ones.
/// </summary>
class NearPoints
{
    /// <summary>
    /// Structure made for KDTree as function to 
    /// acess support point coordinate by index into global support point storage
    /// </summary>
    struct PointAccessor {
        // multiple trees points into same data storage of all support points
        LayerSupportPoints *m_supports_ptr;
        explicit PointAccessor(LayerSupportPoints *supports_ptr) : m_supports_ptr(supports_ptr) {}
        // accessor to coordinate for kd tree
        const coord_t &operator()(size_t idx, size_t dimension) const {
            return m_supports_ptr->at(idx).position_on_layer[dimension];
        }
    };

    PointAccessor m_points;
    using Tree = KDTreeIndirect<2, coord_t, PointAccessor>;
    Tree m_tree;
public:
    /// <summary>
    /// Constructor get pointer on the global storage of support points
    /// </summary>
    /// <param name="supports_ptr">Pointer on Support vector</param>
    explicit NearPoints(LayerSupportPoints* supports_ptr)
        : m_points(supports_ptr), m_tree(m_points) {}

    NearPoints get_copy(){ 
        NearPoints copy(m_points.m_supports_ptr);
        copy.m_tree = m_tree.get_copy(); // copy tree
        return copy;
    }

    /// <summary>
    /// Remove support points from KD-Tree which lay out of expolygons
    /// </summary>
    /// <param name="shapes">Define area where could be support points</param>
    /// <param name="current_z">Current layer z coordinate 
    /// to prevent remove permanent points in prev layer influence</param>
    void remove_out_of(const ExPolygons &shapes, float current_z) {
        std::vector<size_t> indices = get_indices();
        auto it = std::remove_if(indices.begin(), indices.end(), 
            [&pts = *m_points.m_supports_ptr, &shapes, current_z](size_t point_index) {
                const LayerSupportPoint& lsp = pts.at(point_index);
                if (lsp.is_permanent && lsp.pos.z() >= current_z)
                    return false;
                return !std::any_of(shapes.begin(), shapes.end(), 
                    [&p = lsp.position_on_layer](const ExPolygon &shape) {
                        return shape.contains(p);
                    });
            });
        if (it == indices.end())
            return; // no point to remove
        indices.erase(it, indices.end());
        m_tree.clear();
        m_tree.build(indices); // consume indices
    }

    /// <summary>
    /// Add point new support point into global storage of support points 
    /// and pointer into tree structure of nearest points
    /// </summary>
    /// <param name="point">New added point</param>
    void add(LayerSupportPoint &&point) {
        // IMPROVE: only add to existing tree, do not reconstruct tree
        std::vector<size_t> indices = get_indices();
        LayerSupportPoints &pts = *m_points.m_supports_ptr;
        assert(!exist_point_in_distance(point.pos, point.head_front_radius, pts));
        size_t index = pts.size();
        pts.emplace_back(std::move(point));
        indices.push_back(index);
        m_tree.clear();
        m_tree.build(indices); // consume indices
    }

    using CheckFnc = std::function<bool(const LayerSupportPoint &, const Point&)>;
    /// <summary>
    /// Iterate over support points in 2d radius and search any of fnc with True.
    /// Made for check wheather surrounding supports support current point p.
    /// </summary>
    /// <param name="pos">Center of search circle</param>
    /// <param name="radius">Search circle radius</param>
    /// <param name="fnc">Function to check supports point</param>
    /// <returns>True wheny any of check function return true, otherwise False</returns>
    bool exist_true_in_radius(const Point &pos, coord_t radius, const CheckFnc &fnc) const {
        std::vector<size_t> point_indices = find_nearby_points(m_tree, pos, radius);
        return std::any_of(point_indices.begin(), point_indices.end(), 
            [&points = *m_points.m_supports_ptr, &pos, &fnc](size_t point_index){
                return fnc(points.at(point_index), pos);
            });
    }

    /// <summary>
    /// Merge another tree structure into current one.
    /// Made for connection of two mesh parts.
    /// </summary>
    /// <param name="near_point">Another near points</param>
    void merge(NearPoints &&near_point) {
        // need to have same global storage of support points
        assert(m_points.m_supports_ptr == near_point.m_points.m_supports_ptr);

        // IMPROVE: merge trees instead of rebuild
        std::vector<size_t> indices = get_indices();
        std::vector<size_t> indices2 = near_point.get_indices();
        // merge
        indices.insert(indices.end(),
            std::move_iterator(indices2.begin()), 
            std::move_iterator(indices2.end()));
        // remove duplicit indices - Diamond case
        std::sort(indices.begin(), indices.end());
        auto it = std::unique(indices.begin(), indices.end());
        indices.erase(it, indices.end());
        // rebuild tree
        m_tree.clear();
        m_tree.build(indices); // consume indices
    }

    /// <summary>
    /// Getter on used indices
    /// </summary>
    /// <returns>Current used Indices into m_points</returns>
    std::vector<size_t> get_indices() const {
        std::vector<size_t> indices = m_tree.get_nodes(); // copy
        // nodes in tree contain "max values for size_t" on unused leaf of tree,
        // when count of indices is not exactly power of 2
        auto it = std::remove_if(indices.begin(), indices.end(), 
            [max_index = m_points.m_supports_ptr->size()](size_t i) { return i >= max_index; });
        indices.erase(it, indices.end());
        return indices;
    }
};
using NearPointss = std::vector<NearPoints>;

/// <summary>
/// Intersection of line segment and circle
/// </summary>
/// <param name="p1">Line segment point A, Point lay inside circle</param>
/// <param name="p2">Line segment point B, Point lay outside or on circle</param>
/// <param name="cnt">Circle center point</param>
/// <param name="r2">squared value of Circle Radius (r2 = r*r)</param>
/// <returns>Intersection point</returns>
Point intersection_line_circle(const Point &p1, const Point &p2, const Point &cnt, double r2) {
    // Vector from p1 to p2
    Vec2d dp_d((p2 - p1).cast<double>());
    // Vector from circle center to p1
    Vec2d f_d((p1 - cnt).cast<double>());

    double a = dp_d.squaredNorm();
    double b = 2 * (f_d.x() * dp_d.x() + f_d.y() * dp_d.y());
    double c = f_d.squaredNorm() - r2;

    // Discriminant of the quadratic equation
    double discriminant = b * b - 4 * a * c;

    // No intersection if discriminant is negative
    assert(discriminant >= 0);
    if (discriminant < 0)
        return {}; // No intersection

    // Calculate the two possible values of t (parametric parameter)
    discriminant = sqrt(discriminant);
    double t1 = (-b - discriminant) / (2 * a);

    // Check for valid intersection points within the line segment
    if (t1 >= 0 && t1 <= 1) {
        return {p1.x() + t1 * dp_d.x(), p1.y() + t1 * dp_d.y()};
    }

    // should not be in use
    double t2 = (-b + discriminant) / (2 * a);
    if (t2 >= 0 && t2 <= 1 && t1 != t2) {
        return {p1.x() + t2 * dp_d.x(), p1.y() + t2 * dp_d.y()};
    }
    return {};
}

/// <summary>
/// Move grid from previous layer to current one
/// for given part
/// </summary>
/// <param name="prev_layer_parts">Grids generated in previous layer</param>
/// <param name="part">Current layer part to process</param>
/// <param name="prev_grids">Grids which will be moved to current grid</param>
/// <returns>Grid for given part</returns>
NearPoints create_near_points(
    const LayerParts &prev_layer_parts,
    const LayerPart &part,
    NearPointss &prev_grids
) {
    const LayerParts::const_iterator &prev_part_it = part.prev_parts.front();
    size_t index_of_prev_part = prev_part_it - prev_layer_parts.begin();
    NearPoints near_points = (prev_part_it->next_parts.size() == 1)?
        std::move(prev_grids[index_of_prev_part]) :
        // Need a copy there are multiple parts above previus one
        prev_grids[index_of_prev_part].get_copy(); // copy    

    // merge other grid in case of multiple previous parts
    for (size_t i = 1; i < part.prev_parts.size(); ++i) {
        const LayerParts::const_iterator &prev_part_it = part.prev_parts[i];
        size_t index_of_prev_part = prev_part_it - prev_layer_parts.begin();
        if (prev_part_it->next_parts.size() == 1) {
            near_points.merge(std::move(prev_grids[index_of_prev_part]));
        } else { // Need a copy there are multiple parts above previus one
            NearPoints grid_ = prev_grids[index_of_prev_part].get_copy(); // copy
            near_points.merge(std::move(grid_));
        }
    }
    return near_points;
}

/// <summary>
/// Add support point to near_points when it is neccessary
/// </summary>
/// <param name="part">Current part - keep samples</param>
/// <param name="config">Configuration to sample</param>
/// <param name="near_points">Keep previous sampled suppport points</param>
/// <param name="part_z">current z coordinate of part</param>
/// <param name="maximal_radius">Max distance to seach support for sample</param>
void support_part_overhangs(
    const LayerPart &part,
    const SupportPointGeneratorConfig &config,
    NearPoints &near_points,
    float part_z,
    coord_t maximal_radius
) {
    NearPoints::CheckFnc is_supported = []
    (const LayerSupportPoint &support_point, const Point &p) -> bool {
        // Debug visualization of all sampled outline
        //return false;
        coord_t r = support_point.current_radius;
        Point dp = support_point.position_on_layer - p;
        if (std::abs(dp.x()) > r) return false;
        if (std::abs(dp.y()) > r) return false;
        double r2 = sqr(static_cast<double>(r));
        return dp.cast<double>().squaredNorm() < r2;
    };

    for (const Point &p : part.samples) {
        if (!near_points.exist_true_in_radius(p, maximal_radius, is_supported)) {
            // not supported sample, soo create new support point
            near_points.add(LayerSupportPoint{
                SupportPoint{
                    Vec3f{unscale<float>(p.x()), unscale<float>(p.y()), part_z},
                    /* head_front_radius */ config.head_diameter / 2,
                    SupportPointType::slope
                },
                /* position_on_layer */ p,
                /* radius_curve_index */ 0,
                /* current_radius */ static_cast<coord_t>(scale_(config.support_curve.front().x()))
                });
        }    
    }
}

/// <summary>
/// Sample part as Island
/// Result store to grid
/// </summary>
/// <param name="part">Island to support</param>
/// <param name="near_points">OUT place to store new supports</param>
/// <param name="part_z">z coordinate of part</param>
/// <param name="permanent">z coordinate of part</param>
/// <param name="cfg"></param>
void support_island(const LayerPart &part, NearPoints& near_points, float part_z,
    const Points &permanent, const SupportPointGeneratorConfig &cfg) {
    SupportIslandPoints samples = uniform_support_island(*part.shape, permanent, cfg.island_configuration);
    for (const SupportIslandPointPtr &sample : samples)
        near_points.add(LayerSupportPoint{
            SupportPoint{
                Vec3f{
                    unscale<float>(sample->point.x()), 
                    unscale<float>(sample->point.y()), 
                    part_z
                },
                /* head_front_radius */ cfg.head_diameter / 2,
                SupportPointType::island
            },
            /* position_on_layer */ sample->point,
            /* radius_curve_index */ 0,
            /* current_radius */ static_cast<coord_t>(scale_(cfg.support_curve.front().x()))
        });
}

void support_peninsulas(const Peninsulas& peninsulas, NearPoints& near_points, float part_z,
    const Points &permanent, const SupportPointGeneratorConfig &cfg) {
    for (const Peninsula& peninsula: peninsulas) {
        SupportIslandPoints peninsula_supports =
            uniform_support_peninsula(peninsula, permanent, cfg.island_configuration);
        for (const SupportIslandPointPtr &support : peninsula_supports)
            near_points.add(LayerSupportPoint{
                SupportPoint{
                    Vec3f{
                        unscale<float>(support->point.x()), 
                        unscale<float>(support->point.y()), 
                        part_z
                    },
                    /* head_front_radius */ cfg.head_diameter / 2, 
                    SupportPointType::island
                },
                /* position_on_layer */ support->point,
                /* radius_curve_index */ 0,
                /* current_radius */ static_cast<coord_t>(scale_(cfg.support_curve.front().x()))
            });
    }   
}

/// <summary>
/// Copy parts shapes from link to output
/// </summary>
/// <param name="part_links">Links between part of mesh</param>
/// <returns>Collected expolygons from links</returns>
ExPolygons get_shapes(const PartLinks& part_links) {
    ExPolygons out;
    out.reserve(part_links.size());
    for (const PartLink &part_link : part_links)
        out.push_back(*part_link->shape); // copy
    return out;
}

/// <summary>
/// Uniformly sample Polyline,
/// Use first point and each next point is first crosing radius from last added
/// </summary>
/// <param name="b">Begin of polyline points to sample</param>
/// <param name="e">End of polyline points to sample</param>
/// <param name="dist2">Squared distance for sampling</param>
/// <returns>Uniformly distributed points laying on input polygon
/// with exception of first and last point(they are closer than dist2)</returns>
Slic3r::Points sample(Points::const_iterator b, Points::const_iterator e, double dist2) {
    assert(e-b >= 2);
    if (e - b < 2)
        return {}; // at least one line(2 points)

    // IMPROVE1: start of sampling e.g. center of Polyline
    // IMPROVE2: Random offset(To remove align of point between slices)
    // IMPROVE3: Sample small overhangs with memory for last sample(OR in center)
    Slic3r::Points r;
    r.push_back(*b);

    //Points::const_iterator prev_pt = e;
    const Point *prev_pt = nullptr;
    for (Points::const_iterator it = b; it+1 < e; ++it){        
        const Point &pt = *(it+1);
        double p_dist2 = (r.back() - pt).cast<double>().squaredNorm();
        while (p_dist2 > dist2) { // line segment goes out of radius
            if (prev_pt == nullptr)
                prev_pt = &(*it);
            r.push_back(intersection_line_circle(*prev_pt, pt, r.back(), dist2));
            p_dist2 = (r.back() - pt).cast<double>().squaredNorm();
            prev_pt = &r.back();
        }
        prev_pt = nullptr;
    }
    return r;
}

bool contain_point(const Point &p, const Points &sorted_points) {
    auto it = std::lower_bound(sorted_points.begin(), sorted_points.end(), p);
    if (it == sorted_points.end())
        return false;
    ++it; // next point should be same as searched
    if (it == sorted_points.end())
        return false;
    return it->x() == p.x() && it->y() == p.y();
};

#ifndef NDEBUG
bool exist_same_points(const ExPolygon &shape, const Points& prev_points) {
    auto shape_points = to_points(shape);
    return shape_points.end() !=
        std::find_if(shape_points.begin(), shape_points.end(), [&prev_points](const Point &p) {
            return contain_point(p, prev_points);
        });
}
#endif // NDEBUG

Points sample_overhangs(const LayerPart& part, double dist2) {
    const ExPolygon &shape = *part.shape;

    // Collect previous expolygons by links collected in loop before    
    ExPolygons prev_shapes = get_shapes(part.prev_parts);
    assert(!prev_shapes.empty());
    ExPolygons overhangs = diff_ex(shape, prev_shapes, ApplySafetyOffset::Yes);
    if (overhangs.empty()) // above part is smaller in whole contour
        return {};
    
    Points prev_points = to_points(prev_shapes);
    std::sort(prev_points.begin(), prev_points.end());

    // TODO: solve case when shape and prev points has same point
    assert(!exist_same_points(shape, prev_points));
        
    auto sample_overhang = [&prev_points, dist2](const Polygon &polygon, Points &samples) {
        const Points &pts = polygon.points;
        // first point which is not part of shape
        Points::const_iterator first_bad = pts.end();
        Points::const_iterator start_it = pts.end();
        for (auto it = pts.begin(); it != pts.end(); ++it) {
            const Point &p = *it;
            if (contain_point(p, prev_points)) {
                if (first_bad == pts.end()) {
                    // remember begining
                    first_bad = it;
                }
                if (start_it != pts.end()) {
                    // finish sampling
                    append(samples, sample(start_it, it, dist2));
                    // prepare for new start
                    start_it = pts.end();
                }
            } else if (start_it == pts.end()) {
                start_it = it;
            }
        }

        // sample last segment
        if (start_it == pts.end()) { // tail is without points
            if (first_bad != pts.begin()) // only begining
                append(samples, sample(pts.begin(), first_bad, dist2));
        } else { // tail contain points
            if (first_bad == pts.begin()) { // only tail
                append(samples, sample(start_it, pts.end(), dist2));
            } else if (start_it == pts.begin()) { // whole polygon is overhang
                assert(first_bad == pts.end());
                Points pts2 = pts; // copy
                pts2.push_back(pts.front());
                append(samples, sample(pts2.begin(), pts2.end(), dist2));
            } else { // need connect begining and tail
                Points pts2;
                pts2.reserve((pts.end() - start_it) + 
                             (first_bad - pts.begin()));
                for (auto it = start_it; it < pts.end(); ++it)
                    pts2.push_back(*it);
                for (auto it = pts.begin(); it < first_bad; ++it)
                    pts2.push_back(*it);
                append(samples, sample(pts2.begin(), pts2.end(), dist2));
            }
        }
    };

    Points samples;
    for (const ExPolygon &overhang : overhangs) {
        sample_overhang(overhang.contour, samples);
        for (const Polygon &hole : overhang.holes) {            
            sample_overhang(hole, samples);
        }
    }
    return samples;
}

coord_t calc_influence_radius(float z_distance, const SupportPointGeneratorConfig &config) { 
    float island_support_distance_sq = sqr(config.support_curve.front().x());
    if (!is_approx(config.density_relative, 1.f, 1e-4f)) // exist relative density
        island_support_distance_sq /= config.density_relative;
    float z_distance_sq = sqr(z_distance);
    if (z_distance_sq >= island_support_distance_sq)
        return 0.f;
    // IMPROVE: use curve interpolation instead of sqrt(stored in config).
    // shape of supported area before permanent supports is sphere with radius of island_support_distance
    return static_cast<coord_t>(scale_(std::sqrt(island_support_distance_sq - z_distance_sq)));
}

void prepare_supports_for_layer(LayerSupportPoints &supports, float layer_z, 
    const NearPointss& activ_points, const SupportPointGeneratorConfig &config) {
    auto set_radius = [&config](LayerSupportPoint &support, float radius) {
        if (!is_approx(config.density_relative, 1.f, 1e-4f)) // exist relative density
            radius = std::sqrt(sqr(radius) / config.density_relative);
        support.current_radius = static_cast<coord_t>(scale_(radius));
    };

    std::vector<bool> is_active(supports.size(), {false});
    for (const NearPoints& pts : activ_points) {
        std::vector<size_t> indices = pts.get_indices();
        for (size_t i : indices)
            is_active[i] = true;
    }

    const std::vector<Vec2f>& curve = config.support_curve;
    // calculate support area for each support point as radius
    // IMPROVE: use some offsets of previous supported island
    for (LayerSupportPoint &support : supports) {
        size_t &index = support.radius_curve_index;
        if (index + 1 >= curve.size())
            continue; // already contain maximal radius

        if (!is_active[&support - &supports.front()])
            continue; // Point is not used in any part of the current layer 

        // find current segment
        float diff_z = layer_z - support.pos.z();
        if (diff_z < 0.) {
            // permanent support influence distribution of support points printed before.
            support.current_radius = calc_influence_radius(-diff_z, config);
            continue;
        }
        while ((index + 1) < curve.size() && diff_z > curve[index + 1].y())
            ++index;

        if ((index+1) >= curve.size()) {
            // set maximal radius
            set_radius(support, curve.back().x());
            continue;
        }
        // interpolate radius on input curve
        Vec2f a = curve[index];
        Vec2f b = curve[index+1];
        assert(a.y() <= diff_z && diff_z <= b.y());
        float t = (diff_z - a.y()) / (b.y() - a.y());
        assert(0 <= t && t <= 1);
        set_radius(support, a.x() + t * (b.x() - a.x()));
    }
}

/// <summary>
/// Near points do not have to contain support points out of part.
/// Due to be able support in same area again(overhang above another overhang)
/// Wanted Side effect, it supports thiny part of overhangs
/// </summary>
/// <param name="near_points"></param>
/// <param name="part">current </param>
/// <param name="current_z">Current layer z coordinate 
/// to prevent remove permanent points in prev layer influence</param>
void remove_supports_out_of_part(NearPoints &near_points, const LayerPart &part, float current_z) {
    // Offsetting is made in data preparation - speed up caused by paralelization 
    //ExPolygons extend_shape = offset_ex(*part.shape, config.removing_delta, ClipperLib::jtSquare);
    near_points.remove_out_of(part.extend_shape, current_z);
}

/// <summary>
/// Detect existence of peninsula on current layer part
/// </summary>
/// <param name="part">IN/OUT island part containing peninsulas</param>
/// <param name="min_peninsula_width">minimal width of overhang to become peninsula</param>
/// <param name="self_supported_width">supported distance from mainland</param>
void create_peninsulas(LayerPart &part, const PrepareSupportConfig &config) {
    assert(config.peninsula_min_width > config.peninsula_self_supported_width);
    const ExPolygons below_shapes = get_shapes(part.prev_parts);
    const ExPolygons below_expanded = offset_ex(below_shapes, config.peninsula_min_width, ClipperLib::jtSquare);
    const ExPolygon &part_shape = *part.shape;
    ExPolygons over_peninsula = diff_ex(part_shape, below_expanded);
    if (over_peninsula.empty())
        return; // only tiny overhangs

    ExPolygons below_self_supported = offset_ex(below_shapes, config.peninsula_self_supported_width, ClipperLib::jtSquare);
    // exist weird edge case, where expand function return empty result
    assert(below_self_supported.empty());

    // exist layer part over peninsula limit
    ExPolygons peninsulas_shape = diff_ex(part_shape, below_self_supported);

    // IMPROVE: Anotate source of diff by ClipperLib_Z
    Lines below_lines = to_lines(below_self_supported);
    auto get_angle = [](const Line &l) {
        Point diff = l.b - l.a;
        if (diff.x() < 0) // Only positive direction X
            diff = -diff;
        return atan2(diff.y(), diff.x());
    };
    std::vector<double> belowe_line_angle; // define direction of line with positive X
    belowe_line_angle.reserve(below_lines.size()); 
    for (const Line& l : below_lines)
        belowe_line_angle.push_back(get_angle(l));
    std::vector<size_t> idx(below_lines.size());
    std::iota(idx.begin(), idx.end(), 0);
    auto is_lower = [&belowe_line_angle](size_t i1, size_t i2) {
        return belowe_line_angle[i1] < belowe_line_angle[i2]; };
    std::sort(idx.begin(), idx.end(), is_lower);

    // Check, wheather line exist in set of belowe lines
    // True .. line exist in previous layer (or partialy overlap previous line), connection to land
    // False .. line is made by border of current layer part(peninsula coast)
    auto exist_belowe = [&get_angle, &idx, &below_lines, &belowe_line_angle]
    (const Line &l) {
        // It is edge case of expand
        if (below_lines.empty())
            return false;
        // allowed angle epsilon
        const double angle_epsilon = 1e-3; // < 0.06 DEG
        const double paralel_epsilon = scale_(1e-2); // 10 um
        double angle = get_angle(l);
        double low_angle = angle - angle_epsilon;
        bool is_over = false;
        if (low_angle <= -M_PI_2) {
            low_angle += M_PI;
            is_over = true;
        }
        double hi_angle = angle + angle_epsilon;
        if (hi_angle >= M_PI_2) {
            hi_angle -= M_PI;
            is_over = true;
        }
        int mayorit_idx = 0;
        if (Point d = l.a - l.b; 
            abs(d.x()) < abs(d.y()))
            mayorit_idx = 1;

        coord_t low = l.a[mayorit_idx];
        coord_t high = l.b[mayorit_idx];
        if (low > high)
            std::swap(low, high);

        auto is_lower_angle = [&belowe_line_angle](size_t index, double angle) {
            return belowe_line_angle[index] < angle; };
        auto it_idx = std::lower_bound(idx.begin(), idx.end(), low_angle, is_lower_angle);
        if (it_idx == idx.end()) {
            if (is_over) {
                it_idx = idx.begin();
                is_over = false;
            } else {
                return false;
            }
        }
        while (is_over || belowe_line_angle[*it_idx] < hi_angle) {
            const Line &l2 = below_lines[*it_idx];
            coord_t l2_low = l2.a[mayorit_idx];
            coord_t l2_high = l2.b[mayorit_idx];
            if (low > high)
                std::swap(low, high);
            if ((l2_high >= low && l2_low <= high) && (
                ((l2.a == l.a && l2.b == l.b) ||(l2.a == l.b && l2.b == l.a)) || // speed up - same line
                l.perp_distance_to(l2.a) < paralel_epsilon)) // check distance of parallel lines
                return true;
            ++it_idx;
            if (it_idx == idx.end()){
                if (is_over) {
                    it_idx = idx.begin();
                    is_over = false;
                } else {
                    break;            
                }
            }
        }
        return false;
    };

    // anotate source of peninsula: overhang VS previous layer  
    for (const ExPolygon &peninsula : peninsulas_shape) {
        // Check that peninsula is wide enough(min_peninsula_width)
        if (intersection_ex(ExPolygons{peninsula}, over_peninsula).empty())
            continue; 

        // need to know shape and edges of peninsula
        Lines lines = to_lines(peninsula);
        std::vector<bool> is_outline(lines.size());
        // when line overlap with belowe lines it is not outline
        for (size_t i = 0; i < lines.size(); i++) 
            is_outline[i] = !exist_belowe(lines[i]);
        part.peninsulas.push_back(Peninsula{peninsula, is_outline});
    }
}

struct LayerPartIndex {
    size_t layer_index; // index into layers
    size_t part_index; // index into layer parts
    bool operator<(const LayerPartIndex &other) const {
        return layer_index < other.layer_index ||
            (layer_index == other.layer_index && part_index < other.part_index);
    }
    bool operator==(const LayerPartIndex &other) const {
        return layer_index == other.layer_index && part_index == other.part_index;
    }
};
using SmallPart = std::vector<LayerPartIndex>;
using SmallParts = std::vector<SmallPart>;

std::optional<SmallPart> create_small_part(
    const Layers &layers, const LayerPartIndex& island, float radius_in_mm) {
    // Search for BB with diameter(2*r) distance from part and collect parts
    const LayerPart &part = layers[island.layer_index].parts[island.part_index];
    coord_t radius = static_cast<coord_t>(scale_(radius_in_mm));
    
    // only island part could be source of small part
    assert(part.prev_parts.empty()); 
    // Island bounding box Should be already checked out of function
    assert(part.shape_extent.size().x() <= 2*radius &&
           part.shape_extent.size().y() <= 2*radius );
    
    Point range{radius, radius};
    Point center = part.shape_extent.center();
    BoundingBox range_bb{center - range,center + range};
    // BoundingBox range_bb{ // check exist sphere with radius to overlap model part
    //    part.shape_extent.min - range,
    //    part.shape_extent.max + range};

    /// <summary>
    /// Check parts in layers if they are in range of bounding box
    /// Recursive fast check function without storing any already searched data
    /// NOTE: Small part with holes could slow down checking
    /// </summary>
    /// <param name="check_layer_i">Index of layer with part</param>
    /// <param name="check_part_i">Index of part in layer.parts</param>
    /// <param name="allowed_depth">Recursion protection</param>
    std::function<bool(const LayerPartIndex &, size_t)> check_parts;
    check_parts = [&range_bb, &check_parts, &layers, &island, radius_in_mm]
    (const LayerPartIndex& check, size_t allowed_depth) -> bool {
        const Layer &check_layer = layers[check.layer_index];
        const LayerPart &check_part = check_layer.parts[check.part_index];
        for (const PartLink &link : check_part.next_parts)
            // part bound is far than diameter from source part
            if (!range_bb.contains(link->shape_extent.min) ||
                !range_bb.contains(link->shape_extent.max))
                return false; // part is too large

        --allowed_depth;
        if (allowed_depth == 0)
            return true; // break recursion

        // recursively check next layers
        size_t next_layer_i = check.layer_index + 1;
        for (const PartLink& link : check_part.next_parts) {
            size_t next_part_i = link - layers[next_layer_i].parts.cbegin();
            if (!check_parts({next_layer_i, next_part_i}, allowed_depth))
                return false;
        }

        if (check.layer_index == island.layer_index) {
            if (!check_part.prev_parts.empty())
                return false; // prev layers are already checked

            // When model part start from multi islands on the same layer, 
            // Investigate only the first island(lower index into parts).
            if (check.part_index < island.part_index)
                return false; // part is already checked
        }

        
        if (float max_z = radius_in_mm + layers[island.layer_index].print_z;
            check_layer.print_z > max_z)
            return false; // too far in Z

        // NOTE: multiple investigation same part seems more relevant 
        // instead of store and search for already checked parts

        // check previous parts
        for (const PartLink &link : check_part.prev_parts) {
            // part bound is far than diameter from source part
            if (!range_bb.contains(link->shape_extent.min) ||
                !range_bb.contains(link->shape_extent.max))
                return false; // part is too large
        }

        for (const PartLink &link : check_part.prev_parts) {
            size_t prev_layer_i = check.layer_index - 1; // exist only when exist prev parts - cant be negative
            size_t prev_part_i = link - layers[prev_layer_i].parts.cbegin();
            // Improve: check only parts which are not already checked
            if (!check_parts({prev_layer_i, prev_part_i}, allowed_depth))
                return false;
        }
        return true;
    };

    float layer_height = layers[1].print_z - layers[0].print_z;
    assert(layer_height > 0.f);
    float safe_multiplicator = 1.3f; // 30% more layers than radius for zigzag(up & down layer) search
    size_t allowed_depth = static_cast<size_t>(std::ceil(radius_in_mm / layer_height * safe_multiplicator));
    // Check Bounding boxes and do not create any data - FAST CHECK
    // NOTE: it could check model parts multiple times those there is allowed_depth
    if (!check_parts(island, allowed_depth))
        return {};

    SmallPart collected; // sorted by layer_i, part_i
    std::vector<LayerPartIndex> queue_next;
    LayerPartIndex curr = island;
    // create small part by flood fill neighbor parts
    do {
        if (curr.layer_index >= layers.size()){
            if (queue_next.empty())
                break; // finish collecting
            curr = queue_next.back();
            queue_next.pop_back();
        }
        auto collected_it = std::lower_bound(collected.begin(), collected.end(), curr);
        if (collected_it != collected.end() && 
            *collected_it == curr ) // already processed
            continue;
        collected.insert(collected_it, curr); // insert sorted

        const LayerPart &curr_part = layers[curr.layer_index].parts[curr.part_index];
        LayerPartIndex next{layers.size(), 0}; // set to uninitialized value(layer index is out of layers)
        for (const PartLink &link : curr_part.next_parts) {
            size_t next_layer_i = curr.layer_index + 1;
            size_t part_i = link - layers[next_layer_i].parts.begin();
            LayerPartIndex next_{next_layer_i, part_i};
            auto it = std::lower_bound(collected.begin(), collected.end(), next_);
            if (it != collected.end() && *it == next_)
                continue; // already collected

            if (next.layer_index >= layers.size()) // next is uninitialized
                next = next_;
            else
                queue_next.push_back(next_); // insert sorted
        }
        for (const PartLink &link : curr_part.prev_parts) {
            size_t prev_layer_i = curr.layer_index - 1;
            size_t part_i = link - layers[prev_layer_i].parts.begin();
            LayerPartIndex next_{prev_layer_i, part_i};
            auto it = std::lower_bound(collected.begin(), collected.end(), next_);
            if (it != collected.end() && *it == next_)
                continue; // already collected

            if (next.layer_index >= layers.size()) // next is uninitialized
                next = next_;
            else
                queue_next.push_back(next_); // insert sorted
        }
        curr = next;
    } while (true); // NOTE: It is break when queue is empty && curr is invalid

    // Check that whole model part is inside support head sphere
    float print_z = layers[island.layer_index].print_z;
    std::vector<Vec3f> vertices;
    for (const LayerPartIndex &part_id : collected) {
        const Layer &layer = layers[part_id.layer_index];
        double radius_sq = (sqr(radius_in_mm) - sqr(layer.print_z - print_z)) / sqr(SCALING_FACTOR);
        const LayerPart &layer_part = layer.parts[part_id.part_index];
        for (const Point &p : layer_part.shape->contour.points) {
            Vec2d diff2d = (p - center).cast<double>();
            if (sqr(diff2d.x()) + sqr(diff2d.y()) > radius_sq)
                return {}; // part is too large
        }
    }
    return collected;
}

/// <summary>
/// Detection of small parts of support
/// </summary>
SmallParts get_small_parts(const Layers &layers, float radius_in_mm) {
    // collect islands
    coord_t diameter = static_cast<coord_t>(2 * scale_(radius_in_mm));
    std::vector<LayerPartIndex> islands;
    for (size_t layer_i = 0; layer_i < layers.size(); ++layer_i) {
        const Layer &layer = layers[layer_i];
        for (size_t part_i = 0; part_i < layer.parts.size(); ++part_i) {
            const LayerPart &part = layer.parts[part_i];
            if (!part.prev_parts.empty())
                continue; // not island
            if (const Point size = part.shape_extent.size();
                size.x() > diameter || size.y() > diameter)
                continue; // big island
            islands.push_back({layer_i, part_i});
        }
    }

    // multithreaded investigate islands
    std::mutex m; // write access into result
    SmallParts result;
    execution::for_each(ex_tbb, size_t(0), islands.size(),
    [&layers, radius_in_mm, &islands, &result, &m](size_t island_i) {
        std::optional<SmallPart> small_part_opt = create_small_part(layers, islands[island_i], radius_in_mm);
        if (!small_part_opt.has_value())
            return; // no small part
        std::lock_guard lock(m);
        result.push_back(*small_part_opt);
    }, 8 /* gransize */);
    return result;
}

void erase(const SmallParts &small_parts, Layers &layers) {
    // be carefull deleting small parts could invalidate const reference into vector with parts
    // whole layer must be threated at once
    std::vector<LayerPartIndex> to_erase;
    for (const SmallPart &small_part : small_parts) 
        to_erase.insert(to_erase.end(), small_part.begin(), small_part.end());

    auto cmp = [](const LayerPartIndex &a, const LayerPartIndex &b) {
        return a.layer_index < b.layer_index ||
            (a.layer_index == b.layer_index && a.part_index > b.part_index);};
    std::sort(to_erase.begin(), to_erase.end(), cmp); // sort by layer index and part index
    assert(std::unique(to_erase.begin(), to_erase.end()) == to_erase.end());
    size_t erase_to; // without this index
    for (size_t erase_from = 0; erase_from < to_erase.size(); erase_from = erase_to) {
        erase_to = erase_from + 1;
        size_t layer_index = to_erase[erase_from].layer_index;
        while (erase_to < to_erase.size() && 
            to_erase[erase_to].layer_index == layer_index)
            ++erase_to;

        Layer &layer = layers[layer_index];
        LayerParts& parts_ = layer.parts;
        LayerParts layer_parts = parts_; // copy current

        // https://stackoverflow.com/questions/11021764/does-moving-a-vector-invalidate-iterators
        // use swap where const iterator should be guaranteed, instead of move on the end of loop
        std::swap(layer_parts, parts_);  // swap copy into layer parts

        // NOTE: part indices are sorted descent
        for (size_t i = erase_from; i < erase_to; ++i)
            parts_.erase(parts_.begin() + to_erase[i].part_index); // remove parts
        
        if (layer_index > 0) { // not first layer
            Layer& prev_layer = layers[layer_index - 1];
            for (LayerPart& prev_part: prev_layer.parts){
                for (PartLink &next_part : prev_part.next_parts) {
                    size_t part_i = next_part - layer_parts.cbegin();
                    for (size_t i = erase_from; i < erase_to; ++i)
                        if (part_i >= to_erase[i].part_index)
                            --part_i; // index after removed part
                    assert(part_i < parts_.size());
                    next_part = parts_.begin() + part_i;
                }
            }
        }
        if (layer_index < layers.size() - 1) { // not last layer
            Layer& next_layer = layers[layer_index + 1];
            for (LayerPart &next_part : next_layer.parts) {
                for (PartLink &prev_part : next_part.prev_parts) {
                    size_t part_i = prev_part - layer_parts.cbegin();
                    for (size_t i = erase_from; i < erase_to; ++i)
                        if (part_i >= to_erase[i].part_index)
                            --part_i; // index after removed part
                    assert(part_i < parts_.size());
                    prev_part = parts_.begin() + part_i;
                }
            }
        }
    }
}

} // namespace

SupportPointGeneratorData Slic3r::sla::prepare_generator_data(
    std::vector<ExPolygons> &&slices,
    const std::vector<float> &heights,
    const PrepareSupportConfig &config,
    ThrowOnCancel throw_on_cancel,
    StatusFunction statusfn
) {
    // check input
    assert(!slices.empty());
    assert(slices.size() == heights.size());
    if (slices.empty() || slices.size() != heights.size())
        return SupportPointGeneratorData{};

    // Move input into result
    SupportPointGeneratorData result;
    result.slices = std::move(slices);

    // Allocate empty layers.
    result.layers = Layers(result.slices.size());

    // Generate Extents and SampleLayers
    execution::for_each(ex_tbb, size_t(0), result.slices.size(),
    [&result, &heights, throw_on_cancel](size_t layer_id) {
        if ((layer_id % 128) == 0)
            // Don't call the following function too often as it flushes
            // CPU write caches due to synchronization primitves.
            throw_on_cancel();

        Layer &layer = result.layers[layer_id];
        layer.print_z = heights[layer_id]; // copy
        const ExPolygons &islands = result.slices[layer_id];
        layer.parts.reserve(islands.size());
        for (const ExPolygon &island : islands) {
            layer.parts.push_back(LayerPart{
                &island, {},
                get_extents(island.contour)
                // sample - only hangout part of expolygon could be known after linking
            });
        }        
    }, 4 /*gransize*/);

    // Link parts by intersections
    execution::for_each(ex_tbb, size_t(1), result.slices.size(),
    [&result, throw_on_cancel](size_t layer_id) {
        if ((layer_id % 16) == 0)
            throw_on_cancel();

        LayerParts &parts_above = result.layers[layer_id].parts;
        LayerParts &parts_below = result.layers[layer_id-1].parts;
        for (auto it_above = parts_above.begin(); it_above < parts_above.end(); ++it_above) {
            for (auto it_below = parts_below.begin(); it_below < parts_below.end(); ++it_below) {
                // Improve: do some sort of parts + skip some of them
                if (!it_above->shape_extent.overlap(it_below->shape_extent))
                    continue; // no bounding box overlap

                // Improve: test could be done faster way
                Polygons polys = intersection(*it_above->shape, *it_below->shape);
                if (polys.empty())
                    continue; // no intersection

                // TODO: check minimal intersection!

                it_above->prev_parts.push_back(it_below);
                it_below->next_parts.push_back(it_above);
            }
        }
    }, 8 /* gransize */);

    // erase unsupportable model parts
    SmallParts small_parts = get_small_parts(result.layers, config.minimal_bounding_sphere_radius);
    if(!small_parts.empty()) erase(small_parts, result.layers);

    // Sample overhangs part of island
    double sample_distance_in_um = scale_(config.discretize_overhang_step);
    double sample_distance_in_um2 = sample_distance_in_um * sample_distance_in_um;
    execution::for_each(ex_tbb, size_t(1), result.layers.size(),
    [&result, sample_distance_in_um2, throw_on_cancel](size_t layer_id) {
        if ((layer_id % 32) == 0)
            throw_on_cancel();

        LayerParts &parts = result.layers[layer_id].parts;
        for (auto it_part = parts.begin(); it_part < parts.end(); ++it_part) {
            if (it_part->prev_parts.empty())
                continue; // island

            // IMPROVE: overhangs could be calculated with Z coordninate
            // soo one will know source shape of point and do not have to search this
            // information Get inspiration at
            // https://github.com/Prusa-Development/PrusaSlicerPrivate/blob/e00c46f070ec3d6fc325640b0dd10511f8acf5f7/src/libslic3r/PerimeterGenerator.cpp#L399
            it_part->samples = sample_overhangs(*it_part, sample_distance_in_um2);
        }
    }, 8 /* gransize */);

    // Detect peninsula
    execution::for_each(ex_tbb, size_t(1), result.layers.size(),
    [&layers = result.layers, &config, throw_on_cancel](size_t layer_id) {
        if ((layer_id % 32) == 0)
            throw_on_cancel();
        LayerParts &parts = layers[layer_id].parts;
        for (auto it_part = parts.begin(); it_part < parts.end(); ++it_part) {
            if (it_part->prev_parts.empty())
                continue; // island
            create_peninsulas(*it_part, config);
        }
    }, 8 /* gransize */);

    // calc extended parts, more info PrepareSupportConfig::removing_delta
    execution::for_each(ex_tbb, size_t(1), result.layers.size(),
    [&layers = result.layers, delta = config.removing_delta, throw_on_cancel](size_t layer_id) {
        if ((layer_id % 16) == 0)
            throw_on_cancel();
        LayerParts &parts = layers[layer_id].parts;
        for (auto it_part = parts.begin(); it_part < parts.end(); ++it_part)
            it_part->extend_shape = offset_ex(*it_part->shape, delta, ClipperLib::jtSquare);
    }, 8 /* gransize */);
    return result;
}

#ifdef USE_ISLAND_GUI_FOR_SETTINGS
#include "libslic3r/NSVGUtils.hpp"
#include "libslic3r/Utils.hpp"
std::vector<Vec2f> load_curve_from_file() {
    std::string filePath = Slic3r::resources_dir() + "/data/sla_support.svg";
    EmbossShape::SvgFile svg_file{filePath};
    NSVGimage *image = init_image(svg_file);
    if (image == nullptr) {
        // In test is not known resource_dir!
        // File is not located soo return DEFAULT permanent radius 5mm is returned
        return {Vec2f{5.f,.0f}, Vec2f{5.f, 1.f}};
    }
    for (NSVGshape *shape_ptr = image->shapes; shape_ptr != NULL; shape_ptr = shape_ptr->next) {
        const NSVGshape &shape = *shape_ptr;
        if (!(shape.flags & NSVG_FLAGS_VISIBLE)) continue; // is visible
        if (shape.fill.type != NSVG_PAINT_NONE) continue; // is not used fill
        if (shape.stroke.type == NSVG_PAINT_NONE) continue; // exist stroke
        if (shape.strokeWidth < 1e-5f) continue; // is visible stroke width
        if (shape.stroke.color != 4278190261) continue; // is red

        // use only first path
        const NSVGpath *path = shape.paths;
        size_t count_points = path->npts;
        assert(count_points > 1);
        --count_points;
        std::vector<Vec2f> points;
        points.reserve(count_points/3+1);
        points.push_back({path->pts[0], path->pts[1]});
        for (size_t i = 0; i < count_points; i += 3) {
            const float *p = &path->pts[i * 2];
            points.push_back({p[6], p[7]});
        }
        assert(points.size() >= 2);
        return points;
    }

    // red curve line is not found
    assert(false);
    return {};
}
#endif // USE_ISLAND_GUI_FOR_SETTINGS

// Processing permanent support points
// Permanent are manualy edited points by user
namespace {
size_t get_index_of_closest_part(const Point &coor, const LayerParts &parts, double max_allowed_distance_sq) {
    size_t count_lines = 0;
    std::vector<size_t> part_lines_ends;
    part_lines_ends.reserve(parts.size());
    for (const LayerPart &part : parts) {
        count_lines += count_points(*part.shape);
        part_lines_ends.push_back(count_lines);
    }
    Linesf lines;
    lines.reserve(count_lines);
    for (const LayerPart &part : parts)
        append(lines, to_linesf({*part.shape}));
    AABBTreeIndirect::Tree<2, double> tree = 
        AABBTreeLines::build_aabb_tree_over_indexed_lines(lines);

    size_t line_idx = std::numeric_limits<size_t>::max();
    Vec2d coor_d = coor.cast<double>();
    Vec2d hit_point;
    [[maybe_unused]] double distance_sq =
        AABBTreeLines::squared_distance_to_indexed_lines(lines, tree, coor_d, line_idx, hit_point);
    
    if (distance_sq >= max_allowed_distance_sq) // point is farer than 1mm from any layer part
        return parts.size(); // this support point should not be used any more

    // Find part index of closest line
    for (size_t part_index = 0; part_index < part_lines_ends.size(); ++part_index)
        if (line_idx < part_lines_ends[part_index]) {
            // check point lais inside prev or next part shape
            // When assert appear check that part index is really the correct one
            assert(union_ex(
                get_shapes(parts[part_index].prev_parts),
                get_shapes(parts[part_index].next_parts))[0].contains(coor));
            return part_index;
        }
    
    assert(false); // not found
    return parts.size();
}

/// <summary>
/// Guess range of layers by its centers
/// NOTE: not valid range for variable layer height but divide space
/// </summary>
/// <param name="layers"></param>
/// <param name="layer_id"></param>
/// <returns>Range of layers</returns>
MinMax<float> get_layer_range(const Layers &layers, size_t layer_id) {
    assert(layer_id < layers.size());
    if (layer_id >= layers.size())
        return MinMax<float>{0., 0.};

    float print_z = layers[layer_id].print_z;
    float min = (layer_id == 0) ? 0.f : (layers[layer_id - 1].print_z + print_z) / 2.f;
    float max = ((layer_id + 1) < layers.size()) ?
        (layers[layer_id + 1].print_z + print_z) / 2.f :
        print_z + (print_z - min); // last layer guess height by prev layer center
    return MinMax<float>{min, max};
}

size_t get_index_of_layer_part(const Point& coor, const LayerParts& parts, double max_allowed_distance_sq) {
    size_t part_index = parts.size();
    // find part for support point
    for (const LayerPart &part : parts) {
        if (part.shape_extent.contains(coor) && part.shape->contains(coor)) {
            // parts do not overlap each other
            assert(part_index >= parts.size());
            part_index = &part - &parts.front();
        }
    }
    if (part_index >= parts.size()) { // support point is not in any part
        part_index = get_index_of_closest_part(coor, parts, max_allowed_distance_sq);
        // if (part_index >= parts.size()) // support is too far from any part
    }
    return part_index;
}

LayerParts::const_iterator get_closest_part(const PartLinks &links, Vec2d &coor) {
    if (links.size() == 1)
        return links.front();

    Point coor_p = coor.cast<coord_t>();
    // Note: layer part MUST not overlap each other
    for (const PartLink &link : links) {
        LayerParts::const_iterator part_it = link;
        if (part_it->shape_extent.contains(coor_p) && 
            part_it->shape->contains(coor_p)) {
            return part_it;
        }
    }

    size_t count_lines = 0;
    std::vector<size_t> part_lines_ends;
    part_lines_ends.reserve(links.size());
    for (const PartLink &link: links) {
        count_lines += count_points(*link->shape);
        part_lines_ends.push_back(count_lines);
    }
    Linesf lines;
    lines.reserve(count_lines);
    for (const PartLink &link : links)
        append(lines, to_linesf({*link->shape}));
    AABBTreeIndirect::Tree<2, double> tree = 
        AABBTreeLines::build_aabb_tree_over_indexed_lines(lines);

    size_t line_idx = std::numeric_limits<size_t>::max();
    Vec2d hit_point;
    [[maybe_unused]] double distance_sq =
        AABBTreeLines::squared_distance_to_indexed_lines(lines, tree, coor, line_idx, hit_point);
    
    // Find part index of closest line
    for (size_t part_index = 0; part_index < part_lines_ends.size(); ++part_index) {
        if (line_idx >= part_lines_ends[part_index])
            continue;
        
        // check point lais inside prev or next part shape
        // When assert appear check that part index is really the correct one
        assert(union_ex(
            get_shapes(links[part_index]->prev_parts),
            get_shapes(links[part_index]->next_parts))[0].contains(coor.cast<coord_t>()));
        coor = hit_point; // update closest point
        return links[part_index];        
    }
    
    assert(false); // not found
    return links.front();
}

struct PartId {
    // index into layers
    size_t layer_id;
    // index into parts of the previously addresed layer.
    size_t part_id;
};

/// <summary>
/// Dive into previous layers a trace influence over layer parts before support point
/// </summary>
/// <param name="part_id">Index of part that point will appear</param>
/// <param name="layer_id">Index of layer where point will appear</param>
/// <param name="p">Permanent support point</param>
/// <param name="layers">All layers</param>
/// <param name="config"></param>
/// <returns>First influence: Layer_index + Part_index</returns>
PartId get_index_of_first_influence(
    const PartId& partid,
    const SupportPoint &p,
    const Point& coor,
    const Layers &layers,
    const SupportPointGeneratorConfig &config) {
    // find layer part for support
    
    float max_influence_distance = std::max(
        2 * p.head_front_radius, 
        config.support_curve.front().x());

    const LayerParts& parts = layers[partid.layer_id].parts;
    LayerParts::const_iterator current_part_it = parts.cbegin() + partid.part_id;
    LayerParts::const_iterator prev_part_it = current_part_it; // stop influence just before island
    Vec2d coor_d = coor.cast<double>();

    auto get_part_id = [&layers](size_t layer_index, const LayerParts::const_iterator& part_it) {
        const LayerParts &parts = layers[layer_index].parts;
        size_t part_index = part_it - parts.cbegin();
        assert(part_index < parts.size());
        return PartId{layer_index, part_index};
    };
    // Detect not propagate into island
    // Island supports has different behavior
    // p.pos.z() - p.head_front_radius >= layer.print_z
    for (size_t i = 0; i <= partid.layer_id; ++i) {
        size_t current_layer_id = partid.layer_id - i;
        const Layer &layer = layers[current_layer_id];
        float z_distance = p.pos.z() - layer.print_z;
        if (z_distance >= max_influence_distance)
            return get_part_id(current_layer_id, current_part_it); // above layer index
        
        const PartLinks &prev_parts = current_part_it->prev_parts;
        if (prev_parts.empty()){
            // Island support
            return (z_distance < p.head_front_radius) ?
                get_part_id(current_layer_id, current_part_it) :
                get_part_id(current_layer_id + 1, prev_part_it);
        }

        prev_part_it = current_part_it;
        current_part_it = get_closest_part(prev_parts, coor_d);
    }

    // It is unreachable!
    // The first layer is always island
    assert(false);
    return PartId{std::numeric_limits<size_t>::max(), std::numeric_limits<size_t>::max()};
}

struct PermanentSupport {
    SupportPoints::const_iterator point_it; // reference to permanent

    // Define wheere layer part when start influene support area
    // When part is island also affect distribution of supports on island
    PartId influence;

    // Position of support point in layer
    PartId part;
    Point layer_position;
};
using PermanentSupports = std::vector<PermanentSupport>;

/// <summary>
/// Prepare permanent supports for layer's parts
/// </summary>
/// <param name="permanent_supports">Permanent supports</param>
/// <param name="layers">Define heights of layers</param>
/// <param name="config">Define how to propagate to previous layers</param>
/// <returns>Supoorts to add into layer parts</returns>
PermanentSupports prepare_permanent_supports(
    const SupportPoints &permanent_supports,
    const Layers &layers,
    const SupportPointGeneratorConfig &config
) {
    // How to propagate permanent support position into previous layers? and how deep? requirements
    // are chained. IMHO it should start togetjer from islands and permanent than propagate over surface

    if (permanent_supports.empty())
        return {};

    // permanent supports MUST be sorted by z
    assert(std::is_sorted(permanent_supports.begin(), permanent_supports.end(),
        [](const SupportPoint &a, const SupportPoint &b) { return a.pos.z() < b.pos.z(); }));

    size_t permanent_index = 0;
    PermanentSupports result;
    for (size_t layer_id = 0; layer_id < layers.size(); ++layer_id) {
        float layer_max_z = get_layer_range(layers, layer_id).max;
        if (permanent_index >= permanent_supports.size())
            break; // no more permanent supports
            
        if (permanent_supports[permanent_index].pos.z() >= layer_max_z)
            continue; // no permanent support for this layer
            
        const Layer &layer = layers[layer_id];
        for (; permanent_index < permanent_supports.size(); ++permanent_index) {
            SupportPoints::const_iterator point_it = permanent_supports.begin()+permanent_index;
            if (point_it->pos.z() > layer_max_z)
                // support point belongs to another layer
                // Points are sorted by z
                break;

            // find layer part for support
            Point coor(static_cast<coord_t>(scale_(point_it->pos.x())),
                       static_cast<coord_t>(scale_(point_it->pos.y())));

            double allowed_distance_sq = std::max(config.max_allowed_distance_sq,
                sqr(scale_(point_it->head_front_radius)));            
            size_t part_index = get_index_of_layer_part(coor, layer.parts, allowed_distance_sq);
            if (part_index >= layer.parts.size()) 
                continue; // support point is not in any part
            
            PartId part_id{layer_id, part_index};
            // find part of first inlfuenced layer and part for this support point
            PartId influence = get_index_of_first_influence(part_id, *point_it, coor, layers, config);
            result.push_back(PermanentSupport{point_it, influence, part_id, coor});
        }
    }

    // sort by layer index and part index
    std::sort(result.begin(), result.end(), [](const PermanentSupport& s1, const PermanentSupport& s2) {
        return s1.influence.layer_id != s2.influence.layer_id ?
            s1.influence.layer_id < s2.influence.layer_id :
            s1.influence.part_id < s2.influence.part_id; });

    return result;
}

bool exist_permanent_support(const PermanentSupports& supports, size_t current_support_index, 
    size_t layer_index, size_t part_index) {
    if (current_support_index >= supports.size())
        return false;

    const PartId &influence = supports[current_support_index].influence;
    assert(influence.layer_id >= layer_index);
    return influence.layer_id == layer_index && 
           influence.part_id == part_index;
}

/// <summary>
/// copy permanent supports into near points 
/// which has influence into current layer part
/// </summary>
/// <param name="near_points">OUTPUT for all permanent supports for this layer and part</param>
/// <param name="supports">source for Copy</param>
/// <param name="support_index">current index into supports</param>
/// <param name="print_z">current layer index</param>
/// <param name="layer_index">current layer index</param>
/// <param name="part_index">current part index</param>
void copy_permanent_supports(NearPoints& near_points, const PermanentSupports& supports, size_t& support_index, 
    float print_z, size_t layer_index, size_t part_index, const SupportPointGeneratorConfig &config) {
    while (exist_permanent_support(supports, support_index, layer_index, part_index)) {
        const PermanentSupport &support = supports[support_index];
        near_points.add(LayerSupportPoint{
            /* SupportPoint */       *support.point_it,
            /* position_on_layer */  support.layer_position, 
            /* radius_curve_index */ 0, // before support point - earlier influence on point distribution
            /* current_radius */     calc_influence_radius(fabs(support.point_it->pos.z() - print_z), config),
            /* active_in_part */ true,
            /* is_permanent */ true
        });

        // NOTE: increment index globaly
        ++support_index;
    }
}

Points get_permanents(const PermanentSupports &supports, size_t support_index, 
    size_t layer_index, size_t part_index) {
    Points result;
    while (exist_permanent_support(supports, support_index, layer_index, part_index)) {
        result.push_back(supports[support_index].layer_position); // copy
        ++support_index; // only local(temporary) increment
    }
    return result;
}

} // namespace

namespace Slic3r::sla {
using namespace Slic3r;

std::vector<Vec2f> create_default_support_curve(){
#ifdef USE_ISLAND_GUI_FOR_SETTINGS
    return {};
#else  // USE_ISLAND_GUI_FOR_SETTINGS
    return std::vector<Vec2f>{
        Vec2f{3.2f, 0.f},
        Vec2f{4.f, 3.9f},
        Vec2f{5.f, 15.f},
        Vec2f{6.f, 40.f},
    };
#endif // USE_ISLAND_GUI_FOR_SETTINGS
}

SampleConfig create_default_island_configuration(float head_diameter_in_mm) {
    return SampleConfigFactory::create(head_diameter_in_mm);
}

LayerSupportPoints generate_support_points(
    const SupportPointGeneratorData &data,
    const SupportPointGeneratorConfig &config,
    ThrowOnCancel throw_on_cancel,
    StatusFunction statusfn
) {
    const Layers &layers = data.layers;
    double increment = 100.0 / static_cast<double>(layers.size());
    double status = 0; // current progress
    int status_int = 0;
#ifdef USE_ISLAND_GUI_FOR_SETTINGS
    // Hack to set curve for testing
    if (config.support_curve.empty())
        const_cast<SupportPointGeneratorConfig &>(config).support_curve = load_curve_from_file();
#endif // USE_ISLAND_GUI_FOR_SETTINGS

    // Maximal radius of supported area of one support point
    double max_support_radius = config.support_curve.back().x();
    // check distance to nearest support points from grid
    coord_t maximal_radius = static_cast<coord_t>(scale_(max_support_radius));

    // Storage for support points used by grid
    LayerSupportPoints result;

    // Index into data.permanent_supports
    size_t permanent_index = 0;
    PermanentSupports permanent_supports =
        prepare_permanent_supports(data.permanent_supports, layers, config);

    // grid index == part in layer index
    NearPointss prev_grids; // same count as previous layer item size
    for (size_t layer_id = 0; layer_id < layers.size(); ++layer_id) {
        const Layer &layer = layers[layer_id];
        prepare_supports_for_layer(result, layer.print_z, prev_grids, config);

        // grid index == part in layer index
        NearPointss grids;
        grids.reserve(layer.parts.size());

        for (const LayerPart &part : layer.parts) {
            size_t part_id = &part - &layer.parts.front();
            if (part.prev_parts.empty()) {   // Island ?
                grids.emplace_back(&result); // only island add new grid
                Points permanent =
                    get_permanents(permanent_supports, permanent_index, layer_id, part_id);
                support_island(part, grids.back(), layer.print_z, permanent, config);
                copy_permanent_supports(
                    grids.back(), permanent_supports, permanent_index, layer.print_z, layer_id,
                    part_id, config
                );
                continue;
            }

            // first layer should have empty prev_part
            assert(layer_id != 0);
            const LayerParts &prev_layer_parts = layers[layer_id - 1].parts;
            NearPoints near_points = create_near_points(prev_layer_parts, part, prev_grids);
            remove_supports_out_of_part(near_points, part, layer.print_z);
            assert(!near_points.get_indices().empty());
            if (!part.peninsulas.empty()) {
                // only get copy of points do not modify permanent_index
                Points permanent =
                    get_permanents(permanent_supports, permanent_index, layer_id, part_id);
                support_peninsulas(part.peninsulas, near_points, layer.print_z, permanent, config);
            }
            copy_permanent_supports(
                near_points, permanent_supports, permanent_index, layer.print_z, layer_id, part_id,
                config
            );
            support_part_overhangs(part, config, near_points, layer.print_z, maximal_radius);
            grids.push_back(std::move(near_points));
        }
        prev_grids = std::move(grids);

        throw_on_cancel();

        int old_status_int = status_int;
        status += increment;
        status_int = static_cast<int>(std::round(status));
        if (old_status_int < status_int)
            statusfn(status_int);
    }
    // Remove permanent supports from result
    // To preserve permanent 3d position it is necessary to append points after move_on_mesh_surface
    result.erase(
        std::remove_if(
            result.begin(), result.end(), [](const LayerSupportPoint &p) { return p.is_permanent; }
        ),
        result.end()
    );
    return result;
}

SupportPoints move_on_mesh_surface(
    const LayerSupportPoints &points,
    const AABBMesh &mesh,
    double allowed_move,
    ThrowOnCancel throw_on_cancel
) {
    SupportPoints pts;
    pts.reserve(points.size());
    for (const LayerSupportPoint &p : points)
        pts.push_back(static_cast<SupportPoint>(p));

    // The function  makes sure that all the points are really exactly placed on the mesh.
    execution::for_each(
        ex_tbb, size_t(0), pts.size(),
        [&pts, &mesh, &throw_on_cancel, allowed_move](size_t idx) {
            if ((idx % 16) == 0)
                // Don't call the following function too often as it flushes CPU write caches due to
                // synchronization primitves.
                throw_on_cancel();

            Vec3f &p = pts[idx].pos;
            Vec3d p_double = p.cast<double>();
            const Vec3d up_vec(0., 0., 1.);
            const Vec3d down_vec(0., 0., -1.);
            // Project the point upward and downward and choose the closer intersection with the mesh.
            AABBMesh::hit_result hit_up = mesh.query_ray_hit(p_double, up_vec);
            AABBMesh::hit_result hit_down = mesh.query_ray_hit(p_double, down_vec);

            bool up = hit_up.is_hit();
            bool down = hit_down.is_hit();
            // no hit means support points lay exactly on triangle surface
            if (!up && !down)
                return;

            AABBMesh::hit_result &hit = (!down || hit_up.distance() < hit_down.distance()) ?
                hit_up :
                hit_down;
            if (hit.distance() <= allowed_move) {
                p[2] += static_cast<float>(hit.distance() * hit.direction()[2]);
                return;
            }

            // big distance means that ray fly over triangle side (space between triangles)
            int triangle_index;
            Vec3d closest_point;
            double distance = mesh.squared_distance(p_double, triangle_index, closest_point);
            if (distance <= std::numeric_limits<float>::epsilon())
                return; // correct coordinate
            p = closest_point.cast<float>();
        },
        64 /* gransize */
    );
    return pts;
}

} // namespace Slic3r::sla
