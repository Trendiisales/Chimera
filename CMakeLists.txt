cmake_minimum_required(VERSION 3.20)
project(chimera LANGUAGES CXX)

# ============================================================
# INSTITUTIONAL BUILD CONFIG
# ============================================================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Warnings as errors — you want discipline
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Werror -Wpedantic)
endif()

message(STATUS "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━")
message(STATUS "CHIMERA INSTITUTIONAL BUILD SYSTEM")

# ============================================================
# DEPENDENCIES
# ============================================================
find_package(Threads REQUIRED)
find_package(Boost REQUIRED COMPONENTS json)
find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)

# ============================================================
# INCLUDE ROOTS
# ============================================================
set(CHIMERA_PUBLIC_INCLUDE
  ${PROJECT_SOURCE_DIR}/core/include
)

include_directories(
  ${CHIMERA_PUBLIC_INCLUDE}
)

# ============================================================
# SOURCE DISCOVERY (HYBRID MODE)
# ============================================================
# This ends missing .cpp / linker hell forever
file(GLOB_RECURSE CHIMERA_CORE_SRC
  CONFIGURE_DEPENDS
  ${PROJECT_SOURCE_DIR}/core/src/*.cpp
)

set(CHIMERA_TOPLEVEL_SRC
  ${PROJECT_SOURCE_DIR}/main.cpp
  ${PROJECT_SOURCE_DIR}/dashboard_embed.cpp
)

# ============================================================
# ARCHITECTURE ENFORCEMENT
# ============================================================
function(chimera_enforce_public_headers)
  message(STATUS "CHIMERA: Enforcing public header discipline")

  foreach(src ${CHIMERA_CORE_SRC} ${CHIMERA_TOPLEVEL_SRC})
    if (EXISTS ${src})
      file(READ ${src} CONTENTS)

      string(REGEX MATCHALL "#include \"chimera/([^\"]+)\"" HITS "${CONTENTS}")

      foreach(hit ${HITS})
        string(REGEX REPLACE ".*chimera/([^\"]+).*" "\\1" HDR "${hit}")
        set(HDR_PATH "${PROJECT_SOURCE_DIR}/core/include/chimera/${HDR}")

        if (NOT EXISTS "${HDR_PATH}")
          message(FATAL_ERROR
            "\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n"
            "CHIMERA BUILD VIOLATION\n"
            "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n"
            "Source file:\n  ${src}\n\n"
            "Includes:\n  chimera/${HDR}\n\n"
            "But this file DOES NOT EXIST at:\n"
            "  core/include/chimera/${HDR}\n\n"
            "RULE:\n"
            "  Public headers MUST live in core/include/chimera/\n\n"
            "FIX:\n"
            "  Move the header into core/include/chimera/\n"
            "  OR change the include to a private path\n"
            "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n"
          )
        endif()
      endforeach()
    endif()
  endforeach()
endfunction()

chimera_enforce_public_headers()

# ============================================================
# BUILD TARGET
# ============================================================
add_executable(chimera
  ${CHIMERA_CORE_SRC}
  ${CHIMERA_TOPLEVEL_SRC}
)

target_link_libraries(chimera
  PRIVATE
    Threads::Threads
    Boost::json
    OpenSSL::SSL
    OpenSSL::Crypto
    CURL::libcurl
)

# ============================================================
# PREFLIGHT REPORT
# ============================================================
list(LENGTH CHIMERA_CORE_SRC SRC_COUNT)
message(STATUS "CHIMERA: Sources found: ${SRC_COUNT}")
message(STATUS "CHIMERA: Public include root: ${CHIMERA_PUBLIC_INCLUDE}")
message(STATUS "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━")

