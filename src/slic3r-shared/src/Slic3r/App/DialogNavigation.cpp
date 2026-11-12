#include "Slic3r/App/DialogNavigation.hpp"

#include "Slic3r/App/Yoga/Dialog.hpp"

#include "Slic3r/Assert.hpp"

#include <stack>

namespace Slic3r::App {

DialogNavigation::DialogNode::DialogNode(Yoga::Dialog* dialog, DialogNode* parent) :
    parent(parent),
    dialog(dialog)
{}

bool DialogNavigation::DialogNode::contains(DialogNode* node)
{
    if (this == node) {
        return true;
    }

    return std::any_of(children.begin(), children.end(), [node](DialogNodePtr& child) {
        return child->contains(node);
    });
}

void DialogNavigation::DialogNode::close()
{
    dialog->close();
    for (DialogNodePtr& child : children) {
        child->close();
    }
}

DialogNavigation::DialogNode* DialogNavigation::DialogNode::find_dialog_node(Yoga::Dialog* dialog)
{
    if (dialog == this->dialog) {
        return this;
    }

    for (DialogNodePtr& child : children) {
        DialogNavigation::DialogNode* node = child->find_dialog_node(dialog);
        if (node) {
            return node;
        }
    }

    return nullptr;
}

void DialogNavigation::insert_dialog(Yoga::Dialog* dialog, Yoga::Dialog* parent)
{
    ASSERT(dialog, "Dialog must be valid");
    ASSERT(!find_dialog_node(dialog), "Dialog is already present in the DialogLogic system");

    DialogNode* parent_node = find_dialog_node(parent);
    if (parent_node) {
        parent_node->children.push_back(std::make_unique<DialogNode>(dialog, parent_node));
    } else {
        m_dialogs.push_back(std::make_unique<DialogNode>(dialog));
    }
}

void DialogNavigation::open_dialog(Yoga::Dialog* dialog)
{
    if (!dialog) {
        // Dialog is null, close everything
        for (DialogNodePtr& tree : m_dialogs) {
            tree->close();
        }
        return;
    }

    // Find Root node of our dialog
    DialogNode* dialog_node = find_dialog_node(dialog);
    ASSERT(dialog_node, "Dialog isn't present in the DialogLogic system");

    // Close all trees that are not part of our node, also look for our tree
    DialogNode* root_node = nullptr;
    for (DialogNodePtr& tree : m_dialogs) {
        if (!tree->contains(dialog_node)) {
            tree->close();
        } else {
            root_node = tree.get();
        }
    }

    // Nothing to open
    if (!root_node) {
        return;
    }

    // Close all side/sub dialogs from our target node
    std::stack<DialogNode*> to_process;
    to_process.push(root_node);
    while (!to_process.empty()) {
        DialogNode* process_node = to_process.top();
        to_process.pop();

        if (process_node->contains(dialog_node)) {
            for (DialogNodePtr& child : process_node->children) {
                to_process.push(child.get());
            }
        } else {
            process_node->close();
        }
    }

    // Open dialog till our target node
    open_tree(dialog_node);
}

void DialogNavigation::open_tree(DialogNode* finish_node)
{
    const std::list<DialogNode*> path = get_path(finish_node);
    for (DialogNode* node : std::as_const(path)) {
        node->dialog->open();
    }
}

std::list<DialogNavigation::DialogNode*> DialogNavigation::get_path(DialogNode* target) const
{
    ASSERT(target);

    std::list<DialogNode*> path;
    while (target) {
        path.push_front(target);
        target = target->parent;
    }

    return path;
}

DialogNavigation::DialogNode* DialogNavigation::find_dialog_node(Yoga::Dialog* dialog)
{
    if (!dialog) {
        return nullptr;
    }

    std::stack<DialogNode*> to_process;
    for (DialogNodePtr& root_node : m_dialogs) {
        DialogNode* found_node = root_node->find_dialog_node(dialog);
        if (found_node) {
            return found_node;
        }
    }

    return nullptr;
}

} // namespace Slic3r::App
