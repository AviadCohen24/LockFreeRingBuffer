cmake_minimum_required(VERSION 3.10)

project(LockFreeRingBuffer VERSION 1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

# Header-only library — no sources to compile, just expose the include path.
# Consumers link against ring_buffer and get the header + thread support.
add_library(ring_buffer INTERFACE)
target_include_directories(ring_buffer INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ring_buffer INTERFACE Threads::Threads)

# Mutex comparison library (still a compiled static lib)
add_library(buffer_mutex STATIC buffer_mutex.c)
target_include_directories(buffer_mutex PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(buffer_mutex PUBLIC Threads::Threads)

add_executable(RingBufferMain main.c)
target_link_libraries(RingBufferMain PRIVATE ring_buffer)

enable_testing()
add_executable(RingBufferTest tests/test_buffer.c)
target_link_libraries(RingBufferTest PRIVATE ring_buffer buffer_mutex)
add_test(NAME BufferTests COMMAND RingBufferTest)
