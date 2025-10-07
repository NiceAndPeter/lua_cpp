# CMake Build Instructions

This project uses modern CMake (3.20+) as the build system for the C++23 Lua implementation.

## Quick Start

```bash
# Configure
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build

# Run tests
cd build && ctest --output-on-failure

# Install (optional)
cmake --install build --prefix /usr/local
```

## Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `CMAKE_BUILD_TYPE` | - | Build type: `Debug`, `Release`, `RelWithDebInfo`, `MinSizeRel` |
| `LUA_BUILD_TESTS` | `ON` | Enable test mode with ltests.h and assertions |
| `LUA_BUILD_SHARED` | `OFF` | Build shared library in addition to static |
| `LUA_ENABLE_ASAN` | `OFF` | Enable AddressSanitizer |
| `LUA_ENABLE_UBSAN` | `OFF` | Enable UndefinedBehaviorSanitizer |
| `LUA_ENABLE_LTO` | `OFF` | Enable Link Time Optimization |

### Examples

**Debug build with sanitizers:**
```bash
cmake -B build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DLUA_ENABLE_ASAN=ON \
  -DLUA_ENABLE_UBSAN=ON
cmake --build build
```

**Release build with LTO:**
```bash
cmake -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DLUA_ENABLE_LTO=ON
cmake --build build
```

**Production build without tests:**
```bash
cmake -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DLUA_BUILD_TESTS=OFF \
  -DLUA_BUILD_SHARED=ON
cmake --build build
```

## Targets

- **liblua_static**: Static library `liblua.a`
- **liblua_shared**: Shared library `liblua.so` (if `LUA_BUILD_SHARED=ON`)
- **lua**: Lua interpreter executable

## Testing

The project uses CTest for testing:

```bash
# Run all tests
cd build && ctest

# Verbose output
cd build && ctest --output-on-failure

# Run with timeout
cd build && ctest --timeout 120
```

## Installation

Install to system directories:
```bash
sudo cmake --install build
```

Install to custom prefix:
```bash
cmake --install build --prefix $HOME/.local
```

Installed files:
- **Library**: `lib/liblua.a` (and `liblua.so` if shared)
- **Headers**: `include/lua/*.h`
- **Executable**: `bin/lua`
- **CMake config**: `lib/cmake/Lua/` (for downstream projects)

## Using Lua in CMake Projects

After installation, use Lua in other CMake projects:

```cmake
find_package(Lua 5.5 REQUIRED)

add_executable(myapp main.cpp)
target_link_libraries(myapp PRIVATE Lua::liblua_static)
```

## Parallel Builds

Use all CPU cores for faster compilation:

```bash
# With CMake
cmake --build build --parallel

# Or with make directly
cmake --build build -- -j$(nproc)
```

## Cleaning

```bash
# Clean build artifacts
cmake --build build --target clean

# Full clean (remove build directory)
rm -rf build
```

## IDE Support

CMake generates build files for various IDEs:

**Visual Studio Code**: CMake Tools extension auto-detects `CMakeLists.txt`

**CLion**: Opens `CMakeLists.txt` directly

**Ninja**: Fast build system
```bash
cmake -B build -G Ninja
ninja -C build
```

## Comparison with Makefile

The original `makefile` is still available for compatibility. Both build systems produce identical binaries.

| Feature | Makefile | CMake |
|---------|----------|-------|
| Out-of-source builds | ❌ | ✅ |
| IDE integration | ❌ | ✅ |
| Cross-platform | ⚠️ Linux only | ✅ |
| Install target | ❌ | ✅ |
| Package config | ❌ | ✅ |
| CTest integration | ❌ | ✅ |
| Build speed | Similar | Similar |

## Troubleshooting

**C++23 not supported:**
Update compiler (GCC 11+, Clang 15+) or CMake (3.20+)

**Missing dependencies:**
Install required packages: `libreadline-dev` (optional)

**Test failures:**
Run with verbose output: `ctest --output-on-failure --verbose`
