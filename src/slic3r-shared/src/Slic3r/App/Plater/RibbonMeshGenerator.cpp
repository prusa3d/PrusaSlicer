#include "Slic3r/App/Plater/RibbonMeshGenerator.hpp"

namespace Slic3r::App::Plater {

void RibbonMeshGenerator::generate(const Points& points, bool closed) const
{
    if (points.size() < 2)
        return;

    constexpr float PI     = M_PI;
    constexpr float TWO_PI = 2.0f * PI;
    size_t numPoints       = points.size();

    float w = width * 0.5f;

    // Helper: Fetches properties of the given segment index
    auto get_seg_props = [&](size_t segIdx, Domain::Vec3f& dir, Domain::Vec3f& norm, float& len)
    {
        size_t nextIdx     = (segIdx + 1) % numPoints;
        Domain::Vec3f diff = points[nextIdx] - points[segIdx];
        len                = diff.norm();
        dir                = (len > 1e-6f) ? (diff / len) : Domain::Vec3f(1.0f, 0.0f, 0.0f);
        norm               = Domain::Vec3f::UnitZ().cross(dir).normalized();
    };

    struct Fillet
    {
        bool has_fillet = false;
        Domain::Vec3f p_start;
        Domain::Vec3f p_end;
        Domain::Vec3f center;
        float r_eff       = 0.0f;
        float start_a     = 0.0f;
        float delta       = 0.0f;
        bool is_left_turn = false;
    };

    // Helper: Computes inset tangent points and the arc center for a rounded corner
    auto compute_fillet = [&](const Domain::Vec3f& p,
                              const Domain::Vec3f& d_prev,
                              const Domain::Vec3f& d_next,
                              float l_prev,
                              float l_next,
                              const Domain::Vec3f& n_prev,
                              Fillet& f)
    {
        float crossZ = d_prev.x() * d_next.y() - d_prev.y() * d_next.x();
        // Dot product using -d_prev to find the inner angle between the incoming and outgoing segments
        float dot = -(d_prev.x() * d_next.x() + d_prev.y() * d_next.y() + d_prev.z() * d_next.z());

        // Skip fillet if collinear, parallel, or radius is 0
        if (corner_radius <= 1e-5f || dot > 0.999f || dot < -0.999f || std::abs(crossZ) <= 1e-5f) {
            f.has_fillet = false;
            f.p_start    = p;
            f.p_end      = p;
            return;
        }

        float theta = std::acos(std::clamp(dot, -1.0f, 1.0f));
        float t     = corner_radius / std::tan(theta / 2.0f);

        // Clamp tangency distance to half the length of the shortest adjacent segment (CSS behavior)
        float t_max = std::min(l_prev, l_next) * 0.5f;
        if (t > t_max) {
            t       = t_max;
            f.r_eff = t * std::tan(theta / 2.0f);
        } else {
            f.r_eff = corner_radius;
        }

        f.p_start      = p - d_prev * t;
        f.p_end        = p + d_next * t;
        f.is_left_turn = (crossZ > 0);

        // Arc center
        f.center = f.is_left_turn ? Domain::Vec3f(f.p_start + n_prev * f.r_eff) :
                                    Domain::Vec3f(f.p_start - n_prev * f.r_eff);

        f.start_a   = std::atan2(f.p_start.y() - f.center.y(), f.p_start.x() - f.center.x());
        float end_a = std::atan2(f.p_end.y() - f.center.y(), f.p_end.x() - f.center.x());

        f.delta = end_a - f.start_a;
        if (f.is_left_turn) {
            while (f.delta <= 0)
                f.delta += TWO_PI;
        } else {
            while (f.delta >= 0)
                f.delta -= TWO_PI;
        }
        f.has_fillet = true;
    };

    // Helper: Generates the actual geometry steps for a joint (Arc or Sharp pivot)
    auto emit_joint = [&](const Fillet& f,
                          const Domain::Vec3f& p,
                          const Domain::Vec3f& d_prev,
                          const Domain::Vec3f& d_next,
                          const Domain::Vec3f& n_prev,
                          const Domain::Vec3f& n_next)
    {
        if (f.has_fillet) {
            int steps = std::max(
                1,
                static_cast<int>(std::ceil((std::abs(f.delta) / TWO_PI) * circle_segments))
            );
            for (int j = 1; j < steps; ++j) {
                float a = f.start_a + f.delta * (static_cast<float>(j) / steps);
                Domain::Vec3f v_rad(std::cos(a), std::sin(a), 0.0f);
                Domain::Vec3f p_arc = f.center + v_rad * f.r_eff;
                Domain::Vec3f n_arc = f.is_left_turn ? -v_rad : v_rad;

                emit(p_arc + n_arc * w);
                emit(p_arc - n_arc * w);
            }
        } else {
            // Fallback to miter pivot if no radius is requested or possible
            float cross_z = d_prev.x() * d_next.y() - d_prev.y() * d_next.x();
            if (std::abs(cross_z) > 1e-5f) {
                if (cross_z > 0) { // Left turn
                    float s_a = std::atan2(-n_prev.y(), -n_prev.x());
                    float e_a = std::atan2(-n_next.y(), -n_next.x());
                    float del = e_a - s_a;
                    while (del <= 0)
                        del += TWO_PI;
                    int steps =
                        std::max(1, static_cast<int>(std::ceil((del / TWO_PI) * circle_segments)));
                    for (int j = 1; j < steps; ++j) {
                        float a = s_a + del * (static_cast<float>(j) / steps);
                        emit(p); // Pivot Left
                        emit(
                            p + Domain::Vec3f(std::cos(a) * w, std::sin(a) * w, 0.0f)
                        ); // Arc Right
                    }
                } else { // Right turn
                    float s_a = std::atan2(n_prev.y(), n_prev.x());
                    float e_a = std::atan2(n_next.y(), n_next.x());
                    float del = e_a - s_a;
                    while (del >= 0)
                        del -= TWO_PI;
                    int steps = std::max(
                        1,
                        static_cast<int>(std::ceil((std::abs(del) / TWO_PI) * circle_segments))
                    );
                    for (int j = 1; j < steps; ++j) {
                        float a = s_a + del * (static_cast<float>(j) / steps);
                        emit(p + Domain::Vec3f(std::cos(a) * w, std::sin(a) * w, 0.0f)); // Arc Left
                        emit(p); // Pivot Right
                    }
                }
            }
        }
    };

    // --- Strip Processing ---

    if (!closed) {
        Domain::Vec3f d, n;
        float l;

        // 1. Emit flat start
        get_seg_props(0, d, n, l);
        emit(points[0] + n * w);
        emit(points[0] - n * w);

        // 2. Process all intermediate joints
        for (size_t i = 1; i < numPoints - 1; ++i) {
            Domain::Vec3f d_prev, n_prev, d_next, n_next;
            float l_prev, l_next;

            get_seg_props(i - 1, d_prev, n_prev, l_prev);
            get_seg_props(i, d_next, n_next, l_next);

            Fillet f;
            compute_fillet(points[i], d_prev, d_next, l_prev, l_next, n_prev, f);

            emit(f.p_start + n_prev * w);
            emit(f.p_start - n_prev * w);

            emit_joint(f, points[i], d_prev, d_next, n_prev, n_next);

            emit(f.p_end + n_next * w);
            emit(f.p_end - n_next * w);
        }

        // 3. Emit flat end
        get_seg_props(numPoints - 2, d, n, l);
        emit(points.back() + n * w);
        emit(points.back() - n * w);

    } else {
        // Closed loop
        Domain::Vec3f first_p_end;
        Domain::Vec3f first_n_next;

        // We loop one extra time to wrap the ribbon back around to the first point
        for (size_t iter = 0; iter <= numPoints; ++iter) {
            size_t i            = iter % numPoints;
            size_t prev_seg_idx = (i + numPoints - 1) % numPoints;
            size_t curr_seg_idx = i;

            Domain::Vec3f d_prev, n_prev, d_next, n_next;
            float l_prev, l_next;

            get_seg_props(prev_seg_idx, d_prev, n_prev, l_prev);
            get_seg_props(curr_seg_idx, d_next, n_next, l_next);

            Fillet f;
            compute_fillet(points[i], d_prev, d_next, l_prev, l_next, n_prev, f);

            if (iter == 0) {
                // Start of loop: Save the exit properties of the first filleted joint
                first_p_end  = f.p_end;
                first_n_next = n_next;

                // Actually begin emitting at the point where point 0's corner fillet finishes
                emit(f.p_end + n_next * w);
                emit(f.p_end - n_next * w);
                continue;
            }

            emit(f.p_start + n_prev * w);
            emit(f.p_start - n_prev * w);

            emit_joint(f, points[i], d_prev, d_next, n_prev, n_next);

            emit(f.p_end + n_next * w);
            emit(f.p_end - n_next * w);
        }
    }
}

} // namespace Slic3r::App::Plater
