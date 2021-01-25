#!/bin/sh
APPIMAGETOOLURL="https://github.com/AppImage/AppImageKit/releases/latest/download/appimagetool-x86_64.AppImage"


APP_IMAGE=$1

wget ${APPIMAGETOOLURL} -O ../appimagetool.AppImage
chmod +x ../appimagetool.AppImage

sed -i -e 's#/usr#././#g' bin/superslicer
mv superslicer AppRun
chmod +x AppRun

cp resources/icons/Slic3r_192px.png superslicer.png
cat <<EOF > SuperSlicer.desktop
[Desktop Entry]
Name=SuperSlicer
Exec=AppRun %F
Icon=superslicer
Type=Application
Categories=Utility;
MimeType=model/stl;application/vnd.ms-3mfdocument;application/prs.wavefront-obj;application/x-amf;
EOF


../appimagetool.AppImage .
mv SuperSlicer-x86_64.AppImage ${APP_IMAGE}
chmod +x ${APP_IMAGE}
