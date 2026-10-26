#pragma once

#include "Slic3r/App/Lua/IPluginRescanListener.hpp"
#include "Slic3r/Biz/Platform/ListenerScope.hpp"
#include "Slic3r/Biz/PrintModeInteractor.hpp"

namespace Slic3r::App::Lua {

class PluginSystem;

class PrintModePlugins : public Biz::IPrintModesProvider, public IPluginRescanListener
{
public:
    PrintModePlugins(PluginSystem& plugin_system, Biz::PrintModeInteractor& interactor);
    ~PrintModePlugins() override;

    Biz::PrintModeAvailabilities modes(const Domain::Preset::HwPrinterConfig& hw_config
    ) const override;

    Biz::PrintMode evaluate(
        const std::string& id,
        const std::map<std::string, Domain::ConfigValue>& inputs,
        const Domain::PrintModeParams& params,
        const Domain::Preset::HwPrinterConfig& hw_config) const override;

    void on_plugins_scanned(const PluginRegistry& registry) override;

private:
    PluginSystem& m_plugin_system;
    Biz::PrintModeInteractor& m_interactor;
    Biz::ListenerScope<IPluginRescanListener, PluginSystem, PrintModePlugins> m_rescan_scope;
};

} // namespace Slic3r::App::Lua
