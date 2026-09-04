#include "Slic3r/App/Config/ConfigItemSubstitutions.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/IConfigBoxSetter.hpp"

#include "Slic3r/App/Yoga/InputTextField.hpp"
#include "Slic3r/App/Yoga/ToggleButton.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

ConfigItemSubstitutions::ConfigItemSubstitutions(
    size_t index,
    const Domain::ConfigItem& data,
    Biz::IConfigBoxSetter& cb_setter,
    std::vector<size_t> cbi_index
) :
    ConfigItemControl(index, data, cb_setter, cbi_index)
{
    set_orientation(Orientation::Vertical);
    set_gap(5);

    Item* control_row = emplace_back<Item>();
    control_row->set_orientation(Orientation::Horizontal);
    control_row->set_gap(5);

    LayoutButton* button_add = control_row->emplace_back<LayoutButton>(
        Biz::_u8L("Add"),
        Render::Icon::Plus,
        Biz::_u8L("Add new substitution")
    );
    button_add->callbacks().action = [this] { add_substitution(); };
    m_button_remove_all            = control_row->emplace_back<LayoutButton>(
        Biz::_u8L("Remove All"),
        Render::Icon::Minus,
        Biz::_u8L("Remove all substitutions")
    );
    m_button_remove_all->callbacks().action = [this] { clear_substitutions(); };

    m_list_view = emplace_back<SubstitutionListView>(SubstitutionListViewFactory{this});
    m_list_view->set_orientation(Orientation::Vertical);
    m_list_view->set_source_list(this);
}

ConfigItemSubstitutions::~ConfigItemSubstitutions()
{
    m_list_view->set_source_list(nullptr, true);
}

void ConfigItemSubstitutions::set_field(size_t index, Field field, const std::string& value)
{
    ASSERT(index < m_substitutions.size(), "Substitution index does not exists");
    std::vector<Substitution> substitutions = m_substitutions;

    Substitution& edited_row = substitutions.at(index);
    switch (field) {
    case Field::Find:
        edited_row.find = value;
        break;
    case Field::Replace:
        edited_row.replace = value;
        break;
    case Field::Notes:
        edited_row.notes = value;
        break;
    }

    send_data(substitutions);
}

void ConfigItemSubstitutions::set_option(size_t index, Option option, bool value)
{
    ASSERT(index < m_substitutions.size(), "Substitution index does not exists");
    std::vector<Substitution> substitutions = m_substitutions;

    Substitution& edited_row = substitutions.at(index);
    switch (option) {
    case Option::RegExp:
        edited_row.regexp = value;
        break;
    case Option::CaseInsensitive:
        edited_row.case_insensitive = value;
        break;
    case Option::SingleLine:
        edited_row.single_line = value;
        break;
    case Option::WholeWord:
        edited_row.whole_word = value;
        break;
    }

    send_data(substitutions);
}

void ConfigItemSubstitutions::remove_substitution(size_t index)
{
    ASSERT(index < m_substitutions.size(), "Substitution index does not exists");
    std::vector<Substitution> substitutions = m_substitutions;
    substitutions.erase(substitutions.begin() + index);

    send_data(substitutions);
}

const Substitution& ConfigItemSubstitutions::at(size_t index) const
{
    return m_substitutions.at(index);
}

size_t ConfigItemSubstitutions::size() const
{
    return m_substitutions.size();
}

void ConfigItemSubstitutions::on_data_update()
{
    // deserializ vector of strings
    const std::vector<std::string> data = m_state->value().get<std::vector<std::string>>();
    const size_t count                  = data.size() / 4;
    if (count != m_substitutions.size()) {
        // reset whole table
        invoke_listeners<Biz::IListObserver<Substitution>>([&](Biz::IListObserver<Substitution>* l)
                                                           { l->on_will_be_reset(); });

        m_substitutions.clear();
        m_substitutions.reserve(data.size() / 4);

        for (size_t index = 0; index < data.size(); index += 4) {
            m_substitutions.emplace_back(
                data.at(index + 0),
                data.at(index + 1),
                data.at(index + 2),
                data.at(index + 3)
            );
        }

        invoke_listeners<Biz::IListObserver<Substitution>>([&](Biz::IListObserver<Substitution>* l)
                                                           { l->on_reset(); });
    } else if (!m_substitutions.empty()) {
        // only update data (if there are any)
        for (size_t index = 0, row = 0; index < data.size(); index += 4, ++row) {
            Substitution& substitution = m_substitutions.at(row);
            substitution.find          = data.at(index + 0);
            substitution.replace       = data.at(index + 1);
            substitution.notes         = data.at(index + 3);
            substitution.load_options(data.at(index + 2));
        }
        invoke_listeners<Biz::IListObserver<Substitution>>(
            [&](Biz::IListObserver<Substitution>* l)
            { l->on_updated({0, m_substitutions.size() - 1}); }
        );
    }
    m_button_remove_all->set_visible(count);
}

void ConfigItemSubstitutions::send_data(const std::vector<Substitution>& substitutions)
{
    // serialize to vector of strings
    std::vector<std::string> data;
    data.reserve(m_substitutions.size() * 4);

    for (const Substitution& substitution : substitutions) {
        data.push_back(substitution.find);
        data.push_back(substitution.replace);
        data.push_back(substitution.serialize_options());
        data.push_back(substitution.notes);
    }

    set_item_value(Domain::ConfigValue{std::move(data)});
}

void ConfigItemSubstitutions::add_substitution()
{
    std::vector<Substitution> substitutions = m_substitutions;
    substitutions.emplace_back();

    send_data(substitutions);
}

void ConfigItemSubstitutions::clear_substitutions()
{
    set_item_value(Domain::ConfigValue{std::vector<std::string>()});
}

SubstitutionRow::SubstitutionRow(
    size_t index,
    const Substitution& substitution,
    ConfigItemSubstitutions* config_item_substitutions
) :
    Biz::DataObserver<Substitution>(index, substitution),
    m_config_item_substitutions(config_item_substitutions)
{
    set_orientation(Orientation::Vertical);
    set_gap(5);

    Item* first_row = emplace_back<Item>();
    first_row->set_gap(5);
    Item* first_column = first_row->emplace_back<Item>();
    first_column->set_flex_grow(1);
    first_column->set_orientation(Orientation::Vertical);
    first_column->set_gap(5);
    first_column->emplace_back<Text>(Biz::_u8L("Find"), Render::ImguiFontType::Bold);
    m_textfield_find = first_column->emplace_back<InputTextField>("TextFieldFind");
    m_textfield_find->callbacks().text_edited = [this]
    {
        m_config_item_substitutions
            ->set_field(m_index, ConfigItemSubstitutions::Field::Find, m_textfield_find->text());
    };
    Item* second_column = first_row->emplace_back<Item>();
    second_column->set_flex_grow(1);
    second_column->set_orientation(Orientation::Vertical);
    second_column->set_gap(5);
    second_column->emplace_back<Text>(Biz::_u8L("Replace with"), Render::ImguiFontType::Bold);
    m_textfield_replace = second_column->emplace_back<InputTextField>("TextFieldReplace");
    m_textfield_replace->callbacks().text_edited = [this]
    {
        m_config_item_substitutions->set_field(
            m_index,
            ConfigItemSubstitutions::Field::Replace,
            m_textfield_replace->text()
        );
    };

    Item* third_column = first_row->emplace_back<Item>();
    third_column->set_flex_grow(1);
    third_column->set_orientation(Orientation::Vertical);
    third_column->set_gap(5);
    Item* notes_row = third_column->emplace_back<Item>();
    Text* notes_text =
        notes_row->emplace_back<Text>(Biz::_u8L("Notes"), Render::ImguiFontType::Bold);
    notes_text->set_flex_grow(1);

    LayoutButton* remove =
        notes_row->emplace_back<LayoutButton>(std::string{}, Render::Icon::Minus);
    remove->set_self_align(YGAlign::YGAlignCenter);
    remove->set_width(ImGui::GetFontSize());
    remove->set_height(ImGui::GetFontSize());
    remove->callbacks().action = [this]
    { m_config_item_substitutions->remove_substitution(m_index); };

    m_textfield_notes = third_column->emplace_back<InputTextField>("TextFieldNotes");
    m_textfield_notes->callbacks().text_edited = [this]
    {
        m_config_item_substitutions
            ->set_field(m_index, ConfigItemSubstitutions::Field::Notes, m_textfield_notes->text());
    };

    Item* checkbox_row = emplace_back<Item>();
    checkbox_row->set_flex_wrap(YGWrap::YGWrapWrap);
    checkbox_row->set_gap(5);
    m_checkbox_regexp =
        checkbox_row->emplace_back<Yoga::ToggleButton>(Biz::_u8L("Regular expression"));
    m_checkbox_regexp->set_checkable(false);
    m_checkbox_regexp->callbacks().action = [this]()
    {
        m_config_item_substitutions->set_option(
            m_index,
            ConfigItemSubstitutions::Option::RegExp,
            !m_checkbox_regexp->checked()
        );
    };
    m_checkbox_case_insensitive =
        checkbox_row->emplace_back<Yoga::ToggleButton>(Biz::_u8L("Case insensitive"));
    m_checkbox_case_insensitive->set_checkable(false);
    m_checkbox_case_insensitive->callbacks().action = [this]()
    {
        m_config_item_substitutions->set_option(
            m_index,
            ConfigItemSubstitutions::Option::CaseInsensitive,
            !m_checkbox_case_insensitive->checked()
        );
    };
    m_checkbox_whole_word = checkbox_row->emplace_back<Yoga::ToggleButton>(Biz::_u8L("Whole word"));
    m_checkbox_whole_word->set_checkable(false);
    m_checkbox_whole_word->callbacks().action = [this]()
    {
        m_config_item_substitutions->set_option(
            m_index,
            ConfigItemSubstitutions::Option::WholeWord,
            !m_checkbox_whole_word->checked()
        );
    };
    m_checkbox_single_line = emplace_back<Yoga::ToggleButton>(Biz::_u8L("Match single line"));
    m_checkbox_single_line->set_checkable(false);
    m_checkbox_single_line->callbacks().action = [this]()
    {
        m_config_item_substitutions->set_option(
            m_index,
            ConfigItemSubstitutions::Option::SingleLine,
            !m_checkbox_single_line->checked()
        );
    };
    m_checkbox_single_line->set_checked(false);

    on_data_update();
}

void SubstitutionRow::on_data_update()
{
    m_textfield_find->set_text(m_state->find);
    m_textfield_notes->set_text(m_state->notes);
    m_textfield_replace->set_text(m_state->replace);

    m_checkbox_regexp->set_checked(m_state->regexp);
    m_checkbox_case_insensitive->set_checked(m_state->case_insensitive);
    m_checkbox_single_line->set_checked(m_state->single_line);
    m_checkbox_whole_word->set_checked(m_state->whole_word);
    m_checkbox_single_line->set_visible(m_state->regexp);
}

Substitution::Substitution(
    const std::string& find,
    const std::string& replace,
    const std::string& options,
    const std::string& notes
) :
    find(find),
    replace(replace),
    notes(notes)
{
    load_options(options);
}

std::string Substitution::serialize_options() const
{
    std::string options;

    if (regexp) {
        options.push_back('R');
    }
    if (case_insensitive) {
        options.push_back('I');
    }
    if (whole_word) {
        options.push_back('W');
    }
    if (single_line) {
        options.push_back('S');
    }

    return options;
}

void Substitution::load_options(const std::string& options)
{
    regexp           = false;
    case_insensitive = false;
    whole_word       = false;
    single_line      = false;

    for (const char ch : options) {
        switch (std::toupper(ch)) {
        case 'R':
            regexp = true;
            break;
        case 'I':
            case_insensitive = true;
            break;
        case 'W':
            whole_word = true;
            break;
        case 'S':
            single_line = true;
            break;
        default:
            break;
        }
    }
}

} // namespace Slic3r::App
