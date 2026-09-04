#include "Slic3r/App/Yoga/Object.hpp"

#include "Slic3r/App/Yoga/ItemEvents.hpp"
#include "Slic3r/Log.hpp"

#include <stack>

namespace Slic3r::App::Yoga {

/**
 * @brief utility function to traverse tree from object ana run function on every visited object
 */
static void traverse(Object* object, const std::function<void(Object* object)>& function)
{
    std::stack<Object*> children;
    for (Object* child : object->objects()) {
        children.push(child);
    }
    while (!children.empty()) {
        Object* child = children.top();
        children.pop();

        function(child);

        for (Object* child_object : child->objects()) {
            children.push(child_object);
        }
    }
}

YGConfigRef Object::m_config = YGConfigNew();

Theme* Object::m_theme = nullptr;

std::unordered_map<std::string, int> Object::m_object_names = {};

Object::Object() : m_heartbeat(std::make_shared<int>(1)) {
    set_object_name("Object");
}

Object::~Object() = default;

const std::string& Object::object_name() const
{
    return m_object_name;
}

void Object::set_object_name(const std::string& object_name)
{
    ASSERT(
        object_name.find('_') == std::string::npos,
        "Yoga::Object name cannot contain underscore '_'"
    );

    // Get item name without increment
    std::string old_name = m_object_name.substr(0, m_object_name.find('_'));
    if (object_name == old_name) {
        // No item change -> early exit
        return;
    }

    if (m_object_names.contains(object_name)) {
        m_object_name = object_name + "_" + std::to_string(++m_object_names[object_name]);
    } else {
        m_object_name = object_name + "_1";
        m_object_names.insert({object_name, 1});
    }
}

void Object::resize(const SizeInfo& size_info) {}

void Object::render(const Vec2f& pos, const Vec2f& size) {}

void Object::style_node()
{
    for (const ObjectPtr& child : std::as_const(m_children)) {
        child->style_node();
    }
}

Object* Object::parent() const
{
    return m_parent;
}

std::vector<Object*> Object::objects() const
{
    std::vector<Object*> objects;
    objects.reserve(m_children.size());
    std::ranges::transform(
        m_children,
        std::back_inserter(objects),
        [](const ObjectPtr& object) { return object.get(); }
    );

    return objects;
}

void Object::prepend(ObjectPtr child)
{
    insert(std::move(child), 0);
}

void Object::append(ObjectPtr child)
{
    insert(std::move(child), m_children.size());
}

void Object::insert(ObjectPtr child, size_t index)
{
    add_child(std::move(child), index);
}

ObjectPtr Object::remove(Object* child)
{
    return remove_child(child);
}

ObjectHeartBeat Object::heartbeat() const
{
    return m_heartbeat.get();
}

void Object::push_event(std::unique_ptr<Event> event)
{
    Object* item_to_push = nullptr;

    if (m_root && m_root != this) {
        item_to_push = m_root;
    }

    if (item_to_push) {
        // Event will be pushed either to parent or directly to RootItem for processing
        item_to_push->push_event(std::move(event));
    } else {
        // What just happened is that some Object (this or our children) pushed event
        // But we are not able to reach RootItem therefore there is nobody to process this event.
        // This usually happen in two cases:
        // 1) Event is pushed during RootItem destruction and the vtable cannot be derived
        // Solution: Do not push any event during RootItem destruction
        // 2) Event is pushed through isolated tree which is not parented to RootItem (therefore tree is not valid)
        // Solution: Either do not push events in these trees or insert that tree to RootItem tree.

        // We would like this to catch this in Debug always, majority of these issues
        // are present only during destruction and in that case it would be overkill to crash
        // in Release
        DEBUG_ASSERT(
            item_to_push,
            "ItemEvent {} cannot be pushed from {}, you are trying to push event either during destruction of the tree or to isolated island"
        );
        SPDLOG_WARN(
            "ItemEvent {} cannot be pushed from {}, please report",
            std::to_string(reinterpret_cast<unsigned long long>(event.get())),
            object_name()
        );
    }
}

void Object::add_child(ObjectPtr child, size_t index)
{
    ASSERT(child);
    ASSERT(!index_of(child.get()).has_value(), "Child is already parented to this item");
    ASSERT(index <= m_children.size(), "Invalid child index");

    child->m_parent = this;

    if (m_root != child->m_root) {
        child->m_root = m_root;
        traverse(
            child.get(),
            [this](Object* object)
            {
                object->root_item_about_to_update();
                object->m_root = m_root;
                object->root_item_updated();
            }
        );
    }

    m_children.insert(m_children.cbegin() + index, std::move(child));
}

ObjectPtr Object::remove_child(Object* child)
{
    ASSERT(child);

    std::vector<ObjectPtr>::iterator it = std::find_if(
        m_children.begin(),
        m_children.end(),
        [child](const ObjectPtr& child_item) { return child_item.get() == child; }
    );

    ASSERT(it != m_children.end(), "Trying to remove unmaintained child");
    if (it == m_children.end()) {
        return nullptr;
    }

    child->m_parent = nullptr;
    child->m_root   = child;

    traverse(
        child,
        [child](Object* object)
        {
            if (object->m_root) {
                object->root_item_about_to_update();
                object->m_root = child;
                object->root_item_updated();
            }
        }
    );

    ObjectPtr result(std::move(*it));
    m_children.erase(it);

    return result;
}

Object* Object::root_item() const
{
    return m_root;
}

void Object::root_item_about_to_update() {}

void Object::root_item_updated() {}

size_t Object::object_count() const
{
    return m_children.size();
}

Object* Object::get_object(size_t index) const
{
    return m_children.at(index).get();
}

void Object::set_style_dirty()
{
    if (m_root != this) {
        m_root->set_style_dirty();
    }
}

std::optional<size_t> Object::index_of(Object* item) const
{
    std::vector<ObjectPtr>::const_iterator it = std::find_if(
        m_children.cbegin(),
        m_children.cend(),
        [item](const ObjectPtr& object) { return item == object.get(); }
    );
    return it == m_children.cend() ? std::nullopt :
                                     std::optional<size_t>(std::distance(m_children.cbegin(), it));
}

} // namespace Slic3r::App::Yoga
