
```
#change directory to PrusaSlicer/

rm -rf build
cd build

# Prefers /ussr/local libs, followed by system libs
cmake .. -DSLIC3R_STATIC=OFF -DSLIC3R_GTK=3 -DSLIC3R_PCH=OFF \
    -DCMAKE_PREFIX_PATH=$(pwd)/../deps/build/destdir/usr/local;/usr/local;/usr \
    -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_LIBRARY_PATH=/usr/local/lib64 \
        -DCMAKE_INCLUDE_PATH=/usr/local/include
make
```
