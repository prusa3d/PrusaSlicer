#pragma once

#include "Slic3r/Biz/ObservableList.hpp"

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::Biz::ObjectList {

struct Instance
{
    size_t id;
    size_t ordinal_id;
    size_t object_id;
    bool printable;
    bool selected{false};
};

class InstanceObservableList : public Biz::IObservableList<Instance>
{
public:
    InstanceObservableList(
        Biz::ProjectInteractor& project_interactor,
        size_t project_id,
        std::optional<size_t> bed_id,
        size_t object_id
    );

    const Instance& at(size_t index) const override;

    size_t size() const override;

    void update_data();

private:
    std::vector<Instance> m_items;

    Biz::ProjectInteractor& m_project_interactor;
    size_t m_project_id;
    size_t m_object_id;
    std::optional<size_t> m_bed_id;
};

} // namespace Slic3r::Biz::ObjectList
