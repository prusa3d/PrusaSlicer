#pragma once

#include "Slic3r/App/PopNotification/PopNotificationView.hpp"
#include "Slic3r/App/PopNotification/PopNotificationData.hpp"
#include "Slic3r/App/PopNotification/PopNotificationCenter.hpp"

#include "Slic3r/App/Yoga/ListView.hpp"

namespace Slic3r::App::PopNotification {

using PopNotificationViewFactory = Yoga::ViewFactory<PopNotificationView, PopNotificationData, PopNotificationObservableList&>;
using PopNotificationBaseListView = Yoga::ListView<PopNotificationView, PopNotificationData, PopNotificationViewFactory>;

class PopNotificationListView final : public PopNotificationBaseListView
{
public:
    PopNotificationListView(PopNotificationObservableList& notification_list) :
        PopNotificationBaseListView(PopNotificationViewFactory(notification_list))
    {}

    ~PopNotificationListView() = default;
private:
};
} // namespace Slic3r::App::PopNotification
