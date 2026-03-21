cmake_minimum_required(VERSION 3.10)
project(OnlineStorageServer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows specific
if(WIN32)
    add_definitions(-D_WINSOCK_DEPRECATED_NO_WARNINGS)
    link_libraries(ws2_32)
endif()

# MySQL library
set(MYSQL_INCLUDE_DIR "C:/Program Files/MySQL/MySQL Server 8.0/include")
set(MYSQL_LIBRARY_DIR "C:/Program Files/MySQL/MySQL Server 8.0/lib")

find_library(MYSQL_LIBRARY mysql PATHS ${MYSQL_LIBRARY_DIR})

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/Server
    ${MYSQL_INCLUDE_DIR}
)

set(SOURCE_FILES
    Server/main.cpp
    Server/netWork/tcpnet.cpp
    Server/CMySQL/cmysql.cpp
)

add_executable(Server ${SOURCE_FILES})

if(MYSQL_LIBRARY)
    target_link_libraries(Server ${MYSQL_LIBRARY})
endif()
