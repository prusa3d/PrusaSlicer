#ifndef SLA_SPATINDEX_HPP
#define SLA_SPATINDEX_HPP

#include <stddef.h>
#include <memory>
#include <utility>
#include <vector>
#include <functional>
#include <cstddef>

#include "Slic3r/Domain/BoundingBox.hpp"
#include "Slic3r/Domain/Types.hpp"

namespace Slic3r::sla {

using PointIndexEl = std::pair<Domain::Vec3d, unsigned>;

class PointIndex {
    class Impl;

    // We use Pimpl because it takes a long time to compile boost headers which
    // is the engine of this class. We include it only in the cpp file.
    std::unique_ptr<Impl> m_impl;
public:

    PointIndex();
    ~PointIndex();

    PointIndex(const PointIndex&);
    PointIndex(PointIndex&&);
    PointIndex& operator=(const PointIndex&);
    PointIndex& operator=(PointIndex&&);

    void insert(const PointIndexEl&);
    bool remove(const PointIndexEl&);

    inline void insert(const Domain::Vec3d& v, unsigned idx)
    {
        insert(std::make_pair(v, unsigned(idx)));
    }

    std::vector<PointIndexEl> query(std::function<bool(const PointIndexEl&)>) const;
    std::vector<PointIndexEl> nearest(const Domain::Vec3d&, unsigned k) const;
    std::vector<PointIndexEl> query(const Domain::Vec3d &v, unsigned k) const // wrapper
    {
        return nearest(v, k);
    }

    // For testing
    size_t size() const;
    bool empty() const { return size() == 0; }

    void foreach(std::function<void(const PointIndexEl& el)> fn);
    void foreach(std::function<void(const PointIndexEl& el)> fn) const;
};

using BoxIndexEl = std::pair<Slic3r::Domain::BoundingBox2crd, unsigned>;

class BoxIndex {
    class Impl;
    
    // We use Pimpl because it takes a long time to compile boost headers which
    // is the engine of this class. We include it only in the cpp file.
    std::unique_ptr<Impl> m_impl;
public:
    
    BoxIndex();
    ~BoxIndex();
    
    BoxIndex(const BoxIndex&);
    BoxIndex(BoxIndex&&);
    BoxIndex& operator=(const BoxIndex&);
    BoxIndex& operator=(BoxIndex&&);
    
    void insert(const BoxIndexEl&);
    void insert(const Domain::BoundingBox2crd & bb, unsigned idx)
    {
        insert(std::make_pair(bb, unsigned(idx)));
    }
    
    bool remove(const BoxIndexEl&);

    enum QueryType { qtIntersects, qtWithin };

    std::vector<BoxIndexEl> query(const Domain::BoundingBox2crd&, QueryType qt);
    
    // For testing
    size_t size() const;
    bool empty() const { return size() == 0; }
    
    void foreach(std::function<void(const BoxIndexEl& el)> fn);
};

} // namespace Slic3r::sla

#endif // SPATINDEX_HPP
