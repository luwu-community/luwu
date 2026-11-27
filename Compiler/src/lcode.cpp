// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "luacode.h"

#include "Luau/Compiler.h"

#include <stdlib.h>
#include <string.h>

// Luwu: the bytecode in a buffer the caller frees with free()
static char* copyBytecode(const std::string& bytecode, size_t* outsize)
{
    char* copy = static_cast<char*>(malloc(bytecode.size()));
    if (!copy)
        return nullptr;

    memcpy(copy, bytecode.data(), bytecode.size());
    *outsize = bytecode.size();
    return copy;
}

// Luwu: the fields lua_CompileOptions and luwu_CompileOptions share, copied into the compiler's options
template<typename Options>
static void copySharedOptions(Luau::CompileOptions& opts, const Options& options)
{
    opts.optimizationLevel = options.optimizationLevel;
    opts.debugLevel = options.debugLevel;
    opts.typeInfoLevel = options.typeInfoLevel;
    opts.coverageLevel = options.coverageLevel;
    opts.vectorLib = options.vectorLib;
    opts.vectorCtor = options.vectorCtor;
    opts.vectorType = options.vectorType;
    opts.mutableGlobals = options.mutableGlobals;
    opts.userdataTypes = options.userdataTypes;
    opts.librariesWithKnownMembers = options.librariesWithKnownMembers;
    opts.libraryMemberTypeCb = options.libraryMemberTypeCb;
    opts.libraryMemberConstantCb = options.libraryMemberConstantCb;
    opts.disabledBuiltins = options.disabledBuiltins;
}

char* luau_compile(const char* source, size_t size, lua_CompileOptions* options, size_t* outsize)
{
    LUAU_ASSERT(outsize);

    // Luwu: upstream's entry point compiles Luau with upstream's options, so an embedder written against upstream
    // compiles its code the way upstream does
    Luau::CompileOptions opts;
    if (options)
        copySharedOptions(opts, *options);
    opts.language = LUA_LANGUAGE_LUAU;

    return copyBytecode(compile(std::string(source, size), opts), outsize);
}

void luwu_init_compile_options(luwu_CompileOptions* options)
{
    memset(options, 0, sizeof(*options));
    options->optimizationLevel = 1;
    options->debugLevel = 1;
    options->typeInfoLevel = 1;
    options->language = LUA_LANGUAGE_LUWU;
}

char* luwu_compile(const char* source, size_t size, const luwu_CompileOptions* options, size_t* outsize)
{
    LUAU_ASSERT(outsize);

    luwu_CompileOptions defaults;
    if (!options)
    {
        luwu_init_compile_options(&defaults);
        options = &defaults;
    }

    Luau::CompileOptions opts;
    copySharedOptions(opts, *options);
    opts.vectorPrecision = options->vectorPrecision;
    opts.language = options->language;

    return copyBytecode(compile(std::string(source, size), opts), outsize);
}

void luau_set_compile_constant_nil(lua_CompileConstant* constant)
{
    Luau::setCompileConstantNil(constant);
}

void luau_set_compile_constant_boolean(lua_CompileConstant* constant, int b)
{
    Luau::setCompileConstantBoolean(constant, b != 0);
}

void luau_set_compile_constant_number(lua_CompileConstant* constant, double n)
{
    Luau::setCompileConstantNumber(constant, n);
}

void luau_set_compile_constant_integer64(lua_CompileConstant* constant, int64_t l)
{
    Luau::setCompileConstantInteger64(constant, l);
}

void luau_set_compile_constant_vector(lua_CompileConstant* constant, float x, float y, float z, float w)
{
    Luau::setCompileConstantVector(constant, x, y, z, w);
}

void luau_set_compile_constant_vectord(lua_CompileConstant* constant, double x, double y, double z, double w)
{
    Luau::setCompileConstantVectord(constant, x, y, z, w);
}

void luau_set_compile_constant_string(lua_CompileConstant* constant, const char* s, size_t l)
{
    Luau::setCompileConstantString(constant, s, l);
}
