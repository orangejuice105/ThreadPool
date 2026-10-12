cmake_minimum_required(VERSION 3.13)
project(ThreadPoolProject LANGUAGES CXX)

# 使用 C++17，并禁止依赖编译器特有的语言扩展。
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 查找平台所需的线程编译和链接选项。
find_package(Threads REQUIRED)

# 线程池库：模板实现保留在头文件，普通成员函数编译到库中。
add_library(thread_pool src/thread_pool.cpp)
target_include_directories(thread_pool PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(thread_pool PUBLIC Threads::Threads)

# 示例程序链接线程池库，同时获得它的头文件路径和线程依赖。
add_executable(main src/main.cpp)
target_link_libraries(main PRIVATE thread_pool)
