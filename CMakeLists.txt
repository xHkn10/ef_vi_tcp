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


target_include_directories(ef_vi_tcp PRIVATE
        include
        ~/src/onload/src/include/etherfabric/
)

target_link_directories(ef_vi_tcp PRIVATE
        ~/src/onload/build/gnu_x86_64/
)

target_link_libraries(ef_vi_tcp PRIVATE ciul1)
