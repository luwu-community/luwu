-- This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
-- Luwu String Extensions (rfcs/string-extensions.md): trim, trimstart, trimend, startswith, endswith, contains, replace
-- and split's maxpieces argument
print("testing string extensions")

local function eq(a: { string }, b: { string }): boolean
    if #a ~= #b then
        return false
    end
    for i = 1, #a do
        if a[i] ~= b[i] then
            return false
        end
    end
    return true
end

local function raises(f: (...any) -> ...any, ...: any): boolean
    return not pcall(f, ...)
end

-- startswith, endswith, contains
assert(string.startswith("hello", "he"))
assert(not string.startswith("hello", "lo"))
assert(string.startswith("hello", ""))
assert(not string.startswith("he", "hello"))
assert(string.startswith("hello", "x", "y", "hel"))
assert(not string.startswith("hello", "x", "y"))
assert(("hello"):startswith("hello"))

assert(string.endswith("hello", "lo"))
assert(not string.endswith("hello", "he"))
assert(string.endswith("hello", ""))
assert(not string.endswith("lo", "hello"))
assert(string.endswith("hello", "x", "llo"))
assert(("hello"):endswith("o"))

assert(string.contains("hello", "ell"))
assert(not string.contains("hello", "le"))
assert(string.contains("hello", ""))
assert(string.contains("", ""))
assert(not string.contains("", "a"))
assert(string.contains("hello", "x", "lo"))
assert(string.contains("a\0b", "\0"))
-- needles are literal, never patterns
assert(not string.contains("hello", "."))
assert(string.contains("1.5", "."))
assert(string.startswith("%d", "%d"))

-- a number needle falls back from the fastcall to the library function, which coerces it
assert(string.startswith("12abc", 12))
assert(string.contains("a1b", 1))

-- the first needle is required
assert(raises(string.startswith, "hello"))
assert(raises(string.endswith, "hello"))
assert(raises(string.contains, "hello"))

-- trim with no strings removes Unicode White_Space and the byte order mark
assert(string.trim("  hi  ") == "hi")
assert(string.trim("\t\n\v\f\r hi \r\n") == "hi")
assert(string.trim("  a b  ") == "a b")
assert(string.trim("   ") == "")
assert(string.trim("") == "")
assert(string.trim("\u{A0}hi\u{3000}") == "hi")
assert(string.trim("\u{2028}\u{2029}hi\u{85}\u{1680}") == "hi")
assert(string.trim("\u{2000}\u{200A}hi\u{202F}\u{205F}") == "hi")
assert(string.trim("\u{FEFF}hi") == "hi")
-- not White_Space: zero width space and the ASCII separators Python also strips
assert(string.trim("\u{200B}hi") == "\u{200B}hi")
assert(string.trim("\x1Chi") == "\x1Chi")
-- an invalid byte stops trimming, and a lone continuation byte isn't whitespace
assert(string.trim(" \xFF ") == "\xFF")
assert(string.trim("\xA0hi") == "\xA0hi")

assert(string.trimstart("  hi  ") == "hi  ")
assert(string.trimend("  hi  ") == "  hi")
assert(("  hi  "):trim() == "hi")

-- with strings, trim removes only those, repeatedly, and each string is a whole token
assert(string.trim("--hi--", "-") == "hi")
assert(string.trim("  hi  ", "-") == "  hi  ")
assert(string.trim("/path/", "/") == "path")
assert(string.trimend("line\r\n\r\n", "\r\n") == "line")
assert(string.trimend("line\n", "\r\n") == "line\n")
assert(string.trimend("line\r\n\n", "\r", "\n") == "line")
assert(string.trimstart("abab-x", "ab") == "-x")
assert(string.trim("xyhixy", "y", "x") == "hi")
assert(string.trim("aaa", "a") == "")
-- empty strings never match
assert(string.trim("  hi  ", "") == "  hi  ")
assert(string.trim("-hi-", "", "-") == "hi")
-- trim strings are literal, never patterns
assert(string.trim("..hi..", ".") == "hi")
assert(string.trim("hi", ".") == "hi")

-- replace
assert(string.replace("the cat sat", "at", "og") == "the cog sog")
assert(string.replace("aaaa", "a", "b", 2) == "bbaa")
assert(string.replace("aaaa", "a", "b", 0) == "aaaa")
assert(string.replace("aaaa", "a", "b", 10) == "bbbb")
assert(string.replace("hello", "x", "y") == "hello")
assert(string.replace("aaa", "aa", "b") == "ba")
assert(string.replace("a.b.c", ".", "") == "abc")
assert(string.replace("100%", "%", "%%") == "100%%")
assert(string.replace("x", "x", "%1") == "%1")
assert(string.replace("a\0b", "\0", "-") == "a-b")
assert(("hello"):replace("l", "L") == "heLLo")
assert(raises(string.replace, "hello", "", "x"))
assert(raises(string.replace, "hello", "l", "x", -1))

-- split's maxpieces argument: at most that many pieces, the last holding the rest
assert(eq(string.split("a,b,c", ",", 2), { "a", "b,c" }))
assert(eq(string.split("a,b,c", ",", 1), { "a,b,c" }))
assert(eq(string.split("a,b,c", ",", 3), { "a", "b", "c" }))
assert(eq(string.split("a,b,c", ",", 10), { "a", "b", "c" }))
assert(eq(string.split("a,b,c", ",", nil), { "a", "b", "c" }))
assert(eq(string.split("abc", "", 2), { "a", "bc" }))
assert(eq(string.split("abc", "", 1), { "abc" }))
assert(eq(string.split("key = a = b", " = ", 2), { "key", "a = b" }))
assert(raises(string.split, "a,b", ",", 0))

-- method calls on proven strings compile as function calls and behave the same
local literal = "Hello"
assert(literal:lower() == "hello")
assert(("abc"):startswith("a"))
local built = string.rep("ab", 2)
assert(built:sub(2, 3) == "ba")
assert(built:upper():lower() == "abab")
assert(("a" .. 1):len() == 2)

local function guarded(v: any): any
    if type(v) == "string" then
        return v:upper()
    end
    return v
end
assert(guarded("x") == "X")
assert(guarded(1) == 1)

local function asserted(v: any): number
    assert(type(v) == "string")
    return v:len()
end
assert(asserted("abc") == 3)
assert(not pcall(asserted, 1))

-- a method call that may not be on a string tries the builtin, and anything else runs its own method
local custom = {
    startswith = function(self, prefix)
        return "custom " .. prefix
    end,
    sub = function(self, i, j)
        return i + j
    end,
    len = function(self)
        return -1
    end,
}

local function callMethods(receiver: any, prefix: any): (any, any, any)
    return receiver:startswith(prefix), receiver:sub(1, 2), receiver:len()
end

for _ = 1, 3 do
    local a, b, c = callMethods("hello", "he")
    assert(a == true and b == "he" and c == 5)
    a, b, c = callMethods(custom, "x")
    assert(a == "custom x" and b == 3 and c == -1)
end

-- a number prefix falls back to the library function, which coerces it
assert(callMethods("12ab", 12) == true)

-- a name the string library doesn't have still raises as a missing method
local ok, message = pcall(function()
    return ("abc"):fo()
end)
assert(not ok and string.find(message, "fo", 1, true))

return "OK"
