-- This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
print("testing string.format and interpolated strings through the string.format fastcall")

local function check(actual, expected)
    assert(actual == expected, `expected {expected}, got {actual}`)
end

-- interpolation writes every plain value the way string.format's %* does
local values = { 0, -0, 1, -1, 1.5, 0.1, 1e15, 1e16, 2 ^ 53, 1 / 0, -1 / 0, "text", "", "100%", true, false }
for _, value in values do
    check(`<{value}>`, string.format("<%*>", value))
    check(`{value}{value}`, string.format("%*%*", value, value))
end
check(`{nil}`, "nil")
check(`a {1} b {"c"} d {true}`, "a 1 b c d true")
check(`100% {"sure"}`, "100% sure")

-- direct calls with %s, %d and %.Nf, including the ones only the call handles (width, flags, numeric strings)
for _, x in { 0, 1.005, 382.255, -0.001, 0.125, 1023.99999, 12345.678, -42.5, 1e10 } do
    for _, format in { "%.0f", "%.2f", "%.4f", "%.9f", "%8.2f", "%-8.2f|", "%e", "%g", "%5d", "%d" } do
        local fromCall = string.format(format, x)
        local f = string.format
        check(f(format, x), fromCall)
    end
end
check(string.format("%s and %s", "a", "b"), "a and b")
check(string.format("%d", "42"), "42")
check(string.format("%s", 12), "12")

-- a value with __tostring runs it inside string.format's frame, as without the fastcall
local seenFrom = nil
local withTostring = setmetatable({}, {
    __tostring = function()
        seenFrom = debug.info(2, "n")
        return "custom"
    end,
})
check(`[{withTostring}]`, "[custom]")
check(seenFrom, "format")
seenFrom = nil
check(string.format("[%*]", withTostring), "[custom]")
check(seenFrom, "format")

-- %s takes only strings and numbers, so a table raises from string.format whether or not it has __tostring
local tableOk, tableMessage = pcall(function()
    return string.format("[%s]", withTostring)
end)
assert(not tableOk and string.find(tableMessage, "string expected", 1, true))

-- errors still come from string.format
local ok, message = pcall(function()
    return string.format("%d", "not a number")
end)
assert(not ok and string.find(message, "format", 1, true))
ok, message = pcall(function()
    return string.format("%d %d", 1)
end)
assert(not ok and string.find(message, "missing argument", 1, true))

-- a local named `string` doesn't change what interpolation calls
do
    local string = { format = function() return "wrong" end }
    check(`{1}`, "1")
    assert(string.format() == "wrong")
end

-- a result longer than the fast path's buffer
local long = string.rep("x", 600)
check(`{long}{1}`, long .. "1")
check(#string.format("%s%s", long, long), 1200)

return "OK"
