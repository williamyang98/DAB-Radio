$package_dir = "dab_radio_windows_x64"
mkdir $package_dir

Get-ChildItem ./build/Release -Filter *.exe | Copy-Item -Destination $package_dir -Force -PassThru
Get-ChildItem ./build/Release -Filter *.dll | Copy-Item -Destination $package_dir -Force -PassThru
Copy-Item -Path ./res -Destination $package_dir/res -Recurse
Copy-Item -Path ./bin -Destination $package_dir/bin -Recurse
Copy-Item -Path ./get_live_data.sh -Destination $package_dir
Copy-Item -Path ./README.md -Destination $package_dir

Compress-Archive -Path $package_dir -DestinationPath $package_dir".zip" -Force
rm -Force -Recurse $package_dir