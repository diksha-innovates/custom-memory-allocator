cmake_minimum_required(VERSION 3.16)
project(custom_memory_allocator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

option(ENABLE_ASAN "Build with AddressSanitizer + LeakSanitizer" OFF)
option(ENABLE_UBSAN "Build with UndefinedBehaviorSanitizer" OFF)
option(ENABLE_TSAN "Build with ThreadSanitizer" OFF)
option(CMA_BUILD_TESTS "Build the GoogleTest test suite" ON)
option(CMA_BUILD_BENCHMARKS "Build the benchmark CLI" ON)

if(ENABLE_TSAN AND (ENABLE_ASAN OR ENABLE_UBSAN))
  message(FATAL_ERROR "ThreadSanitizer cannot be combined with ASan/UBSan in the same binary")
endif()

add_library(cma_warnings INTERFACE)
target_compile_options(cma_warnings INTERFACE -Wall -Wextra -Wpedantic -Werror)

add_library(cma_sanitizers INTERFACE)
if(ENABLE_ASAN)
  target_compile_options(cma_sanitizers INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer -g)
  target_link_options(cma_sanitizers INTERFACE -fsanitize=address,undefined)
elseif(ENABLE_UBSAN)
  target_compile_options(cma_sanitizers INTERFACE -fsanitize=undefined -fno-omit-frame-pointer -g)
  target_link_options(cma_sanitizers INTERFACE -fsanitize=undefined)
elseif(ENABLE_TSAN)
  target_compile_options(cma_sanitizers INTERFACE -fsanitize=thread -fno-omit-frame-pointer -g)
  target_link_options(cma_sanitizers INTERFACE -fsanitize=thread)
endif()

add_library(cma_allocator STATIC
  src/arena_allocator.cpp
  src/free_list_allocator.cpp
)
target_include_directories(cma_allocator PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(cma_allocator PUBLIC cma_sanitizers)
target_link_libraries(cma_allocator PRIVATE cma_warnings)

if(CMA_BUILD_BENCHMARKS)
  add_executable(cma_benchmark benchmarks/bench_main.cpp)
  target_link_libraries(cma_benchmark PRIVATE cma_allocator cma_warnings)
endif()

if(CMA_BUILD_TESTS)
  enable_testing()
  include(FetchContent)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(googletest)

  add_executable(cma_tests
    tests/unit/test_arena_allocator.cpp
    tests/unit/test_free_list_allocator.cpp
    tests/unit/test_pmr_adapter.cpp
    tests/unit/test_property_bounds.cpp
  )
  target_link_libraries(cma_tests PRIVATE cma_allocator gtest_main cma_warnings)

  include(GoogleTest)
  gtest_discover_tests(cma_tests)
endif()
