#pragma once

#include "Slic3r/App/Scene/SceneNodeTag.hpp"
#include "Slic3r/App/Scene/BedNodeTag.hpp"
#include "Slic3r/App/Scene/Scene.hpp"

namespace Slic3r::App::Plater {

    inline bool is_selectable(const Scene::Node& n)
    { return n.has_tag_of_type<Scene::SceneNodeTag>() || n.has_tag_of_type<Scene::BedNodeTag>(); }

    inline bool is_draggable(const Scene::Node& n)
    { return n.has_tag_of_type<Scene::SceneNodeTag>(); }

    inline bool any_of(const Scene::Node::ConstNodeList& nodes, std::function<bool(const Scene::Node&)> predicate)
    {
        return std::find_if(nodes.begin(), nodes.end(), [&](auto node) {
            return predicate(*node);
        }) != nodes.end();
    }

    inline bool any_of(const Scene::Node::NodeList& nodes, std::function<bool(const Scene::Node&)> predicate)
    {
        return std::find_if(nodes.begin(), nodes.end(), [&](auto node) {
            return predicate(*node);
        }) != nodes.end();
    }

    inline bool any_of(const Scene::ConstNodePickResults& nodes, std::function<bool(const Scene::Node&)> predicate)
    {
        return std::find_if(nodes.begin(), nodes.end(), [&](const Scene::ConstNodePickResult& npr) {
            return predicate(*npr.node);
        }) != nodes.end();
    }

    inline bool any_of(const Scene::NodePickResults& nodes, std::function<bool(const Scene::Node&)> predicate)
    {
        return std::find_if(nodes.begin(), nodes.end(), [&](const Scene::NodePickResult& npr) {
            return predicate(*npr.node);
        }) != nodes.end();
    }

    template <typename C>
    concept NodeContainer = requires(const C& c, std::function<bool(const Scene::Node&)> p)
    {
        { any_of(c, p) } -> std::same_as<bool>;
    };

    template <typename C>
    bool any_draggable(const C& nodes)
    { return any_of(nodes, is_draggable); }

    template <typename C>
    bool any_selectable(const C& nodes)
    { return any_of(nodes, is_selectable); }
}
