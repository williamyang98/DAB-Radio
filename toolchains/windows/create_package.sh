#!/bin/bash
PACKAGE_DIR="${PACKAGE_DIR:-dab_radio_windows_x64}"
BUILD_DIR="${BUILD_DIR:-build}"

# clean directory
rm -rf ${PACKAGE_DIR}
mkdir -p ${PACKAGE_DIR}

# copy binaries
cp ${BUILD_DIR}/examples/*.exe ${PACKAGE_DIR}/
cp ${BUILD_DIR}/examples/*.dll ${PACKAGE_DIR}/

# copy resources
cp -rf res/ ${PACKAGE_DIR}/

# copy imgui workspace files
cp *.ini ${PACKAGE_DIR}/

# copy user docs
cp README.md ${PACKAGE_DIR}/