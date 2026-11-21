#include <catch2/catch_test_macros.hpp>

#include "Slic3r/Biz/GCodeReader/GCodeReader.hpp"
#include "libslic3r/Layer.hpp"

#include "test_data.hpp" // get access to init_print, etc

using namespace Slic3r::Test;
using namespace Slic3r;
using Domain::FloatOrPercentage;
using Domain::Percentage;
using Domain::SupportMode;
using Slic3r::Biz::Algorithms::TriangleMesh::make_cube;
using Slic3r::Domain::BoundingBox3d;
using Slic3r::Domain::TriangleMesh;

TEST_CASE("SupportMaterial: Three raft layers created", "[SupportMaterial]")
{
	Slic3r::Print print;

    TestConfig config;
    config.print.items.opt("support_material").set(SupportMode::Everywhere);
    config.print.items.opt("raft_layers").set(3);

	Slic3r::Test::init_and_process_print({ TestMesh::cube_20x20x20 }, print, config);
    REQUIRE(print.objects().front()->support_layers().size() == 3);
}

SCENARIO("SupportMaterial: support_layers_z and contact_distance", "[SupportMaterial]")
{
    // Box h = 20mm, hole bottom at 5mm, hole height 10mm (top edge at 15mm).
    Domain::TriangleMesh mesh = Slic3r::Test::mesh(Slic3r::Test::TestMesh::cube_with_hole);
    mesh.rotate(float(M_PI / 2), Domain::Axis::X);
//    mesh.write_binary("d:\\temp\\cube_with_hole.stl");

	auto check = [](Slic3r::Print &print, bool &first_support_layer_height_ok, bool &layer_height_minimum_ok, bool &layer_height_maximum_ok, bool &top_spacing_ok)
	{
        SpanOfConstPtrs<SupportLayer> support_layers = print.objects().front()->support_layers();

		first_support_layer_height_ok = support_layers.front()->print_z == print.config().get<FloatOrPercentage>("first_layer_height").float_value();

		layer_height_minimum_ok = true;
		layer_height_maximum_ok = true;
		double min_layer_height = print.config().get<std::vector<double>>("min_layer_height").front();
		double max_layer_height = std::get<double>(print.config().hw_config().tools.front().features.at("nozzle_diameter"));
		if (print.config().get<std::vector<double>>("max_layer_height").front() > EPSILON)
			max_layer_height = std::min(max_layer_height, print.config().get<std::vector<double>>("max_layer_height").front());
		for (size_t i = 1; i < support_layers.size(); ++ i) {
			if (support_layers[i]->print_z - support_layers[i - 1]->print_z < min_layer_height - EPSILON)
				layer_height_minimum_ok = false;
			if (support_layers[i]->print_z - support_layers[i - 1]->print_z > max_layer_height + EPSILON)
				layer_height_maximum_ok = false;
		}

#if 0
		double expected_top_spacing = print.default_object_config().layer_height + print.config().nozzle_diameter.get_at(0);
		bool wrong_top_spacing = 0;
        std::vector<double> top_z { 1.1 };
		for (double top_z_el : top_z) {
			// find layer index of this top surface.
			size_t layer_id = -1;
			for (size_t i = 0; i < support_z.size(); ++ i) {
				if (abs(support_z[i] - top_z_el) < EPSILON) {
					layer_id = i;
					i = static_cast<int>(support_z.size());
				}
			}

			// check that first support layer above this top surface (or the next one) is spaced with nozzle diameter
			if (abs(support_z[layer_id + 1] - support_z[layer_id] - expected_top_spacing) > EPSILON && 
				abs(support_z[layer_id + 2] - support_z[layer_id] - expected_top_spacing) > EPSILON) {
				wrong_top_spacing = 1;
			}
		}
		d = ! wrong_top_spacing;
#else
		top_spacing_ok = true;
#endif
	};

    GIVEN("A print object having one modelObject") {
        WHEN("First layer height = 0.4") {
			Slic3r::Print print;

            TestConfig config;
            config.print.items.opt("support_material").set(SupportMode::Everywhere);
            config.print.items.opt("layer_height").set(0.2);
            config.print.items.opt("first_layer_height").set(FloatOrPercentage{0.4});
            config.print.items.opt("dont_support_bridges").set(false);

			Slic3r::Test::init_and_process_print({ mesh }, print, config);
			bool a, b, c, d;
            check(print, a, b, c, d);
            THEN("First layer height is honored")					{ REQUIRE(a == true); }
            THEN("No null or negative support layers")				{ REQUIRE(b == true); }
            THEN("No layers thicker than nozzle diameter")			{ REQUIRE(c == true); }
//            THEN("Layers above top surfaces are spaced correctly")	{ REQUIRE(d == true); }
        }
        WHEN("Layer height = 0.2 and, first layer height = 0.3") {
			Slic3r::Print print;

            TestConfig config;
            config.print.items.opt("support_material").set(SupportMode::Everywhere);
            config.print.items.opt("layer_height").set(0.2);
            config.print.items.opt("first_layer_height").set(FloatOrPercentage{0.3});
            config.print.items.opt("dont_support_bridges").set(false);

			Slic3r::Test::init_and_process_print({ mesh }, print, config);
            bool a, b, c, d;
            check(print, a, b, c, d);
            THEN("First layer height is honored")					{ REQUIRE(a == true); }
            THEN("No null or negative support layers")				{ REQUIRE(b == true); }
            THEN("No layers thicker than nozzle diameter")			{ REQUIRE(c == true); }
//            THEN("Layers above top surfaces are spaced correctly")	{ REQUIRE(d == true); }
        }
        WHEN("Layer height = nozzle_diameter[0]") {
			Slic3r::Print print;

            TestConfig config;
            config.print.items.opt("support_material").set(SupportMode::Everywhere);
            config.print.items.opt("layer_height").set(0.2);
            config.print.items.opt("first_layer_height").set(FloatOrPercentage{0.3});
            config.print.items.opt("dont_support_bridges").set(false);

			Slic3r::Test::init_and_process_print({ mesh }, print, config);
            bool a, b, c, d;
            check(print, a, b, c, d);
            THEN("First layer height is honored")					{ REQUIRE(a == true); }
            THEN("No null or negative support layers")				{ REQUIRE(b == true); }
            THEN("No layers thicker than nozzle diameter")			{ REQUIRE(c == true); }
//            THEN("Layers above top surfaces are spaced correctly")	{ REQUIRE(d == true); }
        }
    }
}

TEST_CASE("SupportMaterial: Raft under a levitating object", "[SupportMaterial]")
{
    using Biz::Algorithms::TriangleMesh::make_cube;

    // 20mm cube lifted 10mm into the air.
    TriangleMesh cube = make_cube(20.0, 20.0, 20.0);
    cube.translate(Vec3f(0.0f, 0.0f, 10.0f));

    TestConfig config;
    config.print.items.opt("support_material").set(SupportMode::Everywhere);
    config.print.items.opt("raft_layers").set(20);

    Print print;
    Test::init_and_process_print({cube}, print, config, false);

    const PrintObject* object   = print.objects().front();
    const SlicingParameters& sp = object->slicing_parameters();

    // The raft must have at least one dense interface layer.
    bool has_interface_layer = false;
    for (const SupportLayer* support_layer : object->support_layers()) {
        if (support_layer->print_z > sp.raft_interface_top_z + EPSILON) {
            continue;
        }

        const ExtrusionRole role = support_layer->support_fills.role();
        if (role.is_support_interface()) {
            has_interface_layer = true;
        }
    }

    REQUIRE(has_interface_layer);
}

TEST_CASE("SupportMaterial: Raft under a levitating edge-balanced object", "[SupportMaterial]")
{
    using Biz::Algorithms::TriangleMesh::make_cube;

    // 20mm cube tilted 45 deg onto its edge and lifted 10mm into the air.
    TriangleMesh cube = make_cube(20.0, 20.0, 20.0);
    cube.rotate(std::numbers::pi_v<float> / 4.f, Axis::X);
    const BoundingBox3d bb = cube.bounding_box();
    cube.translate(Vec3f(
        static_cast<float>(-bb.min.x()),
        static_cast<float>(-bb.min.y()),
        static_cast<float>(10.0 - bb.min.z())
    ));

    TestConfig config;
    config.print.items.opt("support_material").set(SupportMode::Everywhere);
    config.print.items.opt("raft_layers").set(20);

    Print print;
    Test::init_and_process_print({cube}, print, config, false);

    const PrintObject* object   = print.objects().front();
    const SlicingParameters& sp = object->slicing_parameters();

    // The raft interface must have role SupportMaterialInterface, not Mixed.
    // Mixed means a dense interface only on the edge plus the support columns.
    bool pure_raft_interface_layer = false;
    for (const SupportLayer* support_layer : object->support_layers()) {
        if (support_layer->print_z > sp.raft_interface_top_z + EPSILON) {
            continue;
        }

        if (support_layer->support_fills.role() == ExtrusionRole::SupportMaterialInterface) {
            pure_raft_interface_layer = true;
        }
    }

    REQUIRE(pure_raft_interface_layer);
}

#if 0
// Test 8.
TEST_CASE("SupportMaterial: forced support is generated", "[SupportMaterial]")
{
    // Create a mesh & modelObject.
    TriangleMesh mesh = TriangleMesh::make_cube(20, 20, 20);

    Model model = Model();
    ModelObject *object = model.add_object();
    object->add_volume(mesh);
    model.add_default_instances();
    model.align_instances_to_origin();

    Print print = Print();

    std::vector<double> contact_z = {1.9};
    std::vector<double> top_z = {1.1};
    print.default_object_config.support_material_enforce_layers = 100;
    print.default_object_config.support_material = 0;
    print.default_object_config.layer_height = 0.2;
    print.default_object_config.set_deserialize("first_layer_height", "0.3");

    print.add_model_object(model.objects[0]);
    print.objects.front()->_slice();

    SupportMaterial *support = print.objects.front()->_support_material();
    auto support_z = support->support_layers_z(contact_z, top_z, print.default_object_config.layer_height);

    bool check = true;
    for (size_t i = 1; i < support_z.size(); i++) {
        if (support_z[i] - support_z[i - 1] <= 0)
            check = false;
    }

    REQUIRE(check == true);
}

// TODO
bool test_6_checks(Print& print)
{
	bool has_bridge_speed = true;

	// Pre-Processing.
	PrintObject* print_object = print.objects.front();
	print_object->infill();
	SupportMaterial* support_material = print.objects.front()->_support_material();
	support_material->generate(print_object);
	// TODO but not needed in test 6 (make brims and make skirts).

	// Exporting gcode.
	// TODO validation found in Simple.pm


	return has_bridge_speed;
}

// Test 6.
SCENARIO("SupportMaterial: Checking bridge speed", "[SupportMaterial]")
{
    GIVEN("Print object") {
        // Create a mesh & modelObject.
        TriangleMesh mesh = TriangleMesh::make_cube(20, 20, 20);

        Model model = Model();
        ModelObject *object = model.add_object();
        object->add_volume(mesh);
        model.add_default_instances();
        model.align_instances_to_origin();

        Print print = Print();
        print.config.brim_width = 0;
        print.config.skirts = 0;
        print.config.skirts = 0;
        print.default_object_config.support_material = 1;
        print.default_region_config.top_solid_layers = 0; // so that we don't have the internal bridge over infill.
        print.default_region_config.bridge_speed = 99;
        print.config.cooling = 0;
        print.config.set_deserialize("first_layer_speed", "100%");

        WHEN("support_material_contact_distance = 0.2") {
            print.default_object_config.support_material_contact_distance = 0.2;
            print.add_model_object(model.objects[0]);

            bool check = test_6_checks(print);
            REQUIRE(check == true); // bridge speed is used.
        }

        WHEN("support_material_contact_distance = 0") {
            print.default_object_config.support_material_contact_distance = 0;
            print.add_model_object(model.objects[0]);

            bool check = test_6_checks(print);
            REQUIRE(check == true); // bridge speed is not used.
        }

        WHEN("support_material_contact_distance = 0.2 & raft_layers = 5") {
            print.default_object_config.support_material_contact_distance = 0.2;
            print.default_object_config.raft_layers = 5;
            print.add_model_object(model.objects[0]);

            bool check = test_6_checks(print);
            REQUIRE(check == true); // bridge speed is used.
        }

        WHEN("support_material_contact_distance = 0 & raft_layers = 5") {
            print.default_object_config.support_material_contact_distance = 0;
            print.default_object_config.raft_layers = 5;
            print.add_model_object(model.objects[0]);

            bool check = test_6_checks(print);

            REQUIRE(check == true); // bridge speed is not used.
        }
    }
}

#endif


/* 

Old Perl tests, which were disabled by Vojtech at the time of first Support Generator refactoring.

#if 0
{
    my $config = Slic3r::Config::new_from_defaults;
    $config->set('support_material', 1);
    my @contact_z = my @top_z = ();
    
    my $test = sub {
        my $print = Slic3r::Test::init_print('20mm_cube', config => $config);
        my $object_config = $print->print->objects->[0]->config;
        my $flow = Slic3r::Flow->new_from_width(
            width               => $object_config->support_material_extrusion_width || $object_config->extrusion_width,
            role                => FLOW_ROLE_SUPPORT_MATERIAL,
            nozzle_diameter     => $print->config->nozzle_diameter->[$object_config->support_material_extruder-1] // $print->config->nozzle_diameter->[0],
            layer_height        => $object_config->layer_height,
        );
        my $support = Slic3r::Print::SupportMaterial->new(
            object_config       => $print->print->objects->[0]->config,
            print_config        => $print->print->config,
            flow                => $flow,
            interface_flow      => $flow,
            first_layer_flow    => $flow,
        );
        my $support_z = $support->support_layers_z($print->print->objects->[0], \@contact_z, \@top_z, $config->layer_height);
        my $expected_top_spacing = $support->contact_distance($config->layer_height, $config->nozzle_diameter->[0]);
        
        is $support_z->[0], $config->first_layer_height,
            'first layer height is honored';
        is scalar(grep { $support_z->[$_]-$support_z->[$_-1] <= 0 } 1..$#$support_z), 0,
            'no null or negative support layers';
        is scalar(grep { $support_z->[$_]-$support_z->[$_-1] > $config->nozzle_diameter->[0] + epsilon } 1..$#$support_z), 0,
            'no layers thicker than nozzle diameter';
        
        my $wrong_top_spacing = 0;
        foreach my $top_z (@top_z) {
            # find layer index of this top surface
            my $layer_id = first { abs($support_z->[$_] - $top_z) < epsilon } 0..$#$support_z;
            
            # check that first support layer above this top surface (or the next one) is spaced with nozzle diameter
            $wrong_top_spacing = 1
                if ($support_z->[$layer_id+1] - $support_z->[$layer_id]) != $expected_top_spacing
                && ($support_z->[$layer_id+2] - $support_z->[$layer_id]) != $expected_top_spacing;
        }
        ok !$wrong_top_spacing, 'layers above top surfaces are spaced correctly';
    };
    
    $config->set('layer_height', 0.2);
    $config->set('first_layer_height', 0.3);
    @contact_z = (1.9);
    @top_z = (1.1);
    $test->();
    
    $config->set('first_layer_height', 0.4);
    $test->();
    
    $config->set('layer_height', $config->nozzle_diameter->[0]);
    $test->();
}

{
    my $config = Slic3r::Config::new_from_defaults;
    $config->set('raft_layers', 3);
    $config->set('brim_width',  0);
    $config->set('skirts', 0);
    $config->set('support_material_extruder', 2);
    $config->set('support_material_interface_extruder', 2);
    $config->set('layer_height', 0.4);
    $config->set('first_layer_height', 0.4);
    my $print = Slic3r::Test::init_print('overhang', config => $config);
    ok my $gcode = Slic3r::Test::gcode($print), 'no conflict between raft/support and brim';
    
    my $tool = 0;
    Slic3r::GCode::Reader->new->parse($gcode, sub {
        my ($self, $cmd, $args, $info) = @_;
        
        if ($cmd =~ /^T(\d+)/) {
            $tool = $1;
        } elsif ($info->{extruding}) {
            if ($self->Z <= ($config->raft_layers * $config->layer_height)) {
                fail 'not extruding raft with support material extruder'
                    if $tool != ($config->support_material_extruder-1);
            } else {
                fail 'support material exceeds raft layers'
                    if $tool == $config->support_material_extruder-1;
                # TODO: we should test that full support is generated when we use raft too
            }
        }
    });
}

{
    my $config = Slic3r::Config::new_from_defaults;
    $config->set('skirts', 0);
    $config->set('raft_layers', 3);
    $config->set('support_material_pattern', 'honeycomb');
    $config->set('support_material_extrusion_width', 0.6);
    $config->set('first_layer_extrusion_width', '100%');
    $config->set('bridge_speed', 99);
    $config->set('cooling', [ 0 ]);             # prevent speed alteration
    $config->set('first_layer_speed', '100%');  # prevent speed alteration
    $config->set('start_gcode', '');            # prevent any unexpected Z move
    my $print = Slic3r::Test::init_print('20mm_cube', config => $config);
    
    my $layer_id = -1;  # so that first Z move sets this to 0
    my @raft = my @first_object_layer = ();
    my %first_object_layer_speeds = ();  # F => 1
    Slic3r::GCode::Reader->new->parse(Slic3r::Test::gcode($print), sub {
        my ($self, $cmd, $args, $info) = @_;
        
        if ($info->{extruding} && $info->{dist_XY} > 0) {
            if ($layer_id <= $config->raft_layers) {
                # this is a raft layer or the first object layer
                my $line = Slic3r::Line->new_scale([ $self->X, $self->Y ], [ $info->{new_X}, $info->{new_Y} ]);
                my @path = @{$line->grow(scale($config->support_material_extrusion_width/2))};
                if ($layer_id < $config->raft_layers) {
                    # this is a raft layer
                    push @raft, @path;
                } else {
                    push @first_object_layer, @path;
                    $first_object_layer_speeds{ $args->{F} // $self->F } = 1;
                }
            }
        } elsif ($cmd eq 'G1' && $info->{dist_Z} > 0) {
            $layer_id++;
        }
    });
    
    ok !@{diff(\@first_object_layer, \@raft)},
        'first object layer is completely supported by raft';
    is scalar(keys %first_object_layer_speeds), 1,
        'only one speed used in first object layer';
    ok +(keys %first_object_layer_speeds)[0] == $config->bridge_speed*60,
        'bridge speed used in first object layer';
}

{
    my $config = Slic3r::Config::new_from_defaults;
    $config->set('skirts', 0);
    $config->set('layer_height', 0.35);
    $config->set('first_layer_height', 0.3);
    $config->set('nozzle_diameter', [0.5]);
    $config->set('support_material_extruder', 2);
    $config->set('support_material_interface_extruder', 2);
    
    my $test = sub {
        my ($raft_layers) = @_;
        $config->set('raft_layers', $raft_layers);
        my $print = Slic3r::Test::init_print('20mm_cube', config => $config);
        my %raft_z = ();  # z => 1
        my $tool = undef;
        Slic3r::GCode::Reader->new->parse(Slic3r::Test::gcode($print), sub {
            my ($self, $cmd, $args, $info) = @_;
        
            if ($cmd =~ /^T(\d+)/) {
                $tool = $1;
            } elsif ($info->{extruding} && $info->{dist_XY} > 0) {
                if ($tool == $config->support_material_extruder-1) {
                    $raft_z{$self->Z} = 1;
                }
            }
        });
    
        is scalar(keys %raft_z), $config->raft_layers, 'correct number of raft layers is generated';
    };
    
    $test->(2);
    $test->(70);
    
    $config->set('layer_height', 0.4);
    $config->set('first_layer_height', 0.35);
    $test->(3);
    $test->(70);
}

{
    my $config = Slic3r::Config::new_from_defaults;
    $config->set('brim_width',  0);
    $config->set('skirts', 0);
    $config->set('support_material', 1);
    $config->set('top_solid_layers', 0); # so that we don't have the internal bridge over infill
    $config->set('bridge_speed', 99);
    $config->set('cooling', [ 0 ]);
    $config->set('first_layer_speed', '100%');
    
    my $test = sub {
        my $print = Slic3r::Test::init_print('overhang', config => $config);
    
        my $has_bridge_speed = 0;
        Slic3r::GCode::Reader->new->parse(Slic3r::Test::gcode($print), sub {
            my ($self, $cmd, $args, $info) = @_;
        
            if ($info->{extruding}) {
                if (($args->{F} // $self->F) == $config->bridge_speed*60) {
                    $has_bridge_speed = 1;
                }
            }
        });
        return $has_bridge_speed;
    };
    
    $config->set('support_material_contact_distance', 0.2);
    ok $test->(), 'bridge speed is used when support_material_contact_distance > 0';
    
    $config->set('support_material_contact_distance', 0);
    ok !$test->(), 'bridge speed is not used when support_material_contact_distance == 0';
    
    $config->set('raft_layers', 5);
    $config->set('support_material_contact_distance', 0.2);
    ok $test->(), 'bridge speed is used when raft_layers > 0 and support_material_contact_distance > 0';
    
    $config->set('support_material_contact_distance', 0);
    ok !$test->(), 'bridge speed is not used when raft_layers > 0 and support_material_contact_distance == 0';
}

{
    my $config = Slic3r::Config::new_from_defaults;
    $config->set('skirts', 0);
    $config->set('start_gcode', '');
    $config->set('raft_layers', 8);
    $config->set('nozzle_diameter', [0.4, 1]);
    $config->set('layer_height', 0.1);
    $config->set('first_layer_height', 0.8);
    $config->set('support_material_extruder', 2);
    $config->set('support_material_interface_extruder', 2);
    $config->set('support_material_contact_distance', 0);
    my $print = Slic3r::Test::init_print('20mm_cube', config => $config);
    ok my $gcode = Slic3r::Test::gcode($print), 'first_layer_height is validated with support material extruder nozzle diameter when using raft layers';
    
    my $tool = undef;
    my @z = (0);
    my %layer_heights_by_tool = ();  # tool => [ lh, lh... ]
    Slic3r::GCode::Reader->new->parse($gcode, sub {
        my ($self, $cmd, $args, $info) = @_;
    
        if ($cmd =~ /^T(\d+)/) {
            $tool = $1;
        } elsif ($cmd eq 'G1' && exists $args->{Z} && $args->{Z} != $self->Z) {
            push @z, $args->{Z};
        } elsif ($info->{extruding} && $info->{dist_XY} > 0) {
            $layer_heights_by_tool{$tool} ||= [];
            push @{ $layer_heights_by_tool{$tool} }, $z[-1] - $z[-2];
        }
    });
    
    ok !defined(first { $_ > $config->nozzle_diameter->[0] + epsilon }
        @{ $layer_heights_by_tool{$config->perimeter_extruder-1} }),
        'no object layer is thicker than nozzle diameter';
    
    ok !defined(first { abs($_ - $config->layer_height) < epsilon }
        @{ $layer_heights_by_tool{$config->support_material_extruder-1} }),
        'no support material layer is as thin as object layers';
}

*/

// ---------------------------------------------------------------------------
// Tree / organic supports must keep their footprint on the build plate.
// Support geometry is generated in object local coordinates and shifted per instance when the
// G-code is written, so both sides of these tests are compared in world coordinates.
// ---------------------------------------------------------------------------
// Extents of the support toolpaths that will actually be printed, read from the G-code in world
// coordinates. support_islands is bookkeeping only, so what a change does to the print has to be
// measured here.
struct ToolpathExtents
{
    double min_x{ 0 }, min_y{ 0 }, max_x{ 0 }, max_y{ 0 };
    double max_radius{ 0 }; // from an optional centre, for beds that are not rectangular
    bool   defined{ false };
};

static ToolpathExtents support_gcode_extents(const std::string &gcode, const double centre_x = 0., const double centre_y = 0.)
{
    ToolpathExtents out;
    bool            in_support = false;
    std::istringstream lines(gcode);
    std::string        line;
    std::regex         xy(R"(^\s*G[01]\s.*X(-?[0-9.]+)\s.*Y(-?[0-9.]+))");
    std::smatch        match;

    while (std::getline(lines, line)) {
        if (line.rfind(";TYPE:", 0) == 0) {
            in_support = line.rfind(";TYPE:Support", 0) == 0;
            continue;
        }
        if (! in_support || ! std::regex_search(line, match, xy))
            continue;
        const double x = std::stod(match[1]), y = std::stod(match[2]);
        const double radius = std::hypot(x - centre_x, y - centre_y);
        if (! out.defined) {
            out = { x, y, x, y, radius, true };
            continue;
        }
        out.min_x = std::min(out.min_x, x);
        out.min_y = std::min(out.min_y, y);
        out.max_x = std::max(out.max_x, x);
        out.max_y = std::max(out.max_y, y);
        out.max_radius = std::max(out.max_radius, radius);
    }
    return out;
}

static TestConfig organic_tree_config()
{
    TestConfig config;
    config.print.items.opt("support_material").set(SupportMode::Everywhere);
    config.print.items.opt("support_material_style").set(Domain::SupportMaterialStyle::smsOrganic);
    config.print.items.opt("support_material_constrain_to_build_volume").set(false); // opt-in, off by default
    return config;
}

TEST_CASE("TreeSupport: the constraint keeps the printed support on the plate", "[SupportMaterial][TreeSupport]")
{
    // The re-centred part hangs over this plate, so without the constraint the pads are printed beside
    // it. Same fixture as the default-off case below, which proves the spill is real.
    TestConfig config = organic_tree_config();
    config.print.items.opt("support_material_constrain_to_build_volume").set(true);
    config.printer.items.opt("bed_shape").set(std::vector<Vec2d>{ {0., 0.}, {110., 0.}, {110., 110.}, {0., 110.} });

    const ToolpathExtents toolpath = support_gcode_extents(slice({ TestMesh::overhang }, config));
    REQUIRE(toolpath.defined);

    const double slack = 0.01; // Clipper rounding at the plate boundary
    CHECK(toolpath.min_x >= 0. - slack);
    CHECK(toolpath.min_y >= 0. - slack);
    CHECK(toolpath.max_x <= 110. + slack);
    CHECK(toolpath.max_y <= 110. + slack);
}

TEST_CASE("TreeSupport: the plate constraint is off by default", "[SupportMaterial][TreeSupport]")
{
    // At its default the option must leave the tree generator unconstrained: the printed support
    // spills past the plate, as it did before the constraint existed. This asserts "not clipped to
    // the plate" rather than a specific unconstrained extent (the fixture spills well inside the
    // reach of the historical 300 mm square, so it cannot by itself catch that clip).
    TestConfig config = organic_tree_config();
    config.printer.items.opt("bed_shape").set(std::vector<Vec2d>{ {0., 0.}, {110., 0.}, {110., 110.}, {0., 110.} });

    const ToolpathExtents toolpath = support_gcode_extents(slice({ TestMesh::overhang }, config));
    REQUIRE(toolpath.defined);

    const double slack = 0.01;
    CHECK((toolpath.min_x < 0. - slack || toolpath.min_y < 0. - slack ||
           toolpath.max_x > 110. + slack || toolpath.max_y > 110. + slack));
}

TEST_CASE(
    "TreeSupport: a clockwise bed_shape still constrains to the plate",
    "[SupportMaterial][TreeSupport]"
)
{
    // Printer profiles store bed_shape in whatever winding the vendor chose. The plate is cut out of
    // the off-plate collision volume by winding alone, so a clockwise plate that is not normalised
    // first turns the cut-out into a filled contour: the collision volume then encloses the whole bed
    // and the generator emits no support at all. Same 110 mm square as the other cases, listed
    // clockwise.
    TestConfig config = organic_tree_config();
    config.print.items.opt("support_material_constrain_to_build_volume").set(true);
    config.printer.items.opt("bed_shape")
        .set(std::vector<Vec2d>{{0., 0.}, {0., 110.}, {110., 110.}, {110., 0.}});

    const ToolpathExtents toolpath = support_gcode_extents(slice({TestMesh::overhang}, config));
    REQUIRE(toolpath.defined); // the winding guard must not cost the print its support

    const double slack = 0.01; // Clipper rounding at the plate boundary
    CHECK(toolpath.min_x >= 0. - slack);
    CHECK(toolpath.min_y >= 0. - slack);
    CHECK(toolpath.max_x <= 110. + slack);
    CHECK(toolpath.max_y <= 110. + slack);
}

TEST_CASE(
    "TreeSupport: the raft's first-layer column bases stay on the plate",
    "[SupportMaterial][TreeSupport]"
)
{
    // With a raft, the first layer fills first_layer_support_polygons on its own once its density
    // differs from the raft's. That member used to escape the plate trim, so the column bases were
    // printed beside the part even with the constraint on.
    TestConfig config = organic_tree_config();
    config.print.items.opt("support_material_constrain_to_build_volume").set(true);
    config.printer.items.opt("bed_shape")
        .set(std::vector<Vec2d>{{0., 0.}, {110., 0.}, {110., 110.}, {0., 110.}});
    config.print.items.opt("raft_layers").set(2);
    config.print.items.opt("raft_first_layer_density").set(Percentage{80.});
    config.print.items.opt("support_material_first_layer_density").set(Percentage{30.});

    const ToolpathExtents toolpath = support_gcode_extents(slice({TestMesh::overhang}, config));
    REQUIRE(toolpath.defined);

    const double slack = 0.01; // Clipper rounding at the plate boundary
    CHECK(toolpath.min_x >= 0. - slack);
    CHECK(toolpath.min_y >= 0. - slack);
    CHECK(toolpath.max_x <= 110. + slack);
    CHECK(toolpath.max_y <= 110. + slack);
}

TEST_CASE("TreeSupport: a circular bed clips support along the arc", "[SupportMaterial][TreeSupport]")
{
    // No delta printer ships in this tree, so a circular bed can only be checked synthetically: a
    // 64-gon standing in for the arc. The radius sits between the fixture's corners (33.0 mm from
    // its centre) and its support (37.7 mm), the only band where a circle cuts support but not part.
    constexpr double   two_pi = 6.283185307179586;
    const double       radius = 35.;
    std::vector<Vec2d> bed;
    for (int i = 0; i < 64; ++i) {
        const double angle = two_pi * double(i) / 64.;
        bed.push_back(Vec2d{100. + radius * std::cos(angle), 100. + radius * std::sin(angle)});
    }

    const double slack = 0.01; // Clipper rounding at the plate boundary
    for (const bool constrained : {false, true}) {
        TestConfig config = organic_tree_config();
        config.print.items.opt("support_material_constrain_to_build_volume").set(constrained);
        config.printer.items.opt("bed_shape").set(bed);

        const ToolpathExtents toolpath =
            support_gcode_extents(slice({TestMesh::overhang}, config), 100., 100.);
        REQUIRE(toolpath.defined);
        if (constrained)
            CHECK(toolpath.max_radius <= radius + slack);
        else
            CHECK(toolpath.max_radius > radius + slack); // the arc has to cut something, or this proves nothing
    }
}

TEST_CASE(
    "TreeSupport: the constraint holds for every instance of an object",
    "[SupportMaterial][TreeSupport]"
)
{
    // Support geometry is generated once and printed at every instance, so constraining it to the
    // plate around one instance would leave the others printing support beside the bed. The plate is
    // therefore intersected over all instances. Two instances on the fixture bed are far enough apart
    // for the unconstrained spill to show, which makes the pair below a real test rather than a
    // tautology.
    const double slack = 0.01; // Clipper rounding at the plate boundary
    for (const bool constrained : {false, true}) {
        TestConfig config = organic_tree_config();
        config.print.items.opt("support_material_constrain_to_build_volume").set(constrained);
        config.printer.items.opt("bed_shape")
            .set(std::vector<Vec2d>{{0., 0.}, {110., 0.}, {110., 110.}, {0., 110.}});

        Print         print;
        Domain::Model model;
        init_print({TestMesh::overhang}, print, model, config, 2, true);
        REQUIRE(print.objects().front()->instances().size() == 2); // else this proves nothing

        const ToolpathExtents toolpath = support_gcode_extents(gcode(print));
        REQUIRE(toolpath.defined);
        if (constrained) {
            CHECK(toolpath.min_x >= 0. - slack);
            CHECK(toolpath.min_y >= 0. - slack);
            CHECK(toolpath.max_x <= 110. + slack);
            CHECK(toolpath.max_y <= 110. + slack);
        } else {
            CHECK((toolpath.min_x < 0. - slack || toolpath.min_y < 0. - slack ||
                   toolpath.max_x > 110. + slack || toolpath.max_y > 110. + slack));
        }
    }
}
