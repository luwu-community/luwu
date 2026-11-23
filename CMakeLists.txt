# This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
if(EXT_PLATFORM_STRING)
    include(EXTLuau.cmake)
    return()
endif()

cmake_minimum_required(VERSION 3.10)

option(LUAU_BUILD_CLI "Build CLI" ON)
option(LUAU_BUILD_TESTS "Build tests" ON)
option(LUAU_BUILD_WEB "Build Web module" OFF)
# Luwu: upstream always adds the libFuzzer targets when tests are built, which prints a warning on every
# configure with a compiler that can't build them (GCC, AppleClang). Here they are opt-in.
option(LUAU_BUILD_FUZZ "Build libFuzzer fuzz targets (needs LLVM Clang)" OFF)
option(LUAU_WERROR "Warnings as errors" OFF)
option(LUAU_STATIC_CRT "Link with the static CRT (/MT)" OFF)
option(LUAU_EXTERN_C "Use extern C for all APIs" OFF)
option(LUAU_BUILD_SHARED "Build as a shared library" OFF)

if(LUAU_BUILD_SHARED AND NOT LUAU_EXTERN_C)
    message(FATAL_ERROR "LUAU_BUILD_SHARED requires LUAU_EXTERN_C to be ON")
endif()

cmake_policy(SET CMP0054 NEW)
cmake_policy(SET CMP0091 NEW)

if(LUAU_STATIC_CRT)
    cmake_minimum_required(VERSION 3.15)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

project(Luau LANGUAGES CXX C)

if (LUAU_BUILD_SHARED)
    add_library(Luwu.Common SHARED)
    add_library(Luwu.CLI.lib SHARED)
    add_library(Luwu.Ast SHARED)
    add_library(Luwu.Bytecode SHARED)
    add_library(Luwu.Inliner SHARED)
    add_library(Luwu.Compiler SHARED)
    add_library(Luwu.Config SHARED)
    add_library(Luwu.Analysis SHARED)
    add_library(Luwu.CodeGen SHARED)
    add_library(Luwu.VM SHARED)
    add_library(Luwu.Require SHARED)
    add_library(isocline SHARED)
else()
    add_library(Luwu.Common STATIC)
    add_library(Luwu.CLI.lib STATIC)
    add_library(Luwu.Ast STATIC)
    add_library(Luwu.Bytecode STATIC)
    add_library(Luwu.Inliner STATIC)
    add_library(Luwu.Compiler STATIC)
    add_library(Luwu.Config STATIC)
    add_library(Luwu.Analysis STATIC)
    add_library(Luwu.CodeGen STATIC)
    add_library(Luwu.VM STATIC)
    add_library(Luwu.Require STATIC)
    add_library(isocline STATIC)
endif()

if(LUAU_BUILD_CLI)
    add_executable(Luwu.Repl.CLI)
    add_executable(Luwu.Analyze.CLI)
    add_executable(Luwu.Ast.CLI)
    add_executable(Luwu.Reduce.CLI)
    add_executable(Luwu.Compile.CLI)
    add_executable(Luwu.Bytecode.CLI)

    # This also adds target `name` on Linux/macOS and `name.exe` on Windows
    set_target_properties(Luwu.Repl.CLI PROPERTIES OUTPUT_NAME luwu)
    set_target_properties(Luwu.Analyze.CLI PROPERTIES OUTPUT_NAME luwu-analyze)
    set_target_properties(Luwu.Ast.CLI PROPERTIES OUTPUT_NAME luwu-ast)
    set_target_properties(Luwu.Reduce.CLI PROPERTIES OUTPUT_NAME luwu-reduce)
    set_target_properties(Luwu.Compile.CLI PROPERTIES OUTPUT_NAME luwu-compile)
    set_target_properties(Luwu.Bytecode.CLI PROPERTIES OUTPUT_NAME luwu-bytecode)
endif()

if(LUAU_BUILD_TESTS)
    add_executable(Luwu.UnitTest)
    add_executable(Luwu.Conformance)
    add_executable(Luwu.CLI.Test)
endif()

if(LUAU_BUILD_WEB)
    add_executable(Luwu.Web)
endif()

# Proxy target to make it possible to depend on private VM headers
add_library(Luwu.VM.Internals INTERFACE)

include(Sources.cmake)

target_compile_features(Luwu.Common PUBLIC cxx_std_17)
target_include_directories(Luwu.Common PUBLIC Common/include)

target_compile_features(Luwu.CLI.lib PUBLIC cxx_std_17)
target_include_directories(Luwu.CLI.lib PUBLIC CLI/include)
target_link_libraries(Luwu.CLI.lib PRIVATE Luwu.Common Luwu.Config)

target_compile_features(Luwu.Ast PUBLIC cxx_std_17)
target_include_directories(Luwu.Ast PUBLIC Ast/include)
target_link_libraries(Luwu.Ast PUBLIC Luwu.Common)

target_compile_features(Luwu.Bytecode PUBLIC cxx_std_17)
target_include_directories(Luwu.Bytecode PUBLIC Bytecode/include)
target_link_libraries(Luwu.Bytecode PUBLIC Luwu.Common)

target_compile_features(Luwu.Inliner PUBLIC cxx_std_17)
target_include_directories(Luwu.Inliner PUBLIC Inliner/include)
target_include_directories(Luwu.Inliner PRIVATE Bytecode/src)
target_link_libraries(Luwu.Inliner PRIVATE Luwu.VM Luwu.VM.Internals)
target_link_libraries(Luwu.Inliner PUBLIC Luwu.Bytecode)

target_compile_features(Luwu.Compiler PUBLIC cxx_std_17)
target_include_directories(Luwu.Compiler PUBLIC Compiler/include)
target_link_libraries(Luwu.Compiler PUBLIC Luwu.Ast Luwu.Bytecode)

target_compile_features(Luwu.Config PUBLIC cxx_std_17)
target_include_directories(Luwu.Config PUBLIC Config/include)
target_link_libraries(Luwu.Config PUBLIC Luwu.Ast)
target_link_libraries(Luwu.Config PRIVATE Luwu.Compiler Luwu.VM)

target_compile_features(Luwu.Analysis PUBLIC cxx_std_17)
target_include_directories(Luwu.Analysis PUBLIC Analysis/include)
target_link_libraries(Luwu.Analysis PUBLIC Luwu.Ast Luwu.Config)
target_link_libraries(Luwu.Analysis PRIVATE Luwu.Compiler Luwu.VM)

target_compile_features(Luwu.CodeGen PRIVATE cxx_std_17)
target_include_directories(Luwu.CodeGen PUBLIC CodeGen/include)
target_link_libraries(Luwu.CodeGen PRIVATE Luwu.VM Luwu.VM.Internals) # Code generation needs VM internals
target_link_libraries(Luwu.CodeGen PUBLIC Luwu.Common)

target_compile_features(Luwu.VM PRIVATE cxx_std_11)
target_include_directories(Luwu.VM PUBLIC VM/include)
target_link_libraries(Luwu.VM PUBLIC Luwu.Common)

target_compile_features(Luwu.Require PUBLIC cxx_std_17)
target_include_directories(Luwu.Require PUBLIC Require/include)
target_link_libraries(Luwu.Require PUBLIC Luwu.Config Luwu.VM)

target_include_directories(isocline PUBLIC extern/isocline/include)

target_include_directories(Luwu.VM.Internals INTERFACE VM/src)

set(LUAU_OPTIONS)

if(MSVC)
    list(APPEND LUAU_OPTIONS /D_CRT_SECURE_NO_WARNINGS) # We need to use the portable CRT functions.
    list(APPEND LUAU_OPTIONS "/we4018") # Signed/unsigned mismatch
    list(APPEND LUAU_OPTIONS "/we4388") # Also signed/unsigned mismatch
else()
    list(APPEND LUAU_OPTIONS -Wall) # All warnings
    list(APPEND LUAU_OPTIONS -Wimplicit-fallthrough)
    list(APPEND LUAU_OPTIONS -Wsign-compare) # This looks to be included in -Wall for GCC but not clang
endif()

if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    list(APPEND LUAU_OPTIONS /MP) # Distribute single project compilation across multiple cores
endif()

if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # Some gcc versions treat var in `if (type var = val)` as unused
    # Some gcc versions treat variables used in constexpr if blocks as unused
    list(APPEND LUAU_OPTIONS -Wno-unused)
    # some gcc versions warn maybe uninitialized on optional<string> members on structs
    list(APPEND LUAU_OPTIONS -Wno-maybe-uninitialized)
endif()

# Enabled in CI; we should be warning free on our main compiler versions but don't guarantee being warning free everywhere
if(LUAU_WERROR)
    if(MSVC)
        list(APPEND LUAU_OPTIONS /WX) # Warnings are errors
    else()
        list(APPEND LUAU_OPTIONS -Werror) # Warnings are errors
    endif()
endif()

if(LUAU_BUILD_WEB)
    # add -fexceptions for emscripten to allow exceptions to be caught in C++
    list(APPEND LUAU_OPTIONS -fexceptions)
endif()

set(ISOCLINE_OPTIONS)

if (NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    list(APPEND ISOCLINE_OPTIONS -Wno-unused-function)
endif()

target_compile_options(Luwu.Ast PRIVATE ${LUAU_OPTIONS})
target_compile_options(Luwu.Analysis PRIVATE ${LUAU_OPTIONS})
target_compile_options(Luwu.CLI.lib PRIVATE ${LUAU_OPTIONS})
target_compile_options(Luwu.CodeGen PRIVATE ${LUAU_OPTIONS})
target_compile_options(Luwu.VM PRIVATE ${LUAU_OPTIONS})
target_compile_options(isocline PRIVATE ${LUAU_OPTIONS} ${ISOCLINE_OPTIONS})

if(LUAU_EXTERN_C)
    # enable extern "C" for VM (lua.h, lualib.h) and Compiler (luacode.h) to make Luau friendlier to use from non-C++ languages
    # note that we enable LUA_USE_LONGJMP=1 as well; otherwise functions like luaL_error will throw C++ exceptions, which can't be done from extern "C" functions
    target_compile_definitions(Luwu.VM PUBLIC LUA_USE_LONGJMP=1)

    if(LUAU_BUILD_SHARED)
        if(MSVC)
            target_compile_definitions(Luwu.VM PUBLIC "LUA_API=extern \"C\" __declspec(dllexport)")
            target_compile_definitions(Luwu.Compiler PUBLIC "LUACODE_API=extern \"C\" __declspec(dllexport)")
            target_compile_definitions(Luwu.CodeGen PUBLIC "LUACODEGEN_API=extern \"C\" __declspec(dllexport)")
        else()
            # GCC/Clang
            target_compile_definitions(Luwu.VM PUBLIC "LUA_API=extern \"C\" __attribute__((visibility(\"default\")))")
            target_compile_definitions(Luwu.Compiler PUBLIC "LUACODE_API=extern \"C\" __attribute__((visibility(\"default\")))")
            target_compile_definitions(Luwu.CodeGen PUBLIC "LUACODEGEN_API=extern \"C\" __attribute__((visibility(\"default\")))")
        endif()
    else()
        # For static builds (or if not exporting) just use extern "C"
        target_compile_definitions(Luwu.VM PUBLIC "LUA_API=extern \"C\"")
        target_compile_definitions(Luwu.Compiler PUBLIC "LUACODE_API=extern \"C\"")
        target_compile_definitions(Luwu.CodeGen PUBLIC "LUACODEGEN_API=extern \"C\"")
    endif()
endif()

if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC" AND MSVC_VERSION GREATER_EQUAL 1924)
    # disable partial redundancy elimination which regresses interpreter codegen substantially in VS2022:
    # https://developercommunity.visualstudio.com/t/performance-regression-on-a-complex-interpreter-lo/1631863
    set_source_files_properties(VM/src/lvmexecute.cpp PROPERTIES COMPILE_FLAGS /d2ssa-pre-)
endif()

if (NOT MSVC)
    # disable support for math_errno which allows compilers to lower sqrt() into a single CPU instruction
    target_compile_options(Luwu.VM PRIVATE -fno-math-errno)
endif()

if(MSVC AND LUAU_BUILD_CLI)
    # the default stack size that MSVC linker uses is 1 MB; we need more stack space in Debug because stack frames are larger
    set_target_properties(Luwu.Analyze.CLI PROPERTIES LINK_FLAGS_DEBUG /STACK:2097152)
    set_target_properties(Luwu.Repl.CLI PROPERTIES LINK_FLAGS_DEBUG /STACK:2097152)
    set_target_properties(Luwu.UnitTest PROPERTIES LINK_FLAGS_DEBUG /STACK:2097152)
endif()

if(MSVC AND LUAU_BUILD_TESTS)
    # the default stack size that MSVC linker uses is 1 MB; we need more stack space in Debug because stack frames are larger
    set_target_properties(Luwu.CLI.Test PROPERTIES LINK_FLAGS_DEBUG /STACK:2097152)
    set_target_properties(Luwu.Conformance PROPERTIES LINK_FLAGS_DEBUG /STACK:2097152)
endif()

# embed .natvis inside the library debug information
if(MSVC)
    target_link_options(Luwu.Ast INTERFACE /NATVIS:${CMAKE_CURRENT_SOURCE_DIR}/tools/natvis/Ast.natvis)
    target_link_options(Luwu.Analysis INTERFACE /NATVIS:${CMAKE_CURRENT_SOURCE_DIR}/tools/natvis/Analysis.natvis)
    target_link_options(Luwu.CodeGen INTERFACE /NATVIS:${CMAKE_CURRENT_SOURCE_DIR}/tools/natvis/CodeGen.natvis)
    target_link_options(Luwu.VM INTERFACE /NATVIS:${CMAKE_CURRENT_SOURCE_DIR}/tools/natvis/VM.natvis)
endif()

# make .natvis visible inside the solution
if(MSVC_IDE)
    target_sources(Luwu.Ast PRIVATE tools/natvis/Ast.natvis)
    target_sources(Luwu.Analysis PRIVATE tools/natvis/Analysis.natvis)
    target_sources(Luwu.CodeGen PRIVATE tools/natvis/CodeGen.natvis)
    target_sources(Luwu.VM PRIVATE tools/natvis/VM.natvis)
endif()

# On Windows and Android threads are provided, on Linux/Mac/iOS we use pthreads
add_library(osthreads INTERFACE)
if(CMAKE_SYSTEM_NAME MATCHES "Linux|Darwin|iOS")
    target_link_libraries(osthreads INTERFACE "-lpthread")
endif ()

if(LUAU_BUILD_CLI)
    target_compile_options(Luwu.Repl.CLI PRIVATE ${LUAU_OPTIONS})
    target_compile_options(Luwu.Reduce.CLI PRIVATE ${LUAU_OPTIONS})
    target_compile_options(Luwu.Analyze.CLI PRIVATE ${LUAU_OPTIONS})
    target_compile_options(Luwu.Ast.CLI PRIVATE ${LUAU_OPTIONS})
    target_compile_options(Luwu.Compile.CLI PRIVATE ${LUAU_OPTIONS})
    target_compile_options(Luwu.Bytecode.CLI PRIVATE ${LUAU_OPTIONS})

    target_include_directories(Luwu.Repl.CLI PRIVATE extern extern/isocline/include)

    target_link_libraries(Luwu.Repl.CLI PRIVATE Luwu.Compiler Luwu.Inliner Luwu.Config Luwu.CodeGen Luwu.VM Luwu.Require Luwu.CLI.lib isocline)

    target_link_libraries(Luwu.Repl.CLI PRIVATE osthreads)
    target_link_libraries(Luwu.Reduce.CLI PRIVATE osthreads)
    target_link_libraries(Luwu.Analyze.CLI PRIVATE osthreads)
    target_link_libraries(Luwu.Ast.CLI PRIVATE osthreads)
    target_link_libraries(Luwu.Compile.CLI PRIVATE osthreads)
    target_link_libraries(Luwu.Bytecode.CLI PRIVATE osthreads)

    target_link_libraries(Luwu.Analyze.CLI PRIVATE Luwu.Analysis Luwu.CLI.lib Luwu.Require)

    target_link_libraries(Luwu.Ast.CLI PRIVATE Luwu.Ast Luwu.Analysis Luwu.CLI.lib)

    target_compile_features(Luwu.Reduce.CLI PRIVATE cxx_std_17)
    target_include_directories(Luwu.Reduce.CLI PUBLIC Reduce/include)
    target_link_libraries(Luwu.Reduce.CLI PRIVATE Luwu.Common Luwu.Ast Luwu.Analysis Luwu.CLI.lib)

    target_link_libraries(Luwu.Compile.CLI PRIVATE Luwu.Compiler Luwu.VM Luwu.CodeGen Luwu.CLI.lib)

    target_link_libraries(Luwu.Bytecode.CLI PRIVATE Luwu.Compiler Luwu.VM Luwu.CodeGen Luwu.CLI.lib)
endif()

if(LUAU_BUILD_TESTS)
    target_compile_options(Luwu.UnitTest PRIVATE ${LUAU_OPTIONS})
    target_compile_definitions(Luwu.UnitTest PRIVATE DOCTEST_CONFIG_DOUBLE_STRINGIFY DOCTEST_CONFIG_USE_STD_HEADERS)
    target_include_directories(Luwu.UnitTest PRIVATE extern)
    target_link_libraries(Luwu.UnitTest PRIVATE Luwu.Analysis Luwu.Bytecode Luwu.Compiler Luwu.CodeGen)

    target_compile_options(Luwu.Conformance PRIVATE ${LUAU_OPTIONS})
    target_compile_definitions(Luwu.Conformance PRIVATE DOCTEST_CONFIG_DOUBLE_STRINGIFY DOCTEST_CONFIG_USE_STD_HEADERS)
    target_include_directories(Luwu.Conformance PRIVATE extern VM/src)
    target_link_libraries(Luwu.Conformance PRIVATE Luwu.Analysis Luwu.Bytecode Luwu.Inliner Luwu.Compiler Luwu.CodeGen Luwu.VM)
    if(CMAKE_SYSTEM_NAME MATCHES "Android|iOS")
        set(LUAU_CONFORMANCE_SOURCE_DIR "Client/Luau/tests/conformance")
    else ()
        file(REAL_PATH "tests/conformance" LUAU_CONFORMANCE_SOURCE_DIR)
    endif ()
    target_compile_definitions(Luwu.Conformance PRIVATE LUAU_CONFORMANCE_SOURCE_DIR="${LUAU_CONFORMANCE_SOURCE_DIR}")

    # Luwu Classes: the ClassesFuzz conformance case drives tests/classes_fuzz (a seal script) against the luau CLI
    if(TARGET Luwu.Repl.CLI AND NOT CMAKE_SYSTEM_NAME MATCHES "Android|iOS")
        file(REAL_PATH "tests/classes_fuzz" LUWU_CLASSES_FUZZ_DIR)
        target_compile_definitions(Luwu.Conformance PRIVATE LUWU_CLASSES_FUZZ_DIR="${LUWU_CLASSES_FUZZ_DIR}" LUWU_REPL_CLI_PATH="$<TARGET_FILE:Luwu.Repl.CLI>")
        add_dependencies(Luwu.Conformance Luwu.Repl.CLI)
    endif()

    target_compile_options(Luwu.CLI.Test PRIVATE ${LUAU_OPTIONS})
    target_compile_definitions(Luwu.CLI.Test PRIVATE DOCTEST_CONFIG_USE_STD_HEADERS)
    target_include_directories(Luwu.CLI.Test PRIVATE extern CLI)
    target_link_libraries(Luwu.CLI.Test PRIVATE Luwu.Compiler Luwu.Inliner Luwu.Config Luwu.CodeGen Luwu.VM Luwu.Require Luwu.CLI.lib isocline)
    target_link_libraries(Luwu.CLI.Test PRIVATE osthreads)

    if(LUAU_BUILD_FUZZ)
        add_subdirectory(fuzz)
    endif()
endif()

if(LUAU_BUILD_WEB)
    target_compile_options(Luwu.Web PRIVATE ${LUAU_OPTIONS})
    target_link_libraries(Luwu.Web PRIVATE Luwu.Compiler Luwu.VM Luwu.Analysis)

    # declare exported functions to emscripten
    target_link_options(Luwu.Web PRIVATE -sEXPORTED_FUNCTIONS=["_executeScript","_checkScript"] -sEXPORTED_RUNTIME_METHODS=["ccall","cwrap"])

    # add -fexceptions for emscripten to allow exceptions to be caught in C++
    target_link_options(Luwu.Web PRIVATE -fexceptions)

    # the output is a single .js file with an embedded wasm blob
    target_link_options(Luwu.Web PRIVATE -sSINGLE_FILE=1)
endif()

# validate dependencies for internal libraries
foreach(LIB Luwu.Ast Luwu.Compiler Luwu.Config Luwu.Analysis Luwu.CodeGen Luwu.VM)
    if(TARGET ${LIB})
        get_target_property(DEPENDS ${LIB} LINK_LIBRARIES)
        if(LIB MATCHES "CodeGen|VM" AND DEPENDS MATCHES "Ast|Analysis|Config|Compiler")
            message(FATAL_ERROR ${LIB} " is a runtime component but it depends on one of the offline components")
        endif()
        if(LIB MATCHES "Ast|Compiler" AND DEPENDS MATCHES "CodeGen|VM")
            message(FATAL_ERROR ${LIB} " is an offline component but it depends on one of the runtime components")
        endif()
        if(LIB MATCHES "Ast|Compiler" AND DEPENDS MATCHES "Analysis|Config")
            message(FATAL_ERROR ${LIB} " is a compiler component but it depends on one of the analysis components")
        endif()
    endif()
endforeach()
