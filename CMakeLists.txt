cmake_minimum_required(VERSION 4.2)
project(ef_vi_tcp)

set(CMAKE_CXX_STANDARD 23)

add_compile_options(-fno-strict-aliasing)

set(CMAKE_CXX_FLAGS_RELEASE "-O3 -g -march=native -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-g -DDEBUG")

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)

set(CORE_SRC
        src/tcp/socket.cpp
        src/soup/Session.cpp
        src/ouch/Application.cpp
)

set(ONLOAD_ROOT "" CACHE PATH "onload tree root (optional; auto-detected if empty)")

set(_onload_hints
        ${ONLOAD_ROOT}
        $ENV{HOME}/hknw/onload
        $ENV{HOME}/src/onload
        ${CMAKE_SOURCE_DIR}/../onload
)

find_path(ONLOAD_INCLUDE
        NAMES etherfabric/ef_vi.h
        HINTS ${_onload_hints}
        PATH_SUFFIXES src/include
        PATHS /usr/include)

find_library(CIUL
        NAMES ciul1 ciul
        HINTS ${_onload_hints}
        PATH_SUFFIXES build/gnu_x86_64/lib/ciul
        PATHS /usr/lib64 /usr/lib)

if(NOT ONLOAD_INCLUDE OR NOT CIUL)
    message(FATAL_ERROR
            "onload not found (ONLOAD_INCLUDE=${ONLOAD_INCLUDE}, CIUL=${CIUL}). "
            "Pass -DONLOAD_ROOT=/path/to/onload (or -DCIUL=/abs/path/libciul1.a).")
endif()
message(STATUS "onload include: ${ONLOAD_INCLUDE}")
message(STATUS "onload ciul:    ${CIUL}")


# ---- build a _dma and a _ctpio variant of one ef_vi-backed executable ----
function(add_efvi_variants name)
    cmake_parse_arguments(A "" "INTERFACE;PASSIVE" "SOURCES;DEFS" ${ARGN})
    foreach(variant dma ctpio)
        if(variant STREQUAL "ctpio")
            set(ctpio 1)
        else()
            set(ctpio 0)
        endif()
        set(tgt ${name}_${variant})
        add_executable(${tgt} ${A_SOURCES})
        target_link_libraries(${tgt} PRIVATE ${CIUL})
        target_include_directories(${tgt} PRIVATE include ${ONLOAD_INCLUDE})
        target_compile_definitions(${tgt} PRIVATE
                EFVI_TCP_USE_CTPIO=${ctpio}
                ENABLE_PASSIVE_OPEN=${A_PASSIVE}
                EFVI_TCP_INTERFACE="${A_INTERFACE}"
                ${A_DEFS})
    endforeach()
endfunction()


# ----------------------------------------------------------------------------------------------
# production
add_efvi_variants(ef_vi_tcp
        SOURCES src/main.cpp ${CORE_SRC} src/io/backend_efvi.cpp
        INTERFACE "enp1s0f0" PASSIVE false)
# ----------------------------------------------------------------------------------------------


# ----------------------------------------------------------------------------------------------
# bench: active initiator on enp1s0f0, active connect only
add_efvi_variants(bench_initiate_peer
        SOURCES src/bench/bench_initiate_peer.cpp src/tcp/socket.cpp src/io/backend_efvi.cpp
        INTERFACE "enp1s0f0" PASSIVE false)
# ----------------------------------------------------------------------------------------------


# ----------------------------------------------------------------------------------------------
# bench: passive peer on enp1s0f1
add_efvi_variants(bench_listen_peer
        SOURCES src/bench/bench_listen_peer.cpp src/tcp/socket.cpp src/io/backend_efvi.cpp
        INTERFACE "enp1s0f1" PASSIVE true  DEFS TCP_BENCH_HOOKS)
# ----------------------------------------------------------------------------------------------


# ----------------------------------------------------------------------------------------------
# bench: bulk ordered/unordered send, initiator on enp1s0f0 (peer: bench_listen_peer)
add_efvi_variants(bench_ordered_send
        SOURCES src/bench/bench_ordered_send.cpp src/tcp/socket.cpp src/io/backend_efvi.cpp
        INTERFACE "enp1s0f0" PASSIVE false)

add_efvi_variants(bench_unordered_send
        SOURCES src/bench/bench_unordered_send.cpp src/tcp/socket.cpp src/io/backend_efvi.cpp
        INTERFACE "enp1s0f0" PASSIVE false DEFS TCP_BENCH_HOOKS)
# ----------------------------------------------------------------------------------------------


# ----------------------------------------------------------------------------------------------
# tests: fake NIC, no hardware, no libciul
add_executable(tcp_tests
        ${CORE_SRC}
        src/io/backend_test.cpp
        src/tests/tcp_tests.cpp
        src/tests/soup_tests.cpp
        src/tests/main.cpp
        src/tests/engine_tests.cpp
)
target_include_directories(tcp_tests PRIVATE include ${ONLOAD_INCLUDE})
target_compile_definitions(tcp_tests PRIVATE
        TCP_TEST_HOOKS
        EFVI_TCP_USE_CTPIO=0
        ENABLE_PASSIVE_OPEN=false
        EFVI_TCP_INTERFACE="test0")   # fake backend never opens it, macro just needs to exist
# ----------------------------------------------------------------------------------------------
