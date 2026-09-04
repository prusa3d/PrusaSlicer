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
