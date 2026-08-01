-- This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
print("testing null")

-- falsiness
assert((not null) == true)
if null then
    error("null should be falsy")
end

-- equality
assert(null == null)
assert(nil ~= null)
assert(null ~= nil)
assert(null ~= false)
assert(null ~= 0)

-- type and tostring
assert(type(null) == "null")
assert(tostring(null) == "null")

-- tables: values should not leave holes like nil does
local t = {1, null, 3}
assert(#t == 3)
assert(t[1] == 1)
assert(t[2] == null)
assert(t[3] == 3)

t[2] = null
assert(#t == 3)
assert(t[2] == null)

-- tables: keys
local k = {}
k[null] = "value"
assert(k[null] == "value")
k[null] = nil
assert(k[null] == nil)

return('OK')
