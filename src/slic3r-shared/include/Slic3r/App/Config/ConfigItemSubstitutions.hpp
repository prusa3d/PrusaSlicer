#pragma once

#include "Slic3r/Biz/IObservableList.hpp"

#include "Slic3r/App/Config/ConfigItemControl.hpp"
#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/App/Yoga/ListView.hpp"

namespace Slic3r::Biz {
class IConfigBoxSetter;
} // namespace Slic3r::Biz

namespace Slic3r::App::Yoga {
class InputTextField;
class ToggleButton;
class LayoutButton;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App {

struct Substitution
{
    explicit Substitution(
        const std::string& find    = {},
        const std::string& replace = {},
        const std::string& options = {},
        const std::string& notes   = {}
    );
    std::string serialize_options() const;
    void load_options(const std::string& options);

    std::string find;
    std::string replace;
    std::string notes;

    bool regexp{false};
    bool case_insensitive{false};
    bool whole_word{false};
    bool single_line{false};
};

class ConfigItemSubstitutions;

class SubstitutionRow : public Yoga::Item, public Biz::DataObserver<Substitution>
{
public:
    explicit SubstitutionRow(
        size_t index,
        const Substitution& substitution,
        ConfigItemSubstitutions* config_item_substitutions
    );

private:
    void on_data_update() override;

private:
    ConfigItemSubstitutions* m_config_item_substitutions{nullptr};
    Yoga::InputTextField* m_textfield_find{nullptr};
    Yoga::InputTextField* m_textfield_replace{nullptr};
    Yoga::InputTextField* m_textfield_notes{nullptr};
    Yoga::ToggleButton* m_checkbox_regexp{nullptr};
    Yoga::ToggleButton* m_checkbox_case_insensitive{nullptr};
    Yoga::ToggleButton* m_checkbox_whole_word{nullptr};
    Yoga::ToggleButton* m_checkbox_single_line{nullptr};
};

class ConfigItemSubstitutions :
    public ConfigItemControl,
    public Yoga::Item,
    public Biz::IObservableList<Substitution>
{
public:
    enum class Field
    {
        Find,
        Replace,
        Notes
    };

    enum class Option
    {
        RegExp,
        CaseInsensitive,
        WholeWord,
        SingleLine
    };

    ConfigItemSubstitutions(
        size_t index,
        const Domain::ConfigItem& data,
        Biz::IConfigBoxSetter& cb_setter,
        std::vector<size_t> cbi_index
    );
    ~ConfigItemSubstitutions();

    void set_field(size_t index, Field field, const std::string& value);
    void set_option(size_t index, Option option, bool value);
    void remove_substitution(size_t index);

    const Substitution& at(size_t index) const override;

    size_t size() const override;

protected:
    void on_data_update() override;
    void send_data(const std::vector<Substitution>& substitutions);
    void add_substitution();
    void clear_substitutions();

private:
    using SubstitutionListViewFactory =
        Yoga::ViewFactory<SubstitutionRow, Substitution, ConfigItemSubstitutions*>;
    using SubstitutionListView =
        Yoga::ListView<SubstitutionRow, Substitution, SubstitutionListViewFactory>;

    std::vector<Substitution> m_substitutions;
    SubstitutionListView* m_list_view{nullptr};
    Yoga::LayoutButton* m_button_remove_all{nullptr};
};

} // namespace Slic3r::App
