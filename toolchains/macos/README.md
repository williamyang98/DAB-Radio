# Instructions
1. brew update
2. brew bundle install --file=./toolchains/macos/Brewfile
3. cmake . -B build-macos -DCMAKE_BUILD_TYPE=Release -G Ninja
4. cmake --build build-macos --config Release
5. ./build-macos/examples/radio_app
