
[![you can get this shield at shields.io](https://img.shields.io/discord/771316156203270154?color=7289da&logo=discord&logoColor=white)](https://discord.gg/ygBBdRRwJY)

# SuperSlicer

**A PrusaSlicer fork (which is a slic3r fork)** (previously Slic3r++)

Prebuilt Windows, Linux and macOS 64-bit releases are available through the [git releases page](https://github.com/supermerill/SuperSlicer/releases). The Linux & macOS builds aren't tested (by me), just compiled, so please report any bugs that may occur during use.
Nightly builds are available through the [git actions page](https://github.com/supermerill/SuperSlicer/actions). Click on the build for your platform and then on 'Artifacts (1)' in the top right corner.

SuperSlicer takes 3D models (STL, OBJ, AMF) and converts them into G-code
instructions for FFF printers or PNG layers for mSLA 3D printers. It's compatible with any modern printer based on the RepRap toolchain which is running a firmware based on Marlin, Prusa, Klipper, etc.

SuperSlicer is based on [PrusaSlicer](https://github.com/prusa3d/PrusaSlicer) by Prusa Research.
PrusaSlicer is based on [Slic3r](https://github.com/Slic3r/Slic3r) by Alessandro Ranellucci and the RepRap community.

See the [wiki](https://github.com/supermerill/SuperSlicer/wiki) and
the [documentation directory](doc/) for information about compilation.

### What are SuperSlicer main features? main new features/differences?

* Custom-made generated calibration tests.
* **Ironing** top surface & many new settings to fine-tune the top surface quality, like 'only one perimeter on top'.
* A "denser infill" option for supporting the (solid) top layers.
* Better **Thin walls** (anchored inside the print, no more random bits at the ends, embedded in perimeter loops).
* Options to change holes dimensions and/or geometry, to print them at the right size.
* Better overhangs (add perimeters if needed, slice them in opposite direction each layer).
* Brim rework: many more options (inside, outside only, 'ears', per object)
* Some new seam options, to help hide them.
* Built-in calibration prints
* Built-in object generation script
* Can join perimeters into a big one to avoid travel moves.
* Many other little options and corrections (like the filled concentric pattern).
* It has also all the current slic3rPE/PrusaSlicer features.

### Complete changelog [here](https://github.com/supermerill/SuperSlicer/wiki)

See the wiki for examples.

### What are SuperSlicer / PrusaSlicer / Slic3r's main features?

Key features are:

* **Multi-platform** (Linux/Mac/Win) and packaged as standalone-app with no dependencies required
* Complete **command-line interface** to use it without GUI
* Multi-material **(multiple extruders)** object printing
* Multiple G-code flavors supported (RepRap, Makerbot, Mach3, Machinekit, etc.)
* Ability to plate **multiple objects having distinct print settings**
* **Multithread** processing
* **STL auto-repair** (tolerance for broken models)
* Wide automated unit testing

Other major features are:

* Combine infill every 'n' perimeters layer & varying density to speed up printing
* **3D preview** (including multi-material files)
* **Multiple layer heights** in a single print
* **Spiral vase** mode for bumpless vases
* Fine adjustment of speed, acceleration, and extrusion width
* Several infill patterns including honeycomb, spirals, Hilbert curves, gyroid
* Support material, raft, brim, skirt
* **Standby temperature** and automatic wiping for multi-extruder printing
* [Customizable **G-code macros**](https://github.com/prusa3d/PrusaSlicer/wiki/Slic3r-Prusa-Edition-Macro-Language) and output filename with variable placeholders
* Support for **post-processing scripts**
* **Cooling logic** controlling fan speed and dynamic print speed

[Command-Line Interface](https://github.com/prusa3d/PrusaSlicer/wiki/Command-Line-Interface) wiki page.

## Development

### What language is it written in?

Almost everything is written in C++.

The slicing core is the `libslic3r` library, which can be built and used in a standalone way.
The command-line interface is a thin wrapper over `libslic3r`.
You can download a precompiled package from the release page - it will run without the need for any dependency.

### How to compile

If you want to compile the source yourself, follow the instructions on one of
these documentation pages:
* [Linux](doc/How%20to%20build%20-%20Linux%20et%20al.md)
* [macOS](doc/How%20to%20build%20-%20Mac%20OS.md)
* [Windows](doc/How%20to%20build%20-%20Windows.md)

You can also look at the workflow YAML-files for [git actions](https://github.com/supermerill/Slic3r/tree/master/.github/workflows), as they describe how to build from source from a "virgin" dev computer.

### Can I help?

Sure! You can do the following to find things that are available to help with:
* Add an issue to the GitHub tracker **if it isn't already present**.

Before sending patches and pull requests contact me (preferably through opening a GitHub issue or commenting on an existing, related, issue) to discuss your proposed
changes. This way we can ensure that nobody wastes their time and no conflicts arise in development.

## License and attribution

SuperSlicer is licensed under the _GNU Affero General Public License, version 3_.
SuperSlicer is based on PrusaSlicer by PrusaResearch.

PrusaSlicer is licensed under the _GNU Affero General Public License, version 3_.
PrusaSlicer is owned by Prusa Research.
PrusaSlicer is originally based on Slic3r by Alessandro Ranellucci.

Slic3r is licensed under the _GNU Affero General Public License, version 3_.
Slic3r was created by Alessandro Ranellucci with the help of many other contributors.

The _GNU Affero General Public License, version 3_ ensures that if you **use** any part of this software in any way (even behind a web server), your software must be released under the same license.
