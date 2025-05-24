cmake_minimum_required(VERSION 3.13.0 FATAL_ERROR)

project(uSockets CXX C)

set(CMAKE_CXX_STANDARD 17)
if(WIN32)
    set(CMAKE_CXX_FLAGS "/O2 /EHsc /W4")
    set(CMAKE_C_FLAGS "/O2 /W4")
else()
    set(CMAKE_CXX_FLAGS "-O3")
    set(CMAKE_C_FLAGS "-O3")
endif()

file(GLOB USOCKETS_SOURCES 
    "uWebSockets-0.17.3/uSockets/src/*.c"
    "uWebSockets-0.17.3/uSockets/src/eventing/*.c"
    "uWebSockets-0.17.3/uSockets/src/crypto/*.c"
)

if(WIN32)
    file(GLOB USOCKETS_EVENTING_SOURCES "uWebSockets-0.17.3/uSockets/src/eventing/libuv.c")
    list(APPEND USOCKETS_SOURCES ${USOCKETS_EVENTING_SOURCES})
    
    include_directories(
        "uWebSockets-0.17.3/uSockets/src"
        "vcpkg/installed/x64-windows/include"
    )
    
    add_library(uSockets STATIC ${USOCKETS_SOURCES})
    
    # Windows için gerekli ek tanımlamalar
    target_compile_definitions(uSockets PRIVATE LIBUS_USE_LIBUV=1)
    target_link_libraries(uSockets
        ${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/installed/x64-windows/lib/libuv.lib
        ${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/installed/x64-windows/lib/zlib.lib
    )
    
else()
    file(GLOB USOCKETS_EVENTING_SOURCES "uWebSockets-0.17.3/uSockets/src/eventing/epoll.c")
    list(APPEND USOCKETS_SOURCES ${USOCKETS_EVENTING_SOURCES})
    
    include_directories(
        "uWebSockets-0.17.3/uSockets/src"
    )
    
    add_library(uSockets STATIC ${USOCKETS_SOURCES})
    
    # Linux için gerekli ek tanımlamalar
    target_compile_definitions(uSockets PRIVATE LIBUS_USE_EPOLL=1)
    target_link_libraries(uSockets z)
endif()

install(TARGETS uSockets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
) 