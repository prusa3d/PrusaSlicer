///|/ Copyright (c) Prusa Research 2023 Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef ARRANGE2_HPP
#define ARRANGE2_HPP

#include <libslic3r/MinAreaBoundingBox.hpp>
#include <arrange/NFP/NFPArrangeItemTraits.hpp>

#include "Scene.hpp"
#include "Items/MutableItemTraits.hpp"

namespace Slic3r { namespace arr2 {

template<class ArrItem> class Arranger
{
public:
    class Ctl : public ArrangeTaskCtl {
    public:
        virtual void on_packed(ArrItem &item) {};
    };

    virtual ~Arranger() = default;

    virtual void arrange(std::vector<ArrItem> &items,
                         const std::vector<ArrItem> &fixed,
                         const ExtendedBed &bed,
                         Ctl &ctl) = 0;

    void arrange(std::vector<ArrItem> &items,
                 const std::vector<ArrItem> &fixed,
                 const ExtendedBed &bed,
                 ArrangeTaskCtl &ctl);

    void arrange(std::vector<ArrItem> &items,
                 const std::vector<ArrItem> &fixed,
                 const ExtendedBed &bed,
                 Ctl &&ctl)
    {
        arrange(items, fixed, bed, ctl);
    }

    void arrange(std::vector<ArrItem> &items,
                 const std::vector<ArrItem> &fixed,
                 const ExtendedBed &bed,
                 ArrangeTaskCtl &&ctl)
    {
        arrange(items, fixed, bed, ctl);
    }

    static std::unique_ptr<Arranger> create(const ArrangeSettingsView &settings);
};

template<class ArrItem> using ArrangerCtl = typename Arranger<ArrItem>::Ctl;

template<class ArrItem>
class DefaultArrangerCtl : public Arranger<ArrItem>::Ctl {
    ArrangeTaskCtl *taskctl = nullptr;

public:
    DefaultArrangerCtl() = default;

    explicit DefaultArrangerCtl(ArrangeTaskCtl &ctl) : taskctl{&ctl} {}

    void update_status(int st) override
    {
        if (taskctl)
            taskctl->update_status(st);
    }

    bool was_canceled() const override
    {
        if (taskctl)
            return taskctl->was_canceled();

        return false;
    }
};

template<class ArrItem>
void Arranger<ArrItem>::arrange(std::vector<ArrItem> &items,
             const std::vector<ArrItem> &fixed,
             const ExtendedBed &bed,
             ArrangeTaskCtl &ctl)
{
    arrange(items, fixed, bed, DefaultArrangerCtl<ArrItem>{ctl});
}

class EmptyItemOutlineError: public std::exception {
    static constexpr const char *Msg = "No outline can be derived for object";

public:
    const char* what() const noexcept override { return Msg; }
};

template<class ArrItem> class ArrangeableToItemConverter
{
public:
    virtual ~ArrangeableToItemConverter() = default;

    // May throw EmptyItemOutlineError
    virtual ArrItem convert(const Arrangeable &arrbl, coord_t offs = 0) const = 0;

    // Returns the extent of simplification that the converter utilizes when
    // creating arrange items. Zero shall mean no simplification at all.
    virtual coord_t simplification_tolerance() const { return 0; }

    static std::unique_ptr<ArrangeableToItemConverter> create(
        ArrangeSettingsView::GeometryHandling geometry_handling,
        coord_t                               safety_d);

    static std::unique_ptr<ArrangeableToItemConverter> create(
        const Scene &sc)
    {
        return create(sc.settings().get_geometry_handling(),
                      scaled(sc.settings().get_distance_from_objects()));
    }
};

template<class DStore, class = WritableDataStoreOnly<DStore>>
class AnyWritableDataStore: public AnyWritable
{
    DStore &dstore;

public:
    AnyWritableDataStore(DStore &store): dstore{store} {}

    void write(std::string_view key, std::any d) override
    {
        set_data(dstore, std::string{key}, std::move(d));
    }
};

template<class ArrItem>
class BasicItemConverter : public ArrangeableToItemConverter<ArrItem>
{
    coord_t m_safety_d;
    coord_t m_simplify_tol;

public:
    BasicItemConverter(coord_t safety_d = 0, coord_t simpl_tol = 0)
        : m_safety_d{safety_d}, m_simplify_tol{simpl_tol}
    {}

    coord_t safety_dist() const noexcept { return m_safety_d; }

    coord_t simplification_tolerance() const override
    {
        return m_simplify_tol;
    }
};

template<class ArrItem>
class ConvexItemConverter : public BasicItemConverter<ArrItem>
{
public:
    using BasicItemConverter<ArrItem>::BasicItemConverter;

    ArrItem convert(const Arrangeable &arrbl, coord_t offs) const override;
};

template<class ArrItem>
class AdvancedItemConverter : public BasicItemConverter<ArrItem>
{
protected:
    virtual ArrItem get_arritem(const Arrangeable &arrbl, coord_t eps) const;

public:
    using BasicItemConverter<ArrItem>::BasicItemConverter;

    ArrItem convert(const Arrangeable &arrbl, coord_t offs) const override;
};

template<class ArrItem>
class BalancedItemConverter : public AdvancedItemConverter<ArrItem>
{
protected:
    ArrItem get_arritem(const Arrangeable &arrbl, coord_t offs) const override;

public:
    using AdvancedItemConverter<ArrItem>::AdvancedItemConverter;
};

template<class ArrItem, class En = void> struct ImbueableItemTraits_
{
    static constexpr const char *Key = "object_id";

    static void imbue_id(ArrItem &itm, const ObjectID &id)
    {
        set_arbitrary_data(itm, Key, id);
    }

    static std::optional<ObjectID> retrieve_id(const ArrItem &itm)
    {
        std::optional<ObjectID> ret;
        auto                    idptr = get_data<const ObjectID>(itm, Key);
        if (idptr)
            ret = *idptr;

        return ret;
    }
};

template<class ArrItem>
using ImbueableItemTraits = ImbueableItemTraits_<StripCVRef<ArrItem>>;

template<class ArrItem>
void imbue_id(ArrItem &itm, const ObjectID &id)
{
    ImbueableItemTraits<ArrItem>::imbue_id(itm, id);
}

template<class ArrItem>
std::optional<ObjectID> retrieve_id(const ArrItem &itm)
{
    return ImbueableItemTraits<ArrItem>::retrieve_id(itm);
}

template<class ArrItem>
bool apply_arrangeitem(const ArrItem &itm, ArrangeableModel &mdl)
{
    bool ret = false;

    if (auto id = retrieve_id(itm)) {
        mdl.visit_arrangeable(*id, [&itm, &ret](Arrangeable &arrbl) {
            if ((ret = arrbl.assign_bed(get_bed_index(itm))))
                arrbl.transform(unscaled(get_translation(itm)), get_rotation(itm));
        });
    }

    return ret;
}

template<class ArrItem>
double get_min_area_bounding_box_rotation(const ArrItem &itm)
{
    return MinAreaBoundigBox{envelope_convex_hull(itm),
                             MinAreaBoundigBox::pcConvex}
        .angle_to_X();
}

template<class ArrItem>
double get_fit_into_bed_rotation(const ArrItem &itm, const RectangleBed &bed)
{
    double ret = 0.;

    auto bbsz = envelope_bounding_box(itm).size();
    auto binbb = bounding_box(bed);
    auto binbbsz = binbb.size();

    if (bbsz.x() >= binbbsz.x() || bbsz.y() >= binbbsz.y())
        ret = fit_into_box_rotation(envelope_convex_hull(itm), binbb);

    return ret;
}

template<class ArrItem>
auto get_corrected_bed(const ExtendedBed &bed,
                       const ArrangeableToItemConverter<ArrItem> &converter)
{
    auto bedcpy = bed;
    visit_bed([tol = -converter.simplification_tolerance()](auto &rawbed) {
        rawbed = offset(rawbed, tol);
    }, bedcpy);

    return bedcpy;
}

}} // namespace Slic3r::arr2

#endif // ARRANGE2_HPP
