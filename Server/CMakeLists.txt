cmake_minimum_required(VERSION 3.20)
project(Server CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra -D_FILE_OFFSET_BITS=64)

find_package(Threads REQUIRED)

# 定位 MySQL 客户端头文件与库；动态库的传递依赖由运行时链接器解析。
find_path(MYSQL_INCLUDE_DIR mysql/mysql.h REQUIRED)
find_library(MYSQL_LIBRARY mysqlclient REQUIRED)

add_executable(Server
    main.cpp
    netWork/epoller.cpp
    netWork/session.cpp
    netWork/reactor.cpp
    CMySQL/cmysql.cpp
    Kernel/kernel.cpp
)

target_include_directories(Server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${MYSQL_INCLUDE_DIR})
target_link_libraries(Server PRIVATE Threads::Threads ${MYSQL_LIBRARY})
