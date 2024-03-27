#!/bin/bash

CD=$(dirname $(readlink -f $0))
cd "$CD"

docker build . -t prusa-slicer && \
docker rm -f prusa-build 2>/dev/null && \
docker run --rm -ti --name prusa-build \
	-v "$CD/../":/data/ \
	prusa-slicer \
/bin/bash -c "	cd /data/deps && \
		mkdir -p build && \
		cd build && \
		cmake .. -DDEP_WX_GTK3=ON && \
		(make -j 8 || /bin/bash) && \
		cd /data/ && \
		mkdir -p build && \
		cd build && \
		cmake .. -DSLIC3R_STATIC=1 -DSLIC3R_GTK=3 -DSLIC3R_PCH=OFF -DCMAKE_PREFIX_PATH=/data/deps/build/destdir/usr/local -DSLIC3R_GUI=1 -DSLIC3R_STATIC=1  && \
		(make -j 8 || /bin/bash) && \
		cp /data/build/src/prusa-slicer /data/ \
"
