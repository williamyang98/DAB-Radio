#!/bin/bash
output_dir="dab_radio_windows_x64"
build_dir="build/Release"
rm -rf ${output_dir}
mkdir -p ${output_dir}
cp ${build_dir}/*.exe ${output_dir}/
cp ${build_dir}/*.dll ${output_dir}/
cp -rf bin/ ${output_dir}/
cp -rf res/ ${output_dir}/
cp *.ini ${output_dir}/
cp get_live_data.sh ${output_dir}/get_live_data.sh
cp README.md ${output_dir}/