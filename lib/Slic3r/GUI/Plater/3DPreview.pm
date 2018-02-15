package Slic3r::GUI::Plater::3DPreview;
use strict;
use warnings;
use utf8;

use Slic3r::Print::State ':steps';
use Wx qw(:misc :sizer :slider :statictext :keycode wxWHITE wxCB_READONLY);
use Wx::Event qw(EVT_SLIDER EVT_KEY_DOWN EVT_CHECKBOX EVT_CHOICE EVT_CHECKLISTBOX);
use base qw(Wx::Panel Class::Accessor);

__PACKAGE__->mk_accessors(qw(print gcode_preview_data enabled _loaded canvas slider_low slider_high single_layer auto_zoom));

sub new {
    my $class = shift;
    my ($parent, $print, $gcode_preview_data, $config) = @_;
    
    my $self = $class->SUPER::new($parent, -1, wxDefaultPosition);
    $self->{config} = $config;
    $self->{number_extruders} = 1;
    # Show by feature type by default.
    $self->{preferred_color_mode} = 0;
    $self->auto_zoom(1);

    # init GUI elements
    my $canvas = Slic3r::GUI::3DScene->new($self);
    $canvas->use_plain_shader(1);
    $self->canvas($canvas);
    my $slider_low = Wx::Slider->new(
        $self, -1,
        0,                              # default
        0,                              # min
        # we set max to a bogus non-zero value because the MSW implementation of wxSlider
        # will skip drawing the slider if max <= min:
        1,                              # max
        wxDefaultPosition,
        wxDefaultSize,
        wxVERTICAL | wxSL_INVERSE,
    );
    $self->slider_low($slider_low);
    my $slider_high = Wx::Slider->new(
        $self, -1,
        0,                              # default
        0,                              # min
        # we set max to a bogus non-zero value because the MSW implementation of wxSlider
        # will skip drawing the slider if max <= min:
        1,                              # max
        wxDefaultPosition,
        wxDefaultSize,
        wxVERTICAL | wxSL_INVERSE,
    );
    $self->slider_high($slider_high);
    
    my $z_label_low = $self->{z_label_low} = Wx::StaticText->new($self, -1, "", wxDefaultPosition,
        [40,-1], wxALIGN_CENTRE_HORIZONTAL);
    $z_label_low->SetFont($Slic3r::GUI::small_font);
    my $z_label_high = $self->{z_label_high} = Wx::StaticText->new($self, -1, "", wxDefaultPosition,
        [40,-1], wxALIGN_CENTRE_HORIZONTAL);
    $z_label_high->SetFont($Slic3r::GUI::small_font);

    $self->single_layer(0);
    my $checkbox_singlelayer = $self->{checkbox_singlelayer} = Wx::CheckBox->new($self, -1, "1 Layer");
    
    my $label_view_type = $self->{label_view_type} = Wx::StaticText->new($self, -1, "View");
    
    my $choice_view_type = $self->{choice_view_type} = Wx::Choice->new($self, -1);
    $choice_view_type->Append("Feature type");
    $choice_view_type->Append("Height");
    $choice_view_type->Append("Width");
    $choice_view_type->Append("Speed");
    $choice_view_type->Append("Tool");
    $choice_view_type->SetSelection(0);

    my $label_show_features = $self->{label_show_features} = Wx::StaticText->new($self, -1, "Show");
    
    my $combochecklist_features = Wx::ComboCtrl->new();
    $combochecklist_features->Create($self, -1, "Feature types", wxDefaultPosition, [200, -1], wxCB_READONLY);
    #FIXME If the following line is removed, the combo box popup list will not react to mouse clicks.
    # On the other side, with this line the combo box popup cannot be closed by clicking on the combo button on Windows 10.
    $combochecklist_features->UseAltPopupWindow();
    $combochecklist_features->EnablePopupAnimation(0);
    my $feature_text = "Feature types";
    my $feature_items = "Perimeter|External perimeter|Overhang perimeter|Internal infill|Solid infill|Top solid infill|Bridge infill|Gap fill|Skirt|Support material|Support material interface|Wipe tower";
    Slic3r::GUI::create_combochecklist($combochecklist_features, $feature_text, $feature_items, 1);
    
    my $checkbox_travel = Wx::CheckBox->new($self, -1, "Travel");
    my $checkbox_retractions = Wx::CheckBox->new($self, -1, "Retractions");    
    my $checkbox_unretractions = Wx::CheckBox->new($self, -1, "Unretractions");
    my $checkbox_shells  = Wx::CheckBox->new($self, -1, "Shells");

    my $hsizer = Wx::BoxSizer->new(wxHORIZONTAL);
    my $vsizer = Wx::BoxSizer->new(wxVERTICAL);
    my $vsizer_outer = Wx::BoxSizer->new(wxVERTICAL);
    $vsizer->Add($slider_low, 3, 0, 0);
    $vsizer->Add($z_label_low, 0, 0, 0);
    $hsizer->Add($vsizer, 0, wxEXPAND, 0);
    $vsizer = Wx::BoxSizer->new(wxVERTICAL);
    $vsizer->Add($slider_high, 3, 0, 0);
    $vsizer->Add($z_label_high, 0, 0, 0);
    $hsizer->Add($vsizer, 0, wxEXPAND, 0);
    $vsizer_outer->Add($hsizer, 3, wxALIGN_CENTER_HORIZONTAL, 0);
    $vsizer_outer->Add($checkbox_singlelayer, 0, wxTOP | wxALIGN_CENTER_HORIZONTAL, 5);

    my $bottom_sizer = Wx::BoxSizer->new(wxHORIZONTAL);
    $bottom_sizer->Add($label_view_type, 0, wxALIGN_CENTER_VERTICAL, 5);
    $bottom_sizer->Add($choice_view_type, 0, wxEXPAND | wxALL | wxALIGN_CENTER_VERTICAL, 5);
    $bottom_sizer->AddSpacer(10);
    $bottom_sizer->Add($label_show_features, 0, wxALIGN_CENTER_VERTICAL, 5);
    $bottom_sizer->Add($combochecklist_features, 0, wxEXPAND | wxALL | wxALIGN_CENTER_VERTICAL, 5);
    $bottom_sizer->AddSpacer(20);
    $bottom_sizer->Add($checkbox_travel, 0, wxEXPAND | wxALL | wxALIGN_CENTER_VERTICAL, 5);
    $bottom_sizer->AddSpacer(10);
    $bottom_sizer->Add($checkbox_retractions, 0, wxEXPAND | wxALL | wxALIGN_CENTER_VERTICAL, 5);
    $bottom_sizer->AddSpacer(10);
    $bottom_sizer->Add($checkbox_unretractions, 0, wxEXPAND | wxALL | wxALIGN_CENTER_VERTICAL, 5);
    $bottom_sizer->AddSpacer(10);
    $bottom_sizer->Add($checkbox_shells, 0, wxEXPAND | wxALL | wxALIGN_CENTER_VERTICAL, 5);
    
    my $sizer = Wx::BoxSizer->new(wxHORIZONTAL);
    $sizer->Add($canvas, 1, wxALL | wxEXPAND, 0);
    $sizer->Add($vsizer_outer, 0, wxTOP | wxBOTTOM | wxEXPAND, 5);

    my $main_sizer = Wx::BoxSizer->new(wxVERTICAL);
    $main_sizer->Add($sizer, 1, wxALL | wxEXPAND, 0);
    $main_sizer->Add($bottom_sizer, 0, wxALL | wxEXPAND, 0); 
    
    EVT_SLIDER($self, $slider_low,  sub {
        $slider_high->SetValue($slider_low->GetValue) if $self->single_layer;
        $self->set_z_idx_low ($slider_low ->GetValue)
    });
    EVT_SLIDER($self, $slider_high, sub { 
        $slider_low->SetValue($slider_high->GetValue) if $self->single_layer;
        $self->set_z_idx_high($slider_high->GetValue) 
    });
    EVT_KEY_DOWN($canvas, sub {
        my ($s, $event) = @_;
        my $key = $event->GetKeyCode;
        if ($event->HasModifiers) {
            $event->Skip;
        } else {
            if ($key == ord('U')) {
                $slider_high->SetValue($slider_high->GetValue + 1);
                $slider_low->SetValue($slider_high->GetValue) if ($event->ShiftDown());
                $self->set_z_idx_high($slider_high->GetValue);
            } elsif ($key == ord('D')) {
                $slider_high->SetValue($slider_high->GetValue - 1);
                $slider_low->SetValue($slider_high->GetValue) if ($event->ShiftDown());
                $self->set_z_idx_high($slider_high->GetValue);
            } elsif ($key == ord('S')) {
                $checkbox_singlelayer->SetValue(! $checkbox_singlelayer->GetValue());
                $self->single_layer($checkbox_singlelayer->GetValue());
                if ($self->single_layer) {
                    $slider_low->SetValue($slider_high->GetValue);
                    $self->set_z_idx_high($slider_high->GetValue);
                }
            } else {
                $event->Skip;
            }
        }
    });
    EVT_KEY_DOWN($slider_low, sub {
        my ($s, $event) = @_;
        my $key = $event->GetKeyCode;
        if ($event->HasModifiers) {
            $event->Skip;
        } else {
            if ($key == WXK_LEFT) {
            } elsif ($key == WXK_RIGHT) {
                $slider_high->SetFocus;
            } else {
                $event->Skip;
            }
        }
    });
    EVT_KEY_DOWN($slider_high, sub {
        my ($s, $event) = @_;
        my $key = $event->GetKeyCode;
        if ($event->HasModifiers) {
            $event->Skip;
        } else {
            if ($key == WXK_LEFT) {
                $slider_low->SetFocus;
            } elsif ($key == WXK_RIGHT) {
            } else {
                $event->Skip;
            }
        }
    });
    EVT_CHECKBOX($self, $checkbox_singlelayer, sub {
        $self->single_layer($checkbox_singlelayer->GetValue());
        if ($self->single_layer) {
            $slider_low->SetValue($slider_high->GetValue);
            $self->set_z_idx_high($slider_high->GetValue);
        }
    });
    EVT_CHOICE($self, $choice_view_type, sub {
        my $selection = $choice_view_type->GetCurrentSelection();
        $self->{preferred_color_mode} = $selection;
        $self->gcode_preview_data->set_type($selection);
        $self->auto_zoom(0);
        $self->reload_print;
        $self->auto_zoom(1);
    });
    EVT_CHECKLISTBOX($self, $combochecklist_features, sub {
        my $flags = Slic3r::GUI::combochecklist_get_flags($combochecklist_features);
        
        $self->gcode_preview_data->set_extrusion_flags($flags);
        $self->auto_zoom(0);
        $self->refresh_print;
        $self->auto_zoom(1);
    });    
    EVT_CHECKBOX($self, $checkbox_travel, sub {
        $self->gcode_preview_data->set_travel_visible($checkbox_travel->IsChecked());
        $self->auto_zoom(0);
        $self->refresh_print;
        $self->auto_zoom(1);
    });    
    EVT_CHECKBOX($self, $checkbox_retractions, sub {
        $self->gcode_preview_data->set_retractions_visible($checkbox_retractions->IsChecked());
        $self->auto_zoom(0);
        $self->refresh_print;
        $self->auto_zoom(1);
    });
    EVT_CHECKBOX($self, $checkbox_unretractions, sub {
        $self->gcode_preview_data->set_unretractions_visible($checkbox_unretractions->IsChecked());
        $self->auto_zoom(0);
        $self->refresh_print;
        $self->auto_zoom(1);
    });
    EVT_CHECKBOX($self, $checkbox_shells, sub {
        $self->gcode_preview_data->set_shells_visible($checkbox_shells->IsChecked());
        $self->auto_zoom(0);
        $self->refresh_print;
        $self->auto_zoom(1);
    });
    
    $self->SetSizer($main_sizer);
    $self->SetMinSize($self->GetSize);
    $sizer->SetSizeHints($self);
    
    # init canvas
    $self->print($print);
    $self->gcode_preview_data($gcode_preview_data);
    
    # sets colors for gcode preview extrusion roles
    my @extrusion_roles_colors = (
                                    'Perimeter'                  => 'FFA500',
                                    'External perimeter'         => 'FFFF66',
                                    'Overhang perimeter'         => '0000FF',
                                    'Internal infill'            => 'FF0000',
                                    'Solid infill'               => 'CD00CD',
                                    'Top solid infill'           => 'FF3333',
                                    'Bridge infill'              => '9999FF',
                                    'Gap fill'                   => 'FFFFFF',
                                    'Skirt'                      => '7F0000',
                                    'Support material'           => '00FF00',
                                    'Support material interface' => '008000',
                                    'Wipe tower'                 => 'B3E3AB',
                                 );
    $self->gcode_preview_data->set_extrusion_paths_colors(\@extrusion_roles_colors);
    
    $self->reload_print;
    
    return $self;
}

sub reload_print {
    my ($self, $force) = @_;
    
    $self->canvas->reset_objects;
    $self->_loaded(0);

    if (! $self->IsShown && ! $force) {
#        $self->{reload_delayed} = 1;
        return;
    }

    $self->load_print;
}

sub refresh_print {
    my ($self) = @_;

    $self->_loaded(0);
    
    if (! $self->IsShown) {
        return;
    }

    $self->load_print;
}

sub load_print {
    my ($self) = @_;
    
    return if $self->_loaded;
    
    # we require that there's at least one object and the posSlice step
    # is performed on all of them (this ensures that _shifted_copies was
    # populated and we know the number of layers)
    my $n_layers = 0;
    if ($self->print->object_step_done(STEP_SLICE)) {
        my %z = ();  # z => 1
        foreach my $object (@{$self->{print}->objects}) {
            foreach my $layer (@{$object->layers}, @{$object->support_layers}) {
                $z{$layer->print_z} = 1;
            }
        }
        $self->{layers_z} = [ sort { $a <=> $b } keys %z ];
        $n_layers = scalar(@{$self->{layers_z}});
    }

    if ($n_layers == 0) {
        $self->enabled(0);
        $self->set_z_range(0,0);
        $self->slider_low->Hide;
        $self->slider_high->Hide;
        $self->{z_label_low}->SetLabel("");
        $self->{z_label_high}->SetLabel("");
        $self->canvas->reset_legend_texture();
        $self->canvas->Refresh;  # clears canvas
        return;
    }
    
    my $z_idx_low = $self->slider_low->GetValue;
    my $z_idx_high = $self->slider_high->GetValue;
    $self->enabled(1);
    $self->slider_low->SetRange(0, $n_layers - 1);
    $self->slider_high->SetRange(0, $n_layers - 1);
    if ($z_idx_high < $n_layers && ($self->single_layer || $z_idx_high != 0)) {
        # use $z_idx
    } else {
        # Out of range. Disable 'single layer' view.
        $self->single_layer(0);
        $self->{checkbox_singlelayer}->SetValue(0);
        $z_idx_low = 0;
        $z_idx_high = $n_layers - 1;
    }
    if ($self->single_layer) {
        $z_idx_low = $z_idx_high;
    } elsif ($z_idx_low > $z_idx_high) {
        $z_idx_low = 0;
    }
    $self->slider_low->SetValue($z_idx_low);
    $self->slider_high->SetValue($z_idx_high);
    $self->slider_low->Show;
    $self->slider_high->Show;
    $self->Layout;

    # Collect colors per extruder.
    my @colors = ();
    if (! $self->gcode_preview_data->empty() || $self->gcode_preview_data->type == 4) {
        my @extruder_colors = @{$self->{config}->extruder_colour};
        my @filament_colors = @{$self->{config}->filament_colour};
        for (my $i = 0; $i <= $#extruder_colors; $i += 1) {
            my $color = $extruder_colors[$i];
            $color = $filament_colors[$i] if (! defined($color) || $color !~ m/^#[[:xdigit:]]{6}/);
            $color = '#FFFFFF' if (! defined($color) || $color !~ m/^#[[:xdigit:]]{6}/);
            push @colors, $color;
        }
    }

    if ($self->IsShown) {
        if ($self->gcode_preview_data->empty) {
            # load skirt and brim
            $self->canvas->load_print_toolpaths($self->print, \@colors);
            $self->canvas->load_wipe_tower_toolpaths($self->print, \@colors);        
            foreach my $object (@{$self->print->objects}) {
                $self->canvas->load_print_object_toolpaths($object, \@colors);            
                # Show the objects in very transparent color.
                #my @volume_ids = $self->canvas->load_object($object->model_object);
                #$self->canvas->volumes->[$_]->color->[3] = 0.2 for @volume_ids;
            }
        } else {
            $self->canvas->load_gcode_preview($self->print, $self->gcode_preview_data, \@colors);
        }
        if ($self->auto_zoom) {
            $self->canvas->zoom_to_volumes;
        }
        $self->_loaded(1);
    }
    
    $self->set_z_range($self->{layers_z}[$z_idx_low], $self->{layers_z}[$z_idx_high]);
}

sub set_z_range
{
    my ($self, $z_low, $z_high) = @_;
    
    return if !$self->enabled;
    $self->{z_label_low}->SetLabel(sprintf '%.2f', $z_low);
    $self->{z_label_high}->SetLabel(sprintf '%.2f', $z_high);
    $self->canvas->set_toolpaths_range($z_low - 1e-6, $z_high + 1e-6);
    $self->canvas->Refresh if $self->IsShown;
}

sub set_z_idx_low
{
    my ($self, $idx_low) = @_;
    if ($self->enabled) {
        my $idx_high = $self->slider_high->GetValue;
        if ($idx_low >= $idx_high) {
            $idx_high = $idx_low;
            $self->slider_high->SetValue($idx_high);
        }
        $self->set_z_range($self->{layers_z}[$idx_low], $self->{layers_z}[$idx_high]);
    }
}

sub set_z_idx_high
{
    my ($self, $idx_high) = @_;
    if ($self->enabled) {
        my $idx_low  = $self->slider_low->GetValue;
        if ($idx_low > $idx_high) {
            $idx_low = $idx_high;
            $self->slider_low->SetValue($idx_low);
        }
        $self->set_z_range($self->{layers_z}[$idx_low], $self->{layers_z}[$idx_high]);
    }
}

sub set_bed_shape {
    my ($self, $bed_shape) = @_;
    $self->canvas->set_bed_shape($bed_shape);
}

sub set_number_extruders {
    my ($self, $number_extruders) = @_;
    if ($self->{number_extruders} != $number_extruders) {
        $self->{number_extruders} = $number_extruders;
        $self->{preferred_color_mode} = ($number_extruders > 1) ?
              4  # color by a tool number
            : 0; # color by a feature type
        $self->{choice_view_type}->SetSelection($self->{preferred_color_mode});
        $self->gcode_preview_data->set_type($self->{preferred_color_mode});
    }
}

# Called by the Platter wxNotebook when this page is activated.
sub OnActivate {
#    my ($self) = @_;
#    $self->reload_print(1) if ($self->{reload_delayed});
}

1;
