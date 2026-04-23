To build the standalone SLA estimate testing app:

git clone https://github.com/prusa3d/PrusaSlicer

cd PrusaSlicer/deps
mkdir build && cd build
cmake ..
cmake --build . --target dep_json
cd ../..

cd src/sla-time-estimates
mkdir build && cd build
cmake .. -DCMAKE_PREFIX_PATH=$(pwd)/../../../deps/build/destdir/usr/local
cmake --build .
