#pragma once

#include <initializer_list>

namespace Slic3r::App::Plater {

// Utility class used to calculate distance circle-circle
// Adaptation of code found in:
// https://github.com/davideberly/GeometricTools/blob/master/GTE/Mathematics/Polynomial1.h

class Polynomial1
{
public:
    Polynomial1(std::initializer_list<double> values)
    {
        // C++ 11 will call the default constructor for
        // Polynomial1<Real> p{}, so it is guaranteed that
        // values.size() > 0.
        m_coefficient.resize(values.size());
        std::copy(values.begin(), values.end(), m_coefficient.begin());
        eliminate_leading_zeros();
    }

    // Construction and destruction.  The first constructor creates a
    // polynomial of the specified degree but sets all coefficients to
    // zero (to ensure initialization).  You are responsible for setting
    // the coefficients, presumably with the degree-term set to a nonzero
    // number.  In the second constructor, the degree is the number of
    // initializers plus 1, but then adjusted so that coefficient[degree]
    // is not zero (unless all initializer values are zero).
    explicit Polynomial1(uint32_t degree) : m_coefficient(size_t(degree) + 1, 0.0) {}

    // Eliminate any leading zeros in the polynomial, except in the case
    // the degree is 0 and the coefficient is 0.  The elimination is
    // necessary when arithmetic operations cause a decrease in the degree
    // of the result.  For example, (1 + x + x^2) + (1 + 2*x - x^2) =
    // (2 + 3*x).  The inputs both have degree 2, so the result is created
    // with degree 2.  After the addition we find that the degree is in
    // fact 1 and resize the array of coefficients.  This function is
    // called internally by the arithmetic operators, but it is exposed in
    // the public interface in case you need it for your own purposes.
    void eliminate_leading_zeros()
    {
        const size_t size = m_coefficient.size();
        if (size > 1) {
            const double zero = 0.0;
            int32_t leading;
            for (leading = int32_t(size) - 1; leading > 0; --leading) {
                if (m_coefficient[leading] != zero)
                    break;
            }

            m_coefficient.resize(++leading);
        }
    }

    // Set all coefficients to the specified value.
    void set_coefficients(double value)
    {
        std::fill(m_coefficient.begin(), m_coefficient.end(), value);
    }

    inline uint32_t degree() const
    {
        // By design, m_coefficient.size() > 0.
        return uint32_t(m_coefficient.size() - 1);
    }

    inline const double& operator[](uint32_t i) const
    {
        return m_coefficient[i];
    }

    inline double& operator[](uint32_t i)
    {
        return m_coefficient[i];
    }

    // Evaluate the polynomial.  If the polynomial is invalid, the
    // function returns zero.
    double operator()(double t) const
    {
        int32_t i     = int32_t(m_coefficient.size());
        double result = m_coefficient[--i];
        for (--i; i >= 0; --i) {
            result *= t;
            result += m_coefficient[i];
        }
        return result;
    }

protected:
    // The class is designed so that m_coefficient.size() >= 1.
    std::vector<double> m_coefficient;
};

inline Polynomial1 operator*(const Polynomial1& p0, const Polynomial1& p1)
{
    const uint32_t p0Degree = p0.degree();
    const uint32_t p1Degree = p1.degree();
    Polynomial1 result(p0Degree + p1Degree);
    result.set_coefficients(0.0);
    for (uint32_t i0 = 0; i0 <= p0Degree; ++i0) {
        for (uint32_t i1 = 0; i1 <= p1Degree; ++i1) {
            result[i0 + i1] += p0[i0] * p1[i1];
        }
    }
    return result;
}

inline Polynomial1 operator+(const Polynomial1& p0, const Polynomial1& p1)
{
    const uint32_t p0Degree = p0.degree();
    const uint32_t p1Degree = p1.degree();
    uint32_t i;
    if (p0Degree >= p1Degree) {
        Polynomial1 result(p0Degree);
        for (i = 0; i <= p1Degree; ++i) {
            result[i] = p0[i] + p1[i];
        }
        for (/**/; i <= p0Degree; ++i) {
            result[i] = p0[i];
        }
        result.eliminate_leading_zeros();
        return result;
    } else {
        Polynomial1 result(p1Degree);
        for (i = 0; i <= p0Degree; ++i) {
            result[i] = p0[i] + p1[i];
        }
        for (/**/; i <= p1Degree; ++i) {
            result[i] = p1[i];
        }
        result.eliminate_leading_zeros();
        return result;
    }
}

inline Polynomial1 operator-(const Polynomial1& p0, const Polynomial1& p1)
{
    const uint32_t p0Degree = p0.degree();
    const uint32_t p1Degree = p1.degree();
    uint32_t i;
    if (p0Degree >= p1Degree) {
        Polynomial1 result(p0Degree);
        for (i = 0; i <= p1Degree; ++i) {
            result[i] = p0[i] - p1[i];
        }
        for (/**/; i <= p0Degree; ++i) {
            result[i] = p0[i];
        }
        result.eliminate_leading_zeros();
        return result;
    } else {
        Polynomial1 result(p1Degree);
        for (i = 0; i <= p0Degree; ++i) {
            result[i] = p0[i] - p1[i];
        }
        for (/**/; i <= p1Degree; ++i) {
            result[i] = -p1[i];
        }
        result.eliminate_leading_zeros();
        return result;
    }
}

inline Polynomial1 operator*(double scalar, const Polynomial1& p)
{
    const uint32_t degree = p.degree();
    Polynomial1 result(degree);
    for (uint32_t i = 0; i <= degree; ++i) {
        result[i] = scalar * p[i];
    }
    return result;
}

// Utility class used to calculate distance circle-circle
// Adaptation of code found in:
// https://github.com/davideberly/GeometricTools/blob/master/GTE/Mathematics/RootsPolynomial.h

class RootsPolynomial
{
public:
    // General equations: sum_{i=0}^{d} c(i)*t^i = 0.  The input array 'c'
    // must have at least d+1 elements and the output array 'root' must
    // have at least d elements.

    // Find the roots on (-infinity,+infinity).
    static int32_t find(int32_t degree, const double* c, uint32_t maxIterations, double* roots)
    {
        if (degree >= 0 && c != nullptr) {
            const double zero = 0.0;
            while (degree >= 0 && c[degree] == zero) {
                --degree;
            }

            if (degree > 0) {
                // Compute the Cauchy bound.
                const double one        = 1.0;
                const double invLeading = one / c[degree];
                double maxValue         = zero;
                for (int32_t i = 0; i < degree; ++i) {
                    const double value = std::fabs(c[i] * invLeading);
                    if (value > maxValue)
                        maxValue = value;
                }
                const double bound = one + maxValue;

                return find_recursive(degree, c, -bound, bound, maxIterations, roots);
            } else if (degree == 0)
                // The polynomial is a nonzero constant.
                return 0;
            else {
                // The polynomial is identically zero.
                roots[0] = zero;
                return 1;
            }
        } else
            // Invalid degree or c.
            return 0;
    }

    // If you know that p(tmin) * p(tmax) <= 0, then there must be at
    // least one root in [tmin, tmax].  Compute it using bisection.
    static bool find(
        int32_t degree,
        const double* c,
        double tmin,
        double tmax,
        uint32_t maxIterations,
        double& root
    )
    {
        const double zero = 0.0;
        double pmin       = evaluate(degree, c, tmin);
        if (pmin == zero) {
            root = tmin;
            return true;
        }
        double pmax = evaluate(degree, c, tmax);
        if (pmax == zero) {
            root = tmax;
            return true;
        }

        if (pmin * pmax > zero)
            // It is not known whether the interval bounds a root.
            return false;

        if (tmin >= tmax)
            // Invalid ordering of interval endpoitns.
            return false;

        for (uint32_t i = 1; i <= maxIterations; ++i) {
            root = 0.5 * (tmin + tmax);

            // This test is designed for 'float' or 'double' when tmin
            // and tmax are consecutive floating-point numbers.
            if (root == tmin || root == tmax)
                break;

            const double p       = evaluate(degree, c, root);
            const double product = p * pmin;
            if (product < zero) {
                tmax = root;
                pmax = p;
            } else if (product > zero) {
                tmin = root;
                pmin = p;
            } else
                break;
        }

        return true;
    }

    // Support for the Find functions.
    static int32_t find_recursive(
        int32_t degree,
        double const* c,
        double tmin,
        double tmax,
        uint32_t maxIterations,
        double* roots
    )
    {
        // The base of the recursion.
        const double zero = 0.0;
        double root       = zero;
        if (degree == 1) {
            int32_t numRoots;
            if (c[1] != zero) {
                root     = -c[0] / c[1];
                numRoots = 1;
            } else if (c[0] == zero) {
                root     = zero;
                numRoots = 1;
            } else
                numRoots = 0;

            if (numRoots > 0 && tmin <= root && root <= tmax) {
                roots[0] = root;
                return 1;
            }
            return 0;
        }

        // Find the roots of the derivative polynomial scaled by 1/degree.
        // The scaling avoids the factorial growth in the coefficients;
        // for example, without the scaling, the high-order term x^d
        // becomes (d!)*x through multiple differentiations.  With the
        // scaling we instead get x.  This leads to better numerical
        // behavior of the root finder.
        const int32_t derivDegree = degree - 1;
        std::vector<double> derivCoeff(size_t(derivDegree) + 1);
        std::vector<double> derivRoots(derivDegree);
        for (int32_t i = 0, ip1 = 1; i <= derivDegree; ++i, ++ip1) {
            derivCoeff[i] = c[ip1] * (double) (ip1) / (double) degree;
        }
        const int32_t numDerivRoots = find_recursive(
            degree - 1,
            &derivCoeff[0],
            tmin,
            tmax,
            maxIterations,
            &derivRoots[0]
        );

        int32_t numRoots = 0;
        if (numDerivRoots > 0) {
            // Find root on [tmin,derivRoots[0]].
            if (find(degree, c, tmin, derivRoots[0], maxIterations, root))
                roots[numRoots++] = root;

            // Find root on [derivRoots[i],derivRoots[i+1]].
            for (int32_t i = 0, ip1 = 1; i <= numDerivRoots - 2; ++i, ++ip1) {
                if (find(degree, c, derivRoots[i], derivRoots[ip1], maxIterations, root))
                    roots[numRoots++] = root;
            }

            // Find root on [derivRoots[numDerivRoots-1],tmax].
            if (find(degree, c, derivRoots[size_t(numDerivRoots) - 1], tmax, maxIterations, root))
                roots[numRoots++] = root;
        } else {
            // The polynomial is monotone on [tmin,tmax], so has at most one root.
            if (find(degree, c, tmin, tmax, maxIterations, root))
                roots[numRoots++] = root;
        }
        return numRoots;
    }

    static double evaluate(int32_t degree, const double* c, double t)
    {
        int32_t i     = degree;
        double result = c[i];
        while (--i >= 0) {
            result = t * result + c[i];
        }
        return result;
    }
};

// Adaptation of code found in:
// https://github.com/davideberly/GeometricTools/blob/master/GTE/Mathematics/Vector.h

// Construct a single vector orthogonal to the nonzero input vector.  If
// the maximum absolute component occurs at index i, then the orthogonal
// vector U has u[i] = v[i+1], u[i+1] = -v[i], and all other components
// zero.  The index addition i+1 is computed modulo N.
inline Domain::Vec3d orthogonal(const Domain::Vec3d& v, bool unitLength)
{
    double cmax  = std::fabs(v[0]);
    int32_t imax = 0;
    for (int32_t i = 1; i < 3; ++i) {
        double c = std::fabs(v[i]);
        if (c > cmax) {
            cmax = c;
            imax = i;
        }
    }

    Domain::Vec3d result = Domain::Vec3d::Zero();
    int32_t inext        = imax + 1;
    if (inext == 3)
        inext = 0;

    result[imax]  = v[inext];
    result[inext] = -v[imax];
    if (unitLength) {
        const double sqrDistance = result[imax] * result[imax] + result[inext] * result[inext];
        const double invLength   = 1.0 / std::sqrt(sqrDistance);
        result[imax] *= invLength;
        result[inext] *= invLength;
    }
    return result;
}

} // namespace Slic3r::App::Plater
