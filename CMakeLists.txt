cmake_minimum_required(VERSION 3.16)

project(SipProxy)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(SipProxy
    src/Main.cpp
    src/SipMessage.cpp
)

target_include_directories(SipProxy PRIVATE
    /usr/local/include
)

target_link_directories(SipProxy PRIVATE
    /usr/local/lib
)

target_link_libraries(SipProxy PRIVATE
    resip
    rutil
    pthread
)