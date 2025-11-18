# Lua C++ - Modern C++23 Conversion of Lua 5.5

A comprehensive modernization of Lua from C to C++23, achieving **zero performance regression** while adding type safety and encapsulation.

## Project Status

**Performance**: 4.20s baseline (current machine) ✅
**Converted**: 19 structs → classes (100%)
**Encapsulated**: 19/19 classes fully private (100%) ✅
**Macros Converted**: ~500 (37% of convertible macros)
**Enum Classes**: All major enums converted (Phases 96-100) ✅
**Build Status**: Zero warnings with `-Werror`
**Tests**: All passing
**Current Phase**: 100 - Enum class conversion complete

## Key Features

- **Zero Performance Regression**: Strict ≤1% tolerance enforced
- **Full C API Compatibility**: Public API unchanged
- **Modern C++23**: CRTP, inline constexpr, enum classes
- **Type Safety**: Replaced macros with type-safe inline functions
- **Encapsulation**: Private fields with accessor methods
- **Exception Handling**: Modern C++ exceptions (replaced setjmp/longjmp)
- **Clean Architecture**: CRTP inheritance, organized source tree

## Architecture Highlights

### CRTP (Curiously Recurring Template Pattern)

Static polymorphism without vtable overhead:

```cpp
template<typename Derived>
class GCBase {
public:
    GCObject* next;
    lu_byte tt;
    lu_byte marked;

    bool isWhite() const noexcept { return testbits(marked, WHITEBITS); }
    bool isBlack() const noexcept { return testbit(marked, BLACKBIT); }
};

class Table : public GCBase<Table> { /* ... */ };
class TString : public GCBase<TString> { /* ... */ };
```

All 9 GC-managed classes inherit from `GCBase<Derived>` for zero-cost abstraction.

### Fully Encapsulated Classes (19/19 - 100%)

**Core Data Types:**
- ✅ `Table`, `TString`, `Proto`, `UpVal`, `Udata`
- ✅ `CClosure`, `LClosure` - Closure types

**VM Internals:**
- ✅ `lua_State`, `global_State`, `CallInfo`
- ✅ `GCObject`, `TValue` - Base types

**Compiler Types:**
- ✅ `FuncState`, `LexState`, `expdesc`
- ✅ `LocVar`, `AbsLineInfo`, `Upvaldesc`

**Utilities:**
- ✅ `stringtable` - String interning

## Building

### Requirements

- C++23 compatible compiler (GCC 13+ or Clang 16+)
- CMake 3.20+

### Build Commands

```bash
# Configure and build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Run tests
cd testes
../build/lua all.lua

# Expected output: "final OK !!!"
```

### Performance Benchmarking

```bash
cd testes
for i in 1 2 3 4 5; do
    ../build/lua all.lua 2>&1 | grep "total time:"
done

# Current machine baseline: 4.20s
# Target: ≤4.33s (3% tolerance)
# Historical baseline: 2.17s (different hardware)
```

## Project Structure

```
src/
├── objects/        - Core data types (Table, TString, Proto, UpVal)
├── core/          - VM core (ldo, lapi, ldebug, lstate)
├── vm/            - Bytecode interpreter
├── compiler/      - Parser and code generator
├── memory/        - GC and memory management
├── libraries/     - Standard libraries
├── auxiliary/     - Auxiliary library
├── serialization/ - Bytecode dump/undump
└── interpreter/   - Interactive interpreter
```

## Documentation

- **[CLAUDE.md](CLAUDE.md)** - ⭐ Comprehensive AI assistant guide with full project documentation
- **[CMAKE_BUILD.md](CMAKE_BUILD.md)** - Build system configuration and options
- **[REFACTORING_SUMMARY.md](REFACTORING_SUMMARY.md)** - SRP refactoring achievements (Phases 90-93)
- **[Documentation Index](CLAUDE.md#documentation-index)** - Complete index of all 29 documentation files organized by category

## Key Achievements

- **19/19 classes fully encapsulated** with private fields and comprehensive accessors ✅
- **500+ macros** converted to inline constexpr functions (37% of convertible)
- **CRTP implementation** across all 9 GC types for zero-cost polymorphism
- **SRP refactoring complete** - FuncState, global_State, Proto decomposed (6% faster!)
- **Enum class conversion** - All major enums modernized (Phases 96-100)
- **GC modularization** - Extracted GCCore, GCMarking, GCCollector modules
- **LuaStack centralization** - Complete stack encapsulation (Phase 94)
- **Zero API breakage** - Full C API compatibility maintained
- **Modern exception handling** - C++ exceptions replace setjmp/longjmp

## Contributing

This is an experimental modernization project. The focus is on:

1. Maintaining zero performance regression
2. Preserving full C API compatibility
3. Incremental, tested improvements
4. Comprehensive benchmarking after every change

## Performance Philosophy

**Strict performance enforcement**:
- Every significant change must be benchmarked
- Current machine target: ≤4.33s (≤3% from 4.20s baseline)
- Historical baseline: 2.17s (different hardware)
- Immediate revert if performance degrades beyond tolerance

## License

Same as Lua - see the [official Lua repository](https://www.lua.org/) for license details.

## Related Links

- [Lua Official Site](https://www.lua.org/)
- [Lua Downloads](https://www.lua.org/download.html)
- [Lua Mailing List](https://www.lua.org/lua-l.html)

---

**Note**: This is a C++ modernization project, not the official Lua repository. For official Lua releases and support, visit [Lua.org](https://www.lua.org/).
