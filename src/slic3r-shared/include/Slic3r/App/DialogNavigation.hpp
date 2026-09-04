#pragma once

#include <vector>
#include <list>
#include <memory>

namespace Slic3r::App {

namespace Yoga {
class Dialog;
} // namespace Yoga

class DialogNavigation
{
    struct DialogNode;
    using DialogNodePtr = std::unique_ptr<DialogNode>;

    struct DialogNode
    {
        explicit DialogNode(Yoga::Dialog* dialog, DialogNode* parent = nullptr);

        DialogNode* parent{nullptr};
        Yoga::Dialog* dialog;
        std::vector<DialogNodePtr> children;

        bool contains(DialogNode* node);
        void close();
        DialogNode* find_dialog_node(Yoga::Dialog* dialog);
    };

public:
    void insert_dialog(Yoga::Dialog* dialog, Yoga::Dialog* parent = nullptr);
    void open_dialog(Yoga::Dialog* dialog);

private:
    DialogNode* find_dialog_node(Yoga::Dialog* dialog);
    void open_tree(DialogNode* finish_node);
    std::list<DialogNode*> get_path(DialogNode* target) const;

private:
    std::vector<DialogNodePtr> m_dialogs;
};

} // namespace Slic3r::App
