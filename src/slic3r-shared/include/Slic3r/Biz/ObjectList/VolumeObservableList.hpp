#pragma once

#include "Slic3r/Biz/ObservableList.hpp"

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::Domain {
enum class ModelVolumeType : int;
} // namespace Slic3r::Domain

namespace Slic3r::Biz::ObjectList {

struct Volume
{
    size_t obj_id;
    size_t vol_id;
    Domain::ModelVolumeType type;
    std::string name = std::string{};
    bool is_text{false};
    bool is_svg{false};
    bool selected{false};
    std::optional<size_t> config_container_id;
};

class VolumeObservableList : public Biz::IObservableList<Volume>
{
public:
    VolumeObservableList(
        Biz::ProjectInteractor& project_interactor,
        size_t project_id,
        size_t object_id,
        std::optional<size_t> config_container_id
    );

    const Volume& at(size_t index) const override;

    size_t size() const override;

    void update_data();

    void refresh_name(size_t volume_id);

private:
    std::vector<Volume> m_items;

    Biz::ProjectInteractor& m_project_interactor;
    size_t m_project_id;
    size_t m_object_id;
    std::optional<size_t> m_config_container_id;
};

} // namespace Slic3r::Biz::ObjectList
