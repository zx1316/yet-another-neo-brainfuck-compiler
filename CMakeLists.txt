cmake_minimum_required(VERSION 3.16)
project(YetAnotherNeoBrainfuckCompiler)

set(CMAKE_CXX_STANDARD 23)

if (MSVC)
    if (CMAKE_BUILD_TYPE MATCHES "Release")
        add_compile_options(/GR- /GS- /GL /Gw)
        add_link_options(/LTCG:INCREMENTAL)
    endif ()
else ()
    if (CMAKE_BUILD_TYPE MATCHES "Release")
        add_compile_options(-fno-rtti -fno-stack-protector -flto)
    endif ()
endif ()

add_executable(bf-compiler main.cpp
        parser/neo_parser.h
        parser/neo_parser.cpp
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
        generator/x64_asm.cpp
        generator/x64_asm.h
        generator/x64_basic_block.cpp
        generator/x64_basic_block.h
        generator/x64_encoding_ctx.cpp
        generator/x64_encoding_ctx.h
        interpreter/interpreter.cpp
        interpreter/interpreter.h
)
