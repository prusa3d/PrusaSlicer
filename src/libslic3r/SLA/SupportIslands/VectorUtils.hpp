#ifndef slic3r_SLA_SuppotstIslands_VectorUtils_hpp_
#define slic3r_SLA_SuppotstIslands_VectorUtils_hpp_

#include <vector>
#include <algorithm>
#include <numeric> // iota
#include <functional>

namespace Slic3r::sla {

/// <summary>
/// Class which contain collection of static function
/// for work with std vector
/// QUESTION: Is it only for SLA?
/// </summary>
class VectorUtils
{
public:
    VectorUtils() = delete;

    /// <summary>
    /// For sorting a vector by calculated value
    /// CACHE for calculated values
    /// </summary>
    /// <param name="data">vetor to be sorted</param>
    /// <param name="calc">function to calculate sorting value</param>
    template<typename T1, typename T2>
    static void sort_by(std::vector<T1> &data, std::function<T2(const T1 &)> &calc)
    {
        assert(!data.empty());
        if (data.size() <= 1) return;

        // initialize original index locations
        std::vector<size_t> idx(data.size());
        std::iota(idx.begin(), idx.end(), 0);

        // values used for sort
        std::vector<T2> v;
        v.reserve(data.size());
        for (const T1 &d : data) v.emplace_back(calc(d));

        // sort indexes based on comparing values in v
        // using std::stable_sort instead of std::sort
        // to avoid unnecessary index re-orderings
        // when v contains elements of equal values
        std::stable_sort(idx.begin(), idx.end(), [&v](size_t i1, size_t i2) {
            return v[i1] < v[i2];
        });

        reorder_destructive(idx.begin(), idx.end(), data.begin());
    }

    /// <summary>
    /// shortcut to use std::transform with alocation for result
    /// </summary>
    /// <param name="data">vetor to be transformed</param>
    /// <param name="transform_func">lambda to transform data types</param>
    /// <returns>result vector</returns>
    template<typename T1, typename T2>
    static std::vector<T2> transform(const std::vector<T1> &data, std::function<T2(const T1 &)> &transform_func)
    {
        std::vector<T2> result;
        result.reserve(data.size());
        std::transform(data.begin(), data.end(), std::back_inserter(result), transform_func);
        return result;
    }

    /// <summary>
    /// Reorder vector by indexes given by iterators
    /// </summary>
    /// <param name="order_begin">Start index</param>
    /// <param name="order_end">Last index</param>
    /// <param name="v">data to reorder. e.g. vector::begin()</param>
    template<typename order_iterator, typename value_iterator>
    static void reorder(order_iterator order_begin,
                        order_iterator order_end,
                        value_iterator v)
    {
        typedef typename std::iterator_traits<value_iterator>::value_type value_t;
        typedef typename std::iterator_traits<order_iterator>::value_type index_t;
        typedef typename std::iterator_traits<order_iterator>::difference_type diff_t;

        diff_t remaining = order_end - 1 - order_begin;
        for (index_t s = index_t(), d; remaining > 0; ++s) {
            for (d = order_begin[s]; d > s; d = order_begin[d])
                ;
            if (d == s) {
                --remaining;
                value_t temp = v[s];
                while (d = order_begin[d], d != s) {
                    std::swap(temp, v[d]);
                    --remaining;
                }
                v[s] = temp;
            }
        }
    }

    /// <summary>
    /// Same as function 'reorder' but destroy order vector for speed
    /// </summary>
    /// <param name="order_begin">Start index</param>
    /// <param name="order_end">Last index</param>
    /// <param name="v">data to reorder. e.g. vector::begin()</param>
    template<typename order_iterator, typename value_iterator>
    static void reorder_destructive(order_iterator order_begin,
                                    order_iterator order_end,
                                    value_iterator v)
    {
        typedef typename std::iterator_traits<value_iterator>::value_type value_t;
        typedef typename std::iterator_traits<order_iterator>::value_type index_t;
        typedef typename std::iterator_traits<order_iterator>::difference_type diff_t;
        static const index_t done_index = static_cast<index_t>(-1);
        diff_t remaining = order_end - 1 - order_begin;
        // s = start sequence index
        for (index_t s = index_t(); remaining > 0; ++s) {
            // d = index1 for swap
            index_t d = order_begin[s];
            if (d == done_index) continue;
            --remaining;
            if (s == d) continue; // correct order
            value_t temp = v[s];
            do {
                std::swap(temp, v[d]);
                index_t d2 = done_index;
                std::swap(order_begin[d], d2);
                d = d2;
                --remaining;
            } while (d != s);
            v[s] = temp;
        }
    }

    /// <summary>
    /// Insert item into sorted vector of items
    /// </summary>
    /// <param name="data">Sorted vector with items</param>
    /// <param name="item">Item to insert</param>
    /// <param name="pred">Predicate for sorting</param>
    /// <returns>now inserted item</returns>
    template<typename T, typename Pred>
    static typename std::vector<T>::iterator insert_sorted(
        std::vector<T> &data, const T &item, Pred pred)
    {
        auto iterator = std::upper_bound(data.begin(), data.end(), item, pred);
        return data.insert(iterator, item);
    }

};

} // namespace Slic3r::sla
#endif // slic3r_SLA_SuppotstIslands_VectorUtils_hpp_
