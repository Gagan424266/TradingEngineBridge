cmake_minimum_required(VERSION 3.14)
project(WebServer VERSION 1.0.0 LANGUAGES CXX)

# Include gmDatabaseManager
include_directories(gmDatabaseManager/include/)
include_directories(gmDatabaseManager/SQLAPI_new/include/)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find system libraries
find_package(PkgConfig REQUIRED)
pkg_check_modules(MHD REQUIRED libmicrohttpd)

# gmDatabaseManager subdirectory
add_subdirectory(gmDatabaseManager)
target_include_directories(gmDatabaseManager PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/gmDatabaseManager/include
    ${CMAKE_CURRENT_SOURCE_DIR}/gmDatabaseManager/SQLAPI_new/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../CMS/CMS/gmExternal/gmHeaders
    ${CMAKE_CURRENT_SOURCE_DIR}/../CMS/CMS/gmCore/gmNetworks/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../CMS/CMS/gmCore/gmUtilities/include
)

# Source files
set(SOURCES
    src/main.cpp
    src/ConfigParser.cpp
    src/JsonHandler.cpp
    src/RequestHandler.cpp
    src/SymbolConverter.cpp
    src/TcpClient.cpp
    src/WebServer.cpp
    src/Logger.cpp
)

# Header files
set(HEADERS
    include/ConfigParser.hpp
    include/JsonHandler.hpp
    include/RequestHandler.hpp
    include/SymbolConverter.hpp
    include/TcpClient.hpp
    include/WebServer.hpp
    include/Logger.h
)

add_executable(webserver ${SOURCES} ${HEADERS})

target_include_directories(webserver 
    PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/gmDatabaseManager/include
    ${MHD_INCLUDE_DIRS}
)

target_link_libraries(webserver PRIVATE ${MHD_LIBRARIES})
target_link_libraries(webserver PRIVATE gmDatabaseManager)
target_link_libraries(webserver PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/gmDatabaseManager/SQLAPI_new/lib/libsqlapi.so
)

# Enable threading
find_package(Threads REQUIRED)
target_link_libraries(webserver PRIVATE Threads::Threads)

# Link filesystem library (required for C++17 std::filesystem)
target_link_libraries(webserver PRIVATE stdc++fs)

