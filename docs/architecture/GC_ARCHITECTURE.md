# Lua Garbage Collector: Architecture, Pitfalls & Modernization

**Last Updated**: 2025-12-14
**Purpose**: Comprehensive GC documentation combining pitfall analysis and simplification opportunities
**Status**: Reference documentation for GC modernization work

---

## Overview

This document combines two critical analyses of Lua's garbage collector:
1. **Part 1: GC Pitfalls** - Critical warnings, edge cases, and risks when modernizing
2. **Part 2: GC Simplification** - Opportunities for modularization and complexity reduction

The Lua GC uses a **tri-color incremental mark-and-sweep algorithm** with **generational optimization**. Understanding both the pitfalls AND the simplification opportunities is essential for safe modernization.

---

# PART 1: GC PITFALLS & RISKS

**Original**: GC_PITFALLS_ANALYSIS.md (2025-11-15)

## Executive Summary

Analysis reveals 10 critical pitfall categories and multiple edge cases that must be preserved during C++ modernization.

**Key Constraints for Modernization**:
- ❌ **std::vector/std::list for GC object lists** - Breaks sweep algorithm
- ❌ **std::shared_ptr for GC objects** - Conflicts with tri-color marking
- ❌ **std::unordered_set for object tracking** - Loses traversal order
- ✅ **Custom allocator wrapper** - Safe if preserves exact semantics
- ✅ **std::sort for table sorting** - Safe (non-GC algorithm)
- ✅ **std::span for array views** - Safe (view-only, no ownership)

---

## 1. TRI-COLOR MARKING SYSTEM

### 1.1 Color Encoding (lgc.h:107-113)

The GC uses 8 bits in the `marked` field:

```cpp
Bit 7: TESTBIT (for tests only)
Bit 6: FINALIZEDBIT (has __gc metamethod)
Bit 5: BLACKBIT (fully marked object)
Bit 4: WHITE1BIT (white shade 1)
Bit 3: WHITE0BIT (white shade 0)
Bits 0-2: Age (7 generational age values)
```

**Color States**:
- **WHITE** (0b00011000 or 0b00010000): Not visited - candidate for collection
- **GRAY** (0b00000000): Visited but children not processed - in work queue
- **BLACK** (0b00100000): Fully processed - safe until next cycle

**Critical Property** (lgc.h:138-140):
```cpp
inline bool GCObject::isGray() const noexcept {
  return !testbits(marked, bitmask(BLACKBIT) | WHITEBITS);
}
```
Gray is **absence of color**, not a separate bit! This means:
- Setting any color bit makes object non-gray
- Clearing all color bits makes object gray
- Race conditions in bit manipulation can cause immediate misclassification

### 1.2 Two-White Scheme (lgc.cpp:43-52, 78-83)

**Purpose**: Distinguish objects allocated DURING marking from old dead objects.

**Mechanism** (lgc.cpp:206-208):
```cpp
inline lu_byte otherwhite(const global_State* g) noexcept {
  return g->getCurrentWhite() ^ WHITEBITS;  // Flip both white bits
}
```

**Lifecycle**:
1. **GC cycle N begins**: currentWhite = WHITE0 (0b00001000)
2. **New allocation during marking**: Gets WHITE1 (0b00010000) - the "other" white
3. **Sweep phase**: Only objects with WHITE0 (old white) are dead
4. **Cycle N ends**: Flip white → currentWhite = WHITE1
5. **Cycle N+1 begins**: New objects get WHITE0 again

**Pitfall 1.2.1: Double-Free Risk if White Flips Early**

```cpp
// Thread 1: Sweep ongoing
lu_byte ow = otherwhite(g);  // ow = WHITE1 (0b00010000)

// Thread 2: Atomic phase completes, flips white
g->setCurrentWhite(otherwhite(g));  // now current = WHITE1

// Thread 1: Continues sweep
if (isdeadm(ow, obj->getMarked())) {  // TRUE for both whites now!
  freeobj(L, obj);  // DOUBLE FREE RISK!
}
```

**Mitigation**: Atomic phase runs without interruption; sweep uses cached `ow` value (lgc.cpp:992-1007). But custom threading or callbacks during GC could violate this.

---

## 2. INVARIANT: BLACK → WHITE FORBIDDEN

### 2.1 The Core Invariant (lgc.h:18-30)

> "A black object can never point to a white one."

**Why**: If black→white pointers allowed:
```
1. Object A marked BLACK (fully processed, won't revisit)
2. A writes pointer to WHITE object B
3. GC never marks B (A already processed)
4. B swept as garbage even though reachable
5. DANGLING POINTER
```

**Enforcement**: Write barriers (lgc.cpp:309-383)
- **Forward barrier**: Gray the white child
- **Backward barrier**: Re-gray the black parent

---

> **Note**: For the complete pitfall analysis, see the full documentation at:
> `/home/peter/claude/lua/docs/GC_PITFALLS_ANALYSIS.md` (before deletion)
>
> Key sections include:
> - Pointer-to-Pointer Sweep (Pitfall 3)
> - Weak Tables & Ephemerons (Pitfall 4)
> - Resurrection & Finalization (Pitfall 5)
> - Generational Mode (Pitfall 6)
> - Emergency GC (Pitfall 7)
> - Thread Interaction (Pitfall 8)
> - Userdata & External State (Pitfall 9)
> - Performance Tuning (Pitfall 10)

---

# PART 2: GC SIMPLIFICATION OPPORTUNITIES

**Original**: GC_SIMPLIFICATION_ANALYSIS.md (2025-11-17)

## Executive Summary

This analysis identifies **7 major opportunities** for simplification and modularization that can:

- ✅ **Reduce complexity** by ~30-40%
- ✅ **Improve maintainability** through better separation of concerns
- ✅ **Minimize codebase impact** through incremental refactoring
- ✅ **Preserve performance** (target: ≤4.33s, ≤3% regression from 4.20s baseline)
- ✅ **Maintain C API compatibility** completely

**Key Finding**: The GC is fundamentally necessary for Lua's semantics (circular references, weak tables, resurrection), but its **implementation complexity can be significantly reduced** without changing its core algorithm.

**Recommended Strategy**: **Incremental modularization** focusing on phase extraction, state machine cleanup, and list consolidation - NOT wholesale GC removal.

---

## 1. Current GC Architecture

### 1.1 Core Algorithm

**Type**: Tri-color incremental mark-and-sweep with generational optimization

**Color Encoding** (lgc.h:107-148):
- **WHITE**: Unmarked (candidate for collection) - 2 shades for new vs old
- **GRAY**: Marked but children unprocessed (work queue)
- **BLACK**: Fully processed (safe until next cycle)

**Critical Invariant**: Black objects never point to white objects (enforced by write barriers)

### 1.2 GC Phases (lstate.h:154-164)

```
Pause → Propagate → EnterAtomic → Atomic → SweepAllGC → SweepFinObj →
        SweepToBeFnz → SweepEnd → CallFin → Pause
```

**8 distinct phases** with complex state transitions

### 1.3 Object Lists (16 concurrent lists!)

**Incremental Mode Lists** (10):
1. `allgc` - All collectable objects
2. `sweepgc` - Current sweep position (pointer-to-pointer)
3. `finobj` - Objects with finalizers
4. `gray` - Gray work queue
5. `grayagain` - Objects to revisit in atomic
6. `weak` - Weak-value tables
7. `ephemeron` - Weak-key tables
8. `allweak` - All-weak tables
9. `tobefnz` - Ready for finalization
10. `fixedgc` - Never collected (interned strings)

**Generational Mode Pointers** (+6):
11. `survival` - Survived one cycle
12. `old1` - Survived two cycles
13. `reallyold` - Survived 3+ cycles
14-16. Finobj generations (finobjsur, finobjold1, finobjrold)

### 1.4 Age State Machine (7 ages!)

```cpp
enum class GCAge : lu_byte {
  New       = 0,  // Created this cycle
  Survival  = 1,  // Survived 1 cycle
  Old0      = 2,  // Barrier-aged (not truly old)
  Old1      = 3,  // Survived 2 cycles
  Old       = 4,  // Truly old (skip in minor GC)
  Touched1  = 5,  // Old object modified this cycle
  Touched2  = 6   // Old object modified last cycle
};
```

**Age Transitions**: Complex state machine with 7 states and conditional transitions

---

## 2. Complexity Analysis

### 2.1 Code Size

| Module | Lines | Purpose |
|--------|-------|---------|
| lgc.cpp | 1,676 | Main GC logic |
| gc_marking.cpp | 556 | Marking phases |
| gc_weak.cpp | 387 | Weak table handling |
| gc_sweeping.cpp | 368 | Sweep phases |
| gc_finalizer.cpp | 325 | Finalization |
| gc_generational.cpp | 285 | Generational mode |
| **Total** | **3,597** | |

### 2.2 Complexity Metrics

- **Cyclomatic Complexity**: Very high (multiple nested switches, 16 lists, 8 phases)
- **Data Flow**: 16 concurrent object lists with complex handoffs
- **State Machine**: 8 GC phases + 7 age states = 56 potential combinations
- **Conditionals**: Heavy use of bit-testing, list checks, mode switches

---

## 3. Simplification Opportunities

### Opportunity 1: Extract Phase Logic into Classes ⭐⭐⭐⭐

**Problem**: 8 GC phases scattered across switch statements

**Proposal**: Phase objects with execute() method

```cpp
class GCPhase {
public:
  virtual GCState execute(lua_State* L, global_State* g) = 0;
  virtual const char* name() const = 0;
};

class PropagatePhase : public GCPhase {
  GCState execute(lua_State* L, global_State* g) override {
    // Current propagate logic here
  }
};
```

**Benefits**:
- Clear separation of concerns
- Easier testing (mock individual phases)
- Reduced switch complexity

**Risk**: LOW (phase logic already isolated)
**Impact**: Moderate (cleaner code structure)

---

### Opportunity 2: Consolidate Object Lists ⭐⭐⭐⭐⭐

**Problem**: 16 object lists (10 incremental + 6 generational)

**Proposal**: Reduce to 6-8 essential lists

**Analysis**:
- `gray` + `grayagain` could merge (both gray queues, different timing)
- `weak` + `ephemeron` + `allweak` could use single list with type tags
- Generational survival/old1/reallyold could collapse

**Benefits**:
- Simpler state management
- Fewer pointer-to-pointer manipulations
- Easier to reason about object lifecycle

**Risk**: MEDIUM (requires careful analysis of list semantics)
**Impact**: HIGH (significant complexity reduction)

---

### Opportunity 3: Simplify Age State Machine ⭐⭐⭐

**Problem**: 7 age states with complex transitions

**Proposal**: Reduce to 4 ages

```cpp
enum class GCAge : lu_byte {
  New       = 0,  // New objects (collect every cycle)
  Survival  = 1,  // Survived 1 cycle
  Old       = 2,  // Truly old (skip in minor GC)
  Touched   = 3   // Old object modified (check this cycle)
};
```

**Eliminate**:
- Old0 (barrier-aged) - merge into Survival
- Old1 - merge into Old
- Touched2 - merge into Touched

**Benefits**:
- Simpler state machine (4 states vs 7)
- Fewer edge cases
- Easier to understand age transitions

**Risk**: LOW-MEDIUM (may slightly reduce generational precision)
**Impact**: MEDIUM (clearer generational logic)

---

### Opportunity 4: Modularize Weak Table Handling ⭐⭐⭐⭐

**Problem**: Weak table logic scattered across gc_weak.cpp with complex traversal

**Proposal**: WeakTableManager class

```cpp
class WeakTableManager {
public:
  void clearWeakValues(global_State* g);
  void clearWeakKeys(global_State* g);
  void processEphemerons(global_State* g);
};
```

**Benefits**:
- Encapsulated weak table semantics
- Easier to test weak table edge cases
- Clear separation from main GC logic

**Risk**: LOW (already isolated in gc_weak.cpp)
**Impact**: MEDIUM (better encapsulation)

---

### Opportunity 5: Extract Write Barrier Logic ⭐⭐⭐

**Problem**: Forward/backward barriers mixed with main GC logic

**Proposal**: BarrierManager class

```cpp
class BarrierManager {
public:
  void forwardBarrier(global_State* g, GCObject* obj, GCObject* child);
  void backwardBarrier(global_State* g, GCObject* obj);
  void updateGenBarrier(global_State* g, GCObject* obj);
};
```

**Benefits**:
- Isolated invariant enforcement
- Easier to verify correctness
- Clear barrier semantics

**Risk**: LOW
**Impact**: LOW-MEDIUM (clearer code structure)

---

### Opportunity 6: Simplify Finalizer Queue ⭐⭐

**Problem**: 4 finalizer lists (finobj + 3 generational variants)

**Proposal**: Single finalizer queue with age tracking

**Benefits**:
- Simpler finalization logic
- Fewer list manipulations
- Clearer object lifecycle

**Risk**: MEDIUM (finalizers are tricky)
**Impact**: MEDIUM

---

### Opportunity 7: Extract Sweep Strategy ⭐⭐⭐⭐

**Problem**: Sweep logic tied to specific list traversal

**Proposal**: SweepStrategy interface

```cpp
class SweepStrategy {
public:
  virtual void sweep(lua_State* L, GCObject** list) = 0;
};

class AllGCSweep : public SweepStrategy { /* ... */ };
class FinObjSweep : public SweepStrategy { /* ... */ };
```

**Benefits**:
- Testable sweep logic
- Clear separation of sweep phases
- Easier to add custom sweep strategies

**Risk**: LOW
**Impact**: MEDIUM (cleaner sweep code)

---

## 4. Implementation Phases

### Phase 1: Extract Phase Logic (Low Risk) ✅

**Work**: Create GCPhase base class + 8 derived classes
**Effort**: 4-6 hours
**Benefit**: Foundation for future simplifications

### Phase 2: Modularize Weak Tables (Low Risk) ✅

**Work**: Extract WeakTableManager
**Effort**: 3-4 hours
**Benefit**: Isolated weak table semantics

### Phase 3: Extract Write Barriers (Low Risk) ✅

**Work**: Create BarrierManager
**Effort**: 3-4 hours
**Benefit**: Clear invariant enforcement

### Phase 4: Extract Sweep Strategy (Medium Risk) ⚠️

**Work**: Create SweepStrategy interface
**Effort**: 4-6 hours + testing
**Benefit**: Cleaner sweep logic

### Phase 5: Simplify Age States (Medium Risk) ⚠️

**Work**: Reduce 7 ages → 4 ages
**Effort**: 6-8 hours + extensive testing
**Benefit**: Simpler state machine

### Phase 6: Consolidate Lists (High Risk) ⚠️⚠️

**Work**: Reduce 16 lists → 6-8 lists
**Effort**: 10-15 hours + extensive testing
**Benefit**: Major complexity reduction

**IMPORTANT**: Only attempt after Phases 1-5 complete and proven stable

---

## 5. Impact Assessment

### Code Reduction Estimate

| Category | Current | After | Reduction |
|----------|---------|-------|-----------|
| GC phases | 8 switch cases | 8 classes | ✅ Better organized |
| Object lists | 16 lists | 6-8 lists | -50% |
| Age states | 7 states | 4 states | -43% |
| Weak logic | Scattered | 1 class | ✅ Encapsulated |
| Barriers | Inline | 1 class | ✅ Isolated |
| **Total Lines** | ~3,600 | ~2,400 | **-33%** |

### Maintainability Improvement

- ✅ **Clear ownership**: Each class owns its phase/strategy
- ✅ **Testable**: Mock individual components
- ✅ **Understandable**: Fewer edge cases, clearer state machine
- ✅ **Extensible**: Add new phases/strategies easily

---

## 6. Performance Considerations

### Performance Targets

**Current**: ~2.24s avg (47% faster than 4.20s baseline)
**Target**: ≤4.33s (≤3% regression from 4.20s baseline)
**Headroom**: Massive (2.24s vs 4.33s = 48% margin)

### Risk Assessment

**Low Risk Changes** (Phases 1-3):
- Class extraction is zero-cost at runtime (inline + optimization)
- Expected: No performance change

**Medium Risk Changes** (Phases 4-5):
- Virtual dispatch overhead minimal (GC not hot path)
- Age simplification may slightly reduce generational precision
- Expected: <1% performance impact

**High Risk Changes** (Phase 6):
- List consolidation affects traversal patterns
- Must benchmark carefully
- Expected: 1-3% impact (still well within budget)

---

## 7. Recommendations

### Immediate (Phase 158+)

1. **Extract Phase Logic** (Opportunity 1) - LOW RISK, foundational
2. **Modularize Weak Tables** (Opportunity 4) - LOW RISK, clear win
3. **Extract Write Barriers** (Opportunity 5) - LOW RISK, better encapsulation

### Medium Term

4. **Extract Sweep Strategy** (Opportunity 7) - MEDIUM RISK
5. **Simplify Age States** (Opportunity 3) - MEDIUM RISK

### Long Term (Defer)

6. **Consolidate Lists** (Opportunity 2) - HIGH RISK, requires extensive analysis
7. **Simplify Finalizer Queue** (Opportunity 6) - MEDIUM RISK, finalizers are tricky

### Not Recommended

- ❌ **Replace GC entirely** (circular refs, weak tables, resurrection require GC)
- ❌ **Use std::shared_ptr** (conflicts with tri-color marking)
- ❌ **Use std::vector for lists** (breaks pointer-to-pointer sweep)

---

## 8. Conclusion

The Lua GC can be significantly simplified through **incremental modularization** without sacrificing performance or correctness. The recommended approach:

1. **Start small**: Extract phase logic (Opportunity 1)
2. **Build confidence**: Modularize weak tables (Opportunity 4)
3. **Continue carefully**: Extract barriers, sweep strategies
4. **Defer risky changes**: List consolidation only after foundation proven

**Expected outcome**: 30-40% complexity reduction while maintaining excellent performance (2.24s avg, 47% faster than baseline).

---

## References

- **Source Code**: `src/memory/gc/` (6 modules, ~3,600 lines)
- **Documentation**: See [GC_PITFALLS_ANALYSIS.md](../GC_PITFALLS_ANALYSIS.md) for critical edge cases
- **Performance**: See CLAUDE.md for current benchmarks (~2.24s avg, target ≤4.33s)
- **Testing**: `testes/gc.lua` for comprehensive GC test coverage

---

**Last Updated**: 2025-12-14
**Status**: Merged documentation combining pitfalls and simplification analysis
**Next Steps**: Consider implementing Opportunities 1, 4, 5 in Phase 158+
