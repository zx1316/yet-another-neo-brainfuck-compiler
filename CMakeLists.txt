cmake_minimum_required(VERSION 3.16)
project(YetAnotherNeoBrainfuckCompiler)

set(CMAKE_CXX_STANDARD 23)

add_definitions(-D_HAS_EXCEPTIONS=0)
add_definitions(-DDEFAULT_MEM_SIZE=80000000)
#add_definitions(-DINTERPRETER_CHECK)

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    add_definitions(-DHOST_OS_WIN)
elseif (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    add_definitions(-DHOST_OS_MAC)
else ()
    add_definitions(-DHOST_OS_LINUX)
endif ()

if (MSVC)
    add_compile_options(/GR-)
    if (CMAKE_BUILD_TYPE MATCHES "Release")
        add_compile_options(/GS- /GL /Gw)
        add_link_options(/LTCG:INCREMENTAL)
    endif ()
    string(REPLACE "/EHsc" "/EHs-c-" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
else ()
    add_compile_options(-fno-rtti -fno-exceptions)
    if (CMAKE_BUILD_TYPE MATCHES "Release")
        add_compile_options(-fno-stack-protector -flto)
    endif ()
endif ()

add_executable(bf-compiler main.cpp
        option.h
        option.cpp
        parser/neo_parser.h
        parser/neo_parser.cpp
        optimizer/optimization_level.h
        optimizer/quadruple.cpp
        optimizer/quadruple.h
        optimizer/mid_basic_block.cpp
        optimizer/mid_basic_block.h
        optimizer/ptr_state.h
        optimizer/code_optimizer.cpp
        optimizer/code_optimizer.h
        optimizer/var_value_info.h
        optimizer/block_live_var_info.h
        generator/windows_pe_plus_generator.cpp
        generator/windows_pe_plus_generator.h
        generator/x64_asm.h
        generator/x64_basic_block.cpp
        generator/x64_basic_block.h
        generator/x64_encoding_ctx.cpp
        generator/x64_encoding_ctx.h
        interpreter/interpreter.cpp
        interpreter/interpreter.h
        error.h
)
