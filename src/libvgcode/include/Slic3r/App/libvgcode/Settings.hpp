#pragma once

#include "Slic3r/App/libvgcode/Types.hpp"

#include <map>

namespace Slic3r::App::libvgcode {

struct Settings
{
		//
	  // Visualization parameters
		//
		ViewType view_type{ ViewType::FeatureType };
		Biz::libpgcode::TimeMode time_mode{ Biz::libpgcode::TimeMode::Normal };
		bool top_layer_only_view_range{ false };
		bool spiral_vase_enabled{ false };
		//
		// Required update flags
		//
		bool update_view_full_range{ true };
		bool update_enabled_entities{ true };
		bool update_colors{ true };

		//
		// Visibility maps
		//
		std::array<bool, Biz::libpgcode::OPTION_TYPES_COUNT> options_visibility{
			  false, // Travels
				false, // Wipes
				false, // Retractions
				false, // Unretractions
				false, // Seams
				false, // ToolChanges
				false, // ColorChanges
				false, // PausePrints
				false, // CustomGCodes
				false, // CenterOfGravity
				true   // ToolMarker
		};

		std::array<bool, Biz::libpgcode::GCODE_EXTRUSION_ROLES_COUNT> extrusion_roles_visibility{
				true, // None
				true, // Perimeter
				true, // ExternalPerimeter
				true, // OverhangPerimeter
				true, // InternalInfill
        true, // SolidInfill
				true, // TopSolidInfill
				true, // Ironing
				true, // BridgeInfill
				true, // GapFill
				true, // Skirt
				true, // SupportMaterial
				true, // SupportMaterialInterface
				true, // WipeTower
				true  // Custom
		};
};

} // namespace Slic3r::App::libvgcode
