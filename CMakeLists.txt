cmake_minimum_required(VERSION 4.2)
project(ef_vi_tcp)

set(CMAKE_CXX_STANDARD 23)

add_executable(
        ef_vi_tcp
        src/main.cpp
        src/SfTcpSocket.cpp
        src/SfUdpSocket.cpp
        src/RxBuffer.cpp
        src/TxBuffer.cpp
        src/sgl.cpp
        src/SoupBinTcpSession.cpp
        src/OuchApplication.cpp
)

target_link_libraries(ef_vi_tcp PRIVATE
        /home/shield_test/hkn/onload/build/gnu_x86_64/lib/ciul/libciul1.a
)

target_include_directories(ef_vi_tcp PRIVATE
        include
        /home/shield_test/hkn/onload/src/include
)
