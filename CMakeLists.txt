cmake_minimum_required(VERSION 4.2)
project(ef_vi_tcp)

set(CMAKE_CXX_STANDARD 23)

option(USE_CTPIO "Transmit via CTPIO instead of DMA descriptors" ON)
add_compile_definitions(EFVI_TCP_USE_CTPIO=$<BOOL:${USE_CTPIO}>)

add_compile_options(-fno-strict-aliasing)

set(CMAKE_CXX_FLAGS_RELEASE "-O3 -g -march=native -DNDEBUG")

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





# ----------------------------------------------------------------------------------------------
# production
add_executable(ef_vi_tcp
        src/main.cpp
        ${CORE_SRC}
        src/io/backend_efvi.cpp
)
target_link_libraries(ef_vi_tcp PRIVATE ${CIUL})
target_include_directories(ef_vi_tcp PRIVATE include ${ONLOAD_INCLUDE})
target_compile_definitions(ef_vi_tcp PRIVATE
        ENABLE_PASSIVE_OPEN=false
        EFVI_TCP_INTERFACE="enp1s0f0")
# ----------------------------------------------------------------------------------------------






# ----------------------------------------------------------------------------------------------
# bench: active initiator on enp1s0f0, active connect only
add_executable(bench_initiate_peer
        src/bench/bench_initiate_peer.cpp
        src/tcp/socket.cpp
        src/io/backend_efvi.cpp
)
target_link_libraries(bench_initiate_peer PRIVATE ${CIUL})
target_include_directories(bench_initiate_peer PRIVATE include ${ONLOAD_INCLUDE})
target_compile_definitions(bench_initiate_peer PRIVATE
        ENABLE_PASSIVE_OPEN=false
        EFVI_TCP_INTERFACE="enp1s0f0")
# ----------------------------------------------------------------------------------------------






# ----------------------------------------------------------------------------------------------
# bench: passive peer on enp1s0f1
add_executable(bench_listen_peer
        src/bench/bench_listen_peer.cpp
        src/tcp/socket.cpp
        src/io/backend_efvi.cpp
)
target_link_libraries(bench_listen_peer PRIVATE ${CIUL})
target_include_directories(bench_listen_peer PRIVATE include ${ONLOAD_INCLUDE})
target_compile_definitions(bench_listen_peer PRIVATE
        ENABLE_PASSIVE_OPEN=true
        EFVI_TCP_INTERFACE="enp1s0f1")
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
        ENABLE_PASSIVE_OPEN=false
        EFVI_TCP_INTERFACE="test0")   # fake backend never opens it, macro just needs to exist
# ----------------------------------------------------------------------------------------------





# ----------------------------------------------------------------------------------------------
# bench: bulk ordered/unordered send, initiator on enp1s0f0 (peer: bench_listen_peer)
foreach(bench bench_ordered_send bench_unordered_send)
    add_executable(${bench}
            src/tests/${bench}.cpp
            src/tcp/socket.cpp
            src/io/backend_efvi.cpp
            src/bench/bench_config.h
            src/bench/bench_config.h
    )
    target_link_libraries(${bench} PRIVATE ${CIUL})
    target_include_directories(${bench} PRIVATE include ${ONLOAD_INCLUDE})
    target_compile_definitions(${bench} PRIVATE
            ENABLE_PASSIVE_OPEN=false
            EFVI_TCP_INTERFACE="enp1s0f0")
endforeach()
# ----------------------------------------------------------------------------------------------



