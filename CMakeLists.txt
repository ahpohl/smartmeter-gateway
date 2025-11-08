cmake_minimum_required(VERSION 3.20)

# --- Version from git tag ---
find_package(Git QUIET)
if(Git_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" describe --tags --abbrev=0
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE GIT_TAG
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _tag_rc
        ERROR_QUIET
    )
endif()

if(NOT _tag_rc EQUAL 0 OR GIT_TAG STREQUAL "")
    set(GIT_TAG "0.0.0")
endif()
string(REGEX REPLACE "^v" "" GIT_TAG_STRIPPED "${GIT_TAG}")

# Set some basic project attributes
project (fronius-bridge
	DESCRIPTION "Protocol gateway for domestic meter telemetry"
	VERSION ${GIT_TAG_STRIPPED}
	LANGUAGES CXX
)

include(GNUInstallDirs)

# --- Git commit hash (optional) ---
if(Git_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE GIT_COMMIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _hash_rc
        ERROR_QUIET
    )
endif()

if(NOT _hash_rc EQUAL 0 OR GIT_COMMIT_HASH STREQUAL "")
    set(GIT_COMMIT_HASH "unknown")
endif()

# Create a simple configuration header
configure_file(config.h.in config.h)

# --- Build type (only for single-config generators) ---
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# --- Language/standard ---
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Dependencies ---
find_package(fronius CONFIG REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(spdlog REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(CLI11 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(MOSQUITTO REQUIRED IMPORTED_TARGET libmosquitto)

# --- Sources ---
set(SRC_FILES
    src/main.cpp
    #src/modbus_slave.cpp
    src/config_yaml.cpp
    src/mqtt_client.cpp
)

# --- Executable ---
add_executable(${PROJECT_NAME} ${SRC_FILES})

target_include_directories(${PROJECT_NAME} PRIVATE 
    ${CMAKE_CURRENT_BINARY_DIR}    # for generated config.h
    ${PROJECT_SOURCE_DIR}/include  # project's public headers
)
 
target_link_libraries(${PROJECT_NAME} PRIVATE
    fronius::shared
    yaml-cpp
    spdlog::spdlog
    PkgConfig::MOSQUITTO
    nlohmann_json::nlohmann_json
    CLI11::CLI11
)

# --- Install (so CPack has something to package) ---
install(TARGETS ${PROJECT_NAME}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# --- CPack (components) ---
include(InstallRequiredSystemLibraries)

# Global metadata
set(CPACK_GENERATOR "TGZ;DEB")
set(CPACK_PACKAGE_NAME "${PROJECT_NAME}")
set(CPACK_PACKAGE_CONTACT "Alexander Pohl <alex@ahpohl.com>")
set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})

# DEB naming: canonical names per Debian policy
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SECTION "net")

# TGZ custom archive name
string(TOLOWER "${CMAKE_SYSTEM_NAME}" OS_NAME)
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" ARCH)
set(CPACK_PACKAGE_FILE_NAME "${PROJECT_NAME}_v${PROJECT_VERSION}_${OS_NAME}_${ARCH}")

include(CPack)