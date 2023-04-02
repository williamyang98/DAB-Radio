#!/bin/bash
output_dir="dab_radio_windows_x64"
build_dir="build/src/examples/Release"
rm -rf ${output_dir}
mkdir -p ${output_dir}
cp ${build_dir}/*.exe ${output_dir}/
cp ${build_dir}/*.dll ${output_dir}/
cp -rf res/ ${output_dir}/
cp *.ini ${output_dir}/
cp README.md ${output_dir}/