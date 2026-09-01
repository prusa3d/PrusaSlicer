#ifndef SUPPORT_TREE_TYPES_HPP
#define SUPPORT_TREE_TYPES_HPP  

#include "libslic3r/Point.hpp"

namespace Slic3r {
namespace sla {

namespace {
const Vec3d DOWN = {0.0, 0.0, -1.0};
} // anonymous namespace

struct SupportTreeNode
{
    static const constexpr long ID_UNSET = -1;

    long id = ID_UNSET; // For identification withing a tree.
};

// A junction connecting bridges and pillars
struct Junction: public SupportTreeNode {
    double r = 1;
    Vec3d pos;

    Junction(const Vec3d &tr, double r_mm) : r(r_mm), pos(tr) {}
};

// A pinhead originating from a support point
struct Head: public SupportTreeNode {
    Vec3d dir = DOWN;
    Vec3d pos = {0, 0, 0};

    double r_back_mm = 1;
    double r_pin_mm = 0.5;
    double width_mm = 2;
    double penetration_mm = 0.5;

    // If there is a pillar connecting to this head, then the id will be set.
    long pillar_id = ID_UNSET;

    long bridge_id = ID_UNSET;

    inline void invalidate() { id = ID_UNSET; }
    inline bool is_valid() const { return id >= 0; }

    Head(double r_big_mm,
         double r_small_mm,
         double length_mm,
         double penetration,
         const Vec3d& direction = DOWN,  // direction (normal to the dull end)
         const Vec3d& offset = {0, 0, 0}      // displacement
         )
    : dir(direction)
    , pos(offset)
    , r_back_mm(r_big_mm)
    , r_pin_mm(r_small_mm)
    , width_mm(length_mm)
    , penetration_mm(penetration)
    {
    }


    inline double real_width() const
    {
        return 2 * r_pin_mm + width_mm + 2 * r_back_mm ;
    }

    inline double fullwidth() const
    {
        return real_width() - penetration_mm;
    }

    inline Junction junction() const
    {
        Junction j{pos + (fullwidth() - r_back_mm) * dir, r_back_mm};
        j.id = -this->id; // Remember that this junction is from a head

        return j;
    }

    inline Vec3d junction_point() const
    {
        return junction().pos;
    }
};

// A straight pillar. Only has an endpoint and a height. No explicit starting
// point is given, as it would allow the pillar to be angled.
// Some connection info with other primitives can also be tracked.
struct Pillar: public SupportTreeNode {
    double height, r_start, r_end;
    Vec3d endpt;
    
    // If the pillar connects to a head, this is the id of that head
    bool starts_from_head = true; // Could start from a junction as well
    long start_junction_id = ID_UNSET;
    
    // How many bridges are connected to this pillar
    unsigned bridges = 0;
    
    // How many pillars are cascaded with this one
    unsigned links = 0;

    Pillar(const Vec3d &endp, double h, double start_radius, double end_radius)
        : height{h}
        , r_start(start_radius)
        , r_end(end_radius)
        , endpt(endp)
        , starts_from_head(false)
    {}

    Pillar(const Vec3d &endp, double h, double start_radius)
        : Pillar(endp, h, start_radius, start_radius)
    {}

    Vec3d startpoint() const
    {
        return {endpt.x(), endpt.y(), endpt.z() + height};
    }
    
    const Vec3d& endpoint() const { return endpt; }
};

// A base for pillars or bridges that end on the ground
struct Pedestal: public SupportTreeNode {
    Vec3d pos;
    double height, r_bottom, r_top;

    Pedestal(const Vec3d &p, double h, double rbottom, double rtop)
        : pos{p}, height{h}, r_bottom{rbottom}, r_top{rtop}
    {}
};

// This is the thing that anchors a pillar or bridge to the model body.
// It is actually a reverse pinhead.
struct Anchor: public Head { using Head::Head; };

// A Bridge between two pillars (with junction endpoints)
struct Bridge: public SupportTreeNode {
    double r = 0.8;
    Vec3d startp = Vec3d::Zero(), endp = Vec3d::Zero();
    
    Bridge(const Vec3d &j1,
           const Vec3d &j2,
           double       r_mm  = 0.8): r{r_mm}, startp{j1}, endp{j2}
    {}

    double get_length() const { return (endp - startp).norm(); }
    Vec3d  get_dir() const { return (endp - startp).normalized(); }
};

struct DiffBridge: public Bridge {
    double end_r;

    DiffBridge(const Vec3d &p_s, const Vec3d &p_e, double r_s, double r_e)
        : Bridge{p_s, p_e, r_s}, end_r{r_e}
    {}

    DiffBridge(const Junction &j_s, const Junction &j_e)
        : Bridge{j_s.pos, j_e.pos, j_s.r}, end_r{j_e.r}
    {}
};

struct SupportTreeOutput {
    SupportTreeOutput() = default;
    SupportTreeOutput(std::vector<Pillar>&& p,
                      std::vector<Head>&& h,
                      std::vector<Junction>&& j,
                      std::vector<Bridge>&& b,
                      std::vector<Bridge>&& cb,
                      std::vector<DiffBridge>&& db,
                      std::vector<Pedestal>&& pe,
                      std::vector<Anchor>&& a)
        : pillars(std::move(p))
        , heads(std::move(h))
        , junctions(std::move(j))
        , bridges(std::move(b))
        , crossbridges(std::move(cb))
        , diffbridges(std::move(db))
        , pedestals(std::move(pe))
        , anchors(std::move(a))
    {}

    SupportTreeOutput(SupportTreeOutput&&) = default;
    SupportTreeOutput& operator=(SupportTreeOutput&&) = default;
    SupportTreeOutput(const SupportTreeOutput&) = delete;
    SupportTreeOutput& operator=(const SupportTreeOutput&) = delete;

    std::vector<Head>       heads;
    std::vector<size_t>     head_indices;
    std::vector<Pillar>     pillars;
    std::vector<Junction>   junctions;
    std::vector<Bridge>     bridges;
    std::vector<Bridge>     crossbridges;
    std::vector<DiffBridge> diffbridges;
    std::vector<Pedestal>   pedestals;
    std::vector<Anchor>     anchors;
};



} // namespace sla
} // namespace Slic3r

#endif // SUPPORT_TREE_TYPES_HPP
