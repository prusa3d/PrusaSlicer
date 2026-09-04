#include "ConflictChecker.hpp"

#include <oneapi/tbb/blocked_range.h>
#include <oneapi/tbb/concurrent_vector.h>
#include <oneapi/tbb/parallel_for.h>
#include <map>
#include <functional>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <cassert>
#include <cfloat>
#include <cstdlib>

#include "Slic3r/Biz/Algorithms/Line.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/GCode/WipeTower.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/LayerRegion.hpp"
#include "libslic3r/libslic3r.h"
#include "Slic3r/Biz/Algorithms/Point.hpp"

using namespace Slic3r::Biz;

namespace Slic3r {

namespace {

struct LayerPaths {
    ExtrusionPaths paths;
    double height;
};

struct LineWithID
{
    Line _line;
    int  _obj_id;
    int  _inst_id;
    ExtrusionRole _role;

    LineWithID(const Line& line, int obj_id, int inst_id, const ExtrusionRole& role) :
        _line(line), _obj_id(obj_id), _inst_id(inst_id), _role(role) {}
};

using LineWithIDs = std::vector<LineWithID>;

class LinesBucket
{
private:
    double   _curHeight  = 0.0;
    unsigned _curPileIdx = 0;

    std::vector<LayerPaths> _piles;
    int                         _id;
    Points                      _offsets;

public:
    LinesBucket(std::vector<LayerPaths> &&paths, int id, Points offsets)
    : _piles(std::move(paths)),
      _id(id),
      _offsets(offsets)
      {}
    LinesBucket(LinesBucket &&) = default;

    bool valid() const { return _curPileIdx < _piles.size(); }
    void raise()
    {
        if (valid()) {
            _curHeight += _piles[_curPileIdx].height;
            _curPileIdx++;
        }
    }
    double      curHeight() const { return _curHeight; }
    LineWithIDs curLines() const
    {
        using Slic3r::Biz::Algorithms::Polyline::to_lines;

        LineWithIDs lines;
        for (const ExtrusionPath &path : _piles[_curPileIdx].paths) {
            Polyline check_polyline;
            for (int i = 0; i < (int)_offsets.size(); ++i) {
                check_polyline = path.polyline;
                check_polyline.translate(_offsets[i]);
                Lines tmpLines = to_lines(check_polyline);
                for (const Line& line : tmpLines) { lines.emplace_back(line, _id, i, path.role()); }
            }
        }
        return lines;
    }

    friend bool operator>(const LinesBucket &left, const LinesBucket &right) { return left._curHeight > right._curHeight; }
    friend bool operator<(const LinesBucket &left, const LinesBucket &right) { return left._curHeight < right._curHeight; }
    friend bool operator==(const LinesBucket &left, const LinesBucket &right) { return left._curHeight == right._curHeight; }
};

struct LinesBucketPtrComp
{
    bool operator()(const LinesBucket *left, const LinesBucket *right) { return *left > *right; }
};

class LinesBucketQueue
{
private:
    std::vector<LinesBucket>                                                           _buckets;
    std::priority_queue<LinesBucket *, std::vector<LinesBucket *>, LinesBucketPtrComp> _pq;
    std::map<int, const void *>                                                        _idToObjsPtr;
    std::map<const void *, int>                                                        _objsPtrToId;

public:
    void        emplace_back_bucket(std::vector<LayerPaths> &&paths, const void *objPtr, Points offset);
    void        build_queue();
    bool        valid() const { return _pq.empty() == false; }
    const void *idToObjsPtr(int id)
    {
        if (_idToObjsPtr.find(id) != _idToObjsPtr.end())
            return _idToObjsPtr[id];
        else
            return nullptr;
    }
    double      removeLowests();
    LineWithIDs getCurLines() const;
};

struct ConflictComputeResult
{
    int _obj1;
    int _obj2;

    ConflictComputeResult(int o1, int o2) : _obj1(o1), _obj2(o2) {}
    ConflictComputeResult() = default;
};

using ConflictComputeOpt = std::optional<ConflictComputeResult>;
using ConflictObjName = std::optional<std::pair<std::string, std::string>>;



namespace RasterizationImpl {
using IndexPair = std::pair<int64_t, int64_t>;
using Grids     = std::vector<IndexPair>;

inline constexpr int64_t RasteXDistance = scale_(1);
inline constexpr int64_t RasteYDistance = scale_(1);

inline IndexPair point_map_grid_index(const Point &pt, int64_t xdist, int64_t ydist)
{
    auto x = pt.x() / xdist;
    auto y = pt.y() / ydist;
    return std::make_pair(x, y);
}

inline bool nearly_equal(const Point &p1, const Point &p2) { return std::abs(p1.x() - p2.x()) < SCALED_EPSILON && std::abs(p1.y() - p2.y()) < SCALED_EPSILON; }

inline Grids line_rasterization(const Line &line, int64_t xdist = RasteXDistance, int64_t ydist = RasteYDistance)
{
    Grids     res;
    Point     rayStart     = line.a;
    Point     rayEnd       = line.b;
    IndexPair currentVoxel = point_map_grid_index(rayStart, xdist, ydist);
    IndexPair lastVoxel    = point_map_grid_index(rayEnd, xdist, ydist);

    Point ray = rayEnd - rayStart;

    double stepX = ray.x() >= 0 ? 1 : -1;
    double stepY = ray.y() >= 0 ? 1 : -1;

    double nextVoxelBoundaryX = (currentVoxel.first + stepX) * xdist;
    double nextVoxelBoundaryY = (currentVoxel.second + stepY) * ydist;

    if (stepX < 0) { nextVoxelBoundaryX += xdist; }
    if (stepY < 0) { nextVoxelBoundaryY += ydist; }

    double tMaxX = ray.x() != 0 ? (nextVoxelBoundaryX - rayStart.x()) / ray.x() : DBL_MAX;
    double tMaxY = ray.y() != 0 ? (nextVoxelBoundaryY - rayStart.y()) / ray.y() : DBL_MAX;

    double tDeltaX = ray.x() != 0 ? static_cast<double>(xdist) / ray.x() * stepX : DBL_MAX;
    double tDeltaY = ray.y() != 0 ? static_cast<double>(ydist) / ray.y() * stepY : DBL_MAX;

    res.push_back(currentVoxel);

    double tx = tMaxX;
    double ty = tMaxY;

    while (lastVoxel != currentVoxel) {
        if (lastVoxel.first == currentVoxel.first) {
            for (int64_t i = currentVoxel.second; i != lastVoxel.second; i += (int64_t) stepY) {
                currentVoxel.second += (int64_t) stepY;
                res.push_back(currentVoxel);
            }
            break;
        }
        if (lastVoxel.second == currentVoxel.second) {
            for (int64_t i = currentVoxel.first; i != lastVoxel.first; i += (int64_t) stepX) {
                currentVoxel.first += (int64_t) stepX;
                res.push_back(currentVoxel);
            }
            break;
        }

        if (tx < ty) {
            currentVoxel.first += (int64_t) stepX;
            tx += tDeltaX;
        } else {
            currentVoxel.second += (int64_t) stepY;
            ty += tDeltaY;
        }
        res.push_back(currentVoxel);
        if (res.size() >= 100000) { // bug
            assert(0);
        }
    }

    return res;
}
} // namespace RasterizationImpl

using Slic3r::Biz::Algorithms::Point::round;

static std::vector<LayerPaths> getFakeExtrusionPathsFromWipeTower(const WipeTowerData& wtd)
{
    float h = wtd.height;
    float lh = wtd.first_layer_height;
    int   d = scale_(wtd.depth);
    int   w = scale_(wtd.width);
    int   bd = scale_(wtd.brim_width);
    Point minCorner = Point{round(Vec2d{ -wtd.brim_width, -wtd.brim_width }).cast<coord_t>()};
    Point maxCorner = Point{ minCorner.x() + w + bd, minCorner.y() + d + bd };
    float width = wtd.width;
    float depth = wtd.depth;
    float height = wtd.height;
    const auto& z_and_depth_pairs = wtd.z_and_depth_pairs;

    const auto& cone_base_R = wtd.cone_radius;
    const auto& cone_scale_x = wtd.cone_x_scale;

    std::vector<LayerPaths> paths;
    for (float hh = 0.f; hh < h; hh += lh) {
        
        if (hh != 0.f) {
            // The wipe tower may be getting smaller. Find the depth for this layer.
            size_t i = 0;
            for (i=0; i<z_and_depth_pairs.size()-1; ++i)
                if (hh >= z_and_depth_pairs[i].first && hh < z_and_depth_pairs[i+1].first)
                    break;
            d = scale_(z_and_depth_pairs[i].second);
            minCorner = round(Vec2d{0.f, -d/2 + scale_(z_and_depth_pairs.front().second/2.f)}).cast<coord_t>();
            maxCorner = Point{ minCorner.x() + w, minCorner.y() + d };
        }


        ExtrusionPath path({ minCorner, {maxCorner.x(), minCorner.y()}, maxCorner, {minCorner.x(), maxCorner.y()}, minCorner },
            ExtrusionAttributes{ ExtrusionRole::WipeTower, ExtrusionFlow{ 0.0, 0.0, lh } });
        paths.push_back(LayerPaths{{path}, lh});

        // We added the border, now add several parallel lines so we can detect an object that is fully inside the tower.
        // For now, simply use fixed spacing of 3mm.
        for (coord_t y=minCorner.y()+scale_(3.); y<maxCorner.y(); y+=scale_(3.)) {
            path.polyline = { {minCorner.x(), y}, {maxCorner.x(), y} };
            paths.back().paths.emplace_back(path);
        }

        // And of course the stabilization cone and its base...
        if (cone_base_R > 0.) {
            path.polyline.clear();
            double r = cone_base_R * (1 - hh/height);
            for (double alpha=0; alpha<2.01*M_PI; alpha+=2*M_PI/20.)
                path.polyline.points.emplace_back(scaled(Vec2d(width/2. + r * std::cos(alpha)/cone_scale_x, depth/2. + r * std::sin(alpha))));
            paths.back().paths.emplace_back(path);
            if (hh == 0.f) { // Cone brim.
                for (float bw=wtd.brim_width; bw>0.f; bw-=3.f) {
                    path.polyline.clear();
                    for (double alpha=0; alpha<2.01*M_PI; alpha+=2*M_PI/20.) // see load_wipe_tower_preview, where the same is a bit clearer
                        path.polyline.points.emplace_back(scaled(Vec2d(
                            width/2. + cone_base_R * std::cos(alpha)/cone_scale_x * (1. + cone_scale_x*bw/cone_base_R),
                            depth/2. + cone_base_R * std::sin(alpha) * (1. + bw/cone_base_R)))
                        );
                    paths.back().paths.emplace_back(path);
                }
            }
        }

        // Only the first layer has brim.
        if (hh == 0.f) {
            minCorner = minCorner + Point(bd, bd);
            maxCorner = maxCorner - Point(bd, bd);
        }
    }

    // Rotate and translate the tower into the final position.
    for (LayerPaths& ps : paths) {
        for (ExtrusionPath& p : ps.paths) {
            p.polyline.rotate(deg2rad(wtd.rotation_angle));
            p.polyline.translate(scale_(wtd.position.x()), scale_(wtd.position.y()));
        }
    }

    return paths;
}



void LinesBucketQueue::emplace_back_bucket(std::vector<LayerPaths> &&paths, const void *objPtr, Points offsets)
{
    if (_objsPtrToId.find(objPtr) == _objsPtrToId.end()) {
        _objsPtrToId.insert({objPtr, _objsPtrToId.size()});
        _idToObjsPtr.insert({_objsPtrToId.size() - 1, objPtr});
    }
    _buckets.emplace_back(std::move(paths), _objsPtrToId[objPtr], offsets);
}

void LinesBucketQueue::build_queue()
{
    assert(_pq.empty());
    for (LinesBucket &bucket : _buckets)
        _pq.push(&bucket);
}

double LinesBucketQueue::removeLowests()
{
    auto lowest = _pq.top();
    _pq.pop();
    double                     curHeight = lowest->curHeight();
    std::vector<LinesBucket *> lowests;
    lowests.push_back(lowest);

    while (_pq.empty() == false && std::abs(_pq.top()->curHeight() - lowest->curHeight()) < EPSILON) {
        lowests.push_back(_pq.top());
        _pq.pop();
    }

    for (LinesBucket *bp : lowests) {
        bp->raise();
        if (bp->valid()) { _pq.push(bp); }
    }
    return curHeight;
}

LineWithIDs LinesBucketQueue::getCurLines() const
{
    LineWithIDs lines;
    for (const LinesBucket &bucket : _buckets) {
        if (bucket.valid()) {
            LineWithIDs tmpLines = bucket.curLines();
            lines.insert(lines.end(), tmpLines.begin(), tmpLines.end());
        }
    }
    return lines;
}

void getExtrusionPathsFromEntity(const ExtrusionEntityCollection *entity, ExtrusionPaths &paths)
{
    std::function<void(const ExtrusionEntityCollection *, ExtrusionPaths &)> getExtrusionPathImpl = [&](const ExtrusionEntityCollection *entity, ExtrusionPaths &paths) {
        for (auto entityPtr : entity->entities) {
            if (const ExtrusionEntityCollection *collection = dynamic_cast<ExtrusionEntityCollection *>(entityPtr)) {
                getExtrusionPathImpl(collection, paths);
            } else if (const ExtrusionPath *path = dynamic_cast<ExtrusionPath *>(entityPtr)) {
                paths.push_back(*path);
            } else if (const ExtrusionMultiPath *multipath = dynamic_cast<ExtrusionMultiPath *>(entityPtr)) {
                for (const ExtrusionPath &path : multipath->paths) { paths.push_back(path); }
            } else if (const ExtrusionLoop *loop = dynamic_cast<ExtrusionLoop *>(entityPtr)) {
                for (const ExtrusionPath &path : loop->paths) { paths.push_back(path); }
            }
        }
    };
    getExtrusionPathImpl(entity, paths);
}

LayerPaths getExtrusionPathsFromLayer(LayerRegionPtrs layerRegionPtrs)
{
    ExtrusionPaths paths;
    for (auto regionPtr : layerRegionPtrs) {
        getExtrusionPathsFromEntity(&regionPtr->perimeters(), paths);
        if (!regionPtr->perimeters().empty()) { getExtrusionPathsFromEntity(&regionPtr->fills(), paths); }
    }
    return {std::move(paths), layerRegionPtrs.front()->layer()->height};
}

LayerPaths getExtrusionPathsFromSupportLayer(const SupportLayer *supportLayer)
{
    ExtrusionPaths paths;
    getExtrusionPathsFromEntity(&supportLayer->support_fills, paths);
    return {std::move(paths), supportLayer->height};
}

std::pair<std::vector<LayerPaths>, std::vector<LayerPaths>> getAllLayersExtrusionPathsFromObject(const PrintObject *obj)
{
    std::vector<LayerPaths> objPaths;
    std::vector<LayerPaths> supportPaths;

    for (auto layerPtr : obj->layers()) {
        objPaths.emplace_back(getExtrusionPathsFromLayer(layerPtr->regions()));
    }

    for (auto supportLayerPtr : obj->support_layers()) {
        supportPaths.emplace_back(getExtrusionPathsFromSupportLayer(supportLayerPtr));
    }

    return {std::move(objPaths), std::move(supportPaths)};
}

ConflictComputeOpt line_intersect(const LineWithID &l1, const LineWithID &l2)
{
    if (l1._obj_id == l2._obj_id && l1._inst_id == l2._inst_id) { return {}; } // lines are from same instance

    Point inter;
    bool  intersect = Algorithms::Line::intersection(l1._line, l2._line, inter);
    if (intersect) {
        auto dist1 = std::min(unscale(Point(l1._line.a - inter)).norm(), unscale(Point(l1._line.b - inter)).norm());
        auto dist2 = std::min(unscale(Point(l2._line.a - inter)).norm(), unscale(Point(l2._line.b - inter)).norm());
        auto dist  = std::min(dist1, dist2);
        if (dist > 0.01) { return std::make_optional<ConflictComputeResult>(l1._obj_id, l2._obj_id); } // the two lines intersects if dist>0.01mm
    }
    return {};
}

ConflictComputeOpt find_inter_of_lines(const LineWithIDs &lines)
{
    using namespace RasterizationImpl;
    std::map<IndexPair, std::vector<int>> indexToLine;

    for (int i = 0; i < (int)lines.size(); ++i) {
        const LineWithID &l1      = lines[i];
        auto              indexes = line_rasterization(l1._line);
        for (auto index : indexes) {
            const auto &possibleIntersectIdxs = indexToLine[index];
            for (auto possibleIntersectIdx : possibleIntersectIdxs) {
                const LineWithID &l2 = lines[possibleIntersectIdx];
                if (auto interRes = line_intersect(l1, l2); interRes.has_value()) { return interRes; }
            }
            indexToLine[index].push_back(i);
        }
    }
    return {};
}

} // anonymous namespace

namespace Biz::Slicing {

ConflictResultOpt find_inter_of_lines_in_diff_objs(
    SpanOfConstPtrs<PrintObject> objs,
    const std::optional<WipeTowerData>& wipe_tower_data
) // find the first intersection point of lines in different objects
{
    // There is no conflict when there are no objects,
    // or when there is only one object with a single instance and the wipe tower is disabled.
    if (objs.empty()
     || (objs.size() == 1 && objs.front()->instances().size() == 1 && !wipe_tower_data)) {
        return {};
    }

    // The code ported from BS uses void* to identify objects...
    // Let's use the address of this variable to represent the wipe tower.
    int wtptr = 0;

    LinesBucketQueue conflictQueue;
    if (wipe_tower_data) {
        // The wipe tower is being generated.
        const Point plate_origin = Point::Zero();
        std::vector<LayerPaths> wtpaths = getFakeExtrusionPathsFromWipeTower(*wipe_tower_data);
        conflictQueue.emplace_back_bucket(std::move(wtpaths), &wtptr, Points{plate_origin});
    }
    for (const PrintObject *obj : objs) {
        std::pair<std::vector<LayerPaths>, std::vector<LayerPaths>> layers = getAllLayersExtrusionPathsFromObject(obj);

        Points instances_shifts;
        for (const PrintInstance& inst : obj->instances())
            instances_shifts.emplace_back(inst.shift());

        conflictQueue.emplace_back_bucket(std::move(layers.first), obj, instances_shifts);
        conflictQueue.emplace_back_bucket(std::move(layers.second), obj, instances_shifts);
    }
    conflictQueue.build_queue();

    std::vector<LineWithIDs> layersLines;
    std::vector<double>      heights;
    while (conflictQueue.valid()) {
        LineWithIDs lines     = conflictQueue.getCurLines();
        double      curHeight = conflictQueue.removeLowests();
        heights.push_back(curHeight);
        layersLines.push_back(std::move(lines));
    }

    tbb::concurrent_vector<std::pair<ConflictComputeResult,double>> conflict;

    tbb::parallel_for(tbb::blocked_range<size_t>(0, layersLines.size()), [&](tbb::blocked_range<size_t> range) {
        for (size_t i = range.begin(); i < range.end(); i++) {
            auto interRes = find_inter_of_lines(layersLines[i]);
            if (interRes.has_value()) {
                conflict.emplace_back(*interRes, i+1 < heights.size() ? heights[i+1] : heights.back());
                break;
            }
        }
    });

    if (! conflict.empty()) {
        std::sort(conflict.begin(), conflict.end(), [](const std::pair<ConflictComputeResult, double>& i1, const std::pair<ConflictComputeResult, double>& i2) {
            return i1.second < i2.second;
        });

        const void *ptr1   = conflictQueue.idToObjsPtr(conflict[0].first._obj1);
        const void *ptr2   = conflictQueue.idToObjsPtr(conflict[0].first._obj2);
        double conflict_z  = conflict[0].second;
        if (ptr1 == &wtptr || ptr2 == &wtptr) {
            assert(wipe_tower_data);
            if (ptr2 == &wtptr) { std::swap(ptr1, ptr2); }
            const PrintObject *obj2 = reinterpret_cast<const PrintObject *>(ptr2);
            ConflictResult out = {
                .obj_name_1 = "WipeTower",
                .obj_name_2 = obj2->model_object()->name,
                .height = float(conflict_z),
                .obj_1 = nullptr,
                .obj_2 = ptr2,
            };
            return out;
        }
        const PrintObject *obj1 = reinterpret_cast<const PrintObject *>(ptr1);
        const PrintObject *obj2 = reinterpret_cast<const PrintObject *>(ptr2);
        ConflictResult out = {
            .obj_name_1 = obj1->model_object()->name,
            .obj_name_2 = obj2->model_object()->name,
            .height = float(conflict_z),
            .obj_1 = ptr1,
            .obj_2 = ptr2
        };
        return out;
    } else
        return {};
}



void ConflictResult::reset()
{
    height = 0.0f;
    obj_1 = nullptr;
    obj_2 = nullptr;
    obj_name_1.clear();
    obj_name_2.clear();
}

} // namespace Biz::Slicing
} // namespace Slic3r
