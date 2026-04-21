cmake_minimum_required(VERSION 3.10)

project(LockFreeRingBuffer VERSION 1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

add_library(buffer STATIC buffer.c)
target_include_directories(buffer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(buffer PUBLIC Threads::Threads)

add_executable(RingBufferMain main.c)
target_link_libraries(RingBufferMain PRIVATE buffer)

enable_testing()
add_executable(RingBufferTest tests/test_buffer.c)
target_link_libraries(RingBufferTest PRIVATE buffer)
add_test(NAME BufferTests COMMAND RingBufferTest)
