#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>
#include "Slic3r/App/Platform/KeyboardShortcut.hpp"
#include "Slic3r/App/Platform/CommandExecutionNotifier.hpp"

namespace Slic3r::App::Platform {

using KeyboardShortcuts = std::vector<KeyboardShortcut>;

class ICommandExecutor
{
public:
    virtual ~ICommandExecutor() = default;

    void execute() const
    {
        do_execute();
        CommandExecutionNotifier::instance().notify_command_executed();
    }

protected:
    virtual void do_execute() const = 0;
};

class ICommand : public ICommandExecutor
{
public:
    virtual ~ICommand() = default;

    virtual const char* name() const = 0;

    virtual const std::optional<KeyboardShortcuts> keyboard_shortcuts() const
    {
        return std::nullopt;
    }

    virtual bool enabled() const
    {
        return true;
    }

    std::string keyboard_shortcut_string(KeyboardShortcut::Translator translator) const;

    std::vector<std::string> keyboard_shortcut_accel_string() const;
};

struct FuncCommandExtraOpts
{
    std::optional<KeyboardShortcuts> keyboard_shortcuts = std::nullopt;
    std::function<bool()> enabled                      = nullptr;
};

class FuncCommand final : public ICommand
{
public:
    FuncCommand(
        const std::string& name,
        std::function<void()> execute,
        FuncCommandExtraOpts extra_opts = FuncCommandExtraOpts{}
    ) :
        m_name(name),
        m_execute(std::move(execute)),
        m_extra_opts(extra_opts)
    {}

    const char* name() const override
    {
        return m_name.c_str();
    }

    const std::optional<KeyboardShortcuts> keyboard_shortcuts() const override
    {
        return m_extra_opts.keyboard_shortcuts;
    }

    bool enabled() const override
    {
        return m_extra_opts.enabled ? m_extra_opts.enabled() : true;
    }

private:

    void do_execute() const override
    {
        m_execute();
    }

private:
    std::string m_name;

    std::function<void()> m_execute;
    FuncCommandExtraOpts m_extra_opts;
};

} // namespace Slic3r::App::Platform
