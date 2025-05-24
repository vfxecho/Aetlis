cmake_minimum_required(VERSION 3.13.0 FATAL_ERROR)

project(AetlisServer CXX C)

################################################################################
# Source groups
################################################################################
set(Header_Files
    "Aetlis/src/bots/PlayerBot.h"
    "Aetlis/src/cells/Cell.h"
    "Aetlis/src/commands/CommandList.h"
    "Aetlis/src/gamemodes/FFA.h"
    "Aetlis/src/gamemodes/Gamemode.h"
    "Aetlis/src/gamemodes/GamemodeList.h"
    "Aetlis/src/misc/Misc.h"
    "Aetlis/src/misc/Stopwatch.h"
    "Aetlis/src/misc/Ticker.h"
    "Aetlis/src/primitives/Logger.h"
    "Aetlis/src/primitives/QuadTree.h"
    "Aetlis/src/primitives/Reader.h"
    "Aetlis/src/primitives/Rect.h"
    "Aetlis/src/primitives/SimplePool.h"
    "Aetlis/src/primitives/Writer.h"
    "Aetlis/src/protocols/Protocol.h"
    "Aetlis/src/protocols/Protocol6.h"
    "Aetlis/src/protocols/ProtocolModern.h"
    "Aetlis/src/protocols/ProtocolStore.h"
    "Aetlis/src/protocols/ProtocolVanis.h"
    "Aetlis/src/ServerHandle.h"
    "Aetlis/src/Settings.h"
    "Aetlis/src/sockets/ChatChannel.h"
    "Aetlis/src/sockets/Connection.h"
    "Aetlis/src/sockets/Listener.h"
    "Aetlis/src/sockets/Router.h"
    "Aetlis/src/sockets/DualMinionRouter.h"
    "Aetlis/src/web/AsyncFileReader.h"
    "Aetlis/src/web/AsyncFileStreamer.h"
    "Aetlis/src/web/Middleware.h"
    "Aetlis/src/worlds/MatchMaker.h"
    "Aetlis/src/worlds/Player.h"
    "Aetlis/src/worlds/World.h"
)
source_group("Header Files" FILES ${Header_Files})

set(Source_Files
    "Aetlis/src/bots/PlayerBot.cpp"
    "Aetlis/src/cells/Cell.cpp"
    "Aetlis/src/cli/Main.cpp"
    "Aetlis/src/gamemodes/FFA.cpp"
    "Aetlis/src/gamemodes/Gamemode.cpp"
    "Aetlis/src/gamemodes/GamemodeList.cpp"
    "Aetlis/src/primitives/QuadTree.cpp"
    "Aetlis/src/primitives/SimplePool.cpp"
    "Aetlis/src/protocols/Protocol6.cpp"
    "Aetlis/src/protocols/ProtocolModern.cpp"
    "Aetlis/src/protocols/ProtocolVanis.cpp"
    "Aetlis/src/ServerHandle.cpp"
    "Aetlis/src/sockets/ChatChannel.cpp"
    "Aetlis/src/sockets/Connection.cpp"
    "Aetlis/src/sockets/Listener.cpp"
    "Aetlis/src/sockets/Router.cpp"
    "Aetlis/src/sockets/DualMinionRouter.cpp"
    "Aetlis/src/worlds/MatchMaker.cpp"
    "Aetlis/src/worlds/Player.cpp"
    "Aetlis/src/worlds/World.cpp"
)
source_group("Source Files" FILES ${Source_Files})

set(CMAKE_CXX_STANDARD 17)

# Windows ile uyumlu derleyici bayrakları
if(WIN32)
    set(CMAKE_CXX_FLAGS "/O2 /EHsc /W4")
    set(CMAKE_C_FLAGS "/O2 /W4")
else()
    set(CMAKE_CXX_FLAGS "-O3")
    set(CMAKE_C_FLAGS "-O3")
endif()

set(ALL_FILES
    ${Header_Files}
    ${Source_Files}
)

# Create an include directory for uwebsockets and copy header files
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/include/uwebsockets")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/uwebsockets")
    file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/uWebSockets-0.17.0/src/" 
         DESTINATION "${CMAKE_CURRENT_SOURCE_DIR}/include/uwebsockets"
         FILES_MATCHING PATTERN "*.h")
endif()

# First build uSockets as a library
# Explicitly define uSockets source files instead of using GLOB
set(USOCKETS_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/uWebSockets-0.17.0/uSockets/src/context.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/uWebSockets-0.17.0/uSockets/src/loop.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/uWebSockets-0.17.0/uSockets/src/socket.c"
)

# Add proper eventing based on platform
if(WIN32)
    list(APPEND USOCKETS_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/uWebSockets-0.17.0/uSockets/src/eventing/libuv.c")
    # Windows için gerekli ek tanımlamalar
    add_definitions(-DLIBUS_USE_LIBUV)
else()
    list(APPEND USOCKETS_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/uWebSockets-0.17.0/uSockets/src/eventing/epoll_kqueue.c")
    # Linux için gerekli ek tanımlamalar
    add_definitions(-DLIBUS_USE_EPOLL)
endif()

# Check if files exist
foreach(SRC_FILE ${USOCKETS_SOURCES})
    if(NOT EXISTS ${SRC_FILE})
        message(WARNING "Source file does not exist: ${SRC_FILE}")
    endif()
endforeach()

# Build uSockets as a static library with explicit language
add_library(uSockets STATIC ${USOCKETS_SOURCES})
set_target_properties(uSockets PROPERTIES LINKER_LANGUAGE C)
target_include_directories(uSockets PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/uWebSockets-0.17.0/uSockets/src")
target_compile_definitions(uSockets PRIVATE LIBUS_NO_SSL)

# Make sure to copy libusockets.h to include directory
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/include/libusockets.h")
    file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/uWebSockets-0.17.0/uSockets/src/libusockets.h" 
         DESTINATION "${CMAKE_CURRENT_SOURCE_DIR}/include")
endif()

# Set include paths for the project
include_directories(
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

# Yüklediğimiz uWebSockets-0.17.0 ve vcpkg kütüphaneleri için include ve link path'lerini ayarlayalım
if(WIN32)
    # Önce manuel yüklediğimiz uWebSockets'i include edelim
    include_directories(
        "${CMAKE_CURRENT_SOURCE_DIR}/include/uwebsockets"
        "${CMAKE_CURRENT_SOURCE_DIR}/uWebSockets-0.17.0/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/uWebSockets-0.17.0/uSockets/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/installed/x64-windows/include/"
    )
    
    # Windows için libuv gerekli
    find_package(ZLIB REQUIRED)
    find_path(LIBUV_INCLUDE_DIR NAMES uv.h)
    find_library(LIBUV_LIBRARY NAMES uv libuv)
    
    if(LIBUV_LIBRARY)
        target_include_directories(uSockets PRIVATE ${LIBUV_INCLUDE_DIR})
        target_link_libraries(uSockets ${LIBUV_LIBRARY})
    else()
        # Vcpkg ile yüklenen libuv'yi doğrudan dosya yolundan kullanabiliriz
        target_include_directories(uSockets PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/installed/x64-windows/include")
        target_link_libraries(uSockets "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/installed/x64-windows/lib/libuv.lib")
    endif()
else()
    include_directories(
        "${CMAKE_CURRENT_SOURCE_DIR}/include/uwebsockets"
        "${CMAKE_CURRENT_SOURCE_DIR}/uWebSockets-0.17.0/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/uWebSockets-0.17.0/uSockets/src"
        $ENV{VCPKG_ROOT}/installed/x64-linux/include/
    )
    
    # Linux için z (zlib) gerekli
    find_package(ZLIB REQUIRED)
    target_link_libraries(uSockets ZLIB::ZLIB)
endif()

# Windows'ta -static-libstdc++ ve pthread olmadığından bunları koşullu ekleyelim
if(NOT WIN32)
    set(CMAKE_CXX_STANDARD_LIBRARIES "-static-libstdc++")
endif()

################################################################################
# Target
################################################################################
add_executable(${PROJECT_NAME} ${ALL_FILES})

# Add include directories to main target
target_include_directories(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/uwebsockets"
)

# Windows için bağımlılıkları düzenleyelim
if(WIN32)
    target_link_libraries(${PROJECT_NAME} uSockets ZLIB::ZLIB)
else()
    target_link_libraries(${PROJECT_NAME} pthread uSockets z stdc++fs)
endif()

# Set the output directory for the executable
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

