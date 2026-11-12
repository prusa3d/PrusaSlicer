#pragma once

#include <cereal/access.hpp>
#include "Slic3r/App/Undo/ChannelId.hpp"
#include "Slic3r/Domain/ObjectID.hpp"
#include "Slic3r/Domain/Transformation.hpp"
#include <cereal/types/variant.hpp>

namespace cereal {

template <class Archive>
void serialize(Archive& ar, Slic3r::Domain::Transformation& transformation)
{
    ar(transformation.m_matrix);
}

template <class Archive>
void serialize(Archive& ar, Slic3r::App::Undo::Id& id)
{
    ar(id.value);
}

template <class Archive>
void serialize(Archive& ar, Slic3r::Domain::ObjectBase& base)
{
    ar(base.m_id);
}

template <class Archive>
void load_and_construct(Archive& ar, cereal::construct<Slic3r::Domain::ObjectBase>& construct)
{
    Slic3r::Domain::ObjectID id;
    ar(id);
    construct(id);
}

} // namespace cereal

