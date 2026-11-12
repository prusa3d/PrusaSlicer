#ifndef SUPPORTTREEUTILSLEGACY_HPP
#define SUPPORTTREEUTILSLEGACY_HPP

#include "SupportTreeUtils.hpp"

// Old functions are gathered here that are used in DefaultSupportTree
// to maintain functionality that was well tested.

namespace Slic3r { namespace sla {

// Helper function for pillar interconnection where pairs of already connected
// pillars should be checked for not to be processed again. This can be done
// in constant time with a set of hash values uniquely representing a pair of
// integers. The order of numbers within the pair should not matter, it has
// the same unique hash. The hash value has to have twice as many bits as the
// arguments need. If the same integral type is used for args and return val,
// make sure the arguments use only the half of the type's bit depth.
template<class I, class DoubleI = IntegerOnly<I>>
IntegerOnly<DoubleI> pairhash(I a, I b)
{
    using std::ceil; using std::log2; using std::max; using std::min;
    static const auto constexpr Ibits = int(sizeof(I) * CHAR_BIT);
    static const auto constexpr DoubleIbits = int(sizeof(DoubleI) * CHAR_BIT);
    static const auto constexpr shift = DoubleIbits / 2 < Ibits ? Ibits / 2 : Ibits;

    I g = min(a, b), l = max(a, b);

       // Assume the hash will fit into the output variable
    assert((g ? (ceil(log2(g))) : 0) <= shift);
    assert((l ? (ceil(log2(l))) : 0) <= shift);

    return (DoubleI(g) << shift) + l;
}

template<class Ex>
std::optional<DiffBridge> search_widening_path(Ex                     policy,
                                               const SupportableMesh &sm,
                                               const Vec3d           &jp,
                                               const Vec3d           &dir,
                                               double                 radius,
                                               double new_radius)
{
    double w = radius + 2 * sm.cfg.head_back_radius_mm;
    double stopval = w + jp.z() - ground_level(sm);
    Optimizer<AlgNLoptSubplex> solver(get_criteria(sm.cfg).stop_score(stopval));

    auto [polar, azimuth] = dir_to_spheric(dir);

    double fallback_ratio = radius / sm.cfg.head_back_radius_mm;

    auto oresult = solver.to_max().optimize(
        [&policy, &sm, jp, radius, new_radius](const Biz::Algorithms::Optimize::Input<3> &input) {
            auto &[plr, azm, t] = input;

            auto d = spheric_to_dir(plr, azm).normalized();

            auto sd = sm.cfg.safety_distance(new_radius);

            double ret = pinhead_mesh_hit(policy, sm.emesh, jp, d, radius,
                                          new_radius, t, sd)
                             .distance();

            Beam beam{jp + t * d, d, new_radius};
            double down = beam_mesh_hit(policy, sm.emesh, beam, sd).distance();

            if (ret > t && std::isinf(down))
                ret += jp.z() - ground_level(sm);

            return ret;
        },
        initvals({polar, azimuth, w}), // start with what we have
        bounds({
            {PI - sm.cfg.bridge_slope, PI}, // Must not exceed the slope limit
            {-PI, PI}, // azimuth can be a full search
            {radius + sm.cfg.head_back_radius_mm,
             fallback_ratio * sm.cfg.max_bridge_length_mm}
        }));

    if (oresult.score >= stopval) {
        polar       = std::get<0>(oresult.optimum);
        azimuth     = std::get<1>(oresult.optimum);
        double t    = std::get<2>(oresult.optimum);
        Vec3d  endp = jp + t * spheric_to_dir(polar, azimuth);

        return DiffBridge(jp, endp, radius, sm.cfg.head_back_radius_mm);
    }

    return {};
}

// This is a proxy function for pillar creation which will mind the gap
// between the pad and the model bottom in zero elevation mode.
// 'pinhead_junctionpt' is the starting junction point which needs to be
// routed down. sourcedir is the allowed direction of an optional bridge
// between the jp junction and the final pillar.
template<class Ex>
std::pair<bool, long> create_ground_pillar(
    Ex                     policy,
    SupportTreeBuilder    &builder,
    const SupportableMesh &sm,
    const Vec3d           &pinhead_junctionpt,
    const Vec3d           &sourcedir,
    double                 radius,
    double                 end_radius,
    long                   head_id = SupportTreeNode::ID_UNSET)
{
    Vec3d  jp           = pinhead_junctionpt, endp = jp, dir = sourcedir;
    long   pillar_id    = SupportTreeNode::ID_UNSET;
    bool   can_add_base = false, non_head = false;

    double gndlvl = 0.; // The Z level where pedestals should be
    double jp_gnd = 0.; // The lowest Z where a junction center can be
    double gap_dist = 0.; // The gap distance between the model and the pad

    double r2 = radius + (end_radius - radius) / (jp.z() - ground_level(sm));

    auto to_floor = [&gndlvl](const Vec3d &p) { return Vec3d{p.x(), p.y(), gndlvl}; };

    auto eval_limits = [&sm, &radius, &can_add_base, &gndlvl, &gap_dist, &jp_gnd]
        (bool base_en = true)
    {
        can_add_base  = base_en && radius >= sm.cfg.head_back_radius_mm;
        double base_r = can_add_base ? sm.cfg.base_radius_mm : 0.;
        gndlvl        = ground_level(sm);
        if (!can_add_base) gndlvl -= sm.pad_cfg.wall_thickness_mm;
        jp_gnd   = gndlvl + (can_add_base ? 0. : sm.cfg.head_back_radius_mm);
        gap_dist = sm.cfg.pillar_base_safety_distance_mm + base_r + EPSILON;
    };

    eval_limits();

         // We are dealing with a mini pillar that's potentially too long
    if (radius < sm.cfg.head_back_radius_mm && jp.z() - gndlvl > 20 * radius)
    {
        std::optional<DiffBridge> diffbr =
            search_widening_path(policy, sm, jp, dir, radius,
                                 sm.cfg.head_back_radius_mm);

        if (diffbr && diffbr->endp.z() > jp_gnd) {
            auto &br = builder.add_diffbridge(*diffbr);
            if (head_id >= 0) builder.head(head_id).bridge_id = br.id;
            endp = diffbr->endp;
            radius = diffbr->end_r;
            end_radius = diffbr->end_r;
            builder.add_junction(endp, radius);
            non_head = true;
            dir = diffbr->get_dir();
            eval_limits();
        } else return {false, pillar_id};
    }

    if (sm.cfg.object_elevation_mm < EPSILON)
    {
        // get a suitable direction for the corrector bridge. It is the
        // original sourcedir's azimuth but the polar angle is saturated to the
        // configured bridge slope.
        auto [polar, azimuth] = dir_to_spheric(dir);
        polar = PI - sm.cfg.bridge_slope;
        Vec3d d = spheric_to_dir(polar, azimuth).normalized();
        auto sd = radius * sm.cfg.safety_distance_mm / sm.cfg.head_back_radius_mm;
        double t = beam_mesh_hit(policy, sm.emesh, Beam{endp, d, radius, r2}, sd).distance();
        double tmax = std::min(sm.cfg.max_bridge_length_mm, t);
        t = 0.;

        double zd = endp.z() - jp_gnd;
        double tmax2 = zd / std::sqrt(1 - sm.cfg.bridge_slope * sm.cfg.bridge_slope);
        tmax = std::min(tmax, tmax2);

        Vec3d nexp = endp;
        double dlast = 0.;
        while (((dlast = std::sqrt(sm.emesh.squared_distance(to_floor(nexp)))) < gap_dist ||
                !std::isinf(beam_mesh_hit(policy, sm.emesh, Beam{nexp, DOWN, radius, r2}, sd).distance())) &&
               t < tmax)
        {
            t += radius;
            nexp = endp + t * d;
        }

        if (dlast < gap_dist && can_add_base) {
            nexp         = endp;
            t            = 0.;
            can_add_base = false;
            eval_limits(can_add_base);

            zd = endp.z() - jp_gnd;
            tmax2 = zd / std::sqrt(1 - sm.cfg.bridge_slope * sm.cfg.bridge_slope);
            tmax = std::min(tmax, tmax2);

            while (((dlast = std::sqrt(sm.emesh.squared_distance(to_floor(nexp)))) < gap_dist ||
                    !std::isinf(beam_mesh_hit(policy, sm.emesh, Beam{nexp, DOWN, radius}, sd).distance())) && t < tmax) {
                t += radius;
                nexp = endp + t * d;
            }
        }

        // Could not find a path to avoid the pad gap
        if (dlast < gap_dist) return {false, pillar_id};

        if (t > 0.) { // Need to make additional bridge
            const Bridge& br = builder.add_bridge(endp, nexp, radius);
            if (head_id >= 0) builder.head(head_id).bridge_id = br.id;

            builder.add_junction(nexp, radius);
            endp = nexp;
            non_head = true;
        }
    }

    Vec3d gp = to_floor(endp);
    double h = endp.z() - gp.z();

    pillar_id = head_id >= 0 && !non_head ? builder.add_pillar(head_id, h) :
                                            builder.add_pillar(gp, h, radius, end_radius);

    if (can_add_base)
        builder.add_pillar_base(pillar_id, sm.cfg.base_height_mm,
                                sm.cfg.base_radius_mm);

    return {true, pillar_id};
}

template<class Ex>
std::pair<bool, long> connect_to_ground(Ex                     policy,
                                        SupportTreeBuilder    &builder,
                                        const SupportableMesh &sm,
                                        const Junction        &j,
                                        const Vec3d           &dir,
                                        double                 end_r)
{
    auto   hjp = j.pos;
    double r   = j.r;
    auto   sd  = r * sm.cfg.safety_distance_mm / sm.cfg.head_back_radius_mm;
    double r2  = j.r + (end_r - j.r) / (j.pos.z() - ground_level(sm));

    double t   = beam_mesh_hit(policy, sm.emesh, Beam{hjp, dir, r, r2}, sd).distance();
    double d   = 0, tdown = 0;
    t          = std::min(t, sm.cfg.max_bridge_length_mm * r / sm.cfg.head_back_radius_mm);

    while (d < t &&
           !std::isinf(tdown = beam_mesh_hit(policy, sm.emesh,
                                             Beam{hjp + d * dir, DOWN, r, r2}, sd)
                                   .distance())) {
        d += r;
    }

    if(!std::isinf(tdown))
        return {false, SupportTreeNode::ID_UNSET};

    Vec3d endp = hjp + d * dir;
    auto ret = create_ground_pillar(policy, builder, sm, endp, dir, r, end_r);

    if (ret.second >= 0) {
        builder.add_bridge(hjp, endp, r);
        builder.add_junction(endp, r);
    }

    return ret;
}

template<class Ex>
std::pair<bool, long> search_ground_route(Ex                     policy,
                                          SupportTreeBuilder    &builder,
                                          const SupportableMesh &sm,
                                          const Junction        &j,
                                          double                 end_radius,
                                          const Vec3d &init_dir = DOWN)
{
    double downdst = j.pos.z() - ground_level(sm);

    auto res = connect_to_ground(policy, builder, sm, j, init_dir, end_radius);
    if (res.first)
        return res;

         // Optimize bridge direction:
         // Straight path failed so we will try to search for a suitable
         // direction out of the cavity.
    auto [polar, azimuth] = dir_to_spheric(init_dir);

    Optimizer<AlgNLoptGenetic> solver(get_criteria(sm.cfg).stop_score(1e6));
    solver.seed(0); // we want deterministic behavior

    auto   sd  = j.r * sm.cfg.safety_distance_mm / sm.cfg.head_back_radius_mm;
    auto oresult = solver.to_max().optimize(
        [&j, sd, &policy, &sm, &downdst, &end_radius](const Biz::Algorithms::Optimize::Input<2> &input) {
            auto &[plr, azm] = input;
            Vec3d n = spheric_to_dir(plr, azm).normalized();
            Beam beam{Ball{j.pos, j.r}, Ball{j.pos + downdst * n, end_radius}};
            return beam_mesh_hit(policy, sm.emesh, beam, sd).distance();
        },
        initvals({polar, azimuth}),  // let's start with what we have
        bounds({ {PI - sm.cfg.bridge_slope, PI}, {-PI, PI} })
        );

    Vec3d bridgedir = spheric_to_dir(oresult.optimum).normalized();

    return connect_to_ground(policy, builder, sm, j, bridgedir, end_radius);
}

}} // namespace Slic3r::sla

#endif // SUPPORTTREEUTILSLEGACY_HPP
