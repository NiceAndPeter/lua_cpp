# Lua C++ - Modern C++23 Conversion of Lua 5.5

A comprehensive modernization of Lua from C to C++23, achieving **zero performance regression** while adding type safety and encapsulation.

## Project Status

**Performance**: 2.14s (3% faster than 2.17s baseline) ✅
**Converted**: 19 structs → classes (100%)
**Encapsulated**: 13/19 classes fully private (68%)
**Macros Converted**: ~500 (37% of convertible macros)
**Build Status**: Zero warnings with `-Werror`
**Tests**: All passing

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

### Fully Encapsulated Classes

- ✅ `Table`, `TString`, `Proto`, `UpVal` - Core data types
- ✅ `CClosure`, `LClosure` - Closure types
- ✅ `CallInfo`, `GCObject` - VM internals
- ✅ `expdesc`, `LocVar`, `AbsLineInfo`, `Upvaldesc` - Compiler types
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

# Target: ≤2.21s (1% tolerance)
# Current: ~2.14s ✓
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

- **[claude.md](claude.md)** - Comprehensive project guide and architecture
- **[ENCAPSULATION_PLAN.md](ENCAPSULATION_PLAN.md)** - Encapsulation roadmap (Phases 37-42)
- **[CONSTRUCTOR_PLAN.md](CONSTRUCTOR_PLAN.md)** - Constructor conversion details
- **[CMAKE_BUILD.md](CMAKE_BUILD.md)** - Build system documentation

## Key Achievements

- **500+ macros** converted to inline constexpr functions
- **CRTP implementation** across all GC types for zero-cost polymorphism
- **Performance improvement** despite adding type safety (2.14s vs 2.17s)
- **Zero API breakage** - Full backward compatibility maintained
- **Modern exception handling** - Cleaner error propagation

## Contributing

This is an experimental modernization project. The focus is on:

1. Maintaining zero performance regression
2. Preserving full C API compatibility
3. Incremental, tested improvements
4. Comprehensive benchmarking after every change

## Performance Philosophy

**Zero tolerance for regression**:
- Every change must be benchmarked
- Target: ≤2.21s (≤1% from baseline)
- Current: 2.14s (3% improvement) ✓
- Immediate revert if performance degrades

## License

Same as Lua - see the [official Lua repository](https://www.lua.org/) for license details.

## Related Links

- [Lua Official Site](https://www.lua.org/)
- [Lua Downloads](https://www.lua.org/download.html)
- [Lua Mailing List](https://www.lua.org/lua-l.html)

---

**Note**: This is a C++ modernization project, not the official Lua repository. For official Lua releases and support, visit [Lua.org](https://www.lua.org/).
