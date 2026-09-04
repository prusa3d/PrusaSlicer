#pragma once

#include "Slic3r/Biz/Yaml/Yaml.hpp"
#include "Slic3r/Biz/Yaml/YamlSlic3rTypes.hpp"
#include "Slic3r/Domain/Preset/PresetTree.hpp"

ENUM_DESC(Slic3r::Domain::Preset::PresetKind,
    ("printer", FdmPrinter),
    ("print", FdmPrint),
    ("tool_print", FdmToolPrint),
    ("filament", FdmMaterial),
    ("sla_printer", SlaPrinter),
    ("sla_print", SlaPrint),
    ("sla_tool_print", SlaToolPrint),
    ("sla_material", SlaMaterial)
);

ENUM_DESC(Slic3r::Domain::Preset::ConditionMatchMode,
    ("first_match", FirstMatch),
    ("all_matches", AllMatches)
)

#define PRESET_VARIANT_FIELDS                                               \
FIELD_DESC_SIMPLE(condition),                                               \
FIELD_DESC(id, FIELD_DEFAULT, {}, FIELD_DEFAULT),                           \
FIELD_DESC_SIMPLE(name),                                                    \
FIELD_DESC(inherits, FIELD_DEFAULT, {}, FIELD_DEFAULT),                     \
FIELD_DESC(unconditional_inherits, FIELD_DEFAULT, {}, FIELD_DEFAULT),       \
FIELD_DESC(values, FIELD_DEFAULT, {}, FIELD_DEFAULT),                       \
FIELD_DESC(features, FIELD_DEFAULT, {}, FIELD_DEFAULT),                     \
FIELD_DESC_SIMPLE(match_mode),                                              \
FIELD_DESC(variants, FIELD_DEFAULT, {}, FIELD_DEFAULT),                     \
FIELD_DESC(source_location, FIELD_NAME_SELF, FIELD_DEFAULT, FIELD_DEFAULT)

STRUCT_DESC(Slic3r::Domain::Preset::PresetNode,
    PRESET_VARIANT_FIELDS
);

STRUCT_DESC(Slic3r::Domain::Preset::RootPresetNode,
    FIELD_DESC_SIMPLE(kind),
    PRESET_VARIANT_FIELDS
);

#undef PRESET_VARIANT_FIELDS

