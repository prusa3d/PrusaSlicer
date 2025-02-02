# Building PrusaSlicer on Windows


## 0. Prerequisities

The following tools need to be installed on your computer:
- Microsoft Visual Studio version 16 2019 or 17 2022
- CMake
- git




## 1. Download sources

Clone the respository. Use a directory relatively close to the drive root, so the path is not too long. Avoid spaces and non-ASCII characters. To place it in `C:\src\PrusaSlicer`, run:
```
c:> mkdir src
c:> cd src
c:\src> git clone https://github.com/prusa3d/PrusaSlicer.git
```


## 2.A Manual Build Instructions

_As an alternative, you can go to 2.B to compile PrusaSlicer using one automatic script._

### Compile the dependencies.
Dependencies are updated seldomly, thus they are compiled out of the PrusaSlicer source tree.
Open the MSVC x64 Native Tools Command Prompt and run the following:
```
cd c:\src\PrusaSlicer\deps
mkdir build
cd build
cmake ..
cmake --build .
```
Expect this to take some time. Note that both _Debug_ and _Release_ variants are built. You can force only the _Release_ build by passing `-DDEP_DEBUG=OFF` to the first CMake call.

### Generate Visual Studio project file for PrusaSlicer, referencing the precompiled dependencies.
Open the MSVC x64 Native Tools Command Prompt and run the following:
```
cd c:\src\PrusaSlicer\
mkdir build
cd build
cmake .. -DCMAKE_PREFIX_PATH="c:\src\PrusaSlicer\deps\build\destdir\usr\local"
```

Note that `CMAKE_PREFIX_PATH` must be absolute path. A relative path will not work.

### Compile PrusaSlicer. 

Double-click c:\src\PrusaSlicer\build\PrusaSlicer.sln to open in Visual Studio and select `PrusaSlicer_app_gui` as your startup project (right-click->Set as Startup Project).

Run Build->Rebuild Solution once to populate all required dependency modules. This is NOT done automatically when you Build/Run. If you run both Debug and Release variants, you will need to do this once for each.

Debug->Start Debugging or press F5

PrusaSlicer should start. You're up and running!

### Running Unit Tests

For the most complete unit testing, use the Debug build option `-DCMAKE_BUILD_TYPE=Debug` when running cmake.
Without the Debug build, internal assert statements are not tested.

To run the unit tests:

    cd build
    make test


To run a specific unit test:

    cd build\tests

The unit tests can be found by

    `dir *\*_tests`

Any of these unit tests can be run directly e.g.

    `.\fff_print\fff_print_tests`

## 2.B Run the automatic build script

_This is an alternative to the manual build described above. It relies on `build_win.bat` script which is part of the repository and which will find the MSVC installation, set up the build environment and build the dependencies and PrusaSlicer itself. The script was provided by @jschuh and PrusaSlicer team does not maintain it._

Just run the following command to get everything going with the default configs:

```
c:\src>cd c:\src\PrusaSlicer
c:\src\PrusaSlicer>build_win.bat -d=..\PrusaSlicer-deps -r=console
```

The build script will run for a while and automatically perform the following steps:
1. Configure and build [deps](#compile-the-dependencies) as RelWithDebInfo with `c:\src\PrusaSlicer-deps` as the destination directory
2. Configure and build all [application targets](#compile-prusaslicer) as RelWithDebInfo
3. Launch the resulting `prusa-slicer-console.exe` binary

You can change the above command line options to do things like:
* Change the destination for the dependencies by pointing `-d` to a different directory such as: `build_win.bat -d=s:\PrusaSlicerDeps`
* Open the solution in Visual Studio after the build completes by changing the `-r` switch to `-r=ide`
* Generate a release build without debug info by adding `-c=Release` or a full debug build with `-c=Debug`
* Perform an incremental application build (the default) with: `build_win.bat -s=app-dirty`
* Clean and rebuild the application: `build_win.bat -s=app`
* Clean and rebuild the dependencies: `build_win.bat -s=deps`
* Clean and rebuild everything (app and deps): `build_win.bat -s=all`
* _The full list of build script options can be listed by running:_ `build_win.bat -?`

#### Troubleshooting

You're best off initiating builds from within Visual Studio for day-to-day development. However, the `build_win.bat` script can be very helpful if you run into build failures after updating your source tree. Here are some tips to keep in mind:
* The last several lines of output from `build_win.bat` will usually have the most helpful error messages.
* If CMake complains about missing binaries or paths (e.g. after updating Visual Studio), building with `build_win.bat` will force CMake to regenerate its cache on an error.
* After a deps change, you may just need to rebuild everything with the `-s=all` switch.
* Reading through the instructions in the next section may help diagnose more complex issues.

