#ifndef slic3r_FillLightning_hpp_
#define slic3r_FillLightning_hpp_

#include <functional>
#include <memory>
#include <utility>

#include "Slic3r/Domain/ExPolygon.hpp"
#include "Slic3r/Domain/Polyline.hpp"

#include "libslic3r/Fill/FillBase.hpp"

namespace Slic3r {

class PrintObject;

namespace FillLightning {

class Generator;

// To keep the definition of Octree opaque, we have to define a custom deleter.
struct GeneratorDeleter { void operator()(Generator *p); };
using  GeneratorPtr = std::unique_ptr<Generator, GeneratorDeleter>;

GeneratorPtr build_generator(const PrintObject &print_object, const double fill_density, const std::function<void()> &throw_on_cancel_callback);

class Filler : public Slic3r::Fill
{
public:
    ~Filler() override = default;
    bool is_self_crossing() override { return false; }

    Generator   *generator { nullptr };

protected:
    Fill* clone() const override { return new Filler(*this); }

    void _fill_surface_single(const FillParams&                       params,
                              unsigned int                            thickness_layers,
                               const std::pair<float, Domain::Point>& direction,
                                Domain::ExPolygon                     expolygon,
                                Domain::Polylines&                    polylines_out) override;

    // Let the G-code export reoder the infill lines.
	bool no_sort() const override { return false; }
};

} // namespace FillAdaptive
} // namespace Slic3r

#endif // slic3r_FillLightning_hpp_
