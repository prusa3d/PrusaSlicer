///|/ Copyright (c) Prusa Research 2023 Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef SVGDEBUGOUTPUTKERNELWRAPPER_HPP
#define SVGDEBUGOUTPUTKERNELWRAPPER_HPP

#include <memory>

#include "KernelTraits.hpp"

#include "arrange/PackingContext.hpp"
#include "arrange/NFP/NFPArrangeItemTraits.hpp"
#include "arrange/Beds.hpp"

#include <libslic3r/SVG.hpp>

namespace Slic3r { namespace arr2 {

template<class Kernel>
struct SVGDebugOutputKernelWrapper {
    Kernel &k;
    std::unique_ptr<Slic3r::SVG> svg;
    BoundingBox drawbounds;

    template<class... Args>
    SVGDebugOutputKernelWrapper(const BoundingBox &bounds, Kernel &kern)
        : k{kern}, drawbounds{bounds}
    {}

    template<class ArrItem, class Bed, class Context, class RemIt>
    bool on_start_packing(ArrItem &itm,
                          const Bed &bed,
                          const Context &packing_context,
                          const Range<RemIt> &rem)
    {
        using namespace Slic3r;

        bool ret = KernelTraits<Kernel>::on_start_packing(k, itm, bed,
                                                          packing_context,
                                                          rem);

        if (arr2::get_bed_index(itm) < 0)
            return ret;

        svg.reset();
        auto bounds = drawbounds;
        auto fixed = all_items_range(packing_context);
        svg = std::make_unique<SVG>(std::string("arrange_bed") +
                                        std::to_string(
                                            arr2::get_bed_index(itm)) +
                                        "_" + std::to_string(fixed.size()) +
                                        ".svg",
                                    bounds, 0, false);

        svg->draw(ExPolygon{arr2::to_rectangle(drawbounds)}, "blue", .2f);

        auto nfp = calculate_nfp(itm, packing_context, bed);
        svg->draw_outline(nfp);
        svg->draw(nfp, "green", 0.2f);

        for (const auto &fixeditm : fixed) {
            ExPolygons fixeditm_outline = to_expolygons(fixed_outline(fixeditm));
            svg->draw_outline(fixeditm_outline);
            svg->draw(fixeditm_outline, "yellow", 0.5f);
        }

        return ret;
    }

    template<class ArrItem>
    double placement_fitness(const ArrItem &item, const Vec2crd &transl) const
    {
        return KernelTraits<Kernel>::placement_fitness(k, item, transl);
    }

    template<class ArrItem>
    bool on_item_packed(ArrItem &itm)
    {
        using namespace Slic3r;
        using namespace Slic3r::arr2;

        bool ret = KernelTraits<Kernel>::on_item_packed(k, itm);

        if (svg) {
            ExPolygons itm_outline = to_expolygons(fixed_outline(itm));

            svg->draw_outline(itm_outline);
            svg->draw(itm_outline, "grey");

            svg->Close();
        }

        return ret;
    }
};

}} // namespace Slic3r::arr2

#endif // SVGDEBUGOUTPUTKERNELWRAPPER_HPP
