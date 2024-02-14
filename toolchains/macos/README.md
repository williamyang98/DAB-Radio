# Instructions
1. Update brew: ```brew update```
2. Install dependencies: ```brew bundle install --file=./toolchains/macos/Brewfile```
3. Configure cmake: ```cmake . -B build --preset clang -DCMAKE_BUILD_TYPE=Release```
4. Build: ```cmake --build build --config Release```
5. Run: ```./build/examples/radio_app```
