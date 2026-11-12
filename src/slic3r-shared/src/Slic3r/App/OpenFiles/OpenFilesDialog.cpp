#include "Slic3r/App/OpenFiles/OpenFilesDialog.hpp"

#include "Slic3r/App/Navigator.hpp"
#include "Slic3r/App/Theme.hpp"
#include "Slic3r/App/Yoga/ButtonGroup.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/Rectangle.hpp"
#include "Slic3r/App/Yoga/ScrollArea.hpp"
#include "Slic3r/App/Yoga/Separator.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/ToggleButton.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

#include <boost/algorithm/string/case_conv.hpp>
#include <boost/filesystem/path.hpp>
#include <fmt/format.h>

#include <algorithm>
#include <functional>
#include <set>

using namespace Slic3r::App::Yoga;
using Slic3r::Biz::FileOpen::ArchiveFile;
using Slic3r::Biz::FileOpen::ArchiveSelection;

namespace Slic3r::App {

namespace {

constexpr Unit CARET_COLUMN_WIDTH  = 22_fpx;
constexpr Unit SWITCH_COLUMN_WIDTH = 34_fpx;
constexpr Unit COLUMN_GAP          = 8_fpx;
constexpr Unit TEXT_HEIGHT         = 20_fpx;
constexpr Unit SCROLLBAR_WIDTH     = 16_fpx;

enum class SectionType
{
    Projects,
    Models,
    Skipped
};

using Changed = std::function<void()>;

ImColor dim_text_color(const Theme* theme)
{
    return theme->color_imgui(Platform::Color::Text, Platform::ColorGroup::Disabled);
}

std::string extension_label(const std::string& name)
{
    std::string extension = boost::filesystem::path(name).extension().string();
    if (extension.empty()) {
        return "?";
    }
    return boost::algorithm::to_upper_copy(extension.substr(1, 3));
}

Item* add_fixed_cell(Item* parent, const Unit& width)
{
    Item* cell = parent->emplace_back<Item>();
    cell->set_width(width);
    cell->set_flex_shrink(0);
    cell->set_align_items(YGAlignCenter);
    return cell;
}

/**
 * @brief Text buttons of which at most one is checked, none when the value is mixed.
 */
class TextSegments : public Rectangle
{
public:
    using Picked = std::function<void(std::size_t)>;

    TextSegments(const std::vector<std::string>& labels, Picked picked)
    {
        set_orientation(Orientation::Horizontal);
        set_fill(m_theme->color_imgui(Platform::Color::WindowBg, Platform::ColorGroup::Default));
        set_rounding(4.f);
        set_padding(2_fpx);
        set_gap(2_fpx);
        set_flex_shrink(0);

        m_group.set_always_checked(false);
        for (const std::string& label : labels) {
            LayoutButton* button = emplace_back<LayoutButton>(label);
            button->set_checkable(true);
            button->set_background_color(Platform::Color::ButtonTransparent);
            button->set_background_color_checked(Platform::Color::Button);
            m_group.insert_button(button);
            m_buttons.push_back(button);
        }
        m_group.callbacks().action = [this, picked = std::move(picked)](AbstractButton* button) {
            const auto it = std::find(m_buttons.begin(), m_buttons.end(), button);
            if (it != m_buttons.end()) {
                picked(static_cast<std::size_t>(it - m_buttons.begin()));
            }
        };
    }

    void set_value(std::optional<std::size_t> value)
    {
        if (value) {
            m_buttons.at(*value)->set_checked(true);
        } else if (AbstractButton* checked = m_group.checked_button()) {
            checked->set_checked(false);
        }
    }

private:
    ButtonGroup m_group;
    std::vector<LayoutButton*> m_buttons;
};

class TypeTile : public Rectangle
{
public:
    TypeTile(const std::string& name, SectionType type)
    {
        struct Colors
        {
            ImColor fill;
            ImColor text;
        };
        const Colors colors = type == SectionType::Projects ? Colors{ImColor(58, 38, 24), ImColor(240, 138, 75)}
            : type == SectionType::Models                   ? Colors{ImColor(29, 42, 64), ImColor(125, 162, 239)}
                                                            : Colors{ImColor(38, 38, 38), ImColor(122, 122, 122)};
        set_width(32_fpx);
        set_height(22_fpx);
        set_flex_shrink(0);
        set_rounding(4.f);
        set_fill(colors.fill);
        set_disabled_fill(colors.fill);
        set_justify_content(YGJustifyCenter);
        set_align_items(YGAlignCenter);

        Text* label = emplace_back<Text>(extension_label(name), Render::ImguiFontType::Bold);
        label->set_text_color(colors.text);
    }
};

std::string skipped_summary(const std::vector<ArchiveFile>& files, const std::vector<std::size_t>& indices)
{
    std::set<std::string> reasons;
    for (std::size_t index : indices) {
        if (files[index].skip_reason) {
            reasons.insert(skip_reason_text(*files[index].skip_reason));
        }
    }
    std::string result = fmt::format(fmt::runtime(Biz::_u8L("Files: {}")), indices.size());
    for (const std::string& reason : reasons) {
        result += " · " + reason;
    }
    return result;
}

} // namespace

class OpenFileRow : public Item
{
public:
    OpenFileRow(std::size_t index, SectionType type, OpenFilesDialogModel& model, const Changed& changed) :
        m_index(index),
        m_model(model)
    {
        const ArchiveFile& item = model.files()[index];
        const std::string display_name = Biz::FileOpen::sanitized_file_name(item.name);

        set_orientation(Orientation::Horizontal);
        set_align_items(YGAlignCenter);
        set_padding({2_fpx, 4_fpx});
        set_gap(COLUMN_GAP);
        set_flex_shrink(0);

        add_fixed_cell(this, CARET_COLUMN_WIDTH);

        Item* switch_cell = add_fixed_cell(this, SWITCH_COLUMN_WIDTH);
        if (type != SectionType::Skipped) {
            m_switch = switch_cell->emplace_back<ToggleButton>();
            m_switch->callbacks().action = [this, changed] {
                m_model.set_selected(m_index, m_switch->checked());
                changed();
            };
        }

        emplace_back<TypeTile>(display_name, type);

        Item* info = emplace_back<Item>();
        info->set_orientation(Orientation::Vertical);
        info->set_flex_grow(1);
        info->set_flex_shrink(1);
        info->set_min_width(0_fpx);

        m_name = info->emplace_back<Text>(display_name);
        m_name->set_wrap_mode(Text::WrapMode::WrapElide);
        m_name->set_height(TEXT_HEIGHT);

        std::string meta = format_file_size(item.size);
        if (item.skip_reason) {
            meta += " · " + skip_reason_text(*item.skip_reason);
        } else if (item.is_project && !item.has_config) {
            meta += " · " + Biz::_u8L("Not a PrusaSlicer project");
        }
        Text* meta_text = info->emplace_back<Text>(meta);
        meta_text->set_wrap_mode(Text::WrapMode::WrapElide);
        meta_text->set_height(TEXT_HEIGHT);
        meta_text->set_text_color(dim_text_color(m_theme));
        meta_text->set_visible(!meta.empty());

        if (type == SectionType::Projects) {
            m_mode = emplace_back<TextSegments>(
                std::vector<std::string>{Biz::_u8L("Project"), Biz::_u8L("Geometry")},
                [this, changed](std::size_t picked) {
                    m_model.set_as_project(m_index, picked == 0);
                    changed();
                }
            );
        }

        if (type == SectionType::Skipped) {
            m_name->set_text_color(dim_text_color(m_theme));
        }
        refresh();
    }

    void refresh()
    {
        if (!m_switch) {
            return;
        }
        const bool selected = m_model.selected(m_index);
        m_switch->set_checked(selected);
        m_name->set_text_color(
            selected ? m_theme->color_imgui(Platform::Color::Text, Platform::ColorGroup::Default)
                     : dim_text_color(m_theme)
        );
        if (m_mode) {
            m_mode->set_value(m_model.as_project(m_index) ? 0 : 1);
            m_mode->set_enabled(selected);
        }
    }

private:
    std::size_t m_index;
    OpenFilesDialogModel& m_model;
    ToggleButton* m_switch{nullptr};
    Text* m_name{nullptr};
    TextSegments* m_mode{nullptr};
};

/**
 * @brief Collapsible group of rows with a switch for all of them and a shared option.
 */
class OpenFilesSection : public Item
{
public:
    OpenFilesSection(
        const std::string& title,
        SectionType type,
        std::vector<std::size_t> indices,
        OpenFilesDialogModel& model,
        const Changed& changed
    ) :
        m_type(type),
        m_indices(std::move(indices)),
        m_model(model)
    {
        set_orientation(Orientation::Vertical);
        set_flex_shrink(0);

        Item* header = emplace_back<Item>();
        header->set_orientation(Orientation::Horizontal);
        header->set_align_items(YGAlignCenter);
        header->set_padding({2_fpx, 10_fpx});
        header->set_gap(COLUMN_GAP);

        m_caret = header->emplace_back<LayoutButton>(std::string{}, Render::Icon::CaretRight);
        m_caret->set_width(CARET_COLUMN_WIDTH);
        m_caret->set_height(CARET_COLUMN_WIDTH);
        m_caret->set_flex_shrink(0);
        m_caret->callbacks().action = [this] { set_expanded(!m_expanded); };

        Item* switch_cell = add_fixed_cell(header, SWITCH_COLUMN_WIDTH);
        if (type != SectionType::Skipped) {
            m_master = switch_cell->emplace_back<ToggleButton>();
            m_master->callbacks().action = [this, changed] {
                for (std::size_t index : m_indices) {
                    m_model.set_selected(index, m_master->checked());
                }
                changed();
            };
        }

        AbstractButton* title_button = header->emplace_back<AbstractButton>();
        title_button->set_orientation(Orientation::Vertical);
        title_button->set_flex_grow(1);
        title_button->set_flex_shrink(1);
        title_button->set_min_width(0_fpx);
        title_button->set_cursor(ImGuiMouseCursor_Hand);
        title_button->callbacks().action = [this] { set_expanded(!m_expanded); };

        Text* title_text = title_button->emplace_back<Text>(title, Render::ImguiFontType::Bold);
        title_text->set_height(TEXT_HEIGHT);
        m_summary = title_button->emplace_back<Text>(std::string{});
        m_summary->set_wrap_mode(Text::WrapMode::WrapElide);
        m_summary->set_height(TEXT_HEIGHT);
        m_summary->set_text_color(dim_text_color(m_theme));

        if (type == SectionType::Projects) {
            m_option = header->emplace_back<TextSegments>(
                std::vector<std::string>{Biz::_u8L("Project"), Biz::_u8L("Geometry")},
                [this, changed](std::size_t picked) {
                    for (std::size_t index : m_indices) {
                        m_model.set_as_project(index, picked == 0);
                    }
                    changed();
                }
            );
        } else if (type == SectionType::Models) {
            m_option = header->emplace_back<TextSegments>(
                std::vector<std::string>{Biz::_u8L("Current project"), Biz::_u8L("New project")},
                [this, changed](std::size_t picked) {
                    m_model.set_models_to_new_project(picked == 1);
                    changed();
                }
            );
        }

        m_rows_container = emplace_back<Item>();
        m_rows_container->set_orientation(Orientation::Vertical);
        m_rows_container->set_padding({0_fpx, 0_fpx, 0_fpx, 8_fpx});
        m_rows_container->set_visible(false);
        for (std::size_t index : m_indices) {
            m_rows.push_back(m_rows_container->emplace_back<OpenFileRow>(index, type, model, changed));
        }

        if (type == SectionType::Skipped) {
            m_summary->set_text(skipped_summary(model.files(), m_indices));
            title_text->set_text_color(dim_text_color(m_theme));
        }
        refresh();
    }

    void refresh()
    {
        for (OpenFileRow* row : m_rows) {
            row->refresh();
        }
        if (m_type == SectionType::Skipped) {
            return;
        }

        const std::size_t total = m_indices.size();
        const std::size_t selected = std::count_if(m_indices.begin(), m_indices.end(), [this](std::size_t index) {
            return m_model.selected(index);
        });
        m_summary->set_text(
            selected == 0       ? Biz::_u8L("None selected")
            : selected == total ? fmt::format(fmt::runtime(Biz::_u8L("All {}")), total)
                                : fmt::format(fmt::runtime(Biz::_u8L("{} of {}")), selected, total)
        );
        m_master->set_checked(selected == total);
        m_master->set_third_state(selected != 0 && selected != total);

        if (m_type == SectionType::Projects) {
            const std::size_t as_project = std::count_if(m_indices.begin(), m_indices.end(), [this](std::size_t index) {
                return m_model.as_project(index);
            });
            m_option->set_value(
                as_project == total ? std::optional<std::size_t>{0}
                : as_project == 0   ? std::optional<std::size_t>{1}
                                    : std::nullopt
            );
            m_option->set_enabled(selected > 0);
        } else {
            m_option->set_value(m_model.models_to_new_project() ? 1 : 0);
            m_option->set_enabled(m_model.imports_models());
        }
    }

private:
    void set_expanded(bool expanded)
    {
        m_expanded = expanded;
        m_rows_container->set_visible(expanded);
        m_caret->set_icon(expanded ? Render::Icon::CaretDown : Render::Icon::CaretRight);
    }

    SectionType m_type;
    std::vector<std::size_t> m_indices;
    OpenFilesDialogModel& m_model;
    bool m_expanded{false};

    LayoutButton* m_caret{nullptr};
    ToggleButton* m_master{nullptr};
    Text* m_summary{nullptr};
    TextSegments* m_option{nullptr};
    Item* m_rows_container{nullptr};
    std::vector<OpenFileRow*> m_rows;
};

namespace {

std::string archive_names(const std::vector<ArchiveFile>& files)
{
    std::set<std::string> names;
    for (const ArchiveFile& file : files) {
        names.insert(file.archive.filename().string());
    }
    std::string result;
    for (const std::string& name : names) {
        result += result.empty() ? name : ", " + name;
    }
    return result;
}

std::vector<std::size_t> section_indices(const OpenFilesDialogModel& model, SectionType type)
{
    const std::vector<ArchiveFile>& files = model.files();
    std::vector<std::size_t> indices;
    for (std::size_t index = 0; index < files.size(); ++index) {
        const SectionType item_type = !model.selectable(index) ? SectionType::Skipped
            : files[index].is_project                          ? SectionType::Projects
                                                               : SectionType::Models;
        if (item_type == type) {
            indices.push_back(index);
        }
    }
    return indices;
}

} // namespace

OpenFilesDialog::OpenFilesDialog(
    std::vector<ArchiveFile> files,
    Biz::FileOpen::OpenFilesOptions options,
    Done done,
    Navigator& navigator
) :
    Dialog({Biz::_u8L("Open from archive")}, "OpenFilesDialog"),
    m_model(std::move(files), options),
    m_done(std::move(done)),
    m_navigator(navigator)
{
    content_item()->set_modal(true);
    content_item()->set_width(640_fpx);

    content()->set_orientation(Orientation::Vertical);
    content()->set_gap(5_fpx);

    Text* subtitle = content()->emplace_back<Text>(archive_names(m_model.files()), Render::ImguiFontType::Italic);
    subtitle->set_wrap_mode(Text::WrapMode::WrapElide);
    subtitle->set_height(TEXT_HEIGHT);
    subtitle->set_text_color(dim_text_color(m_theme));

    add_separator();

    ScrollArea* scroll_area = content()->emplace_back<ScrollArea>();
    scroll_area->set_orientation(Orientation::Vertical);
    scroll_area->set_height(400_fpx);
    scroll_area->set_padding(Paddings{0, 0, SCROLLBAR_WIDTH, 0});

    const auto add_section = [&](const std::string& title, SectionType type) {
        std::vector<std::size_t> indices = section_indices(m_model, type);
        if (indices.empty()) {
            return;
        }
        if (!m_sections.empty()) {
            scroll_area->emplace_back<Separator>();
        }
        m_sections.push_back(
            scroll_area->emplace_back<OpenFilesSection>(title, type, std::move(indices), m_model, [this] {
                update_controls();
            })
        );
    };
    add_section(Biz::_u8L("Projects"), SectionType::Projects);
    add_section(Biz::_u8L("Models"), SectionType::Models);
    add_section(Biz::_u8L("Skipped"), SectionType::Skipped);

    add_separator();

    Item* controls = content()->emplace_back<Item>();
    controls->set_orientation(Orientation::Horizontal);
    controls->set_padding(5_fpx);
    controls->set_gap(5_fpx);

    Item* spacer = controls->emplace_back<Item>();
    spacer->set_flex_grow(1);

    LayoutButton* cancel_button = controls->emplace_back<LayoutButton>(Biz::_u8L("Cancel"));
    cancel_button->callbacks().action = [this] { close_action(); };

    m_open_button = controls->emplace_back<LayoutButton>(std::string{});
    m_open_button->set_background_color(Platform::Color::AccentPrimary);
    m_open_button->set_label_font_type(Render::ImguiFontType::Bold);
    m_open_button->callbacks().action = [this] { answer(m_model.to_selection()); };

    update_controls();
}

OpenFilesDialog::~OpenFilesDialog()
{
    if (m_done) {
        Done done = std::move(m_done);
        m_done    = nullptr;
        done(std::nullopt);
    }
}

void OpenFilesDialog::close_action()
{
    answer(std::nullopt);
}

void OpenFilesDialog::update_controls()
{
    for (OpenFilesSection* section : m_sections) {
        section->refresh();
    }
    const std::size_t count = m_model.selected_count();
    m_open_button->set_label(fmt::format(fmt::runtime(Biz::_u8L("Open ({})")), count));
    m_open_button->set_enabled(count > 0);
}

void OpenFilesDialog::answer(std::optional<ArchiveSelection> selection)
{
    if (!m_done) {
        return;
    }
    Done done = std::move(m_done);
    m_done    = nullptr;
    m_navigator.set_modal_dialog(ModalDialog::None);
    done(std::move(selection));
}

} // namespace Slic3r::App
