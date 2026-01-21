cmake_minimum_required(VERSION 3.16)
project(chimera_causal_lab)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include core Chimera headers
include_directories(
    ${CMAKE_SOURCE_DIR}/../core/include
    include
)

# Causal Lab Library
add_library(causal_lab
    src/EventBus.cpp
    src/EventTypes.cpp
    src/ReplayEngine.cpp
    src/ShadowFarm.cpp
    src/AttributionEngine.cpp
    src/RegimeStore.cpp
)

# Replay Tool
add_executable(chimera_replay tools/replay_main.cpp)
target_link_libraries(chimera_replay causal_lab)

# Attribution Tool
add_executable(chimera_attrib tools/attrib_main.cpp)
target_link_libraries(chimera_attrib causal_lab)

# Batch Analysis Tool
add_executable(chimera_batch tools/batch_main.cpp)
target_link_libraries(chimera_batch causal_lab)
