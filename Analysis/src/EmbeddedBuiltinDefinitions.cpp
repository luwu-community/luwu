// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/BuiltinDefinitions.h"

LUAU_FASTFLAG(LuauIntegerLibrary)
LUAU_FASTFLAG(LuauIntegerType2)
LUAU_FASTFLAG(LuauAllowGlobalDeclarationToBeCalledClass)
LUAU_FASTFLAG(LuwuClasses)
LUAU_FASTFLAG(LuwuTraits)
LUAU_FASTFLAG(LuauUdtfTypeIsSubtypeOf)
LUAU_FASTFLAG(LuwuBufferIsFrozen)
LUAU_FASTFLAG(LuwuTableDrop)

namespace Luau
{

static constexpr const char* kBuiltinDefinitionBaseSrc = R"BUILTIN_SRC(

@checked declare function require(target: any): any

@checked declare function getfenv(target: any): { [string]: any }

declare _G: any
declare _VERSION: string
declare none: none

declare function gcinfo(): number

declare function print<T...>(...: T...)

declare function type<T>(value: T): string
declare function typeof<T>(value: T): string

-- `assert` has a magic function attached that will give more detailed type information
declare function assert<T>(value: T, errorMessage: string?): T
declare function error<T>(message: T, level: number?): never

declare function tostring<T>(value: T): string
declare function tonumber<T>(value: T, radix: number?): number?

declare function rawequal<T1, T2>(a: T1, b: T2): boolean
declare function rawget<K, V>(tab: {[K]: V}, k: K): V?
declare function rawset<K, V>(tab: {[K]: V}, k: K, v: V): {[K]: V}
declare function rawlen<K, V>(obj: {[K]: V} | string): number

declare function setfenv<T..., R...>(target: number | (T...) -> R..., env: {[string]: any}): ((T...) -> R...)?

declare function ipairs<V>(tab: {V}): (({V}, number) -> (number?, V), {V}, number)

declare function pcall<A..., R...>(f: (A...) -> R..., ...: A...): (boolean, R...)

-- FIXME: The actual type of `xpcall` is:
-- <E, A..., R1..., R2...>(f: (A...) -> R1..., err: (E) -> R2..., A...) -> (true, R1...) | (false, R2...)
-- Since we can't represent the return value, we use (boolean, R1...).
declare function xpcall<E, A..., R1..., R2...>(f: (A...) -> R1..., err: (E) -> R2..., ...: A...): (boolean, R1...)

-- `select` has a magic function attached to provide more detailed type information
declare function select<A...>(i: string | number, ...: A...): ...any

-- FIXME: This type is not entirely correct - `loadstring` returns a function or
-- (nil, string).
declare function loadstring<A...>(src: string, chunkname: string?): (((A...) -> any)?, string?)

@checked declare function newproxy(mt: boolean?): any

-- Cannot use `typeof` here because it will produce a polytype when we expect a monotype.
declare function unpack<V>(tab: {V}, i: number?, j: number?): ...V

)BUILTIN_SRC";

static constexpr const char* kBuiltinDefinitionBit32Src = R"BUILTIN_SRC(

declare bit32: {
    band: @checked (...number) -> number,
    bor: @checked (...number) -> number,
    bxor: @checked (...number) -> number,
    btest: @checked (number, ...number) -> boolean,
    rrotate: @checked (x: number, disp: number) -> number,
    lrotate: @checked (x: number, disp: number) -> number,
    lshift: @checked (x: number, disp: number) -> number,
    arshift: @checked (x: number, disp: number) -> number,
    rshift: @checked (x: number, disp: number) -> number,
    bnot: @checked (x: number) -> number,
    extract: @checked (n: number, field: number, width: number?) -> number,
    replace: @checked (n: number, v: number, field: number, width: number?) -> number,
    countlz: @checked (n: number) -> number,
    countrz: @checked (n: number) -> number,
    byteswap: @checked (n: number) -> number,
}

)BUILTIN_SRC";

static constexpr const char* kBuiltinDefinitionMathSrc = R"BUILTIN_SRC(

declare math: {
    frexp: @checked (n: number) -> (number, number),
    ldexp: @checked (s: number, e: number) -> number,
    fmod: @checked (x: number, y: number) -> number,
    modf: @checked (n: number) -> (number, number),
    pow: @checked (x: number, y: number) -> number,
    exp: @checked (n: number) -> number,

    ceil: @checked (n: number) -> number,
    floor: @checked (n: number) -> number,
    abs: @checked (n: number) -> number,
    sqrt: @checked (n: number) -> number,

    log: @checked (n: number, base: number?) -> number,
    log10: @checked (n: number) -> number,

    rad: @checked (n: number) -> number,
    deg: @checked (n: number) -> number,

    sin: @checked (n: number) -> number,
    cos: @checked (n: number) -> number,
    tan: @checked (n: number) -> number,
    sinh: @checked (n: number) -> number,
    cosh: @checked (n: number) -> number,
    tanh: @checked (n: number) -> number,
    atan: @checked (n: number) -> number,
    acos: @checked (n: number) -> number,
    asin: @checked (n: number) -> number,
    atan2: @checked (y: number, x: number) -> number,

    min: @checked (number, ...number) -> number,
    max: @checked (number, ...number) -> number,

    pi: number,
    huge: number,
    nan: number,
    e: number,
    phi: number,
    sqrt2: number,
    tau: number,

    randomseed: @checked (seed: number) -> (),
    random: @checked (number?, number?) -> number,

    sign: @checked (n: number) -> number,
    clamp: @checked (n: number, min: number, max: number) -> number,
    noise: @checked (x: number, y: number?, z: number?) -> number,
    round: @checked (n: number) -> number,
    map: @checked (x: number, inmin: number, inmax: number, outmin: number, outmax: number) -> number,
    lerp: @checked (a: number, b: number, t: number) -> number,

    isnan: @checked (x: number) -> boolean,
    isinf: @checked (x: number) -> boolean,
    isfinite: @checked (x: number) -> boolean,
}

)BUILTIN_SRC";

static constexpr const char* kBuiltinDefinitionOsSrc = R"BUILTIN_SRC(

type DateTypeArg = {
    year: number,
    month: number,
    day: number,
    hour: number?,
    min: number?,
    sec: number?,
    isdst: boolean?,
}

type DateTypeResult = {
    year: number,
    month: number,
    wday: number,
    yday: number,
    day: number,
    hour: number,
    min: number,
    sec: number,
    isdst: boolean,
}

declare os: {
    time: (time: DateTypeArg?) -> number,
    date: ((formatString: "*t" | "!*t", time: number?) -> DateTypeResult) & ((formatString: string?, time: number?) -> string),
    difftime: (t2: DateTypeResult | number, t1: DateTypeResult | number) -> number,
    clock: () -> number,
}

)BUILTIN_SRC";

static constexpr const char* kBuiltinDefinitionCoroutineSrc = R"BUILTIN_SRC(

declare coroutine: {
    create: <A..., R...>(f: (A...) -> R...) -> thread,
    resume: <A..., R...>(co: thread, A...) -> (boolean, R...),
    running: () -> thread,
    status: @checked (co: thread) -> "dead" | "running" | "normal" | "suspended",
    wrap: <A..., R...>(f: (A...) -> R...) -> ((A...) -> R...),
    yield: <A..., R...>(A...) -> R...,
    isyieldable: () -> boolean,
    close: @checked (co: thread) -> (boolean, any)
}

)BUILTIN_SRC";

static constexpr const char* kBuiltinDefinitionTableSrc = R"BUILTIN_SRC(

declare table: {
    concat: <V>(t: {V}, sep: string?, i: number?, j: number?) -> string,
    insert: (<V>(t: {V}, value: V) -> ()) & (<V>(t: {V}, pos: number, value: V) -> ()),
    maxn: <V>(t: {V}) -> number,
    remove: <V>(t: {V}, number?) -> V?,
    sort: <V>(t: {V}, comp: ((V, V) -> boolean)?) -> (),
    create: <V>(count: number, value: V?) -> {V},
    find: <V>(haystack: {V}, needle: V, init: number?) -> number?,
    drop: <V>(t: {V}, value: V, count: number?) -> number,

    unpack: <V>(list: {V}, i: number?, j: number?) -> ...V,
    pack: <V>(...V) -> { n: number, [number]: V },

    getn: <V>(t: {V}) -> number,
    foreach: <K, V>(t: {[K]: V}, f: (K, V) -> ()) -> (),
    foreachi: <V>({V}, (number, V) -> ()) -> (),

    move: <V>(src: {V}, a: number, b: number, t: number, dst: {V}?) -> {V},
    clear: (table: {}) -> (),
    isfrozen: (t: {}) -> boolean,
}

)BUILTIN_SRC";

static constexpr const char* kBuiltinDefinitionDebugSrc = R"BUILTIN_SRC(

declare debug: {
    info: ((thread: thread, level: number, options: string) -> ...any) & ((level: number, options: string) -> ...any) & (<A..., R1...>(func: (A...) -> R1..., options: string) -> ...any),
    traceback: ((message: string?, level: number?) -> string) & ((thread: thread, message: string?, level: number?) -> string),
}

)BUILTIN_SRC";

static constexpr const char* kBuiltinDefinitionUtf8Src = R"BUILTIN_SRC(

declare utf8: {
    char: @checked (...number) -> string,
    charpattern: string,
    codes: @checked (str: string) -> ((string, number) -> (number, number), string, number),
    codepoint: @checked (str: string, i: number?, j: number?) -> ...number,
    len: @checked (s: string, i: number?, j: number?) -> (number?, number?),
    offset: @checked (s: string, n: number?, i: number?) -> number,
}

)BUILTIN_SRC";

static constexpr const char* kBuiltinDefinitionBufferSrcCore = R"BUILTIN_SRC(
--- Buffer API
declare buffer: {
    create: @checked (size: number) -> buffer,
    fromstring: @checked (str: string) -> buffer,
    tostring: @checked (b: buffer) -> string,
    len: @checked (b: buffer) -> number,
    copy: @checked (target: buffer, targetOffset: number, source: buffer, sourceOffset: number?, count: number?) -> (),
    fill: @checked (b: buffer, offset: number, value: number, count: number?) -> (),
    readi8: @checked (b: buffer, offset: number) -> number,
    readu8: @checked (b: buffer, offset: number) -> number,
    readi16: @checked (b: buffer, offset: number) -> number,
    readu16: @checked (b: buffer, offset: number) -> number,
    readi32: @checked (b: buffer, offset: number) -> number,
    readu32: @checked (b: buffer, offset: number) -> number,
    readf32: @checked (b: buffer, offset: number) -> number,
    readf64: @checked (b: buffer, offset: number) -> number,
    writei8: @checked (b: buffer, offset: number, value: number) -> (),
    writeu8: @checked (b: buffer, offset: number, value: number) -> (),
    writei16: @checked (b: buffer, offset: number, value: number) -> (),
    writeu16: @checked (b: buffer, offset: number, value: number) -> (),
    writei32: @checked (b: buffer, offset: number, value: number) -> (),
    writeu32: @checked (b: buffer, offset: number, value: number) -> (),
    writef32: @checked (b: buffer, offset: number, value: number) -> (),
    writef64: @checked (b: buffer, offset: number, value: number) -> (),
    readstring: @checked (b: buffer, offset: number, count: number) -> string,
    writestring: @checked (b: buffer, offset: number, value: string, count: number?) -> (),
    readbits: @checked (b: buffer, bitOffset: number, bitCount: number) -> number,
    writebits: @checked (b: buffer, bitOffset: number, bitCount: number, value: number) -> (),
)BUILTIN_SRC";

static constexpr const char* kBuiltinDefinitionBufferSrcInteger = R"BUILTIN_SRC(
    readinteger: @checked (b: buffer, offset: number) -> integer,
    writeinteger: @checked (b: buffer, offset: number, value: integer) -> (),
)BUILTIN_SRC";

static constexpr const char* kBuiltinDefinitionBufferSrcIsFrozen = R"BUILTIN_SRC(
    isfrozen: @checked (b: buffer) -> boolean,
)BUILTIN_SRC";

static constexpr const char* kBuiltinDefinitionBufferSrcClose = R"BUILTIN_SRC(
}

)BUILTIN_SRC";

static const char* const kBuiltinDefinitionVectorSrc = R"BUILTIN_SRC(

-- `vector` is a language primitive that reuses the ExternType representation; its type (a nominal
-- root with read x/y/z fields + operator metatable) is created in C++ as `builtinTypes->vectorType`
-- and bound to the name `vector` in GlobalTypes (see makeVectorMetatable). Only the library value
-- is declared here.

declare vector: {
    create: @checked (x: number, y: number, z: number?) -> vector,
    magnitude: @checked (vec: vector) -> number,
    normalize: @checked (vec: vector) -> vector,
    cross: @checked (vec1: vector, vec2: vector) -> vector,
    dot: @checked (vec1: vector, vec2: vector) -> number,
    angle: @checked (vec1: vector, vec2: vector, axis: vector?) -> number,
    floor: @checked (vec: vector) -> vector,
    ceil: @checked (vec: vector) -> vector,
    abs: @checked (vec: vector) -> vector,
    sign: @checked (vec: vector) -> vector,
    clamp: @checked (vec: vector, min: vector, max: vector) -> vector,
    max: @checked (vector, ...vector) -> vector,
    min: @checked (vector, ...vector) -> vector,
    lerp: @checked (vec1: vector, vec2: vector, t: number) -> vector,

    zero: vector,
    one: vector,
}

)BUILTIN_SRC";

static const char* const kBuiltinDefinitionIntegerSrc = R"BUILTIN_SRC(

declare integer: {
    create: @checked (x: number) -> integer,
    tonumber: @checked (x: integer) -> number,
    neg: @checked (value: integer) -> integer,
    add: @checked (x: integer, y: integer) -> integer,
    sub: @checked (x: integer, y: integer) -> integer,
    mul: @checked (x: integer, y: integer) -> integer,
    div: @checked (x: integer, y: integer) -> integer,
    rem: @checked (x: integer, y: integer) -> integer,
    idiv: @checked (x: integer, y: integer) -> integer,
    mod: @checked (x: integer, y: integer) -> integer,
    udiv: @checked (x: integer, y: integer) -> integer,
    urem: @checked (x: integer, y: integer) -> integer,
    min: @checked (integer, ...integer) -> integer,
    max: @checked (integer, ...integer) -> integer,
    band: @checked (...integer) -> integer,
    bor: @checked (...integer) -> integer,
    bnot: @checked (x: integer) -> integer,
    bxor: @checked (...integer) -> integer,
    lt: @checked (x: integer, y: integer) -> boolean,
    le: @checked (x: integer, y: integer) -> boolean,
    ult: @checked (x: integer, y: integer) -> boolean,
    ule: @checked (x: integer, y: integer) -> boolean,
    gt: @checked (x: integer, y: integer) -> boolean,
    ge: @checked (x: integer, y: integer) -> boolean,
    ugt: @checked (x: integer, y: integer) -> boolean,
    uge: @checked (x: integer, y: integer) -> boolean,
    lshift: @checked (x: integer, numBitPositions: integer) -> integer,
    rshift: @checked (x: integer, numBitPositions: integer) -> integer,
    arshift: @checked (x: integer, numBitPositions: integer) -> integer,
    lrotate: @checked (x: integer, numBitPositions: integer) -> integer,
    rrotate: @checked (x: integer, numBitPositions: integer) -> integer,
    extract: @checked (value: integer, bitPosition: integer, numBits: integer?) -> integer,
    replace: @checked (value: integer, replacement: integer, bitPosition: integer, numBits: integer?) -> integer,
    clamp: @checked (value: integer, min: integer, max: integer) -> integer,
    btest: @checked (...integer) -> boolean,
    countrz: @checked (x: integer) -> integer,
    countlz: @checked (x: integer) -> integer,
    bswap: @checked (x: integer) -> integer,
    fromstring: @checked (str: string, base: number?) -> integer,
    minsigned: integer,
    maxsigned: integer
}

)BUILTIN_SRC";

static const char* kBuiltinDefinitionClassSrc = R"CLASS_SRC(
declare class: {
    isinstance: @checked (o: unknown, c: class) -> boolean,
    of: @checked (o: unknown) -> class?,
    name: @checked (o: class | object) -> string,
    fields: @checked (o: class | object) -> ({ [string]: unknown }, boolean)
}
)CLASS_SRC";

// Luwu Traits (rfcs/classes/traits.md): a trait's value is a `trait`. `class.isinstance` never matches one, so it
// doesn't take one either.
static const char* kBuiltinDefinitionClassTraitsSrc = R"CLASS_SRC(
declare class: {
    isinstance: @checked (o: unknown, c: class) -> boolean,
    implements: @checked (o: unknown, t: trait) -> boolean,
    of: @checked (o: unknown) -> class?,
    name: @checked (o: class | object | trait) -> string,
    fields: @checked (o: class | object | trait) -> ({ [string]: unknown }, boolean)
}
)CLASS_SRC";

std::string getBuiltinDefinitionSource()
{
    std::string result = kBuiltinDefinitionBaseSrc;

    result += kBuiltinDefinitionBit32Src;
    result += kBuiltinDefinitionMathSrc;
    result += kBuiltinDefinitionOsSrc;
    result += kBuiltinDefinitionCoroutineSrc;

    std::string tableSrc = kBuiltinDefinitionTableSrc;
    if (!FFlag::LuwuTableDrop)
    {
        std::string dropEntry = "    drop: <V>(t: {V}, value: V, count: number?) -> number,\n";
        size_t dropPos = tableSrc.find(dropEntry);
        if (dropPos != std::string::npos)
            tableSrc.erase(dropPos, dropEntry.size());
    }
    result += tableSrc;

    result += kBuiltinDefinitionDebugSrc;
    result += kBuiltinDefinitionUtf8Src;
    result += kBuiltinDefinitionBufferSrcCore;
    if (FFlag::LuauIntegerType2 && FFlag::LuauIntegerLibrary)
        result += kBuiltinDefinitionBufferSrcInteger;
    if (FFlag::LuwuBufferIsFrozen)
        result += kBuiltinDefinitionBufferSrcIsFrozen;
    result += kBuiltinDefinitionBufferSrcClose;

    result += kBuiltinDefinitionVectorSrc;

    if (FFlag::LuauIntegerType2 && FFlag::LuauIntegerLibrary)
    {
        result += kBuiltinDefinitionIntegerSrc;
    }

    if (FFlag::LuwuClasses && FFlag::LuauAllowGlobalDeclarationToBeCalledClass)
    {
        result += FFlag::LuwuTraits ? kBuiltinDefinitionClassTraitsSrc : kBuiltinDefinitionClassSrc;
    }

    return result;
}

// Luwu: the type function API's typedefs. Upstream's `type` is one table with every kind's methods, so calling one on
// the wrong kind type checks and fails when it runs. Here `type` is a union with one member per kind, each with a
// singleton `tag` and only the methods that work on it, and `t.tag == "table"` or `t:is("table")` narrows to the kind
// (MagicTypeFunctionIs). The runtime is the same. The tables the API only reads are `read`-only, so a table of
// narrower kinds can be passed.
std::string getTypeFunctionDefinitionSource()
{
    std::string common = R"(
    is: (self: type, tag: Tag) -> boolean,
)";
    if (FFlag::LuauUdtfTypeIsSubtypeOf)
        common += "    issubtypeof: (self: type, arg: type) -> boolean,\n";
    common += R"(
    metadata: (self: type) -> Metadata,
    setmetadata: (self: type, metadata: Metadata?) -> (),
)";

    // properties, indexers and metatables, which tables and extern types both have
    const char* members = R"(
    properties: (self: type) -> { [Singleton]: { read: type?, write: type?, comments: { Comment }? } },
    indexer: (self: type) -> { index: type, readresult: type, writeresult: type }?,
    readindexer: (self: type) -> { index: type, result: type }?,
    writeindexer: (self: type) -> { index: type, result: type }?,
    metatable: (self: type) -> type?,
)";

    auto variant = [&](const char* name, const std::string& tag, const std::string& own)
    {
        return std::string("export type ") + name + " = {\n    tag: " + tag + ",\n" + common + own + "}\n\n";
    };

    std::string primitiveTags = R"("nil" | "unknown" | "never" | "any" | "boolean" | "number" | "string" | "buffer" | "thread" | "none" | "error")";
    if (FFlag::LuauIntegerType2)
        primitiveTags += R"( | "integer")";

    std::string result = R"(
export type type = Primitive | Singleton | Negation | Union | Intersection | Table | Function | Extern | Object | Class | Trait | Generic

-- every kind's `tag`, what `type:is` takes
export type Tag = )" + primitiveTags + R"( | "singleton" | "negation" | "union" | "intersection" | "table" | "function" | "extern" | "object" | "class" | "trait" | "generic"

-- any type's metadata (`type:metadata()`, `type:setmetadata`): whatever a type function stores, plus doc comments.
-- `comments` holds the comments above and to the right of where the type was declared; a type function can replace
-- them, and only `---` and block comments (`--[=[`) count as docs.
export type Comment = { prefix: string, message: string, location: ("above" | "side")? }
export type Metadata = { comments: { Comment }?, [any]: any }

-- a field or function of an object, class or trait; `final`, `expected` and `optional` are only ever true in traits
export type Field = { type: type, private: boolean, const: boolean, final: boolean, expected: boolean }
export type FunctionMember = { type: type, private: boolean, final: boolean, expected: boolean, optional: boolean }

-- what `types.newclass` and `types.newtrait` take: a member is its type, or a table with the type and its flags. A
-- function with `method = true` takes the new object as `self`, put in front of its parameters.
export type FieldDefinition = type | { type: type, private: boolean?, const: boolean?, final: boolean?, expected: boolean? }
export type FunctionDefinition = type | { type: type, method: boolean?, private: boolean?, final: boolean?, expected: boolean?, optional: boolean? }
export type ClassDefinition = {
    fields: { [string]: FieldDefinition }?,
    functions: { [string]: FunctionDefinition }?,
    implements: { Trait }?,
    constructor: (type | { type: type, private: boolean? })?,
}
-- how `types.format` prints a type; `tostring(type)` uses the defaults
export type TypeFormatOptions = {
    -- "root" (the default): the type itself as its structure, the aliases inside it by name; "none": an alias as its
    -- name even at the top; "all": every alias as its structure
    expand: ("none" | "root" | "all")?,
    -- one table property per line, long unions and signatures broken up (default false)
    multiline: boolean?,
    -- spaces per indentation level when multiline (default 4)
    indent: number?,
    -- `(path: string) -> ()` rather than `(string) -> ()` (default true)
    parameternames: boolean?,
    -- when multiline, the first line of each property's doc comment above it, cut to this many characters (true is 80;
    -- default false)
    comments: (boolean | number)?,
    -- cut the type short past this many characters (default: the usual limit)
    maxlength: number?,
}
export type TraitDefinition = {
    fields: { [string]: FieldDefinition }?,
    functions: { [string]: FunctionDefinition }?,
    needs: { Trait }?,
}

)";
    result += variant("Primitive", primitiveTags, "");
    result += variant("Singleton", R"("singleton")", "    value: (self: type) -> (string | boolean | nil),\n");
    result += variant("Negation", R"("negation")", "    inner: (self: type) -> type,\n");
    result += variant("Union", R"("union")", "    components: (self: type) -> {type},\n");
    result += variant("Intersection", R"("intersection")", "    components: (self: type) -> {type},\n");
    result += variant("Table", R"("table")", std::string(members) + R"(
    setproperty: (self: type, key: type, value: type?) -> (),
    setpropertycomments: (self: type, key: type, comments: { Comment }?) -> (),
    setreadproperty: (self: type, key: type, value: type?) -> (),
    setwriteproperty: (self: type, key: type, value: type?) -> (),
    readproperty: (self: type, key: type) -> type?,
    writeproperty: (self: type, key: type) -> type?,
    setindexer: (self: type, index: type, result: type) -> (),
    setreadindexer: (self: type, index: type, result: type) -> (),
    setwriteindexer: (self: type, index: type, result: type) -> (),
    setmetatable: (self: type, arg: type) -> (),
)");
    result += variant("Function", R"("function")", R"(
    setparameters: (self: type, head: { read [number]: type }?, tail: type?) -> (),
    parameters: (self: type) -> { head: {type}?, tail: type? },
    setreturns: (self: type, head: { read [number]: type }?, tail: type?) -> (),
    returns: (self: type) -> { head: {type}?, tail: type? },
    setgenerics: (self: type, generics: { read [number]: type }?) -> (),
    generics: (self: type) -> {type},
    getparameternames: (self: type) -> { [number]: string },
    setparametername: (self: type, index: number, name: string?) -> (),
)");
    result += variant("Extern", R"("extern")", std::string(members) + R"(
    name: (self: type) -> string,
    readparent: (self: type) -> type?,
    writeparent: (self: type) -> type?,
)");
    // what `Cat` or `Greeter` means in an annotation: an object of a class, or of any class implementing a trait
    result += variant("Object", R"("object")", R"(
    name: (self: type) -> string,
    class: (self: type) -> Class?,
    traits: (self: type) -> { Trait },
    typearguments: (self: type) -> { type },
    fields: (self: type) -> { [string]: Field },
    functions: (self: type) -> { [string]: FunctionMember },
)");
    // `class<Cat>`
    result += variant("Class", R"("class")", R"(
    name: (self: type) -> string,
    object: (self: type) -> Object,
    traits: (self: type) -> { Trait },
    functions: (self: type) -> { [string]: FunctionMember },
    constructor: (self: type) -> { type: type, private: boolean }?,
)");
    // `trait<Greeter>`
    result += variant("Trait", R"("trait")", R"(
    name: (self: type) -> string,
    object: (self: type) -> Object,
    needs: (self: type) -> { Trait },
    fields: (self: type) -> { [string]: Field },
    functions: (self: type) -> { [string]: FunctionMember },
)");
    result += variant("Generic", R"("generic")", R"(
    name: (self: type) -> string?,
    ispack: (self: type) -> boolean,
)");

    // what each constructor makes is known, so a new table can be filled in without narrowing it first. `unionof` and
    // `intersectionof` of one type return that type, so they stay `type`. `newtable` reads its tables' keys as types at
    // run time; they're `any` here because a key table is never read by an arbitrary `type`, only iterated, and a
    // read-only indexer's key is still invariant, so `{ [Singleton]: ... }` from a local wouldn't be a `type`-keyed
    // table.
    result += R"(
declare types: {
    unknown: Primitive,
    never: Primitive,
    any: Primitive,
    boolean: Primitive,
    number: Primitive,
    string: Primitive,
    thread: Primitive,
    buffer: Primitive,
    none: Primitive,
)";
    if (FFlag::LuauIntegerType2)
        result += "    integer: Primitive,\n";
    result += R"(
    -- `types.singleton(nil)` is the `nil` primitive
    singleton: @checked (arg: string | boolean | nil) -> Singleton | Primitive,
    error: @checked (message: string) -> Primitive,
    optional: @checked (arg: type) -> Union,
    generic: @checked (name: string, ispack: boolean?) -> Generic,
    negationof: @checked (arg: type) -> Negation,
    unionof: @checked (...type) -> type,
    intersectionof: @checked (...type) -> type,
    newtable: @checked (
        props: { read [any]: type } | { read [any]: { read read: type?, read write: type? } }?,
        indexer: { read index: type, read readresult: type, read writeresult: type }?,
        metatable: type?
    ) -> Table,
    newfunction: @checked (
        parameters: { read head: { read [number]: type }?, read tail: type? }?,
        returns: { read head: { read [number]: type }?, read tail: type? }?,
        generics: { read [number]: type }?
    ) -> Function,
    copy: @checked <T>(arg: T) -> T,
    -- a class or trait that's only a type, like `declare class type`; two with the same name, members and traits are the
    -- same type
    newclass: @checked (name: string, definition: ClassDefinition?) -> Class,
    newtrait: @checked (name: string, definition: TraitDefinition?) -> Trait,
    -- `arg` as text, the way a hover shows it
    format: @checked (arg: type, options: TypeFormatOptions?) -> string,
}
)";

    return result;
}

} // namespace Luau
