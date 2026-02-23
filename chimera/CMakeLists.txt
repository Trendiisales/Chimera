cmake_minimum_required(VERSION 3.20)
project(chimera LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB_RECURSE CHIMERA_CORE
  src/*.cpp
  telemetry/*.cpp
)

add_executable(chimera
  src/main.cpp
  
  src/platform/platform_windows.cpp
)

target_include_directories(chimera PRIVATE
  
  /include
  /src
  /include/platform
  /src/platform
  "C:/Program Files/OpenSSL-Win64/include"
)

target_link_libraries(chimera PRIVATE
  "C:/Program Files/OpenSSL-Win64/lib/VC/x64/MD/libssl.lib"
  "C:/Program Files/OpenSSL-Win64/lib/VC/x64/MD/libcrypto.lib"
  ws2_32
)

add_definitions(-DWIN32_LEAN_AND_MEAN)
