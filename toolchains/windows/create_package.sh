#!/bin/bash
output_dir="dab_radio_windows_x64"
build_dir="build-windows/examples"

# clean directory
rm -rf ${output_dir}
mkdir -p ${output_dir}

# copy binaries
cp ${build_dir}/*.exe ${output_dir}/
cp ${build_dir}/*.dll ${output_dir}/

# copy resources
cp -rf res/ ${output_dir}/

# copy imgui workspace files
cp *.ini ${output_dir}/

# copy user docs
cp README.md ${output_dir}/