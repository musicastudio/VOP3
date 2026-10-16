# VOP3: the Yamaha YSS236 DSP core, its interpreter and its AsmJit compiler.
#   cmake -B build -S . && cmake --build build --config Release && ctest --test-dir build -C Release
cmake_minimum_required(VERSION 3.20)
project(vop3 LANGUAGES CXX)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_STANDARD 20)   # AsmJit needs it
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# AsmJit, third_party/asmjit, under its own zlib licence
file(GLOB ASMJIT CONFIGURE_DEPENDS third_party/asmjit/asmjit/core/*.cpp third_party/asmjit/asmjit/x86/*.cpp
     third_party/asmjit/asmjit/arm/*.cpp third_party/asmjit/asmjit/axl/*.cpp)
add_library(asmjit STATIC ${ASMJIT})
target_include_directories(asmjit PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/asmjit)
target_compile_definitions(asmjit PUBLIC ASMJIT_STATIC ASMJIT_NO_BUILDER ASMJIT_NO_COMPILER ASMJIT_NO_UJIT ASMJIT_NO_FOREIGN ASMJIT_NO_SHM_OPEN)
find_package(Threads REQUIRED)
target_link_libraries(asmjit PUBLIC Threads::Threads)

# The interpreter and the compiled pass are bit-exact only without FMA contraction.
add_library(vop3 STATIC src/vop3.cpp src/yss236_jit.cpp)
target_include_directories(vop3 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(vop3 PRIVATE asmjit)
if(MSVC)
  target_compile_options(vop3 PRIVATE /W3 /fp:precise /permissive-)
else()
  target_compile_options(vop3 PRIVATE -Wall -ffp-contract=off)
endif()

enable_testing()
add_executable(vop3_jit_check tests/vop3_jit_check.cpp)
target_link_libraries(vop3_jit_check PRIVATE vop3)
target_compile_definitions(vop3_jit_check PRIVATE VOP3_DOCS="${CMAKE_CURRENT_SOURCE_DIR}/docs")
add_test(NAME vop3_jit COMMAND vop3_jit_check)
set_tests_properties(vop3_jit PROPERTIES SKIP_RETURN_CODE 77)
add_test(NAME vop3_disasm COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/vop3_disasm.py)

add_executable(vop3_minimal examples/minimal.cpp)
target_link_libraries(vop3_minimal PRIVATE vop3)
