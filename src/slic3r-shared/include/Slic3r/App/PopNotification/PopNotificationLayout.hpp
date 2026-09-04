#pragma once

#include "Slic3r/App/Render/ImguiTypes.hpp"

#include <string>
#include <functional>
#include <variant>

namespace Slic3r::App::PopNotification {

struct PopNotificationButtonData
{
    std::string text;
    std::function<bool(void)> callback; // returns true if notification should close.
};

struct PopNotificationLayoutText
{
    std::string text;
    Render::Icon icon{Render::Icon::None};
};

struct PopNotificationLayoutHeaderText
{
    std::string header;
    std::string text;
    Render::Icon icon{Render::Icon::None};
};

struct PopNotificationLayoutHeader
{
    std::string header;
    Render::Icon icon{Render::Icon::None};
};

struct PopNotificationLayoutImageHeader
{
    std::string image_path;
    std::string header;
};

struct PopNotificationLayoutImageText
{
    std::string image_path;
    std::string text;
};

struct PopNotificationLayoutImageHeaderText
{
    std::string image_path;
    std::string header;
    std::string text;
};

struct PopNotificationLayoutTextButtons
{
    std::string text;
    std::vector<PopNotificationButtonData> buttons;
    Render::Icon icon{Render::Icon::None};
};

struct PopNotificationLayoutHeaderTextButtons
{
    std::string header;
    std::string text;
    std::vector<PopNotificationButtonData> buttons;
    Render::Icon icon{Render::Icon::None};
};

struct PopNotificationLayoutTextProgress
{
    std::string text;
    int progress;
    Render::Icon icon{Render::Icon::None};
};

struct PopNotificationLayoutHeaderTextProgress
{
    std::string header;
    std::string text;
    int progress;
    Render::Icon icon{Render::Icon::None};
};

struct PopNotificationLayoutHeaderProgress
{
    std::string header;
    int progress;
    Render::Icon icon{Render::Icon::None};
};

using PopNotificationLayout = std::variant<
    PopNotificationLayoutText,
    PopNotificationLayoutHeaderText,
    PopNotificationLayoutHeader,
    PopNotificationLayoutImageHeader,
    PopNotificationLayoutImageText,
    PopNotificationLayoutImageHeaderText,
    PopNotificationLayoutTextButtons,
    PopNotificationLayoutHeaderTextButtons,
    PopNotificationLayoutTextProgress,
    PopNotificationLayoutHeaderTextProgress,
    PopNotificationLayoutHeaderProgress>;

} // namespace Slic3r::App::PopNotification
