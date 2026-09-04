#include "Slic3r/App/InvalidDataDialog.hpp"

#include "Slic3r/App/CustomGCodeMigration.hpp"
#include "Slic3r/App/DisplayStrings.hpp"
#include "Slic3r/App/Navigator.hpp"
#include "Slic3r/App/Yoga/ScrollArea.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/App/Yoga/Rectangle.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"

namespace Slic3r::App {

using Yoga::operator""_fpx;

InvalidDataDialog::InvalidDataDialog(
    Biz::ProjectInteractor& project_interactor,
    Navigator& navigator
) :
    Yoga::Dialog("InvalidDataDialog"),
    m_project_interactor(project_interactor),
    m_navigator(navigator)
{
    set_closable(true);
    content_item()->set_modal(true);
    content_item()->set_width(520_fpx);
    content_item()->set_height(320_fpx);
    append_tab(Biz::_u8L("Invalid settings"));

    content()->set_padding(0);
    content()->set_align_items(YGAlignStretch);
    content()->set_orientation(Yoga::Orientation::Vertical);

    content()->set_padding({0, 10_fpx, 0, 10_fpx});
    m_errors = content()->emplace_back<Yoga::ScrollArea>();
    m_errors->set_orientation(Yoga::Orientation::Vertical);
    m_errors->set_padding(20_fpx);
    m_errors->set_gap(10_fpx);
    m_errors->set_align_items(YGAlignStretch);

    m_project_interactor.status_cache().add_listener<Biz::IStatusCacheChangedListener>(this);
    m_project_interactor.scene_interactor()
        .add_listener<Biz::ISelectedBedInstancesChangedListener>(this);
    m_project_interactor.add_listener<Biz::ISelectedProjectChangedListener>(this);

    callbacks().opened = [this]() { reload(); };

    reload();
}

InvalidDataDialog::~InvalidDataDialog()
{
    m_project_interactor.status_cache().remove_listener<Biz::IStatusCacheChangedListener>(this);
    m_project_interactor.scene_interactor()
        .remove_listener<Biz::ISelectedBedInstancesChangedListener>(this);
    m_project_interactor.remove_listener<Biz::ISelectedProjectChangedListener>(this);
}

void InvalidDataDialog::on_status_cache_status_code_changed(const Domain::SlicingId)
{
    reload();
}

void InvalidDataDialog::on_status_cache_errors_changed(const Domain::SlicingId)
{
    reload();
}

void InvalidDataDialog::on_selected_bed_instances_changed(
    Domain::SelectionId,
    const Biz::Scene::BedSelection&
)
{
    reload();
}

void InvalidDataDialog::on_selected_project_changed(size_t)
{
    reload();
}

void InvalidDataDialog::close_action()
{
    m_navigator.set_opened_dialog(nullptr);
}

static bool contains(const std::string& string, const std::string& substring) {
    return string.find(substring) != std::string::npos;
}

class Error : public Yoga::Rectangle
{
public:
    Error(const Domain::Project& project, Biz::ProjectInteractor& project_interactor, const Biz::Slicing::Error& error) {
        set_flex_shrink(0);
        set_fill(m_theme->color_imgui(
            Platform::Color::WindowBgAlternate,
            Platform::ColorGroup::Disabled));
        set_padding(10_fpx);
        set_align_items(YGAlignStretch);

        if (error.code == Biz::Slicing::ErrorCode::PlaceholderParser) {
            set_orientation(Yoga::Orientation::Vertical);
            set_gap(20_fpx);

            auto title{emplace_back<Yoga::Text>(to_display_string(error.code))};
            title->set_font_type(Render::ImguiFontType::Bold);
            title->set_flex_shrink(0);

            auto errors{std::get<Biz::Slicing::PlaceholderParserErrorPayload>(error.payload)};

            const bool repairable{std::ranges::any_of(
                errors,
                [](const auto& pair)
                {
                    const std::string& error{pair.second};
                    if (contains(error, "scalar is expected")) {
                        return true;
                    }
                    bool renamed{false};
                    for (const auto& [original, _] : get_renames()) {
                        if (contains(error, original)) {
                            renamed = true;
                            break;
                        }
                    }
                    if (contains(error, "Not a variable name") && renamed) {
                        return true;
                    }
                    return false;
                })};

            auto info{emplace_back<Yoga::Text>(Biz::_u8L(
                "See the errors bellow and fix the custom gcode sections in the printer settings."
            ))};
            if (repairable) {
                info->set_text(Biz::_u8L(
                    "This may be caused by loading a 3mf saved from a different version of PrusaSlicer. "
                    "If that is the case, the easiest solution is to choose a system provided printer in the right panel. "
                    "Other features loaded from the 3mf, such as multi material painting, will remain unchanged. "
                    "Alternativelly, if you want keep using the loaded printer profile, "
                    "you may try the custom gcode automatic repair, but it is not guaranteed to work. "
                ));
            }
            info->set_flex_shrink(0);
            info->set_wrap_mode(Yoga::Text::WrapMode::Wrap);

            auto auto_repair_button{emplace_back<Yoga::LayoutButton>("Auto repair")};
            auto_repair_button->set_content_padding({20_fpx, 8_fpx, 20_fpx, 10_fpx});
            auto_repair_button->set_background_color(Platform::Color::AccentPrimary);
            auto_repair_button->set_label_font_type(Render::ImguiFontType::Bold);
            auto_repair_button->set_self_align(YGAlignFlexEnd);
            auto_repair_button->set_visible(repairable);

            std::vector<std::string> sections;
            std::ranges::transform(errors, std::back_inserter(sections), [](const auto& pair){
                return pair.first;
            });

            auto auto_repair_result{emplace_back<Yoga::Item>()};
            auto_repair_result->set_visible(false);
            auto_repair_result->set_flex_shrink(0);

            auto_repair_button->callbacks().action = [&project_interactor, sections, auto_repair_result, auto_repair_button]()
            {
                const auto& selected_preset{
                    project_interactor.selected_config_container().selected_preset()};
                const Domain::ConfigBox& config{selected_preset.printer.config_box()};

                for (const std::string& section : sections) {
                    const Domain::ConfigItem& custom_gcode_item{config.items.opt(section)};
                    const std::string custom_gcode{config.items.opt(section).get<std::string>()};
                    const std::string new_custom_gcode{
                        migrate_custom_gcode(custom_gcode)};

                    auto_repair_result->set_visible(true);
                    auto_repair_button->set_visible(false);

                    auto result{auto_repair_result->emplace_back<Yoga::Text>("")};
                    result->set_wrap_mode(Yoga::Text::WrapMode::Wrap);
                    result->set_font_type(Render::ImguiFontType::Bold);
                    result->set_flex_shrink(0);
                    result->set_flex_grow(1);

                    if (new_custom_gcode == custom_gcode) {
                        result->set_text(
                            "We were unable to peform the auto repair. Please select "
                            "a system preset or edit the custom gcode manually.");
                        return;
                    }
                    result->set_text(
                        "Auto repair performed, please try re-slicing the project.");

                    project_interactor.preset_interactor()
                        .set_item_value(custom_gcode_item, Domain::ConfigValue{new_custom_gcode});
                }
            };

            auto error_items{emplace_back<Yoga::Item>()};
            error_items->set_flex_shrink(0);
            error_items->set_orientation(Yoga::Orientation::Vertical);
            error_items->set_gap(10_fpx);
            error_items->set_padding(10_fpx);

            for (const auto& [section, error_message] : errors) {
                auto single_error{error_items->emplace_back<Yoga::Item>()};
                single_error->set_flex_shrink(0);
                single_error->set_orientation(Yoga::Orientation::Vertical);
                single_error->set_gap(10_fpx);

                auto title{single_error->emplace_back<Yoga::Text>(section)};
                title->set_font_type(Render::ImguiFontType::Bold);

                auto background{single_error->emplace_back<Yoga::Rectangle>()};
                background->set_fill(m_theme->color_imgui(Platform::Color::WindowBgAlternate));
                background->set_orientation(Yoga::Orientation::Vertical);
                background->set_flex_shrink(0);

                auto* scroll{background->emplace_back<Yoga::ScrollArea>("HScrollText")};
                scroll->set_window_flags(ImGuiWindowFlags_HorizontalScrollbar);
                scroll->set_remap_horizontal_scroll(true);
                scroll->set_flex_shrink(0);
                scroll->set_height(100_fpx);
                scroll->set_padding({20_fpx, 0, 20_fpx, 0});
                scroll->set_align_items(YGAlignCenter);

                auto* text{scroll->emplace_back<Yoga::Text>(error_message)};
                text->set_wrap_mode(Yoga::Text::WrapMode::NoWrap);
                text->set_flex_shrink(0.f);
            }
        } else {
            auto text{emplace_back<Yoga::Text>(to_display_string(error, project))};
            text->set_flex_shrink(0);
        }
    }
};

void InvalidDataDialog::reload()
{
    if (!opened()) {
        return;
    }

    while (!m_errors->items().empty()) {
        m_errors->remove(m_errors->items().back());
    }

    const Domain::SelectionId project_id{m_project_interactor.selected_project_id()};
    if (!m_project_interactor.project_exists(project_id)) {
        close();
        return;
    }

    const Domain::Project& project{m_project_interactor.workbench().project(project_id)};
    const Domain::BedRef bed_instance{
        m_project_interactor.scene_interactor().bed_selection().last_selected_bed()};

    const Domain::SlicingId slicing_id{project_id, bed_instance.instance_id};
    const auto status = m_project_interactor.status_cache().get_status(slicing_id);
    if (!status || status->code != Biz::Slicing::StatusCode::InvalidData) {
        close();
        return;
    }

    if (status->errors.empty()) {
        close();
        return;
    }

    for (const Biz::Slicing::Error& error : status->errors) {
        m_errors->emplace_back<Error>(project, m_project_interactor, error);
    }
}

} // namespace Slic3r::App
