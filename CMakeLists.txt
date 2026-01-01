cmake_minimum_required(VERSION 3.16)
project(Chimera VERSION 4.4.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(-Wall -Wextra -O2)

find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

option(ENABLE_ML_INFERENCE "Enable ONNX Runtime ML inference" ON)

if(ENABLE_ML_INFERENCE)
    find_library(ONNXRUNTIME_LIBRARY onnxruntime)
    if(ONNXRUNTIME_LIBRARY)
        add_definitions(-DONNX_RUNTIME_AVAILABLE)
    endif()
endif()

add_executable(crypto_test src/crypto_test.cpp)
target_include_directories(crypto_test PRIVATE 
    ${CMAKE_SOURCE_DIR}/crypto_engine/include
    ${CMAKE_SOURCE_DIR}/include/shared
    ${CMAKE_SOURCE_DIR}/include/ml
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(crypto_test PRIVATE OpenSSL::SSL OpenSSL::Crypto Threads::Threads)

add_executable(cfd_test src/cfd_test.cpp)
add_executable(fix_diag src/fix_diag.cpp)
target_include_directories(cfd_test PRIVATE 
    ${CMAKE_SOURCE_DIR}/cfd_engine/include
    ${CMAKE_SOURCE_DIR}/include/shared
    ${CMAKE_SOURCE_DIR}/include/ml
    ${CMAKE_SOURCE_DIR}/include
)
target_include_directories(fix_diag PRIVATE 
    ${CMAKE_SOURCE_DIR}/cfd_engine/include
    ${CMAKE_SOURCE_DIR}/include/shared
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(cfd_test PRIVATE OpenSSL::SSL OpenSSL::Crypto Threads::Threads)
target_link_libraries(fix_diag PRIVATE OpenSSL::SSL OpenSSL::Crypto Threads::Threads)

# ═══════════════════════════════════════════════════════════════════════════════
# CHIMERA (Triple Engine with Income Engine)
# ═══════════════════════════════════════════════════════════════════════════════
add_executable(chimera src/main_triple.cpp)
target_include_directories(chimera PRIVATE 
    ${CMAKE_SOURCE_DIR}/crypto_engine/include
    ${CMAKE_SOURCE_DIR}/cfd_engine/include
    ${CMAKE_SOURCE_DIR}/income_engine/include
    ${CMAKE_SOURCE_DIR}/include/shared
    ${CMAKE_SOURCE_DIR}/include/ml
    ${CMAKE_SOURCE_DIR}/include/risk
    ${CMAKE_SOURCE_DIR}/include/metrics
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(chimera PRIVATE OpenSSL::SSL OpenSSL::Crypto Threads::Threads)

if(ENABLE_ML_INFERENCE AND ONNXRUNTIME_LIBRARY)
    target_link_libraries(chimera PRIVATE ${ONNXRUNTIME_LIBRARY})
    target_link_libraries(cfd_test PRIVATE ${ONNXRUNTIME_LIBRARY})
endif()

if(WIN32)
    target_link_libraries(chimera PRIVATE ws2_32)
    target_link_libraries(crypto_test PRIVATE ws2_32)
    target_link_libraries(cfd_test PRIVATE ws2_32)
    target_link_libraries(fix_diag PRIVATE ws2_32)
endif()

message(STATUS "Chimera v4.6.0 - Triple Engine Architecture")
message(STATUS "  Target: chimera (with Income Engine)")
message(STATUS "  ML Inference: ${ENABLE_ML_INFERENCE}")
