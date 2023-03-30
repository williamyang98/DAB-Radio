# Introduction
Files for setting up a aarch64 qemu emulator on ubuntu
Instructions taken from [here](https://azeria-labs.com/arm-on-x86-qemu-user/)

## Instructions
1. <code>./toolchains/arm/install_packages.sh</code>
2. <code>./toolchains/arm/cmake_configure.sh</code>
3. <code>ninja -C build-arm</code>
4. <code>./toolchains/arm/run.sh ./build-arm/\<program\></code>