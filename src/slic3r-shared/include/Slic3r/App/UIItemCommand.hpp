#pragma once

#include "Slic3r/App/Platform/ICommand.hpp"

namespace Slic3r::App {

struct UIItemCommandExtraOpts
{
    std::optional<std::vector<Platform::KeyboardShortcut>> keyboard_shortcuts = std::nullopt;
    std::function<bool()> enabled                               = nullptr;
    std::function<bool()> visible                               = nullptr;
    std::function<bool()> checked                               = nullptr;
    std::function<void(bool)> checked_changed                   = nullptr;
    bool todo{false};
};

/**
 * @brief Concrete implementation of ICommand for UI-bound commands.
 *
 * UIItemCommand represents an executable command that is directly associated
 * with one or more UI elements (menu items, toolbar buttons, etc.).
 *
 * Instances of UIItemCommand are registered in CommandBindingManager, which
 * binds them to corresponding UI items and keeps their state synchronized
 * across the UI.
 *
 * @note This class is intended to be used exclusively through
 *       CommandBindingManager for binding commands to related UI items.
 */
class UIItemCommand final : public Platform::ICommand
{
public:

    UIItemCommand(
        const char* name,
        std::function<void()> execute,
        UIItemCommandExtraOpts extra_opts = UIItemCommandExtraOpts{}
    ) :
        m_name(name),
        m_execute(std::move(execute)),
        m_extra_opts(extra_opts)
    {}

    UIItemCommand(
        std::string name,
        std::function<void()> execute,
        UIItemCommandExtraOpts extra_opts = UIItemCommandExtraOpts{}
    ) :
        m_name(std::move(name)),
        m_execute(std::move(execute)),
        m_extra_opts(extra_opts)
    {}

    const char* name() const override
    {
        return m_name.c_str();
    }

    bool has_todo_state() const
    {
        return m_extra_opts.todo;
    }

    const std::optional<std::vector<Platform::KeyboardShortcut>> keyboard_shortcuts() const override
    {
        return m_extra_opts.keyboard_shortcuts;
    }

    bool enabled() const override
    {
        if (has_todo_state())
            return false;
        return m_extra_opts.enabled ? m_extra_opts.enabled() : true;
    }

    bool visible() const
    {
        return m_extra_opts.visible ? m_extra_opts.visible() : true;
    }

    bool checked() const
    {
        return m_extra_opts.checked ? m_extra_opts.checked() : false;
    }

    void checked_changed(bool checked) const
    {
        if (m_extra_opts.checked_changed) {
            m_extra_opts.checked_changed(checked);
        }
    }

private:
    void do_execute() const override
    {
        if (m_execute) {
            m_execute();
        }
    }

private:
    std::string m_name;

    std::function<void()> m_execute;
    UIItemCommandExtraOpts m_extra_opts;
};

} // namespace Slic3r::App
