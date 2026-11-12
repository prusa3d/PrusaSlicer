#include "Slic3r/App/ConfigSettingsDialog.hpp"

#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/App/Yoga/StackLayout.hpp"
#include "Slic3r/App/Config/ConfigSubcategoryListView.hpp"

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

ConfigSettingsDialog::ConfigSettingsDialog(
    Biz::ProjectInteractor& project_interactor,
    Navigator& navigator,
    const std::string& name
) :
    AbstractSettingsDialog({}, name.empty() ? "ConfigSettingsDialog" : name),
    m_project_interactor(&project_interactor),
    m_cbi_container(m_project_interactor->preset_interactor()),
    m_navigator(navigator)
{}

ConfigSettingsDialog::ConfigSettingsDialog(
    Biz::IConfigBoxSetter& cbi_container,
    Navigator& navigator,
    const std::string& name
) :
    AbstractSettingsDialog({}, name.empty() ? "ConfigSettingsDialog" : name),
    m_cbi_container(cbi_container),
    m_navigator(navigator)
{}

void ConfigSettingsDialog::navigate_to_item(const Domain::ConfigItem* config_item)
{
    m_config_tabs.front()->navigate_to_item(config_item);
}

void ConfigSettingsDialog::clear_navigation()
{
    m_config_tabs.front()->clear_navigation();
}

void ConfigSettingsDialog::remove_tab(size_t index)
{
    AbstractSettingsDialog::remove_tab(index);
    m_config_tabs.erase(m_config_tabs.begin() + index);
}

void ConfigSettingsDialog::on_about_to_close()
{
    clear_navigation();
}

ConfigSettingsDialog::ConfigTab::ConfigTab(
    Biz::ConfigBoxInteractor* cbi,
    Tab* tab,
    Biz::ProjectInteractor& project_interactor,
    size_t cbi_index
) :
    cbi(cbi),
    tab(tab),
    project_interactor(&project_interactor),
    cbi_container(project_interactor.preset_interactor()),
    cbi_index(cbi_index),
    observable_categorizer(std::make_shared<ObservableCategorizer>()),
    category_page_transformer(std::make_shared<CategoryPageTransformer>())
{
    init();
}

ConfigSettingsDialog::ConfigTab::ConfigTab(
    Biz::ConfigBoxInteractor* cbi,
    Tab* tab,
    Biz::IConfigBoxSetter& cbi_container
) :
    cbi(cbi),
    tab(tab),
    cbi_container(cbi_container),
    observable_categorizer(std::make_shared<ObservableCategorizer>()),
    category_page_transformer(std::make_shared<CategoryPageTransformer>())
{
    init();
}

void ConfigSettingsDialog::ConfigTab::init()
{
    observable_categorizer->set_source_model(cbi->config_box_list());
    category_page_transformer->set_project_interactor(project_interactor);
    category_page_transformer->set_source_model(observable_categorizer.get());

    using CategoryListViewFactory = ViewFactory<
        ConfigSubcategoryListView,
        Biz::ConfigItemContext,
        Biz::IConfigBoxSetter&,
        Biz::ConfigBoxInteractor&,
        size_t>;
    using CategoryListView = ListView<
        ConfigSubcategoryListView,
        Biz::ConfigItemContext,
        CategoryListViewFactory,
        StackLayout>;

    std::unique_ptr<CategoryListView> category_list_view =
        std::make_unique<CategoryListView>(CategoryListViewFactory{cbi_container, *cbi, cbi_index});

    category_list_view->set_source_list(observable_categorizer.get());

    tab->replace_stack_layout(std::move(category_list_view));

    tab->page_list_view->set_source_list(category_page_transformer.get());

    if (tab->page_list_view->list_item_count()) {
        tab->page_list_view->item_at(0)->set_checked(true);
    }
}

void ConfigSettingsDialog::ConfigTab::navigate_to_item(const Domain::ConfigItem* config_item)
{
    clear_navigation();

    const Domain::ConfigItemDef::Category category = config_item->def().category;
    for (size_t category_index = 0; category_index < observable_categorizer->size();
         ++category_index)
    {
        if (observable_categorizer->at(category_index).config_item->def().category == category) {
            tab->page_list_view->item_at(category_index)->callbacks().action();
            dynamic_cast<ConfigSubcategoryListView*>(
                tab->pages_stack_layout->get_item(category_index)
            )
                ->navigate_to_item(config_item);
            break;
        }
    }
}

void ConfigSettingsDialog::ConfigTab::clear_navigation()
{
    for (size_t index = 0; index < tab->pages_stack_layout->object_count(); ++index) {
        dynamic_cast<ConfigSubcategoryListView*>(tab->pages_stack_layout->get_item(index))
            ->clear_navigation();
    }
}

} // namespace Slic3r::App
