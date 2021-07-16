#!/usr/bin/env bash
set -Eeuo pipefail

# Retrieve build tool

APPIMAGETOOLURL="https://github.com/AppImage/AppImageKit/releases/latest/download/appimagetool-x86_64.AppImage"
appimagetool="$GITHUB_WORKSPACE/appimagetool.AppImage"
wget "$APPIMAGETOOLURL" -O "$appimagetool"
chmod +x "$appimagetool"

appimage_name="$1"

pushd "$DESTDIR"
    # sed -i -e 's#/usr#././#g' bin/@SLIC3R_APP_CMD@
    # mv @SLIC3R_APP_CMD@ AppRun
    # chmod +x AppRun

    cp resources/icons/PrusaSlicer_192px.png PrusaSlicer.png

    cat <<EOF > Slic3r.desktop
[Desktop Entry]
Name=PrusaSlicer (AppImage)
Exec=AppRun %F
Icon=PrusaSlicer
Type=Application
Categories=Utility;
MimeType=model/stl;application/vnd.ms-3mfdocument;application/prs.wavefront-obj;application/x-amf;
EOF

    "$appimagetool" .
    ls -lah
    mv ./*.AppImage "$appimage_name"
    chmod +x "$appimage_name"
popd
