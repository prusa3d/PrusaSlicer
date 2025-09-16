# Prusaslicer build writeup for Linux Fedora 42

## Get fresh snapshot of your debug branch for this build test.

```
git clone --depth 1 --branch [my_branch]  [my_project]

#if you run into problems:
#fetch remaining commits for git log, git bisect, etc.
git fetch --unshallow
```

## Build deps

```
cd deps
mkdir build
cd build
cmake .. -DDEP_WX_GTK3=ON

CMake Error at $HOME/.local/lib/python3.10/site-packages/cmake/data/share/cmake-4.0/Modules/FindPackageHandleStandardArgs.cmake:227 (message):
  Could NOT find GLEW (missing: GLEW_LIBRARIES) (found version "2.2.0")                                                                                                                       
Call Stack (most recent call first):                                                                                                                                                          
  /home/k/.local/lib/python3.10/site-packages/cmake/data/share/cmake-4.0/Modules/FindPackageHandleStandardArgs.cmake:591 (_FPHSA_FAILURE_MESSAGE)                                             
  /home/k/.local/lib/python3.10/site-packages/cmake/data/share/cmake-4.0/Modules/FindGLEW.cmake:245 (find_package_handle_standard_args)                                                       
  cmake/modules/FindGLEW.cmake:18 (include)                                                                                                                                                   
  CMakeLists.txt:483 (find_package)  
  
  
export GLEW_LIBRARIES=/home/k/Downloads/src/PrusaSlicer/deps/build/builds/GLEW/lib
```

Use dep_GMP.patch if you get cofigure error:

```
configure: error: could not find a working compiler, see config.log for details
make[2]: *** [CMakeFiles/dep_GMP.dir/build.make:92: dep_GMP-prefix/src/dep_GMP-stamp/dep_GMP-configure] Error 1
make[1]: *** [CMakeFiles/Makefile2:452: CMakeFiles/dep_GMP.dir/all] Error 2
make: *** [Makefile:136: all] Error 2
```

Disable link-time optimizations with "-fno-lto" if you get this LTO error:

lto1: fatal error: bytecode stream in file ‘$HOME/Downloads/src/PrusaSlicer/deps/build/destdir/usr/local/lib64/libtbb.a’ generated with LTO version 15.0 instead of the expected 15.1
compilation terminated.

```
cd deps
mkdir build
cd build

cat << EOF > dep_GMP.patch
> --- ./dep_GMP-prefix/src/dep_GMP/configure    2025-06-29 11:39:49.503093949 -0800
+++ /home/k/configure   2025-06-28 23:07:52.905346758 -0800
@@ -6531,7 +6531,7 @@
 static __inline__ t1 e(t2 rp,t2 up,int n,t1 v0)
 {t1 c,x,r;int i;if(v0){c=1;for(i=1;i<n;i++){x=up[i];r=x+1;rp[i]=r;}}return c;}
 void f(){static const struct{t1 n;t1 src[9];t1 want[9];}d[]={{1,{0},{1}},};t1 got[9];int i;
-for(i=0;i<1;i++){if(e(got,got,9,d[i].n)==0)h();g(i,d[i].src,d[i].n,got,d[i].want,9);if(d[i].n)h();}}
+for(i=0;i<1;i++){if(e(got,got,9,d[i].n)==0)h();g();if(d[i].n)h();}}
 #else
 int dummy;
 #endif
@@ -8150,7 +8150,7 @@
 static __inline__ t1 e(t2 rp,t2 up,int n,t1 v0)
 {t1 c,x,r;int i;if(v0){c=1;for(i=1;i<n;i++){x=up[i];r=x+1;rp[i]=r;}}return c;}
 void f(){static const struct{t1 n;t1 src[9];t1 want[9];}d[]={{1,{0},{1}},};t1 got[9];int i;
-for(i=0;i<1;i++){if(e(got,got,9,d[i].n)==0)h();g(i,d[i].src,d[i].n,got,d[i].want,9);if(d[i].n)h();}}
+for(i=0;i<1;i++){if(e(got,got,9,d[i].n)==0)h();g();if(d[i].n)h();}}
 #else
 int dummy;
 #endif
EOF

patch -p1 < dep_GMP.patch
export CMAKE_POLICY_VERSION_MINIMUM=3.5
cmake .. -DDEP_WX_GTK3=ON 
#cmake .. -DDEP_WX_GTK3=ON -DCMAKE_BUILD_TYPE=Release
#-DCMAKE_CXX_FLAGS="-fno-lto" -DCMAKE_C_FLAGS="-fno-lto"
make
cd ../..
```

## Build project

```
mkdir build
cd build
cmake .. -DSLIC3R_STATIC=1 -DSLIC3R_GTK=3 -DSLIC3R_PCH=OFF -DCMAKE_PREFIX_PATH=/home/k/Downloads/src/PrusaSlicer/build/../deps/build/destdir/usr/local -DCMAKE_BUILD_TYPE=Release

make -j$(nproc)
```

### If build fails due to TBB:

 Rebuild TBB with Your Current GCC. If the error points to a prebuilt TBB in the deps build directory, rebuild it to match your GCC version:

    Navigate to the TBB source in PrusaSlicer's deps:  

`cd ../deps/build/builds/TBB`

Clean any existing build:

```
rm -rf build
mkdir build && cd build
cmake ..  -DCMAKE_BUILD_TYPE=Debug

# or without LTO
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-flto" -DCMAKE_C_FLAGS="-flto"

cd ../../
make -B dep_TBB
```

Return to PrusaSlicer's root and retry the build:
```
cd ../../../../build
make # without parallel build if -j is still giving error due to caching
make install # DESTDIR= is already set by cmake
```
