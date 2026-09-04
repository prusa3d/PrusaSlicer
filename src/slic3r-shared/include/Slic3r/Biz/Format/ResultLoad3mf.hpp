///|/ Copyright (c) Prusa Research 2023 Filip Sykala @Jony01
#pragma once

#include <map>
#include <string>
#include <vector>

#include "Slic3r/Biz/VirtualExtrudersConfig.hpp"
#include "Slic3r/Domain/ConfigPack.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/Preset/SelectedPreset.hpp"
#include "Slic3r/Semver.hpp"
#include "Slic3r/Domain/ProjectMetadata.hpp"
#include "Slic3r/Domain/OnBeds.hpp"

namespace Slic3r {



enum class Read3mfIssueType: unsigned short {
    zip_error, // miniz: MZ_ZIP errors, source is MZ_Archive::get_errorstr()

// Relations issues:
    relations_missing,    // Can't find realtions file by mz_zip_reader_locate_file
    relations_unreadable, // Can't read relations stats by mz_zip_reader_file_stat
    relations_bad_size,   // Invalid Relations file size - empty file size
    relations_no_memmory, // Can't allocate memory for Relations buffer by
                          // pugi::get_memory_allocation_function
    relations_cant_extract, // Can't extract Relation file to memory by mz_zip_reader_extract_to_mem
    relations_pugi_error,       // in source is pugi text description of error
    relations_missing_root,     // There is missing root tag <Relationships> OR it is empty
    relations_unexpected_xmlns, // Attribute "xmlns" is not
                                // http://schemas.openxmlformats.org/package/2006/relationships,
                                // source is founded value
    relations_unknown_attr,
    relations_unknown_node,
    relation_missing_main_model, // No Relationship with Type
                                 // http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel
    relation_missing_thumbnail,  // No Relationship with Type
                                // http://schemas.openxmlformats.org/package/2006/relationships/metadata/thumbnail
    relation_model_without_target,
    relation_thumbnail_without_target,
    relation_project_without_target,
    relation_unexpected_type,    // in source is stored fouded type
    relation_unknown_attr,
    relation_should_have_id,
    relation_should_have_type,

    content_types_file_missing,

// .model file issues
    unable_to_locate_model_file,
    cant_read_model_stats,    // mz_zip_reader_file_stat return false on .model file
    cant_read_file_stats,     // mz_zip_reader_file_stat return false on given file index
    cant_extract_iterator,    // mz_zip_reader_extract_iter_new return nullptr
    expat_cant_create_parser, // XML_ParserCreate() return nullptr
    expat_cant_create_buffer, // XML_GetBuffer() return nullptr
    expat_parse_error,        // XML_ParseBuffer() return XML_STATUS_ERROR
    sub_model_issue,

    model_bad_root_tag,
    model_unknown_tag,
    model_unexpected_xml_characters,
    model_unknown_xml_characters, // characters inside of uknown tag

    model_mesh_unknown_tag,
    model_mesh_unknown_attr,

    model_components_unknown_tag,
    model_components_unknown_attr,

    model_component_unknown_attr,
    model_component_require_objectid_attr,
    model_component_unknown_objectid, // during process appear unknown 'objectid' (may be object
                                      // definition has bad order)
    model_component_bad_objectid,
    model_component_has_parent_objectid, // component reference on parent Object (self recursion)
    model_component_require_uuid_attr,
    model_component_bad_transformation_format, // transformation matrix has issue

    model_unknown_language,
    model_unknown_attr,
    model_bad_xmlns,
    model_unknown_namespace,

    model_metadata_require_name,
    model_metadata_unknown_attr,

    model_resources_unknown_tag,
    model_resource_unknown_attr,
    model_resource_multiple_appear, // resource MUST BE exactly one in model
    model_resource_missing,         // Missing <Resource>

    model_build_unknown_tag,
    model_build_unknown_attr,
    model_build_multiple_appear, // build MUST BE exactly one in model
    model_build_missing,         // Missing <build>
    model_build_need_uuid,

    model_item_require_objectid_attr,
    model_item_bad_objectid,
    model_item_require_uuid_attr,
    model_item_unknown_objectid,
    model_item_unknown_attr,
    model_item_bad_transformation_format, // transformation matrix has issue

    model_object_unknown_tag,
    model_object_require_id_attr,
    model_object_id_is_invalid,
    model_object_id_is_not_unique,
    model_object_missing_uuid,
    model_object_need_mesh_or_component,
    model_object_cant_contain_mesh_with_component,
    model_object_unknown_attr,
    model_object_unknown_type,
    model_object_contain_unknown_componenet,

    model_mesh_mirror_invalid_id,
    model_mesh_mirror_unknown_id,
    model_mesh_mirror_required_id,
    model_mesh_mirror_nx_issue,
    model_mesh_mirror_ny_issue,
    model_mesh_mirror_nz_issue,
    model_mesh_mirror_d_issue,
    model_mesh_mirror_unknown_attr,

    model_vertices_unknown_attr,
    model_vertices_unknown_tag,
    model_vertex_unknown_attr,

    model_triangles_unknown_attr,
    model_triangles_unknown_tag,

    model_triangle_unknown_attr,

    unprocessed_file_in_3mf, // in 3mf archive is unknown

//////////////////////////////////
//  Loading PRUSA project files //
//////////////////////////////////
    project_file_is_corrupted,
    project_unknown_type,
    project_config_issue,
    project_objects_must_be_array,
    project_object_missing_id,
    project_object_bad_id,
    project_object_unknown_property,
    project_object_configuration_issue,
    project_object_ranges_must_be_array,
    project_object_ranges_must_not_be_empty,
    project_object_range_bad_z1,
    project_object_range_bad_z2,
    project_object_range_bad_must_be_array,
    project_object_range_unknown_property,
    project_object_range_config_issue,

    project_instance_unknown_property,
    project_instance_order_issue,
    project_instance_order_out_of_range_issue,

    project_volume_unknown_property,
    project_volume_missing_id,
    project_volume_bad_id,
    project_volume_unknown_type,
    project_volume_config_issue,

    project_source_unknown_property,
    project_source_offset_issue,
    project_source_repair_issue,
    project_source_filepath_issue,
    project_source_object_idx_issue,
    project_source_volume_idx_issue,
    project_source_is_from_inch_issue,
    project_source_is_from_meters_issue,

    project_repair_edge_fixed_issue,
    project_repair_degenerate_facets_issue,
    project_repair_facets_removed_issue,
    project_repair_facets_reversed_issue,
    project_repair_backwards_edges_issue,

    project_sla_support_points_must_be_array,
    project_sla_support_point_unknown_property,
    project_sla_support_point_position_issue,
    project_sla_support_point_radius_issue,
    project_sla_support_point_is_new_island_issue,

    project_sla_drain_holes_must_be_array,
    project_sla_drain_hole_unknown_property,
    project_sla_drain_hole_position_issue,
    project_sla_drain_hole_normal_issue,
    project_sla_drain_hole_radius_issue,
    project_sla_drain_hole_height_issue,

    project_text_configuration_unknown_property,
    project_text_configuration_text_issue,
    project_text_configuration_style_name_issue,
    project_text_configuration_font_descriptor_issue,
    project_text_configuration_font_descriptor_type_issue,
    project_text_configuration_char_gap_issue,
    project_text_configuration_line_gap_issue,
    project_text_configuration_line_height_issue,
    project_text_configuration_boldness_issue,
    project_text_configuration_skew_issue,
    project_text_configuration_per_glyph_issue,
    project_text_configuration_horizontal_align_issue,
    project_text_configuration_vertical_align_issue,
    project_text_configuration_collection_number_issue,
    project_text_configuration_font_family_issue,
    project_text_configuration_font_face_name_issue,
    project_text_configuration_font_face_style_issue,
    project_text_configuration_font_weight_issue,

    project_emboss_shape_unknown_property,
    project_emboss_shape_svg_file_path_issue,
    project_emboss_shape_svg_file_path_in_3mf_issue,
    project_emboss_shape_scale_issue,
    project_emboss_shape_is_unhealed_issue,
    project_emboss_shape_depth_issue,
    project_emboss_shape_use_surface_issue,

    project_cut_info_unknown_property,
    project_cut_info_type_issue,
    project_cut_info_radius_tolerance_issue,
    project_cut_info_height_tolerance_issue,
    project_cut_object_unknown_property,
    project_cut_object_id_issue,
    project_cut_object_checksum_issue,
    project_cut_object_connector_count_issue,

// FACETS_ANNOTATION
    facets_annotation_file_is_corrupted, // mz_zip_reader_file_stat == false || stat.m_uncomp_size == 0
    facets_must_be_array,
    facets_cant_identify_source, // missing ID
    facets_bad_id, // out of volumes defined in model
    facets_unknown_type,
    facets_unknown_facet_key,
    facets_triangle_id_issue,
    facets_dividing_data_issue,
    facets_newer_version_skipped,

// LAYER_HEIGHTS_PROFILE
    layer_heights_must_be_array,
    layer_heights_must_be_array_of_pairs,

// OTHER checks
    archive_contain_non_unique_uuid, // next param is first found non unique uuid (Not a stopper for import)

    cant_load_zip_file, // embossed svg in 3mf archive( mz_zip_reader_extract_to_mem)
    legacy_loader_required, // project identified as produced by PrusaSlicer 2.x. Should be readable by legacy loader.
    legacy_loader_failed, // Legacy loader tried to read the file but failed for unknown reason.

    unknown
}; // Read3mfIssueType


using Read3mfIssueData = std::pair<std::string, int>;

struct Read3mfIssue {
    Read3mfIssue() : type{ Read3mfIssueType::unknown } {}
    Read3mfIssue(
        Read3mfIssueType type,
        std::optional<std::string> msg = std::nullopt, 
        std::optional<std::string> source_str = std::nullopt,
        std::optional<int> = std::nullopt
    );

    Read3mfIssueType type;
    std::string msg; // user-readable description 
    std::vector<Read3mfIssueData> sources; // additional data describing problems (e.g. unknown tag or attribute in XML) + line number
};

struct Read3mfIssues {
    void add_issue(const Read3mfIssue& issue);
    bool has_issue(Read3mfIssueType type) const;
    std::vector<Read3mfIssue> issues;
};

struct Loaded3MF {
    struct ConfigContainerData {
        Domain::Preset::SelectedPresetMetadata preset;
        Domain::ConfigPack config_pack;
        std::vector<Domain::Vec2d> bed_offsets;
        Domain::WipeTowersOnBeds wipe_towers;
        Domain::CustomGCodesOnBeds custom_gcodes;
        Biz::VirtualExtrudersConfig virtual_extruders_config;
    };

    Domain::ProjectMetadata metadata;
    Domain::Model model;
    std::string filepath_3mf;
    std::vector<Loaded3MF::ConfigContainerData> config_containers_data;
    Read3mfIssues issues_map;
    boost::optional<Slic3r::Semver> version;
};



class Loaded3MFException : std::runtime_error
{
public:
    Loaded3MFException(const Read3mfIssue& fatal_issue)
    : std::runtime_error("Error when loading 3mf.")
    {
        issue = fatal_issue;
    }

    Read3mfIssue issue;
};





} // namespace Slic3r
