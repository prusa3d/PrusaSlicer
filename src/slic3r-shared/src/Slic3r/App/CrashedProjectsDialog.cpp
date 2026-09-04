#include "Slic3r/App/CrashedProjectsDialog.hpp"

#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

#include "Slic3r/App/Navigator.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/ScrollArea.hpp"
#include "Slic3r/App/WarningPanel.hpp"
#include "Slic3r/App/Yoga/ToggleButton.hpp"
#include "Slic3r/App/Yoga/Separator.hpp"

#include <iomanip>
#include <fmt/format.h>

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

[[nodiscard]] std::string to_local_date(std::time_t time)
{
    const std::tm* local_time = std::localtime(&time);
    if (!local_time) {
        // failed to convert time
        return std::string{};
    }

    std::ostringstream stream;
    stream.imbue(std::locale("")); // Locale from the user's environment
    stream << std::put_time(local_time, "%x %T");

    if (!stream) {
        // failed to format date
        return std::string{};
    }

    return stream.str();
}

std::optional<std::string_view> extract_name(std::string_view s)
{
    const auto first  = s.find('_');
    const auto second = s.find('_', first + 1);

    if (first == s.npos || second == s.npos || !s.ends_with(".3mf"))
        return std::nullopt;

    return s.substr(second + 1, s.size() - second - 1 - 4);
}

class CrashedProjectEntry : public Item
{
public:
    using SelectionChangedFn = std::function<void()>;

    CrashedProjectEntry(
        const SelectionChangedFn& selection_changed_fn,
        const boost::filesystem::path& path
    ) :
        m_path(path)
    {
        set_orientation(Orientation::Horizontal);
        set_padding({10_fpx, 5_fpx});
        set_gap(5_fpx);

        Item* switch_cell = emplace_back<Item>();
        switch_cell->set_width(70_fpx);
        switch_cell->set_justify_content(YGJustifyCenter);
        switch_cell->set_align_items(YGAlignCenter);
        m_recover_switch = switch_cell->emplace_back<ToggleButton>(
            std::string{},
            Biz::_u8L("If turned on, this project will be attempted to be recovered")
        );
        m_recover_switch->callbacks().checked_changed = [=](bool checked)
        { selection_changed_fn(); };

        Item* column = emplace_back<Item>();
        column->set_orientation(Orientation::Vertical);
        column->set_gap(5_fpx);
        column->set_flex_grow(1);

        const std::string filename           = m_path.filename().string();
        std::optional<std::string_view> name = extract_name(filename);

        column->emplace_back<Text>(
            std::string(name.value_or(filename)),
            Render::ImguiFontType::Bold
        );
        column->emplace_back<Text>(
            to_local_date(boost::filesystem::last_write_time(m_path))
                + " "
                + (name.has_value() ? m_path.filename().string() : std::string{}),
            Render::ImguiFontType::Italic
        );
    }

    bool recover_checked() const
    {
        return m_recover_switch->checked();
    }

    const boost::filesystem::path& path() const
    {
        return m_path;
    }

private:
    ToggleButton* m_recover_switch{nullptr};
    const boost::filesystem::path m_path;
};

CrashedProjectsDialog::CrashedProjectsDialog(
    Biz::ProjectInteractor& project_interactor,
    Navigator& navigator
) :
    Dialog({Biz::_u8L("Project recovery")}, "CrashedProjectsDialog"),
    m_project_interactor(project_interactor),
    m_navigator(navigator),
    m_backup_store_listener_scope(project_interactor.backup_store(), *this)
{
    set_closable(false);

    content_item()->set_modal(true);
    content_item()->set_width(500_fpx);

    content()->set_orientation(Orientation::Vertical);
    content()->set_gap(5_fpx);

    WarningPanel* panel = content()->emplace_back<WarningPanel>(Platform::Color::Warning);

    panel->set_warning(
        Biz::_u8L("Restore project files"),
        Biz::_u8L(
            "PrusaSlicer was closed incorrectly and unsaved projects were found.\n"
            "Select if you want to attempt a recovery or you want to discard them."
        )
    );

    Item* header = content()->emplace_back<Item>();
    header->set_padding({10_fpx, 0});
    header->set_margin({0, 10_fpx, 0, 0});
    Text* label_recover = header->emplace_back<Text>(Biz::_u8L("Recover"));
    label_recover->set_align({AlignH::Center, AlignV::Center});
    label_recover->set_width(70_fpx);
    content()->emplace_back<Separator>(Orientation::Vertical);
    Text* label_project = header->emplace_back<Text>(Biz::_u8L("Project"));
    label_project->set_flex_grow(1);

    add_separator();

    m_scroll_area = content()->emplace_back<ScrollArea>();
    m_scroll_area->set_orientation(Orientation::Vertical);
    m_scroll_area->set_gap(5_fpx);
    m_scroll_area->set_height(325_fpx);
    m_scroll_area->set_padding(Paddings{0, 0, 12, 0});

    add_separator();

    Item* controls = content()->emplace_back<Item>();
    controls->set_padding(5_fpx);
    controls->set_justify_content(YGJustifyFlexEnd);

    m_recover_selected_button = controls->emplace_back<LayoutButton>(std::string{});
    m_recover_selected_button->set_padding({5_fpx, 3_fpx});
    m_recover_selected_button->callbacks().action = [this]
    {
        std::vector<std::pair<boost::filesystem::path, bool>> paths;
        paths.reserve(m_projects.size());
        std::ranges::transform(
            m_projects,
            std::back_inserter(paths),
            [](CrashedProjectEntry* entry) -> std::pair<boost::filesystem::path, bool>
            { return {entry->path(), entry->recover_checked()}; }
        );

        m_project_interactor.backup_store().restore_backups(paths);
    };
}

void CrashedProjectsDialog::on_crashed_projects_detected(
    const std::vector<boost::filesystem::path>& crashed_projects
)
{
    if (crashed_projects.empty()) {
        return;
    }

    for (const boost::filesystem::path& project : crashed_projects) {
        m_projects.push_back(m_scroll_area->emplace_back<CrashedProjectEntry>(
            [this] { update_button_label(); },
            project
        ));
    }

    update_button_label();
    m_navigator.set_modal_dialog(ModalDialog::CrashedProjects);
}

void CrashedProjectsDialog::on_project_restore_completed()
{
    m_navigator.set_modal_dialog(ModalDialog::None);
}

void CrashedProjectsDialog::update_button_label()
{
    size_t to_recover = 0;
    size_t to_discard = 0;

    for (CrashedProjectEntry* project : std::as_const(m_projects)) {
        if (project->recover_checked()) {
            to_recover++;
        } else {
            to_discard++;
        }
    }

    if (to_recover == 0) {
        m_recover_selected_button->set_label(Biz::_u8L("Discard all projects"));
        m_recover_selected_button->set_background_color(Platform::Color::Error);
    } else if (to_discard == 0) {
        m_recover_selected_button->set_label(Biz::_u8L("Recover all projects"));
        m_recover_selected_button->set_background_color(Platform::Color::AccentSecondary);
    } else {
        m_recover_selected_button->set_label(
            fmt::format(
                fmt::runtime(Biz::_u8L("Recover {} discard {} projects")),
                to_recover,
                to_discard
            )
        );
        m_recover_selected_button->set_background_color(Platform::Color::Button);
    }
}

} // namespace Slic3r::App
