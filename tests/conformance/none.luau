-- This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
print("testing none")

-- falsiness
assert((not none) == true)
if none then
    error("none should be falsy")
end

-- equality
assert(none == none)
assert(nil ~= none)
assert(none ~= nil)
assert(none ~= false)
assert(none ~= 0)

-- type and tostring
assert(type(none) == "none")
assert(tostring(none) == "none")

-- tables: values should not leave holes like nil does
local t = {1, none, 3}
assert(#t == 3)
assert(t[1] == 1)
assert(t[2] == none)
assert(t[3] == 3)

t[2] = none
assert(#t == 3)
assert(t[2] == none)

-- tables: keys
local k = {}
k[none] = "value"
assert(k[none] == "value")
k[none] = nil
assert(k[none] == nil)

return('OK')
