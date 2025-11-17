#!/bin/bash
echo "=== Side-by-side benchmark: Macro vs Lambda ==="
echo ""

for i in {1..10}; do
    echo "=== Iteration $i ==="
    
    echo -n "MACRO: "
    timeout 180 ../build/lua_macro all.lua 2>&1 | grep "total time:"
    
    echo -n "LAMBDA: "
    timeout 180 ../build/lua_lambda all.lua 2>&1 | grep "total time:"
    
    echo ""
done
