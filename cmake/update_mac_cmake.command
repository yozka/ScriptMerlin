#!/bin/bash

cmake --version >/dev/null 2>&1 || {
    if [ -f /Applications/CMake.app/Contents/bin/cmake-gui ]; then
         echo "CMake doesn't added to PATH. Fixing..."
         sudo "/Applications/CMake.app/Contents/bin/cmake-gui" --install
         echo "Rerun this script again, please."
    else
         echo "CMake doesn't installed."
    fi
    exit 1;
}

BUILD_DIR=$1
if [ -z "$BUILD_DIR" ]; then
    BUILD_DIR=../build/mac
fi

cd "${0%/*}"

cmake   -G Xcode \
        -T buildsystem=12 \
        -BUILD_LIBRARY_FOR_DISTRIBUTION=YES -UseModernBuildSystem=YES \
        -B "${BUILD_DIR}" \
        -S ..

