#include "Slic3r/App/WipeTowerSettings.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/App/Plater/PlaterGizmosHelper.hpp"
#include "Slic3r/Math.hpp"

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {
WipeTowerSettings::WipeTowerSettings(Biz::ProjectInteractor& project_interactor) :
    m_project_interactor{project_interactor},
    m_scene_interactor{project_interactor.scene_interactor()},
    m_workbench{project_interactor.workbench()},
    m_scene_changed_listener_scope{m_scene_interactor, *this}
{
    set_orientation(Yoga::Orientation::Vertical);
    set_gap(5_fpx);

    auto position_label{emplace_back<Yoga::Text>("Position")};
    position_label->set_font_type(Render::ImguiFontType::Bold);
    m_position_input = emplace_back<Plater::TripleInput>(Biz::_u8L("mm"));
    m_position_input->set_visible({true, true, false});

    m_position_input->on_change = [&](const Domain::Vec3d& position, int index)
    {
        ASSERT(index < 2);
        const Domain::BedInstance& bed_instance{get_bed_instance()};
        const Domain::ModelWipeTower& wipe_tower{bed_instance.wipe_tower};
        Domain::Transform3d transform{Domain::Transform3d::Identity()};
        transform.translate(
            position - Domain::Vec3d{wipe_tower.position.x(), wipe_tower.position.y(), 0.0}
        );
        m_scene_interactor.transform_selection(transform.matrix());
    };

    auto rotation_label{emplace_back<Yoga::Text>("Rotation")};
    rotation_label->set_font_type(Render::ImguiFontType::Bold);
    m_rotation_input = emplace_back<Plater::TripleInput>(Biz::_u8L("°"));
    m_rotation_input->set_visible({false, false, true});

    m_rotation_input->on_change = [&](const Domain::Vec3d& rotation, int index)
    {
        ASSERT(index == 2);
        const Domain::BedInstance& bed_instance{get_bed_instance()};
        const Domain::ModelWipeTower& wipe_tower{bed_instance.wipe_tower};

        const double angle{
            Slic3r::angle_to_0_2PI(Slic3r::deg2rad(rotation.z() - wipe_tower.rotation))
        };

        const Domain::SquareMatrix4d& bed_trafo{bed_instance.transformation.get_matrix().matrix()};

        const Domain::SquareMatrix4d rotation_matrix{Plater::get_rotation_matrix(
            Domain::SquareMatrix3d::Identity(),
            Domain::Vec3d{wipe_tower.position.x(), wipe_tower.position.y(), 0.0},
            Domain::Vec3d{0.0, 0.0, angle}
        )};

        m_scene_interactor.transform_selection(bed_trafo * rotation_matrix * bed_trafo.inverse());
    };

    reload();
}

void WipeTowerSettings::on_wipe_tower_moved(Domain::SlicingId slicing_id)
{
    const Domain::BedRef bed_ref{m_scene_interactor.bed_selection().last_selected_bed()};
    const std::size_t project_id{m_project_interactor.selected_project_id()};
    if (project_id != slicing_id.project_id) {
        return;
    }
    if (bed_ref.instance_id != slicing_id.bed_instance_id) {
        return;
    }
    reload();
}

void WipeTowerSettings::reload()
{
    const Domain::BedInstance& bed_instance{get_bed_instance()};
    const Domain::ModelWipeTower& wipe_tower{bed_instance.wipe_tower};
    m_position_input->set_value(
        Domain::Vec3d{wipe_tower.position.x(), wipe_tower.position.y(), 0.0}
    );
    m_rotation_input->set_value(Domain::Vec3d{0.0, 0.0, wipe_tower.rotation});
}

const Domain::BedInstance& WipeTowerSettings::get_bed_instance() const
{
    const Domain::BedRef bed_ref{m_scene_interactor.bed_selection().last_selected_bed()};
    const std::size_t project_id{m_project_interactor.selected_project_id()};

    const Domain::Project& project{m_workbench.project(project_id)};
    const Domain::ConfigContainer* config_container{
        project.find_config_container(bed_ref.config_container_id)
    };
    ASSERT(config_container);

    const Domain::BedInstance& bed_instance{
        config_container->find_bed_instance(bed_ref.instance_id)
    };
    return bed_instance;
}
} // namespace Slic3r::App
