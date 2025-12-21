cmake_minimum_required(VERSION 3.20)
project(Chimera LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(crypto_engine)
add_subdirectory(cfd_engine)

add_executable(chimera
    main.cpp
    src/accounting/PnlLedger.cpp
    src/accounting/DailyPnlStore.cpp
    src/gui/MetricsHttpServer.cpp
    src/latency/LatencyRegistry.cpp
)

target_link_libraries(chimera
    crypto_engine
    cfd_engine
)

target_include_directories(chimera PUBLIC
    include
    src
    crypto_engine/include
)
