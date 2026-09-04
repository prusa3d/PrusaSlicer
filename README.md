![PrusaSlicer logo](/resources/icons/PrusaSlicer_128px.png)

# PrusaSlicer

PrusaSlicer enables you to take your 3D models, generate 3D printing instructions and send them to your 3D printer. It supports both FDM 3D printers and mSLA 3D printers. It is developed by [Prusa Research](https://www.prusa3d.com/) and apart from Prusa printers it supports machines from a wide variety of manufacturers.

PrusaSlicer is originally based on [Slic3r](https://github.com/Slic3r/Slic3r) by Alessandro Ranellucci and the RepRap community.

## Installation

The **recommended installation method** is to go to the [PrusaSlicer project page](https://www.prusa3d.com/prusaslicer/) and download and install the software by following the instructions there.

**Alternatively**, for Windows and macOS, you can download the software directly from the [GitHub releases page](https://github.com/prusa3d/PrusaSlicer/releases). For Linux, PrusaSlicer is currently distributed exclusively through [Flathub](https://flathub.org/en/apps/com.prusa3d.PrusaSlicer).

If you prefer, you can always build PrusaSlicer yourself from source. See the [documentation](doc/Build.md) to learn how to do it.

### Main features

* Set the **printing parameters** with precision - from settings affecting the **whole print** down to **single-layer adjustments**.
* Modify your objects before printing to best suit your needs, you can **paint**, **cut**, **arrange** and do **many more** directly in PrusaSlicer.
* Open **multiple projects** at once, each containing **multiple beds** (each with potentially **different settings**).
* Make full use of **multi-material printing**
* Use your **preferred method to send your prints to the printer**, PrusaSlicer supports a wide range of possibilities.
* **View the generated print instructions** in an advanced **3D preview**.
* You can make use of the **command-line interface** to use **PrusaSlicer without GUI** in your automation setups.
* If you want, you **can make use of the integration** with [PrusaConnect](https://connect.prusa3d.com/) and [Printables](https://www.printables.com) to greatly simplify your workflow.

### Reporting a bug

Did you find a bug? Bugs can be reported in our [GitHub issue tracker](https://github.com/prusa3d/PrusaSlicer/issues), but **first make sure your report complies with the [Issue tracker policy](https://github.com/prusa3d/PrusaSlicer?tab=contributing-ov-file#issue-tracker-policy)**. **Are you not sure?** Get in touch in our [GitHub Discussions](https://www.github.com/prusa3d/discussions), before filing a bug report in the issue tracker. You can always file a bug report later, once you have more confidence.

### How to get in touch

We maintain a [GitHub Discussions](https://www.github.com/prusa3d/discussions) page in this repository to be used for **general discussions**, **questions** and **feature requests**. Furthermore, there is an announcements category, which we use to communicate with you directly.

### Pull requests

Read our [contribution guide](.github/CONTRIBUTING.md) to get more information.

### Technical stack

All of PrusaSlicer is written in C++, using CMake as the build system. The code assumes that the compiler supports C++20.

The slicing backend heavily relies on [Clipper](https://www.angusj.com/clipper2) by Angus Johnson, which handles polygon boolean operations, offsets and similar. [Eigen](https://libeigen.gitlab.io/) library is used for basic types and linear algebra calculations.

[wxWidgets](https://wxwidgets.org) library is used to handle GUI windows and events across platforms. Most of the UI is implemented using a custom OpenGL-based UI toolkit based on the [Yoga layout engine](https://github.com/facebook/yoga) and [Dear ImGui](https://github.com/ocornut/imgui), which makes it platform-independent.

The application uses many other libraries. You can see the [deps/](deps/) and [bundled_deps/](bundled_deps/) folders in the source tree to see the complete list. We are grateful to the authors and maintainers for open-sourcing their work.
