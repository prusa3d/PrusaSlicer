#ifndef PRUSASLICER_OPTIMIZER_HPP
#define PRUSASLICER_OPTIMIZER_HPP

#include <utility>
#include <tuple>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <cassert>
#include <optional>

#ifdef WIN32
#undef min
#undef max
#endif

namespace Slic3r::Biz::Algorithms::Optimize {

template<class T, class O = T>
using FloatingOnly = std::enable_if_t<std::is_floating_point<T>::value, O>;

template<class T, class = FloatingOnly<T>>
constexpr T NaN = std::numeric_limits<T>::quiet_NaN();

constexpr float NaNf = NaN<float>;
constexpr double NaNd = NaN<double>;

// A type to hold the complete result of the optimization.
template<size_t N> struct Result {
    int resultcode;     // Method dependent
    std::array<double, N> optimum;
    double score;
};

// An interval of possible input values for optimization
class Bound {
    double m_min, m_max;

public:
    Bound(double min = std::numeric_limits<double>::min(),
          double max = std::numeric_limits<double>::max())
        : m_min(min), m_max(max)
    {}

    double min() const noexcept { return m_min; }
    double max() const noexcept { return m_max; }
};

// Helper types for optimization function input and bounds
template<size_t N> using Input = std::array<double, N>;
template<size_t N> using Bounds = std::array<Bound, N>;

// A type for specifying the stop criteria. Setter methods can be concatenated
class StopCriteria {

    // If the absolute value difference between two scores.
    double m_abs_score_diff = NaNd;

    // If the relative value difference between two scores.
    double m_rel_score_diff = NaNd;

    // Stop if this value or better is found.
    double m_stop_score = NaNd;

    // A predicate that if evaluates to true, the optimization should terminate
    // and the best result found prior to termination should be returned.
    std::function<bool()> m_stop_condition = [] { return false; };

    // The max allowed number of iterations.
    unsigned m_max_iterations = 0;

public:

    StopCriteria & abs_score_diff(double val)
    {
        m_abs_score_diff = val; return *this;
    }

    double abs_score_diff() const { return m_abs_score_diff; }

    StopCriteria & rel_score_diff(double val)
    {
        m_rel_score_diff = val; return *this;
    }

    double rel_score_diff() const { return m_rel_score_diff; }

    StopCriteria & stop_score(double val)
    {
        m_stop_score = val; return *this;
    }

    double stop_score() const { return m_stop_score; }

    StopCriteria & max_iterations(unsigned val)
    {
        m_max_iterations = val; return *this;
    }

    double max_iterations() const { return m_max_iterations; }

    template<class Fn> StopCriteria & stop_condition(Fn &&cond)
    {
        m_stop_condition = cond; return *this;
    }

    bool stop_condition() { return m_stop_condition(); }
};

// Helper class to use optimization methods involving gradient.
template<size_t N> struct ScoreGradient {
    double score;
    std::optional<std::array<double, N>> gradient;

    ScoreGradient(double s, const std::array<double, N> &grad)
        : score{s}, gradient{grad}
    {}
};

// Helper to be used in static_assert.
template<class T> struct always_false { enum { value = false }; };

// Basic interface to optimizer object
template<class Method, class Enable = void> class Optimizer {
public:

    Optimizer(const StopCriteria &)
    {
        static_assert (always_false<Method>::value,
                       "Optimizer unimplemented for given method!");
    }

    // Switch optimization towards function minimum
    Optimizer &to_min() { return *this; }

    // Switch optimization towards function maximum
    Optimizer &to_max() { return *this; }

    // Set criteria for successive optimizations
    Optimizer &set_criteria(const StopCriteria &) { return *this; }

    // Get current criteria
    StopCriteria get_criteria() const { return {}; };

    // Find function minimum or maximum for Func which has has signature:
    // double(const Input<N> &input) and input with dimension N
    //
    // Initial starting point can be given as the second parameter.
    //
    // For each dimension an interval (Bound) has to be given marking the bounds
    // for that dimension.
    //
    // Optionally, some constraints can be given in the form of double(Input<N>)
    // functors. The parameters eq_constraints and ineq_constraints can be used
    // to add equality and inequality (<= 0) constraints to the optimization.
    // Note that it is up the the particular method if it accepts these
    // constraints.
    //
    // initvals have to be within the specified bounds, otherwise its undefined
    // behavior.
    //
    // Func can return a score of type double or optionally a ScoreGradient
    // class to indicate the function gradient for a optimization methods that
    // make use of the gradient.
    template<class Func, size_t N, class...EqConstraints, class...IneqConstraints>
    Result<N> optimize(Func&& /*func*/,
                       const Input<N> &/*initvals*/,
                       const Bounds<N>& /*bounds*/,
                       const std::tuple<EqConstraints...> &eq_constraints = {},
                       const std::tuple<IneqConstraints...> &ineq_constraint = {}
                       ) { return {}; }

    // optional for randomized methods:
    void seed(long /*s*/) {}
};

namespace detail {

// Helper to convert C style array to std::array. The copy should be optimized
// away with modern compilers.
template<size_t N, class T> auto to_arr(const T *a)
{
    std::array<T, N> r;
    std::copy(a, a + N, std::begin(r));
    return r;
}

template<size_t N, class T> auto to_arr(const T (&a) [N])
{
    return to_arr<N>(static_cast<const T *>(a));
}

} // namespace detail

// Helper functions to create bounds, initial value
template<size_t N> Bounds<N> bounds(const Bound (&b) [N]) { return detail::to_arr(b); }
template<size_t N> Input<N> initvals(const double (&a) [N]) { return detail::to_arr(a); }
template<size_t N> auto score_gradient(double s, const double (&grad)[N])
{
    return ScoreGradient<N>(s, detail::to_arr(grad));
}

}

#endif // OPTIMIZER_HPP
