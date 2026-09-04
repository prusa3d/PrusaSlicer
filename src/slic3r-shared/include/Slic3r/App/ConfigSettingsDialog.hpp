#pragma once

#include "Slic3r/App/Yoga/AbstractSettingsDialog.hpp"
#include "Slic3r/App/Config/ObservableCategorizer.hpp"
#include "Slic3r/App/Config/CategoryPageTransformer.hpp"
#include "Slic3r/App/IConfigNavigable.hpp"

#include <Slic3r/Biz/IListObserver.hpp>

namespace Slic3r::Biz {
class ConfigBoxInteractor;
class IConfigBoxSetter;
} // namespace Slic3r::Biz

namespace Slic3r::App {

class ConfigSubcategoryListView;
class Navigator;

class ConfigSettingsDialog : public Yoga::AbstractSettingsDialog, public IConfigNavigable
{
public:
    ConfigSettingsDialog(
        Biz::ProjectInteractor& project_interactor,
        Navigator& navigator,
        const std::string& name = {}
    );

    ConfigSettingsDialog(
        Biz::IConfigBoxSetter& cbi_container,
        Navigator& navigator,
        const std::string& name = {}
    );

    void navigate_to_item(const Domain::ConfigItem *config_item) override;
    void clear_navigation() override;

protected:
    void remove_tab(size_t index) override;

private:
    void on_about_to_close() override;

protected:
    struct ConfigTab
    {
        ConfigTab(
            Biz::ConfigBoxInteractor* cbi,
            Tab* tab,
            Biz::ProjectInteractor& project_interactor,
            size_t cbi_index
        );

        ConfigTab(
            Biz::ConfigBoxInteractor* cbi,
            Tab* tab,
            Biz::IConfigBoxSetter& cbi_container
        );

        void init();

        void navigate_to_item(const Domain::ConfigItem* config_item);
        void clear_navigation();

        Biz::ConfigBoxInteractor* cbi{nullptr};
        Tab* tab{nullptr};
        Biz::ProjectInteractor* project_interactor{nullptr};
        Biz::IConfigBoxSetter& cbi_container;
        size_t cbi_index{0};
        Biz::UnsharedPointer<ObservableCategorizer> observable_categorizer;
        Biz::UnsharedPointer<CategoryPageTransformer> category_page_transformer;
    };

    using ConfigTabPtr = std::unique_ptr<ConfigTab>;
    using ConfigTabs   = std::vector<ConfigTabPtr>;

    ConfigTabs m_config_tabs;

    Biz::ProjectInteractor* m_project_interactor{nullptr};
    Biz::IConfigBoxSetter& m_cbi_container;
    Navigator& m_navigator;
};

} // namespace Slic3r::App
