local T = require('ltests')

collectgarbage("stop")
T.gcstate("pause")
local sup = {x = 0}
local a = setmetatable({}, {__newindex = sup})
print("Before: sup.x =", sup.x, "type:", type(sup.x))
T.gcstate("enteratomic")
assert(T.gccolor(sup) == "black")
a.x = {}   -- should not break the invariant
print("After: sup.x =", sup.x, "type:", type(sup.x))
assert(not (T.gccolor(sup) == "black" and T.gccolor(sup.x) == "white"))
T.gcstate("pause")  -- complete the GC cycle
sup.x.y = 10
print("Final: sup.x.y =", sup.x.y)
collectgarbage("restart")
print("Test passed!")
